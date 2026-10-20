#include "client_connector.hpp"
#include "frame.hpp"
#include "messages.hpp"
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

using namespace ticklab::connection;
using namespace std::chrono_literals;

using ticklab::transport::FrameHeader;
using ticklab::transport::FrameType;

namespace
{

const TimePoint t0{};

const Endpoint kServer{ "203.0.113.1", 9000 };
const Endpoint kStranger{ "203.0.113.2", 9000 };

constexpr ClientId kAssignedId = 1;
constexpr uint64_t kAssignedToken = 0x1122334455667788;

std::vector<uint8_t> make_frame(FrameType type, uint32_t sequence, const std::vector<uint8_t>& payload = {}) {
    const FrameHeader header{ sequence, 0, type, static_cast<uint16_t>(payload.size()) };
    return ticklab::transport::build_frame(header, payload);
}

std::vector<uint8_t> make_welcome_frame(ClientId client_id, uint64_t token, uint32_t sequence = 0) {
    return make_frame(FrameType::Welcome, sequence, serialize_welcome(Welcome{ client_id, token }));
}

std::vector<uint8_t> make_reject_frame(RejectReason reason) {
    return make_frame(FrameType::Reject, 0, serialize_reject(Reject{ reason }));
}

bool read_frame(const std::vector<uint8_t>& bytes, FrameType expected,
                FrameHeader& header, std::vector<uint8_t>& payload) {
    if (!ticklab::transport::parse_frame(bytes, header, payload)) { return false; }
    return header.type == expected;
}

bool read_hello(const std::vector<uint8_t>& bytes, Hello& out, uint32_t& sequence) {
    FrameHeader header{};
    std::vector<uint8_t> payload;
    if (!read_frame(bytes, FrameType::Hello, header, payload)) { return false; }

    sequence = header.sequence;
    return parse_hello(payload, out);
}

std::optional<FrameType> frame_type(const std::vector<uint8_t>& bytes) {
    FrameHeader header{};
    std::vector<uint8_t> payload;
    if (!ticklab::transport::parse_frame(bytes, header, payload)) { return std::nullopt; }
    return header.type;
}

ClientConnector connected_connector(Identity previous = {}) {
    ClientConnector connector{ kServer, previous };
    connector.connect(t0);
    connector.on_datagram(kServer, make_welcome_frame(kAssignedId, kAssignedToken), t0);
    return connector;
}

}

TEST(ClientConnectorTest, ConnectSendsHelloForNewClient) {
    ClientConnector connector{ kServer };
    EXPECT_EQ(connector.state(), ClientState::Idle);
 
    ClientResult result = connector.connect(t0);
 
    ASSERT_EQ(result.frames.size(), 1u);
 
    Hello hello{};
    uint32_t sequence = 99;
    ASSERT_TRUE(read_hello(result.frames[0], hello, sequence));
    EXPECT_EQ(hello.magic, kProtocolMagic);
    EXPECT_EQ(hello.version, kProtocolVersion);
    EXPECT_EQ(hello.client_id, kNoClientId);
    EXPECT_EQ(hello.token, 0u);
    EXPECT_EQ(sequence, 0u); // sent before any connection exists
 
    EXPECT_TRUE(result.events.empty());
    EXPECT_EQ(connector.state(), ClientState::Connecting);
    EXPECT_EQ(connector.connection(), nullptr);
}

TEST(ClientConnectorTest, ConnectSendsHelloWithPreviousIdentity) {
    // identity saved by an earlier run
    const Identity previous{ 7, 0xAABBCCDDEEFF0011 };
    ClientConnector connector{ kServer, previous };
 
    ClientResult result = connector.connect(t0);
 
    ASSERT_EQ(result.frames.size(), 1u);
 
    Hello hello{};
    uint32_t sequence = 0;
    ASSERT_TRUE(read_hello(result.frames[0], hello, sequence));
    EXPECT_EQ(hello.client_id, previous.client_id);
    EXPECT_EQ(hello.token, previous.token);
}

