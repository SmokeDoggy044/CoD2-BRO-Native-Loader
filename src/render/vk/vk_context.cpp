// Vulkan instance, device, frames, images and uploads. See vk_context.h.

#include "vk_context.h"

#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

#include "log.h"

namespace render::vk {

namespace {

constexpr VkDeviceSize kStagingBytes = 16u << 20;  // per frame in flight

VKAPI_ATTR VkBool32 VKAPI_CALL debug_message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void* /*user*/) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        LOG_ERROR("vulkan", "%s", data->pMessage);
    else
        LOG_WARN("vulkan", "%s", data->pMessage);
    return VK_FALSE;
}

bool has_layer(const char* name) {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const VkLayerProperties& layer : layers)
        if (std::strcmp(layer.layerName, name) == 0) return true;
    return false;
}

bool has_instance_extension(const char* name) {
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
    for (const VkExtensionProperties& extension : extensions)
        if (std::strcmp(extension.extensionName, name) == 0) return true;
    return false;
}

bool has_device_extension(VkPhysicalDevice device, const char* name) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
    for (const VkExtensionProperties& extension : extensions)
        if (std::strcmp(extension.extensionName, name) == 0) return true;
    return false;
}

std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

}  // namespace

const char* result_name(VkResult result) {
    switch (result) {
        case VK_SUCCESS: return "success";
        case VK_NOT_READY: return "not ready";
        case VK_TIMEOUT: return "timeout";
        case VK_SUBOPTIMAL_KHR: return "suboptimal";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "out of host memory";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "out of device memory";
        case VK_ERROR_INITIALIZATION_FAILED: return "initialization failed";
        case VK_ERROR_DEVICE_LOST: return "device lost";
        case VK_ERROR_LAYER_NOT_PRESENT: return "layer not present";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "extension not present";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "feature not present";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "incompatible driver";
        case VK_ERROR_SURFACE_LOST_KHR: return "surface lost";
        case VK_ERROR_OUT_OF_DATE_KHR: return "out of date";
        default: return "error";
    }
}

Context::~Context() {
    if (device_) {
        vkDeviceWaitIdle(device_);
        destroy_deferred(true);
        for (Frame& frame : frames_) {
            if (frame.staging) vmaDestroyBuffer(allocator_, frame.staging, frame.staging_allocation);
            if (frame.descriptors) vkDestroyDescriptorPool(device_, frame.descriptors, nullptr);
            if (frame.acquired) vkDestroySemaphore(device_, frame.acquired, nullptr);
            if (frame.done) vkDestroyFence(device_, frame.done, nullptr);
            if (frame.pool) vkDestroyCommandPool(device_, frame.pool, nullptr);
        }
        if (allocator_) vmaDestroyAllocator(allocator_);
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_) SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);
    if (messenger_) vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
    if (instance_) vkDestroyInstance(instance_, nullptr);
}

