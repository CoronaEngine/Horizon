include_guard(GLOBAL)

function(_horizon_example_find_conan_package package_name)
    if(HORIZON_DEPENDENCY_PROVIDER STREQUAL "fetchcontent")
        return()
    endif()

    find_package(${package_name} CONFIG QUIET)
    if(NOT ${package_name}_FOUND AND HORIZON_DEPENDENCY_PROVIDER STREQUAL "conan")
        find_package(${package_name} CONFIG REQUIRED)
    endif()
endfunction()

function(_horizon_example_alias_target alias_name)
    if(TARGET ${alias_name})
        return()
    endif()

    foreach(candidate IN LISTS ARGN)
        if(TARGET ${candidate})
            add_library(${alias_name} INTERFACE IMPORTED GLOBAL)
            target_link_libraries(${alias_name} INTERFACE ${candidate})
            return()
        endif()
    endforeach()
endfunction()

function(_horizon_example_header_alias alias_name include_dir)
    if(TARGET ${alias_name})
        return()
    endif()

    string(MAKE_C_IDENTIFIER "${alias_name}" local_name)
    add_library(${local_name} INTERFACE)
    target_include_directories(${local_name} INTERFACE "${include_dir}")
    add_library(${alias_name} ALIAS ${local_name})
endfunction()

_horizon_example_find_conan_package(stb)
_horizon_example_alias_target(horizon::stb stb::stb stb)
if(NOT TARGET horizon::stb)
    horizon_fetchcontent_declare(
        stb
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(stb)
    _horizon_example_header_alias(horizon::stb "${stb_SOURCE_DIR}")
endif()

_horizon_example_find_conan_package(glfw3)
_horizon_example_alias_target(horizon::glfw glfw glfw::glfw glfw3)
if(NOT TARGET horizon::glfw)
    horizon_fetchcontent_declare(
        glfw
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(glfw)
    _horizon_example_alias_target(horizon::glfw glfw)
endif()

_horizon_example_find_conan_package(tinyobjloader)
_horizon_example_alias_target(horizon::tinyobjloader tinyobjloader::tinyobjloader tinyobjloader)
if(NOT TARGET horizon::tinyobjloader)
    horizon_fetchcontent_declare(
        tinyobjloader
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(tinyobjloader)
    _horizon_example_header_alias(horizon::tinyobjloader "${tinyobjloader_SOURCE_DIR}")
endif()

_horizon_example_find_conan_package(glm)
_horizon_example_alias_target(horizon::glm glm::glm glm)
if(NOT TARGET horizon::glm)
    horizon_fetchcontent_declare(
        glm
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(glm)
    _horizon_example_alias_target(horizon::glm glm)
endif()
