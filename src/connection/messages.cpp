#include "messages.hpp"

namespace ticklab::connection
{

namespace
{

void write_u8(std::vector<uint8_t>& out, uint8_t value)
{
    out.push_back(value);
}

void write_u16(std::vector<uint8_t>& out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

void write_u32(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value >> 24));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

void write_u64(std::vector<uint8_t>& out, uint64_t value)
{
    write_u32(out, static_cast<uint32_t>(value >> 32));
    write_u32(out, static_cast<uint32_t>(value));
}

uint8_t read_u8(const std::vector<uint8_t>& in, std::size_t& offset)
{
    return in[offset++];
}

uint16_t read_u16(const std::vector<uint8_t>& in, std::size_t& offset)
{
    uint16_t value = static_cast<uint16_t>(in[offset] << 8 | in[offset + 1]);
    offset += 2;
    return value;
}

uint32_t read_u32(const std::vector<uint8_t>& in, std::size_t& offset)
{
    uint32_t value = static_cast<uint32_t>(in[offset]) << 24
                   | static_cast<uint32_t>(in[offset + 1]) << 16
                   | static_cast<uint32_t>(in[offset + 2]) << 8
                   | static_cast<uint32_t>(in[offset + 3]);
    offset += 4;
    return value;
}

uint64_t read_u64(const std::vector<uint8_t>& in, std::size_t& offset)
{
    const uint64_t high = read_u32(in, offset);
    const uint64_t low = read_u32(in, offset);
    return (high << 32) | low;
}

bool is_known_reject_reason(RejectReason reason)
{
    switch (reason)
    {
    case RejectReason::VersionMismatch:
        return true;
    case RejectReason::ServerFull:
        return true;
    }

    return false;
}

}

std::vector<uint8_t> serialize_hello(const Hello& hello)
{
    std::vector<uint8_t> out;
    out.reserve(kHelloSize);

    write_u32(out, hello.magic);
    write_u16(out, hello.version);
    write_u32(out, hello.client_id);
    write_u64(out, hello.token);

    return out;
}

bool parse_hello(const std::vector<uint8_t>& payload, Hello& out)
{
    if (payload.size() != kHelloSize) { return false; }

    std::size_t offset = 0;

    out.magic = read_u32(payload, offset);
    out.version = read_u16(payload, offset);
    out.client_id = read_u32(payload, offset);
    out.token = read_u64(payload, offset);

    return true;
}

std::vector<uint8_t> serialize_welcome(const Welcome& welcome)
{
    std::vector<uint8_t> out;
    out.reserve(kWelcomeSize);

    write_u32(out, welcome.client_id);
    write_u64(out, welcome.token);

    return out;
}

bool parse_welcome(const std::vector<uint8_t>& payload, Welcome& out)
{
    if (payload.size() != kWelcomeSize) { return false; }

    std::size_t offset = 0;

    out.client_id = read_u32(payload, offset);
    out.token = read_u64(payload, offset);

    return true;
}

std::vector<uint8_t> serialize_reject(const Reject& reject)
{
    std::vector<uint8_t> out;
    out.reserve(kRejectSize);

    write_u8(out, static_cast<uint8_t>(reject.reason));

    return out;
}

bool parse_reject(const std::vector<uint8_t>& payload, Reject& out)
{
    if (payload.size() != kRejectSize) { return false; }

    std::size_t offset = 0;

    const RejectReason reason = static_cast<RejectReason>(read_u8(payload, offset));
    if (!is_known_reject_reason(reason)) { return false; }

    out.reason = reason;

    return true;
}

}