TEST(ClientConnectorTest, ConnectIsIgnoredWhileConnectingOrConnected) {
    {
        ClientConnector connector{ kServer };
        connector.connect(t0);

        ClientResult result = connector.connect(t0 + 1s);

        EXPECT_TRUE(result.frames.empty());
        EXPECT_TRUE(result.events.empty());
        EXPECT_EQ(connector.state(), ClientState::Connecting);

        // the handshake clock was not restarted: giving up still happens 5 s after the first connect()
        ClientResult give_up = connector.update(t0 + 5s);
        ASSERT_EQ(give_up.events.size(), 1u);
        EXPECT_EQ(give_up.events[0], ClientEvent::GaveUp);
    }
    {
        ClientConnector connector = connected_connector();

        ClientResult result = connector.connect(t0 + 1s);

        EXPECT_TRUE(result.frames.empty());
        EXPECT_TRUE(result.events.empty());
        EXPECT_EQ(connector.state(), ClientState::Connected);
        EXPECT_NE(connector.connection(), nullptr); // the existing connection is untouched
    }
}

TEST(ClientConnectorTest, HelloIsResentAtInterval) {
    ClientConnector connector{ kServer };
    connector.connect(t0);
 
    EXPECT_TRUE(connector.update(t0 + 249ms).frames.empty());
 
    ClientResult first_resend = connector.update(t0 + 250ms);
    ASSERT_EQ(first_resend.frames.size(), 1u);
 
    Hello hello{};
    uint32_t sequence = 0;
    EXPECT_TRUE(read_hello(first_resend.frames[0], hello, sequence));
 
    // the interval restarts from the resend
    EXPECT_TRUE(connector.update(t0 + 499ms).frames.empty());
 
    ClientResult second_resend = connector.update(t0 + 500ms);
    ASSERT_EQ(second_resend.frames.size(), 1u);
    EXPECT_TRUE(read_hello(second_resend.frames[0], hello, sequence));
 
    EXPECT_EQ(connector.state(), ClientState::Connecting);
}

TEST(ClientConnectorTest, GivesUpAfterHandshakeTimeout) {
    ClientConnector connector{ kServer };
    connector.connect(t0);
 
    EXPECT_TRUE(connector.update(t0 + 4999ms).events.empty());
    EXPECT_EQ(connector.state(), ClientState::Connecting);
 
    ClientResult result = connector.update(t0 + 5s);
 
    ASSERT_EQ(result.events.size(), 1u);
    EXPECT_EQ(result.events[0], ClientEvent::GaveUp);
    EXPECT_TRUE(result.frames.empty()); // no last Hello when giving up
    EXPECT_EQ(connector.state(), ClientState::Closed);
}

TEST(ClientConnectorTest, WelcomeConnects) {
    ClientConnector connector{ kServer };
    connector.connect(t0);
 
    ClientResult result = connector.on_datagram(kServer, make_welcome_frame(kAssignedId, kAssignedToken), t0 + 10ms);
 
    ASSERT_EQ(result.events.size(), 1u);
    EXPECT_EQ(result.events[0], ClientEvent::Connected);
    EXPECT_TRUE(result.frames.empty());
 
    EXPECT_EQ(connector.state(), ClientState::Connected);
    EXPECT_EQ(connector.identity().client_id, kAssignedId);
    EXPECT_EQ(connector.identity().token, kAssignedToken);
 
    const Connection* connection = connector.connection();
    ASSERT_NE(connection, nullptr);
    EXPECT_EQ(connection->client_id(), kAssignedId);
    EXPECT_EQ(connection->token(), kAssignedToken);
    EXPECT_EQ(connection->endpoint(), kServer);
}

