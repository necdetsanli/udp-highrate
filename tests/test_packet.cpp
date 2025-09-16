#include <gtest/gtest.h>
#include "udp/common.hpp"
 
using namespace udp;
 
TEST(Packet, HeaderSize) {
    EXPECT_GE(sizeof(PacketHeader), (size_t)sizeof(uint64_t)*2 + sizeof(uint32_t));
}
 
TEST(Packet, NowNsMonotonic) {
    auto a = now_ns();
    auto b = now_ns();
    EXPECT_LE(a, b);
}
 
TEST(Packet, HumanRate) {
    EXPECT_NE(human_rate(500), "");
    EXPECT_NE(human_rate(5e4), "");
    EXPECT_NE(human_rate(5e7), "");
}