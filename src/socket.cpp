/**

* @file

* @brief POSIX/Linux UDP socket implementation and in-memory MockSocket.

*

* @details

* This translation unit provides the concrete implementation of the socket

* abstraction declared in `include/udp/socket.hpp`:

*  - `udp::UdpSocket`: uses non-blocking UDP with batch syscalls

*    (`recvmmsg`/`sendmmsg`) when available, and falls back to classic

*    `recvfrom`/`sendto` loops otherwise.

*  - `udp::MockSocket`: a simple in-memory test double that can be preloaded

*    with datagrams and captures sent buffers for assertions.

*

* Concurrency notes:

*  - `UdpSocket` methods are not meant to be called concurrently on the same

*    instance without external synchronization.

*  - The socket is created non-blocking and typical transient errors

*    (`EAGAIN`/`EWOULDBLOCK`) are mapped to a return value of 0 messages.

*/
 
#include "udp/socket.hpp"

#include <arpa/inet.h>

#include <cstring>

#include <stdexcept>

#include <cerrno>

#include <sys/types.h>

#include <fcntl.h>
 
namespace udp {
 
/// \copydoc udp::ISocket::set_rcvbuf

void ISocket::set_rcvbuf(int bytes) {

    (void)bytes; // default no-op; concrete implementations may override

}
 
/// \copydoc udp::ISocket::set_sndbuf

void ISocket::set_sndbuf(int bytes) {

    (void)bytes; // default no-op; concrete implementations may override

}
 
/// \cond INTERNAL

/**

* @brief Create a non-blocking UDP socket (IPv4).

*

* @details

* - Uses `AF_INET`/`SOCK_DGRAM` with protocol 0.

* - Sets `O_NONBLOCK` so that hot paths can poll and treat `EAGAIN`/`EWOULDBLOCK`

*   as "no messages" (return 0) rather than blocking the thread.

*

* @throws std::runtime_error if `socket()` fails.

*/

static int make_socket() {

    int s = ::socket(AF_INET, SOCK_DGRAM, 0);

    if (s < 0) throw std::runtime_error("socket() failed");

    int flags = fcntl(s, F_GETFL, 0);

    fcntl(s, F_SETFL, flags | O_NONBLOCK);

    return s;

}

/// \endcond
 
/**

* @brief Construct a UDP socket and apply basic defaults.

*

* @details

* - Creates a non-blocking socket via `make_socket()`.

* - Enables `SO_REUSEADDR` to ease local restarts during tests/demos.

* - Stores `batch_hint_` for internal pre-allocation strategies.

*/

UdpSocket::UdpSocket(int batch_hint)

    : sockfd_(make_socket()), batch_hint_(batch_hint), connected_(false) {

    int one = 1;

    setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

}
 
/**

* @brief Close the file descriptor if still open.

*/

UdpSocket::~UdpSocket() {

    if (sockfd_ >= 0) ::close(sockfd_);

}
 
/**

* \copydoc udp::ISocket::bind

*

* @details

* - Optionally enables `SO_REUSEPORT` when requested and supported.

* - Binds to `INADDR_ANY` on the given port (host byte order converted via `htons`).

* - Throws `std::runtime_error` with `strerror(errno)` on failure.

*/

void UdpSocket::bind(uint16_t port, bool reuseport) {

    if (reuseport) {

#ifdef SO_REUSEPORT

        int one = 1;

        setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));

#endif

    }

    sockaddr_in addr{};

    addr.sin_family = AF_INET;

    addr.sin_addr.s_addr = INADDR_ANY;

    addr.sin_port = htons(port);

    if (::bind(sockfd_, (sockaddr*)&addr, sizeof(addr)) < 0)

        throw std::runtime_error("bind() failed: " + std::string(strerror(errno)));

}
 
/**

* \copydoc udp::ISocket::connect

*

* @details

* - Resolves the dotted IPv4 string with `inet_pton`.

* - Calls `::connect()` and sets `connected_ = true` on success.

* - Throws `std::runtime_error` with `strerror(errno)` on failure.

*/

void UdpSocket::connect(const std::string& ip, uint16_t port) {

    memset(&peer_, 0, sizeof(peer_));

    peer_.sin_family = AF_INET;

    inet_pton(AF_INET, ip.c_str(), &peer_.sin_addr);

    peer_.sin_port = htons(port);

    if (::connect(sockfd_, (sockaddr*)&peer_, sizeof(peer_)) < 0)

        throw std::runtime_error("connect() failed: " + std::string(strerror(errno)));

    connected_ = true;

}
 
/**

* \copydoc udp::ISocket::recv_batch

*

* @details Linux fast-path:

* - Prepares `mmsghdr`/`iovec` arrays for up to `bufs.size()` messages.

* - Calls `recvmmsg()` once; on `EAGAIN`/`EWOULDBLOCK` returns 0 (no messages).

* - On other errors returns -1; otherwise returns the number of messages received.

*

* Fallback:

* - Performs a single `recvfrom` into `bufs[0]` and returns 1 on success,

*   0 on `EAGAIN`/`EWOULDBLOCK`, or -1 on error.

*

* @note Return value is a **message count**, not bytes.

*/

