//
// Created by Zero on 15/05/2022.
//

#pragma once

#include "core/type_system/type_desc.h"
#include "../core/var.h"
#include "soa.h"

#define OC_STRUCT_ALIAS(NS, S)      \
    namespace NS {                  \
    using S##Var = ocarina::Var<S>; \
    }

#define OC_STRUCT_IMPL(NS, S, ...)                                            \
    OC_MAKE_STRUCT_REFLECTION(NS::S, ##__VA_ARGS__)                           \
    OC_MAKE_STORAGE_TYPE(NS::S, ##__VA_ARGS__)                                \
    OC_MAKE_STRUCT_DESC(NS::S, ##__VA_ARGS__)                                 \
    OC_MAKE_STRUCT_IS_DYNAMIC(NS::S, ##__VA_ARGS__)                           \
    OC_MAKE_COMPUTABLE_BODY(NS::S, ##__VA_ARGS__)                             \
    OC_MAKE_STRUCT_SOA_VAR(template<typename TBuffer>, NS::S, ##__VA_ARGS__)  \
    OC_MAKE_STRUCT_SOA_VIEW(template<typename TBuffer>, NS::S, ##__VA_ARGS__) \
    OC_STRUCT_ALIAS(NS, S)                                                    \
    OC_MAKE_PROXY(NS::S)

#define OC_STRUCT(NS, S, ...) \
    OC_STRUCT_IMPL(NS, S, ##__VA_ARGS__)

#define OC_BUILTIN_STRUCT(NS, S, ...) \
    OC_MAKE_BUILTIN_STRUCT(NS::S)     \
    OC_STRUCT_IMPL(NS, S, ##__VA_ARGS__)

#define OC_PARAM_STRUCT(NS, S, ...)                 \
    OC_MAKE_PARAM_STRUCT(NS::S)                     \
    OC_MAKE_STRUCT_REFLECTION(NS::S, ##__VA_ARGS__) \
    OC_MAKE_STRUCT_DESC(NS::S, ##__VA_ARGS__)       \
    OC_MAKE_COMPUTABLE_BODY(NS::S, ##__VA_ARGS__)   \
    OC_STRUCT_ALIAS(NS, S)                          \
    OC_MAKE_PROXY(NS::S)