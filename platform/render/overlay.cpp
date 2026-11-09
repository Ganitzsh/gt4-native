#include "overlay.h"

#include "font8x8.h"
#include "gt4_shaders.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

namespace gt4 {

float Canvas2D::scaleY() const {
    const float height = static_cast<float>(content.extent.height);
    return height * (1.0f - 2.0f * safeArea) / kHeight;
}

float Canvas2D::scaleX() const {
    const float width = static_cast<float>(content.extent.width);
    const float height = static_cast<float>(content.extent.height);
    if (layout == HudLayout::Stretched) return width * (1.0f - 2.0f * safeArea * height / std::max(width, 1.0f)) / kWidth;
    // Square canvas units (640 x 480 is 4:3), limited so a narrower-than-4:3 content still fits.
    return std::min(scaleY(), width / kWidth);
}

float Canvas2D::x(float canvasX, Anchor anchor) const {
    const float left = static_cast<float>(content.offset.x);
    const float width = static_cast<float>(content.extent.width);
    const float inset = safeArea * static_cast<float>(content.extent.height);
    const float sx = scaleX();
    switch (layout) {
    case HudLayout::Stretched: return left + inset + canvasX * sx;
    case HudLayout::Centered: return left + (width - kWidth * sx) * 0.5f + canvasX * sx;
    case HudLayout::Anchored: break;
    }
    switch (anchor) {
    case Anchor::Left: return left + inset + canvasX * sx;
    case Anchor::Right: return left + width - inset - (kWidth - canvasX) * sx;
    case Anchor::Center: break;
    }
    return left + width * 0.5f + (canvasX - kWidth * 0.5f) * sx;
}

float Canvas2D::y(float canvasY) const {
    return static_cast<float>(content.offset.y) + safeArea * static_cast<float>(content.extent.height) + canvasY * scaleY();
}

namespace {
struct OverlayVertex {
    float x, y, u, v;
    float r, g, b, a;
};
// 16 x 6 cells of 10 x 10 holding the 8 x 8 glyphs with a transparent border (so filtered sampling at a
// glyph's edge never picks up its neighbours), then one white cell for solid rectangles.
constexpr uint32_t kCell = 10;
constexpr uint32_t kAtlasWidth = 16 * kCell, kAtlasHeight = 7 * kCell;
constexpr uint32_t kMaxQuads = 32768;
// canvasText's size is the 8 x 8 font's cell; the UI fonts' em at that size keeps capitals about as tall.
constexpr float kCanvasEm = 1.2f;

Color8 color8(ui::PageColor c) { return {c.r, c.g, c.b, c.a}; }

// A page's clip in framebuffer pixels: rounded inward (a pixel only partly inside is clipped away) and clamped to
// the target; an empty rectangle when nothing is left (Vulkan refuses a negative extent).
VkRect2D inwardScissor(const ui::PageRect& clip, VkExtent2D target) {
    const float x0 = std::max(0.0f, std::ceil(clip.x)), y0 = std::max(0.0f, std::ceil(clip.y));
    const float x1 = std::min(static_cast<float>(target.width), std::floor(clip.x + clip.w));
    const float y1 = std::min(static_cast<float>(target.height), std::floor(clip.y + clip.h));
    if (x1 <= x0 || y1 <= y0) return {};
    return {{static_cast<int32_t>(x0), static_cast<int32_t>(y0)},
            {static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1 - y0)}};
}

bool sameRect(const VkRect2D& a, const VkRect2D& b) {
    return a.offset.x == b.offset.x && a.offset.y == b.offset.y && a.extent.width == b.extent.width &&
           a.extent.height == b.extent.height;
}
}

struct Overlay::Impl : ui::PageSink {
    const vk::Device& device;
    VkCommandPool pool;
    vk::Image atlas;
    VkSampler pointSampler = VK_NULL_HANDLE, linearSampler = VK_NULL_HANDLE;
    TextureFilter filter = TextureFilter::Bilinear;
    vk::Pipeline pipeline;
    std::array<vk::Buffer, 2> buffers{};
    std::vector<OverlayVertex> canvasVertices, pixelVertices;
    // The UI fonts (E5): the SDF atlas, its pipeline, the frame's text quads. Canvas text is drawn after the canvas
    // rectangles and before the pixel-space ones (the HUD under a menu), pixel-space text after those.
    ui::SdfFont font;
    vk::Image textAtlas;
    vk::Pipeline textPipeline;
    std::vector<OverlayVertex> canvasTextVertices, pixelTextVertices;
    uint32_t slot = 0;
    VkExtent2D target{};
    bool srgbTarget = false;
    float paperWhite = 0.0f;   // HDR10 output (Overlay::setHdr)
    // The page layer (M3): vertices and runs in call order, each run shapes or text under one scissor.
    struct PageRun {
        bool text = false;
        VkRect2D scissor{};
        uint32_t first = 0, count = 0;
    };
    std::vector<OverlayVertex> pageVertices;
    std::vector<PageRun> pageRuns;
    VkRect2D pageScissor{};   // the current clip; the whole target when none

