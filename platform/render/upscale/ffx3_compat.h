// Forced include for the vendored FidelityFX SDK 1.1.4 (FSR 3.1's upscaler; third_party/fidelityfx-sdk, PLAN.md B17), so
// it builds unmodified on every compiler of this project, as ffx_compat.h does for FSR 2. gt4_fsr3 forces it PUBLIC
// (cmake/Host.cmake), so every C++ file of gt4_fsr3 and of a target linking it is compiled with it, and all of them
// agree on the context's size below.
#pragma once
#include "ffx_compat.h"   // volk, the MSVC CRT helpers FSR 2 needs too
#ifdef __cplusplus
#include <bit>      // std::popcount (ffx_util.h; MSVC's headers include it transitively)
#include <cmath>    // log2, floor (ffx_vk.cpp)
#include <cstdio>
#include <cwchar>
#include <cstring>
#if !defined(_MSC_VER)
// (ffx_vk.cpp's non-Windows UTF-16 to UTF-8 conversion)
inline int strcpy_s(char* destination, size_t destinationSize, const char* source) {
    if (!destination || destinationSize == 0) return 22;   // EINVAL
    std::strncpy(destination, source ? source : "", destinationSize - 1);
    destination[destinationSize - 1] = '\0';
    return 0;
}
// (ffx_vk.cpp's non-Windows UTF-8 to UTF-16 conversion)
inline int wcscpy_s(wchar_t* destination, size_t destinationSize, const wchar_t* source) {
    if (!destination || destinationSize == 0) return 22;   // EINVAL
    std::wcsncpy(destination, source ? source : L"", destinationSize - 1);
    destination[destinationSize - 1] = L'\0';
    return 0;
}
// (ffx_breadcrumbs_list.h's number formatting: the length written, as MSVC's)
template <typename... Args>
inline int sprintf_s(char* buffer, size_t size, const char* format, Args... args) {
    const int written = std::snprintf(buffer, size, format, args...);
    return written < 0 ? -1 : (static_cast<size_t>(written) < size ? written : static_cast<int>(size) - 1);
}
#endif
#include <FidelityFX/host/ffx_types.h>
#include <FidelityFX/host/ffx_util.h>   // FFX_UNUSED (ffx_message.cpp's non-Windows path uses it without the include)
// The SDK sizes its public contexts for Windows' 2-byte wchar_t: FSR 3's private context holds wide resource names, and
// with a 4-byte wchar_t (Linux, macOS) it is 837,848 bytes against the 524,288 the public one gives (measured with
// GCC 13), which ffx_fsr3upscaler.cpp's static assertion refuses. The public size is raised there, to 1 MiB, before the
// SDK's headers are included (both #pragma once: the SDK's own includes find them done). Windows keeps the SDK's size.
#if WCHAR_MAX > 0xFFFF
#undef FFX_SDK_DEFAULT_CONTEXT_SIZE
#define FFX_SDK_DEFAULT_CONTEXT_SIZE (1024 * 256)
#endif
// sizeof(FfxFsr3UpscalerContext) as gt4_fsr3 was built (platform/render/upscale/fsr3_shaders.cpp), for a caller to
// compare with its own before creating a context. Declared only here, so a file that misses this header can't call it.
std::size_t gt4Fsr3ContextSize();
#endif
