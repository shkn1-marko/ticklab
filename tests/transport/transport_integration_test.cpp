#include "frame.hpp"
#include "socket.hpp"
#include "sequence_tracker.hpp"
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

TEST(TransportIntegrationTest, ReceiverSeesSenderAddressAndPort) {
    auto sender = UdpSocket::create();
    auto receiver = UdpSocket::create();
    ASSERT_TRUE(sender.has_value());
    ASSERT_TRUE(receiver.has_value());

    ASSERT_EQ(receiver->bind(0), SocketResult::Success);
    auto receiver_port = receiver->local_port();
    ASSERT_TRUE(receiver_port.has_value());

    std::vector<uint8_t> data{ 1, 2, 3 };
    ASSERT_EQ(sender->send_to("127.0.0.1", *receiver_port, data), SocketResult::Success);

    auto sender_port = sender->local_port();
    ASSERT_TRUE(sender_port.has_value());

    std::vector<uint8_t> raw;
    std::string from_address;
    uint16_t from_port = 0;
    ASSERT_TRUE(receive_with_deadline(*receiver, raw, from_address, from_port));

    EXPECT_EQ(raw, data);
    EXPECT_EQ(from_address, "127.0.0.1");
    EXPECT_EQ(from_port, *sender_port);
}

TEST(TransportIntegrationTest, BurstOfFramesAllArrive) {
    constexpr uint32_t kFrameCount = 50;

    auto sender = UdpSocket::create();
    auto receiver = UdpSocket::create();
    ASSERT_TRUE(sender.has_value());
    ASSERT_TRUE(receiver.has_value());

    ASSERT_EQ(receiver->bind(0), SocketResult::Success);
    auto port = receiver->local_port();
    ASSERT_TRUE(port.has_value());

    std::vector<uint8_t> empty_payload;

    for (uint32_t sequence = 0; sequence < kFrameCount; ++sequence) {
        FrameHeader header { sequence, sequence, FrameType::Data, 0 };
        ASSERT_EQ(sender->send_to("127.0.0.1", *port, build_frame(header, empty_payload)),
                  SocketResult::Success)
            << "sequence " << sequence;
    }

    std::vector<bool> seen(kFrameCount, false);

    for (uint32_t i = 0; i < kFrameCount; ++i) {
        std::vector<uint8_t> raw;
        std::string from_address;
        uint16_t from_port = 0;
        ASSERT_TRUE(receive_with_deadline(*receiver, raw, from_address, from_port))
            << "datagram " << i << " of " << kFrameCount;

        FrameHeader parsed_header{};
        std::vector<uint8_t> parsed_payload;
        ASSERT_TRUE(parse_frame(raw, parsed_header, parsed_payload));

        ASSERT_LT(parsed_header.sequence, kFrameCount);
        EXPECT_FALSE(seen[parsed_header.sequence]) << "duplicate sequence " << parsed_header.sequence;
        seen[parsed_header.sequence] = true;
    }

    for (uint32_t sequence = 0; sequence < kFrameCount; ++sequence) {
        EXPECT_TRUE(seen[sequence]) << "sequence " << sequence << " never arrived";
    }
}

TEST(TransportIntegrationTest, MalformedFramesArriveIntactAndAreRejectedByParser) {
    auto sender = UdpSocket::create();
    auto receiver = UdpSocket::create();
    ASSERT_TRUE(sender.has_value());
    ASSERT_TRUE(receiver.has_value());

    ASSERT_EQ(receiver->bind(0), SocketResult::Success);
    auto port = receiver->local_port();
    ASSERT_TRUE(port.has_value());

    std::vector<uint8_t> too_short(kHeaderSize - 1, 0);

    std::vector<uint8_t> three_bytes{ 'a', 'b', 'c' };
    std::vector<uint8_t> lying_length =
        build_frame(FrameHeader{ 1, 1, FrameType::Data, 999 }, three_bytes);

    std::vector<uint8_t> ten_bytes(10, 0xAB);
    std::vector<uint8_t> trailing_bytes =
        build_frame(FrameHeader{ 1, 1, FrameType::Data, 3 }, ten_bytes);

    const std::vector<std::vector<uint8_t>> cases{ too_short, lying_length, trailing_bytes };

    for (std::size_t i = 0; i < cases.size(); ++i) {
        ASSERT_EQ(sender->send_to("127.0.0.1", *port, cases[i]), SocketResult::Success)
            << "case " << i;

        std::vector<uint8_t> raw;
        std::string from_address;
        uint16_t from_port = 0;
        ASSERT_TRUE(receive_with_deadline(*receiver, raw, from_address, from_port))
            << "case " << i;

        // socket layer delivers what arrived; parser rejects malformed frames
        EXPECT_EQ(raw, cases[i]) << "case " << i;

        FrameHeader parsed_header{};
        std::vector<uint8_t> parsed_payload;
        EXPECT_FALSE(parse_frame(raw, parsed_header, parsed_payload)) << "case " << i;
    }
}