bool Context::create(SDL_Window* window, bool validation) {
    if (volkInitialize() != VK_SUCCESS) {
        LOG_ERROR("render", "Vulkan: no Vulkan loader (install the graphics driver's Vulkan support)");
        return false;
    }
    if (volkGetInstanceVersion() < VK_API_VERSION_1_3) {
        LOG_ERROR("render", "Vulkan: the loader offers %u.%u, 1.3 is needed",
                  VK_API_VERSION_MAJOR(volkGetInstanceVersion()), VK_API_VERSION_MINOR(volkGetInstanceVersion()));
        return false;
    }
    Uint32 sdl_count = 0;
    const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_count);
    if (!sdl_extensions) {
        LOG_ERROR("render", "Vulkan: %s", SDL_GetError());
        return false;
    }
    std::vector<const char*> extensions(sdl_extensions, sdl_extensions + sdl_count);
    std::vector<const char*> layers;
    if (validation) {
        if (has_layer("VK_LAYER_KHRONOS_validation"))
            layers.push_back("VK_LAYER_KHRONOS_validation");
        else
            LOG_WARN("render", "Vulkan: validation asked for, but VK_LAYER_KHRONOS_validation is not installed");
        if (has_instance_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        else
            validation = false;
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Call of Duty 2: Big Red One";
    app.pEngineName = "cod_bro";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    info.enabledLayerCount = static_cast<uint32_t>(layers.size());
    info.ppEnabledLayerNames = layers.data();
    if (VkResult r = vkCreateInstance(&info, nullptr, &instance_); r != VK_SUCCESS) {
        LOG_ERROR("render", "Vulkan: no instance (%s)", result_name(r));
        return false;
    }
    volkLoadInstanceOnly(instance_);
    if (validation) {
        VkDebugUtilsMessengerCreateInfoEXT messenger{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        messenger.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messenger.pfnUserCallback = debug_message;
        vkCreateDebugUtilsMessengerEXT(instance_, &messenger, nullptr, &messenger_);
    }
    if (!SDL_Vulkan_CreateSurface(window, instance_, nullptr, &surface_)) {
        LOG_ERROR("render", "Vulkan: no window surface (%s)", SDL_GetError());
        return false;
    }

    bool has_d24s8 = false;
    if (!pick_device(has_d24s8)) return false;
    depth_format_ = has_d24s8 ? VK_FORMAT_D24_UNORM_S8_UINT : VK_FORMAT_D32_SFLOAT_S8_UINT;

    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(physical_, &supported);
    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext = &features13;
    features.features.samplerAnisotropy = supported.samplerAnisotropy;
    features.features.fillModeNonSolid = supported.fillModeNonSolid;
    features.features.depthBiasClamp = supported.depthBiasClamp;
    features.features.textureCompressionBC = supported.textureCompressionBC;  // the title's DXT textures
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = queue_family_;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.pNext = &features;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue;
    device_info.enabledExtensionCount = 1;
    device_info.ppEnabledExtensionNames = device_extensions;
    if (VkResult r = vkCreateDevice(physical_, &device_info, nullptr, &device_); r != VK_SUCCESS) {
        LOG_ERROR("render", "Vulkan: no device (%s)", result_name(r));
        return false;
    }
    volkLoadDevice(device_);
    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);

    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo allocator{};
    allocator.vulkanApiVersion = VK_API_VERSION_1_3;
    allocator.physicalDevice = physical_;
    allocator.device = device_;
    allocator.instance = instance_;
    allocator.pVulkanFunctions = &functions;
    if (VkResult r = vmaCreateAllocator(&allocator, &allocator_); r != VK_SUCCESS) {
        LOG_ERROR("render", "Vulkan: no memory allocator (%s)", result_name(r));
        return false;
    }
    if (!create_frames()) return false;
    begin_commands();
    return true;
}

// The first device with Vulkan 1.3, dynamic rendering, synchronization2, a swap chain and a queue that
// draws and presents to our surface; discrete GPUs first. COD_BRO_VK_DEVICE (part of the name) picks one.
bool Context::pick_device(bool& has_d24s8) {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());
    const char* wanted = std::getenv("COD_BRO_VK_DEVICE");
    int best_score = -1;
    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(device, &properties);
        VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &features13;
        vkGetPhysicalDeviceFeatures2(device, &features);
        const char* missing = nullptr;
        if (properties.apiVersion < VK_API_VERSION_1_3)
            missing = "Vulkan 1.3";
        else if (!features13.dynamicRendering || !features13.synchronization2)
            missing = "dynamic rendering / synchronization2";
        else if (!has_device_extension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            missing = "a swap chain";
        uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, families.data());
        int family = -1;
        for (uint32_t i = 0; i < family_count && family < 0 && !missing; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &present);
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) family = static_cast<int>(i);
        }
        if (!missing && family < 0) missing = "a queue that draws to the window";
        if (missing) {
            LOG_INFO("render", "Vulkan: %s has no %s", properties.deviceName, missing);
            continue;
        }
        int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 3
                    : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                    : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU            ? 0
                                                                                      : 1;
        if (wanted && *wanted && lower(properties.deviceName).find(lower(wanted)) != std::string::npos) score = 10;
        if (score > best_score) {
            best_score = score;
            physical_ = device;
            queue_family_ = static_cast<uint32_t>(family);
        }
    }
    if (!physical_) {
        LOG_ERROR("render", "Vulkan: no device with Vulkan 1.3 that can draw to the window");
        return false;
    }
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical_, &properties);
    VkFormatProperties d24s8;
    vkGetPhysicalDeviceFormatProperties(physical_, VK_FORMAT_D24_UNORM_S8_UINT, &d24s8);
    has_d24s8 = (d24s8.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
    LOG_INFO("render", "Vulkan %u.%u.%u: %s (driver %08x)%s", VK_API_VERSION_MAJOR(properties.apiVersion),
             VK_API_VERSION_MINOR(properties.apiVersion), VK_API_VERSION_PATCH(properties.apiVersion),
             properties.deviceName, properties.driverVersion, has_d24s8 ? "" : ", depth 32-bit float (no D24S8)");
    return true;
}

