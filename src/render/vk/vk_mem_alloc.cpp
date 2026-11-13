// The Vulkan Memory Allocator's implementation. Its Vulkan functions come from volk at run time
// (VmaVulkanFunctions in Context::create): nothing links against a Vulkan library.
#include <volk.h>

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
