#pragma once

#ifdef __cplusplus
#define VS_EXTERN_C extern "C"
#else
#define VS_EXTERN_C
#endif

#ifdef _MSC_VER
#define VS_EXPORT_API VS_EXTERN_C __declspec(dllexport)
#else
#define VS_EXPORT_API VS_EXTERN_C [[gnu::visibility("default")]]
#endif
