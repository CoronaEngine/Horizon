//
// Created by Zero on 2024/8/30.
//

#pragma once

#include "demo.h"
#include "hotfix/module_interface.h"

namespace vision::inline hotfix {

class HotfixTest : public Observer {
public:
    SP<Demo> demo{ModuleInterface::instance().construct_shared<Demo>()};
    SP<Test> test{ModuleInterface::instance().construct_shared<Test>()};

public:
    HotfixTest();
    void update_runtime_object(const IObjectConstructor *constructor) noexcept override;
};
}// namespace vision::inline hoftix