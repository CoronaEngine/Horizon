# Horizon Examples 新 API 可见窗口任务说明

## 适用场景

- 需要恢复或验证 `HorizonExamples` 是否能用新 public API 打开 GLFW 窗口、提交 Vulkan 渲染并 present。
- 目标是重构后的新 API 路线；不要为了跑通示例恢复 `ImageFormat`、`BufferUsage`、`HardwareImageCreateInfo` 等旧 public 名字。
- 旧 `examples/example_baseline`、`example_glsl`、`example_edsl` 可以作为参考；当前可运行示例统一迁移到 `examples/example_default/` 的新 API mode，不重新编入旧示例 target。
- 第一阶段验收是可见窗口和 present smoke；第二阶段是 `mesh/render/display` 三线程 default smoke；第三迁移批次恢复 texture、compute、EDSL、GLSL 和 multi-window 的最小 smoke mode。

## 入口和范围

- public umbrella 是 `include/Horizon.h`，应导出 `format.h`、`resource.h`、`horizon_refac.h` 和新执行/显示 facade。
- 示例入口是 `examples/main.cpp`，默认运行 `default`，应支持 `--frames N` 自动退出。
- 当前可运行示例放在 `examples/example_default/`，通过 `default`、`glsl`、`edsl`、`texture`、`compute`、`multi-window` 这些 mode 覆盖新 API smoke。
- 三线程第二阶段通过 `default --threads mesh-render-display` 显式启用；`--threads` 仅用于 default mode，其他 mode 保持单线程 smoke。
- 实现范围优先放在 `src/hardware_wrapper` 和 `src/hardware_wrapper_vulkan`；`src/HardwareWrapper` 和 `src/HardwareWrapperVulkan` 只作历史参考。

## 三线程第二阶段

- 第二阶段仍只做单窗口、单 render target、最小 mesh；texture、compute、EDSL、GLSL 和多窗口作为独立 smoke mode 迁移，不混入三线程 default 管线。
- `mesh` 线程负责 CPU mesh 数据生成或更新，产出不可变帧快照或上传请求，不共享可变 mesh 容器。
- `render` 线程消费 mesh 快照，录制/提交 `executor.stream() << ... << present(displayer, image) << commit()`。
- `display` 线程负责 GLFW 窗口生命周期、事件轮询、退出信号和 `HardwareDisplayer` 创建；主线程只启动、join 和汇总错误。
- `present(displayer, image)` 仍是 execution graph node，不要把 present 改成 display 线程里的 commit 后 side step。
- `HardwareBuffer`、`HardwareImage`、pipeline wrapper 跨线程只复制 handle；不要跨线程裸改 backend 可变状态。

## 第三批 smoke mode

- `glsl` 使用内联 GLSL fullscreen triangle，验证新 `RasterizerPipeline` 的 GLSL 编译和 present。
- `edsl` 使用最小 EDSL indexed triangle，验证 EDSL codegen、reflection 和新 rasterizer facade。
- `texture` 使用 CPU checker 数据上传到 image 后 present，验证 buffer-to-image copy、image layout transition 和 present。
- `compute` 使用最小 GLSL compute shader 写 storage image 后 present，验证 `ComputePipeline::command_batch()`、storage image descriptor 和 dispatch encoder。
- `multi-window` 使用两个 GLFW Win32 窗口和两个 render target，验证 lower-case display/swapchain 可创建并 present 多个窗口。
- 这些 mode 仍是 smoke，不恢复旧 default 的 texture、compute、EDSL、GLSL、多窗口组合行为，也不恢复旧 public API 兼容层。

## Present 路径

- `present(displayer, image)` 是 execution graph 中的命令节点，不是 `commit()` 后的额外 side step。
- lower-case `DisplayManager` 负责 Win32 surface、swapchain acquire、等待 render submit token、copy/blit 离屏 image 到 swapchain image、signal binary semaphore、`vkQueuePresentKHR`。
- 第一阶段只要求主设备 present；如果主设备 queue 不支持 surface present，应返回清晰错误，不要静默走跨设备或 CPU bridge。
- swapchain `OUT_OF_DATE`、`SUBOPTIMAL` 和 skipped/presented 状态应通过 `SubmitReceipt` / present result 返回。

## 资源生命周期坑

- `RasterizerPipeline` 记录 draw 时不要让内部 `RecordedDraw` 强持自己的 public `ResourceHandle`；这会形成 pipeline token -> impl -> draw -> pipeline token 的自引用环。
- 需要把 pipeline 引用做成弱 token，生成 `command_batch()` 时再临时 lock 并填入 `DrawIndexedDesc::pipeline`。
- 如果 VMA 在测试结束时报 `Some allocations were not freed`，优先检查 pipeline、command batch、queue in-flight keep-alive 是否仍强持 buffer/image token。
- 改动 public header 中私有类布局后，如果 MSVC/Ninja 增量构建出现明显不合理的崩溃，先做一次 clean rebuild 排除旧目标文件 ABI 布局不一致。

## 验证

```powershell
cmake --preset ninja-msvc -DHORIZON_BUILD_EXAMPLES=ON -DHORIZON_BUILD_TESTS=ON
cmake --build --preset msvc-debug --target Horizon
cmake --build --preset msvc-debug --target HorizonExamples
cmake --build --preset msvc-debug --target HorizonTests

build\ninja-msvc\tests\Debug\HorizonTests.exe hardware_context.lazy_construction
build\ninja-msvc\tests\Debug\HorizonTests.exe execution.stream_facade_commit
build\ninja-msvc\tests\Debug\HorizonTests.exe execution.rasterizer_pipeline_ir
build\ninja-msvc\tests\Debug\HorizonTests.exe execution.present_receipt
build\ninja-msvc\tests\Debug\HorizonTests.exe execution.mesh_render_display_threads
build\ninja-msvc\tests\Debug\HorizonTests.exe execution.rasterizer_pipeline_real_vulkan_render
build\ninja-msvc\examples\Debug\HorizonExamples.exe --frames 3
build\ninja-msvc\examples\Debug\HorizonExamples.exe default --threads mesh-render-display --frames 3
build\ninja-msvc\examples\Debug\HorizonExamples.exe glsl --frames 3
build\ninja-msvc\examples\Debug\HorizonExamples.exe edsl --frames 3
build\ninja-msvc\examples\Debug\HorizonExamples.exe texture --frames 3
build\ninja-msvc\examples\Debug\HorizonExamples.exe compute --frames 3
build\ninja-msvc\examples\Debug\HorizonExamples.exe multi-window --frames 3

git diff --check
```

`HorizonTests` 的 CTest 属性应有合理 `TIMEOUT`；真实 Vulkan 不可用时测试应 skip 或失败返回，不应无限挂起。
