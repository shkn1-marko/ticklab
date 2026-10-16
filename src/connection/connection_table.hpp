#pragma once

#include "connection.hpp"
#include "messages.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <random>
#include <vector>

namespace ticklab::connection
{

constexpr std::size_t kMaxClients = 4;

struct OutgoingFrame
{
    Endpoint to;
    std::vector<uint8_t> bytes;
};

enum class EventType
{
    Connected,
    Reconnected,
    Disconnected,
    TimedOut,
    Rejected,
};

struct Event
{
    EventType type;
    ClientId client_id;
    Endpoint endpoint;
};

struct Result
{
    std::vector<OutgoingFrame> frames;
    std::vector<Event> events;
};

class ConnectionTable
{
public:
    explicit ConnectionTable(uint64_t seed);

    Result on_datagram(const Endpoint& from, const std::vector<uint8_t>& raw, TimePoint now);

    Result update(TimePoint now);

    Result close_all(TimePoint now);

    std::size_t size() const { return connections_.size(); }
    const Connection* find(ClientId client_id) const;
    const Connection* find(const Endpoint& endpoint) const;

private:
    Result on_hello_from_unknown(const Endpoint& from, const std::vector<uint8_t>& payload, TimePoint now);

    OutgoingFrame make_welcome(Connection& connection, TimePoint now);
    uint64_t generate_token();

    std::map<ClientId, Connection> connections_;
    ClientId next_client_id_ = 1;
    std::mt19937_64 rng_;
};

}