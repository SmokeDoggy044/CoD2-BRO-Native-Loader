# Vulkan back end (own engine 0.5): headers, volk (loads Vulkan at run time, so the game starts without a
# Vulkan driver and falls back to D3D9) and the Vulkan Memory Allocator. All fetched at configure time on
# every platform (Apache-2.0 / MIT / MIT); nothing is linked against a Vulkan library.
include(FetchContent)
FetchContent_Declare(vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG vulkan-sdk-1.4.350.0
    SOURCE_SUBDIR no-cmake)
FetchContent_Declare(volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG vulkan-sdk-1.4.350.0
    SOURCE_SUBDIR no-cmake)
FetchContent_Declare(vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.3.0
    SOURCE_SUBDIR no-cmake)
# Sources only (SOURCE_SUBDIR names no CMake project): their own projects are not added.
FetchContent_MakeAvailable(vulkan_headers volk vma)

add_library(vulkan_headers INTERFACE)
target_include_directories(vulkan_headers SYSTEM INTERFACE ${vulkan_headers_SOURCE_DIR}/include)
target_compile_definitions(vulkan_headers INTERFACE VK_NO_PROTOTYPES)
if(WIN32)
    target_compile_definitions(vulkan_headers INTERFACE VK_USE_PLATFORM_WIN32_KHR)
endif()

add_library(volk STATIC ${volk_SOURCE_DIR}/volk.c)
target_include_directories(volk SYSTEM PUBLIC ${volk_SOURCE_DIR})
target_link_libraries(volk PUBLIC vulkan_headers)
if(NOT WIN32)
    target_link_libraries(volk PRIVATE ${CMAKE_DL_LIBS})
endif()

add_library(vma INTERFACE)
target_include_directories(vma SYSTEM INTERFACE ${vma_SOURCE_DIR}/include)
target_link_libraries(vma INTERFACE vulkan_headers)

# Our own Vulkan shaders (GLSL) are compiled to SPIR-V at build time with glslang.
find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslang)
if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR "glslangValidator not found (Fedora: dnf install glslang; Debian/Ubuntu: glslang-tools)")
endif()

# Dear ImGui's Vulkan renderer for the in-game PC menu (cmake/imgui.cmake fetched ImGui).
add_library(imgui_vulkan STATIC ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
target_compile_definitions(imgui_vulkan PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)
target_link_libraries(imgui_vulkan PUBLIC imgui volk)
