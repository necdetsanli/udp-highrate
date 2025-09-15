#pragma once

#include <cstdint>

#include <cstddef>

#include <string>

#include <chrono>
 
/**

* @file

* @brief Common wire-level types and tiny utilities for the UDP high-rate demo.

*

* This header defines the on-the-wire packet header used by both client and server,

* plus two helpers:

*  - a monotonic nanosecond timestamp provider (@ref udp::now_ns),

*  - and a human-readable rate formatter (@ref udp::human_rate).

*

* @note All functions here are thread-safe and lock-free.

* @warning The wire layout does not perform endian conversion; see notes on @ref udp::PacketHeader.

*/
 
namespace udp {
 
/**

* The header is packed to 1-byte alignment to avoid implicit padding and keep

* a stable, predictable wire layout across compilers.

*

* @warning Fields are expressed in host endianness. If packets are exchanged

*          across machines with different endianness, convert to network byte

*          order before sending and back after receiving.

*/

#pragma pack(push, 1)
 
/**

* @brief Fixed wire header prepended to every UDP payload.

*

* This compact header allows the receiver to validate frames (via @ref magic),

* reconstruct send order (via @ref seq), and compute one-way/RTT timing

* (via @ref send_ts_ns) when combined with a receive timestamp.

*/

struct PacketHeader {

    /**

     * @brief Monotonically increasing sequence number assigned by the sender.

     *

     * Typically increments by 1 per packet. Useful to detect loss, reordering,

     * and to compute simple delivery metrics.

     */

    uint64_t seq;         // sequence number
 
    /**

     * @brief Sender timestamp in nanoseconds (monotonic time base).

     *

     * The value is intended to be taken from a monotonic clock (e.g.,

     * std::chrono::steady_clock). It is not a wall-clock timestamp and should

     * be used for interval math (differences), not for display.

     */

    uint64_t send_ts_ns;  // sender timestamp (ns)
 
    /**

     * @brief Magic constant for quick sanity checking of the header.

     *

     * Receivers may drop frames whose @ref magic does not match @ref kMagic.

     */

    uint32_t magic;       // magic for sanity

};

#pragma pack(pop)
 
/**

* @brief Magic value expected in @ref PacketHeader::magic.

*

* Chosen to be visually distinctive in hex dumps.

*/

static constexpr uint32_t kMagic = 0xC0DEF00D;
 
/**

* @brief Returns a monotonic timestamp in nanoseconds.

*

* @details Uses @c std::chrono::steady_clock so it will not jump backwards if

*          the system wall clock is adjusted (e.g., by NTP).

*

* @return Nanoseconds since an unspecified steady epoch. Only differences

*         between two calls are meaningful for measuring intervals/latency.

*

* @see PacketHeader::send_ts_ns

*/

inline uint64_t now_ns() {

    using namespace std::chrono;

    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();

}
 
/**

* @brief Formats a packet rate as a human-readable string.

*

* @param v Rate in packets per second (pps).

* @return A short string such as:

*         - @c "500.00 pps"   (for values ≤ 1e3)

*         - @c "12.34 kpps"   (for values in (1e3, 1e6])

*         - @c "1.23 Mpps"    (for values > 1e6)

*

* @note Uses a dot as decimal separator and prints two fractional digits.

*       The formatting is locale-independent (via @c snprintf).

* @warning This is intended for logs/diagnostics, not for strict machine parsing.

*/

inline std::string human_rate(double v) {

    char buf[64];

    if (v > 1e6) snprintf(buf, sizeof(buf), "%.2f Mpps", v / 1e6);

    else if (v > 1e3) snprintf(buf, sizeof(buf), "%.2f kpps", v / 1e3);

    else snprintf(buf, sizeof(buf), "%.2f pps", v);

    return std::string(buf);

}
 
} // namespace udp

 