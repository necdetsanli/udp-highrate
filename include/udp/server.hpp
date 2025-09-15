#pragma once

#include <vector>

#include <atomic>

#include <thread>

#include <memory>

#include "udp/socket.hpp"

#include "udp/stats.hpp"

#include "udp/common.hpp"

#include "udp/metrics_http.hpp"
 
/**

* @file

* @brief High-rate UDP server: batch receive, optional echo, runtime metrics.

*

* This header defines the configuration and the server façade used by the

* executable entry point. The server runs a background thread that:

*  - receives datagrams in batches (preferring `recvmmsg` via @ref ISocket),

*  - updates @ref udp::Stats counters on the hot path using relaxed atomics,

*  - optionally echoes received payloads back to their sender (when enabled),

*  - periodically computes a per-second receive rate (pps) for logging/inspection,

*  - optionally exposes `/metrics` via @ref udp::MetricsHttpServer.

*

* @par Design

* Core logic depends on the @ref ISocket interface (Strategy/Port); a concrete

* adapter such as @ref UdpSocket is injected at construction time (DI). This keeps

* the server testable (with @ref MockSocket) and evolvable (future adapters).

*

* @note Thread-safety: one owner/thread should manage a @ref UdpServer instance.

*       Internally the server owns a worker thread for the I/O loop. Public

*       API is minimal (`start`/`stop`/getters) and not intended for concurrent

*       calls without external synchronization.

*/
 
namespace udp {
 
/**

* @brief Runtime configuration knobs for @ref UdpServer.

*

* @details

* - @ref port : UDP port to bind on the local host (host byte order).

* - @ref batch : Target number of datagrams to process per syscall. Larger values

*   reduce syscall overhead (PPS↑) at the cost of slightly higher per-packet latency.

* - @ref echo : If true, mirror incoming datagrams back to their source address.

* - @ref reuseport : If true, attempt to enable `SO_REUSEPORT` (multi-worker setups).

* - @ref verbose : If true, log periodic rate/counter lines to stdout.

* - @ref metrics_port : TCP port for `/metrics` endpoint (0 disables HTTP metrics).

*/

struct ServerConfig {

    uint16_t port = 9000;      ///< UDP listen port (host order).

    int      batch = 64;       ///< Batch size hint for recv/send operations.

    bool     echo = false;     ///< Echo received payloads back to sender if true.

    bool     reuseport = false;///< Try SO_REUSEPORT for multi-workers if true.

    bool     verbose = true;   ///< Print periodic stats/rate logs if true.

    uint16_t metrics_port = 9100; ///< HTTP metrics port; 0 = disabled.

};
 
/**

* @brief High-rate UDP server using a pluggable @ref ISocket.

*

* @details

* Lifecycle:

* - Construct with a concrete @ref ISocket and @ref ServerConfig.

* - Call @ref start() to spawn the worker thread and begin receiving.

* - Call @ref stop() to request a graceful shutdown and join the thread.

*

* Behavior:

* - The worker @ref run_loop() uses batch I/O to pull datagrams, updates

*   @ref Stats, and (optionally) echoes payloads. Once per second it computes

*   a moving per-second receive rate, exposed via @ref last_rate_pps().

* - If @ref ServerConfig::metrics_port is non-zero, a @ref MetricsHttpServer

*   is created and started to expose counters for scraping.

*/

class UdpServer {

public:

    /**

     * @brief Construct the server with a socket strategy and configuration.

     *

     * @param sock Concrete @ref ISocket (e.g., @ref UdpSocket or @ref MockSocket).

     *             Ownership is transferred to the server.

     * @param cfg  Server configuration (port, batch size, echo, metrics, …).

     */

    explicit UdpServer(std::unique_ptr<ISocket> sock, ServerConfig cfg);
 
    /**

     * @brief Destructor; ensures the worker thread is stopped and joined.

     */

    ~UdpServer();
 
    /**

     * @brief Start the worker thread (idempotent).

     *

     * If already running, additional calls are no-ops.

     */

    void start();
 
    /**

     * @brief Request a graceful shutdown and join the worker thread (idempotent).

     *

     * Safe to call multiple times; returns after the background thread exits.

     */

    void stop();
 
    /**

     * @brief Last computed receive rate in packets per second.

     *

     * @return The most recent one-second pps estimate (may be 0 before the first tick).

     */

    double last_rate_pps() const { return last_rate_pps_; }
 
    /**

     * @brief Read-only access to cumulative counters.

     *

     * @return Const reference to internal @ref Stats.

     */

    const Stats& stats() const { return stats_; }
 
private:

    /**

     * @brief Background I/O loop: batch receive, optional echo, rate computation.

     *

     * Runs while @ref running_ is true. Updates @ref stats_ on the hot path and

     * refreshes @ref last_rate_pps_ once per second. Errors are handled by retrying

     * or breaking the loop depending on severity.

     */

    void run_loop();
 
    std::unique_ptr<ISocket> sock_;   ///< Injected socket strategy (owned).

    ServerConfig             cfg_;    ///< Immutable server configuration copy.

    Stats                    stats_;  ///< Hot-path counters (relaxed atomics) + unique clients.

    std::unique_ptr<MetricsHttpServer> metrics_; ///< Optional `/metrics` HTTP server.

    std::thread              th_;     ///< Worker thread running @ref run_loop().

    std::atomic<bool>        running_{false}; ///< Run flag observed by @ref run_loop().

    double                   last_rate_pps_{0.0}; ///< Last 1-second receive rate (pps).

};
 
} // namespace udp

 