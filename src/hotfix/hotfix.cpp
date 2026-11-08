//
// Created by Zero on 2024/7/29.
//

#include "hotfix.h"
#include "module_interface.h"
#include "horizon/core/logging.h"

#include <algorithm>

namespace vision ::inline hotfix {
HotfixSystem *HotfixSystem::s_mgr = nullptr;

Observer::Observer() {
    HotfixSystem::instance().register_observer(this);
}

Observer::~Observer() {
    HotfixSystem::instance().deregister_observer(this);
}

void HotfixSystem::register_observer(vision::Observer *observer) noexcept {
    if (std::find(observers_.cbegin(), observers_.cend(), observer) != observers_.cend()) {
        return;
    }
    observers_.push_back(observer);
}

void HotfixSystem::deregister_observer(vision::Observer *observer) noexcept {
    erase_if(observers_, [&](const Observer *iter) -> bool {
        return observer == iter;
    });
}

void HotfixSystem::defer_delete(SP<const Observer> observer) noexcept {
    const_cast<Observer *>(observer.get())->invalidation();
    defer_delete_.push_back(std::move(observer));
}

void HotfixSystem::enqueue_function(std::function<void()> fn) noexcept {
    callbacks_.push(std::move(fn));
}

void HotfixSystem::execute_callback() {
    if (pending_build_.valid() &&
        pending_build_.wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
        const auto &modules = pending_build_.get();
        if (!modules.empty()) {
            on_build_all_finish(modules);
        }
        pending_build_ = {};
    }
    while (!callbacks_.empty()) {
        std::invoke(callbacks_.front());
        callbacks_.pop();
    }
}

void HotfixSystem::on_build_all_finish(const vector<Target> &modules) noexcept {
    Version version;
    for (const auto &module : modules) {
        ModuleInterface *module_interface = module.module_interface();
        if (!module_interface) {
            CFW_LOG_WARNING("Hotfix module {} could not be loaded", module.name);
            return;
        }
        const auto constructors = module_interface->constructors();
        if (constructors.empty()) {
            CFW_LOG_WARNING("Hotfix module {} has no object constructors", module.name);
            return;
        }
        version.targets.emplace_back(true, module);
        version.constructors.insert(version.constructors.end(), constructors.begin(), constructors.end());
    }

    // Resolve the initial constructors before changing objects or history.
    vector<const IObjectConstructor *> initial_constructors;
    for (const auto *constructor : version.constructors) {
        const auto *initial = ModuleInterface::instance().find_constructor(string{constructor->class_name()});
        if (!initial) {
            CFW_LOG_WARNING("Hotfix object {} has no initial constructor", constructor->class_name());
            return;
        }
        initial_constructors.push_back(initial);
    }
    for (auto &ver : versions_) {
        ver.merge_constructors(initial_constructors);
    }

    update_objects(version.constructors);
    for (const auto &module : modules) {
        file_tool_.acknowledge_build(module);
        CFW_LOG_INFO("module {} build success!", module.name);
    }
    versions_.push_back(std::move(version));
    cur_ver_ = versions_.size() - 1;
}

void HotfixSystem::on_version_change() noexcept {
    if (versions_.empty()) {
        return;
    }
    Version version = versions_[cur_ver_];
    update_objects(version.constructors);
}

void HotfixSystem::previous_version() noexcept {
    cur_ver_ = std::clamp(cur_ver_ - 1, 0, int(versions_.size() - 1));
    on_version_change();
}

void HotfixSystem::next_version() noexcept {
    cur_ver_ = std::clamp(cur_ver_ + 1, 0, int(versions_.size() - 1));
    on_version_change();
}

void HotfixSystem::update_objects(const vector<const vision::IObjectConstructor *> &constructors) noexcept {
    for (int i = 0; i < observers_.size(); ++i) {
        Observer *observer = observers_[i];
        if (!observer->valid_) {
            continue;
        }
        observer->notified(constructors);
    }
    defer_delete_.clear();
}

bool HotfixSystem::set_build_backend(std::shared_ptr<::horizon::hotfix::IBuildBackend> backend,
                                     fs::path build_directory, string configuration) {
    if (is_working()) {
        return false;
    }
    return build_system_.set_build_backend(std::move(backend), std::move(build_directory), std::move(configuration));
}

bool HotfixSystem::check_and_build() noexcept {
    if (is_working()) {
        return false;
    }
    auto modules = file_tool_.get_modified_targets();
    if (modules.empty()) {
        return false;
    }
    try {
        pending_build_ = std::async(std::launch::async, [this, modules = std::move(modules)]() mutable -> vector<Target> {
            if (!build_system_.build_targets(modules)) {
                return {};
            }
            return std::move(modules);
        }).share();
    } catch (const std::exception &error) {
        CFW_LOG_WARNING("Hotfix could not start a background build: {}", error.what());
        return false;
    }
    return true;
}

HotfixSystem &HotfixSystem::instance() noexcept {
    if (s_mgr == nullptr) {
        s_mgr = new HotfixSystem();
    }
    return *s_mgr;
}

void HotfixSystem::destroy_instance() noexcept {
    if (s_mgr == nullptr) {
        return;
    }
    delete s_mgr;
    s_mgr = nullptr;
}

HotfixSystem::~HotfixSystem() {
    if (pending_build_.valid()) {
        pending_build_.wait();
    }
}

}// namespace vision::inline hotfix
