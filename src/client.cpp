/**

* @file

* @brief UdpClient implementation: paced batch sending toward a target PPS.

*

* @details

* Responsibilities:

*  - Connect to the server and set sane socket buffers for high-rate transmit.

*  - Generate packets with a small header (`PacketHeader`) and send them in batches.

*  - Pace transmission to approximate a target packets-per-second (PPS) rate.

*  - Maintain hot-path counters (`udp::Stats`) without locks.

*

* Concurrency model:

*  - One worker thread created by `start()` runs `run_loop()` until `stop()` or

*    the configured duration elapses.

*  - Public getters (`stats()`) are safe to call from other threads.

*

* Notes:

*  - Pacing is best-effort and depends on OS scheduler granularity. We use

*    `clock_nanosleep(CLOCK_MONOTONIC, ...)` to avoid wall-clock adjustments.

*  - Batch size amortizes syscall overhead (`send_batch` favors `sendmmsg`).

*/
 
#include "udp/client.hpp"

#include <iostream>

#include <thread>

#include <chrono>

#include <arpa/inet.h>

#include <cstring>

#include <sys/time.h>
 
namespace udp {
 
/**

* @brief Construct a UdpClient, connect the socket, and prepare for high-rate TX.

*

* @details

* - Connects to `cfg_.server_ip:cfg_.port` so the socket can use the connected

*   peer without per-send destination.

* - Requests a 1 MiB send buffer (`SO_SNDBUF`) as a reasonable default for bursty

*   traffic.

*

* @param sock Socket strategy injected by the caller (ownership transferred).

* @param cfg  Client configuration (server endpoint, PPS, duration, payload, batch, etc.).

*/

UdpClient::UdpClient(std::unique_ptr<ISocket> sock, ClientConfig cfg)

: sock_(std::move(sock)), cfg_(cfg) {

    sock_->connect(cfg_.server_ip, cfg_.port);

    sock_->set_sndbuf(1<<20);

}
 
/**

* @brief Destructor; ensures the worker thread is stopped and joined.

*/

UdpClient::~UdpClient() { stop(); }
 
/**

* @brief Start the client: spawn the worker thread that performs paced sending.

*/

void UdpClient::start() {

    running_ = true;

    th_ = std::thread(&UdpClient::run_loop, this);

}
 
/**

* @brief Stop the client: request loop exit and join the worker thread (idempotent).

*/

void UdpClient::stop() {

    if (th_.joinable()) {

        running_ = false;

        th_.join();

    }

}
 
/**

* @brief Join the worker thread without forcing an early stop.

*

* @details

* Useful when you want the client to run for `cfg_.seconds` and only return

* after it naturally completes.

*/

void UdpClient::join() {

    // Wait until the worker thread exits naturally (e.g., after --seconds duration)

    if (th_.joinable()) {

        th_.join();

    }

}
 
/**

* @brief Main worker loop: build batches, send, update stats, and pace to target PPS.

*

* @details

* Pacing:

* - `interval_ns = 1e9 / PPS` is the nominal spacing between packets.

* - We send `batch` packets together and then advance the next target timestamp by

*   `interval_ns * batch`, sleeping the difference if we're early.

*

* Payload:

* - Each packet contains a `PacketHeader` at the start: incrementing `seq_`,

*   `send_ts_ns = now_ns()`, and `kMagic` for basic sanity checks.

* - The total payload size is `max(cfg_.payload, sizeof(PacketHeader))`.

*

* Counters:

* - On successful `send_batch`, we increment `sent` by the number of messages and

*   add all payload sizes to `tx_bytes`.

*

* Verbose logging:

* - Once per second (approx), prints cumulative `sent` and `tx_bytes` for this client.

*/

void UdpClient::run_loop() {

    const uint64_t interval_ns = 1'000'000'000ull / (cfg_.pps ? cfg_.pps : 1);

    uint64_t next_ts = now_ns();

    auto start = std::chrono::steady_clock::now();

    auto end = start + std::chrono::seconds(cfg_.seconds);
 
    std::vector<std::vector<uint8_t>> batch;

    batch.reserve(cfg_.batch);
 
    while (running_ && std::chrono::steady_clock::now() < end) {

        // Prepare a batch of packets with header

        batch.clear();

        for (int i=0; i<cfg_.batch; ++i) {

            std::vector<uint8_t> pkt(std::max(cfg_.payload, (int)sizeof(PacketHeader)), 0);

            PacketHeader* hdr = reinterpret_cast<PacketHeader*>(pkt.data());

            hdr->seq = ++seq_;

            hdr->send_ts_ns = now_ns();

            hdr->magic = kMagic;

            batch.push_back(std::move(pkt));

        }

        auto s = sock_->send_batch(batch, nullptr);

        if (s > 0) {

            stats_.inc_sent(s);

            size_t total_bytes = 0; for (auto& b: batch) total_bytes += b.size();

            stats_.add_tx_bytes(total_bytes);

        }
 
        // Pace to target pps

        next_ts += interval_ns * cfg_.batch;

        uint64_t now = now_ns();

        if (next_ts > now) {

            uint64_t sleep_ns = next_ts - now;

            timespec ts{ (time_t)(sleep_ns/1'000'000'000ull), (long)(sleep_ns%1'000'000'000ull) };

            clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, nullptr);

        }
 
        static uint64_t last_print_ns = now_ns();

        if (cfg_.verbose && now - last_print_ns > 1'000'000'000ull) {

            std::cout << "[client " << cfg_.id << "] sent=" << stats_.sent()
<< " tx_bytes=" << stats_.tx_bytes() << "\n";

            last_print_ns = now;

        }

    }

}
 
} // namespace udp

 