    Impl(const vk::Device& d, VkCommandPool p) : device(d), pool(p) {
        std::vector<uint8_t> pixels(kAtlasWidth * kAtlasHeight * 4, 0);
        for (int glyph = 0; glyph < 95; ++glyph) {
            const uint32_t gx = static_cast<uint32_t>(glyph % 16) * kCell + 1, gy = static_cast<uint32_t>(glyph / 16) * kCell + 1;
            for (uint32_t row = 0; row < 8; ++row)
                for (uint32_t column = 0; column < 8; ++column)
                    if (kFont8x8[glyph][row] & (1u << column)) {
                        uint8_t* texel = &pixels[((gy + row) * kAtlasWidth + gx + column) * 4];
                        texel[0] = texel[1] = texel[2] = texel[3] = 255;
                    }
        }
        for (uint32_t y = 6 * kCell; y < kAtlasHeight; ++y)
            for (uint32_t x = 0; x < kCell; ++x) {
                uint8_t* texel = &pixels[(y * kAtlasWidth + x) * 4];
                texel[0] = texel[1] = texel[2] = texel[3] = 255;
            }
        vk::ImageDesc desc;
        desc.format = VK_FORMAT_R8G8B8A8_UNORM;
        desc.extent = {kAtlasWidth, kAtlasHeight};
        desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        desc.name = "overlay atlas";
        atlas = vk::createImage(device, desc);
        {
            vk::Uploader upload(device, pool);
            vk::Barriers barriers;
            barriers.image(atlas, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_WRITE_BIT, true);
            barriers.flush(upload.command());
            upload.copyToImage(atlas, pixels.data(), pixels.size(), 0, 0, {kAtlasWidth, kAtlasHeight});
            vk::toSampled(barriers, atlas, vk::kFragment);
            barriers.flush(upload.command());
            upload.submit();
        }
        vk::ImageDesc text;
        text.format = VK_FORMAT_R8_UNORM;
        text.extent = {ui::SdfFont::kAtlasSize, ui::SdfFont::kAtlasSize};
        text.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        text.name = "overlay text atlas";
        textAtlas = vk::createImage(device, text);
        uploadText();
        const auto& stats = font.stats();
        std::printf("UI text: %d glyphs in %.1f ms, atlas %zu KB\n", stats.glyphs, stats.buildMs,
                    stats.atlasBytes / 1024);
        pointSampler = vk::createSampler(device, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        linearSampler = vk::createSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        for (auto& buffer : buffers)
            buffer = vk::createBuffer(device, sizeof(OverlayVertex) * 6 * kMaxQuads, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
    }

    ~Impl() {
        vk::destroy(device, pipeline);
        vk::destroy(device, textPipeline);
        vk::destroy(device, atlas);
        vk::destroy(device, textAtlas);
        for (auto& buffer : buffers) vk::destroy(device, buffer);
        if (pointSampler) vkDestroySampler(device.device, pointSampler, nullptr);
        if (linearSampler) vkDestroySampler(device.device, linearSampler, nullptr);
    }

    // The whole text atlas page, as the font has it now.
    void uploadText() {
        font.takeDirty();
        vk::Uploader upload(device, pool);
        vk::Barriers barriers;
        barriers.image(textAtlas, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                       true);
        barriers.flush(upload.command());
        upload.copyToImage(textAtlas, font.atlas().data(), font.atlas().size(), 0, 0,
                           {ui::SdfFont::kAtlasSize, ui::SdfFont::kAtlasSize});
        vk::toSampled(barriers, textAtlas, vk::kFragment);
        barriers.flush(upload.command());
        upload.submit();
    }

    // Laid-out texts (M9a): a text's glyph quads at the origin (its line's top-left), per face and em, offset when
    // drawn, so a page or readout drawn again each frame is not laid out again. Glyphs keep their atlas place once
    // added, so an entry stays valid; entries unused for kTextKeepFrames go once the cache is over kTextCacheSize.
    struct LaidOut {
        std::vector<ui::GlyphQuad> quads;
        float width = 0.0f;
        uint32_t used = 0;
    };
    static constexpr size_t kTextCacheSize = 1024;
    static constexpr uint32_t kTextKeepFrames = 120;
    std::unordered_map<std::string, LaidOut> laidOut;
    uint32_t textFrame = 0;

    const LaidOut& layoutText(float em, ui::FontFace face, std::string_view string) {
        std::string key(sizeof(float) + 1, '\0');
        key[0] = static_cast<char>(face);
        std::memcpy(&key[1], &em, sizeof(float));
        key.append(string);
        auto [it, added] = laidOut.try_emplace(std::move(key));
        if (added) it->second.width = font.layout(face, em, string, 0.0f, font.ascent(face, em), it->second.quads);
        it->second.used = textFrame;
        return it->second;
    }

    void forgetOldTexts() {
        ++textFrame;
        if (laidOut.size() <= kTextCacheSize) return;
        for (auto it = laidOut.begin(); it != laidOut.end();)
            it = textFrame - it->second.used > kTextKeepFrames ? laidOut.erase(it) : std::next(it);
    }

    // UI-font text at (x, top) in output pixels, `em` pixels: quads into `out`; returns the width.
    float text(std::vector<OverlayVertex>& out, float x, float top, float em, ui::FontFace face,
               std::string_view string, Color8 c) {
        const LaidOut& text = layoutText(em, face, string);
        for (const auto& g : text.quads)
            quad(out, x + g.x0, top + g.y0, g.x1 - g.x0, g.y1 - g.y0, g.u0, g.v0, g.u1, g.v1, c);
        return text.width;
    }

    static void quad(std::vector<OverlayVertex>& out, float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color8 c) {
        quad(out, x, y, w, h, u0, v0, u1, v1, c, c);
    }

    // `top` along the top edge, `bottom` along the bottom: interpolated per vertex (a page's gradient).
    static void quad(std::vector<OverlayVertex>& out, float x, float y, float w, float h, float u0, float v0, float u1,
                     float v1, Color8 top, Color8 bottom) {
        const float r = static_cast<float>(top.r) / 255.0f, g = static_cast<float>(top.g) / 255.0f;
        const float b = static_cast<float>(top.b) / 255.0f, a = static_cast<float>(top.a) / 255.0f;
        const float r2 = static_cast<float>(bottom.r) / 255.0f, g2 = static_cast<float>(bottom.g) / 255.0f;
        const float b2 = static_cast<float>(bottom.b) / 255.0f, a2 = static_cast<float>(bottom.a) / 255.0f;
        const OverlayVertex v00{x, y, u0, v0, r, g, b, a}, v10{x + w, y, u1, v0, r, g, b, a};
        const OverlayVertex v01{x, y + h, u0, v1, r2, g2, b2, a2}, v11{x + w, y + h, u1, v1, r2, g2, b2, a2};
        out.insert(out.end(), {v00, v10, v11, v00, v11, v01});
    }

    static void solid(std::vector<OverlayVertex>& out, float x, float y, float w, float h, Color8 c) {
        const float u = 5.0f / kAtlasWidth, v = (6.0f * kCell + 5.0f) / kAtlasHeight;
        quad(out, x, y, w, h, u, v, u, v, c);
    }

    // The page layer's sink (ui::PageSink). Vertices go in only when the current clip leaves something; consecutive
    // ones of one kind under one scissor extend the last run.
    void pageRun(bool isText, size_t start) {
        const auto first = static_cast<uint32_t>(start), count = static_cast<uint32_t>(pageVertices.size() - start);
        if (!count) return;
        if (!pageRuns.empty()) {
            PageRun& last = pageRuns.back();
            if (last.text == isText && sameRect(last.scissor, pageScissor) && last.first + last.count == first) {
                last.count += count;
                return;
            }
        }
        pageRuns.push_back({isText, pageScissor, first, count});
    }
    bool pageVisible() const { return pageScissor.extent.width && pageScissor.extent.height; }

    void rect(const ui::PageRect& r, ui::PageColor top, ui::PageColor bottom) override {
        if (!pageVisible()) return;
        const size_t start = pageVertices.size();
        const float u = 5.0f / kAtlasWidth, v = (6.0f * kCell + 5.0f) / kAtlasHeight;
        quad(pageVertices, r.x, r.y, r.w, r.h, u, v, u, v, color8(top), color8(bottom));
        pageRun(false, start);
    }
    float textWidth(ui::FontFace face, float em, std::string_view string) override {
        return font.measure(face, em, string);   // (no glyph cache entry: fitText measures many prefixes)
    }
    float lineHeight(ui::FontFace face, float em) override { return font.lineHeight(face, em); }
    void text(float x, float top, float em, ui::FontFace face, std::string_view string, ui::PageColor c) override {
        if (!pageVisible()) return;
        const size_t start = pageVertices.size();
        text(pageVertices, x, top, em, face, string, color8(c));
        pageRun(true, start);
    }
    void clip(const ui::PageRect* clip) override {
        pageScissor = clip ? inwardScissor(*clip, target) : VkRect2D{{0, 0}, target};
    }

    static void glyph(std::vector<OverlayVertex>& out, float x, float y, float size, char character, Color8 c) {
        const int index = (character >= 32 && character < 127) ? character - 32 : '?' - 32;
        const float u0 = static_cast<float>((index % 16) * kCell + 1) / kAtlasWidth;
        const float v0 = static_cast<float>((index / 16) * kCell + 1) / kAtlasHeight;
        quad(out, x, y, size, size, u0, v0, u0 + 8.0f / kAtlasWidth, v0 + 8.0f / kAtlasHeight, c);
    }
};

Overlay::Overlay(const vk::Device& device, VkCommandPool pool) : impl(std::make_unique<Impl>(device, pool)) {}
Overlay::~Overlay() = default;

void Overlay::setHdr(float paperWhite) { impl->paperWhite = paperWhite; }

void Overlay::prepare(VkFormat swapchainFormat, bool srgbTarget) {
    vk::destroy(impl->device, impl->pipeline);
    vk::GraphicsDesc desc;
    desc.vertex = shaders::overlay_vert();
    desc.fragment = shaders::overlay_frag();
    desc.colors = {swapchainFormat};
    desc.blend = vk::Blend::Alpha;
    desc.vertices.stride = sizeof(OverlayVertex);
    desc.vertices.attributes = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32_SFLOAT, 8},
                                {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16}};
    desc.types = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
    desc.pushSize = 16;
    desc.name = "overlay";
    impl->pipeline = vk::createGraphics(impl->device, desc);
    vk::destroy(impl->device, impl->textPipeline);
    desc.fragment = shaders::overlay_sdf_frag();
    desc.name = "overlay text";
    impl->textPipeline = vk::createGraphics(impl->device, desc);
    impl->srgbTarget = srgbTarget;
}