TEST(ClientConnectorTest, WelcomeReplacesPreviousIdentity) {
    // the server forgot ID 7 (for example, it restarted) and hands out ID 3
    ClientConnector connector{ kServer, Identity{ 7, 0xAABBCCDDEEFF0011 } };
    connector.connect(t0);
 
    const uint64_t new_token = 0x0102030405060708;
    ClientResult result = connector.on_datagram(kServer, make_welcome_frame(3, new_token), t0 + 10ms);
 
    ASSERT_EQ(result.events.size(), 1u);
    EXPECT_EQ(result.events[0], ClientEvent::Connected);
 
    EXPECT_EQ(connector.identity().client_id, 3u);
    EXPECT_EQ(connector.identity().token, new_token);
}

TEST(ClientConnectorTest, RejectServerFullClosesAndKeepsIdentity) {
    const Identity previous{ 7, 0xAABBCCDDEEFF0011 };
    ClientConnector connector{ kServer, previous };
    connector.connect(t0);
 
    ClientResult result = connector.on_datagram(kServer, make_reject_frame(RejectReason::ServerFull), t0 + 10ms);
 
    ASSERT_EQ(result.events.size(), 1u);
    EXPECT_EQ(result.events[0], ClientEvent::RejectedServerFull);
    EXPECT_TRUE(result.frames.empty());
 
    EXPECT_EQ(connector.state(), ClientState::Closed);
    EXPECT_EQ(connector.connection(), nullptr);
 
    // a later connect() can still try to reclaim it
    EXPECT_EQ(connector.identity().client_id, previous.client_id);
    EXPECT_EQ(connector.identity().token, previous.token);
}

TEST(ClientConnectorTest, RejectVersionMismatchClosesWithItsEvent) {
    ClientConnector connector{ kServer };
    connector.connect(t0);
 
    ClientResult result = connector.on_datagram(kServer, make_reject_frame(RejectReason::VersionMismatch), t0 + 10ms);
 
    ASSERT_EQ(result.events.size(), 1u);
    EXPECT_EQ(result.events[0], ClientEvent::RejectedVersionMismatch);
    EXPECT_EQ(connector.state(), ClientState::Closed);
}

TEST(ClientConnectorTest, FramesFromOtherEndpointsAreIgnored) {
    ClientConnector connector{ kServer };
    connector.connect(t0);
 
    // a perfectly valid Welcome, but not from the server
    ClientResult result = connector.on_datagram(kStranger, make_welcome_frame(kAssignedId, kAssignedToken), t0 + 10ms);
 
    EXPECT_TRUE(result.frames.empty());
    EXPECT_TRUE(result.events.empty());
    EXPECT_EQ(connector.state(), ClientState::Connecting);
    EXPECT_EQ(connector.identity().client_id, kNoClientId);
}

TEST(ClientConnectorTest, WelcomeWithClientIdZeroIsIgnored) {
    ClientConnector connector{ kServer };
    connector.connect(t0);
 
    // the server never assigns 0, so this is not a genuine Welcome
    ClientResult result = connector.on_datagram(kServer, make_welcome_frame(kNoClientId, kAssignedToken), t0 + 10ms);
 
    EXPECT_TRUE(result.events.empty());
    EXPECT_EQ(connector.state(), ClientState::Connecting);
    EXPECT_EQ(connector.connection(), nullptr);
}

TEST(ClientConnectorTest, HeartbeatAndGoodbyeWhileConnectingAreIgnored) {
    ClientConnector connector{ kServer };
    connector.connect(t0);
 
    const FrameType types[] = { FrameType::Heartbeat, FrameType::Goodbye };
 
    for (FrameType type : types) {
        ClientResult result = connector.on_datagram(kServer, make_frame(type, 0), t0 + 10ms);
 
        EXPECT_TRUE(result.frames.empty()) << "type " << static_cast<int>(type);
        EXPECT_TRUE(result.events.empty()) << "type " << static_cast<int>(type);
    }
 
    EXPECT_EQ(connector.state(), ClientState::Connecting);
}

