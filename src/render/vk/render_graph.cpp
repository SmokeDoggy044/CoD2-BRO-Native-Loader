#include "render_graph.h"

#include "log.h"

namespace render::vk {
namespace {

bool writes(RenderGraph::Access access) {
    return access == RenderGraph::Access::Color || access == RenderGraph::Access::TransferDst;
}

VkImageLayout layout_for(RenderGraph::Access access) {
    switch (access) {
    case RenderGraph::Access::Sampled: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case RenderGraph::Access::Color: return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case RenderGraph::Access::TransferSrc: return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case RenderGraph::Access::TransferDst: return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }
    return VK_IMAGE_LAYOUT_GENERAL;
}

}  // namespace

RenderGraph::~RenderGraph() {
    for (Owned* owned : owned_) {
        if (vk_.device()) vk_.destroy_image(owned->image);
        delete owned;
    }
}

RenderGraph::Handle RenderGraph::import(const char* name, Image& image) {
    resources_.push_back({name, &image, true});
    return static_cast<Handle>(resources_.size() - 1);
}

RenderGraph::Handle RenderGraph::transient(const char* name, VkFormat format, uint32_t width, uint32_t height) {
    Owned* found = nullptr;
    for (Owned* owned : owned_)
        if (owned->name == name) found = owned;
    if (!found) {
        found = new Owned{name, {}};
        owned_.push_back(found);
    }
    Image& image = found->image;
    if (image && (image.format != format || image.width != width || image.height != height)) vk_.destroy_image(image);
    if (!image && !vk_.create_image(image, format, width, height,
                                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        LOG_WARN("render", "Vulkan: render graph image %s (%ux%u) cannot be made", name, width, height);
    // Its old contents are not needed.
    image.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    resources_.push_back({name, &image, false});
    return static_cast<Handle>(resources_.size() - 1);
}

Image& RenderGraph::image(Handle handle) { return *resources_[handle].image; }

void RenderGraph::add(const char* name, std::vector<Use> uses, std::function<void()> record) {
    passes_.push_back({name, std::move(uses), std::move(record)});
}

void RenderGraph::run() {
    // Culling, last pass first: a pass lives when it writes an imported image or one a later living pass reads.
    std::vector<bool> needed(resources_.size(), false), live(passes_.size(), false);
    for (size_t i = 0; i < resources_.size(); ++i) needed[i] = resources_[i].imported;
    for (size_t p = passes_.size(); p-- > 0;) {
        for (const Use& use : passes_[p].uses)
            if (writes(use.access) && needed[use.image]) live[p] = true;
        if (!live[p]) continue;
        // What it reads (and what it draws over: Color keeps the old contents) is needed before it.
        for (const Use& use : passes_[p].uses)
            if (!writes(use.access) || use.access == Access::Color) needed[use.image] = true;
    }
    last_run_.clear();
    for (size_t p = 0; p < passes_.size(); ++p) {
        if (!live[p]) continue;
        bool usable = true;
        for (const Use& use : passes_[p].uses) usable &= static_cast<bool>(*resources_[use.image].image);
        if (!usable) continue;
        for (const Use& use : passes_[p].uses) vk_.transition(*resources_[use.image].image, layout_for(use.access));
        passes_[p].record();
        last_run_.push_back(passes_[p].name);
    }
    passes_.clear();
    resources_.clear();
}

}  // namespace render::vk
