#include "sequence_tracker.hpp"
#include "gtest/gtest.h"

#include <cstdint>

using namespace ticklab::transport;

TEST(SequenceTrackerTest, FirstPacketIsInOrder) {
    {
        SequenceTracker tracker;
        EXPECT_EQ(tracker.observe(0).status, SequenceStatus::InOrder);
    }
    {
        // first packet sets the baseline, wherever it happens to be
        SequenceTracker tracker;
        EXPECT_EQ(tracker.observe(1000).status, SequenceStatus::InOrder);
    }
}

TEST(SequenceTrackerTest, ConsecutiveStreamIsAllInOrder) {
    SequenceTracker tracker;

    for (uint32_t sequence = 0; sequence < 100; ++sequence) {
        EXPECT_EQ(tracker.observe(sequence).status, SequenceStatus::InOrder)
            << "sequence " << sequence;
    }
}

TEST(SequenceTrackerTest, MultiNumberGapIsReportedWithRange) {
    SequenceTracker tracker;

    EXPECT_EQ(tracker.observe(4).status, SequenceStatus::InOrder);

    Observation result = tracker.observe(10);

    EXPECT_EQ(result.status, SequenceStatus::Gap);
    EXPECT_EQ(result.first_missing, 5u);
    EXPECT_EQ(result.missing_count, 5u);
}

TEST(SequenceTrackerTest, StreamContinuesInOrderAfterGap) {
    SequenceTracker tracker;

    tracker.observe(4);
    EXPECT_EQ(tracker.observe(7).status, SequenceStatus::Gap);

    EXPECT_EQ(tracker.observe(8).status, SequenceStatus::InOrder);
}

TEST(SequenceTrackerTest, ReorderedArrivalDoesNotMoveHighest) {
    SequenceTracker tracker;

    tracker.observe(4);
    EXPECT_EQ(tracker.observe(7).status, SequenceStatus::Gap);
    EXPECT_EQ(tracker.observe(5).status, SequenceStatus::Reordered);

    EXPECT_EQ(tracker.observe(8).status, SequenceStatus::InOrder);
}

TEST(SequenceTrackerTest, DuplicateOfReorderedNumberIsDetected) {
    SequenceTracker tracker;

    tracker.observe(4);
    EXPECT_EQ(tracker.observe(7).status, SequenceStatus::Gap);
    EXPECT_EQ(tracker.observe(5).status, SequenceStatus::Reordered);

    EXPECT_EQ(tracker.observe(5).status, SequenceStatus::Duplicate);
}