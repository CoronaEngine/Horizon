#pragma once

// Native window handle used to create the Vulkan presentation surface.
//
// The GLFW_EXPOSE_NATIVE_* selects must be defined before <GLFW/glfw3.h>, and on
// Linux the X11 variant pulls in <X11/Xlib.h>, which leaks object-like macros
// such as Always / None / Bool / Status / Success. Those macros collide with
// ordinary C++ identifiers, so they are removed right after the X11 declarations
// have been parsed. Keep this header free of any other include so it can stay the
// first thing an example pulls in.

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#elif defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#endif

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#if defined(_WIN32)
#define HORIZON_NATIVE_WINDOW(window) glfwGetWin32Window(window)
#elif defined(__APPLE__)
#define HORIZON_NATIVE_WINDOW(window) glfwGetCocoaView(window)
#elif defined(__linux__)
#define HORIZON_NATIVE_WINDOW(window) reinterpret_cast<void*>(glfwGetX11Window(window))
#endif

#if defined(__linux__)
#undef Always
#undef None
#undef Bool
#undef Status
#undef Success
#endif
