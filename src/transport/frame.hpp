#pragma once

#include <cstdint>
#include <array>
#include <vector>

namespace ticklab::transport
{

constexpr std::size_t kHeaderSize = 11;

enum class FrameType : uint8_t
{
    Data = 0,
};

struct FrameHeader
{
    uint32_t sequence;
    uint32_t tick;
    FrameType type;
    uint16_t payload_length;
};

void serialize_header(const FrameHeader& header, std::array<uint8_t, kHeaderSize>& out);

FrameHeader deserialize_header(const std::array<uint8_t, kHeaderSize>& in);

std::vector<uint8_t> build_frame(const FrameHeader& header, const std::vector<uint8_t>& payload);

bool parse_frame(const std::vector<uint8_t>& raw, FrameHeader& out_header, std::vector<uint8_t>& out_payload);

}