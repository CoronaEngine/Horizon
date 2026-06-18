//
// Created by Zero on 2024/8/17.
//

#include "test.h"
#include "hotfix/hotfix.h"

namespace vision ::inline hotfix {
using namespace ocarina;

void Test::fill() noexcept {
    attr_float = 6.66;
    attr_int = 12;
#ifdef HOTFIX_FLAG
    attr_double = 678;
#endif
}


void Test::restore(vision::RuntimeObject *old_obj) noexcept {
//        RuntimeObject::restore(old_obj);
    VS_HOTFIX_MOVE_ATTRS(attr_int, attr_float)
#ifdef HOTFIX_FLAG
    VS_HOTFIX_MOVE_ATTR(attr_double)
#endif
}


void Test::clear() noexcept {
    attr_int = 0;
    attr_float = 0;
#ifdef HOTFIX_FLAG
    attr_double = 0;
#endif
}

void Test::print() const noexcept {
    OC_INFO("test new !!!!!!!!!");
    std::cout << "test print begin" << std::endl;
    std::cout << "      attr_float = " << attr_float << endl;
    std::cout << "      attr_int = " << attr_int << endl;
#ifdef HOTFIX_FLAG
    std::cout << "      attr_double = " << attr_double << endl;
#endif
    std::cout << "test print end" << std::endl;
}


string Test::get_string() const {
    return "Test::get_string";
}

}// namespace vision::inline hotfix

VS_REGISTER_CURRENT_PATH(1, "vision-hotfix-test.dll")

VS_REGISTER_HOTFIX(vision, Test)