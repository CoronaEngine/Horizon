include_guard(GLOBAL)

include(FetchContent)

if(COMMAND horizon_fetchcontent_declare)
    horizon_fetchcontent_declare(
        quill
        GIT_REPOSITORY https://github.com/odygrd/quill.git
        GIT_TAG v11.0.1
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL TRUE
    )
else()
    FetchContent_Declare(
        quill
        GIT_REPOSITORY https://github.com/odygrd/quill.git
        GIT_TAG v11.0.1
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL TRUE
    )
endif()

FetchContent_MakeAvailable(quill)
