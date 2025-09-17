/**

* @file

* @brief UDP server entry point: parses CLI, starts/stops UdpServer, handles signals.

*

* @details

* Responsibilities

*  - Parse command-line options into @ref udp::ServerConfig.

*  - Construct a concrete socket (@ref udp::UdpSocket) and the @ref udp::UdpServer.

*  - Start the server worker thread, install signal handlers, and idle until termination.

*  - Perform a graceful shutdown on SIGINT/SIGTERM.

*

* CLI options

*  - `--port <p>`           : UDP listen port (default: 9000).

*  - `--batch <n>`          : Batch size for recv/send operations (default: 64).

*  - `--metrics-port <p>`   : Loopback HTTP port for /metrics (0 disables; default: 9100).

*  - `--echo`               : Echo received packets back to the sender.

*  - `--reuseport`          : Request SO_REUSEPORT (if supported by the platform).

*  - `--verbose | --quiet`  : Toggle periodic server stats logging.

*  - `--help`               : Print usage and exit.

*

* Signals & shutdown

*  - On SIGINT/SIGTERM, a global atomic flag is cleared and the main loop proceeds to

*    stop the server cleanly via @ref udp::UdpServer::stop().

*

* Exit codes

*  - `0` on normal termination.

*  - `1` if an exception occurs during setup or runtime.

*/
 
#include "udp/server.hpp"

#include "udp/socket.hpp"

#include <iostream>

#include <cstring>

#include <thread>

#include <chrono>

#include <atomic>

#include <csignal>
 
using namespace udp;
 
/**

* @var g_keepRunning

* @brief Global run flag toggled by signal handlers to request graceful shutdown.

*

* @details

* Initialized to `true` and polled by `main` while idling. When a termination

* signal arrives, the handler sets this to `false`, causing the main loop to exit.

*/

static std::atomic<bool> g_keepRunning{true};
 
/**

* @brief Signal handler for SIGINT/SIGTERM; requests graceful shutdown.

* @param /*signum*/ Unused signal number.

*

* @note The handler is async-signal-safe here because it only writes to an

*       `std::atomic<bool>` (lock-free); no I/O or allocation is performed.

*/

static void handle_signal(int) {

    g_keepRunning = false;

}
 
/**

* @brief Application entry point: configure, run, and stop the UDP server.

* @param argc Argument count.

* @param argv Argument vector.

* @return 0 on success; 1 on error.

*

* @details

* Steps:

*  1. Parse CLI options into @ref udp::ServerConfig.

*  2. Create a @ref udp::UdpSocket and pass it (via unique_ptr) to @ref udp::UdpServer.

*  3. Start the server and install signal handlers (SIGINT/SIGTERM).

*  4. Idle in a 1-second sleep loop until a termination signal arrives.

*  5. Stop the server and return success.

*

* Error handling:

*  - Any exception thrown during construction, binding, or runtime is caught,

*    printed to `stderr`, and results in exit code 1.

*/

int main(int argc, char** argv) {

    ServerConfig cfg;

    for (int i = 1; i < argc; i++) {

        if (!std::strcmp(argv[i], "--port") && i + 1 < argc) cfg.port = static_cast<uint16_t>(std::atoi(argv[++i]));

        else if (!std::strcmp(argv[i], "--batch") && i + 1 < argc) cfg.batch = std::atoi(argv[++i]);

        else if (!std::strcmp(argv[i], "--metrics-port") && i + 1 < argc) cfg.metrics_port = static_cast<uint16_t>(std::atoi(argv[++i]));

        else if (!std::strcmp(argv[i], "--echo")) cfg.echo = true;

        else if (!std::strcmp(argv[i], "--reuseport")) cfg.reuseport = true;

        else if (!std::strcmp(argv[i], "--verbose")) cfg.verbose = true;

        else if (!std::strcmp(argv[i], "--quiet")) cfg.verbose = false;

        else if (!std::strcmp(argv[i], "--help")) {

            std::cout << "udp_server --port <p> --batch <n> --metrics-port <p> [--echo] [--reuseport] [--verbose|--quiet]\n";

            return 0;

        }

    }
 
    try {

        auto sock = std::make_unique<UdpSocket>(cfg.batch);

        UdpServer server(std::move(sock), cfg);

        server.start();
 
        // Register signal handlers, then idle until a termination signal arrives.

        std::signal(SIGINT,  handle_signal);

        std::signal(SIGTERM, handle_signal);

        while (g_keepRunning) {

            std::this_thread::sleep_for(std::chrono::seconds(1));

        }

        server.stop();

        return 0;

    } catch (const std::exception& e) {

        std::cerr << "Server error: " << e.what() << "\n";

        return 1;

    }

}

 