#include <gtest/gtest.h>
#include "udp/socket.hpp"
 
using namespace udp;
 
TEST(MockSocket, SendAndRecv) {
    MockSocket s;
    std::vector<uint8_t> pkt(32, 0xAB);
    s.preload_recv(pkt);
 
    std::vector<std::vector<uint8_t>> bufs(1, std::vector<uint8_t>(64));
    auto r = s.recv_batch(bufs);
    EXPECT_EQ(r, 1);
 
    auto w = s.send_batch(bufs, nullptr);
    EXPECT_EQ(w, 1);
    EXPECT_EQ(s.sent_count(), 1u);
}
 