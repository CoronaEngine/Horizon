#include "ast/function_requirements.h"
#include "ast/function.h"
#include "core/type_system/type_desc.h"

#include <iostream>

using namespace ocarina;

int main() {
    const auto *accel_type = Type::from("accel");
    const auto *ray_type = Type::from("struct<ocarina::Ray,16,true,false,vector<float,4>,vector<float,4>>");
    const auto callable = Function::define_callable([&] {
        auto *function = Function::current();
        const auto *scene = function->argument(accel_type);
        const auto *ray = function->argument(ray_type);
        function->expr_statement(function->call_builtin(Type::of<bool>(), CallOp::TRACE_OCCLUSION, {scene, ray}));
        function->expr_statement(function->call_builtin(nullptr, CallOp::SYNCHRONIZE_BLOCK, {}));
    });
    const auto kernel = Function::define_kernel([&] {
        auto *function = Function::current();
        const auto *scene = function->argument(accel_type);
        const auto *ray = function->argument(ray_type);
        function->expr_statement(function->call(nullptr, callable, {scene, ray}));
    });

    const auto requirements = analyze_function_requirements(*kernel);
    if (!requirements.uses_trace || !requirements.requires_compute_workgroups) {
        std::cerr << "The kernel did not inherit trace and workgroup requirements from its callable\n";
        return 1;
    }
    if (requirements.reachable_functions.size() != 2 ||
        !requirements.reachable_functions.contains(kernel.get()) ||
        !requirements.reachable_functions.contains(callable.get())) {
        std::cerr << "The entry kernel and its callable must both be reachable\n";
        return 1;
    }
    return 0;
}