void Overlay::setFilter(TextureFilter filter) { impl->filter = filter; }

void Overlay::begin(uint32_t slot, VkExtent2D target, const Canvas2D& canvas) {
    impl->slot = slot;
    impl->target = target;
    impl->canvasVertices.clear();
    impl->pixelVertices.clear();
    impl->canvasTextVertices.clear();
    impl->pixelTextVertices.clear();
    impl->pageVertices.clear();
    impl->pageRuns.clear();
    impl->pageScissor = {{0, 0}, target};
    impl->forgetOldTexts();
    currentCanvas = canvas;
}

ui::PageSink& Overlay::page() { return *impl; }

void Overlay::canvasRect(float x, float y, float w, float h, Color8 color, Anchor anchor) {
    const float sx = currentCanvas.scaleX(), sy = currentCanvas.scaleY();
    Impl::solid(impl->canvasVertices, currentCanvas.x(x, anchor), currentCanvas.y(y), w * sx, h * sy, color);
}

void Overlay::canvasText(float x, float y, float size, const std::string& text, Color8 color, Anchor anchor,
                         ui::FontFace face) {
    const float sx = currentCanvas.scaleX(), sy = currentCanvas.scaleY();
    // Text is positioned as a block: the anchor applies to the block, glyphs advance inside it.
    const float em = size * kCanvasEm * std::min(sx, sy);
    const float width = impl->font.measure(face, em, text) / sx;
    float start = x;
    if (anchor == Anchor::Center) start = x - width * 0.5f;
    else if (anchor == Anchor::Right) start = x - width;
    impl->text(impl->canvasTextVertices, currentCanvas.x(start, anchor), currentCanvas.y(y), em, face, text, color);
}

