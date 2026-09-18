#include "frame.hpp"

#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <arpa/inet.h>
#endif

#include <cstring>

namespace ticklab::transport
{

void serialize_header(const FrameHeader& header, std::array<uint8_t, kHeaderSize>& out)
{
    uint32_t sequence_be = htonl(header.sequence);
    uint32_t tick_be = htonl(header.tick);
    uint16_t length_be = htons(header.payload_length);

    std::size_t offset = 0;

    std::memcpy(out.data() + offset, &sequence_be, sizeof(sequence_be));
    offset += sizeof(sequence_be);

    std::memcpy(out.data() + offset, &tick_be, sizeof(tick_be));
    offset += sizeof(tick_be);

    out[offset] = static_cast<uint8_t>(header.type);
    offset += sizeof(uint8_t);

    std::memcpy(out.data() + offset, &length_be, sizeof(length_be));
    offset += sizeof(length_be);
}

FrameHeader deserialize_header(const std::array<uint8_t, kHeaderSize>& in)
{
    FrameHeader header{};

    uint32_t sequence_be = 0;
    uint32_t tick_be     = 0;
    uint16_t length_be   = 0;

    std::size_t offset = 0;

    std::memcpy(&sequence_be, in.data() + offset, sizeof(sequence_be));
    offset += sizeof(sequence_be);

    std::memcpy(&tick_be, in.data() + offset, sizeof(tick_be));
    offset += sizeof(tick_be);

    header.type = static_cast<FrameType>(in[offset]);
    offset += sizeof(uint8_t);

    std::memcpy(&length_be, in.data() + offset, sizeof(length_be));
    offset += sizeof(length_be);

    header.sequence       = ntohl(sequence_be);
    header.tick           = ntohl(tick_be);
    header.payload_length = ntohs(length_be);

    return header;
}

std::vector<uint8_t> build_frame(const FrameHeader& header, const std::vector<uint8_t>& payload)
{
    std::array<uint8_t, kHeaderSize> header_bytes{};
    serialize_header(header, header_bytes);

    std::vector<uint8_t> frame;
    frame.reserve(kHeaderSize + payload.size());

    frame.insert(frame.end(), header_bytes.begin(), header_bytes.end());
    frame.insert(frame.end(), payload.begin(), payload.end());

    return frame;
}

bool parse_frame(const std::vector<uint8_t>& raw, FrameHeader& out_header, std::vector<uint8_t>& out_payload)
{
    if (raw.size() < kHeaderSize) { return false; }

    std::array<uint8_t, kHeaderSize> header_bytes{};
    std::copy(raw.begin(), raw.begin() + kHeaderSize, header_bytes.begin());
    out_header = deserialize_header(header_bytes);

    std::size_t actual_payload_size = raw.size() - kHeaderSize;
    if (actual_payload_size != out_header.payload_length) { return false; }

    out_payload.assign(raw.begin() + kHeaderSize, raw.end());

    return true;
}

}