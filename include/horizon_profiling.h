#pragma once

// Tracy instrumentation wrapper. When HORIZON_ENABLE_TRACY is OFF the macros
// expand to nothing, so call sites never need #ifdef guards.
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
