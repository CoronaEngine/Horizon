# Portable contracts are available even when Engine/Ocarina/hotfix are disabled.
add_library(horizon-hotfix-api INTERFACE)
target_include_directories(horizon-hotfix-api INTERFACE
    "${PROJECT_SOURCE_DIR}/include")
target_compile_features(horizon-hotfix-api INTERFACE cxx_std_20)
