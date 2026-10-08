#include "frame.hpp"
#include "socket.hpp"
#include "sequence_tracker.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace ticklab::transport;

std::atomic<bool> g_running{ true };

void handle_signal(int)
{
    g_running = false;
}

struct Counters
{
    uint64_t received = 0;
    uint64_t in_order = 0;
    uint64_t gap_events = 0;
    uint64_t missing = 0;
    uint64_t reordered = 0;
    uint64_t duplicates = 0;
    uint64_t too_old = 0;
    uint64_t rejected = 0;
};

bool parse_port(const char* text, uint16_t& out)
{
    if (text[0] < '0' || text[0] > '9') { return false; }

    char* end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 10);

    if (*end != '\0' || value > 65535) { return false; }

    out = static_cast<uint16_t>(value);
    return true;
}

void print_summary(const Counters& counters)
{
    std::cout << "\n--- summary ---\n"
              << "frames received : " << counters.received << "\n"
              << "in order        : " << counters.in_order << "\n"
              << "gap events      : " << counters.gap_events
              << " (" << counters.missing << " frames missing)\n"
              << "reordered       : " << counters.reordered << "\n"
              << "duplicates      : " << counters.duplicates << "\n"
              << "too old         : " << counters.too_old << "\n"
              << "rejected        : " << counters.rejected << "\n";
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: receiver <port>\n";
        return 2;
    }

    uint16_t port = 0;
    if (!parse_port(argv[1], port))
    {
        std::cerr << "receiver: invalid port '" << argv[1] << "'\n";
        return 2;
    }

    auto sock = UdpSocket::create();
    if (!sock)
    {
        std::cerr << "receiver: failed to create socket\n";
        return 1;
    }

    if (sock->bind(port) != SocketResult::Success)
    {
        std::cerr << "receiver: failed to bind UDP port " << port << "\n";
        return 1;
    }

    const auto bound_port = sock->local_port();
    if (!bound_port)
    {
        std::cerr << "receiver: bound, but could not read back the port\n";
        return 1;
    }

    std::cout << "listening on UDP port " << *bound_port << " (Ctrl+C to stop)\n";

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    SequenceTracker tracker;
    Counters counters;
    std::string current_sender;

    std::vector<uint8_t> raw;
    std::string from_address;
    uint16_t from_port = 0;

    while (g_running)
    {
        if (!sock->receive(raw, from_address, from_port))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        const std::string sender = from_address + ":" + std::to_string(from_port);

        FrameHeader header{};
        std::vector<uint8_t> payload;

        if (!parse_frame(raw, header, payload))
        {
            ++counters.rejected;
            std::cout << "REJECTED: " << raw.size() << " bytes from " << sender << "\n";
            continue;
        }

        if (sender != current_sender)
        {
            tracker = SequenceTracker{};
            current_sender = sender;
            std::cout << "new sender " << sender << ", tracker reset\n";
        }

        ++counters.received;

        const Observation observation = tracker.observe(header.sequence);

        switch (observation.status)
        {
        case SequenceStatus::InOrder:
            ++counters.in_order;
            break;

        case SequenceStatus::Gap:
        {
            ++counters.gap_events;
            counters.missing += observation.missing_count;

            const uint32_t last_missing = observation.first_missing + observation.missing_count - 1;
            std::cout << "GAP: sequence " << header.sequence << " arrived, missing "
                      << observation.first_missing << ".." << last_missing
                      << " (" << observation.missing_count << " frames)\n";
            break;
        }

        case SequenceStatus::Reordered:
            ++counters.reordered;
            std::cout << "REORDERED: sequence " << header.sequence << " arrived late\n";
            break;

        case SequenceStatus::Duplicate:
            ++counters.duplicates;
            std::cout << "DUPLICATE: sequence " << header.sequence << "\n";
            break;

        case SequenceStatus::TooOld:
            ++counters.too_old;
            std::cout << "TOO OLD: sequence " << header.sequence << "\n";
            break;
        }
    }

    print_summary(counters);
    return 0;
}