TEST(ClientConnectorTest, FirstHeartbeatStartsClientStreamAtZero) {
    // Welcome received at t0: the connection and its heartbeat timer start there
    ClientConnector connector = connected_connector();

    EXPECT_TRUE(connector.update(t0 + 249ms).frames.empty());

    ClientResult result = connector.update(t0 + 250ms);
    ASSERT_EQ(result.frames.size(), 1u);

    FrameHeader header{};
    std::vector<uint8_t> payload;
    ASSERT_TRUE(read_frame(result.frames[0], FrameType::Heartbeat, header, payload));
    EXPECT_EQ(header.sequence, 0u); // the client's own stream; the Hello was outside it
    EXPECT_TRUE(payload.empty());
}

TEST(ClientConnectorTest, ConsecutiveHeartbeatsIncrementSequence) {
    ClientConnector connector = connected_connector();

    ClientResult first = connector.update(t0 + 250ms);
    ClientResult second = connector.update(t0 + 500ms);
    ASSERT_EQ(first.frames.size(), 1u);
    ASSERT_EQ(second.frames.size(), 1u);

    FrameHeader first_header{};
    FrameHeader second_header{};
    std::vector<uint8_t> payload;
    ASSERT_TRUE(read_frame(first.frames[0], FrameType::Heartbeat, first_header, payload));
    ASSERT_TRUE(read_frame(second.frames[0], FrameType::Heartbeat, second_header, payload));

    EXPECT_EQ(second_header.sequence, first_header.sequence + 1);
}

TEST(ClientConnectorTest, TimesOutWithoutServerTraffic) {
    ClientConnector connector = connected_connector();
 
    // still alive just before the timeout, and still heartbeating
    ClientResult before = connector.update(t0 + 1999ms);
    EXPECT_TRUE(before.events.empty());
    ASSERT_EQ(before.frames.size(), 1u);
    EXPECT_EQ(frame_type(before.frames[0]), FrameType::Heartbeat);
 
    ClientResult result = connector.update(t0 + 2s);
 
    ASSERT_EQ(result.events.size(), 1u);
    EXPECT_EQ(result.events[0], ClientEvent::TimedOut);
    EXPECT_TRUE(result.frames.empty());
 
    EXPECT_EQ(connector.state(), ClientState::Closed);
    EXPECT_EQ(connector.connection(), nullptr);
 
    // kept for the reconnect
    EXPECT_EQ(connector.identity().client_id, kAssignedId);
    EXPECT_EQ(connector.identity().token, kAssignedToken);
}

TEST(ClientConnectorTest, ServerTrafficPreventsTimeout) {
    ClientConnector connector = connected_connector();
 
    // the server's first heartbeat follows its Welcome (sequence 0)
    ClientResult received = connector.on_datagram(kServer, make_frame(FrameType::Heartbeat, 1), t0 + 1500ms);
    EXPECT_TRUE(received.frames.empty());
    EXPECT_TRUE(received.events.empty());
 
    EXPECT_TRUE(connector.update(t0 + 2s).events.empty());
    EXPECT_EQ(connector.state(), ClientState::Connected);
 
    // 2 s after the server's last frame
    ClientResult result = connector.update(t0 + 3500ms);
 
    ASSERT_EQ(result.events.size(), 1u);
    EXPECT_EQ(result.events[0], ClientEvent::TimedOut);
}

TEST(ClientConnectorTest, LateWelcomeWhileConnectedDoesNotChangeIdentity) {
    ClientConnector connector = connected_connector();
 
    // a Welcome with a different identity: not part of this handshake
    ClientResult result = connector.on_datagram(kServer, make_welcome_frame(99, 0xDEADBEEFDEADBEEF, 1), t0 + 10ms);
 
    EXPECT_TRUE(result.frames.empty());
    EXPECT_TRUE(result.events.empty());
 
    EXPECT_EQ(connector.state(), ClientState::Connected);
    EXPECT_EQ(connector.identity().client_id, kAssignedId);
    EXPECT_EQ(connector.identity().token, kAssignedToken);
 
    const Connection* connection = connector.connection();
    ASSERT_NE(connection, nullptr);
    EXPECT_EQ(connection->client_id(), kAssignedId);
    EXPECT_EQ(connection->token(), kAssignedToken);
}