bool Context::create_frames() {
    for (Frame& frame : frames_) {
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool.queueFamilyIndex = queue_family_;
        if (vkCreateCommandPool(device_, &pool, nullptr, &frame.pool) != VK_SUCCESS) return false;
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = frame.pool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device_, &allocate, &frame.cmd) != VK_SUCCESS) return false;
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        if (vkCreateFence(device_, &fence, nullptr, &frame.done) != VK_SUCCESS) return false;
        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (vkCreateSemaphore(device_, &semaphore, nullptr, &frame.acquired) != VK_SUCCESS) return false;
        const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16384},
                                              {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 4096}};
        VkDescriptorPoolCreateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        descriptors.maxSets = 8192;
        descriptors.poolSizeCount = 2;
        descriptors.pPoolSizes = sizes;
        if (vkCreateDescriptorPool(device_, &descriptors, nullptr, &frame.descriptors) != VK_SUCCESS) return false;
        VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer.size = kStagingBytes;
        // Also read straight by draws: caller vertices and indices, shader constants.
        buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        VmaAllocationCreateInfo memory{};
        memory.usage = VMA_MEMORY_USAGE_AUTO;
        memory.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo mapped{};
        if (vmaCreateBuffer(allocator_, &buffer, &memory, &frame.staging, &frame.staging_allocation, &mapped) !=
            VK_SUCCESS)
            return false;
        frame.staging_memory = static_cast<uint8_t*>(mapped.pMappedData);
    }
    return true;
}

void Context::begin_commands() {
    Frame& frame = frames_[frame_];
    if (frame.submitted) {
        vkWaitForFences(device_, 1, &frame.done, VK_TRUE, UINT64_MAX);
        vkResetFences(device_, 1, &frame.done);
        frame.submitted = false;
    }
    destroy_deferred(false);
    vkResetCommandPool(device_, frame.pool, 0);
    vkResetDescriptorPool(device_, frame.descriptors, 0);
    frame.staging_used = 0;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(frame.cmd, &begin);
}

