# Ocarina DLLs and their host must share one Quill registry and backend.
if(HORIZON_BUILD_OCARINA)
    add_library(horizon-logging SHARED logging.cpp)
    target_compile_definitions(horizon-logging PRIVATE QUILL_DLL_EXPORT INTERFACE QUILL_DLL_IMPORT)
    set_target_properties(horizon-logging PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
else()
    add_library(horizon-logging STATIC logging.cpp)
endif()

target_include_directories(horizon-logging PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_compile_features(horizon-logging PUBLIC cxx_std_20)
target_link_libraries(horizon-logging PUBLIC quill::quill)
if(MSVC)
    # The public logging header contains UTF-8 text.
    target_compile_options(horizon-logging PUBLIC /utf-8)
endif()
