#include "socket.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

namespace ticklab::transport
{

struct UdpSocket::Impl
{
    int fd = -1;
};

std::optional<UdpSocket> UdpSocket::create()
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { return std::nullopt; }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        close(fd);
        return std::nullopt;
    }

    UdpSocket socket_obj;
    socket_obj.impl_ = new Impl{ fd };
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
            if (impl_->fd >= 0) { close(impl_->fd); }

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
        if (impl_->fd >= 0) { close(impl_->fd); }

        delete impl_;
    }
}

SocketResult UdpSocket::bind(uint16_t port)
{
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(impl_->fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        return SocketResult::BindFailed;
    }

    return SocketResult::Success;
}

std::optional<uint16_t> UdpSocket::local_port() const
{
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);

    if (getsockname(impl_->fd, reinterpret_cast<sockaddr*>(&addr), len) < 0) { return std::nullopt; }

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

    ssize_t sent = ::sendto(impl_->fd, data.data(), data.size(), 0,
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
    socklen_t sender_len = sizeof(sender_addr);

    ssize_t received = ::recvfrom(impl_->fd, buffer.data(), buffer.size(), 0,
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