TEST(TransportIntegrationTest, DatagramAtMaximumSizeIsDelivered) {
    auto sender = UdpSocket::create();
    auto receiver = UdpSocket::create();
    ASSERT_TRUE(sender.has_value());
    ASSERT_TRUE(receiver.has_value());

    ASSERT_EQ(receiver->bind(0), SocketResult::Success);
    auto port = receiver->local_port();
    ASSERT_TRUE(port.has_value());

    std::vector<uint8_t> at_limit(kMaxDatagramSize, 0x5A);
    ASSERT_EQ(sender->send_to("127.0.0.1", *port, at_limit), SocketResult::Success);

    std::vector<uint8_t> raw;
    std::string from_address;
    uint16_t from_port = 0;
    ASSERT_TRUE(receive_with_deadline(*receiver, raw, from_address, from_port));

    EXPECT_EQ(raw.size(), kMaxDatagramSize);
    EXPECT_EQ(raw, at_limit);
}

TEST(TransportIntegrationTest, OversizedDatagramsAreDroppedAndNextFrameStillArrives) {
    auto sender = UdpSocket::create();
    auto receiver = UdpSocket::create();
    ASSERT_TRUE(sender.has_value());
    ASSERT_TRUE(receiver.has_value());

    ASSERT_EQ(receiver->bind(0), SocketResult::Success);
    auto port = receiver->local_port();
    ASSERT_TRUE(port.has_value());

    std::vector<uint8_t> one_byte_over(kMaxDatagramSize + 1, 0xAA);
    std::vector<uint8_t> far_over(kMaxDatagramSize * 2, 0xBB);

    std::vector<uint8_t> payload{ 'o', 'k' };
    std::vector<uint8_t> valid_frame =
        build_frame(FrameHeader{ 1, 1, FrameType::Data, 2 }, payload);

    ASSERT_EQ(sender->send_to("127.0.0.1", *port, one_byte_over), SocketResult::Success);
    ASSERT_EQ(sender->send_to("127.0.0.1", *port, far_over), SocketResult::Success);
    ASSERT_EQ(sender->send_to("127.0.0.1", *port, valid_frame), SocketResult::Success);

    std::vector<uint8_t> raw;
    std::string from_address;
    uint16_t from_port = 0;
    ASSERT_TRUE(receive_with_deadline(*receiver, raw, from_address, from_port));

    EXPECT_EQ(raw, valid_frame);

    FrameHeader parsed_header{};
    std::vector<uint8_t> parsed_payload;
    ASSERT_TRUE(parse_frame(raw, parsed_header, parsed_payload));
    EXPECT_EQ(parsed_payload, payload);

    EXPECT_FALSE(receiver->receive(raw, from_address, from_port));
}

TEST(TransportIntegrationTest, SequenceTrackerReportsGapInFramesReceivedOverSockets) {
    auto sender = UdpSocket::create();
    auto receiver = UdpSocket::create();
    ASSERT_TRUE(sender.has_value());
    ASSERT_TRUE(receiver.has_value());

    ASSERT_EQ(receiver->bind(0), SocketResult::Success);
    auto port = receiver->local_port();
    ASSERT_TRUE(port.has_value());

    SequenceTracker tracker;
    std::vector<uint8_t> empty_payload;
    std::vector<Observation> observations;

    // 6 is never sent, the tracker must report 6 missing
    const uint32_t sequences[] = { 4, 5, 7 };

    for (uint32_t sequence : sequences) {
        FrameHeader header{ sequence, 0, FrameType::Data, 0 };
        ASSERT_EQ(sender->send_to("127.0.0.1", *port, build_frame(header, empty_payload)),
                  SocketResult::Success)
            << "sequence " << sequence;

        std::vector<uint8_t> raw;
        std::string from_address;
        uint16_t from_port = 0;
        ASSERT_TRUE(receive_with_deadline(*receiver, raw, from_address, from_port))
            << "sequence " << sequence;

        FrameHeader parsed_header{};
        std::vector<uint8_t> parsed_payload;
        ASSERT_TRUE(parse_frame(raw, parsed_header, parsed_payload));

        observations.push_back(tracker.observe(parsed_header.sequence));
    }

    ASSERT_EQ(observations.size(), 3u);
    EXPECT_EQ(observations[0].status, SequenceStatus::InOrder);
    EXPECT_EQ(observations[1].status, SequenceStatus::InOrder);
    EXPECT_EQ(observations[2].status, SequenceStatus::Gap);
    EXPECT_EQ(observations[2].first_missing, 6u);
    EXPECT_EQ(observations[2].missing_count, 1u);
}