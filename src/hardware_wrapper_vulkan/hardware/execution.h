#pragma once

#include "horizon.h"
#include "execution_profile.h"

// 内部编译/提交层的类型（SubmissionSync / CompiledSubmission / ExecutionPlan /
// ExecutionCompiler / VulkanCommandEncoder 等）已随 Command IR 一起移入
// command_ir.h。本头现在只是 command_ir.h 之外、面向"提交执行"入口的薄壳，
// 保留它主要是为了让 device_manager.h / display_manager.h 等只依赖提交入口的
// 文件不必直接 include command_ir.h 全量 IR。

#include "hardware_wrapper_vulkan/hardware/command_ir.h"
