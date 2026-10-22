#include "client_connector.hpp"
#include "socket.hpp"
 
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
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
 
// how long to wait before connecting again after the connection ended
constexpr auto kRetryDelay = std::chrono::seconds(1);
 
bool parse_port(const char* text, uint16_t& out)
{
    if (text[0] < '0' || text[0] > '9') { return false; }
 
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 10);
 
    if (*end != '\0' || value > 65535) { return false; }
 
    out = static_cast<uint16_t>(value);
    return true;
}
 
const char* to_string(ClientEvent event)
{
    switch (event)
    {
    case ClientEvent::Connected:               return "CONNECTED";
    case ClientEvent::RejectedVersionMismatch: return "REJECTED (version mismatch)";
    case ClientEvent::RejectedServerFull:      return "REJECTED (server full)";
    case ClientEvent::GaveUp:                  return "GAVE UP (no answer)";
    case ClientEvent::TimedOut:                return "TIMED OUT (server silent)";
    case ClientEvent::Disconnected:            return "DISCONNECTED (server said Goodbye)";
    }
 
    return "?";
}
 
// Identity file: "client_id token" on one line. Missing or unreadable means a new client.
Identity load_identity(const std::string& path)
{
    std::ifstream in(path);
 
    Identity identity;
    if (in >> identity.client_id >> identity.token) { return identity; }
 
    return Identity{};
}
 
void save_identity(const std::string& path, const Identity& identity)
{
    std::ofstream out(path, std::ios::trunc);
    out << identity.client_id << ' ' << identity.token << '\n';
 
    if (!out) { std::cerr << "client: could not save identity to " << path << "\n"; }
}
 
struct Session
{
    UdpSocket& socket;
    ClientConnector& connector;
    std::string identity_path;
    TimePoint start;
 
    std::optional<TimePoint> retry_at; // set when the connection ended and we will try again
};
 
// The only place the client touches the network, the disk or the console:
// send every frame, act on every event.
void handle(const ClientResult& result, Session& session, TimePoint now)
{
    const Endpoint& server = session.connector.server();
 
    for (const auto& frame : result.frames)
    {
        if (session.socket.send_to(server.address, server.port, frame) != SocketResult::Success)
        {
            std::cerr << "client: send failed (is '" << server.address << "' a numeric IPv4 address?)\n";
        }
    }
 
    const double seconds = std::chrono::duration<double>(now - session.start).count();
 
    for (ClientEvent event : result.events)
    {
        std::cout << "[" << std::fixed << std::setprecision(3) << seconds << " s] " << to_string(event);
 
        switch (event)
        {
        case ClientEvent::Connected:
            std::cout << " as client " << session.connector.identity().client_id;
            save_identity(session.identity_path, session.connector.identity());
            break;
 
        case ClientEvent::GaveUp:
        case ClientEvent::TimedOut:
        case ClientEvent::Disconnected:
            std::cout << ", retrying in " << std::chrono::seconds(kRetryDelay).count() << " s";
            session.retry_at = now + kRetryDelay;
            break;
 
        case ClientEvent::RejectedVersionMismatch:
        case ClientEvent::RejectedServerFull:
            g_running = false; // retrying would not help
            break;
        }
 
        std::cout << "\n";
    }
}
 
}

int main(int argc, char** argv)
{
    if (argc != 3 && argc != 4)
    {
        std::cerr << "usage: client <server_ip> <port> [identity_file]\n";
        return 2;
    }
 
    const std::string server_address = argv[1];
 
    uint16_t server_port = 0;
    if (!parse_port(argv[2], server_port))
    {
        std::cerr << "client: invalid port '" << argv[2] << "'\n";
        return 2;
    }
 
    const std::string identity_path = (argc == 4) ? argv[3] : "ticklab_identity.txt";
 
    // Not bound on purpose: the OS picks a fresh port each run, so a restarted
    // client really does arrive from a new endpoint.
    auto socket = UdpSocket::create();
    if (!socket)
    {
        std::cerr << "client: failed to create socket\n";
        return 1;
    }
 
    const Identity previous = load_identity(identity_path);
 
    if (previous.client_id != kNoClientId)
    {
        std::cout << "loaded identity: client " << previous.client_id << " from " << identity_path << "\n";
    }
    else
    {
        std::cout << "no saved identity, connecting as a new client\n";
    }
 
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
 
    ClientConnector connector{ Endpoint{ server_address, server_port }, previous };
    Session session{ *socket, connector, identity_path, Clock::now(), std::nullopt };
 
    std::cout << "connecting to " << server_address << ":" << server_port << " (Ctrl+C to stop)\n";
    handle(connector.connect(Clock::now()), session, Clock::now());
 
    std::vector<uint8_t> raw;
    std::string from_address;
    uint16_t from_port = 0;
 
    while (g_running)
    {
        const TimePoint now = Clock::now();
 
        while (socket->receive(raw, from_address, from_port))
        {
            handle(connector.on_datagram(Endpoint{ from_address, from_port }, raw, now), session, now);
        }
 
        handle(connector.update(now), session, now);
 
        if (session.retry_at && now >= *session.retry_at)
        {
            session.retry_at.reset();
            handle(connector.connect(now), session, now);
        }
 
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
 
    const bool was_connected = connector.state() == ClientState::Connected;
    handle(connector.disconnect(Clock::now()), session, Clock::now());
 
    std::cout << "\nclient stopped" << (was_connected ? ", sent Goodbye" : "") << "\n";
    return 0;
}