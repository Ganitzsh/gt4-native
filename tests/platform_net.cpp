#include "gt4/platform/net.hpp"

#include <array>
#include <cstdio>

int main() {
    gt4::platform::NullNetService net;
    gt4::platform::NetService& service = net;
    int failures = 0;
    const auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "FAIL: %s\n", what);
            ++failures;
        }
    };
    check(service.status() == gt4::platform::NetStatus::unavailable, "null service reports no adapter");
    std::array<std::byte, 8> payload{};
    gt4::platform::NetAddress address;
    check(!service.send(address, payload), "null service refuses to send");
    service.poll();
    check(service.receive(address, payload) == 0, "null service receives nothing");
    return failures == 0 ? 0 : 1;
}