bool Context::submit_frame(VkSemaphore wait, VkSemaphore signal) {
    end_rendering();
    Frame& frame = frames_[frame_];
    vkEndCommandBuffer(frame.cmd);
    VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    command.commandBuffer = frame.cmd;
    VkSemaphoreSubmitInfo wait_info{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait_info.semaphore = wait;
    wait_info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphoreSubmitInfo signal_info{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal_info.semaphore = signal;
    signal_info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command;
    submit.waitSemaphoreInfoCount = wait ? 1 : 0;
    submit.pWaitSemaphoreInfos = &wait_info;
    submit.signalSemaphoreInfoCount = signal ? 1 : 0;
    submit.pSignalSemaphoreInfos = &signal_info;
    const VkResult r = vkQueueSubmit2(queue_, 1, &submit, frame.done);
    if (r != VK_SUCCESS) LOG_ERROR("render", "Vulkan: submit failed (%s)", result_name(r));
    frame.submitted = r == VK_SUCCESS;
    frame_ = (frame_ + 1) % kFramesInFlight;
    ++frame_number_;
    begin_commands();
    return r == VK_SUCCESS;
}

void Context::submit_and_wait() {
    end_rendering();
    Frame& frame = frames_[frame_];
    vkEndCommandBuffer(frame.cmd);
    VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    command.commandBuffer = frame.cmd;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command;
    if (VkResult r = vkQueueSubmit2(queue_, 1, &submit, frame.done); r != VK_SUCCESS) {
        LOG_ERROR("render", "Vulkan: submit failed (%s)", result_name(r));
    } else {
        vkWaitForFences(device_, 1, &frame.done, VK_TRUE, UINT64_MAX);
        vkResetFences(device_, 1, &frame.done);
    }
    // The descriptor sets of this frame stay: commands recorded from here on are new ones.
    vkResetCommandPool(device_, frame.pool, 0);
    frame.staging_used = 0;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(frame.cmd, &begin);
}

VkDescriptorSet Context::descriptor_set(VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = frames_[frame_].descriptors;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device_, &allocate, &set) != VK_SUCCESS) {
        // The pool is full: finish what uses it and start over.
        submit_and_wait();
        vkResetDescriptorPool(device_, frames_[frame_].descriptors, 0);
        if (vkAllocateDescriptorSets(device_, &allocate, &set) != VK_SUCCESS) return VK_NULL_HANDLE;
    }
    return set;
}

Staged Context::stage(VkDeviceSize bytes, VkDeviceSize alignment) {
    if (bytes > kStagingBytes) return {};
    Frame* frame = &frames_[frame_];
    VkDeviceSize offset = (frame->staging_used + alignment - 1) / alignment * alignment;
    if (offset + bytes > kStagingBytes) {
        submit_and_wait();
        frame = &frames_[frame_];
        offset = 0;
    }
    frame->staging_used = offset + bytes;
    return {frame->staging_memory + offset, frame->staging, offset};
}

bool Context::create_image(Image& image, VkFormat format, uint32_t width, uint32_t height,
                           VkImageUsageFlags usage) {
    const bool depth = format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
                       format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_D32_SFLOAT;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width, height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo memory{};
    memory.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(allocator_, &info, &memory, &image.image, &image.allocation, nullptr) != VK_SUCCESS) {
        LOG_ERROR("render", "Vulkan: cannot create a %ux%u image", width, height);
        return false;
    }
    image.format = format;
    image.width = width;
    image.height = height;
    image.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    image.aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    if (format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT)
        image.aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    // Copy-only images (uploads, read-backs) have no view.
    if (!(usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)))
        return true;
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = image.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    // Sampled depth views may only name one aspect; ours never sample depth.
    view.subresourceRange = {image.aspect, 0, 1, 0, 1};
    if (vkCreateImageView(device_, &view, nullptr, &image.view) != VK_SUCCESS) {
        vmaDestroyImage(allocator_, image.image, image.allocation);
        image = {};
        return false;
    }
    return true;
}

void Context::destroy_image(Image& image) {
    if (!image) return;
    if (target_color_ == &image || target_depth_ == &image) set_target(nullptr, nullptr);
    defer_destroy(image.image, image.allocation, image.view, image.sample_view);
    image = {};
}

void Context::transition(Image& image, VkImageLayout layout) {
    if (image.layout == layout && layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) return;
    end_rendering();
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.oldLayout = image.layout;
    barrier.newLayout = layout;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image.image;
    barrier.subresourceRange = {image.aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd(), &dependency);
    image.layout = layout;
}

