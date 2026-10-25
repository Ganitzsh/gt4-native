#pragma once
// The host's network service: the one place game network shims reach the operating system.
// Message based and non-blocking; the game shim polls it once per simulation tick (docs/PLATFORM.md).
// The retail network code is not ported yet: NullNetService reports no adapter, so the game takes
// its offline paths, as on a PS2 without a network adapter.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gt4::platform {

struct NetAddress {
    std::array<std::uint8_t, 16> bytes{};  // IPv4 in the first four bytes, or IPv6
    std::uint16_t port = 0;
    bool ipv6 = false;
};

enum class NetStatus {
    unavailable,  // no adapter: the game must take its offline paths
    ready,
};

class NetService {
public:
    virtual ~NetService() = default;
    virtual NetStatus status() const = 0;
    // Queue one datagram; false when it cannot be sent (no adapter, queue full).
    virtual bool send(const NetAddress& to, std::span<const std::byte> payload) = 0;
    // Take the next received datagram into `buffer`; returns its size, 0 when none is waiting.
    virtual std::size_t receive(NetAddress& from, std::span<std::byte> buffer) = 0;
    // Called once per simulation tick, at a fixed point, before the game reads its messages.
    virtual void poll() = 0;
};

class NullNetService final : public NetService {
public:
    NetStatus status() const override { return NetStatus::unavailable; }
    bool send(const NetAddress&, std::span<const std::byte>) override { return false; }
    std::size_t receive(NetAddress&, std::span<std::byte>) override { return 0; }
    void poll() override {}
};

}  // namespace gt4::platform
