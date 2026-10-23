#include "conditioner.hpp"

#include <algorithm>
#include <utility>

namespace ticklab::conditioner
{

namespace
{

// The std distributions are implemented differently by each standard library, so the same seed
// gives a different network on Windows and Linux. mt19937_64 itself is fully specified, so a
// uniform value built from its raw output keeps runs identical on every platform.
// The top 53 bits fill a double's mantissa exactly, giving [0, 1)
double uniform01(std::mt19937_64& rng)
{
    return static_cast<double>(rng() >> 11) * 0x1.0p-53;
}

}

Conditioner::Conditioner(Config config, uint64_t seed)
    : config_(config), rng_(seed) {}

void Conditioner::send(Datagram datagram, TimePoint now)
{
    if (uniform01(rng_) < config_.loss) { return; }

    if (uniform01(rng_) < config_.duplicate)
    {
        in_flight_.emplace(draw_delivery_time(now), datagram);
    }

    in_flight_.emplace(draw_delivery_time(now), std::move(datagram));
}

std::vector<Datagram> Conditioner::poll(TimePoint now)
{
    const auto due_end = in_flight_.upper_bound(now);

    std::vector<Datagram> delivered;
    for (auto it = in_flight_.begin(); it != due_end; ++it)
    {
        delivered.push_back(std::move(it->second));
    }

    in_flight_.erase(in_flight_.begin(), due_end);
    return delivered;
}

TimePoint Conditioner::draw_delivery_time(TimePoint now)
{
    using Milliseconds = std::chrono::duration<double, std::milli>;

    const Milliseconds offset = config_.jitter * (2.0 * uniform01(rng_) - 1.0);
    const Milliseconds total = std::max(Milliseconds{ 0 }, config_.delay + offset);

    return now + std::chrono::duration_cast<Clock::duration>(total);
}

}
