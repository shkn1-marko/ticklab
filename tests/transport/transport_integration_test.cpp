#include "frame.hpp"
#include "socket.hpp"
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

using namespace ticklab::transport;

namespace {

constexpr auto kReceiveTimeout = std::chrono::seconds(1);

bool receive_with_deadline(UdpSocket& socket,
                           std::vector<uint8_t>& out,
                           std::string& from_address,
                           uint16_t& from_port) {
    const auto deadline = std::chrono::steady_clock::now() + kReceiveTimeout;

    while (std::chrono::steady_clock::now() < deadline) {
        if (socket.receive(out, from_address, from_port)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return false;
}

}

TEST(TransportIntegrationTest, FrameSurvivesRoundTripThroughRealSockets) {
    auto sender = UdpSocket::create();
    auto receiver = UdpSocket::create();
    ASSERT_TRUE(sender.has_value());
    ASSERT_TRUE(receiver.has_value());

    ASSERT_EQ(receiver->bind(0), SocketResult::Success);
    auto port = receiver->local_port();
    ASSERT_TRUE(port.has_value());

    std::vector<uint8_t> payload{ 'h', 'e', 'l', 'l', 'o' };
    FrameHeader header{ 7, 200, FrameType::Data, static_cast<uint16_t>(payload.size()) };

    ASSERT_EQ(sender->send_to("127.0.0.1", *port, build_frame(header, payload)),
              SocketResult::Success);

    std::vector<uint8_t> raw;
    std::string from_address;
    uint16_t from_port = 0;
    ASSERT_TRUE(receive_with_deadline(*receiver, raw, from_address, from_port));

    FrameHeader parsed_header{};
    std::vector<uint8_t> parsed_payload;
    ASSERT_TRUE(parse_frame(raw, parsed_header, parsed_payload));

    EXPECT_EQ(parsed_header.sequence, header.sequence);
    EXPECT_EQ(parsed_header.tick, header.tick);
    EXPECT_EQ(parsed_header.type, header.type);
    EXPECT_EQ(parsed_header.payload_length, header.payload_length);
    EXPECT_EQ(parsed_payload, payload);
}