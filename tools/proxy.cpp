#include "conditioner.hpp"
#include "socket.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace ticklab::conditioner;
using ticklab::transport::SocketResult;
using ticklab::transport::UdpSocket;

namespace
{

std::atomic<bool> g_running{ true };

void handle_signal(int)
{
    g_running = false;
}

constexpr auto kStatsInterval = std::chrono::seconds(5);

bool parse_port(const char* text, uint16_t& out)
{
    if (text[0] < '0' || text[0] > '9') { return false; }

    char* end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 10);

    if (*end != '\0' || value > 65535) { return false; }

    out = static_cast<uint16_t>(value);
    return true;
}

bool parse_probability(const char* text, double& out)
{
    char* end = nullptr;
    const double value = std::strtod(text, &end);

    if (end == text || *end != '\0' || value < 0.0 || value > 1.0) { return false; }

    out = value;
    return true;
}

bool parse_milliseconds(const char* text, std::chrono::milliseconds& out)
{
    if (text[0] < '0' || text[0] > '9') { return false; }

    char* end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 10);

    if (*end != '\0') { return false; }

    out = std::chrono::milliseconds(value);
    return true;
}

bool parse_seed(const char* text, uint64_t& out)
{
    if (text[0] < '0' || text[0] > '9') { return false; }

    char* end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 10);

    if (*end != '\0') { return false; }

    out = value;
    return true;
}

uint64_t random_seed()
{
    std::random_device device;
    return (static_cast<uint64_t>(device()) << 32) | device();
}

void print_usage()
{
    std::cerr << "usage: proxy <listen_port> <server_ip> <server_port>"
                 " [--loss P] [--duplicate P] [--delay MS] [--jitter MS] [--seed N]\n";
}

struct Endpoint
{
    std::string address;
    uint16_t port = 0;

    bool operator==(const Endpoint&) const = default;
};

struct Direction
{
    Conditioner conditioner;
    uint64_t in = 0;
    uint64_t out = 0;
};

}

int main(int argc, char** argv)
{
    if (argc < 4 || (argc - 4) % 2 != 0)
    {
        print_usage();
        return 2;
    }

    uint16_t listen_port = 0;
    if (!parse_port(argv[1], listen_port))
    {
        std::cerr << "proxy: invalid listen port '" << argv[1] << "'\n";
        return 2;
    }

    uint16_t server_port = 0;
    if (!parse_port(argv[3], server_port))
    {
        std::cerr << "proxy: invalid server port '" << argv[3] << "'\n";
        return 2;
    }
    const Endpoint server_endpoint{ argv[2], server_port };

    Config config;
    uint64_t seed = random_seed();

    for (int i = 4; i < argc; i += 2)
    {
        const char* flag = argv[i];
        const char* value = argv[i + 1];

        bool ok = false;
        if (std::strcmp(flag, "--loss") == 0)           { ok = parse_probability(value, config.loss); }
        else if (std::strcmp(flag, "--duplicate") == 0) { ok = parse_probability(value, config.duplicate); }
        else if (std::strcmp(flag, "--delay") == 0)     { ok = parse_milliseconds(value, config.delay); }
        else if (std::strcmp(flag, "--jitter") == 0)    { ok = parse_milliseconds(value, config.jitter); }
        else if (std::strcmp(flag, "--seed") == 0)      { ok = parse_seed(value, seed); }

        if (!ok)
        {
            std::cerr << "proxy: invalid option '" << flag << " " << value << "'\n";
            print_usage();
            return 2;
        }
    }

    auto client_side = UdpSocket::create();
    if (!client_side)
    {
        std::cerr << "proxy: failed to create socket\n";
        return 1;
    }

    if (client_side->bind(listen_port) != SocketResult::Success)
    {
        std::cerr << "proxy: failed to bind UDP port " << listen_port << "\n";
        return 1;
    }

    std::cout << "proxy listening on UDP port " << listen_port
              << ", forwarding to " << server_endpoint.address << ":" << server_endpoint.port << "\n"
              << "both directions: loss " << config.loss << ", duplicate " << config.duplicate
              << ", delay " << config.delay.count() << " ms, jitter " << config.jitter.count() << " ms"
              << ", seed " << seed << " (Ctrl+C to stop)\n";

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    Direction up{ Conditioner{ config, seed } };
    Direction down{ Conditioner{ config, seed + 1 } };

    // The server tells clients apart by address. A restarted client arrives from a new port, so
    // the proxy opens a fresh server-side socket for it; otherwise the server would see the
    // restarted client at the old address and never treat it as a reconnect
    std::optional<UdpSocket> server_side;
    std::optional<Endpoint> client;

    const TimePoint start = Clock::now();
    TimePoint next_stats = start + kStatsInterval;

    std::vector<uint8_t> raw;
    std::string from_address;
    uint16_t from_port = 0;

    while (g_running)
    {
        const TimePoint now = Clock::now();

        while (client_side->receive(raw, from_address, from_port))
        {
            const Endpoint from{ from_address, from_port };

            if (from != client)
            {
                server_side = UdpSocket::create();
                if (!server_side)
                {
                    std::cerr << "proxy: failed to create server-side socket\n";
                    return 1;
                }

                client = from;
                std::cout << "client is " << from.address << ":" << from.port << "\n";
            }

            ++up.in;
            up.conditioner.send(raw, now);
        }

        while (server_side && server_side->receive(raw, from_address, from_port))
        {
            if (Endpoint{ from_address, from_port } != server_endpoint) { continue; }

            ++down.in;
            down.conditioner.send(raw, now);
        }

        for (const Datagram& datagram : up.conditioner.poll(now))
        {
            ++up.out;
            if (server_side->send_to(server_endpoint.address, server_endpoint.port, datagram) != SocketResult::Success)
            {
                std::cerr << "proxy: send to server failed\n";
            }
        }

        for (const Datagram& datagram : down.conditioner.poll(now))
        {
            ++down.out;
            if (client_side->send_to(client->address, client->port, datagram) != SocketResult::Success)
            {
                std::cerr << "proxy: send to client failed\n";
            }
        }

        if (now >= next_stats)
        {
            next_stats += kStatsInterval;

            const double seconds = std::chrono::duration<double>(now - start).count();
            std::cout << "[" << std::fixed << std::setprecision(3) << seconds << " s] "
                      << "up " << up.in << " in / " << up.out << " out, "
                      << "down " << down.in << " in / " << down.out << " out\n";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::cout << "\nproxy stopped\n";
    return 0;
}
