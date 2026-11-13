#pragma once

// The Vulkan back end's core (own engine 0.5): instance, device, queue, memory (VMA), the frames in
// flight with their command buffers, upload space and descriptor pools, images with their layouts, and
// dynamic rendering onto the current target. Vulkan 1.3 is required (dynamic rendering, synchronization2);
// older GPUs keep the D3D9 back end.
//
// Recording model: one command buffer per frame, begun when the previous one was handed to present.
// Draw-time work records into it; work that needs results at once (screenshots, read_target) flushes it
// (submit_and_wait) and continues in a fresh one.

#include <volk.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "vk_mem_alloc.h"

struct SDL_Window;

namespace render::vk {

constexpr uint32_t kFramesInFlight = 2;

struct Image {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkImageView view = VK_NULL_HANDLE;
    VkImageView sample_view = VK_NULL_HANDLE;  // a second view for sampling (swizzled), or none
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspect = 0;
    uint32_t width = 0, height = 0;
    uint32_t depth = 1, levels = 1, layers = 1;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    explicit operator bool() const { return image != VK_NULL_HANDLE; }
};

// A sampled image of the title's: 2D, cube (6 layers) or volume, with mip levels and a component mapping
// (formats Vulkan has no direct match for: L8, A8, X formats).
struct TextureImageDesc {
    VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_2D;
    VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
    uint32_t width = 1, height = 1, depth = 1, levels = 1;
    VkComponentMapping components{};  // identity
};

// Where an upload's bytes went: copy from `buffer` at `offset`.
struct Staged {
    void* memory = nullptr;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
};

class Context {
public:
    Context() = default;
    ~Context();
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    // Instance and device for `window` (made with SDL_WINDOW_VULKAN); `validation`: the Khronos layer
    // when installed. False (logged) when there is no usable Vulkan 1.3 device.
    bool create(SDL_Window* window, bool validation);

    VkInstance instance() const { return instance_; }
    VkPhysicalDevice physical() const { return physical_; }
    VkDevice device() const { return device_; }
    VkQueue queue() const { return queue_; }
    uint32_t queue_family() const { return queue_family_; }
    VkSurfaceKHR surface() const { return surface_; }
    VmaAllocator allocator() const { return allocator_; }
    VkFormat depth_format() const { return depth_format_; }  // D24S8, or D32S8 where there is none
    const VkPhysicalDeviceLimits& limits() const { return limits_; }
    const VkPhysicalDeviceFeatures& features() const { return features_; }  // the enabled ones
    // Changes whenever the upload space starts over (a new frame, or a flush): staged bytes from an older
    // epoch are gone.
    uint64_t staging_epoch() const { return staging_epoch_; }

    // --- Frames ---
    uint32_t frame_slot() const { return frame_; }
    uint64_t frame_number() const { return frame_number_; }
    VkCommandBuffer cmd() const { return frames_[frame_].cmd; }
    VkSemaphore acquire_semaphore() const { return frames_[frame_].acquired; }
    // Ends the frame's commands and submits them (waiting for `wait` before colour output when set,
    // signalling `signal`), then starts the next frame (waiting until its slot's last use finished).
    bool submit_frame(VkSemaphore wait, VkSemaphore signal);
    // Submits what is recorded so far and waits for it; recording continues in the same frame.
    void submit_and_wait();
    // A descriptor set of `layout` for this frame's draws (freed when the slot comes round again).
    VkDescriptorSet descriptor_set(VkDescriptorSetLayout layout);

    // Upload space in this frame's staging buffer (flushes when it is full); null if `bytes` is larger
    // than the whole buffer.
    Staged stage(VkDeviceSize bytes, VkDeviceSize alignment = 16);

    // --- Images ---
    bool create_image(Image& image, VkFormat format, uint32_t width, uint32_t height, VkImageUsageFlags usage);
    void destroy_image(Image& image);  // after the frames that may use it finished
    // Records a barrier moving `image` to `layout` (ends rendering first).
    void transition(Image& image, VkImageLayout layout);
    // A one-colour 2D copy of `pixels` (`pitch` bytes per row) into the whole image.
    void upload(Image& image, const void* pixels, uint32_t pitch, uint32_t bytes_per_pixel);
    bool create_texture_image(Image& image, const TextureImageDesc& desc);
    // One mip level of one layer: `rows` rows of `row_bytes` (block rows for compressed formats) per slice,
    // `pitch` / `slice_pitch` apart in `data`. Recorded in command order: draws before it see the old texels.
    bool upload_level(Image& image, uint32_t level, uint32_t layer, const void* data, uint32_t row_bytes,
                      uint32_t rows, uint32_t pitch, uint32_t slice_pitch);
    // `bytes` into a device buffer at `offset` (command order; vertex/index/uniform reads after it wait).
    bool upload_buffer(VkBuffer buffer, VkDeviceSize offset, const void* data, VkDeviceSize bytes);
    // Reads a colour image back as 4-byte pixels, `width` * `height` (submits and waits).
    bool read(Image& image, std::vector<uint32_t>& pixels);

    // --- Rendering ---
    // Draws and clears go to `color` (and `depth`, may be null) from now on.
    void set_target(Image* color, Image* depth);
    Image* target_color() const { return target_color_; }
    Image* target_depth() const { return target_depth_; }
    // Inside dynamic rendering on the current target (begins it if needed); false without a target.
    bool begin_rendering();
    void end_rendering();
    bool rendering() const { return rendering_; }

    // Destroyed once the GPU no longer uses them (kFramesInFlight frames later).
    void defer_destroy(VkBuffer buffer, VmaAllocation allocation);
    void defer_destroy(VkImage image, VmaAllocation allocation, VkImageView view,
                       VkImageView view2 = VK_NULL_HANDLE);
    void defer_destroy(VkPipeline pipeline);

    void wait_idle();

private:
    struct Deferred {
        uint64_t frame = 0;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE, view2 = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
    };
    struct Frame {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence done = VK_NULL_HANDLE;
        VkSemaphore acquired = VK_NULL_HANDLE;
        VkDescriptorPool descriptors = VK_NULL_HANDLE;
        VkBuffer staging = VK_NULL_HANDLE;
        VmaAllocation staging_allocation = nullptr;
        uint8_t* staging_memory = nullptr;
        VkDeviceSize staging_used = 0;
        bool submitted = false;  // `done` will be signalled
    };

    bool pick_device(bool& has_d24s8);
    bool create_frames();
    void begin_commands();
    void destroy_deferred(bool all);

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queue_family_ = 0;
    VmaAllocator allocator_ = nullptr;
    VkFormat depth_format_ = VK_FORMAT_D24_UNORM_S8_UINT;
    Frame frames_[kFramesInFlight];
    uint32_t frame_ = 0;
    uint64_t frame_number_ = 0;
    std::vector<Deferred> deferred_;
    Image* target_color_ = nullptr;
    Image* target_depth_ = nullptr;
    bool rendering_ = false;
    VkPhysicalDeviceLimits limits_{};
    VkPhysicalDeviceFeatures features_{};
    uint64_t staging_epoch_ = 1;
};

// VkResult -> its name, for logs.
const char* result_name(VkResult result);

}  // namespace render::vk
