#include "gt4/native/object_fields.hpp"
#include <array>
#include <bit>
#include <iostream>
#include <string_view>

static int object_demo(bool quiet) {
    namespace fields = gt4::native::object_fields;
    std::array<std::byte, fields::Layout::wire_size> ram{};
    gt4::guest::AddressSpace memory;
    constexpr gt4::guest::Address address = 0x00130000;
    memory.map(address, ram);
    gt4::guest::NativeCallbacks callbacks;
    fields::bind_callbacks(callbacks, memory);
    const auto call = [&](fields::Function function, std::uint32_t argument = 0) {
        return callbacks.call(static_cast<gt4::guest::Address>(function), address, argument);
    };
    (void)call(fields::Function::set_174, 120);
    (void)call(fields::Function::set_178, 400);
    const auto result = std::bit_cast<float>(call(fields::Function::normalized));
    if (result != 30.0f || memory.read32_le(address + 0x174) != 120 ||
        memory.read32_le(address + 0x178) != 400) return 1;
    if (!quiet)
        std::cout << "GT4 guest object @ 0x00130000: value=120, divisor field=400, "
                     "original normalized result=" << result << '\n'
                  << "13 original accessor/calculation bodies are linked through reviewed native adapters.\n";
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--self-test") return object_demo(true);
    if (argc > 1 && std::string_view(argv[1]) == "--object-demo") return object_demo(false);
    std::cout << "GT4 Native bootstrap - game runtime not yet integrated.\n"
                 "Use --object-demo to run the original accessor chain over checked PS2-format memory.\n";
    return 0;
}
