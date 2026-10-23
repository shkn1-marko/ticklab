#include "client_connector.hpp"
#include "connection_table.hpp"
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <vector>

using namespace ticklab::connection;
using namespace std::chrono_literals;

namespace
{

const TimePoint t0{};
constexpr uint64_t kSeed = 12;

const Endpoint kServer{ "203.0.113.1", 9000 };

Endpoint client_endpoint(uint16_t port) {
    return Endpoint{ "192.0.2.10", port };
}

constexpr auto kStep = 50ms;

void append(Result& into, const Result& from) {
    into.frames.insert(into.frames.end(), from.frames.begin(), from.frames.end());
    into.events.insert(into.events.end(), from.events.begin(), from.events.end());
}

void append(ClientResult& into, const ClientResult& from) {
    into.frames.insert(into.frames.end(), from.frames.begin(), from.frames.end());
    into.events.insert(into.events.end(), from.events.begin(), from.events.end());
}

Result deliver_to_server(ConnectionTable& table, const Endpoint& client, const ClientResult& sent, TimePoint now) {
    Result replies;
    for (const auto& frame : sent.frames) {
        append(replies, table.on_datagram(client, frame, now));
    }
    return replies;
}

ClientResult deliver_to_client(ClientConnector& connector, const Endpoint& client, const Result& sent, TimePoint now) {
    ClientResult replies;
    for (const OutgoingFrame& frame : sent.frames) {
        if (frame.to == client) {
            append(replies, connector.on_datagram(kServer, frame.bytes, now));
        }
    }
    return replies;
}

void connect(ConnectionTable& table, ClientConnector& connector, const Endpoint& client, TimePoint now) {
    const ClientResult hello = connector.connect(now);
    const Result welcome = deliver_to_server(table, client, hello, now);
    deliver_to_client(connector, client, welcome, now);
}

struct Log {
    std::vector<Event> server_events;
    std::vector<ClientEvent> client_events;
};

Log run(ConnectionTable& table, ClientConnector& connector, const Endpoint& client,
        TimePoint from, TimePoint to) {
    Log log;

    for (TimePoint now = from; now <= to; now += kStep) {
        Result server_side = table.update(now);
        ClientResult client_side = connector.update(now);

        append(server_side, deliver_to_server(table, client, client_side, now));
        append(client_side, deliver_to_client(connector, client, server_side, now));

        log.server_events.insert(log.server_events.end(), server_side.events.begin(), server_side.events.end());
        log.client_events.insert(log.client_events.end(), client_side.events.begin(), client_side.events.end());
    }

    return log;
}

}

TEST(ConnectionIntegrationTest, ClientConnectsToServer) {
    ConnectionTable table{ kSeed };
    ClientConnector connector{ kServer };
    const Endpoint client = client_endpoint(40000);
 
    const ClientResult hello = connector.connect(t0);
    const Result welcome = deliver_to_server(table, client, hello, t0);
    const ClientResult received = deliver_to_client(connector, client, welcome, t0);
 
    ASSERT_EQ(welcome.events.size(), 1u);
    EXPECT_EQ(welcome.events[0].type, EventType::Connected);
 
    ASSERT_EQ(received.events.size(), 1u);
    EXPECT_EQ(received.events[0], ClientEvent::Connected);
 
    EXPECT_EQ(connector.state(), ClientState::Connected);
    EXPECT_EQ(table.size(), 1u);
 
    // both sides agree on who this client is
    const Connection* server_side = table.find(client);
    ASSERT_NE(server_side, nullptr);
    EXPECT_EQ(server_side->client_id(), connector.identity().client_id);
    EXPECT_EQ(server_side->token(), connector.identity().token);
}

TEST(ConnectionIntegrationTest, HeartbeatsKeepBothSidesAlive) {
    ConnectionTable table{ kSeed };
    ClientConnector connector{ kServer };
    const Endpoint client = client_endpoint(40000);
 
    connect(table, connector, client, t0);
    ASSERT_EQ(connector.state(), ClientState::Connected);
 
    // far longer than the 2 s timeout: only the heartbeats keep the link alive
    const Log log = run(table, connector, client, t0 + kStep, t0 + 10s);
 
    EXPECT_TRUE(log.server_events.empty());
    EXPECT_TRUE(log.client_events.empty());
 
    EXPECT_EQ(connector.state(), ClientState::Connected);
    EXPECT_EQ(table.size(), 1u);
    EXPECT_NE(table.find(client), nullptr);
}

