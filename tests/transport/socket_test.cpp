#include "socket.hpp"
#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace ticklab::transport;

TEST(SocketTest, ReceiveReturnsImmediatelyWhenNothingIsWaiting) {
    auto sock = UdpSocket::create();
    ASSERT_TRUE(sock.has_value());
    ASSERT_EQ(sock->bind(0), SocketResult::Success);

    std::vector<uint8_t> out;
    std::string from_address;
    uint16_t from_port = 0;

    // A blocking socket would hand here; CTest timeout = 10 seconds
    // turns that into failure.
    EXPECT_FALSE(sock->receive(out, from_address, from_port));
}

TEST(SocketTest, SendToRejectsHostname) {
    auto sock = UdpSocket::create();
    ASSERT_TRUE(sock.has_value());

    std::vector<uint8_t> data{ 1, 2, 3 };

    EXPECT_EQ(sock->send_to("localhost", 9, data), SocketResult::SendFailed);
}

TEST(SocketTest, SendToRejectsMalformedAddress) {
    auto sock = UdpSocket::create();
    ASSERT_TRUE(sock.has_value());

    std::vector<uint8_t> data{ 1, 2, 3 };

    EXPECT_EQ(sock->send_to("999.1.1.1", 9, data), SocketResult::SendFailed);
    EXPECT_EQ(sock->send_to("1.2.3", 9, data), SocketResult::SendFailed);
    EXPECT_EQ(sock->send_to("", 9, data), SocketResult::SendFailed);
}

TEST(SocketTest, BindFailsWhenPortAlreadyInUse) {
    auto first = UdpSocket::create();
    ASSERT_TRUE(first.has_value());
    ASSERT_EQ(first->bind(0), SocketResult::Success);

    auto port = first->local_port();
    ASSERT_TRUE(port.has_value());

    auto second = UdpSocket::create();
    ASSERT_TRUE(second.has_value());

    EXPECT_EQ(second->bind(*port), SocketResult::BindFailed);
}