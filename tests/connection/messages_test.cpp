#include "messages.hpp"
#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using namespace ticklab::connection;

TEST(MessagesTest, HelloRoundTripPreservesAllFields) {
    Hello hello{ kProtocolMagic, kProtocolVersion, 42, 0x1122334455667788 };

    std::vector<uint8_t> bytes = serialize_hello(hello);

    Hello result{};
    bool ok = parse_hello(bytes, result);

    ASSERT_TRUE(ok);
    EXPECT_EQ(result.magic, hello.magic);
    EXPECT_EQ(result.version, hello.version);
    EXPECT_EQ(result.client_id, hello.client_id);
    EXPECT_EQ(result.token, hello.token);
}

TEST(MessagesTest, HelloSerializeProducesExpectedWireBytes) {
    Hello hello{ 0x01020304, 0x0506, 0x0708090A, 0x0B0C0D0E0F101112 };

    std::vector<uint8_t> bytes = serialize_hello(hello);

    const std::vector<uint8_t> expected{
        0x01, 0x02, 0x03, 0x04,                         // magic, big-endian
        0x05, 0x06,                                     // version, big-endian
        0x07, 0x08, 0x09, 0x0A,                         // client_id, big-endian
        0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12  // token, big-endian
    };

    ASSERT_EQ(bytes.size(), kHelloSize);
    EXPECT_EQ(bytes, expected);
}

TEST(MessagesTest, ParseHelloAcceptsWrongMagicAndVersion) {
    // parser checks shape only; rejecting strangers is the server's decision
    Hello hello{ 0xABCDABCD, 999, 0, 0 };

    std::vector<uint8_t> bytes = serialize_hello(hello);

    Hello result{};
    bool ok = parse_hello(bytes, result);

    ASSERT_TRUE(ok);
    EXPECT_EQ(result.magic, 0xABCDABCD);
    EXPECT_EQ(result.version, 999);
}

TEST(MessagesTest, WelcomeRoundTripPreservesAllFields) {
    Welcome welcome{ 42, 0x1122334455667788 };

    std::vector<uint8_t> bytes = serialize_welcome(welcome);

    Welcome result{};
    bool ok = parse_welcome(bytes, result);

    ASSERT_TRUE(ok);
    EXPECT_EQ(result.client_id, welcome.client_id);
    EXPECT_EQ(result.token, welcome.token);
}

TEST(MessagesTest, WelcomeSerializeProducesExpectedWireBytes) {
    Welcome welcome{ 0x01020304, 0x05060708090A0B0C };

    std::vector<uint8_t> bytes = serialize_welcome(welcome);

    const std::vector<uint8_t> expected{
        0x01, 0x02, 0x03, 0x04,                         // client_id, big-endian
        0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C  // token, big-endian
    };

    ASSERT_EQ(bytes.size(), kWelcomeSize);
    EXPECT_EQ(bytes, expected);
}