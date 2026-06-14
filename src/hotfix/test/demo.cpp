//
// Created by Zero on 2024/8/21.
//

#include "demo.h"
#include "hotfix/export.h"
#include "hotfix/hotfix.h"
#include "hotfix/module_interface.h"
#include <random>

namespace vision::inline hotfix {
using namespace ocarina;

Demo::Demo()
    : test() {
}

Demo::~Demo() {
    std::cout << "del Demo" << endl;
}

void Demo::update_runtime_object(const vision::IObjectConstructor *constructor) noexcept {
    OC_INFO("Demo::update_runtime_object");
    if (constructor->class_name() != test->class_name()) {
        return;
    }
    auto new_obj = constructor->construct_shared<Test>();
    new_obj->restore(test.get());
    test = new_obj;
    for (int i = 0; i < tests.size(); ++i) {
        auto new_test = constructor->construct_shared<Test>();
        new_test->restore(tests[i].get());
        tests[i] = new_test;
    }
}


void Demo::restore(vision::RuntimeObject *old_obj) noexcept {
//    RuntimeObject::restore(old_obj);
    VS_HOTFIX_MOVE_ATTRS(attr_int, attr_float, test, tests)
}

void Demo::clear() noexcept {
    test->clear();
    for (const auto &item : tests) {
        item->clear();
    }
    attr_int = 0;
    attr_float = 0;
}

void Demo::print() noexcept {
    OC_INFO("Demo new !!!!!!!!!");
    std::cout << "demo print begin" << std::endl;
    std::cout << "      attr_float = " << attr_float << endl;
    std::cout << "      attr_int = " << attr_int << endl;
    test->print();
    for (const auto &item : tests) {
        item->print();
    }
    std::cout << "demo print end" << std::endl;
}

void Demo::fill() noexcept {
    test->fill();
    attr_float = 57.9;
    attr_int = 789;

    std::random_device rd; 
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> distrib_real(0.0, 100.0);
    for (int i = 0; i < 1; ++i) {
        auto test = ModuleInterface::instance().construct_shared<Test>();
        test->attr_float = distrib_real(gen);
        test->attr_int = distrib_real(gen);
        test->attr_double = distrib_real(gen);
        tests.push_back(test);
    }
}

string Demo::get_string() const {
    return "Demo::string";
}

}// namespace vision::inline hotfix

VS_REGISTER_HOTFIX(vision::hotfix, Demo)