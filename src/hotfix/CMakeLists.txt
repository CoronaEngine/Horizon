set(hotfix_runtime_sources
    build_system.cpp
    file_tool.cpp
    hotfix.cpp
    module_interface.cpp
    staged_module.cpp)

add_library(horizon-hotfix-runtime-api INTERFACE)
target_include_directories(horizon-hotfix-runtime-api INTERFACE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(horizon-hotfix-runtime-api INTERFACE horizon-hotfix-api horizon::fmt)

add_library(horizon-hotfix-runtime OBJECT ${hotfix_runtime_sources})
set_target_properties(horizon-hotfix-runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)

find_package(Threads REQUIRED)
target_link_libraries(horizon-hotfix-runtime PUBLIC
    horizon-hotfix-runtime-api horizon-logging Threads::Threads ${CMAKE_DL_LIBS})

add_library(horizon-hotfix STATIC)
target_link_libraries(horizon-hotfix
    PUBLIC horizon-hotfix-runtime-api
    PRIVATE horizon-hotfix-runtime)
