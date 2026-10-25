// Forced include for the vendored AMD FidelityFX FSR 2.2.1 sources (third_party/fidelityfx-fsr2), so they
// build unmodified on every compiler of this project:
//  - Vulkan entry points come from volk (VK_NO_PROTOTYPES), loaded by the host at start-up;
//  - the few MSVC-only CRT helpers they use get portable definitions elsewhere.
#pragma once
#include <volk.h>
#ifdef __cplusplus
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#if !defined(_MSC_VER)
#ifndef _countof
#define _countof(array) (sizeof(array) / sizeof((array)[0]))
#endif
template <size_t N>
inline int wcscpy_s(wchar_t (&destination)[N], const wchar_t* source) {
    std::wcsncpy(destination, source, N - 1);
    destination[N - 1] = L'\0';
    return 0;
}
inline int wcstombs_s(size_t* converted, char* destination, size_t destinationSize, const wchar_t* source, size_t count) {
    size_t limit = count < destinationSize ? count : destinationSize - 1;
    size_t result = std::wcstombs(destination, source, limit);
    if (result == static_cast<size_t>(-1)) result = 0;
    destination[result < destinationSize ? result : destinationSize - 1] = '\0';
    if (converted) *converted = result + 1;
    return 0;
}
#endif
#endif
