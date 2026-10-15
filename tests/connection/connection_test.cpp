#include "connection.hpp"
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>

using namespace ticklab::connection;
using namespace std::chrono_literals;

namespace
{

// fixed starting point
const TimePoint t0{};

const Endpoint kEndpoint{ "192.0.2.10", 40000 };

}

TEST(ConnectionTest, ConstructorStoresIdentity) {
    Connection connection{ 7, 0x1122334455667788, kEndpoint, t0 };

    EXPECT_EQ(connection.client_id(), 7u);
    EXPECT_EQ(connection.token(), 0x1122334455667788u);
    EXPECT_EQ(connection.endpoint(), kEndpoint);
}

TEST(ConnectionTest, HeartbeatNotDueBeforeInterval) {
    Connection connection{ 1, 1, kEndpoint, t0 };

    EXPECT_FALSE(connection.should_send_heartbeat(t0 + 249ms));
}

TEST(ConnectionTest, HeartbeatDueAtInterval) {
    Connection connection{ 1, 1, kEndpoint, t0 };

    EXPECT_TRUE(connection.should_send_heartbeat(t0 + 250ms));
}

TEST(ConnectionTest, NotTimedOutBeforeTimeout) {
    Connection connection{ 1, 1, kEndpoint, t0 };

    EXPECT_FALSE(connection.is_timed_out(t0 + 1999ms));
}

TEST(ConnectionTest, TimedOutAtTimeout) {
    Connection connection{ 1, 1, kEndpoint, t0 };

    EXPECT_TRUE(connection.is_timed_out(t0 + 2s));
}

TEST(ConnectionTest, SendingResetsHeartbeatTimer) {
    Connection connection{ 1, 1, kEndpoint, t0 };

    connection.next_sequence(t0 + 200ms);

    // any frame sent counts; the interval restarts from the send
    EXPECT_FALSE(connection.should_send_heartbeat(t0 + 300ms));
    EXPECT_TRUE(connection.should_send_heartbeat(t0 + 450ms));
}

TEST(ConnectionTest, ReceivingResetsTimeout) {
    Connection connection{ 1, 1, kEndpoint, t0 };

    connection.on_frame_received(0, t0 + 1s);

    EXPECT_FALSE(connection.is_timed_out(t0 + 2s));
    EXPECT_TRUE(connection.is_timed_out(t0 + 3s));
}

TEST(ConnectionTest, SendingDoesNotResetTimeout) {
    Connection connection{ 1, 1, kEndpoint, t0 };

    connection.next_sequence(t0 + 1500ms);

    EXPECT_TRUE(connection.is_timed_out(t0 + 2s));
}

TEST(ConnectionTest, ReceivingDoesNotResetHeartbeatTimer) {
    Connection connection{ 1, 1, kEndpoint, t0 };

    connection.on_frame_received(0, t0 + 200ms);

    EXPECT_TRUE(connection.should_send_heartbeat(t0 + 250ms));
}

TEST(ConnectionTest, OutgoingSequenceStartsAtZeroAndIncrements) {
    Connection connection{ 1, 1, kEndpoint, t0 };

    EXPECT_EQ(connection.next_sequence(t0), 0u);
    EXPECT_EQ(connection.next_sequence(t0), 1u);
    EXPECT_EQ(connection.next_sequence(t0), 2u);
}

TEST(ConnectionTest, IncomingObservationsComeFromTracker) {
    using ticklab::transport::Observation;
    using ticklab::transport::SequenceStatus;

    Connection connection{ 1, 1, kEndpoint, t0 };

    EXPECT_EQ(connection.on_frame_received(0, t0).status, SequenceStatus::InOrder);
    EXPECT_EQ(connection.on_frame_received(1, t0).status, SequenceStatus::InOrder);

    // 0, 1, 3; 2 did not arrive
    Observation gap = connection.on_frame_received(3, t0);

    EXPECT_EQ(gap.status, SequenceStatus::Gap);
    EXPECT_EQ(gap.first_missing, 2u);
    EXPECT_EQ(gap.missing_count, 1u);
}

TEST(ConnectionTest, NewConnectionStartsFreshSequences) {
    using ticklab::transport::SequenceStatus;

    Connection old_connection{ 1, 1, kEndpoint, t0 };

    for (uint32_t sequence = 0; sequence < 5; ++sequence) {
        old_connection.next_sequence(t0);
        old_connection.on_frame_received(sequence, t0);
    }

    Connection new_connection{ 1, 1, kEndpoint, t0 };

    EXPECT_EQ(new_connection.next_sequence(t0), 0u);

    EXPECT_EQ(old_connection.on_frame_received(0, t0).status, SequenceStatus::Duplicate);
    EXPECT_EQ(new_connection.on_frame_received(0, t0).status, SequenceStatus::InOrder);
}

TEST(ConnectionTest, EndpointEqualityComparesAddressAndPort) {
    const Endpoint endpoint{ "192.0.2.10", 40000 };

    EXPECT_EQ(endpoint, (Endpoint{ "192.0.2.10", 40000 }));
    EXPECT_NE(endpoint, (Endpoint{ "192.0.2.11", 40000 }));
    EXPECT_NE(endpoint, (Endpoint{ "192.0.2.10", 40001 }));
}