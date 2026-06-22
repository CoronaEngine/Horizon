add_library(vision-hotfix-rules_parser INTERFACE)

function(vision_add_rules_parser_plugins name)
    cmake_parse_arguments(PLUGIN "" "CATEGORY" "SOURCES" ${ARGN})
    set(lib_name vision-hotfix-${PLUGIN_CATEGORY}-${name})

    add_library(${lib_name} MODULE ${PLUGIN_SOURCES})
    target_link_libraries(${lib_name} PRIVATE
            vision-hotfix)
    target_compile_definitions(${lib_name} PRIVATE
            VISION_PLUGIN_NAME="${name}"
            VISION_CATEGORY="${PLUGIN_CATEGORY}")
    set(category vision-hotfix-${PLUGIN_CATEGORY})
    add_dependencies(${category} ${lib_name})

    set_target_properties(${lib_name} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
            WINDOWS_EXPORT_ALL_SYMBOLS ON
            UNITY_BUILD OFF
            DEBUG_POSTFIX "")
endfunction()

vision_add_rules_parser_plugins(ninja CATEGORY rules_parser SOURCES ninja.cpp)
vision_add_rules_parser_plugins(json CATEGORY rules_parser SOURCES json.cpp)
