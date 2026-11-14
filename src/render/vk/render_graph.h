#pragma once

// The frame's GPU work as passes over named images (own engine 0.5b, docs/developers/own-engine-host.md "0.5b").
//
// The title's own draws are immediate (they record as they come), so the graph does not reorder: it runs
// the passes added since the last run() in the order they were added. What it does:
// - images by name: imported ones (the back buffer, the swap chain image, the HDR scene) and transient ones
//   the graph owns, kept across frames by name and remade when their size or format changes;
// - before each pass, every image it uses is moved into the layout that use needs (Context::transition);
// - passes whose results nothing reads are culled (a pass lives when it writes an imported image, or an image
//   a later living pass reads), so a feature that is off costs nothing even when its pass is added.
// Frame shape (backend_vulkan.cpp): world (the title's draws until its HUD) -> scene passes (HDR resolve,
// FXAA, sharpen/vibrance) -> HUD (the title's draws) -> present (gamma, overlay, menu) into the swap chain.
// Later lighting passes (ambient occlusion, shadows, bloom) are added between the world and the resolve.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "vk_context.h"

namespace render::vk {

class RenderGraph {
public:
    using Handle = uint32_t;
    enum class Access : uint8_t {
        Sampled,      // read in a shader
        Color,        // drawn into (its contents kept: load + store)
        TransferSrc,  // copied or blitted from
        TransferDst,  // copied or blitted to
    };
    struct Use {
        Handle image;
        Access access;
    };

    explicit RenderGraph(Context& context) : vk_(context) {}
    ~RenderGraph();
    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    // An image the graph does not own, for the passes until the next run().
    Handle import(const char* name, Image& image);
    // An image the graph owns (colour attachment, sampled, transfer src/dst), kept by name across frames; its
    // contents are undefined at the start of a frame. Null image on failure (logged once).
    Handle transient(const char* name, VkFormat format, uint32_t width, uint32_t height);
    Image& image(Handle handle);

    // A pass: `record` runs at run() with the images in their layouts (it may move them further itself through
    // Context::transition, which keeps the layout tracking right).
    void add(const char* name, std::vector<Use> uses, std::function<void()> record);
    // Records the passes added since the last run (culled ones left out) and forgets them and the imports.
    void run();

    // The names of the passes the last run() recorded, in order (diagnostics, tests).
    const std::vector<std::string>& last_run() const { return last_run_; }

private:
    struct Resource {
        std::string name;
        Image* image = nullptr;
        bool imported = false;
    };
    struct Owned {
        std::string name;
        Image image;
    };
    struct Pass {
        std::string name;
        std::vector<Use> uses;
        std::function<void()> record;
    };

    Context& vk_;
    std::vector<Resource> resources_;  // this run's, by Handle
    std::vector<Owned*> owned_;        // kept across runs
    std::vector<Pass> passes_;
    std::vector<std::string> last_run_;
};

}  // namespace render::vk
