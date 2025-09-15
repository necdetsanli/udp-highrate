#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <atomic>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
 
/**
* @file
* @brief Socket abstraction for high-rate UDP I/O (batch send/receive) plus a test double.
*
* This header defines:
*  - @ref udp::ISocket : the strategy/port interface that core logic depends on,
*  - @ref udp::UdpSocket : a concrete Linux/POSIX implementation optimized for batch I/O,
*  - @ref udp::MockSocket : a lightweight in-memory test double.
*
* The goal is to decouple business logic (client/server) from the OS-specific
* socket details, making the system testable (via dependency injection) and
* evolvable (e.g., future io_uring/DPDK adapters) without touching the core.
*
* @note Thread-safety: unless otherwise stated, instances are not designed for
*       concurrent calls into the same object from multiple threads. Prefer
*       single-threaded ownership per @ref ISocket instance, or synchronize externally.
*/
 
namespace udp {
 
/**
* @brief Abstract socket interface (strategy/port).
*
* Core code (client/server) talks to sockets solely through this interface.
* Concrete implementations (e.g., @ref UdpSocket, @ref MockSocket) realize it.
*
* @par Batch semantics
* - @ref recv_batch and @ref send_batch operate on vectors of contiguous buffers.
* - Implementations typically attempt to process up to @c bufs.size() messages in
*   one call (e.g., via @c recvmmsg/@c sendmmsg on Linux) and return the number of
*   messages successfully processed (not bytes).
*/
class ISocket {
public:
    virtual ~ISocket() = default;
 
    /**
     * @brief File descriptor for low-level polling/integration.
     * @return Underlying file descriptor, or -1 if not applicable (e.g., @ref MockSocket).
     */
    virtual int fd() const = 0;
 
    /**
     * @brief Bind the socket to a local UDP port.
     *
     * @param port       Local UDP port in host byte order.
     * @param reuseport  If true, attempt to enable @c SO_REUSEPORT to allow multi-worker setups.
     *
     * @throws std::system_error (typical in implementations) on failure.
     */
    virtual void bind(uint16_t port, bool reuseport) = 0;
 
    /**
     * @brief Optionally connect the socket to a fixed remote endpoint.
     *
     * After a successful connect, datagrams can be sent/received without specifying
     * a destination address on each call. Some implementations may still support
     * per-call destination via the @p addr parameter of @ref send_batch.
     *
     * @param ip   Remote IPv4 address as dotted string (e.g., "127.0.0.1").
     * @param port Remote UDP port in host byte order.
     *
     * @throws std::system_error on failure.
     */
    virtual void connect(const std::string& ip, uint16_t port) = 0;
 
    /**
     * @brief Receive up to @c bufs.size() datagrams in a single call.
     *
     * Each element in @p bufs must be a pre-sized buffer (capacity = max datagram size).
     * Implementations write up to the buffer size for each received message.
     *
     * @param bufs  Vector of destination buffers to fill (one datagram per buffer).
     * @return Number of datagrams received (>= 0), or -1 on error (with errno set).
     *
     * @note Return value counts messages, not bytes. If a datagram exceeds a buffer’s
     *       size, implementations may truncate it (consistent with @c recvfrom semantics).
     */
    virtual ssize_t recv_batch(std::vector<std::vector<uint8_t>>& bufs) = 0;
 
    /**
     * @brief Send up to @c bufs.size() datagrams in a single call.
     *
     * @param bufs  Vector of source buffers (one datagram per element).
     * @param addr  Optional destination. If non-null, sent to this peer for all
     *              buffers in the batch. If null and the socket is connected,
     *              the connected peer is used. If null and not connected, behavior
     *              is implementation-defined (likely an error).
     * @return Number of datagrams sent (>= 0), or -1 on error (with errno set).
     *
     * @note Return value counts messages, not bytes. Implementations try to minimize
     *       syscalls (e.g., @c sendmmsg) but may fall back to per-message sends.
     */
    virtual ssize_t send_batch(const std::vector<std::vector<uint8_t>>& bufs,
                               const sockaddr_in* addr = nullptr) = 0;
 
    /**
     * @brief Hint the desired receive buffer size (bytes).
     * @param bytes Requested size in bytes for @c SO_RCVBUF.
     *
     * @note Implementations may clamp or ignore values depending on OS limits.
     */
    virtual void set_rcvbuf(int bytes);
 
