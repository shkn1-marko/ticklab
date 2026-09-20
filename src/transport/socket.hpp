#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace ticklab::transport
{

constexpr std::size_t kMaxDatagramSize = 2048;

enum class SocketResult
{
    Success,
    BindFailed,
    SendFailed,
};

class UdpSocket
{
public:
    static std::optional<UdpSocket> create();

    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    SocketResult bind(uint16_t port);
    SocketResult send_to(const std::string& address, uint16_t port, const std::vector<uint8_t>& data);

    bool receive(std::vector<uint8_t>& out, std::string& from_address, uint16_t& from_port);

private:
    UdpSocket() = default;

    struct Impl;
    Impl* impl_ = nullptr;
};

}