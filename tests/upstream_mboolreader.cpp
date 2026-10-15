#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>

extern "C" void MboolReader__structor_19(void* object, int flags);

// Host-only link shims. They are deliberately test doubles, not game behavior.
void* MboolReader__vtable = nullptr;
namespace {
int base_calls = 0;
int release_calls = 0;
void* base_object = nullptr;
void* base_second_argument = reinterpret_cast<void*>(std::uintptr_t{1});
void* released_object = nullptr;
}
extern "C" void MReaderBase__structor_0(void* object, void* second) {
    ++base_calls;
    base_object = object;
    base_second_argument = second;
}
extern "C" void func_005C1628(void* object) {
    ++release_calls;
    released_object = object;
}
#define CHECK(expr) do { if (!(expr)) { std::cerr << "GT4 function check failed: " #expr << '\n'; return 1; } } while(false)
int main() {
    alignas(void*) std::array<std::byte, 64> object{};
    MboolReader__structor_19(object.data(), 0);
    CHECK(base_calls == 1);
    CHECK(release_calls == 0);
    CHECK(base_object == object.data());
    CHECK(base_second_argument == nullptr);
    CHECK(*reinterpret_cast<void**>(object.data()) == &MboolReader__vtable);

    MboolReader__structor_19(object.data(), 1);
    CHECK(base_calls == 2);
    CHECK(release_calls == 1);
    CHECK(released_object == object.data());

    MboolReader__structor_19(object.data(), 2);
    CHECK(base_calls == 3);
    CHECK(release_calls == 1);

    MboolReader__structor_19(object.data(), 3);
    CHECK(base_calls == 4);
    CHECK(release_calls == 2);
    std::cout << "GT4 MboolReader__structor_19 native test passed\n";
}
