#include "connection_table.hpp"
#include "socket.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace ticklab::connection;
using ticklab::transport::SocketResult;
using ticklab::transport::UdpSocket;

namespace
{

std::atomic<bool> g_running{ true };

void handle_signal(int)
{
    g_running = false;
}

bool parse_port(const char* text, uint16_t& out)
{
    if (text[0] < '0' || text[0] > '9') { return false; }
 
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 10);
 
    if (*end != '\0' || value > 65535) { return false; }
 
    out = static_cast<uint16_t>(value);
    return true;
}

std::string to_string(const Endpoint& endpoint)
{
    return endpoint.address + ":" + std::to_string(endpoint.port);
}

const char* to_string(EventType type)
{
    switch (type)
    {
    case EventType::Connected:    return "CONNECTED";
    case EventType::Reconnected:  return "RECONNECTED";
    case EventType::Disconnected: return "DISCONNECTED";
    case EventType::TimedOut:     return "TIMED OUT";
    case EventType::Rejected:     return "REJECTED";
    }
 
    return "?";
}

uint64_t random_seed()
{
    std::random_device device;
    return (static_cast<uint64_t>(device()) << 32) | device();
}

void handle(const Result& result, UdpSocket& socket, const ConnectionTable& table,
            TimePoint start, TimePoint now)
{
    for (const OutgoingFrame& frame : result.frames)
    {
        if (socket.send_to(frame.to.address, frame.to.port, frame.bytes) != SocketResult::Success)
        {
            std::cerr << "server: send failed to " << to_string(frame.to) << "\n";
        }
    }
 
    const double seconds = std::chrono::duration<double>(now - start).count();
 
    for (const Event& event : result.events)
    {
        std::cout << "[" << std::fixed << std::setprecision(3) << seconds << " s] "
                  << to_string(event.type);
 
        if (event.client_id != kNoClientId) { std::cout << " client " << event.client_id; }
 
        std::cout << " at " << to_string(event.endpoint)
                  << " (" << table.size() << "/" << kMaxClients << " connected)\n";
    }
}

}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: server <port>\n";
        return 2;
    }
 
    uint16_t port = 0;
    if (!parse_port(argv[1], port))
    {
        std::cerr << "server: invalid port '" << argv[1] << "'\n";
        return 2;
    }
 
    auto socket = UdpSocket::create();
    if (!socket)
    {
        std::cerr << "server: failed to create socket\n";
        return 1;
    }
 
    if (socket->bind(port) != SocketResult::Success)
    {
        std::cerr << "server: failed to bind UDP port " << port << "\n";
        return 1;
    }
 
    const auto bound_port = socket->local_port();
    if (!bound_port)
    {
        std::cerr << "server: bound, but could not read back the port\n";
        return 1;
    }
 
    std::cout << "server listening on UDP port " << *bound_port
              << " (max " << kMaxClients << " clients, Ctrl+C to stop)\n";
 
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
 
    ConnectionTable table{ random_seed() };
    const TimePoint start = Clock::now();
 
    std::vector<uint8_t> raw;
    std::string from_address;
    uint16_t from_port = 0;
 
    while (g_running)
    {
        const TimePoint now = Clock::now();
 
        // drain everything that arrived since the last pass
        while (socket->receive(raw, from_address, from_port))
        {
            handle(table.on_datagram(Endpoint{ from_address, from_port }, raw, now),
                   *socket, table, start, now);
        }
 
        handle(table.update(now), *socket, table, start, now);
 
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
 
    const TimePoint now = Clock::now();
    const std::size_t remaining = table.size();
    handle(table.close_all(now), *socket, table, start, now);
 
    std::cout << "\nserver stopped, sent Goodbye to " << remaining << " client(s)\n";
    return 0;
}