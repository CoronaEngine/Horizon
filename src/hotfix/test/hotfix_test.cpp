//
// Created by Zero on 2024/8/30.
//

#include "hotfix_test.h"
#include "hotfix/export.h"
#include "hotfix/hotfix.h"
#include "test.h"
#include "hotfix/module_interface.h"

namespace vision::inline hotfix {
using namespace ocarina;

HotfixTest::HotfixTest() {
    demo->test = make_shared<Test>();
}

void HotfixTest::update_runtime_object(const vision::IObjectConstructor *constructor) noexcept {
    OC_INFO("HotfixTest::update_runtime_object");
    if (constructor->match(test)) {
        auto new_obj = constructor->construct_shared<Test>();
        new_obj->restore(test.get());
        test = new_obj;
    } else if (constructor->match(demo)) {
        auto new_obj = SP<Demo>(constructor->construct_raw<Demo>());

//        HotfixSystem::instance().serializer().erase_old_object(demo.get());
//        HotfixSystem::instance().defer_delete(demo);
        new_obj->restore(demo.get());
        demo = new_obj;
    }
}

}// namespace vision::inline hotfix