void Overlay::canvasSprite(float x, float y, float size, char glyph, Color8 color, Anchor anchor) {
    const float s = size * std::min(currentCanvas.scaleX(), currentCanvas.scaleY());
    Impl::glyph(impl->canvasVertices, currentCanvas.x(x, anchor), currentCanvas.y(y), s, glyph, color);
}

void Overlay::pixelRect(float x, float y, float w, float h, Color8 color) { Impl::solid(impl->pixelVertices, x, y, w, h, color); }

void Overlay::pixelText(float x, float y, float scale, const std::string& text, Color8 color) {
    float cursorX = x, cursorY = y;
    for (char c : text) {
        if (c == '\n') { cursorX = x; cursorY += 10.0f * scale; continue; }
        Impl::glyph(impl->pixelVertices, cursorX, cursorY, 8.0f * scale, c, color);
        cursorX += 8.0f * scale;
    }
}

float Overlay::uiText(float x, float top, float em, ui::FontFace face, std::string_view text, Color8 color) {
    return impl->text(impl->pixelTextVertices, x, top, em, face, text, color);
}

float Overlay::uiTextWidth(ui::FontFace face, float em, std::string_view text) {
    return impl->font.measure(face, em, text);
}

float Overlay::uiLineHeight(ui::FontFace face, float em) const { return impl->font.lineHeight(face, em); }