TEST(ConnectionIntegrationTest, TwoClientsGetDistinctIds) {
    ConnectionTable table{ kSeed };
    ClientConnector first{ kServer };
    ClientConnector second{ kServer };
    const Endpoint first_endpoint = client_endpoint(40001);
    const Endpoint second_endpoint = client_endpoint(40002);
 
    connect(table, first, first_endpoint, t0);
    connect(table, second, second_endpoint, t0);
 
    ASSERT_EQ(first.state(), ClientState::Connected);
    ASSERT_EQ(second.state(), ClientState::Connected);
    EXPECT_NE(first.identity().client_id, second.identity().client_id);
 
    EXPECT_EQ(table.size(), 2u);
 
    const Connection* first_on_server = table.find(first_endpoint);
    const Connection* second_on_server = table.find(second_endpoint);
    ASSERT_NE(first_on_server, nullptr);
    ASSERT_NE(second_on_server, nullptr);
    EXPECT_EQ(first_on_server->client_id(), first.identity().client_id);
    EXPECT_EQ(second_on_server->client_id(), second.identity().client_id);
}

TEST(ConnectionIntegrationTest, ClientDisconnectRemovesItFromServer) {
    ConnectionTable table{ kSeed };
    ClientConnector connector{ kServer };
    const Endpoint client = client_endpoint(40000);
 
    connect(table, connector, client, t0);
    const ClientId client_id = connector.identity().client_id;
 
    const ClientResult goodbye = connector.disconnect(t0 + 1s);
    const Result result = deliver_to_server(table, client, goodbye, t0 + 1s);
 
    ASSERT_EQ(result.events.size(), 1u);
    EXPECT_EQ(result.events[0].type, EventType::Disconnected);
    EXPECT_EQ(result.events[0].client_id, client_id);
 
    EXPECT_EQ(table.size(), 0u);
    EXPECT_EQ(connector.state(), ClientState::Closed);
}

TEST(ConnectionIntegrationTest, ServerShutdownDisconnectsClient) {
    ConnectionTable table{ kSeed };
    ClientConnector connector{ kServer };
    const Endpoint client = client_endpoint(40000);
 
    connect(table, connector, client, t0);
 
    const Result goodbye = table.close_all(t0 + 1s);
    const ClientResult result = deliver_to_client(connector, client, goodbye, t0 + 1s);
 
    ASSERT_EQ(result.events.size(), 1u);
    EXPECT_EQ(result.events[0], ClientEvent::Disconnected);
 
    EXPECT_EQ(connector.state(), ClientState::Closed);
    EXPECT_EQ(table.size(), 0u);
}

TEST(ConnectionIntegrationTest, RestartedClientReclaimsItsId) {
    ConnectionTable table{ kSeed };
    const Endpoint old_endpoint = client_endpoint(40000);
    const Endpoint new_endpoint = client_endpoint(40001);
 
    ClientConnector original{ kServer };
    connect(table, original, old_endpoint, t0);
    ASSERT_EQ(original.state(), ClientState::Connected);
 
    // what the client tool would write to its file
    const Identity saved = original.identity();
 
    // the process is killed (no Goodbye) and started again on a new port,
    // loading the saved identity; the server still holds the old connection
    ClientConnector restarted{ kServer, saved };
 
    const ClientResult hello = restarted.connect(t0 + 1s);
    const Result welcome = deliver_to_server(table, new_endpoint, hello, t0 + 1s);
    deliver_to_client(restarted, new_endpoint, welcome, t0 + 1s);
 
    ASSERT_EQ(welcome.events.size(), 1u);
    EXPECT_EQ(welcome.events[0].type, EventType::Reconnected);
 
    EXPECT_EQ(restarted.state(), ClientState::Connected);
    EXPECT_EQ(restarted.identity().client_id, saved.client_id);
 
    // still one client, now at its new endpoint
    EXPECT_EQ(table.size(), 1u);
    EXPECT_EQ(table.find(old_endpoint), nullptr);
    EXPECT_NE(table.find(new_endpoint), nullptr);
}