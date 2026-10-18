include_guard(GLOBAL)

include(FetchContent)

FetchContent_Declare(
    quill
    GIT_REPOSITORY https://github.com/odygrd/quill.git
    GIT_TAG 4d08662de3bbb00a0855e9ef670ad0234f6a7606
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL TRUE
)

FetchContent_MakeAvailable(quill)