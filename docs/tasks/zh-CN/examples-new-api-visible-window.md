# Horizon Examples 新 API 可见窗口任务说明

## 适用场景

- 需要恢复或验证 `HorizonExamples` 是否能用新 public API 打开 GLFW 窗口、提交 Vulkan 渲染并 present。
- 目标是重构后的新 API 路线；不要为了跑通示例恢复 `ImageFormat`、`BufferUsage`、`HardwareImageCreateInfo` 等旧 public 名字。
- 旧 `examples/example_baseline`、`example_glsl`、`example_edsl` 可以作为参考，但默认不要编入当前第一阶段验收。
- 第一阶段验收只是可见窗口和 present smoke；texture、compute、EDSL、GLSL、多窗口旧 default 行为都放到后续迁移批次。

## 入口和范围

- public umbrella 是 `include/Horizon.h`，应导出 `format.h`、`resource.h`、`horizon_refac.h` 和新执行/显示 facade。
- 示例入口是 `examples/main.cpp`，默认运行 `default`，应支持 `--frames N` 自动退出。
- 当前可运行示例放在 `examples/example_default/`，保持单窗口、单离屏 color target、最小图元或 fullscreen triangle。
- 三线程第二阶段通过 `--threads mesh-render-display` 显式启用；默认仍保留第一阶段单线程 smoke 路径。
- 实现范围优先放在 `src/hardware_wrapper` 和 `src/hardware_wrapper_vulkan`；`src/HardwareWrapper` 和 `src/HardwareWrapperVulkan` 只作历史参考。

## 三线程第二阶段

- `mesh` 线程负责 CPU mesh 数据生成或更新，产出不可变帧快照或上传请求，不共享可变 mesh 容器。
- `render` 线程消费 mesh 快照，录制/提交 `executor.stream() << ... << present(displayer, image) << commit()`。
- `display` 线程负责 GLFW 窗口生命周期、事件轮询、退出信号和 `HardwareDisplayer` 创建；主线程只启动、join 和汇总错误。
- `present(displayer, image)` 仍是 execution graph node，不要把 present 改成 display 线程里的 commit 后 side step。
- `HardwareBuffer`、`HardwareImage`、pipeline wrapper 跨线程只复制 handle；不要跨线程裸改 backend 可变状态。

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

git diff --check
```

`HorizonTests` 的 CTest 属性应有合理 `TIMEOUT`；真实 Vulkan 不可用时测试应 skip 或失败返回，不应无限挂起。
