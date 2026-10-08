#include "frame.hpp"
#include "socket.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace ticklab::transport;

namespace
{

bool parse_unsigned(const char* text, uint64_t max_value, uint64_t& out)
{
    if (text[0] < '0' || text[0] > '9') { return false; }

    char* end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 10);

    if (*end != '\0' || value > max_value) { return false; }

    out = value;
    return true;
}

void print_usage()
{
    std::cerr << "usage: sender <dest_ip> <port> <count> <interval_ms> [skip_sequence ...]\n";
}

}

int main(int argc, char** argv)
{
    if (argc < 5)
    {
        print_usage();
        return 2;
    }

    const std::string dest_ip = argv[1];

    uint64_t port = 0;
    uint64_t count = 0;
    uint64_t interval_ms = 0;

    if (!parse_unsigned(argv[2], 65535, port) ||
        !parse_unsigned(argv[3], UINT32_MAX, count) ||
        !parse_unsigned(argv[4], UINT32_MAX, interval_ms))
    {
        print_usage();
        return 2;
    }

    std::set<uint32_t> skipped;

    for (int i = 5; i < argc; ++i)
    {
        uint64_t sequence = 0;
        if (!parse_unsigned(argv[i], UINT32_MAX, sequence))
        {
            std::cerr << "sender: invalid skip sequence '" << argv[i] << "'\n";
            return 2;
        }
        skipped.insert(static_cast<uint32_t>(sequence));
    }

    auto sock = UdpSocket::create();
    if (!sock)
    {
        std::cerr << "sender: failed to create socket\n";
        return 1;
    }

    const std::vector<uint8_t> payload{ 'p', 'i', 'n', 'g' };

    uint64_t sent = 0;

    for (uint64_t i = 0; i < count; ++i)
    {
        const uint32_t sequence = static_cast<uint32_t>(i);

        if (skipped.count(sequence) != 0)
        {
            std::cout << "skipping sequence " << sequence << "\n";
        }
        else
        {
            const FrameHeader header{ sequence, sequence, FrameType::Data,
                                      static_cast<uint16_t>(payload.size()) };

            if (sock->send_to(dest_ip, static_cast<uint16_t>(port), build_frame(header, payload))
                != SocketResult::Success)
            {
                std::cerr << "sender: send failed at sequence " << sequence
                          << " (is '" << dest_ip << "' a numeric IPv4 address?)\n";
                return 1;
            }

            ++sent;
        }

        if (interval_ms > 0 && i + 1 < count)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        }
    }

    std::cout << "done: sent " << sent << " of " << count << " sequence numbers, "
              << (count - sent) << " skipped on purpose\n";
    return 0;
}