/**

* @file

* @brief UDP client entry point: parses CLI, configures UdpClient, and runs paced sending.

*

* @details

* Responsibilities

*  - Parse command-line options into @ref udp::ClientConfig.

*  - Construct a concrete socket (@ref udp::UdpSocket) and the @ref udp::UdpClient.

*  - Start the client worker thread and wait for natural completion (based on `--seconds`).

*

* CLI options

*  - `--server <ip>`  : Destination IPv4 address (default: 127.0.0.1).

*  - `--port <p>`     : UDP destination port (default: 9000).

*  - `--pps <n>`      : Target packets-per-second (per client).

*  - `--seconds <n>`  : Duration to run before exiting.

*  - `--payload <n>`  : Bytes per packet (minimum `sizeof(PacketHeader)` is enforced in code).

*  - `--batch <n>`    : Messages per `send_batch` call (amortizes syscalls).

*  - `--id <n>`       : Client identifier for verbose logs.

*  - `--verbose`      : Print periodic transmit stats (approx once per second).

*  - `--help`         : Print usage and exit.

*

* Exit codes

*  - `0` on normal completion after the configured duration.

*  - `1` on setup/runtime error (exception thrown).

*/
 
#include "udp/client.hpp"

#include "udp/socket.hpp"

#include <iostream>

#include <cstring>
 
using namespace udp;
 
/**

* @brief Application entry point: configure and run the paced UDP client.

*

* @param argc Argument count.

* @param argv Argument vector.

* @return 0 on success; 1 on error.

*

* @details

* Steps:

*  1. Parse CLI flags into @ref udp::ClientConfig.

*  2. Create a @ref udp::UdpSocket with the batch hint and pass it (via `std::unique_ptr`)

*     to @ref udp::UdpClient.

*  3. Start the client's worker thread and `join()` to wait for natural completion

*     (driven by `--seconds`).

*  4. Catch and report any exception to `stderr`, returning a non-zero exit code.

*/

int main(int argc, char** argv) {

    ClientConfig cfg;

    for (int i=1;i<argc;i++){

        if (!strcmp(argv[i],"--server") && i+1<argc) cfg.server_ip = argv[++i];

        else if (!strcmp(argv[i],"--port") && i+1<argc) cfg.port = (uint16_t)atoi(argv[++i]);

        else if (!strcmp(argv[i],"--pps") && i+1<argc) cfg.pps = (uint64_t)atoll(argv[++i]);

        else if (!strcmp(argv[i],"--seconds") && i+1<argc) cfg.seconds = atoi(argv[++i]);

        else if (!strcmp(argv[i],"--payload") && i+1<argc) cfg.payload = atoi(argv[++i]);

        else if (!strcmp(argv[i],"--batch") && i+1<argc) cfg.batch = atoi(argv[++i]);

        else if (!strcmp(argv[i],"--id") && i+1<argc) cfg.id = atoi(argv[++i]);

        else if (!strcmp(argv[i],"--verbose")) cfg.verbose = true;

        else if (!strcmp(argv[i],"--help")) {

            std::cout << "udp_client --server <ip> --port <p> --pps <n> --seconds <n> --payload <n> --batch <n> --id <n> [--verbose]\n";

            return 0;

        }

    }

    try {

        auto sock = std::make_unique<UdpSocket>(cfg.batch);

        UdpClient client(std::move(sock), cfg);

        client.start();

        // Wait for the client run loop to finish based on --seconds.

        client.join();

        return 0;

    } catch (const std::exception& e) {

        std::cerr << "Client error: " << e.what() << "\n";

        return 1;

    }

}

 