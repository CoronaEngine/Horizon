#include "fixture_object.h"

#include "hotfix/module_interface.h"

#ifndef HORIZON_HOTFIX_TEST_GENERATION
#define HORIZON_HOTFIX_TEST_GENERATION 1
#endif

namespace horizon::hotfix_test {

int FixtureImplementation::generation() const noexcept {
    return HORIZON_HOTFIX_TEST_GENERATION;
}

}// namespace horizon::hotfix_test

VS_REGISTER_HOTFIX(horizon::hotfix_test, FixtureImplementation)
