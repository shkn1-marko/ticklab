#include "socket.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

namespace ticklab::transport
{

namespace
{

struct WinsockInit
{
    bool ok = false;

    WinsockInit()
    {
        WSADATA wsa_data{};
        ok = (WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0);
    }

    ~WinsockInit()
    {
        if (ok) { WSACleanup(); }
    }
};

}

struct UdpSocket::Impl
{
    SOCKET sock = INVALID_SOCKET;
};

std::optional<UdpSocket> UdpSocket::create()
{
    static WinsockInit winsock_init;
    if (!winsock_init.ok) { return std::nullopt; }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) { return std::nullopt; }

    u_long non_blocking = 1;
    if (ioctlsocket(sock, FIONBIO, &non_blocking) != 0)
    {
        closesocket(sock);
        return std::nullopt;
    }

    UdpSocket socket_obj;
    socket_obj.impl_ = new Impl{ sock };
    return socket_obj;
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept : impl_(other.impl_)
{
    other.impl_ = nullptr;
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept
{
    if (this != &other)
    {
        if (impl_)
        {
            if (impl_->sock != INVALID_SOCKET) { closesocket(impl_->sock); }

            delete impl_;
        }

        impl_ = other.impl_;
        other.impl_ = nullptr;
    }

    return *this;
}

UdpSocket::~UdpSocket()
{
    if (impl_)
    {
        if (impl_->sock != INVALID_SOCKET) { closesocket(impl_->sock); }

        delete impl_;
    }
}

SocketResult UdpSocket::bind(uint16_t port)
{
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(impl_->sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        return SocketResult::BindFailed;
    }

    return SocketResult::Success;
}

std::optional<uint16_t> UdpSocket::local_port() const
{
    sockaddr_in addr{};
    int len = sizeof(addr);

    if (getsockname(impl_->sock, reinterpret_cast<sockaddr*>(&addr), &len) != 0) { return std::nullopt; }

    uint16_t port = ntohs(addr.sin_port);
    if (port == 0) { return std::nullopt; }

    return port;
}

SocketResult UdpSocket::send_to(const std::string& address, uint16_t port, const std::vector<uint8_t>& data)
{
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, address.c_str(), &addr.sin_addr) != 1)
    {
        return SocketResult::SendFailed;
    }

    int sent = ::sendto(impl_->sock, reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), 0,
                        reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    if (sent < 0 || static_cast<size_t>(sent) != data.size())
    {
        return SocketResult::SendFailed;
    }

    return SocketResult::Success;
}

bool UdpSocket::receive(std::vector<uint8_t>& out, std::string& from_address, uint16_t& from_port)
{
    std::vector<uint8_t> buffer(kMaxDatagramSize + 1);

    sockaddr_in sender_addr{};
    int sender_len = sizeof(sender_addr);

    int received = ::recvfrom(impl_->sock, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
                              reinterpret_cast<sockaddr*>(&sender_addr), &sender_len);

    if (received <= 0) { return false; }
    if (static_cast<size_t>(received) > kMaxDatagramSize) { return false; }

    buffer.resize(static_cast<size_t>(received));
    out = std::move(buffer);

    char address_buf[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &sender_addr.sin_addr, address_buf, sizeof(address_buf));
    from_address = address_buf;
    from_port = ntohs(sender_addr.sin_port);

    return true;
}

}