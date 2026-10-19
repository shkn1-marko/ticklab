#include "client_connector.hpp"

#include "frame.hpp"
#include "messages.hpp"

#include <utility>

namespace ticklab::connection
{

namespace
{

using transport::FrameType;

std::vector<uint8_t> build(FrameType type, uint32_t sequence, const std::vector<uint8_t>& payload)
{
    const transport::FrameHeader header{ sequence, 0, type, static_cast<uint16_t>(payload.size()) };
    return transport::build_frame(header, payload);
}

}

ClientConnector::ClientConnector(Endpoint server, Identity previous)
    : server_(std::move(server))
    , identity_(previous) {}

ClientResult ClientConnector::connect(TimePoint now)
{
    if (state_ == ClientState::Connecting || state_ == ClientState::Connected) { return {}; }

    state_ = ClientState::Connecting;
    handshake_started_ = now;
    last_hello_sent_ = now;

    ClientResult result;
    result.frames.push_back(make_hello());
    return result;
}

ClientResult ClientConnector::on_datagram(const Endpoint& from, const std::vector<uint8_t>& raw, TimePoint now)
{
    if (from != server_) { return {}; }

    transport::FrameHeader header{};
    std::vector<uint8_t> payload;

    if (!transport::parse_frame(raw, header, payload)) { return {}; }

    switch (state_)
    {
    case ClientState::Connecting:
        switch (header.type)
        {
        case FrameType::Welcome:
            return on_welcome(header.sequence, payload, now);

        case FrameType::Reject:
            return on_reject(payload);

        case FrameType::Data:
        case FrameType::Hello:
        case FrameType::Heartbeat:
        case FrameType::Goodbye:
            return {};
        }
        return {};

    case ClientState::Connected:
    {
        connection_->on_frame_received(header.sequence, now);

        switch (header.type)
        {
        case FrameType::Goodbye:
        {
            state_ = ClientState::Closed;
            connection_.reset();

            ClientResult result;
            result.events.push_back(ClientEvent::Disconnected);
            return result;
        }

        case FrameType::Welcome:
        case FrameType::Data:
        case FrameType::Hello:
        case FrameType::Reject:
        case FrameType::Heartbeat:
            return {};
        }
        return {};
    }

    case ClientState::Idle:
    case ClientState::Closed:
        return {};
    }

    return {};
}

ClientResult ClientConnector::update(TimePoint now)
{
    ClientResult result;

    switch (state_)
    {
    case ClientState::Connecting:
        if (now - handshake_started_ >= kHandshakeGiveUp)
        {
            state_ = ClientState::Closed;
            result.events.push_back(ClientEvent::GaveUp);
        }
        else if (now - last_hello_sent_ >= kHelloResendInterval)
        {
            last_hello_sent_ = now;
            result.frames.push_back(make_hello());
        }
        return result;

    case ClientState::Connected:
        if (connection_->is_timed_out(now))
        {
            state_ = ClientState::Closed;
            connection_.reset();
            result.events.push_back(ClientEvent::TimedOut);
        }
        else if (connection_->should_send_heartbeat(now))
        {
            result.frames.push_back(build(FrameType::Heartbeat, connection_->next_sequence(now), {}));
        }
        return result;

    case ClientState::Idle:
    case ClientState::Closed:
        return result;
    }

    return result;
}

ClientResult ClientConnector::disconnect(TimePoint now)
{
    ClientResult result;

    switch (state_)
    {
    case ClientState::Connected:
        result.frames.push_back(build(FrameType::Goodbye, connection_->next_sequence(now), {}));
        connection_.reset();
        state_ = ClientState::Closed;
        return result;

    case ClientState::Connecting:
        state_ = ClientState::Closed;
        return result;

    case ClientState::Idle:
    case ClientState::Closed:
        return result;
    }

    return result;
}

ClientResult ClientConnector::on_welcome(uint32_t sequence, const std::vector<uint8_t>& payload, TimePoint now)
{
    Welcome welcome{};
    if (!parse_welcome(payload, welcome)) { return {}; }

    if (welcome.client_id == kNoClientId) { return {}; }

    identity_ = Identity{ welcome.client_id, welcome.token };
    connection_.emplace(welcome.client_id, welcome.token, server_, now);
    connection_->on_frame_received(sequence, now);
    state_ = ClientState::Connected;

    ClientResult result;
    result.events.push_back(ClientEvent::Connected);
    return result;
}

ClientResult ClientConnector::on_reject(const std::vector<uint8_t>& payload)
{
    Reject reject{};
    if (!parse_reject(payload, reject)) { return {}; }

    state_ = ClientState::Closed;

    ClientResult result;

    switch (reject.reason)
    {
    case RejectReason::VersionMismatch:
        result.events.push_back(ClientEvent::RejectedVersionMismatch);
        return result;
    case RejectReason::ServerFull:
        result.events.push_back(ClientEvent::RejectedServerFull);
        return result;
    }

    return result;
}

std::vector<uint8_t> ClientConnector::make_hello() const
{
    const std::vector<uint8_t> payload = serialize_hello(
        Hello{ kProtocolMagic, kProtocolVersion, identity_.client_id, identity_.token }
    );

    return build(FrameType::Hello, 0, payload);
}

}