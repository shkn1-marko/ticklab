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

TEST(SequenceTrackerTest, SingleGapIsReportedWithRange) {
    SequenceTracker tracker;

    EXPECT_EQ(tracker.observe(4).status, SequenceStatus::InOrder);
    EXPECT_EQ(tracker.observe(5).status, SequenceStatus::InOrder);

    Observation result = tracker.observe(7);

    EXPECT_EQ(result.status, SequenceStatus::Gap);
    EXPECT_EQ(result.first_missing, 6u);
    EXPECT_EQ(result.missing_count, 1u);
}

TEST(SequenceTrackerTest, DuplicateOfHighestIsDetected) {
    SequenceTracker tracker;

    EXPECT_EQ(tracker.observe(5).status, SequenceStatus::InOrder);
    EXPECT_EQ(tracker.observe(5).status, SequenceStatus::Duplicate);
}

TEST(SequenceTrackerTest, DuplicateOfOlderReceivedNumberIsDetected) {
    SequenceTracker tracker;

    tracker.observe(4);
    tracker.observe(5);
    tracker.observe(6);

    EXPECT_EQ(tracker.observe(5).status, SequenceStatus::Duplicate);
}

TEST(SequenceTrackerTest, EarlierArrivalSurviveLaterAdvances) {
    SequenceTracker tracker;

    tracker.observe(0);
    EXPECT_EQ(tracker.observe(2).status, SequenceStatus::Gap);
    EXPECT_EQ(tracker.observe(3).status, SequenceStatus::InOrder);

    EXPECT_EQ(tracker.observe(1).status, SequenceStatus::Reordered);
}

TEST(SequenceTrackerTest, WindowEdgeSeparatesReorderedFromTooOld) {
    SequenceTracker tracker;

    tracker.observe(1);
    EXPECT_EQ(tracker.observe(100).status, SequenceStatus::Gap);

    EXPECT_EQ(tracker.observe(37).status, SequenceStatus::Reordered);
    EXPECT_EQ(tracker.observe(36).status, SequenceStatus::TooOld);
}

TEST(SequenceTrackerTest, JumpLargerThanWindowReportsFullGapAndForgetsHistory) {
    SequenceTracker tracker;

    tracker.observe(0);

    Observation result = tracker.observe(1000);

    EXPECT_EQ(result.status, SequenceStatus::Gap);
    EXPECT_EQ(result.first_missing, 1u);
    EXPECT_EQ(result.missing_count, 999u);

    EXPECT_EQ(tracker.observe(500).status, SequenceStatus::TooOld);
}

TEST(SequenceTrackerTest, ShiftBoundaryAtWindowSize) {
    {
        SequenceTracker tracker;

        tracker.observe(0);
        EXPECT_EQ(tracker.observe(63).status, SequenceStatus::Gap);
        EXPECT_EQ(tracker.observe(0).status, SequenceStatus::Duplicate);
    }
    {
        SequenceTracker tracker;

        tracker.observe(0);
        EXPECT_EQ(tracker.observe(64).status, SequenceStatus::Gap);
        EXPECT_EQ(tracker.observe(0).status, SequenceStatus::TooOld);
    }
}

TEST(SequenceTrackerTest, WraparoundAcrossUint32Max) {
    {
        SequenceTracker tracker;

        tracker.observe(UINT32_MAX);
        EXPECT_EQ(tracker.observe(0).status, SequenceStatus::InOrder);
    }
    {
        SequenceTracker tracker;

        tracker.observe(UINT32_MAX - 1);

        Observation result = tracker.observe(0);

        EXPECT_EQ(result.status, SequenceStatus::Gap);
        EXPECT_EQ(result.first_missing, UINT32_MAX);
        EXPECT_EQ(result.missing_count, 1u);
    }
    {
        SequenceTracker tracker;

        tracker.observe(UINT32_MAX);

        Observation gap = tracker.observe(1);

        EXPECT_EQ(gap.status, SequenceStatus::Gap);
        EXPECT_EQ(gap.first_missing, 0u);
        EXPECT_EQ(gap.missing_count, 1u);

        EXPECT_EQ(tracker.observe(0).status, SequenceStatus::Reordered);
    }
}

TEST(SequenceTrackerTest, ArrivalFromBaselineIsDuplicate) {
    SequenceTracker tracker;

    EXPECT_EQ(tracker.observe(100).status, SequenceStatus::InOrder);

    // packet before the first observed is duplicate
    // it should not be reported as recovered loss (reordered)
    EXPECT_EQ(tracker.observe(98).status, SequenceStatus::Duplicate);
}