    /**
     * @brief Hint the desired send buffer size (bytes).
     * @param bytes Requested size in bytes for @c SO_SNDBUF.
     *
     * @note Implementations may clamp or ignore values depending on OS limits.
     */
    virtual void set_sndbuf(int bytes);
};
 
/**
* @brief UDP socket implementation using POSIX/Linux syscalls.
*
* Prefers batch syscalls (e.g., @c recvmmsg/@c sendmmsg) when available to reduce
* syscall overhead and improve packets-per-second (PPS). Falls back to classic
* @c recvfrom/@c sendto loops if batch syscalls are not available.
*/
class UdpSocket : public ISocket {
public:
    /**
     * @brief Construct a UDP socket.
     * @param batch_hint A hint for internal pre-allocation of I/O vectors.
     */
    explicit UdpSocket(int batch_hint = 64);
 
    /// @brief Close the socket and release resources.
    ~UdpSocket() override;
 
    /// @copydoc ISocket::fd()
    int fd() const override { return sockfd_; }
 
    /// @copydoc ISocket::bind(uint16_t,bool)
    void bind(uint16_t port, bool reuseport) override;
 
    /// @copydoc ISocket::connect(const std::string&,uint16_t)
    void connect(const std::string& ip, uint16_t port) override;
 
    /// @copydoc ISocket::recv_batch(std::vector<std::vector<uint8_t>>&)
    ssize_t recv_batch(std::vector<std::vector<uint8_t>>& bufs) override;
 
    /// @copydoc ISocket::send_batch(const std::vector<std::vector<uint8_t>>&,const sockaddr_in*)
    ssize_t send_batch(const std::vector<std::vector<uint8_t>>& bufs,
                       const sockaddr_in* addr = nullptr) override;
 
    /// @copydoc ISocket::set_rcvbuf(int)
    void set_rcvbuf(int bytes) override;
 
    /// @copydoc ISocket::set_sndbuf(int)
    void set_sndbuf(int bytes) override;
 
private:
    int sockfd_;        ///< Underlying socket file descriptor.
    int batch_hint_;    ///< Pre-allocation hint for batch I/O structures.
    bool connected_;    ///< Whether @ref connect has been successfully called.
    sockaddr_in peer_{};///< Connected peer (valid only if @ref connected_ is true).
};
 
/**
* @brief In-memory test double for @ref ISocket (no real network I/O).
*
* @details
* - @ref recv_batch pulls preloaded datagrams from an internal queue (@ref preload_recv).
* - @ref send_batch appends buffers to an internal "sent" store for later inspection.
* - Methods do not set @c errno; return values model success counts in a simplified way.
*
* This class enables deterministic unit tests for higher-level logic without
* requiring real sockets or timers.
*/
class MockSocket : public ISocket {
public:
    /// @brief Construct an empty mock with no preloaded datagrams.
    MockSocket() : recv_cursor_(0) {}
 
    /// @copydoc ISocket::fd()
    int fd() const override { return -1; }
 
    /// @copydoc ISocket::bind(uint16_t,bool)
    void bind(uint16_t, bool) override {}
 
    /// @copydoc ISocket::connect(const std::string&,uint16_t)
    void connect(const std::string&, uint16_t) override {}
 
    /// @copydoc ISocket::recv_batch(std::vector<std::vector<uint8_t>>&)
    ssize_t recv_batch(std::vector<std::vector<uint8_t>>& bufs) override;
 
    /// @copydoc ISocket::send_batch(const std::vector<std::vector<uint8_t>>&,const sockaddr_in*)
    ssize_t send_batch(const std::vector<std::vector<uint8_t>>& bufs,
                       const sockaddr_in* addr = nullptr) override;
 
    /// @copydoc ISocket::set_rcvbuf(int)
    void set_rcvbuf(int) override {}
 
    /// @copydoc ISocket::set_sndbuf(int)
    void set_sndbuf(int) override {}
 
    // ---------------------- Test hooks ----------------------
 
    /**
     * @brief Enqueue a datagram to be returned by the next @ref recv_batch call(s).
     * @param pkt A full datagram payload to be copied into caller-provided buffers.
     */
    void preload_recv(const std::vector<uint8_t>& pkt) { rx_store_.push_back(pkt); }
 
    /**
     * @brief Total number of datagrams "sent" via @ref send_batch so far.
     * @return Count of messages appended to @ref tx_store_.
     */
    size_t sent_count() const { return tx_store_.size(); }
 
    /**
     * @brief Access the in-memory "sent" store for assertions in tests.
     * @return Const reference to the vector of sent datagrams.
     */
    const std::vector<std::vector<uint8_t>>& sent() const { return tx_store_; }
 
private:
    std::vector<std::vector<uint8_t>> rx_store_; ///< Preloaded incoming datagrams.
    std::vector<std::vector<uint8_t>> tx_store_; ///< Captured outgoing datagrams.
    size_t recv_cursor_;                          ///< Read cursor into @ref rx_store_.
};
 
} // namespace udp