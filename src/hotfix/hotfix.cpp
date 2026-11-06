//
// Created by Zero on 2024/7/29.
//

#include "hotfix.h"
#include "core/util/logging.h"
#include "core/concurrency/thread_pool.h"

namespace vision ::inline hotfix {
using namespace ocarina;

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
    defer_delete_.push_back(ocarina::move(observer));
}

void HotfixSystem::enqueue_function(std::function<void()> fn) noexcept {
    callbacks_.push(ocarina::move(fn));
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

void HotfixSystem::load_module(vision::Target target) noexcept {
    ModuleInterface *module_interface = target.module_interface();
    vector<const IObjectConstructor *> constructors = module_interface->constructors(target.modified_files);
    update_objects(constructors);
}

void HotfixSystem::on_build_finish(bool success, const Target &target) noexcept {

    if (!success) {
        OC_INFO_FORMAT("module {} build failure!", target.name);
        return;
    }
    OC_INFO_FORMAT("module {} build success!", target.name);

    load_module(target);
}

void HotfixSystem::on_build_all_finish(const vector<Target> &modules) noexcept {
    Version version;
    for (const auto &module : modules) {
        version.targets.emplace_back(true, module);
        on_build_finish(true, module);
        ModuleInterface *module_interface = module.module_interface();
        auto tmp = module_interface->constructors(module.modified_files);
        version.constructors.insert(version.constructors.cend(), tmp.cbegin(), tmp.cend());
        file_tool_.acknowledge_build(module);
    }

    for (auto &ver : versions_) {
        for (const IObjectConstructor *item : version.constructors) {
            ver.merge_constructor(ModuleInterface::instance().constructor(item->class_name().data()));
        }
    }

    versions_.push_back(version);
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
    cur_ver_ = ocarina::clamp(cur_ver_ - 1, 0, int(versions_.size() - 1));
    on_version_change();
}

void HotfixSystem::next_version() noexcept {
    cur_ver_ = ocarina::clamp(cur_ver_ + 1, 0, int(versions_.size() - 1));
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

bool HotfixSystem::check_and_build() noexcept {
    if (is_working()) {
        return false;
    }
    auto modules = file_tool_.get_modified_targets();
    if (modules.empty()) {
        return false;
    }
    pending_build_ = ThreadPool::instance().async([this, modules = std::move(modules)]() mutable -> vector<Target> {
        if (!build_system_.build_targets(modules)) {
            return {};
        }
        return std::move(modules);
    });
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