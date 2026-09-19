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

TEST(FrameTest, BuildAndParseRoundTripPreservesPayload) {
    std::vector<uint8_t> payload{ 'h', 'e', 'l', 'l', 'o' };
    FrameHeader header{ 7, 200, FrameType::Data, static_cast<uint16_t>(payload.size()) };

    std::vector<uint8_t> raw = build_frame(header, payload);

    FrameHeader parsed_header{};
    std::vector<uint8_t> parsed_payload;
    bool ok = parse_frame(raw, parsed_header, parsed_payload);

    ASSERT_TRUE(ok);
    EXPECT_EQ(parsed_header.sequence, header.sequence);
    EXPECT_EQ(parsed_header.tick, header.tick);
    EXPECT_EQ(parsed_header.type, header.type);
    EXPECT_EQ(parsed_header.payload_length, header.payload_length);
    EXPECT_EQ(parsed_payload, payload);
}

TEST(FrameTest, ParseFrameRejectsTruncatedBuffer) {
    std::vector<uint8_t> too_short(kHeaderSize - 1, 0);

    FrameHeader out_header{};
    std::vector<uint8_t> out_payload;
    bool ok = parse_frame(too_short, out_header, out_payload);

    EXPECT_FALSE(ok);
}

TEST(FrameTest, ParseFrameRejectsPayloadLengthMismatch) {
    std::vector<uint8_t> payload{ 'a', 'b', 'c' };
    FrameHeader header{ 1, 1, FrameType::Data, 999 };

    std::vector<uint8_t> raw = build_frame(header, payload);

    FrameHeader out_header{};
    std::vector<uint8_t> out_payload;
    bool ok = parse_frame(raw, out_header, out_payload);

    EXPECT_FALSE(ok);
}