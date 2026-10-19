#include "connection_table.hpp"

#include "frame.hpp"

namespace ticklab::connection
{

namespace
{

using transport::FrameType;

// tick field stays 0 until stage five
std::vector<uint8_t> build(FrameType type, uint32_t sequence, const std::vector<uint8_t>& payload)
{
    const transport::FrameHeader header{ sequence, 0, type, static_cast<uint16_t>(payload.size()) };
    return transport::build_frame(header, payload);
}

// no connection exists; sequence set to 0
Result make_reject(const Endpoint& to, RejectReason reason)
{
    Result result;
    result.frames.push_back({ to, build(FrameType::Reject, 0, serialize_reject(Reject { reason })) });
    result.events.push_back({ EventType::Rejected, kNoClientId, to });
    return result;
}

}

ConnectionTable::ConnectionTable(uint64_t seed)
    : rng_(seed) {}

const Connection* ConnectionTable::find(ClientId client_id) const
{
    const auto it = connections_.find(client_id);
    return it == connections_.end() ? nullptr : &it->second;
}

const Connection* ConnectionTable::find(const Endpoint& endpoint) const
{
    for (const auto& [client_id, connection] : connections_)
    {
        if (connection.endpoint() == endpoint) { return &connection; }
    }

    return nullptr;
}

Result ConnectionTable::on_datagram(const Endpoint& from, const std::vector<uint8_t>& raw, TimePoint now)
{
    transport::FrameHeader header{};
    std::vector<uint8_t> payload;

    if (!transport::parse_frame(raw, header, payload)) { return {}; }

    auto it = connections_.begin();
    while (it != connections_.end() && it->second.endpoint() != from) { ++it; }

    if (it == connections_.end())
    {
        if (header.type == FrameType::Hello) { return on_hello_from_unknown(from, payload, now); }
        return {};
    }

    Connection& connection = it->second;

    // A repeated Hello is numbered outside the connection (always 0),
    // so it must not reach the tracker
    if (header.type != FrameType::Hello) { connection.on_frame_received(header.sequence, now); }

    switch (header.type)
    {
    case FrameType::Hello:
    {
        // retry; client didn't receive Welcome
        Hello hello{};
        if (!parse_hello(payload, hello)) { return {}; }

        Result result;
        result.frames.push_back(make_welcome(connection, now));
        return result;
    }

    case FrameType::Goodbye:
    {
        Result result;
        result.events.push_back({ EventType::Disconnected, connection.client_id(), connection.endpoint() });
        connections_.erase(it);
        return result;
    }

    case FrameType::Data:
    case FrameType::Heartbeat:
    case FrameType::Welcome:
    case FrameType::Reject:
        return {};
    }

    return {};
}

Result ConnectionTable::on_hello_from_unknown(const Endpoint& from, const std::vector<uint8_t>& payload, TimePoint now)
{
    Hello hello{};
    if (!parse_hello(payload, hello)) { return {}; }

    if (hello.magic != kProtocolMagic) { return {}; }

    if (hello.version != kProtocolVersion) { return make_reject(from, RejectReason::VersionMismatch); }

    // reconnect; matching ID and token, new endpoint
    // replaces the old connection, connection count does not change
    if (hello.client_id != kNoClientId)
    {
        const auto it = connections_.find(hello.client_id);

        if (it != connections_.end() && it->second.token() == hello.token)
        {
            const ClientId client_id = hello.client_id;
            const uint64_t token = hello.token;

            connections_.erase(it);
            Connection& connection = connections_.try_emplace(client_id, client_id, token, from, now).first->second;

            Result result;
            result.frames.push_back(make_welcome(connection, now));
            result.events.push_back({ EventType::Reconnected, client_id, from });
            return result;
        }
    }

    // new client, or a wrong/expired token; new connection

    if (connections_.size() >= kMaxClients) { return make_reject(from, RejectReason::ServerFull); }

    const ClientId client_id = next_client_id_++;
    Connection& connection = connections_.try_emplace(client_id, client_id, generate_token(), from, now).first->second;

    Result result;
    result.frames.push_back(make_welcome(connection, now));
    result.events.push_back({ EventType::Connected, client_id, from });
    return result;
}

Result ConnectionTable::update(TimePoint now)
{
    Result result;

    for (auto it = connections_.begin(); it != connections_.end();)
    {
        Connection& connection = it->second;

        if (connection.is_timed_out(now))
        {
            result.events.push_back({ EventType::TimedOut, connection.client_id(), connection.endpoint() });
            it = connections_.erase(it);
            continue;
        }

        if (connection.should_send_heartbeat(now))
        {
            result.frames.push_back({ connection.endpoint(),
                                      build(FrameType::Heartbeat, connection.next_sequence(now), {}) });
        }

        ++it;
    }

    return result;
}

Result ConnectionTable::close_all(TimePoint now)
{
    Result result;

    for (auto& [client_id, connection] : connections_)
    {
        result.frames.push_back({ connection.endpoint(),
                                  build(FrameType::Goodbye, connection.next_sequence(now), {}) });
    }

    connections_.clear();
    return result;
}

OutgoingFrame ConnectionTable::make_welcome(Connection& connection, TimePoint now)
{
    const std::vector<uint8_t> payload =
        serialize_welcome(Welcome{ connection.client_id(), connection.token() });

    return { connection.endpoint(), build(FrameType::Welcome, connection.next_sequence(now), payload) };
}

uint64_t ConnectionTable::generate_token()
{
    // 0 == new client; never handed out
    uint64_t token = 0;
    while (token == 0) { token = rng_(); }
    return token;
}

}