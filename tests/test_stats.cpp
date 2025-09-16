#include <gtest/gtest.h>
#include "udp/stats.hpp"
 
using namespace udp;
 
TEST(Stats, BasicCounters) {
    Stats s;
    s.inc_recv(10);
    s.inc_sent(5);
    s.add_rx_bytes(100);
    s.add_tx_bytes(200);
    EXPECT_EQ(s.recv(), 10u);
    EXPECT_EQ(s.sent(), 5u);
    EXPECT_EQ(s.rx_bytes(), 100u);
    EXPECT_EQ(s.tx_bytes(), 200u);
}
 
TEST(Stats, Clients) {
    Stats s;
    s.note_client(0x7f000001, 9000);
    s.note_client(0x7f000001, 9000);
    s.note_client(0x7f000001, 9001);
    EXPECT_EQ(s.unique_clients(), 2u);
    EXPECT_NE(s.to_string().size(), 0u);
}