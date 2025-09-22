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

*  - `--max-clients <n>`    : **Admission cap** for distinct clients (default: 100).

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

#include <cstdlib>  // for strtoull
 
using namespace udp;
 
// Global flag toggled by signal handlers to stop the server gracefully.

static std::atomic<bool> g_keepRunning{true};
 
static void handle_signal(int) {

    g_keepRunning = false;

}
 
int main(int argc, char** argv) {

    ServerConfig cfg;

    for (int i = 1; i < argc; i++) {

        if (!std::strcmp(argv[i], "--port") && i + 1 < argc) {

            cfg.port = static_cast<uint16_t>(std::atoi(argv[++i]));

        } else if (!std::strcmp(argv[i], "--batch") && i + 1 < argc) {

            cfg.batch = std::atoi(argv[++i]);

        } else if (!std::strcmp(argv[i], "--metrics-port") && i + 1 < argc) {

            cfg.metrics_port = static_cast<uint16_t>(std::atoi(argv[++i]));

        } else if (!std::strcmp(argv[i], "--max-clients") && i + 1 < argc) {

            cfg.max_clients = static_cast<size_t>(std::strtoull(argv[++i], nullptr, 10));

        } else if (!std::strcmp(argv[i], "--echo")) {

            cfg.echo = true;

        } else if (!std::strcmp(argv[i], "--reuseport")) {

            cfg.reuseport = true;

        } else if (!std::strcmp(argv[i], "--verbose")) {

            cfg.verbose = true;

        } else if (!std::strcmp(argv[i], "--quiet")) {

            cfg.verbose = false;

        } else if (!std::strcmp(argv[i], "--help")) {

            std::cout
<< "udp_server "
<< "--port <p> "
<< "--batch <n> "
<< "--metrics-port <p> "
<< "--max-clients <n> "
<< "[--echo] [--reuseport] [--verbose|--quiet]\n";

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

 