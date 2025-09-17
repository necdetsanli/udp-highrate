/**

* @file

* @brief Minimal `/metrics` HTTP server exposing udp::Stats (Prometheus-like text format).

*

* @details

* Purpose:

*  - Provide a tiny, dependency-free endpoint to observe counters while running E2E tests.

*  - Exports a plaintext exposition that Prometheus-compatible scrapers can read.

*

* Scope & limitations:

*  - Binds to **127.0.0.1** only (loopback) on the configured TCP port.

*  - Single-threaded accept/serve loop; one request → one response → close.

*  - No HTTP parsing beyond writing a 200 OK; ignores request headers/body.

*  - No TLS, no keep-alive, no routing; intended for local/lab use only.

*

* Concurrency:

*  - Runs in a background thread managed by @ref udp::MetricsHttpServer::start()/stop().

*  - Reads from @ref udp::Stats via lock-free getters; no additional synchronization needed.

*/
 
#include "udp/metrics_http.hpp"

#include <sys/socket.h>

#include <netinet/in.h>

#include <arpa/inet.h>

#include <unistd.h>

#include <cstring>

#include <sstream>

#include <thread>

#include <chrono>
 
namespace udp {
 
/**

* @brief Construct a metrics server bound to a specific TCP port.

* @param stats Reference to counters to expose (not owned).

* @param port  TCP port to listen on (0 disables the server).

*/

MetricsHttpServer::MetricsHttpServer(Stats& stats, uint16_t port)

: stats_(stats), port_(port) {}
 
/**

* @brief Destructor; ensures background thread is stopped and joined.

*/

MetricsHttpServer::~MetricsHttpServer() { stop(); }
 
/**

* @brief Start the `/metrics` server in a background thread (idempotent).

*

* @details If @ref port_ is 0, metrics are disabled and this is a no-op.

*/

void MetricsHttpServer::start() {

    if (port_ == 0) return;

    running_ = true;

    th_ = std::thread(&MetricsHttpServer::run, this);

}
 
/**

* @brief Request a graceful shutdown and join the background thread (idempotent).

*/

void MetricsHttpServer::stop() {

    if (th_.joinable()) {

        running_ = false;

        th_.join();

    }

}
 
/**

* @brief Render current counters into a Prometheus-like plaintext exposition.

*

* @details Metrics exported:

*  - `udp_packets_received_total` (counter)

*  - `udp_packets_sent_total` (counter)

*  - `udp_unique_clients` (gauge)

*  - `udp_rx_bytes_total` (counter)

*  - `udp_tx_bytes_total` (counter)

*

* @return Plaintext body including HELP/TYPE lines and current values.

*/

std::string MetricsHttpServer::render() {

    std::ostringstream oss;

    oss << "# HELP udp_packets_received_total Total UDP packets received\n";

    oss << "# TYPE udp_packets_received_total counter\n";

    oss << "udp_packets_received_total " << stats_.recv() << "\n";

    oss << "# HELP udp_packets_sent_total Total UDP packets sent\n";

    oss << "# TYPE udp_packets_sent_total counter\n";

    oss << "udp_packets_sent_total " << stats_.sent() << "\n";

    oss << "# HELP udp_unique_clients Unique client count\n";

    oss << "# TYPE udp_unique_clients gauge\n";

    oss << "udp_unique_clients " << stats_.unique_clients() << "\n";

    oss << "# HELP udp_rx_bytes_total Total received bytes\n";

    oss << "# TYPE udp_rx_bytes_total counter\n";

    oss << "udp_rx_bytes_total " << stats_.rx_bytes() << "\n";

    oss << "# HELP udp_tx_bytes_total Total sent bytes\n";

    oss << "# TYPE udp_tx_bytes_total counter\n";

    oss << "udp_tx_bytes_total " << stats_.tx_bytes() << "\n";

    return oss.str();

}
 
/**

* @brief Accept loop: bind/listen on 127.0.0.1:port_, serve a single 200 OK per connection.

*

* @details

* Socket setup:

*  - Creates a TCP socket, enables `SO_REUSEADDR` (and `SO_REUSEPORT` if available).

*  - Binds to loopback (`127.0.0.1`) only; listens with a small backlog.

*

* Loop semantics:

*  - While @ref running_ is true: `accept()` one client; on failure sleeps briefly

*    (50 ms) and retries.

*  - For each accepted client:

*     1. Build the metrics body via @ref render().

*     2. Write a minimal HTTP/1.1 200 OK response with `Content-Type: text/plain`

*        and `Content-Length`.

*     3. Close the client socket.

*

* Shutdown:

*  - When @ref running_ becomes false, closes the listening socket and exits.

*/

void MetricsHttpServer::run() {

    int s = ::socket(AF_INET, SOCK_STREAM, 0);

    int one = 1;

    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

#ifdef SO_REUSEPORT

    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));

#endif

    sockaddr_in addr{};

    addr.sin_family = AF_INET;

    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    addr.sin_port = htons(port_);

    bind(s, (sockaddr*)&addr, sizeof(addr));

    listen(s, 8);

    while (running_) {

        sockaddr_in peer{};

        socklen_t plen=sizeof(peer);

        int c = accept(s, (sockaddr*)&peer, &plen);

        if (c < 0) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue; }

        std::string body = render();

        std::ostringstream resp;

        resp << "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " << body.size()
<< "\r\nConnection: close\r\n\r\n" << body;

        auto sstr = resp.str();

        (void)send(c, sstr.data(), sstr.size(), 0);

        close(c);

    }

    close(s);

}
 
} // namespace udp

 