TEST(ClientConnectorTest, ServerGoodbyeDisconnects) {
    ClientConnector connector = connected_connector();
 
    ClientResult result = connector.on_datagram(kServer, make_frame(FrameType::Goodbye, 1), t0 + 1s);
 
    ASSERT_EQ(result.events.size(), 1u);
    EXPECT_EQ(result.events[0], ClientEvent::Disconnected);
    EXPECT_TRUE(result.frames.empty()); // Goodbye is not acknowledged
 
    EXPECT_EQ(connector.state(), ClientState::Closed);
    EXPECT_EQ(connector.connection(), nullptr);
 
    // kept for the reconnect
    EXPECT_EQ(connector.identity().client_id, kAssignedId);
    EXPECT_EQ(connector.identity().token, kAssignedToken);
}

TEST(ClientConnectorTest, DisconnectWhileConnectedSendsGoodbye) {
    ClientConnector connector = connected_connector();
 
    // one heartbeat first, so the Goodbye has a sequence to continue from
    ClientResult heartbeat = connector.update(t0 + 250ms);
    ASSERT_EQ(heartbeat.frames.size(), 1u);
 
    FrameHeader heartbeat_header{};
    std::vector<uint8_t> payload;
    ASSERT_TRUE(read_frame(heartbeat.frames[0], FrameType::Heartbeat, heartbeat_header, payload));
 
    ClientResult result = connector.disconnect(t0 + 300ms);
 
    ASSERT_EQ(result.frames.size(), 1u);
 
    FrameHeader header{};
    ASSERT_TRUE(read_frame(result.frames[0], FrameType::Goodbye, header, payload));
    EXPECT_EQ(header.sequence, heartbeat_header.sequence + 1);
    EXPECT_TRUE(payload.empty());
 
    // leaving is the client's own decision, so there is nothing to report
    EXPECT_TRUE(result.events.empty());
 
    EXPECT_EQ(connector.state(), ClientState::Closed);
    EXPECT_EQ(connector.connection(), nullptr);
    EXPECT_EQ(connector.identity().client_id, kAssignedId);
    EXPECT_EQ(connector.identity().token, kAssignedToken);
}

TEST(ClientConnectorTest, DisconnectWhileConnectingIsSilent) {
    ClientConnector connector{ kServer };
    connector.connect(t0);
 
    ClientResult result = connector.disconnect(t0 + 100ms);
 
    // no connection exists yet, so there is nobody to say Goodbye to
    EXPECT_TRUE(result.frames.empty());
    EXPECT_TRUE(result.events.empty());
    EXPECT_EQ(connector.state(), ClientState::Closed);
 
    // the abandoned handshake doesn't retry or give up
    ClientResult later = connector.update(t0 + 5s);
    EXPECT_TRUE(later.frames.empty());
    EXPECT_TRUE(later.events.empty());
}

TEST(ClientConnectorTest, ConnectAfterCloseReclaimsIdentity) {
    ClientConnector connector = connected_connector();
 
    // the server goes silent and the connection times out
    ClientResult timeout = connector.update(t0 + 2s);
    ASSERT_EQ(timeout.events.size(), 1u);
    ASSERT_EQ(timeout.events[0], ClientEvent::TimedOut);
 
    // the caller decides to try again
    ClientResult result = connector.connect(t0 + 3s);
 
    ASSERT_EQ(result.frames.size(), 1u);
 
    Hello hello{};
    uint32_t sequence = 99;
    ASSERT_TRUE(read_hello(result.frames[0], hello, sequence));
    EXPECT_EQ(hello.client_id, kAssignedId);
    EXPECT_EQ(hello.token, kAssignedToken);
    EXPECT_EQ(sequence, 0u);
 
    EXPECT_EQ(connector.state(), ClientState::Connecting);
}