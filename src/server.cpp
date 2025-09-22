/**

* @file

* @brief UdpServer implementation: batch recv, optional echo, PPS accounting, /metrics, and admission control.

*

* @details

* New behavior:

*  - **Admission control** limits the number of distinct clients (by (IP:port)) to

*    @ref udp::ServerConfig::max_clients. When the cap is reached, datagrams from

*    any **new** client are dropped while already-admitted clients continue.

*

* Source address handling:

*  - On Linux with a real socket fd, the server uses `recvmmsg` and reads per-message

*    `msg_name` to determine the sender address without extra syscalls.

*  - If a raw fd is not available (e.g., @ref udp::MockSocket returns -1) the server

*    falls back to `ISocket::recv_batch()` which does not expose source addresses;

*    in that mode the admission limit cannot be enforced (documented limitation).

*

* Echo:

*  - In the Linux/`recvmmsg` path, echo uses `sendmmsg` with per-message destinations.

*  - In the fallback path (no addresses), echo behavior remains best-effort or disabled.

*/
 
#include "udp/server.hpp"

#include <iostream>

#include <cstring>

#include <arpa/inet.h>

#include <sys/socket.h>

#include <sys/types.h>

#include <cerrno>

#include <algorithm>
 
namespace udp {
 
UdpServer::UdpServer(std::unique_ptr<ISocket> sock, ServerConfig cfg)

: sock_(std::move(sock)), cfg_(cfg) {

    sock_->bind(cfg_.port, cfg_.reuseport);

    sock_->set_rcvbuf(1<<20);

    sock_->set_sndbuf(1<<20);

    if (cfg_.metrics_port) {

        metrics_ = std::make_unique<MetricsHttpServer>(stats_, cfg_.metrics_port);

    }

}
 
UdpServer::~UdpServer() {

    stop();

}
 
void UdpServer::start() {

    if (metrics_) metrics_->start();

    running_ = true;

    th_ = std::thread(&UdpServer::run_loop, this);

}
 
void UdpServer::stop() {

    if (th_.joinable()) {

        running_ = false;

        th_.join();

    }

    if (metrics_) metrics_->stop();

}
 
void UdpServer::run_loop() {

    std::vector<std::vector<uint8_t>> bufs(cfg_.batch, std::vector<uint8_t>(2048));

    uint64_t last_recv_total = 0;

    auto last_ts = std::chrono::steady_clock::now();
 
    const int fd = sock_->fd();
 
#if defined(__linux__)

    // Fast path: we can access the raw fd and gather source addresses via recvmmsg.

    const bool can_use_recvmmsg = (fd >= 0);

#else

    const bool can_use_recvmmsg = false;

#endif
 
    while (running_) {

        ssize_t r = 0;
 
        if (can_use_recvmmsg) {

#if defined(__linux__)

            const size_t n = bufs.size();

            std::vector<iovec> iov(n);

            std::vector<mmsghdr> msgs(n);

            std::vector<sockaddr_in> addrs(n);

            std::vector<char> ctrl(64 * n);
 
            for (size_t i=0;i<n;i++) {

                iov[i].iov_base = bufs[i].data();

                iov[i].iov_len  = bufs[i].size();

                std::memset(&msgs[i], 0, sizeof(mmsghdr));

                msgs[i].msg_hdr.msg_iov    = &iov[i];

                msgs[i].msg_hdr.msg_iovlen = 1;

                msgs[i].msg_hdr.msg_name   = &addrs[i];

                msgs[i].msg_hdr.msg_namelen= sizeof(sockaddr_in);

                msgs[i].msg_hdr.msg_control= ctrl.data() + i*64;

                msgs[i].msg_hdr.msg_controllen = 64;

            }
 
            r = ::recvmmsg(fd, msgs.data(), n, 0, nullptr);

            if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) r = 0;

            if (r < 0) {

                // Error: continue best-effort

                continue;

            }
 
            // Process received messages with admission control.

            ssize_t echoed = 0;

            std::vector<mmsghdr> echo_msgs; echo_msgs.reserve(r);

            std::vector<iovec>   echo_iov;  echo_iov.reserve(r);

            std::vector<sockaddr_in> echo_addrs; echo_addrs.reserve(r);
 
            for (ssize_t i=0; i<r; ++i) {

                // Build client key (host-order fields)

                ClientKey key {

                    static_cast<uint32_t>(ntohl(addrs[i].sin_addr.s_addr)),

                    static_cast<uint16_t>(ntohs(addrs[i].sin_port))

                };
 
                // Admission check: admit if seen, otherwise admit only if capacity remains.

                bool allowed = false;

                auto it = admitted_.find(key);

                if (it != admitted_.end()) {

                    allowed = true;

                } else if (admitted_.size() < cfg_.max_clients) {

                    admitted_.insert(key);

                    allowed = true;

                } else {

                    // Over capacity: drop this message from a new client.

                    allowed = false;

                }
 
                if (!allowed) {

                    // Skip counters for dropped packets to make metrics reflect served traffic.

                    continue;

                }
 
                // Metrics (served traffic)

                stats_.note_client(key.addr, key.port);

                stats_.inc_recv(1);

                stats_.add_rx_bytes(msgs[i].msg_len);
 
                if (cfg_.echo) {

                    // Prepare echo mmsghdr referencing the same buffer back to sender.

                    echo_iov.push_back({});

                    echo_iov.back().iov_base = bufs[i].data();

                    echo_iov.back().iov_len  = msgs[i].msg_len;
 
                    echo_addrs.push_back(addrs[i]);
 
                    echo_msgs.push_back({});

                    echo_msgs.back().msg_hdr.msg_iov    = &echo_iov.back();

                    echo_msgs.back().msg_hdr.msg_iovlen = 1;

                    echo_msgs.back().msg_hdr.msg_name   = &echo_addrs.back();

                    echo_msgs.back().msg_hdr.msg_namelen= sizeof(sockaddr_in);

                    echoed++;

                }

            }
 
            if (cfg_.echo && echoed > 0) {

                int w = ::sendmmsg(fd, echo_msgs.data(), echoed, 0);

                if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {

                    w = 0;

                }

                if (w > 0) {

                    stats_.inc_sent(static_cast<uint64_t>(w));

                    size_t total_bytes = 0;

                    for (int i=0; i<w; ++i) {

                        // We used one buffer per message; msg_len not set by sendmmsg,

                        // so approximate with io vector length we queued.

                        total_bytes += echo_msgs[i].msg_hdr.msg_iov[0].iov_len;

                    }

                    stats_.add_tx_bytes(total_bytes);

                }

            }

#endif // __linux__
 
        } else {

            // Fallback: no access to per-message source address (e.g., MockSocket).

            // We cannot enforce admission here; process as before.

            r = sock_->recv_batch(bufs);

            if (r < 0) continue;

            if (r > 0) {

                for (ssize_t i=0;i<r;i++) {

                    stats_.inc_recv(1);

                    stats_.add_rx_bytes(bufs[i].size());

                }

                if (cfg_.echo) {

                    // Best-effort: original ISocket::send_batch cannot target per-message addrs.

                    // Leave as no-op or future improvement if needed.

                    // (Keeping behavior consistent with previous fallback path.)

                }

            }

        }
 
        // Once per second: compute and log PPS.

        auto now = std::chrono::steady_clock::now();

        if (now - last_ts >= std::chrono::seconds(1)) {

            uint64_t recv_total = stats_.recv();

            uint64_t delta = recv_total - last_recv_total;

            last_rate_pps_ = static_cast<double>(delta);

            if (cfg_.verbose) {

                std::cout << "[server] " << stats_.to_string()
<< " rate=" << human_rate(last_rate_pps_)
<< " admitted=" << admitted_.size()
<< " cap=" << cfg_.max_clients
<< "\n";

            }

            last_recv_total = recv_total;

            last_ts = now;

        }

    }

}
 
} // namespace udp
 