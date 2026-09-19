#include "frame.hpp"
#include <gtest/gtest.h>

using namespace ticklab::transport;

TEST(FrameTest, HeaderRoundTripPreservesAllFields) {
    FrameHeader header { 42, 100, FrameType::Data, 5 };

    std::array<uint8_t, kHeaderSize> bytes{};
    serialize_header(header, bytes);

    FrameHeader result = deserialize_header(bytes);

    EXPECT_EQ(result.sequence, header.sequence);
    EXPECT_EQ(result.tick, header.tick);
    EXPECT_EQ(result.type, header.type);
    EXPECT_EQ(result.payload_length, header.payload_length);
}