void Overlay::uploadGlyphs() {
    if (!impl->font.takeDirty()) return;
    // Frames in flight may still sample the atlas: rare (a character not drawn before), so wait for the device.
    vkDeviceWaitIdle(impl->device.device);
    impl->uploadText();
}

const ui::SdfFontStats& Overlay::fontStats() const { return impl->font.stats(); }

void Overlay::record(VkCommandBuffer command) {
    auto& buffer = impl->buffers[impl->slot];
    // Four batches in layer order: canvas shapes, canvas text, pixel-space shapes, pixel-space text; then the page
    // layer (its runs, in order).
    const std::vector<OverlayVertex>* batches[5] = {&impl->canvasVertices, &impl->canvasTextVertices,
                                                    &impl->pixelVertices, &impl->pixelTextVertices,
                                                    &impl->pageVertices};
    size_t counts[5] = {}, total = 0;
    const size_t maxVertices = kMaxQuads * 6;
    for (int i = 0; i < 5; ++i) {
        counts[i] = std::min(batches[i]->size(), maxVertices - total);
        total += counts[i];
    }
    if (total == 0) return;
    auto* out = static_cast<OverlayVertex*>(buffer.mapped);
    size_t first[5] = {};
    for (int i = 0, at = 0; i < 5; at += static_cast<int>(counts[i]), ++i) {
        first[i] = static_cast<size_t>(at);
        std::copy_n(batches[i]->data(), counts[i], out + at);
    }
    VkViewport viewport{0, 0, static_cast<float>(impl->target.width), static_cast<float>(impl->target.height), 0, 1};
    VkRect2D scissor{{0, 0}, impl->target};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    struct { float width, height, srgb, paperWhite; } push{static_cast<float>(impl->target.width),
        static_cast<float>(impl->target.height), impl->srgbTarget ? 1.0f : 0.0f, impl->paperWhite};
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &buffer.buffer, &offset);
    for (int i = 0; i < 4; ++i) {
        if (!counts[i]) continue;
        const bool text = i == 1 || i == 3;
        const vk::Pipeline& pipeline = text ? impl->textPipeline : impl->pipeline;
        vk::bind(command, pipeline);
        vk::pushConstants(command, pipeline, push);
        // Canvas shapes follow the UI filter, pixel-space ones (the 8 x 8 font) are point-sampled; the distance fields
        // are always linearly filtered.
        const bool linear = text || (i == 0 && impl->filter != TextureFilter::Point);
        VkSampler sampler = linear ? impl->linearSampler : impl->pointSampler;
        vk::Descriptors().sampled(0, text ? impl->textAtlas.view : impl->atlas.view, sampler).push(command, pipeline);
        vkCmdDraw(command, static_cast<uint32_t>(counts[i]), 1, static_cast<uint32_t>(first[i]), 0);
    }
    // The page layer: each run with its scissor, shapes as the canvas shapes are drawn (linear, the white cell), text
    // through the SDF pipeline; what the vertex budget cut off is not drawn.
    const auto pageEnd = static_cast<uint32_t>(counts[4]);
    int bound = -1;   // 0 shapes, 1 text: rebound only when the kind changes
    for (const auto& run : impl->pageRuns) {
        if (run.first >= pageEnd || !run.scissor.extent.width || !run.scissor.extent.height) continue;
        const uint32_t count = std::min(run.count, pageEnd - run.first);
        const vk::Pipeline& pipeline = run.text ? impl->textPipeline : impl->pipeline;
        if (bound != (run.text ? 1 : 0)) {
            bound = run.text ? 1 : 0;
            vk::bind(command, pipeline);
            vk::pushConstants(command, pipeline, push);
            vk::Descriptors().sampled(0, run.text ? impl->textAtlas.view : impl->atlas.view, impl->linearSampler)
                .push(command, pipeline);
        }
        vkCmdSetScissor(command, 0, 1, &run.scissor);
        vkCmdDraw(command, count, 1, static_cast<uint32_t>(first[4]) + run.first, 0);
    }
    if (bound >= 0) vkCmdSetScissor(command, 0, 1, &scissor);   // the whole target again for what draws next
}

}  // namespace gt4
