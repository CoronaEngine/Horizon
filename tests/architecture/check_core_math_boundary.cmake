if(NOT DEFINED CORE_SOURCE_DIR)
    message(FATAL_ERROR "CORE_SOURCE_DIR is required")
endif()

file(GLOB_RECURSE CORE_FILES
    "${CORE_SOURCE_DIR}/*.h"
    "${CORE_SOURCE_DIR}/*.hpp"
    "${CORE_SOURCE_DIR}/*.inl"
    "${CORE_SOURCE_DIR}/*.cpp"
)

set(VIOLATIONS "")
foreach(CORE_FILE IN LISTS CORE_FILES)
    file(READ "${CORE_FILE}" CONTENT)
    if(CONTENT MATCHES "horizon::math|(^|[\"<])math/|core/numeric")
        list(APPEND VIOLATIONS "${CORE_FILE}")
    endif()
endforeach()

if(VIOLATIONS)
    list(JOIN VIOLATIONS "\n" VIOLATION_LIST)
    message(FATAL_ERROR
        "horizon-core must not depend on horizon-math. Violating files:\n${VIOLATION_LIST}")
endif()