void Context::upload(Image& image, const void* pixels, uint32_t pitch, uint32_t bytes_per_pixel) {
    const uint32_t row = image.width * bytes_per_pixel;
    const Staged staged = stage(static_cast<VkDeviceSize>(row) * image.height, 16);
    if (!staged.memory) return;
    for (uint32_t y = 0; y < image.height; ++y)
        std::memcpy(static_cast<uint8_t*>(staged.memory) + static_cast<size_t>(y) * row,
                    static_cast<const uint8_t*>(pixels) + static_cast<size_t>(y) * pitch, row);
    transition(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy copy{};
    copy.bufferOffset = staged.offset;
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {image.width, image.height, 1};
    vkCmdCopyBufferToImage(cmd(), staged.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
}

bool Context::create_texture_image(Image& image, const TextureImageDesc& desc) {
    const bool cube = desc.view_type == VK_IMAGE_VIEW_TYPE_CUBE;
    const bool volume = desc.view_type == VK_IMAGE_VIEW_TYPE_3D;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    info.imageType = volume ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    info.format = desc.format;
    info.extent = {desc.width, desc.height, volume ? desc.depth : 1};
    info.mipLevels = desc.levels;
    info.arrayLayers = cube ? 6 : 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo memory{};
    memory.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(allocator_, &info, &memory, &image.image, &image.allocation, nullptr) != VK_SUCCESS) {
        LOG_ERROR("render", "Vulkan: cannot create a %ux%ux%u texture (format %d, %u levels)", desc.width,
                  desc.height, desc.depth, int(desc.format), desc.levels);
        image = {};
        return false;
    }
    image.format = desc.format;
    image.width = desc.width;
    image.height = desc.height;
    image.depth = volume ? desc.depth : 1;
    image.levels = desc.levels;
    image.layers = info.arrayLayers;
    image.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    image.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = image.image;
    view.viewType = desc.view_type;
    view.format = desc.format;
    view.components = desc.components;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, desc.levels, 0, info.arrayLayers};
    if (vkCreateImageView(device_, &view, nullptr, &image.view) != VK_SUCCESS) {
        vmaDestroyImage(allocator_, image.image, image.allocation);
        image = {};
        return false;
    }
    return true;
}

