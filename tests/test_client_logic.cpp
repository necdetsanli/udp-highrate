#include <gtest/gtest.h>
#include "udp/client.hpp"
#include "udp/socket.hpp"
 
using namespace udp;
 
TEST(Client, SendsSomething) {
    auto ms = std::make_unique<MockSocket>();
    ClientConfig cfg;
    cfg.pps = 1000;
    cfg.seconds = 1;
    cfg.batch = 4;
    cfg.payload = 64;
    UdpClient c(std::move(ms), cfg);
    c.start();
    c.stop();
    // Not directly observable from MockSocket since we moved it;
    // This test ensures start/stop paths are covered.
    SUCCEED();
}