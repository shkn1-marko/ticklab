#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <random>
#include <vector>

namespace ticklab::conditioner
{

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

using Datagram = std::vector<uint8_t>;

struct Config
{
    double loss = 0.0;      // probability 0..1 that a datagram is dropped
    double duplicate = 0.0; // probability 0..1 that a delivered datagram arrives twice

    std::chrono::milliseconds delay{ 0 };

    // Each delivery gets delay + a uniform random offset in [-jitter, +jitter], never below zero.
    // Reordering is not a setting: it appears whenever two datagrams sent closer together than
    // the jitter spread draw delays that cross over
    std::chrono::milliseconds jitter{ 0 };
};

// One direction of a simulated link. A two-way link is two conditioners.
// Pure logic: the caller supplies the time and moves the datagrams in and out
class Conditioner
{
public:
    // The same config and seed always produce the same network, so a failing run can be replayed
    Conditioner(Config config, uint64_t seed);

    void send(Datagram datagram, TimePoint now);

    // Returns every datagram whose delivery time is at or before now, in delivery order
    std::vector<Datagram> poll(TimePoint now);

private:
    TimePoint draw_delivery_time(TimePoint now);

    Config config_;
    std::mt19937_64 rng_;

    // Keyed by delivery time. A multimap keeps datagrams with equal times in the order they were
    // sent, so without jitter the link never reorders
    std::multimap<TimePoint, Datagram> in_flight_;
};

}
