include_guard(GLOBAL)

find_package(quill CONFIG REQUIRED)

if(NOT TARGET quill::quill)
    message(FATAL_ERROR "Conan package quill did not provide required target quill::quill")
endif()
