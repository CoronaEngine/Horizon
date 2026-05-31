include_guard(GLOBAL)

horizon_fetchcontent_declare(
    stb
    GIT_REPOSITORY https://ckzgit.iepose.cn/michael/stb.git
    GIT_TAG master
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(stb)

horizon_fetchcontent_declare(
    glfw
    GIT_REPOSITORY https://ckzgit.iepose.cn/michael/glfw.git
    GIT_TAG master
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(glfw)

horizon_fetchcontent_declare(
    tinyobjloader
    GIT_REPOSITORY https://ckzgit.iepose.cn/michael/tinyobjloader.git
    GIT_TAG release
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(tinyobjloader)

horizon_fetchcontent_declare(
    glm
    GIT_REPOSITORY https://ckzgit.iepose.cn/michael/glm.git
    GIT_TAG master
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(glm)
