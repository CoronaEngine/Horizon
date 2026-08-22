include_guard(GLOBAL)

find_package(ktm CONFIG REQUIRED)
_horizon_alias_target(horizon::ktm ktm::ktm)
_horizon_require_target(horizon::ktm "ktm is required by Helicon public headers")

find_package(pfr CONFIG REQUIRED)
_horizon_alias_target(horizon::pfr pfr::pfr pfr)
_horizon_require_target(horizon::pfr "Boost.PFR headers are required by Helicon")

find_package(SPIRV-Tools CONFIG REQUIRED)
_horizon_alias_target(horizon::spirv_tools_link SPIRV-Tools-link spirv-tools::spirv-tools-link)
_horizon_require_target(horizon::spirv_tools_link "SPIRV-Tools link target is required by Helicon")

if(MSVC AND TARGET SPIRV-Tools-opt)
    get_target_property(_horizon_spirv_tools_opt_imported SPIRV-Tools-opt IMPORTED)
endif()
if(MSVC AND TARGET SPIRV-Tools-opt AND NOT _horizon_spirv_tools_opt_imported)
    target_compile_options(SPIRV-Tools-opt PRIVATE /Wv:18)
    target_compile_options(SPIRV-Tools-opt PRIVATE /wd4717 /wd5232)
endif()
unset(_horizon_spirv_tools_opt_imported)

find_package(VulkanHeaders CONFIG REQUIRED)
_horizon_alias_target(horizon::vulkan_headers vulkan-headers::vulkan-headers Vulkan-Headers)
_horizon_require_target(horizon::vulkan_headers "Vulkan headers are required by Horizon and Volk")

find_package(volk CONFIG REQUIRED)
_horizon_alias_target(horizon::volk volk::volk volk)
_horizon_require_target(horizon::volk "Volk is required by the Vulkan backend")

find_package(VulkanMemoryAllocator CONFIG REQUIRED)
_horizon_alias_target(horizon::vma GPUOpen::VulkanMemoryAllocator vulkan-memory-allocator::vulkan-memory-allocator VulkanMemoryAllocator)
_horizon_require_target(horizon::vma "Vulkan Memory Allocator is required by the Vulkan backend")
