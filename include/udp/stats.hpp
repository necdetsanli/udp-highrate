#pragma once
#include <atomic>
#include <unordered_map>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <sstream>
 
/**
* @file
* @brief Lightweight, thread-safe counters and client tracking for UDP throughput tests.
*
* This header exposes:
*  - @ref udp::ClientKey : a compact (IPv4 address, port) tuple with equality.
*  - @ref udp::ClientKeyHash : hash functor for @c unordered_map keys.
*  - @ref udp::Stats : hot-path friendly counters (lock-free atomics) and an optional
*    unique-client tracker guarded by a short-lived mutex.
*
* @note The atomic counters use @c memory_order_relaxed because we only care about
*       numerical accuracy, not cross-counter ordering. Reads may observe slightly
*       skewed snapshots across different counters; this is acceptable for metrics.
*/
 
namespace udp {
 
/**
* @brief Key type representing a client as (IPv4 address, UDP port).
*
* @details
* - @ref addr is expected to be a 32-bit IPv4 address in host byte order unless
*   otherwise noted by the caller.
* - @ref port is the UDP port in host byte order.
* - Equality compares both fields for exact match.
*
* @warning If you store network-byte-order values here, use a consistent convention
*          across the codebase and be explicit when converting (e.g., @c ntohl/ntohs).
*/
struct ClientKey {
    uint32_t addr;  ///< IPv4 address (host order unless documented otherwise).
    uint16_t port;  ///< UDP port (host order).
 
    /// @brief Equality: two keys are equal iff both address and port match.
    bool operator==(const ClientKey& o) const { return addr == o.addr && port == o.port; }
};
 
/**
* @brief Hash functor for @ref ClientKey suitable for @c std::unordered_map.
*
* @details A simple, cheap mix: shift the 32-bit address and XOR with the 16-bit port.
* This is sufficient for basic metrics/indexing. For large maps or adversarial inputs,
* consider a stronger combiner (e.g., 64-bit splitmix or std::hash composition).
*/
struct ClientKeyHash {
    /// @brief Compute a size_t hash from an address-port pair.
    size_t operator()(const ClientKey& k) const {
        return (static_cast<size_t>(k.addr) << 16) ^ k.port;
    }
};
 
/**
* @brief Aggregated counters and (optional) unique-client tracking.
*
* @details
* - **Hot path:** packet/byte counters are @c std::atomic and updated with
*   @c memory_order_relaxed to avoid locks and minimize contention.
* - **Unique clients:** tracked in an @c unordered_map guarded by a small
*   @c std::mutex. This should be called less frequently than the hot-path
*   counters to avoid contention in tight loops.
*
* @par Thread-safety
* - @ref inc_sent, @ref inc_recv, @ref add_rx_bytes, @ref add_tx_bytes and the
*   getters are lock-free and thread-safe.
* - @ref note_client and @ref unique_clients acquire an internal mutex.
*
* @par Consistency
* Reads of different counters are not atomic as a group; a single @ref to_string
* call may reflect slightly different instants per counter. This is generally fine
* for diagnostics and metrics.
*
* @code
* udp::Stats s;
* s.inc_recv(64);
* s.add_rx_bytes(64 * 128);
* s.note_client(0x7f000001 /*127.0.0.1*/, 9000);
* std::string line = s.to_string(); // "recv=64 sent=0 unique_clients=1 rx_bytes=8192 tx_bytes=0"
* @endcode
*/
class Stats {
public:
    /**
     * @brief Increase the number of sent packets by @p n (lock-free).
     * @param n Number of packets to add.
     */
    void inc_sent(uint64_t n) { sent_.fetch_add(n, std::memory_order_relaxed); }
 
    /**
     * @brief Increase the number of received packets by @p n (lock-free).
     * @param n Number of packets to add.
     */
    void inc_recv(uint64_t n) { recv_.fetch_add(n, std::memory_order_relaxed); }
 
    /**
     * @brief Increase the total received bytes by @p n (lock-free).
     * @param n Number of bytes to add.
     */
    void add_rx_bytes(uint64_t n) { rx_bytes_.fetch_add(n, std::memory_order_relaxed); }
 
    /**
     * @brief Increase the total transmitted bytes by @p n (lock-free).
     * @param n Number of bytes to add.
     */
    void add_tx_bytes(uint64_t n) { tx_bytes_.fetch_add(n, std::memory_order_relaxed); }
 
    /**
     * @brief Record (or update) activity for a specific client (addr, port).
     *
     * @details Increments an internal hit counter for the given @ref ClientKey.
     * If the key was not present, inserts it with a count of 1.
     *
     * @note This method acquires a short-lived mutex to protect the map.
     * Prefer calling it at a reduced frequency (e.g., sampling) in very hot loops.
     */
    void note_client(uint32_t addr, uint16_t port) {
        std::lock_guard<std::mutex> lg(mu_);
        clients_[ClientKey{addr, port}]++;
    }
 
    /**
     * @brief Return the current number of unique clients observed.
     *
     * @note Acquires the internal mutex to read the map size.
     */
    size_t unique_clients() const {
        std::lock_guard<std::mutex> lg(mu_);
        return clients_.size();
    }
 
    /// @brief Read the total number of sent packets (lock-free).
    uint64_t sent() const { return sent_.load(std::memory_order_relaxed); }
 
    /// @brief Read the total number of received packets (lock-free).
    uint64_t recv() const { return recv_.load(std::memory_order_relaxed); }
 
    /// @brief Read the total number of received bytes (lock-free).
    uint64_t rx_bytes() const { return rx_bytes_.load(std::memory_order_relaxed); }
 
    /// @brief Read the total number of transmitted bytes (lock-free).
    uint64_t tx_bytes() const { return tx_bytes_.load(std::memory_order_relaxed); }
 
    /**
     * @brief Produce a single-line human-readable snapshot of all counters.
     *
     * @details The values are collected independently; the line is not a transactional
     * snapshot. Suitable for periodic logs and simple diagnostics.
     */
    std::string to_string() const {
        std::ostringstream oss;
        oss << "recv=" << recv() << " sent=" << sent()
<< " unique_clients=" << unique_clients()
<< " rx_bytes=" << rx_bytes() << " tx_bytes=" << tx_bytes();
        return oss.str();
    }
 
private:
    /// @name Hot-path counters (lock-free)
    ///@{
    std::atomic<uint64_t> sent_{0};     ///< Total packets sent.
    std::atomic<uint64_t> recv_{0};     ///< Total packets received.
    std::atomic<uint64_t> rx_bytes_{0}; ///< Total bytes received.
    std::atomic<uint64_t> tx_bytes_{0}; ///< Total bytes transmitted.
    ///@}
 
    mutable std::mutex mu_;  ///< Protects @ref clients_ for insert/size operations.
 
    /**
     * @brief Map from client key to a simple hit count.
     *
     * @details The count is not used for logic in this class beyond existence;
     * it can be helpful if you later want to know per-client packet tallies.
     */
    std::unordered_map<ClientKey, uint64_t, ClientKeyHash> clients_;
};
 
} // namespace udp