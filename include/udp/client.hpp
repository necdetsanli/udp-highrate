#pragma once

#include <vector>

#include <atomic>

#include <thread>

#include <memory>

#include "udp/socket.hpp"

#include "udp/stats.hpp"

#include "udp/common.hpp"
 
/**

* @file

* @brief High-rate UDP client: pacing + batch send with lightweight counters.

*

* This header defines:

*  - @ref udp::ClientConfig : knobs for rate, duration, payload size, batching, etc.

*  - @ref udp::UdpClient    : a small façade that sends UDP datagrams at a target

*                             packets-per-second (PPS), using batch I/O when possible.

*

* @par Design

* The client depends only on the @ref ISocket interface (Strategy/Port). A concrete

* adapter such as @ref UdpSocket is injected at construction time (Dependency Injection),

* which keeps the client testable (with @ref MockSocket) and evolvable (future adapters).

*

* @par Pacing & batching

* The send loop distributes @ref ClientConfig::pps over time using a monotonic clock

* (see @ref udp::now_ns). When supported by the socket implementation, messages are sent

* in batches (e.g., via @c sendmmsg) to reduce syscall overhead and sustain high PPS.

*

* @note Thread-safety: a single owner should manage one @ref UdpClient instance.

*       Internally the client owns a worker thread for the send loop. Public API

*       is minimal and not intended for concurrent calls without external sync.

*/
 
namespace udp {
 
/**

* @brief Runtime configuration for @ref UdpClient.

*

* @details

* - @ref server_ip : Dotted IPv4 address of the server (e.g., "127.0.0.1").

* - @ref port      : Server UDP port (host byte order).

* - @ref pps       : Target packets-per-second to transmit (global rate).

* - @ref seconds   : Total run duration; the client stops after this many seconds.

* - @ref payload   : Payload size per datagram (bytes), not including any header added by caller.

* - @ref batch     : Preferred number of messages per send syscall (throughput vs latency trade-off).

* - @ref id        : Optional client identifier (for logging or payload tagging).

* - @ref verbose   : If true, prints periodic rate/counter lines to stdout.

*/

struct ClientConfig {

    std::string server_ip = "127.0.0.1"; ///< Destination IPv4 address (string).

    uint16_t    port      = 9000;        ///< Destination UDP port (host order).

    uint64_t    pps       = 10000;       ///< Target packets per second (PPS).

    int         seconds   = 5;           ///< Run duration in seconds.

    int         payload   = 64;          ///< Bytes per datagram payload.

    int         batch     = 64;          ///< Messages per batch send attempt.

    int         id        = 0;           ///< Optional client id for diagnostics.

    bool        verbose   = false;       ///< Enable periodic logging if true.

};
 
/**

* @brief High-rate UDP client using a pluggable @ref ISocket.

*

* @details

* Lifecycle:

* - Construct with a concrete @ref ISocket and @ref ClientConfig.

* - Call @ref start() to spawn the worker thread and begin paced sending.

* - Optionally call @ref join() to block until completion.

* - Call @ref stop() for an early, graceful shutdown.

*

* Behavior:

* - The worker @ref run_loop() constructs datagrams (optionally including a

*   @ref PacketHeader set by the caller in the source buffer) and transmits them

*   at the configured PPS. It updates @ref Stats on the hot path using relaxed atomics.

* - When available, the socket implementation batches sends to minimize syscalls.

* - The loop exits when the configured duration elapses or when @ref stop() is called.

*/

class UdpClient {

public:

    /**

     * @brief Construct the client with a socket strategy and configuration.

     *

     * @param sock Concrete @ref ISocket (e.g., @ref UdpSocket or @ref MockSocket).

     *             Ownership is transferred to the client.

     * @param cfg  Client configuration (destination, PPS, duration, batch, …).

     */

    explicit UdpClient(std::unique_ptr<ISocket> sock, ClientConfig cfg);
 
    /**

     * @brief Destructor; ensures the worker thread is stopped and joined.

     */

    ~UdpClient();
 
    /**

     * @brief Start the send loop in a background thread (idempotent).

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

     * @brief Block the caller until the background thread finishes.

     *

     * This is useful when the client was started with a finite duration and the

     * caller wants to wait for completion without polling.

     */

    void join();
 
    /**

     * @brief Read-only access to cumulative counters.

     *

     * @return Const reference to internal @ref Stats (sent/recv, bytes, clients).

     */

    const Stats& stats() const { return stats_; }
 
private:

    /**

     * @brief Background send loop: pacing + batch send + counter updates.

     *

     * Uses a monotonic clock (@ref now_ns) to space out batches so that the total

     * number of packets per second approaches @ref ClientConfig::pps. On each

     * iteration it prepares up to @ref ClientConfig::batch payloads and calls

     * @ref ISocket::send_batch. Counters are updated using relaxed atomics.

     */

    void run_loop();
 
    std::unique_ptr<ISocket> sock_; ///< Injected socket strategy (owned).

    ClientConfig             cfg_;  ///< Immutable client configuration copy.

    Stats                    stats_;///< Hot-path counters (relaxed atomics).

    std::thread              th_;   ///< Worker thread running @ref run_loop().

    std::atomic<bool>        running_{false}; ///< Run flag observed by @ref run_loop().

    uint64_t                 seq_{0};         ///< Sequence number for generated packets.

};
 
} // namespace udp

 