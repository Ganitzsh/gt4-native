// The render graph's compile on the CPU (platform/render/graph/render_graph.h, docs/modern/CONTRACTS.md b.3-b.5):
// ordering, culling, version errors, barrier lists, per-mip ranges, transient aliasing, async-compute edges, Opaque
// uses and histories, with a fake backend. No GPU needed.
#include "render_graph.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace {
using namespace gt4::rg;
namespace vk = gt4::vk;

int failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

// Every texture 1 MiB per 1024 x 256 RGBA8 texel block, aligned to 64 KiB; one memory type; async as asked.
class FakeBackend final : public Backend {
public:
    bool async = false;
    MemoryNeeds requirements(const ImageInfo& image) override {
        const VkDeviceSize texels = VkDeviceSize(image.extent.width) * image.extent.height * image.layers;
        VkDeviceSize bytes = 0;
        for (uint32_t mip = 0; mip < image.mips; ++mip) bytes += std::max<VkDeviceSize>(texels >> (2 * mip), 1) * 4;
        return {bytes, 65536, 1};
    }
    MemoryNeeds requirements(VkDeviceSize bytes, VkBufferUsageFlags) override { return {bytes, 256, 1}; }
    uint32_t memoryType(uint32_t) override { return 0; }
    bool asyncCompute() const override { return async; }
};

FrameInfo frameInfo() {
    FrameInfo f;
    f.render = {1024, 256};
    f.output = {2048, 512};
    return f;
}

TextureDesc colour(const char* name, uint32_t mips = 1) {
    TextureDesc d;
    d.format = VK_FORMAT_R8G8B8A8_UNORM;
    d.mips = mips;
    d.name = name;
    return d;
}

vk::Image importable(VkImageLayout layout, uint32_t mips = 1) {
    vk::Image image;
    image.image = reinterpret_cast<VkImage>(uintptr_t(0x1000));   // never handed to Vulkan here
    image.format = VK_FORMAT_R8G8B8A8_UNORM;
    image.extent = {1024, 256};
    image.mips = mips;
    image.layout = layout;
    image.stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    image.access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    return image;
}

const PlanPass* find(const Plan& plan, const std::string& name) {
    for (const PlanPass& p : plan.passes)
        if (p.name == name) return &p;
    return nullptr;
}

const Placement* placementOf(const Plan& plan, uint32_t id) {
    for (const Placement& p : plan.placements)
        if (p.resource == id) return &p;
    return nullptr;
}

bool hasError(const Plan& plan, const std::string& part) {
    for (const std::string& e : plan.errors)
        if (e.find(part) != std::string::npos) return true;
    return false;
}

constexpr VkPipelineStageFlags2 kFrag = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
constexpr VkPipelineStageFlags2 kOut = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
constexpr VkPipelineStageFlags2 kComp = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
auto nothing = [](PassContext&) {};

}  // namespace

