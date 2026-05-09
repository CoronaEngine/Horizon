include_guard(GLOBAL)

include(FetchContent)

FetchContent_Declare(
    quill
    GIT_REPOSITORY https://github.com/odygrd/quill.git
    GIT_TAG v11.0.1
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL TRUE
)

FetchContent_MakeAvailable(quill)