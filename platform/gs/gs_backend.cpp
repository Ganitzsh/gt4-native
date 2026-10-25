// The GS back end over paraLLEl-GS (see gs_backend.h). This is the only file that includes Granite and
// paraLLEl-GS headers.
#include "gs_backend.h"

#include "context.hpp"
#include "device.hpp"
#include "gs_interface.hpp"
#include "thread_id.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace gt4::gs {
namespace {

std::string lowerCase(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

ParallelGS::SuperSampling superSampling(uint32_t rate) {
    switch (rate) {
    case 2: return ParallelGS::SuperSampling::X2;
    case 4: return ParallelGS::SuperSampling::X4;
    case 8: return ParallelGS::SuperSampling::X8;
    case 16: return ParallelGS::SuperSampling::X16;
    default: return ParallelGS::SuperSampling::X1;
    }
}

// NTSC SMODE1 as the BIOS's SetGsCrt(interlace, NTSC, frame) leaves it: CMOD = 2 (NTSC), LC = 32
// (analog), SPML = 4, PRST/SINT set. The game never writes SMODE1 itself (the kernel does).
constexpr uint64_t kNtscSmode1 = 0x0000000740834504ull;

struct Signals final : ParallelGS::SignalInterface {
    Backend::EventSink sink;
    bool on_signal(uint64_t payload) override { if (sink) sink(GsEventKind::Signal, payload); return false; }
    bool on_finish(uint64_t payload) override { if (sink) sink(GsEventKind::Finish, payload); return false; }
    bool on_label(uint64_t payload) override { if (sink) sink(GsEventKind::Label, payload); return false; }
};

}  // namespace

struct Backend::Impl {
    Vulkan::Context context;
    Vulkan::Device device;
    ParallelGS::GSInterface gs;
    Signals signals;
    std::vector<uint64_t> aligned;   // GIF data copied to 16-byte alignment
    std::string name;
    uint32_t superSampling = 1;
    bool highResolutionScanout = false;
    bool timestamps = false;
    Vulkan::BufferHandle readback;
};

Backend::Backend() : impl(std::make_unique<Impl>()) {}
Backend::~Backend() {
    if (impl) impl->device.wait_idle();
}

std::unique_ptr<Backend> Backend::create(const BackendOptions& options, std::string& error) {
    if (!Vulkan::Context::init_loader(nullptr)) {
        error = "the Vulkan loader is not installed";
        return nullptr;
    }
    Util::register_thread_index(0);
    std::unique_ptr<Backend> backend(new Backend());
    Impl& b = *backend->impl;
    b.context.set_num_thread_indices(1);
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "GT4 native host (GS)";
    app.pEngineName = "paraLLEl-GS";
    app.apiVersion = VK_API_VERSION_1_3;
    b.context.set_application_info(&app);
    if (!b.context.init_instance(nullptr, 0)) {
        error = "vkCreateInstance failed for the GS device";
        return nullptr;
    }
    // Choose the physical device like display.gpu: an index, part of a name, or the first discrete GPU.
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(b.context.get_instance(), &count, nullptr);
    std::vector<VkPhysicalDevice> gpus(count);
    if (count) vkEnumeratePhysicalDevices(b.context.get_instance(), &count, gpus.data());
    if (gpus.empty()) {
        error = "no Vulkan device";
        return nullptr;
    }
    const std::string want = lowerCase(options.gpu);
    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    int bestScore = -1;
    for (uint32_t i = 0; i < count; ++i) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(gpus[i], &p);
        if (!want.empty() && want != "auto") {
            char* end = nullptr;
            const unsigned long index = std::strtoul(want.c_str(), &end, 10);
            const bool match = (end && *end == '\0') ? index == i : lowerCase(p.deviceName).find(want) != std::string::npos;
            if (match) { chosen = gpus[i]; break; }
            continue;
        }
        const int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 :
                          p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
        if (score > bestScore) { bestScore = score; chosen = gpus[i]; }
    }
    if (!chosen) {
        error = "no Vulkan device matches '" + options.gpu + "'";
        return nullptr;
    }
    if (!b.context.init_device(chosen, VK_NULL_HANDLE, nullptr, 0, Vulkan::CONTEXT_CREATION_ENABLE_PUSH_DESCRIPTOR_BIT)) {
        error = "vkCreateDevice failed for the GS device";
        return nullptr;
    }
    b.device.set_context(b.context);
    // paraLLEl-GS cycles through many memory contexts per frame (its own replayer uses 12).
    b.device.init_frame_contexts(12);
    ParallelGS::GSOptions gsOptions{};
    gsOptions.super_sampling = superSampling(options.superSampling);
    gsOptions.dynamic_super_sampling = true;   // the rate can change at run time (settings)
    if (!b.gs.init(&b.device, gsOptions)) {
        error = "the GPU does not meet paraLLEl-GS's requirements (descriptor indexing, timeline semaphores, "
                "8/16-bit storage, subgroup size control)";
        return nullptr;
    }
    VkPhysicalDeviceProperties p{};
    vkGetPhysicalDeviceProperties(chosen, &p);
    b.name = p.deviceName;
    b.superSampling = options.superSampling;
    b.highResolutionScanout = options.highResolutionScanout;
    b.timestamps = options.timestamps;
    if (options.timestamps) {
        ParallelGS::DebugMode debug{};
        debug.timestamps = true;
        b.gs.set_debug_mode(debug);
    }
    b.gs.set_signal_interface(&b.signals);
    auto& priv = b.gs.get_priv_register_state();
    priv.qwords_lo[0x10 / 8] = kNtscSmode1;
    return backend;
}