int main() {
    FakeBackend backend;

    // Order and culling: passes keep their order; a pass whose output nobody reads is culled; keep() and writing an
    // import keep a pass; a pass feeding a kept one is kept.
    {
        Graph g(backend);
        g.begin(frameInfo());
        vk::Image out = importable(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        Texture target = g.import(out, "output");
        Texture scene;
        g.pass("scene", [&](PassBuilder& b) { scene = b.use(b.create(colour("scene")), Use::ColorWrite, kOut); }, nothing);
        g.pass("unused", [&](PassBuilder& b) { b.use(b.create(colour("unused")), Use::ColorWrite, kOut); }, nothing);
        g.pass("debug overlay", [&](PassBuilder& b) { b.keep(); }, nothing);
        g.pass("tonemap", [&](PassBuilder& b) {
            b.use(scene, Use::Sampled, kFrag);
            target = b.use(target, Use::ColorWrite, kOut);
        }, nothing);
        g.compile();
        const Plan& p = g.plan();
        check(p.ok(), "order: compiles");
        check(p.passes.size() == 3 && p.passes[0].name == "scene" && p.passes[1].name == "debug overlay" && p.passes[2].name == "tonemap",
              "order: scene, debug overlay, tonemap in declaration order");
        check(p.culled.size() == 1 && p.culled[0] == "unused", "culling: the unread pass is culled");
        check(target.version == 1, "versions: a write returns the next version");
    }

    // Version errors.
    {
        Graph g(backend);
        g.begin(frameInfo());
        Texture t0, t1;
        g.pass("a", [&](PassBuilder& b) {
            t0 = b.create(colour("t"));
            t1 = b.use(t0, Use::ColorWrite, kOut);
            b.keep();
        }, nothing);
        g.pass("stale read", [&](PassBuilder& b) { b.use(t0, Use::Sampled, kFrag); b.keep(); }, nothing);
        g.pass("second write", [&](PassBuilder& b) { b.use(t1, Use::ColorWrite, kOut); b.keep(); }, nothing);
        g.pass("same version again", [&](PassBuilder& b) { b.use(t1, Use::ColorWrite, kOut); b.keep(); }, nothing);
        g.pass("read before write", [&](PassBuilder& b) { b.use(b.create(colour("u")), Use::Sampled, kFrag); b.keep(); }, nothing);
        g.pass("blend first", [&](PassBuilder& b) { b.use(b.create(colour("v")), Use::ColorReadWrite, kOut); b.keep(); }, nothing);
        g.pass("opaque transient", [&](PassBuilder& b) { b.use(b.create(colour("w")), Use::Opaque, kOut); b.keep(); }, nothing);
        g.compile();
        const Plan& p = g.plan();
        check(!p.ok(), "errors: the graph does not compile");
        check(hasError(p, "pass 'stale read'") && hasError(p, "stale version 0"), "errors: reading a stale version");
        check(hasError(p, "pass 'same version again'") && hasError(p, "already wrote"), "errors: writing a version another pass wrote");
        check(!hasError(p, "pass 'second write':"), "errors: writing the current version is fine");
        check(hasError(p, "pass 'read before write'"), "errors: reading a transient before any write");
        check(hasError(p, "pass 'blend first'"), "errors: ColorReadWrite as a transient's first use (aliased memory)");
        check(hasError(p, "pass 'opaque transient'"), "errors: Opaque on a transient");
    }

    // Barriers: layout transitions, read after read merged, write after read as an execution dependency only, an
    // import's state taken from its vk::Image.
    {
        Graph g(backend);
        g.begin(frameInfo());
        vk::Image out = importable(VK_IMAGE_LAYOUT_GENERAL);
        Texture target = g.import(out, "output");
        Texture t;
        g.pass("draw", [&](PassBuilder& b) { t = b.use(b.create(colour("t")), Use::ColorWrite, kOut); }, nothing);
        g.pass("read 1", [&](PassBuilder& b) { b.use(t, Use::Sampled, kFrag); b.keep(); }, nothing);
        g.pass("read 2", [&](PassBuilder& b) { b.use(t, Use::Sampled, kComp); b.keep(); }, nothing);
        g.pass("read 3", [&](PassBuilder& b) { b.use(t, Use::Sampled, kFrag); b.keep(); }, nothing);
        g.pass("overwrite", [&](PassBuilder& b) {
            t = b.use(t, Use::StorageWrite, kComp);
            target = b.use(target, Use::ColorReadWrite, kOut);
        }, nothing);
        g.pass("present", [&](PassBuilder& b) { b.use(target, Use::Present, VK_PIPELINE_STAGE_2_NONE); }, nothing);
        g.compile();
        const Plan& p = g.plan();
        check(p.ok(), "barriers: compiles");
        const PlanPass* draw = find(p, "draw");
        check(draw && draw->barriers.size() == 1 && draw->barriers[0].oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
                  draw->barriers[0].newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              "barriers: a transient's first write discards (UNDEFINED -> COLOR_ATTACHMENT)");
        const PlanPass* read1 = find(p, "read 1");
        check(read1 && read1->barriers.size() == 1 && read1->barriers[0].oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
                  read1->barriers[0].newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
                  read1->barriers[0].srcAccess == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT && read1->barriers[0].srcStage == kOut &&
                  read1->barriers[0].dstStage == kFrag,
              "barriers: write then sample: COLOR_ATTACHMENT -> SHADER_READ_ONLY, colour writes made visible");
        const PlanPass* read2 = find(p, "read 2");
        check(read2 && read2->barriers.size() == 1 && read2->barriers[0].oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
                  read2->barriers[0].newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && read2->barriers[0].dstStage == kComp &&
                  (read2->barriers[0].srcStage & kFrag),
              "barriers: a read in a stage the last barrier did not cover waits (chained after the fragment one)");
        const PlanPass* read3 = find(p, "read 3");
        check(read3 && read3->barriers.empty(), "barriers: a read in a stage already covered: none");
        const PlanPass* overwrite = find(p, "overwrite");
        bool storage = false, importBarrier = false;
        if (overwrite)
            for (const Barrier& b : overwrite->barriers) {
                if (b.resource == t.id && b.newLayout == VK_IMAGE_LAYOUT_GENERAL)
                    storage = b.srcAccess == 0 && b.srcStage == (kFrag | kComp) && b.oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                if (b.resource == target.id)
                    importBarrier = b.oldLayout == VK_IMAGE_LAYOUT_GENERAL && b.newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
                                    b.srcStage == VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            }
        check(storage, "barriers: write after two reads waits for both stages, execution dependency only");
        check(importBarrier, "barriers: an import starts from its vk::Image state (GENERAL, fragment), contents kept for ColorReadWrite");
        const PlanPass* present = find(p, "present");
        check(present && present->barriers.size() == 1 && present->barriers[0].newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
              "barriers: Present keeps its pass and moves the image to PRESENT_SRC");
    }

    // Ranges: a bloom chain reads mip n and writes mip n + 1 of the same texture.
    {
        Graph g(backend);
        g.begin(frameInfo());
        vk::Image out = importable(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        Texture target = g.import(out, "output");
        Texture chain;
        g.pass("down 0", [&](PassBuilder& b) { chain = b.use(b.create(colour("bloom", 3)), Use::StorageWrite, kComp, {0, 1}); }, nothing);
        g.pass("down 1", [&](PassBuilder& b) {
            b.use(chain, Use::Sampled, kComp, {0, 1});
            chain = b.use(chain, Use::StorageWrite, kComp, {1, 1});
        }, nothing);
        g.pass("composite", [&](PassBuilder& b) {
            b.use(chain, Use::Sampled, kFrag, {0, 2});
            target = b.use(target, Use::ColorWrite, kOut);
        }, nothing);
        g.compile();
        const Plan& p = g.plan();
        check(p.ok(), "ranges: compiles");
        const PlanPass* d1 = find(p, "down 1");
        bool mip0 = false, mip1 = false;
        if (d1)
            for (const Barrier& b : d1->barriers) {
                if (b.mip == 0 && b.mips == 1) mip0 = b.oldLayout == VK_IMAGE_LAYOUT_GENERAL && b.newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                if (b.mip == 1 && b.mips == 1) mip1 = b.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && b.newLayout == VK_IMAGE_LAYOUT_GENERAL;
            }
        check(mip0 && mip1, "ranges: mip 0 to SHADER_READ, mip 1 from UNDEFINED to GENERAL, in one pass");
        const PlanPass* comp = find(p, "composite");
        bool mip0Waits = false, mip1Moves = false;
        if (comp)
            for (const Barrier& b : comp->barriers) {
                if (b.resource != chain.id) continue;
                if (b.mip == 0) mip0Waits = b.oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && b.newLayout == b.oldLayout && b.dstStage == kFrag;
                if (b.mip == 1) mip1Moves = b.oldLayout == VK_IMAGE_LAYOUT_GENERAL && b.newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
        check(mip0Waits, "ranges: mip 0, readable for compute, waits again for the fragment read");
        check(mip1Moves, "ranges: mip 1 from GENERAL to SHADER_READ");
    }

    // Final barriers: an import whose mips end in different layouts is made whole.
    {
        Graph g(backend);
        g.begin(frameInfo());
        vk::Image mipped = importable(VK_IMAGE_LAYOUT_GENERAL, 2);
        Texture t = g.import(mipped, "mipped");
        g.pass("write mip 0", [&](PassBuilder& b) { b.use(t, Use::ColorWrite, kOut, {0, 1}); }, nothing);
        g.compile();
        const Plan& p = g.plan();
        check(p.ok() && p.finalBarriers.size() == 1 && p.finalBarriers[0].mip == 1 &&
                  p.finalBarriers[0].oldLayout == VK_IMAGE_LAYOUT_GENERAL && p.finalBarriers[0].newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              "final barriers: mip 1 brought to mip 0's layout");
    }

    // Aliasing: lifetimes that do not overlap share memory, overlapping ones do not.
    {
        Graph g(backend);
        g.begin(frameInfo());
        vk::Image out = importable(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        Texture target = g.import(out, "output");
        Texture a, b2, c, d;
        g.pass("a", [&](PassBuilder& b) { a = b.use(b.create(colour("a")), Use::ColorWrite, kOut); }, nothing);
        g.pass("b", [&](PassBuilder& b) {
            b.use(a, Use::Sampled, kFrag);
            b2 = b.use(b.create(colour("b")), Use::ColorWrite, kOut);
        }, nothing);
        g.pass("c", [&](PassBuilder& b) {
            b.use(b2, Use::Sampled, kFrag);
            c = b.use(b.create(colour("c")), Use::ColorWrite, kOut);
        }, nothing);
        g.pass("d", [&](PassBuilder& b) {
            b.use(c, Use::Sampled, kFrag);
            TextureDesc half = colour("d");
            half.scaleX = half.scaleY = 0.5f;
            d = b.use(b.create(half), Use::ColorWrite, kOut);
        }, nothing);
        g.pass("out", [&](PassBuilder& b) {
            b.use(d, Use::Sampled, kFrag);
            target = b.use(target, Use::ColorWrite, kOut);
        }, nothing);
        g.compile();
        const Plan& p = g.plan();
        check(p.ok(), "aliasing: compiles");
        const Placement *pa = placementOf(p, a.id), *pb = placementOf(p, b2.id), *pc = placementOf(p, c.id), *pd = placementOf(p, d.id);
        check(pa && pb && pc && pd, "aliasing: every transient placed");
        if (pa && pb && pc && pd) {
            check(pa->first == 0 && pa->last == 1 && pc->first == 2 && pc->last == 3, "aliasing: lifetimes from first to last use");
            check(pa->offset != pb->offset, "aliasing: a and b overlap (pass b), different memory");
            check(pa->offset == pc->offset, "aliasing: a and c do not overlap, same memory");
            check(pd->size == pa->size / 4, "aliasing: a Render texture at half scale is a quarter of the size");
        }
        check(p.transientBytes == 2 * (1024 * 256 * 4) && p.transientBytesWithoutAliasing == 3 * (1024 * 256 * 4) + 512 * 128 * 4,
              "aliasing: 2 MiB with aliasing, 3.125 MiB without (" + std::to_string(p.transientBytes) + ", " +
                  std::to_string(p.transientBytesWithoutAliasing) + ")");
    }

    // Async compute: edges from the graphics producer to the compute run and from the run to the first graphics
    // pass using its results; the run's resources live across the graphics passes it may overlap.
    {
        FakeBackend asyncBackend;
        asyncBackend.async = true;
        Graph g(asyncBackend);
        g.begin(frameInfo());
        vk::Image out = importable(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        Texture target = g.import(out, "output");
        Texture depth, ao, other;
        g.pass("depth", [&](PassBuilder& b) { depth = b.use(b.create(colour("depth")), Use::ColorWrite, kOut); }, nothing);
        g.pass("ao", [&](PassBuilder& b) {
            b.queue(Queue::Compute);
            b.use(depth, Use::Sampled, kComp);
            ao = b.use(b.create(colour("ao")), Use::StorageWrite, kComp);
        }, nothing);
        g.pass("shadows", [&](PassBuilder& b) { other = b.use(b.create(colour("shadow map")), Use::ColorWrite, kOut); }, nothing);
        g.pass("lighting", [&](PassBuilder& b) {
            b.use(ao, Use::Sampled, kFrag);
            b.use(other, Use::Sampled, kFrag);
            target = b.use(target, Use::ColorWrite, kOut);
        }, nothing);
        g.compile();
        const Plan& p = g.plan();
        check(p.ok(), "async: compiles");
        check(p.passes.size() == 4 && p.passes[1].async && !p.passes[0].async && !p.passes[2].async, "async: the compute pass runs on the compute queue");
        bool in = false, out2 = false;
        for (const AsyncEdge& e : p.edges) {
            if (!e.computeToGraphics && e.after == 0 && e.before == 1) in = true;
            if (e.computeToGraphics && e.after == 1 && e.before == 3) out2 = true;
        }
        check(p.edges.size() == 2 && in && out2, "async: depth -> ao and ao -> lighting, nothing for shadows");
        const Placement *pdepth = placementOf(p, depth.id), *pshadow = placementOf(p, other.id), *pao = placementOf(p, ao.id);
        check(pdepth && pshadow && pao && pdepth->first == 0 && pdepth->last == 3, "async: 'depth', used by passes 0 and 1, lives until 'lighting' waits for the run (3)");
        if (pdepth && pshadow) check(pdepth->offset != pshadow->offset, "async: 'shadows' cannot alias what 'ao' may still read");

        // Without async compute: no edges, the same pass in order on the graphics queue.
        Graph inOrder(backend);
        inOrder.begin(frameInfo());
        Texture t2 = inOrder.import(out, "output");
        Texture x;
        inOrder.pass("x", [&](PassBuilder& b) { b.queue(Queue::Compute); x = b.use(b.create(colour("x")), Use::StorageWrite, kComp); }, nothing);
        inOrder.pass("y", [&](PassBuilder& b) { b.use(x, Use::Sampled, kFrag); t2 = b.use(t2, Use::ColorWrite, kOut); }, nothing);
        inOrder.compile();
        check(inOrder.plan().ok() && inOrder.plan().edges.empty() && !inOrder.plan().passes[0].async, "async: off, in order");
    }

    // Opaque: the GS segment synchronises the image itself; the next use starts from vk::Image's state.
    {
        Graph g(backend);
        g.begin(frameInfo());
        vk::Image gsTarget = importable(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        Texture t = g.import(gsTarget, "GS target");
        g.pass("GS segment", [&](PassBuilder& b) { t = b.use(t, Use::Opaque, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT); }, nothing);
        g.pass("scene stage", [&](PassBuilder& b) { t = b.use(t, Use::StorageReadWrite, kComp); }, nothing);
        g.compile();
        const Plan& p = g.plan();
        check(p.ok(), "opaque: compiles");
        const PlanPass* seg = find(p, "GS segment");
        check(seg && seg->barriers.size() == 1 && seg->barriers[0].oldLayout == seg->barriers[0].newLayout &&
                  seg->barriers[0].dstStage == VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT && !seg->barriers[0].fromImage,
              "opaque: a full barrier from the tracked state, layout kept");
        const PlanPass* stage = find(p, "scene stage");
        check(stage && stage->barriers.size() == 1 && stage->barriers[0].fromImage && stage->barriers[0].newLayout == VK_IMAGE_LAYOUT_GENERAL,
              "opaque: the next use's barrier starts from vk::Image");
    }

    // History: version 0 is the last frame's copy; the first write goes to the other copy and must not read.
    {
        Graph g(backend);
        g.begin(frameInfo());
        Texture h = g.history("taa", colour("taa"));
        g.pass("resolve", [&](PassBuilder& b) {
            b.use(h, Use::Sampled, kComp);
            h = b.use(h, Use::StorageWrite, kComp);
        }, nothing);
        g.compile();
        const Plan& p = g.plan();
        check(p.ok(), "history: read the previous copy and write the new one in one pass");
        const PlanPass* resolve = find(p, "resolve");
        bool copies = false;
        if (resolve && resolve->barriers.size() == 2)
            copies = resolve->barriers[0].copy == 0 && resolve->barriers[1].copy == 1 &&
                     resolve->barriers[1].oldLayout == VK_IMAGE_LAYOUT_UNDEFINED;
        check(copies, "history: the read is on the previous copy, the write discards the other one");
        check(p.placements.empty(), "history: never aliased");

        Graph bad(backend);
        bad.begin(frameInfo());
        Texture h2 = bad.history("bad", colour("bad"));
        bad.pass("blend into history", [&](PassBuilder& b) { b.use(h2, Use::StorageReadWrite, kComp); }, nothing);
        bad.compile();
        check(hasError(bad.plan(), "blend into history") && hasError(bad.plan(), "first write of a history"),
              "history: a first write that reads is an error");
    }

    // Opaque after Opaque keeps the image's layout (known only when it runs); an Opaque image of two mips read whole.
    {
        Graph g(backend);
        g.begin(frameInfo());
        vk::Image gsTarget = importable(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 2);
        Texture t = g.import(gsTarget, "GS target");
        g.pass("segment 1", [&](PassBuilder& b) { t = b.use(t, Use::Opaque, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT); }, nothing);
        g.pass("segment 2", [&](PassBuilder& b) { t = b.use(t, Use::Opaque, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT); }, nothing);
        g.pass("read both mips", [&](PassBuilder& b) { b.use(t, Use::Sampled, kFrag); b.keep(); }, nothing);
        g.compile();
        const Plan& p = g.plan();
        check(p.ok(), "opaque twice: compiles");
        const PlanPass* second = find(p, "segment 2");
        check(second && second->barriers.size() == 1 && second->barriers[0].fromImage && second->barriers[0].newLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
                  second->barriers[0].mips == 2,
              "opaque twice: the second keeps the layout the first left (UNDEFINED = keep), both mips in one barrier");
        const PlanPass* read = find(p, "read both mips");
        check(read && read->barriers.size() == 1 && read->barriers[0].fromImage && read->barriers[0].mip == 0 && read->barriers[0].mips == 2 &&
                  read->barriers[0].newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
              "opaque: a read of both mips after it, one barrier from vk::Image's state");
    }

    // Writes of some mips keep the others: their writers are not culled; a mip read before any write is an error.
    {
        Graph g(backend);
        g.begin(frameInfo());
        vk::Image out = importable(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        Texture target = g.import(out, "output");
        Texture t;
        g.pass("mip 0", [&](PassBuilder& b) { t = b.use(b.create(colour("two mips", 2)), Use::StorageWrite, kComp, {0, 1}); }, nothing);
        g.pass("mip 1", [&](PassBuilder& b) { t = b.use(t, Use::StorageWrite, kComp, {1, 1}); }, nothing);
        g.pass("read all", [&](PassBuilder& b) {
            b.use(t, Use::Sampled, kFrag);
            target = b.use(target, Use::ColorWrite, kOut);
        }, nothing);
        g.compile();
        check(g.plan().ok() && g.plan().culled.empty() && g.plan().passes.size() == 3, "partial writes: the mip 0 writer is kept");

        Graph bad(backend);
        bad.begin(frameInfo());
        Texture out2 = bad.import(out, "output");
        Texture u;
        bad.pass("mip 0 only", [&](PassBuilder& b) { u = b.use(b.create(colour("half written", 2)), Use::StorageWrite, kComp, {0, 1}); }, nothing);
        bad.pass("read mip 1", [&](PassBuilder& b) {
            b.use(u, Use::Sampled, kFrag, {1, 1});
            out2 = b.use(out2, Use::ColorWrite, kOut);
        }, nothing);
        bad.compile();
        check(hasError(bad.plan(), "mip 1 is read before it is written"), "partial writes: reading a mip nobody wrote is an error");
    }

    // Imports: one image imported twice, an UNDEFINED import read, an empty range.
    {
        Graph g(backend);
        g.begin(frameInfo());
        vk::Image a = importable(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        vk::Image blank = importable(VK_IMAGE_LAYOUT_UNDEFINED);
        g.import(a, "first");
        g.import(a, "second");
        Texture b = g.import(blank, "blank");
        g.pass("sample blank", [&](PassBuilder& pb) { pb.use(b, Use::Sampled, kFrag); pb.keep(); }, nothing);
        g.pass("no mips", [&](PassBuilder& pb) { pb.use(b, Use::Sampled, kFrag, {0, 0}); pb.keep(); }, nothing);
        g.compile();
        check(hasError(g.plan(), "'second' imports the image 'first' already imports"), "imports: the same image twice is an error");
        check(hasError(g.plan(), "a range of no mips"), "ranges: no mips is an error");

        Graph g2(backend);
        g2.begin(frameInfo());
        Texture b2 = g2.import(blank, "blank");
        g2.pass("sample blank", [&](PassBuilder& pb) { pb.use(b2, Use::Sampled, kFrag); pb.keep(); }, nothing);
        g2.compile();
        check(hasError(g2.plan(), "is read with no contents"), "imports: reading an UNDEFINED import is an error");
    }

    // A host-visible buffer the GPU writes (indirect arguments) and then reads: a barrier between.
    {
        Graph g(backend);
        g.begin(frameInfo());
        Buffer args;
        g.pass("cull", [&](PassBuilder& b) {
            BufferDesc d;
            d.bytes = 64;
            d.hostVisible = true;
            d.name = "args";
            args = b.use(b.create(d), BufferUse::StorageWrite, kComp);
        }, nothing);
        g.pass("draw", [&](PassBuilder& b) { b.use(args, BufferUse::Indirect, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT); b.keep(); }, nothing);
        g.compile();
        const PlanPass* draw = find(g.plan(), "draw");
        check(g.plan().ok() && draw && draw->barriers.size() == 1 && draw->barriers[0].buffer &&
                  draw->barriers[0].srcAccess == VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT && draw->barriers[0].dstStage == VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
              "buffers: compute writes to a host-visible buffer, then an indirect read: a barrier");
    }

    // Across frames: what an import ends in waits for every use since its last write (here a fragment and a compute
    // read), not only the last barrier's stages.
    {
        Graph g(backend);
        g.begin(frameInfo());
        vk::Image xImage = importable(VK_IMAGE_LAYOUT_GENERAL);
        Texture x = g.import(xImage, "x");
        g.pass("write", [&](PassBuilder& b) { x = b.use(x, Use::StorageWrite, kComp); }, nothing);
        g.pass("read in fragment", [&](PassBuilder& b) { b.use(x, Use::StorageRead, kFrag); b.keep(); }, nothing);
        g.pass("read in compute", [&](PassBuilder& b) { b.use(x, Use::StorageRead, kComp); b.keep(); }, nothing);
        g.compile();
        const Plan& p = g.plan();
        bool ok = false;
        for (const Plan::Export& e : p.exports)
            if (e.resource == x.id) ok = (e.stage & kFrag) && (e.stage & kComp) && (e.access & VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        check(p.ok() && ok, "across frames: the import ends waiting for both reads and the write");
        const PlanPass* last = find(p, "read in compute");
        check(last && last->barriers.size() == 1 && !last->barriers[0].replaces,
              "across frames: a read waiting for the write only adds to vk::Image's state");
    }

    // Two uses of one mip in one pass: one barrier for both (two in one flush would not chain).
    {
        Graph g(backend);
        g.begin(frameInfo());
        Texture t;
        g.pass("draw", [&](PassBuilder& b) { t = b.use(b.create(colour("t")), Use::ColorWrite, kOut); }, nothing);
        g.pass("two reads", [&](PassBuilder& b) {
            b.use(t, Use::Sampled, kComp);
            b.use(t, Use::Sampled, kFrag);
            b.keep();
        }, nothing);
        g.compile();
        const PlanPass* two = find(g.plan(), "two reads");
        check(g.plan().ok() && two && two->barriers.size() == 1 && two->barriers[0].dstStage == (kComp | kFrag) &&
                  two->barriers[0].oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              "same pass: the second use joins the first's barrier");
    }

    // A write joining a read's barrier in the same pass waits for the reads before the pass, not only the last write:
    // write in compute, read in fragment, then a compute read and a compute write of the same mip in one pass.
    {
        Graph g(backend);
        g.begin(frameInfo());
        vk::Image xImage = importable(VK_IMAGE_LAYOUT_GENERAL);
        Texture x = g.import(xImage, "x");
        g.pass("write", [&](PassBuilder& b) { x = b.use(x, Use::StorageWrite, kComp); }, nothing);
        g.pass("read in fragment", [&](PassBuilder& b) { b.use(x, Use::StorageRead, kFrag); b.keep(); }, nothing);
        g.pass("read then write", [&](PassBuilder& b) {
            b.use(x, Use::StorageRead, kComp);
            x = b.use(x, Use::StorageWrite, kComp);
        }, nothing);
        g.compile();
        const PlanPass* p3 = find(g.plan(), "read then write");
        check(g.plan().ok() && p3 && p3->barriers.size() == 1 && (p3->barriers[0].srcStage & kFrag) && (p3->barriers[0].srcStage & kComp) &&
                  p3->barriers[0].replaces,
              "same pass: a write joining a read's barrier waits for the fragment read before it (write after read)");
    }

    // Opaque: mips left in different layouts are made one first; a partial range is an error; after it, the import's
    // mips are made whole again from vk::Image's state.
    {
        Graph g(backend);
        g.begin(frameInfo());
        vk::Image twoMips = importable(VK_IMAGE_LAYOUT_GENERAL, 2);
        Texture t = g.import(twoMips, "two mips");
        g.pass("sample mip 0", [&](PassBuilder& b) { b.use(t, Use::Sampled, kFrag, {0, 1}); b.keep(); }, nothing);
        g.pass("segment", [&](PassBuilder& b) { t = b.use(t, Use::Opaque, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT); }, nothing);
        g.pass("sample mip 0 again", [&](PassBuilder& b) { b.use(t, Use::Sampled, kFrag, {0, 1}); b.keep(); }, nothing);
        g.compile();
        const Plan& p = g.plan();
        const PlanPass* seg = find(p, "segment");
        uint32_t mipsTo = 0;
        bool oneLayout = true;
        if (seg)
            for (const Barrier& b : seg->barriers) {
                mipsTo += b.mips;
                oneLayout = oneLayout && b.newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
        check(p.ok() && mipsTo == 2 && oneLayout, "opaque: both mips brought to the newest one's layout before the segment");
        bool mip1 = false;
        for (const Barrier& b : p.finalBarriers)
            mip1 = mip1 || (b.mip == 1 && b.fromImage && b.newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        check(mip1, "opaque: mip 1, left by the segment, made whole with mip 0 at the end");

        Graph bad(backend);
        bad.begin(frameInfo());
        Texture u = bad.import(twoMips, "two mips");
        bad.pass("partial", [&](PassBuilder& b) { b.use(u, Use::Opaque, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, {0, 1}); }, nothing);
        bad.compile();
        check(hasError(bad.plan(), "must cover every mip"), "opaque: a partial range is an error");
    }

    // Opaque on an import nothing has written yet (UNDEFINED, its first frame): no barrier (none to UNDEFINED is
    // allowed, and there is nothing to wait for); the pass takes it as it is and the next use starts from vk::Image.
    {
        Graph g(backend);
        g.begin(frameInfo());
        vk::Image fresh = importable(VK_IMAGE_LAYOUT_UNDEFINED);
        fresh.stage = VK_PIPELINE_STAGE_2_NONE;
        fresh.access = VK_ACCESS_2_NONE;
        Texture t = g.import(fresh, "scene colour, first frame");
        g.pass("scene", [&](PassBuilder& b) { t = b.use(t, Use::Opaque, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT); }, nothing);
        g.pass("read it", [&](PassBuilder& b) { b.use(t, Use::Sampled, kComp); b.keep(); }, nothing);
        g.compile();
        const Plan& p = g.plan();
        const PlanPass* scene = find(p, "scene");
        const PlanPass* read = find(p, "read it");
        check(p.ok() && scene && scene->barriers.empty(), "opaque: a never-written import gets no barrier before the pass");
        check(read && read->barriers.size() == 1 && read->barriers[0].fromImage, "opaque: the next use starts from what the pass left");
    }

    // Buffers: the access counts, not only the stage; an imported buffer waits for everything before the graph.
    {
        Graph g(backend);
        g.begin(frameInfo());
        vk::Buffer imported;
        imported.buffer = reinterpret_cast<VkBuffer>(uintptr_t(0x2000));
        imported.size = 256;
        Buffer in = g.import(imported, "imported");
        Buffer data;
        g.pass("produce", [&](PassBuilder& b) {
            b.use(in, BufferUse::StorageRead, kComp);
            BufferDesc d;
            d.bytes = 256;
            d.name = "data";
            data = b.use(b.create(d), BufferUse::StorageWrite, kComp);
        }, nothing);
        g.pass("storage read", [&](PassBuilder& b) { b.use(data, BufferUse::StorageRead, kFrag); b.keep(); }, nothing);
        g.pass("uniform read", [&](PassBuilder& b) { b.use(data, BufferUse::Uniform, kFrag); b.keep(); }, nothing);
        g.compile();
        const Plan& p = g.plan();
        const PlanPass* produce = find(p, "produce");
        bool importWaits = false;
        if (produce)
            for (const Barrier& b : produce->barriers)
                importWaits = importWaits || (b.resource == in.id && b.srcStage == VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
        check(p.ok() && importWaits, "buffers: an imported buffer's first use waits for all earlier work");
        const PlanPass* uniform = find(p, "uniform read");
        check(uniform && uniform->barriers.size() == 1 && uniform->barriers[0].dstAccess == VK_ACCESS_2_UNIFORM_READ_BIT,
              "buffers: a uniform read in a stage a storage read made visible still waits (another access)");
    }

    // Aliasing: a transient's first barrier waits for the last use of the one whose memory it takes.
    {
        Graph g(backend);
        g.begin(frameInfo());
        vk::Image out = importable(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        Texture target = g.import(out, "output");
        Texture a, c;
        g.pass("a", [&](PassBuilder& b) { a = b.use(b.create(colour("a")), Use::ColorWrite, kOut); }, nothing);
        g.pass("read a", [&](PassBuilder& b) { b.use(a, Use::Sampled, kComp); b.keep(); }, nothing);
        g.pass("c", [&](PassBuilder& b) { c = b.use(b.create(colour("c")), Use::ColorWrite, kOut); }, nothing);
        g.pass("out", [&](PassBuilder& b) {
            b.use(c, Use::Sampled, kFrag);
            target = b.use(target, Use::ColorWrite, kOut);
        }, nothing);
        g.compile();
        const Plan& p = g.plan();
        const Placement *pa = placementOf(p, a.id), *pc = placementOf(p, c.id);
        const PlanPass* cp = find(p, "c");
        check(p.ok() && pa && pc && pa->offset == pc->offset && cp && cp->barriers.size() == 1 && (cp->barriers[0].srcStage & kComp),
              "aliasing: c, in a's memory, waits for a's last read (compute) before its first write");
    }

    // A history's new copy: a mip read before it is written is an error, as for a transient.
    {
        Graph g(backend);
        g.begin(frameInfo());
        Texture h = g.history("chain", colour("chain", 2));
        g.pass("write mip 0", [&](PassBuilder& b) { h = b.use(h, Use::StorageWrite, kComp, {0, 1}); }, nothing);
        g.pass("read mip 1", [&](PassBuilder& b) { b.use(h, Use::Sampled, kFrag, {1, 1}); b.keep(); }, nothing);
        g.compile();
        check(hasError(g.plan(), "mip 1 is read before it is written"), "history: reading a new copy's unwritten mip is an error");
    }

    // History validity: a compile-only graph has no previous frame.
    {
        Graph g(backend);
        g.begin(frameInfo());
        Texture h = g.history("taa", colour("taa"));
        check(!g.historyValid(h), "history: not valid without a previous frame");
    }

    // Blackboard and dump.
    {
        struct Lights { int count = 0; };
        Blackboard board;
        check(board.get<Lights>() == nullptr, "blackboard: empty");
        board.add<Lights>().count = 3;
        check(board.get<Lights>() && board.get<Lights>()->count == 3, "blackboard: one T per frame");
        board.clear();
        check(board.get<Lights>() == nullptr, "blackboard: cleared");
        Graph g(backend);
        g.begin(frameInfo());
        g.pass("only", [&](PassBuilder& b) { b.keep(); }, nothing);
        g.compile();
        check(g.dump().find("digraph") != std::string::npos && g.dump().find("only") != std::string::npos, "dump: Graphviz");
    }

    if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
    else std::printf("render_graph: all checks passed\n");
    return failures ? 1 : 0;
}
