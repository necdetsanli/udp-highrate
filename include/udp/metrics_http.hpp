#pragma once
#include <thread>
#include <atomic>
#include <string>
#include "udp/stats.hpp"
 
/**
* @file
* @brief Minimal HTTP server that exposes runtime metrics for scraping.
*
* This helper runs a tiny HTTP server on a background thread and renders the
* current counters from @ref udp::Stats in a text format suitable for simple
* scraping (e.g., Prometheus exposition format).  It is intentionally small and
* dependency-free so it can be embedded directly into tests and demos.
*
* @par Usage
* @code
* udp::Stats stats;
* udp::MetricsHttpServer http(stats, 9100);
* http.start();
* // ... run workload, then scrape http://<host>:9100/metrics ...
* http.stop(); // or rely on destructor to stop and join
* @endcode
*
* @note Thread-safety: one instance is typically owned and controlled by a single
*       thread. The background thread only *reads* from @ref udp::Stats via its
*       lock-free getters; @ref Stats::unique_clients() may take a short mutex.
*/
 
namespace udp {
 
/**
* @brief Background HTTP endpoint that serves metrics derived from @ref udp::Stats.
*
* @details
* - @ref start spawns a thread that listens on @ref port_ and serves requests.
* - @ref stop requests a graceful shutdown and joins the thread.
* - @ref render builds the plaintext payload from the current stats snapshot.
*
* The implementation is purposely minimal (no TLS, no routing beyond a single
* endpoint such as @c /metrics) to keep the hot path free of heavy dependencies.
*/
class MetricsHttpServer {
public:
    /**
     * @brief Construct a metrics server bound to a port and fed by @p stats.
     * @param stats Reference to a live @ref udp::Stats instance; must outlive this object.
     * @param port  TCP port to listen on (host byte order).
     *
     * @warning The server does not take ownership of @p stats; callers must ensure
     *          the referenced object remains valid for the lifetime of this server.
     */
    MetricsHttpServer(Stats& stats, uint16_t port);
 
    /**
     * @brief Destructor; ensures the background thread is stopped and joined.
     *
     * Equivalent to calling @ref stop() if the server is still running.
     */
    ~MetricsHttpServer();
 
    /**
     * @brief Start the HTTP listener thread (idempotent).
     *
     * If already running, additional calls are no-ops.
     */
    void start();
 
    /**
     * @brief Request a graceful shutdown and join the thread (idempotent).
     *
     * Safe to call multiple times; returns after the background thread exits.
     */
    void stop();
 
private:
    /**
     * @brief Thread entry point: accept loop + request handling.
     *
     * Reads @ref running_ to determine termination, and for each request
     * writes the result of @ref render() as the response body.
     */
    void run();
 
    /**
     * @brief Build the current metrics payload as a plaintext string.
     *
     * @details Formats counters from @ref stats_ into a simple, line-oriented
     *          representation (e.g., Prometheus exposition style). The snapshot
     *          is not transactional across all counters but is sufficient for
     *          human-readable logs and periodic scraping.
     */
    std::string render();
 
    Stats& stats_;               ///< Live source of counters to expose.
    uint16_t port_;              ///< TCP port to listen on.
    std::thread th_;             ///< Background server thread.
    std::atomic<bool> running_{false}; ///< Run flag observed by @ref run().
};
 
} // namespace udp