bool Context::upload_level(Image& image, uint32_t level, uint32_t layer, const void* data, uint32_t row_bytes,
                           uint32_t rows, uint32_t pitch, uint32_t slice_pitch) {
    if (!image || level >= image.levels || layer >= image.layers) return false;
    const uint32_t width = std::max(1u, image.width >> level), height = std::max(1u, image.height >> level);
    const uint32_t depth = std::max(1u, image.depth >> level);
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(row_bytes) * rows * depth;
    const Staged staged = stage(bytes, 16);
    if (!staged.memory) {
        LOG_ERROR("render", "Vulkan: a texture level of %llu bytes does not fit the upload space",
                  static_cast<unsigned long long>(bytes));
        return false;
    }
    auto* out = static_cast<uint8_t*>(staged.memory);
    const auto* in = static_cast<const uint8_t*>(data);
    for (uint32_t z = 0; z < depth; ++z)
        for (uint32_t y = 0; y < rows; ++y)
            std::memcpy(out + (static_cast<size_t>(z) * rows + y) * row_bytes,
                        in + static_cast<size_t>(z) * slice_pitch + static_cast<size_t>(y) * pitch, row_bytes);
    transition(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy copy{};
    copy.bufferOffset = staged.offset;
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, layer, 1};
    copy.imageExtent = {width, height, depth};
    vkCmdCopyBufferToImage(cmd(), staged.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    transition(image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return true;
}

bool Context::upload_buffer(VkBuffer buffer, VkDeviceSize offset, const void* data, VkDeviceSize bytes) {
    // Large uploads (level geometry) go in pieces through the upload space.
    constexpr VkDeviceSize kPiece = 4u << 20;
    end_rendering();
    // Draws recorded before it read the old bytes first.
    VkMemoryBarrier2 before{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    before.srcStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_2_COPY_BIT;
    before.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    before.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    VkDependencyInfo wait_reads{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    wait_reads.memoryBarrierCount = 1;
    wait_reads.pMemoryBarriers = &before;
    vkCmdPipelineBarrier2(cmd(), &wait_reads);
    for (VkDeviceSize done = 0; done < bytes;) {
        const VkDeviceSize piece = std::min(kPiece, bytes - done);
        const Staged staged = stage(piece, 16);
        if (!staged.memory) return false;
        std::memcpy(staged.memory, static_cast<const uint8_t*>(data) + done, static_cast<size_t>(piece));
        VkBufferCopy copy{staged.offset, offset + done, piece};
        vkCmdCopyBuffer(cmd(), staged.buffer, buffer, 1, &copy);
        done += piece;
    }
    VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT |
                            VK_ACCESS_2_TRANSFER_WRITE_BIT;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd(), &dependency);
    return true;
}

bool Context::read(Image& image, std::vector<uint32_t>& pixels) {
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(image.width) * image.height * 4;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = bytes;
    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo memory{};
    memory.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    memory.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VmaAllocationInfo mapped{};
    if (vmaCreateBuffer(allocator_, &info, &memory, &buffer, &allocation, &mapped) != VK_SUCCESS) return false;
    transition(image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {image.width, image.height, 1};
    vkCmdCopyImageToBuffer(cmd(), image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
    submit_and_wait();
    vmaInvalidateAllocation(allocator_, allocation, 0, VK_WHOLE_SIZE);
    pixels.resize(static_cast<size_t>(image.width) * image.height);
    std::memcpy(pixels.data(), mapped.pMappedData, static_cast<size_t>(bytes));
    vmaDestroyBuffer(allocator_, buffer, allocation);
    return true;
}

void Context::set_target(Image* color, Image* depth) {
    if (color == target_color_ && depth == target_depth_) return;
    end_rendering();
    target_color_ = color;
    target_depth_ = depth;
}

bool Context::begin_rendering() {
    if (rendering_) return true;
    if (!target_color_) return false;
    transition(*target_color_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    if (target_depth_) transition(*target_depth_, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = target_color_->view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (target_depth_) {
        depth.imageView = target_depth_->view;
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = {{0, 0}, {target_color_->width, target_color_->height}};
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &color;
    if (target_depth_) {
        info.pDepthAttachment = &depth;
        if (target_depth_->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) info.pStencilAttachment = &depth;
    }
    vkCmdBeginRendering(cmd(), &info);
    rendering_ = true;
    return true;
}

void Context::end_rendering() {
    if (!rendering_) return;
    vkCmdEndRendering(cmd());
    rendering_ = false;
}

void Context::defer_destroy(VkBuffer buffer, VmaAllocation allocation) {
    Deferred d;
    d.frame = frame_number_;
    d.buffer = buffer;
    d.allocation = allocation;
    deferred_.push_back(d);
}

void Context::defer_destroy(VkImage image, VmaAllocation allocation, VkImageView view, VkImageView view2) {
    Deferred d;
    d.frame = frame_number_;
    d.image = image;
    d.allocation = allocation;
    d.view = view;
    d.view2 = view2;
    deferred_.push_back(d);
}

void Context::defer_destroy(VkPipeline pipeline) {
    Deferred d;
    d.frame = frame_number_;
    d.pipeline = pipeline;
    deferred_.push_back(d);
}

// Everything deferred in a frame whose commands have finished (or all, at shutdown after a wait).
void Context::destroy_deferred(bool all) {
    auto done = [&](const Deferred& d) { return all || d.frame + kFramesInFlight <= frame_number_; };
    for (const Deferred& d : deferred_) {
        if (!done(d)) continue;
        if (d.view) vkDestroyImageView(device_, d.view, nullptr);
        if (d.view2) vkDestroyImageView(device_, d.view2, nullptr);
        if (d.image) vmaDestroyImage(allocator_, d.image, d.allocation);
        if (d.buffer) vmaDestroyBuffer(allocator_, d.buffer, d.allocation);
        if (d.pipeline) vkDestroyPipeline(device_, d.pipeline, nullptr);
    }
    deferred_.erase(std::remove_if(deferred_.begin(), deferred_.end(), done), deferred_.end());
}

void Context::wait_idle() {
    if (device_) vkDeviceWaitIdle(device_);
}

}  // namespace render::vk
