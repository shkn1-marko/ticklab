#pragma once

#include "connection.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace ticklab::connection
{

constexpr auto kHelloResendInterval = std::chrono::milliseconds(250);
constexpr auto kHandshakeGiveUp = std::chrono::seconds(5);

struct Identity
{
    ClientId client_id = kNoClientId;
    uint64_t token = 0;
};

enum class ClientState
{
    Idle,
    Connecting,
    Connected,
    Closed,
};

enum class ClientEvent
{
    Connected,
    RejectedVersionMismatch,
    RejectedServerFull,
    GaveUp,
    TimedOut,
    Disconnected,
};

struct ClientResult
{
    std::vector<std::vector<uint8_t>> frames;
    std::vector<ClientEvent> events;
};

class ClientConnector
{
public:
    explicit ClientConnector(Endpoint server, Identity previous = {});

    ClientResult connect(TimePoint now);

    ClientResult on_datagram(const Endpoint& from, const std::vector<uint8_t>& raw, TimePoint now);

    ClientResult update(TimePoint now);

    ClientResult disconnect(TimePoint now);

    ClientState state() const { return state_; }
    const Identity& identity() const { return identity_; }
    const Endpoint& server() const { return server_; }

    const Connection* connection() const { return connection_ ? &*connection_ : nullptr; }

private:
    ClientResult on_welcome(uint32_t sequence, const std::vector<uint8_t>& payload, TimePoint now);
    ClientResult on_reject(const std::vector<uint8_t>& payload);

    std::vector<uint8_t> make_hello() const;

    Endpoint server_;
    Identity identity_;
    ClientState state_ = ClientState::Idle;

    TimePoint handshake_started_{};
    TimePoint last_hello_sent_{};

    std::optional<Connection> connection_;
};

}