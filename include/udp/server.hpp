#pragma once

#include <vector>

#include <string>

#include <cstdint>

#include <atomic>

#include <thread>

#include <memory>

#include <unordered_set>

#include "udp/socket.hpp"

#include "udp/stats.hpp"

#include "udp/common.hpp"

#include "udp/metrics_http.hpp"
 
namespace udp {
 
/**

* @brief Server configuration knobs.

*

* @details

* - @ref max_clients provides an **admission control** guard: the server will accept

*   at most this many distinct clients at a time. A "client" is identified by the

*   (source IPv4 address, UDP port) pair observed on incoming datagrams.

* - When the limit is reached, **packets from previously unseen clients are dropped**,

*   while already admitted clients continue to be served.

*

* @note Enforcing admission requires access to the source address. On Linux the

*       implementation uses `recvmmsg` with `msg_name` to capture per-message

*       addresses. In environments where the raw socket fd is not available

*       (e.g., tests with @ref MockSocket) the server falls back to a mode where

*       the limit cannot be enforced, but normal packet handling still works.

*/

struct ServerConfig {

    uint16_t port = 9000;         ///< UDP listen port.

    int      batch = 64;          ///< Recv/send batch size hint.

    bool     echo = false;        ///< Echo received payloads back to sender.

    bool     reuseport = false;   ///< Request SO_REUSEPORT (if supported).

    bool     verbose = true;      ///< Print periodic stats.

    uint16_t metrics_port = 9100; ///< Loopback HTTP port for /metrics (0 = disabled).

    size_t   max_clients = 100;   ///< **Admission limit**: max distinct (IP:port) clients.

};
 
/**

* @brief High-rate UDP server with batch receive, optional echo, metrics, and admission control.

*

* @details

* Responsibilities:

*  - Bind a UDP socket and run the main receive loop.

*  - Maintain hot-path counters via @ref Stats and compute per-second PPS.

*  - Optionally echo payloads back to senders.

*  - Expose `/metrics` (Prometheus text) via @ref MetricsHttpServer.

*  - **Admission control:** allow up to @ref ServerConfig::max_clients distinct clients.

*

* Admission semantics:

*  - A "client" is the observed (IPv4 address, UDP port) of an incoming datagram.

*  - The first `max_clients` distinct clients are **admitted**.

*  - Packets from any **new** (unseen) client beyond the limit are dropped.

*  - Already-admitted clients are unaffected by later drops.

*

* @note Admission relies on retrieving source addresses from the kernel via

*       `recvmmsg`/`recvfrom`. Where not available (e.g., @ref MockSocket),

*       the server continues to run but cannot enforce the cap.

*/

class UdpServer {

public:

    explicit UdpServer(std::unique_ptr<ISocket> sock, ServerConfig cfg);

    ~UdpServer();
 
    /// @brief Start worker thread (and metrics if configured).

    void start();
 
    /// @brief Request stop and join worker thread; stop metrics.

    void stop();
 
    /// @brief Last computed packets-per-second (1s window).

    double last_rate_pps() const { return last_rate_pps_; }
 
    /// @brief Read-only access to cumulative stats.

    const Stats& stats() const { return stats_; }
 
private:

    void run_loop();
 
    std::unique_ptr<ISocket> sock_;

    ServerConfig             cfg_;

    Stats                    stats_;

    std::unique_ptr<MetricsHttpServer> metrics_;

    std::thread              th_;

    std::atomic<bool>        running_{false};

    double                   last_rate_pps_{0.0};
 
    // Admission set: distinct clients currently admitted (IP:port in host order).

    std::unordered_set<ClientKey, ClientKeyHash> admitted_;

};
 
} // namespace udp

 