#pragma once

#include "sequence_tracker.hpp"

#include <chrono>
#include <cstdint>
#include <string>

namespace ticklab::connection
{

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

constexpr auto kHeartbeatInterval = std::chrono::microseconds(250);
constexpr auto kConnectionTimeout = std::chrono::seconds(2);

using ClientId = uint32_t;
constexpr ClientId kNoClientId = 0;

struct Endpoint
{
    std::string address;
    uint16_t port = 0;

    bool operator==(const Endpoint&) const = default;
};

class Connection
{
    Connection(ClientId client_id, uint64_t token, Endpoint endpoint, TimePoint now);

    ClientId client_id() { return client_id_; }
    uint64_t token() { return token_; }
    const Endpoint& endpoint() const { return endpoint_; }

    // Incoming: any valid frame from the peer is proof of life
    transport::Observation on_frame_received(uint32_t sequence, TimePoint now);

    // Outgoing: returns the sequence number for the next frame and records the sent time
    uint32_t next_sequence(TimePoint now);

    // Liveness. Both use ">=", so the boundry instant counts
    bool should_send_heartbeat(TimePoint now) const; // nothing sent for kHeartbeatInterval
    bool is_timed_out(TimePoint now) const; // nothing heard for kConnectionTimeout

private:
    ClientId client_id_;
    uint64_t token_;
    Endpoint endpoint_;

    transport::SequenceTracker incoming_;
    uint32_t outgoing_sequence_ = 0;

    TimePoint last_heard_;
    TimePoint last_sent_;
};

}