void Backend::gif(uint32_t path, const void* data, size_t bytes) {
    if (!bytes) return;
    bytes &= ~size_t(15);
    if (reinterpret_cast<uintptr_t>(data) & 15) {
        impl->aligned.resize(bytes / 8);
        std::memcpy(impl->aligned.data(), data, bytes);
        data = impl->aligned.data();
    }
    // paraLLEl-GS keeps one GIF path state per index (0..3); PCSX2's integration passes 3 for PATH3.
    impl->gs.gif_transfer(path >= 1 && path <= 3 ? path : 3, data, bytes);
}

void Backend::writePrivileged(uint32_t offset, uint64_t value) {
    auto& priv = impl->gs.get_priv_register_state();
    if (offset < 0x1000) {
        if (offset > 0xE0 || (offset & 0xF)) return;
        // SMODE1, SRFSH, SYNCH1/2 and SYNCV are set by the kernel's SetGsCrt, which the game does not do
        // through this stream: keep the NTSC values unless a real value arrives.
        if ((offset >= 0x10 && offset <= 0x60 && offset != 0x20) && value == 0) return;
        priv.qwords_lo[offset / 8] = value;
        return;
    }
    const uint32_t local = offset - 0x1000;
    if (local >= 0x1000 || (local & 0xF)) return;
    if (local == 0) {
        // CSR: SIGNAL/FINISH/HSINT/VSINT/EDWINT are write-1-to-clear; FIELD and the IDs are read-only.
        uint64_t& csr = priv.qwords_hi[0];
        csr &= ~(value & 0x1Full);
        return;
    }
    priv.qwords_hi[local / 8] = value;
}

uint64_t Backend::readPrivileged(uint32_t offset) const {
    const auto& priv = impl->gs.get_priv_register_state();
    if (offset < 0x1000) return (offset & 7) || offset / 8 >= 0x200 ? 0 : priv.qwords_lo[offset / 8];
    const uint32_t local = offset - 0x1000;
    return (local & 7) || local / 8 >= 0x200 ? 0 : priv.qwords_hi[local / 8];
}

