# Portable contracts are available even when Engine/Ocarina/hotfix are disabled.
add_library(horizon-hotfix-api INTERFACE)
target_include_directories(horizon-hotfix-api INTERFACE
    "${PROJECT_SOURCE_DIR}/include")
target_compile_features(horizon-hotfix-api INTERFACE cxx_std_20)

add_library(horizon-hotfix-build-cmake STATIC cmake_build_backend.cpp)
target_link_libraries(horizon-hotfix-build-cmake PUBLIC horizon-hotfix-api)

if(WIN32)
    add_library(horizon-hotfix-process-windows STATIC windows_process_runner.cpp)
    target_link_libraries(horizon-hotfix-process-windows PUBLIC horizon-hotfix-api)
elseif(UNIX)
    find_package(Threads REQUIRED)
    add_library(horizon-hotfix-process-posix STATIC posix_process_runner.cpp)
    target_link_libraries(horizon-hotfix-process-posix
        PUBLIC horizon-hotfix-api PRIVATE Threads::Threads)
endif()
