#pragma once

#include <cstdint>

namespace ticklab::transport
{

enum class SequenceStatus
{
    InOrder,
    Gap,
    Reordered,
    Duplicate,
    TooOld,
};

struct Observation
{
    SequenceStatus status;

    uint32_t first_missing = 0;
    uint32_t missing_count = 0;
};

class SequenceTracker
{
public:
    static constexpr uint32_t kWindowSize = 64;

    Observation observe(uint32_t sequence);

private:
    bool started_ = false;
    uint32_t highest_ = 0;

    uint64_t received_mask_ = 0;
};

}