#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ticklab::connection
{

constexpr uint32_t kProtocolMagic = 0x544B4C42;
constexpr uint16_t kProtocolVersion = 1;

constexpr std::size_t kHelloSize = 18;
constexpr std::size_t kWelcomeSize = 12;
constexpr std::size_t kRejectSize = 1;

static_assert(kWelcomeSize <= kHelloSize, "(amplification)");
static_assert(kRejectSize  <= kHelloSize, "(amplification)");

struct Hello
{
    uint32_t magic;
    uint16_t version;
    uint32_t client_id;
    uint64_t token;
};

struct Welcome
{
    uint32_t client_id;
    uint64_t token;
};

enum class RejectReason : uint8_t
{
    VersionMismatch = 1,
    ServerFull = 2,
};

struct Reject
{
    RejectReason reason;
};

std::vector<uint8_t> serialize_hello(const Hello& hello);
bool parse_hello(const std::vector<uint8_t>& payload, Hello& out);

std::vector<uint8_t> serialize_welcome(const Welcome& welcome);
bool parse_welcome(const std::vector<uint8_t>& payload, Welcome& out);

std::vector<uint8_t> serialize_reject(const Reject& reject);
bool parse_reject(const std::vector<uint8_t>& payload, Reject& out);

}