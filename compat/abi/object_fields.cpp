#include "gt4/native/object_fields.hpp"
#include "gt4/guest_field.hpp"
#include <array>
#include <bit>

namespace gt4::native::object_fields::detail {

struct View {
    guest::guest_field<std::int32_t> unk174, unk178, unk17C, unk180, unk184, unk188;

    template<class Memory>
    View(Memory& memory, Object object)
        : unk174(memory, object.field<std::int32_t>(0x174)),
          unk178(memory, object.field<std::int32_t>(0x178)),
          unk17C(memory, object.field<std::int32_t>(0x17C)),
          unk180(memory, object.field<std::int32_t>(0x180)),
          unk184(memory, object.field<std::int32_t>(0x184)),
          unk188(memory, object.field<std::int32_t>(0x188)) {
        // Require the entire explicitly supported object extent to be mapped.
        (void)memory.read(object.address(), Layout::wire_size);
    }
};

} // namespace gt4::native::object_fields::detail

#include "object_fields_functions.inc"

namespace gt4::native::object_fields {

#define GT4_SETTER(name, original) \
    void name(guest::AddressSpace& memory, Object object, std::int32_t value) { \
        detail::View view(memory, object); detail::original(&view, value); \
    }
GT4_SETTER(set_174, func_005C9930)
GT4_SETTER(set_178, func_005C9938)
GT4_SETTER(set_17c, func_005C9940)
GT4_SETTER(set_180, func_005C9948)
GT4_SETTER(set_188, func_005C9950)
#undef GT4_SETTER

#define GT4_GETTER(type, name, original) \
    type name(const guest::AddressSpace& memory, Object object) { \
        detail::View view(memory, object); return detail::original(&view); \
    }
GT4_GETTER(std::int32_t, get_174, func_005C9958)
GT4_GETTER(std::int32_t, get_178, func_005C9960)
GT4_GETTER(std::int32_t, get_17c, func_005C99A8)
GT4_GETTER(std::int32_t, get_180, func_005C99B0)
GT4_GETTER(std::int32_t, get_184, func_005C9968)
GT4_GETTER(std::int32_t, get_188, func_005C99B8)
GT4_GETTER(float, divisor, func_001465D8)
GT4_GETTER(float, normalized, func_005C9970)
#undef GT4_GETTER

void bind_callbacks(guest::NativeCallbacks& callbacks, guest::AddressSpace& memory) {
    constexpr std::array functions{Function::divisor, Function::normalized,
        Function::set_174, Function::set_178, Function::set_17c, Function::set_180,
        Function::set_188, Function::get_174, Function::get_178, Function::get_17c,
        Function::get_180, Function::get_184, Function::get_188};
    for (auto function : functions)
        if (callbacks.contains(static_cast<guest::Address>(function)))
            throw std::invalid_argument("object accessor callback already registered");

    const auto setter = [&](Function function, auto call) {
        callbacks.bind(static_cast<guest::Address>(function),
            [&memory, call](guest::Address object, std::uint32_t argument) {
                call(memory, Object(object), std::bit_cast<std::int32_t>(argument));
                return std::uint32_t{0};
            });
    };
    setter(Function::set_174, set_174); setter(Function::set_178, set_178);
    setter(Function::set_17c, set_17c); setter(Function::set_180, set_180);
    setter(Function::set_188, set_188);
    const auto getter = [&](Function function, auto call) {
        callbacks.bind(static_cast<guest::Address>(function),
            [&memory, call](guest::Address object, std::uint32_t) {
                return std::bit_cast<std::uint32_t>(call(memory, Object(object)));
            });
    };
    getter(Function::get_174, get_174); getter(Function::get_178, get_178);
    getter(Function::get_17c, get_17c); getter(Function::get_180, get_180);
    getter(Function::get_184, get_184); getter(Function::get_188, get_188);
    getter(Function::divisor, divisor); getter(Function::normalized, normalized);
}

} // namespace gt4::native::object_fields
