if(NOT DEFINED HORIZON_SOURCE_DIR)
    message(FATAL_ERROR "HORIZON_SOURCE_DIR is required")
endif()

set(image_source_dir "${HORIZON_SOURCE_DIR}/src/image")
set(old_dsl_image_dir "${HORIZON_SOURCE_DIR}/src/dsl/image")
set(dsl_cmake_file "${HORIZON_SOURCE_DIR}/src/dsl/CMakeLists.txt")

if(EXISTS "${old_dsl_image_dir}")
    message(FATAL_ERROR "Image migration is incomplete: ${old_dsl_image_dir} still exists")
endif()

file(GLOB_RECURSE image_files LIST_DIRECTORIES false
    "${image_source_dir}/*.h"
    "${image_source_dir}/*.hpp"
    "${image_source_dir}/*.cpp"
    "${image_source_dir}/CMakeLists.txt")

foreach(image_file IN LISTS image_files)
    file(READ "${image_file}" image_content)
    string(TOLOWER "${image_content}" image_content_lower)
    foreach(forbidden IN ITEMS "dsl/" "ast/" "rhi/" "vulkan" "modules/ocarina")
        string(FIND "${image_content_lower}" "${forbidden}" match_index)
        if(NOT match_index EQUAL -1)
            message(FATAL_ERROR
                "Image module boundary violation in ${image_file}: contains '${forbidden}'")
        endif()
    endforeach()
endforeach()

file(READ "${dsl_cmake_file}" dsl_cmake_content)
string(FIND "${dsl_cmake_content}" "horizon-image" dsl_image_link_index)
if(NOT dsl_image_link_index EQUAL -1)
    message(FATAL_ERROR "horizon-dsl must not depend on horizon-image")
endif()
