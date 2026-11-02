include_guard(GLOBAL)

find_package(ktm CONFIG REQUIRED)
_horizon_alias_target(horizon::ktm ktm::ktm)

find_package(pfr CONFIG REQUIRED)
_horizon_alias_target(horizon::pfr pfr::pfr)

find_package(SPIRV-Tools CONFIG REQUIRED)
_horizon_alias_target(horizon::spirv_tools_link SPIRV-Tools-link)

find_package(VulkanHeaders CONFIG REQUIRED)
_horizon_alias_target(horizon::vulkan_headers vulkan-headers::vulkan-headers)

include(${CMAKE_CURRENT_LIST_DIR}/horizon_vulkan.cmake)

find_package(volk CONFIG REQUIRED)
_horizon_alias_target(horizon::volk volk::volk)

find_package(VulkanMemoryAllocator CONFIG REQUIRED)
_horizon_alias_target(horizon::vma GPUOpen::VulkanMemoryAllocator)
