include_guard(GLOBAL)

# ======================== Tracy Profiler ========================
# HORIZON_ENABLE_TRACY=OFF 时不拉取、不链接，埋点宏全部展开为空。
if(NOT HORIZON_ENABLE_TRACY)
    return()
endif()

set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
# ON_DEMAND: 仅在 Tracy Profiler UI 连接时才采集，未连接时运行开销接近零。
# 若需要从进程启动第一帧就开始采集，把这一项改为 OFF。
set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
set(TRACY_STATIC ON CACHE BOOL "" FORCE)

horizon_fetchcontent_declare(
    tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG v0.13.1
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(tracy)
