include_guard(GLOBAL)

horizon_fetchcontent_declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(stb)

horizon_fetchcontent_declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG master
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(glfw)

horizon_fetchcontent_declare(
    tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG release
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(tinyobjloader)

horizon_fetchcontent_declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG master
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(glm)

# ======================== Dear ImGui（examples 专用） ========================
# ImGui 仓库不带 CMakeLists，FetchContent 仅做源码检出，目标由这里手工创建。
horizon_fetchcontent_declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.8
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(imgui)

# 渲染统一走 examples/imgui_horizon.cpp 的 Horizon 原生后端，
# 这里只需要 ImGui 核心源码 + GLFW 输入后端。
add_library(horizon_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
)
target_include_directories(horizon_imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(horizon_imgui PUBLIC glfw)