ssize_t UdpSocket::recv_batch(std::vector<std::vector<uint8_t>>& bufs) {

#if defined(__linux__)

    // Use recvmmsg if available

    const size_t n = bufs.size();

    std::vector<iovec> iov(n);

    std::vector<mmsghdr> msgs(n);

    std::vector<sockaddr_in> addrs(n);

    std::vector<char> ctrl(64 * n); // ancillary control (unused), keeps API parity
 
    for (size_t i=0;i<n;i++) {

        iov[i].iov_base = bufs[i].data();

        iov[i].iov_len  = bufs[i].size();

        memset(&msgs[i], 0, sizeof(mmsghdr));

        msgs[i].msg_hdr.msg_iov    = &iov[i];

        msgs[i].msg_hdr.msg_iovlen = 1;

        msgs[i].msg_hdr.msg_name   = &addrs[i];

        msgs[i].msg_hdr.msg_namelen= sizeof(sockaddr_in);

        msgs[i].msg_hdr.msg_control= ctrl.data() + i*64;

        msgs[i].msg_hdr.msg_controllen = 64;

    }

    int r = recvmmsg(sockfd_, msgs.data(), n, 0, nullptr);

    if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;

    if (r < 0) return -1;

    return r;

#else

    // Fallback to single recvfrom

    sockaddr_in addr{};

    socklen_t alen = sizeof(addr);

    ssize_t r = recvfrom(sockfd_, bufs[0].data(), bufs[0].size(), 0, (sockaddr*)&addr, &alen);

    if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;

    if (r < 0) return -1;

    return 1;

#endif

}
 
/**

* \copydoc udp::ISocket::send_batch

*

* @details Linux fast-path:

* - Prepares `mmsghdr`/`iovec` arrays for up to `bufs.size()` messages.

* - If the socket is not `connected_`, per-call `addr` is attached to each msg.

* - Calls `sendmmsg()` once; on `EAGAIN`/`EWOULDBLOCK` returns 0, on other errors -1,

*   else the number of messages actually sent.

*

* Fallback:

* - Loops `send()` (connected) or `sendto()` (unconnected) over `bufs`.

* - Returns the **count** of successful sends.

*

* @note Return value is a **message count**, not bytes.

*/

ssize_t UdpSocket::send_batch(const std::vector<std::vector<uint8_t>>& bufs, const sockaddr_in* addr) {

#if defined(__linux__)

    const size_t n = bufs.size();

    std::vector<iovec> iov(n);

    std::vector<mmsghdr> msgs(n);

    for (size_t i=0;i<n;i++) {

        iov[i].iov_base = const_cast<uint8_t*>(bufs[i].data());

        iov[i].iov_len  = bufs[i].size();

        memset(&msgs[i], 0, sizeof(mmsghdr));

        msgs[i].msg_hdr.msg_iov    = &iov[i];

        msgs[i].msg_hdr.msg_iovlen = 1;

        if (!connected_) {

            msgs[i].msg_hdr.msg_name    = const_cast<sockaddr_in*>(addr);

            msgs[i].msg_hdr.msg_namelen = sizeof(sockaddr_in);

        }

    }

    int r = sendmmsg(sockfd_, msgs.data(), n, 0);

    if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;

    if (r < 0) return -1;

    return r;

#else

    // Fallback to single sendto/connect

    ssize_t cnt = 0;

    for (auto& b : bufs) {

        ssize_t r;

        if (connected_) r = ::send(sockfd_, b.data(), b.size(), 0);

        else            r = ::sendto(sockfd_, b.data(), b.size(), 0, (sockaddr*)addr, sizeof(sockaddr_in));

        if (r >= 0) cnt++;

    }

    return cnt;

#endif

}
 
/// \copydoc udp::ISocket::set_rcvbuf

void UdpSocket::set_rcvbuf(int bytes) {

    setsockopt(sockfd_, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes));

}
 
/// \copydoc udp::ISocket::set_sndbuf

void UdpSocket::set_sndbuf(int bytes) {

    setsockopt(sockfd_, SOL_SOCKET, SO_SNDBUF, &bytes, sizeof(bytes));

}
 
/**

* @brief Return up to `bufs.size()` preloaded datagrams into caller buffers.

*

* @details Copies from `rx_store_` into `bufs[i]` up to the minimum of source

* size and destination capacity (truncation possible, consistent with UDP recv).

* Advances an internal cursor so subsequent calls continue where the last ended.

*

* @return Number of messages copied into `bufs` (0..bufs.size()).

*/

ssize_t MockSocket::recv_batch(std::vector<std::vector<uint8_t>>& bufs) {

    size_t i=0;

    for (; i<bufs.size() && recv_cursor_ < rx_store_.size(); ++i, ++recv_cursor_) {

        auto& src = rx_store_[recv_cursor_];

        auto& dst = bufs[i];

        size_t n = std::min(dst.size(), src.size());

        std::copy(src.begin(), src.begin()+n, dst.begin());

    }

    return static_cast<ssize_t>(i);

}
 
/**

* @brief Append the provided buffers to the in-memory sent store.

*

* @details The mock does not perform real I/O and treats all buffers as

* successfully "sent". The content is copied into `tx_store_` for later

* inspection in tests.

*

* @return The number of messages recorded (equals `bufs.size()`).

*/

ssize_t MockSocket::send_batch(const std::vector<std::vector<uint8_t>>& bufs, const sockaddr_in* ) {

    for (auto& b : bufs) tx_store_.push_back(b);

    return static_cast<ssize_t>(bufs.size());

}
 
} // namespace udp

 