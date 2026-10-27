#include "conditioner.hpp"
#include "sequence_tracker.hpp"
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <vector>

using namespace ticklab::conditioner;
using ticklab::transport::SequenceStatus;
using ticklab::transport::SequenceTracker;
using namespace std::chrono_literals;

namespace
{

const TimePoint t0{};
constexpr uint64_t kSeed = 7;

Datagram numbered(uint32_t number) {
    return Datagram{
        static_cast<uint8_t>(number),
        static_cast<uint8_t>(number >> 8),
        static_cast<uint8_t>(number >> 16),
        static_cast<uint8_t>(number >> 24),
    };
}

uint32_t number_of(const Datagram& datagram) {
    return static_cast<uint32_t>(datagram[0])
         | static_cast<uint32_t>(datagram[1]) << 8
         | static_cast<uint32_t>(datagram[2]) << 16
         | static_cast<uint32_t>(datagram[3]) << 24;
}

// sends `count` numbered datagrams `spacing` apart, then collects everything that arrives
std::vector<uint32_t> send_and_collect(Conditioner& conditioner, uint32_t count, std::chrono::milliseconds spacing) {
    for (uint32_t i = 0; i < count; ++i) {
        conditioner.send(numbered(i), t0 + i * spacing);
    }

    std::vector<uint32_t> received;
    for (const Datagram& datagram : conditioner.poll(t0 + count * spacing + 10s)) {
        received.push_back(number_of(datagram));
    }
    return received;
}

std::size_t count_status(const std::vector<uint32_t>& received, SequenceStatus status) {
    SequenceTracker tracker;
    std::size_t count = 0;
    for (const uint32_t number : received) {
        if (tracker.observe(number).status == status) { ++count; }
    }
    return count;
}

}

TEST(ConditionerTest, PerfectLinkDeliversEverythingInOrderAtOnce) {
    Conditioner conditioner{ Config{}, kSeed };

    conditioner.send(numbered(1), t0);
    conditioner.send(numbered(2), t0);

    const std::vector<Datagram> delivered = conditioner.poll(t0);

    ASSERT_EQ(delivered.size(), 2u);
    EXPECT_EQ(number_of(delivered[0]), 1u);
    EXPECT_EQ(number_of(delivered[1]), 2u);
}

TEST(ConditionerTest, NothingArrivesBeforeItsDelay) {
    Config config;
    config.delay = 50ms;
    Conditioner conditioner{ config, kSeed };

    conditioner.send(numbered(1), t0);

    EXPECT_TRUE(conditioner.poll(t0 + 49ms).empty());

    // delivery time itself counts as arrived
    EXPECT_EQ(conditioner.poll(t0 + 50ms).size(), 1u);
    EXPECT_TRUE(conditioner.poll(t0 + 1s).empty());
}

TEST(ConditionerTest, NothingArrivesBeforeItWasSent) {
    Config config;
    config.delay = 5ms;
    config.jitter = 20ms;
    Conditioner conditioner{ config, kSeed };

    const TimePoint sent = t0 + 1s;
    for (uint32_t i = 0; i < 1000; ++i) { conditioner.send(numbered(i), sent); }

    EXPECT_TRUE(conditioner.poll(sent - 1ns).empty());

    // draws below zero are clamped, so those datagrams arrive the instant they were sent
    EXPECT_FALSE(conditioner.poll(sent).empty());
}

TEST(ConditionerTest, SameSeedReplaysTheSameNetwork) {
    Config config;
    config.loss = 0.3;
    config.duplicate = 0.1;
    config.delay = 50ms;
    config.jitter = 20ms;

    Conditioner first{ config, kSeed };
    Conditioner second{ config, kSeed };
    Conditioner other{ config, kSeed + 1 };

    const std::vector<uint32_t> first_received = send_and_collect(first, 1000, 1ms);

    EXPECT_EQ(first_received, send_and_collect(second, 1000, 1ms));
    EXPECT_NE(first_received, send_and_collect(other, 1000, 1ms));
}

// The rate tests send N datagrams, so each count follows a binomial distribution with standard
// deviation sqrt(N * p * (1 - p)). A tolerance of 5 deviations is never hit by a correct
// conditioner, but a wrong one (say, loss rolled twice) lands far outside it

TEST(ConditionerTest, LossRateMatchesConfig) {
    Config config;
    config.loss = 0.2;
    Conditioner conditioner{ config, kSeed };

    // expected 8000 delivered, deviation 40
    const std::vector<uint32_t> received = send_and_collect(conditioner, 10000, 1ms);

    EXPECT_NEAR(static_cast<double>(received.size()), 8000.0, 200.0);
}

TEST(ConditionerTest, DuplicateRateMatchesConfig) {
    Config config;
    config.duplicate = 0.1;
    Conditioner conditioner{ config, kSeed };

    // expected 1000 extra copies, deviation 30
    const std::vector<uint32_t> received = send_and_collect(conditioner, 10000, 1ms);
    const std::size_t duplicates = count_status(received, SequenceStatus::Duplicate);

    EXPECT_NEAR(static_cast<double>(duplicates), 1000.0, 150.0);
    EXPECT_EQ(received.size(), 10000u + duplicates);
}

TEST(ConditionerTest, DelayWithoutJitterNeverReorders) {
    Config config;
    config.delay = 50ms;
    Conditioner conditioner{ config, kSeed };

    const std::vector<uint32_t> received = send_and_collect(conditioner, 1000, 1ms);

    ASSERT_EQ(received.size(), 1000u);
    EXPECT_EQ(count_status(received, SequenceStatus::Reordered), 0u);
}

TEST(ConditionerTest, JitterReordersDatagramsSentCloseTogether) {
    Config config;
    config.delay = 50ms;
    config.jitter = 20ms;
    Conditioner conditioner{ config, kSeed };

    // 1 ms apart, far inside the 40 ms spread of possible delays
    const std::vector<uint32_t> received = send_and_collect(conditioner, 1000, 1ms);

    ASSERT_EQ(received.size(), 1000u);
    EXPECT_GT(count_status(received, SequenceStatus::Reordered), 0u);
}

TEST(ConditionerTest, JitterCannotReorderDatagramsSentFurtherApartThanItsSpread) {
    Config config;
    config.delay = 50ms;
    config.jitter = 20ms;
    Conditioner conditioner{ config, kSeed };

    // 41 ms apart: even the slowest draw for one arrives before the fastest for the next
    const std::vector<uint32_t> received = send_and_collect(conditioner, 1000, 41ms);

    ASSERT_EQ(received.size(), 1000u);
    EXPECT_EQ(count_status(received, SequenceStatus::Reordered), 0u);
}
