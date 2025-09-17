/**

* @file

* @brief UdpServer implementation: batch recv, optional echo, PPS accounting, and /metrics integration.

*

* @details

* Responsibilities:

*  - Bind a UDP socket, configure kernel buffers, and run the main receive loop.

*  - Optionally echo received datagrams back to the sender (when `ServerConfig::echo`).

*  - Track counters via `udp::Stats` (lock-free on the hot path) and compute

*    a 1-second rolling packets-per-second (PPS) rate.

*  - Optionally expose `/metrics` on loopback using `MetricsHttpServer`.

*

* Concurrency model:

*  - One worker thread created by `start()` runs `run_loop()` until `stop()`.

*  - Hot-path counter updates are lock-free; no locks in the receive/send loop.

*

* Notes:

*  - This TU complements the public contract documented in `include/udp/server.hpp`.

*  - Source address tracking: for simplicity we don’t extract per-packet peer

*    addresses here; `Stats::note_client()` can be called if the socket adapter

*    provides source addressing (e.g., via msghdr `msg_name` in `recvmmsg`).

*/
 
#include "udp/server.hpp"

#include <iostream>

#include <cstring>

#include <arpa/inet.h>
 
namespace udp {
 
/**

* @brief Construct a UdpServer, bind the socket, and prepare optional metrics.

*

* @details

* - Binds UDP to `cfg_.port` (with optional `SO_REUSEPORT`).

* - Requests 1 MiB receive/send buffers as a sensible default for high PPS.

* - If `cfg_.metrics_port != 0`, creates a `MetricsHttpServer` instance; it is

*   started in `start()` and stopped in `stop()`.

*

* @param sock Socket strategy injected by the caller (ownership transferred).

* @param cfg  Server configuration (port, batch size, echo, metrics, verbosity).

*/

UdpServer::UdpServer(std::unique_ptr<ISocket> sock, ServerConfig cfg)

: sock_(std::move(sock)), cfg_(cfg) {

    sock_->bind(cfg_.port, cfg_.reuseport);

    sock_->set_rcvbuf(1<<20);

    sock_->set_sndbuf(1<<20);

    if (cfg_.metrics_port) {

        metrics_ = std::make_unique<MetricsHttpServer>(stats_, cfg_.metrics_port);

    }

}
 
/**

* @brief Destructor; ensures the worker thread and metrics are stopped cleanly.

*/

UdpServer::~UdpServer() {

    stop();

}
 
/**

* @brief Start the server: spawn the worker thread and (optionally) metrics server.

*

* @details

* - If metrics were configured, `metrics_->start()` begins the loopback HTTP listener.

* - Sets `running_ = true` and launches `run_loop()` on `th_`.

*/

void UdpServer::start() {

    if (metrics_) metrics_->start();

    running_ = true;

    th_ = std::thread(&UdpServer::run_loop, this);

}
 
/**

* @brief Stop the server: request loop exit and join the thread; stop metrics.

*

* @details

* - Idempotent: if the thread isn’t running, calls are no-ops.

* - Ensures `metrics_` is also stopped so that CI/local runs free the port.

*/

void UdpServer::stop() {

    if (th_.joinable()) {

        running_ = false;

        th_.join();

    }

    if (metrics_) metrics_->stop();

}
 
/**

* @brief Main worker loop: batch receive, optional echo, rate logging, metrics updates.

*

* @details

* Receive & counters:

* - Allocates `cfg_.batch` receive buffers (2048 bytes each) and calls

*   `ISocket::recv_batch(bufs)` repeatedly.

* - For each received message:

*   - Optionally inspects a `PacketHeader` (if the payload contains one) for

*     basic integrity (magic). This example doesn’t extract peer addresses.

*   - Updates hot-path counters: `inc_recv(1)` and `add_rx_bytes(size)`.

*

* Echo:

* - When `cfg_.echo` is true, reuses the received buffers as a batch to send

*   back via `send_batch`. On success, updates `sent` and `tx_bytes` counters.

*

* PPS computation:

* - Every ~1s, computes delta of `stats_.recv()` since the last tick and

*   stores it in `last_rate_pps_`. If `cfg_.verbose`, prints a single-line

*   status: `stats_.to_string()` + humanized rate (pps/kpps/Mpps).

*

* Error handling:

* - If `recv_batch` returns < 0, the loop continues (best-effort behavior).

* - Non-blocking sockets may frequently return 0 (no messages available).

*/

void UdpServer::run_loop() {

    std::vector<std::vector<uint8_t>> bufs(cfg_.batch, std::vector<uint8_t>(2048));

    uint64_t last_recv_total = 0;

    auto last_ts = std::chrono::steady_clock::now();

    while (running_) {

        ssize_t r = sock_->recv_batch(bufs);

        if (r < 0) continue;

        if (r > 0) {

            for (ssize_t i=0;i<r;i++) {

                // Track client from a fake header: in real world we would read src addr from recvmmsg

                // Here we approximate by requiring clients to include magic header at start

                if (bufs[i].size() >= sizeof(PacketHeader)) {

                    PacketHeader* hdr = reinterpret_cast<PacketHeader*>(bufs[i].data());

                    if (hdr->magic == kMagic) {

                        // Cannot access peer addr without msghdr name here (already set in socket), so skip addr track

                    }

                }

                stats_.inc_recv(1);

                stats_.add_rx_bytes(bufs[i].size());

            }

            if (cfg_.echo) {

                // Echo back

                std::vector<std::vector<uint8_t>> out;

                out.reserve(r);

                for (ssize_t i=0;i<r;i++) out.push_back(bufs[i]);

                ssize_t s = sock_->send_batch(out, nullptr);

                if (s > 0) {

                    stats_.inc_sent(s);

                    size_t total_bytes = 0; for (auto& b: out) total_bytes += b.size();

                    stats_.add_tx_bytes(total_bytes);

                }

            }

        }

        auto now = std::chrono::steady_clock::now();

        if (now - last_ts >= std::chrono::seconds(1)) {

            uint64_t recv_total = stats_.recv();

            uint64_t delta = recv_total - last_recv_total;

            last_rate_pps_ = static_cast<double>(delta);

            if (cfg_.verbose) {

                std::cout << "[server] " << stats_.to_string()
<< " rate=" << human_rate(last_rate_pps_) << "\n";

            }

            last_recv_total = recv_total;

            last_ts = now;

        }

    }

}
 
} // namespace udp

 