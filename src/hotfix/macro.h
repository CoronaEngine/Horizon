//
// Created by Zero on 2024/8/27.
//

#pragma once

#include "core/stl.h"

namespace vision ::inline hotfix {
template<typename T>
[[nodiscard]] auto type_string() {
    return typeid(std::remove_cvref_t<T>).name();
}
}// namespace vision::inline hotfix

#define VS_REGISTER_PATH(path, level, ...)                                        \
    namespace {                                                                   \
    struct FileRegistrar {                                                        \
        FileRegistrar() {                                                         \
            auto key = ocarina::parent_path(path, level);                         \
            vision::HotfixSystem::instance().register_target(key, ##__VA_ARGS__); \
        }                                                                         \
    };                                                                            \
    static FileRegistrar registrar;                                               \
    }

#define VS_REGISTER_CURRENT_PATH(level, ...) VS_REGISTER_PATH(__FILE__, level, ##__VA_ARGS__)

#define VS_REGISTER_CURRENT_FILE VS_REGISTER_CURRENT_PATH(0, "vision-" VISION_CATEGORY "-" VISION_PLUGIN_NAME ".dll")

#define VS_REGISTER_DIRECTORY(level) VS_REGISTER_CURRENT_PATH(level, "vision-" VISION_CATEGORY "-" VISION_PLUGIN_NAME ".dll")

#define VS_REGISTER_HOTFIX(NS, Class)                                                  \
    namespace {                                                                        \
    struct Class##ConstructorRegistrar {                                               \
        Class##ConstructorRegistrar() {                                                \
            using namespace vision::hotfix;                                            \
            auto value = ocarina::make_unique<ObjectConstructor<NS::Class>>(__FILE__); \
            ModuleInterface::instance().add_constructor(std::move(value));             \
        }                                                                              \
    };                                                                                 \
    static Class##ConstructorRegistrar s_##Class##_registrar;                          \
    }// namespace