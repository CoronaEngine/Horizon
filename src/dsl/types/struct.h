//
// Created by Zero on 15/05/2022.
//

#pragma once

#include "ast/type_desc.h"
#include "../core/var.h"
#include "soa.h"

#define HORIZON_DSL_IS_DYNAMIC_SIZE(member, S) \
    horizon::math::is_dynamic_size<std::remove_cvref_t<decltype(S::member)>>

#define HORIZON_DSL_MAKE_STRUCT_IS_DYNAMIC(S, ...) \
    template<> \
    struct horizon::math::is_dynamic_size<S> : std::disjunction<MAP_LIST_UD(HORIZON_DSL_IS_DYNAMIC_SIZE, S, ##__VA_ARGS__)> {};

#define OC_STRUCT_ALIAS(NS, S)      \
    namespace NS {                  \
    using S##Var = horizon::dsl::Var<S>; \
    }

#define OC_STRUCT_IMPL(NS, S, ...)                                            \
    OC_MAKE_STRUCT_REFLECTION(NS::S, ##__VA_ARGS__)                           \
    OC_MAKE_STORAGE_TYPE(NS::S, ##__VA_ARGS__)                                \
    OC_MAKE_STRUCT_DESC(NS::S, ##__VA_ARGS__)                                 \
    HORIZON_DSL_MAKE_STRUCT_IS_DYNAMIC(NS::S, ##__VA_ARGS__)                  \
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
