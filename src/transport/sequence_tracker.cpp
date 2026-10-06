#include "sequence_tracker.hpp"

namespace ticklab::transport
{

Observation SequenceTracker::observe(uint32_t sequence)
{
    if (!started_)
    {
        started_ = true;
        highest_ = sequence;

        received_mask_ = ~uint64_t{ 0 };

        return Observation{ SequenceStatus::InOrder, 0, 0 };
    }

    const int32_t diff = static_cast<int32_t>(sequence - highest_);

    if (diff == 0)
    {
        return Observation{ SequenceStatus::Duplicate, 0, 0 };
    }

    if (diff > 0)
    {
        const uint32_t advance = static_cast<uint32_t>(diff);
        const uint32_t first_missing = highest_ + 1;

        received_mask_ = (advance >= kWindowSize) ? uint64_t{ 1 }
                                                  : ((received_mask_ << advance) | uint64_t{ 1 });

        highest_ = sequence;

        if (advance == 1)
        {
            return Observation{ SequenceStatus::InOrder, 0, 0 };
        }

        return Observation{ SequenceStatus::Gap, first_missing, advance - 1 };
    }

    const uint32_t behind = highest_ - sequence;

    if (behind >= kWindowSize)
    {
        return Observation{ SequenceStatus::TooOld, 0, 0 };
    }

    const uint64_t bit = uint64_t{ 1 } << behind;

    if (received_mask_ & bit)
    {
        return Observation{ SequenceStatus::Duplicate, 0, 0 };
    }

    received_mask_ |= bit;
    return Observation{ SequenceStatus::Reordered, 0, 0 };
}

}