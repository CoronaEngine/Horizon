#pragma once

// ================================================================
// Profiling instrumentation —— 内部头，不属于公共 API
// ----------------------------------------------------------------
// Tracy wrapper. When HORIZON_TRACY_ENABLED is OFF the macros expand to
// nothing, so call sites never need #ifdef guards.
//
// 刻意住在 src/ 而非 include/：profiling 是实现细节，不是我们对外承诺的能力。
// 放公共头有两重代价——Tracy.hpp 漏进每个引用公共头的翻译单元，且等于宣告
// "埋点宏是 API"。
//
// 埋点的目标必须自己声明 HORIZON_TRACY_ENABLED 并链 Tracy::TracyClient：
// Horizon 库内部由 src/CMakeLists.txt 以 PRIVATE 给出，in-tree 消费方（如
// example_drawstress）在自己的 CMakeLists 里显式 opt-in。
//
// ⚠️ 失败模式：若目标没拿到该 define，下面的宏会展开成空——**不产生任何编译
// 错误**，埋点无声消失。改动 Tracy 的 CMake 连线后，务必在产物里 grep zone 名
// 验证（例：Horizon::encode、drawstress::submit），别只看构建通过。
// ================================================================

#if defined(HORIZON_TRACY_ENABLED)

#include <tracy/Tracy.hpp>

// Marks the end of a frame (call once per presented frame).
#define HORIZON_PROFILE_FRAME() FrameMark
// Scoped CPU zone named after the enclosing function.
#define HORIZON_PROFILE_SCOPE() ZoneScoped
// Scoped CPU zone with an explicit name (string literal).
#define HORIZON_PROFILE_SCOPE_N(name) ZoneScopedN(name)
// Plots a numeric value over time (name must be a string literal).
#define HORIZON_PROFILE_PLOT(name, value) TracyPlot(name, static_cast<double>(value))
// Names the current thread in the Tracy UI.
#define HORIZON_PROFILE_THREAD(name) tracy::SetThreadName(name)

#else

#define HORIZON_PROFILE_FRAME()
#define HORIZON_PROFILE_SCOPE()
#define HORIZON_PROFILE_SCOPE_N(name)
#define HORIZON_PROFILE_PLOT(name, value)
#define HORIZON_PROFILE_THREAD(name)

#endif
