#pragma once

#include "support.h"

#ifdef __cplusplus
#define VS_EXTERN_C extern "C"
#define VS_NOEXCEPT noexcept
#else
#define VS_EXTERN_C
#define VS_NOEXCEPT
#endif

#ifdef _MSC_VER
#define VS_FORCE_INLINE inline
#define VS_NEVER_INLINE __declspec(noinline)
#define VS_DLL
#define VS_EXPORT_API VS_EXTERN_C __declspec(dllexport)
#define VS_IMPORT_API VS_EXTERN_C __declspec(dllimport)
#else
#define VS_FORCE_INLINE [[gnu::always_inline, gnu::hot]] inline
#define VS_NEVER_INLINE [[gnu::noinline]]
#define VS_DLL
#define VS_EXPORT_API VS_EXTERN_C [[gnu::visibility("default")]]
#define VS_IMPORT_API VS_EXTERN_C
#endif

#ifdef VS_EXPORT_DLL
#define VS_EXPORT_SYMBOL __declspec(dllexport)
#else
#define VS_EXPORT_SYMBOL __declspec(dllimport)
#endif