bool Backend::vsync(uint32_t field, uint64_t vblank, DisplayFrame& frame) {
    Impl& b = *impl;
    auto& priv = b.gs.get_priv_register_state();
    // CSR.FIELD (bit 13) follows the field being displayed.
    priv.qwords_hi[0] = (priv.qwords_hi[0] & ~(1ull << 13)) | (uint64_t(field & 1) << 13);
    ParallelGS::VSyncInfo info{};
    info.phase = field & 1;
    info.dst_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    info.dst_stage = VK_PIPELINE_STAGE_2_COPY_BIT;
    info.dst_access = VK_ACCESS_2_TRANSFER_READ_BIT;
    // Frame-mode interlace (FFMD = 1) draws whole frames: scan the full frame out progressively, as a
    // deinterlacer with perfect weave would. Field mode still alternates lines (handled by paraLLEl-GS).
    info.force_progressive = true;
    info.anti_blur = true;
    info.overscan = false;
    info.crtc_offsets = false;
    info.adapt_to_internal_horizontal_resolution = true;
    info.high_resolution_scanout = b.highResolutionScanout && b.superSampling >= 4;
    b.gs.flush();
    ParallelGS::ScanoutResult result = b.gs.vsync(info);
    // The scanout is recorded into paraLLEl-GS's own command buffer: submit it before the copy below.
    b.gs.flush();
    frame.field = field;
    frame.vblank = vblank;
    if (!result.image) {
        frame.width = frame.height = 0;
        frame.rgba.clear();
        return false;
    }
    const uint32_t width = result.image->get_width(), height = result.image->get_height();
    frame.interlaced = result.interlaced;
    frame.modeWidth = result.mode_width;
    frame.modeHeight = result.mode_height;
    const VkDeviceSize bytes = VkDeviceSize(width) * height * 4;
    if (!b.readback || b.readback->get_create_info().size < bytes) {
        Vulkan::BufferCreateInfo create{};
        create.size = bytes;
        create.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        create.domain = Vulkan::BufferDomain::CachedHost;
        b.readback = b.device.create_buffer(create);
    }
    auto cmd = b.device.request_command_buffer();
    cmd->copy_image_to_buffer(*b.readback, *result.image, 0, {0, 0, 0}, {width, height, 1}, 0, 0,
                              {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
    cmd->barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                 VK_ACCESS_2_HOST_READ_BIT);
    Vulkan::Fence fence;
    b.device.submit(cmd, &fence);
    fence->wait();
    const auto* pixels = static_cast<const uint32_t*>(b.device.map_host_buffer(*b.readback, Vulkan::MEMORY_ACCESS_READ_BIT));
    frame.width = width;
    frame.height = height;
    frame.rgba.resize(size_t(width) * height);
    if (result.image->get_format() == VK_FORMAT_B8G8R8A8_UNORM) {
        for (size_t i = 0; i < frame.rgba.size(); ++i) {
            const uint32_t v = pixels[i];
            frame.rgba[i] = ((v >> 16) & 0xFF) | (v & 0xFF00) | ((v & 0xFF) << 16) | 0xFF000000u;
        }
    } else {
        for (size_t i = 0; i < frame.rgba.size(); ++i) frame.rgba[i] = pixels[i] | 0xFF000000u;
    }
    b.device.unmap_host_buffer(*b.readback, Vulkan::MEMORY_ACCESS_READ_BIT);
    return true;
}

void Backend::readTransfer(void* out, uint32_t qwc) { impl->gs.read_transfer_fifo(out, qwc); }

void Backend::writeVram(uint32_t offset, const void* data, size_t bytes) {
    void* mapped = impl->gs.map_vram_write(offset, bytes);
    if (!mapped) return;
    std::memcpy(mapped, data, bytes);
    impl->gs.end_vram_write(offset, bytes);
}

void Backend::readVram(uint32_t offset, void* data, size_t bytes) {
    impl->gs.flush();
    const void* mapped = impl->gs.map_vram_read(offset, bytes);
    if (mapped) std::memcpy(data, mapped, bytes);
}

void Backend::setEventSink(EventSink sink) { impl->signals.sink = std::move(sink); }

void Backend::bindToCurrentThread() { Util::register_thread_index(0); }

void Backend::setSuperSampling(uint32_t rate, bool highResolutionScanout) {
    impl->superSampling = rate;
    impl->highResolutionScanout = highResolutionScanout;
    impl->gs.set_super_sampling_rate(superSampling(rate), true, highResolutionScanout && rate >= 4);
}

std::string Backend::deviceName() const { return impl->name; }

std::string Backend::statistics() const {
    if (!impl->timestamps) return {};
    static const char* names[] = {"sync host->vram", "copy vram", "palette", "texture upload", "triangle setup",
                                  "binning", "shading", "readback", "vsync"};
    std::ostringstream out;
    for (int i = 0; i < int(ParallelGS::TimestampType::Count); ++i)
        out << (i ? ", " : "") << names[i] << " "
            << 1e3 * impl->gs.get_accumulated_timestamps(ParallelGS::TimestampType(i)) << " ms";
    return out.str();
}

}  // namespace gt4::gs
