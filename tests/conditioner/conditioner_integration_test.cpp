#include "client_connector.hpp"
#include "conditioner.hpp"
#include "connection_table.hpp"
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <vector>

using namespace ticklab::connection;
using ticklab::conditioner::Conditioner;
using ticklab::conditioner::Config;
using ticklab::conditioner::Datagram;
using namespace std::chrono_literals;

namespace
{

const TimePoint t0{};
constexpr uint64_t kTableSeed = 12;

const Endpoint kServer{ "203.0.113.1", 9000 };
const Endpoint kClient{ "192.0.2.10", 40000 };

constexpr auto kStep = 1ms;

struct Link {
    Conditioner up;   // client -> server
    Conditioner down; // server -> client
};

Link make_link(const Config& config, uint64_t seed) {
    return Link{ Conditioner{ config, seed }, Conditioner{ config, seed + 1 } };
}

struct Log {
    std::vector<Event> server_events;
    std::vector<ClientEvent> client_events;
};

void send_up(Link& link, const ClientResult& from_client, TimePoint now) {
    for (const auto& frame : from_client.frames) { link.up.send(frame, now); }
}

void send_down(Link& link, const Result& from_server, TimePoint now) {
    for (const OutgoingFrame& frame : from_server.frames) {
        if (frame.to == kClient) { link.down.send(frame.bytes, now); }
    }
}

// Same loop as the server and client tools, but every frame crosses the link instead of a socket
void run(ConnectionTable& table, ClientConnector& connector, Link& link,
         TimePoint from, TimePoint to, Log& log) {
    for (TimePoint now = from; now <= to; now += kStep) {
        Result server_side = table.update(now);
        ClientResult client_side = connector.update(now);

        for (const Datagram& datagram : link.up.poll(now)) {
            const Result reply = table.on_datagram(kClient, datagram, now);
            server_side.frames.insert(server_side.frames.end(), reply.frames.begin(), reply.frames.end());
            server_side.events.insert(server_side.events.end(), reply.events.begin(), reply.events.end());
        }

        for (const Datagram& datagram : link.down.poll(now)) {
            const ClientResult reply = connector.on_datagram(kServer, datagram, now);
            client_side.frames.insert(client_side.frames.end(), reply.frames.begin(), reply.frames.end());
            client_side.events.insert(client_side.events.end(), reply.events.begin(), reply.events.end());
        }

        send_up(link, client_side, now);
        send_down(link, server_side, now);

        log.server_events.insert(log.server_events.end(), server_side.events.begin(), server_side.events.end());
        log.client_events.insert(log.client_events.end(), client_side.events.begin(), client_side.events.end());
    }
}

std::size_t count(const std::vector<Event>& events, EventType type) {
    std::size_t n = 0;
    for (const Event& event : events) {
        if (event.type == type) { ++n; }
    }
    return n;
}

std::size_t count(const std::vector<ClientEvent>& events, ClientEvent type) {
    std::size_t n = 0;
    for (const ClientEvent event : events) {
        if (event == type) { ++n; }
    }
    return n;
}

}

TEST(ConditionerIntegrationTest, ModerateNetworkConnectsAndStaysAlive) {
    Config config;
    config.loss = 0.1;
    config.duplicate = 0.05;
    config.delay = 40ms;
    config.jitter = 20ms;

    ConnectionTable table{ kTableSeed };
    ClientConnector connector{ kServer };
    Link link = make_link(config, 1);
    Log log;

    send_up(link, connector.connect(t0), t0);
    run(table, connector, link, t0, t0 + 60s, log);

    // Hello resends cover the lost handshakes, and duplicated Hellos and Welcomes are absorbed
    EXPECT_EQ(count(log.server_events, EventType::Connected), 1u);
    EXPECT_EQ(count(log.client_events, ClientEvent::Connected), 1u);

    EXPECT_EQ(count(log.server_events, EventType::TimedOut), 0u);
    EXPECT_EQ(count(log.client_events, ClientEvent::TimedOut), 0u);

    EXPECT_EQ(connector.state(), ClientState::Connected);
    ASSERT_EQ(table.size(), 1u);
    EXPECT_EQ(table.find(kClient)->client_id(), connector.identity().client_id);
}

TEST(ConditionerIntegrationTest, HighLatencyAloneNeverTimesOut) {
    Config config;
    config.delay = 500ms;

    ConnectionTable table{ kTableSeed };
    ClientConnector connector{ kServer };
    Link link = make_link(config, 1);
    Log log;

    send_up(link, connector.connect(t0), t0);
    run(table, connector, link, t0, t0 + 60s, log);

    // liveness measures silence, not delay: heartbeats arrive late but still every 250 ms
    EXPECT_EQ(count(log.server_events, EventType::TimedOut), 0u);
    EXPECT_EQ(count(log.client_events, ClientEvent::TimedOut), 0u);
    EXPECT_EQ(connector.state(), ClientState::Connected);
    EXPECT_EQ(table.size(), 1u);
}

TEST(ConditionerIntegrationTest, HeavyLossEventuallyTimesOutBothSides) {
    Config config;
    config.loss = 0.7;

    ConnectionTable table{ kTableSeed };
    ClientConnector connector{ kServer };
    Link link = make_link(config, 1);
    Log log;

    // the handshake gets a clean network, then the network turns bad
    Link clean = make_link(Config{}, 1);
    send_up(clean, connector.connect(t0), t0);
    run(table, connector, clean, t0, t0 + 10ms, log);
    ASSERT_EQ(connector.state(), ClientState::Connected);

    run(table, connector, link, t0 + 11ms, t0 + 60s, log);

    // whichever side times out first goes quiet, so the other one follows
    EXPECT_EQ(count(log.server_events, EventType::TimedOut) + count(log.client_events, ClientEvent::TimedOut), 2u);
    EXPECT_EQ(connector.state(), ClientState::Closed);
    EXPECT_EQ(table.size(), 0u);
}
