#include "connection.hpp"

#include <utility>

namespace ticklab::connection
{

Connection::Connection(ClientId client_id, uint64_t token, Endpoint endpoint, TimePoint now)
    : client_id_(client_id)
    , token_(token)
    , endpoint_(std::move(endpoint))
    , last_heard_(now)
    , last_sent_(now) {}

transport::Observation Connection::on_frame_received(uint32_t sequence, TimePoint now)
{
    last_heard_ = now;
    return incoming_.observe(sequence);
}

uint32_t Connection::next_sequence(TimePoint now)
{
    last_sent_ = now;
    return outgoing_sequence_++;
}

bool Connection::should_send_heartbeat(TimePoint now) const
{
    return now - last_sent_ >= kHeartbeatInterval;
}

bool Connection::is_timed_out(TimePoint now) const
{
    return now - last_heard_ >= kConnectionTimeout;
}

}