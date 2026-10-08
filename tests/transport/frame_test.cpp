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

TEST(FrameTest, SerializeProducesExpectedWireBytes) {
    FrameHeader header{ 0x01020304, 0x0A0B0C0D, FrameType::Data, 0x0506 };

    std::array<uint8_t, kHeaderSize> bytes{};
    serialize_header(header, bytes);

    const std::array<uint8_t, kHeaderSize> expected{
        0x01, 0x02, 0x03, 0x04,  // sequence, big-endian
        0x0A, 0x0B, 0x0C, 0x0D,  // tick, big-endian
        0x00,                    // type = Data
        0x05, 0x06               // payload_length, big-endian
    };

    EXPECT_EQ(bytes, expected);
}

TEST(FrameTest, HeaderRoundTripAtMaximumFieldValues) {
    FrameHeader header{ UINT32_MAX, UINT32_MAX, FrameType::Data, UINT16_MAX };

    std::array<uint8_t, kHeaderSize> bytes{};
    serialize_header(header, bytes);

    FrameHeader result = deserialize_header(bytes);

    EXPECT_EQ(result.sequence, header.sequence);
    EXPECT_EQ(result.tick, header.tick);
    EXPECT_EQ(result.type, header.type);
    EXPECT_EQ(result.payload_length, header.payload_length);
}

TEST(FrameTest, BuildAndParseEmptyPayload) {
    std::vector<uint8_t> payload;
    FrameHeader header{ 1, 1, FrameType::Data, 0 };

    std::vector<uint8_t> raw = build_frame(header, payload);

    EXPECT_EQ(raw.size(), kHeaderSize);

    FrameHeader parsed_header{};
    std::vector<uint8_t> parsed_payload;
    bool ok = parse_frame(raw, parsed_header, parsed_payload);

    ASSERT_TRUE(ok);
    EXPECT_EQ(parsed_header.payload_length, 0);
    EXPECT_TRUE(parsed_payload.empty());
}

TEST(FrameTest, ParseFrameRejectsTrailingBytesBeyondDeclaredLength) {
    std::vector<uint8_t> payload(10, 0xAB);
    FrameHeader header{ 1, 1, FrameType::Data, 3 }; // 3 != 10; 3 < 10

    std::vector<uint8_t> raw = build_frame(header, payload);

    FrameHeader out_header{};
    std::vector<uint8_t> out_payload;
    bool ok = parse_frame(raw, out_header, out_payload);

    EXPECT_FALSE(ok);
}

TEST(FrameTest, ParseFrameRejectsUnknownFrameType) {
    std::vector<uint8_t> payload{ 'a', 'b', 'c' };
    FrameHeader header{ 1, 1, FrameType::Data, static_cast<uint16_t>(payload.size()) };

    std::vector<uint8_t> raw = build_frame(header, payload);

    // byte 8 is the FrameType byte
    raw[8] = 0xFF;

    FrameHeader out_header{};
    std::vector<uint8_t> out_payload;
    bool ok = parse_frame(raw, out_header, out_payload);

    EXPECT_FALSE(ok);
}