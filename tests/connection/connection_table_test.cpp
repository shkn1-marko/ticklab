#include "connection_table.hpp"
#include "frame.hpp"
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <vector>

using namespace ticklab::connection;
using namespace std::chrono_literals;

using ticklab::transport::FrameHeader;
using ticklab::transport::FrameType;

namespace
{

const TimePoint t0{};

constexpr uint64_t kSeed = 12;

Endpoint client_endpoint(uint16_t port) {
    return Endpoint{ "192.0.2.10", port };
}

std::vector<uint8_t> make_hello_frame(uint32_t client_id = kNoClientId,
                                      uint64_t token = 0,
                                      uint16_t version = kProtocolVersion,
                                      uint32_t magic = kProtocolMagic) {
    const std::vector<uint8_t> payload = serialize_hello(Hello{ magic, version, client_id, token });
    const FrameHeader header{ 0, 0, FrameType::Hello, static_cast<uint16_t>(payload.size()) };
    return ticklab::transport::build_frame(header, payload);
}

bool read_frame(const OutgoingFrame& frame, FrameType expected,
                FrameHeader& header, std::vector<uint8_t>& payload) {
    if (!ticklab::transport::parse_frame(frame.bytes, header, payload)) { return false; }
    return header.type == expected;
}

bool read_welcome(const OutgoingFrame& frame, Welcome& out, uint32_t& sequence) {
    FrameHeader header{};
    std::vector<uint8_t> payload;
    if (!read_frame(frame, FrameType::Welcome, header, payload)) { return false; }

    sequence = header.sequence;
    return parse_welcome(payload, out);
}

bool read_reject(const OutgoingFrame& frame, Reject& out, uint32_t& sequence) {
    FrameHeader header{};
    std::vector<uint8_t> payload;
    if (!read_frame(frame, FrameType::Reject, header, payload)) { return false; }

    sequence = header.sequence;
    return parse_reject(payload, out);
}

}

TEST(ConnectionTableTest, HelloFromUnknownEndpointCreatesConnection) {
    ConnectionTable table{ kSeed };
    const Endpoint from = client_endpoint(40000);

    Result result = table.on_datagram(from, make_hello_frame(), t0);

    ASSERT_EQ(result.frames.size(), 1u);
    EXPECT_EQ(result.frames[0].to, from);

    Welcome welcome{};
    uint32_t sequence = 0;
    ASSERT_TRUE(read_welcome(result.frames[0], welcome, sequence));
    EXPECT_EQ(welcome.client_id, 1u);
    EXPECT_NE(welcome.token, 0u);
    EXPECT_EQ(sequence, 0u);

    ASSERT_EQ(result.events.size(), 1u);
    EXPECT_EQ(result.events[0].type, EventType::Connected);
    EXPECT_EQ(result.events[0].client_id, 1u);
    EXPECT_EQ(result.events[0].endpoint, from);

    EXPECT_EQ(table.size(), 1u);
    const Connection* connection = table.find(from);
    ASSERT_NE(connection, nullptr);
    EXPECT_EQ(connection->client_id(), 1u);
    EXPECT_EQ(connection->token(), welcome.token);
}

TEST(ConnectionTableTest, ClientIdsStartAtOneAndIncrement) {
    ConnectionTable table{ kSeed };

    for (uint32_t expected_id = 1; expected_id <=3; ++expected_id) {
        const Endpoint from = client_endpoint(static_cast<uint16_t>(40000 + expected_id));

        Result result = table.on_datagram(from, make_hello_frame(), t0);

        ASSERT_EQ(result.frames.size(), 1u) << "client " << expected_id;

        Welcome welcome{};
        uint32_t sequence = 0;
        ASSERT_TRUE(read_welcome(result.frames[0], welcome, sequence)) << "client " << expected_id;
        EXPECT_EQ(welcome.client_id, expected_id);
    }

    EXPECT_EQ(table.size(), 3u);
}

TEST(ConnectionTableTest, RepeatedHelloFromSameEndpointResendsSameWelcome) {
    ConnectionTable table{ kSeed };
    const Endpoint from = client_endpoint(40000);

    Result first = table.on_datagram(from, make_hello_frame(), t0);
    ASSERT_EQ(first.frames.size(), 1u);

    Welcome first_welcome{};
    uint32_t first_sequence = 0;
    ASSERT_TRUE(read_welcome(first.frames[0], first_welcome, first_sequence));

    Result second = table.on_datagram(from, make_hello_frame(), t0 + 250ms);

    ASSERT_EQ(second.frames.size(), 1u);
    EXPECT_EQ(second.frames[0].to, from);

    Welcome second_welcome{};
    uint32_t second_sequence = 0;
    ASSERT_TRUE(read_welcome(second.frames[0], second_welcome, second_sequence));

    EXPECT_EQ(second_welcome.client_id, first_welcome.client_id);
    EXPECT_EQ(second_welcome.token, first_welcome.token);
    EXPECT_EQ(second_sequence, first_sequence + 1);

    EXPECT_TRUE(second.events.empty());
    EXPECT_EQ(table.size(), 1u);
}

TEST(ConnectionTableTest, NonHelloFromUnknownEndpointIsIgnored) {
    ConnectionTable table{ kSeed };
    const Endpoint unknown = client_endpoint(40000);

    const FrameType types[] = { FrameType::Data, FrameType::Heartbeat, FrameType::Goodbye };

    for (FrameType type : types) {
        const FrameHeader header{ 0, 0, type, 0 };
        const std::vector<uint8_t> frame = ticklab::transport::build_frame(header, {});

        Result result = table.on_datagram(unknown, frame, t0);

        EXPECT_TRUE(result.frames.empty()) << "type " << static_cast<int>(type);
        EXPECT_TRUE(result.events.empty()) << "type " << static_cast<int>(type);
    }

    EXPECT_EQ(table.size(), 0u);
}

TEST(ConnectionTableTest, FifthClientIsRejectedWhenFull) {
    ConnectionTable table{ kSeed };

    for (uint16_t port = 40001; port <= 40000 + kMaxClients; ++port) {
        table.on_datagram(client_endpoint(port), make_hello_frame(), t0);
    }
    ASSERT_EQ(table.size(), kMaxClients);

    const Endpoint fifth = client_endpoint(40100);
    Result result = table.on_datagram(fifth, make_hello_frame(), t0);

    ASSERT_EQ(result.frames.size(), 1u);
    EXPECT_EQ(result.frames[0].to, fifth);

    Reject reject{};
    uint32_t sequence = 1;
    ASSERT_TRUE(read_reject(result.frames[0], reject, sequence));
    EXPECT_EQ(reject.reason, RejectReason::ServerFull);
    EXPECT_EQ(sequence, 0u);

    ASSERT_EQ(result.events.size(), 1u);
    EXPECT_EQ(result.events[0].type, EventType::Rejected);
    EXPECT_EQ(result.events[0].client_id, kNoClientId);
    EXPECT_EQ(result.events[0].endpoint, fifth);

    EXPECT_EQ(table.size(), kMaxClients);
    EXPECT_EQ(table.find(fifth), nullptr);
}