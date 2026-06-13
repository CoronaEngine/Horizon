# Horizon Examples 新 API 可见窗口任务说明

## 适用场景

- 需要恢复或验证 `HorizonExamples` 是否能用新 public API 打开 GLFW 窗口、提交 Vulkan 渲染并 present。
- 目标是重构后的新 API 路线；不要为了跑通示例恢复 `ImageFormat`、`BufferUsage`、`HardwareImageCreateInfo` 等旧 public 名字。
- `examples/example_baseline`、`example_default`、`example_edsl`、`example_glsl` 现在都应编入 `HorizonExamples`；如果 API 断裂，迁移调用点到当前 API，不恢复旧 public API 兼容层。
- 当前第一验收是 `baseline`、`default`、`edsl`、`glsl` 四个显式入口的真实窗口 smoke；`default` 还要覆盖组合 EDSL/GLSL 窗口。texture、compute、multi-window 和 stress 若再次恢复，应作为后续 mode 验证。

## 入口和范围

- public umbrella 是 `include/horizon.h`；旧 `include/Horizon.h`、`horizon_refac.h` 名字只作迁移历史，不要在新示例中重新引入。
- 示例入口是 `examples/main.cpp`，默认运行 `default`，当前显式支持 `baseline`、`default`、`edsl`、`glsl`。
- `examples/CMakeLists.txt` 应同时编入 `common.cpp`、`example_baseline`、`example_default`、`example_edsl`、`example_glsl`，并把 baseline 预编译 SPIR-V 复制到 target 输出目录。
- 不要假设 `--frames`、`--threads`、`texture`、`compute`、`multi-window` 或 `stress` 已存在；先检查 `examples/main.cpp` 的实际 CLI，再补验证命令。
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

## 多窗口压力模式

- `stress` mode 用于验证多 GLFW 窗口、多 mesh 线程、多 render 线程和多个 swapchain present 的并发路径；默认 8 个窗口和 4 个 render 线程，可用 `--windows N`、`--render-threads N` 调整。
- 每个窗口只应由一个 render 线程提交 present，避免多个线程同时操作同一个 swapchain；多个窗口可以共享主设备 present queue，以覆盖 `Queue` 对 submit / present host access 的串行化。
- `stress` 仍是单 render target、最小动态 mesh 的压力 smoke，不代表恢复旧 default 的多窗口组合行为。

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

## 运行时 bug 优先排查

- 示例运行时验证不要只看进程是否仍在运行。MSVC Debug CRT 断言会弹出 `Microsoft Visual C++ Runtime Library` 窗口并让进程停住；smoke 脚本应枚举进程可见窗口标题，并把这个窗口视为失败。
- 从 repo root 和 target 输出目录各启动一次示例。旧 baseline 代码会用 `readFile("shaders/xxx.spv")` 这类相对路径；如果只在输出目录能跑，优先检查 shader post-build copy、working directory 和源码目录 fallback。
- 截图验证要先把目标窗口置前/置顶并等待至少一两帧，记录窗口标题、退出码、stderr 和截图 hash；如果不同 mode 的截图 hash 完全相同，或窗口标题与 mode 不匹配，这批截图不能作为“画面正常”的证据。
- resize / minimize / hide 验证要覆盖 `default`、`edsl`、`glsl`，并对 `default` 的 EDSL / GLSL 两个窗口都执行操作。成功条件包括：没有 early exit、stderr 为空或不含坏模式、没有 CRT 断言窗口、`WM_CLOSE` 后进程退出且不残留 `HorizonExamples`。
- 遇到 `vector subscript out of range`，先检查 CPU 侧容器和循环边界，再进入 Vulkan 调试：cube 常量顶点数应为 36；aggregate 初始化改动后要确认 `vertices.size()` 没有漂移；draw loop 应遍历稳定的对象数量，不要依赖正在被其他线程修改的容器。
- 多线程 default 路径优先查共享 `std::vector` 的 `push_back` / `resize` / 读取竞态。storage buffer 这类 per-window/per-object 容器应在线程启动前定长创建，线程内只更新已有 buffer 内容。
- 多线程 default 的 render 和 display 若共享同一张输出 image，display present 应显式等待最近一次 render 提交；否则 resize/minimize/hide 时容易把 image layout / queue submit / binary semaphore 复用问题误判成单纯窗口问题。
- 遇到只在 AMD 核显或特定驱动上暴露的 `HardwareImage` / `ResourceStore` / `HorizonExamples.exe default` 崩溃、validation 或同步错误时，先按后端 image layout、barrier、swapchain image/semaphore 复用、present 完成点和 queue 串行化排查；除非用户明确要求 shader 方案，不要修改 `examples/shaders/*.glsl` 或用 shader 源码改动绕过后端问题。
- 手写 GLSL 或 EDSL 走当前 pipeline API 时，如果出现 `bindless space index unavailable` 或在 `ComputePipelineDesc::from_source(...)` / `RasterizerPipelineDesc::from_source(...)` 附近断言，不要用关闭 bindless 长期规避；优先检查 shader 反射出的 set/binding 是否与后端 bindless 表约定一致，UBO 和普通 descriptor 应按反射 set/binding 生成。
- 对本任务的 `default` 复现，拿到调用栈后再设置 `HORIZON_VULKAN_DIAGNOSTICS_PATH`，从 `build\ninja-msvc\examples\Debug` 启动 `HorizonExamples.exe default`，等待 10-20 秒后关闭/终止进程，再筛 `horizon-vulkan-diagnostics.txt` 里的 `warning[E39012]`、`ComputePipeline reflection`、`VUID`、`SYNC-HAZARD`、`VK_ERROR`。
- 对 Helicon Slang 迁移后的 default bindless 问题，先区分“反射/布局错误”和“提交/布局同步错误”。若 diagnostics 里 `inputImageRGBA16 type=_Texture set=2 binding=0 bindType=8 elementCount=0`，且不再出现 `VUID-VkComputePipelineCreateInfo-layout-07988` / 缺 `pSetLayouts[2]`，说明 storage image bindless 反射和 pipeline layout 已基本对齐；后续不要继续往 `BindingKey` / `BoundField` / `AutoBindEntry` 加 `set` / `binding`，也不要恢复 `BindlessSpaceIndex` 或用禁用 bindless 当最终修复。
- 若下一批错误变成 `VUID-vkCmdDraw-None-09600`，并且对象名是 `example_default.output`，优先查 descriptor 写入时的 `VkDescriptorImageInfo::imageLayout` 与命令执行时 image 实际 layout 是否一致。重点文件是 `src/hardware_wrapper_vulkan/hardware/resource_manager.cpp` 的 `store_sampled_descriptor()` / `store_storage_descriptor()`，`src/hardware_wrapper_vulkan/pipeline/vulkan_compute_pipeline.cpp` / `vulkan_rasterizer_pipeline.cpp` 的 transient descriptor 写入，以及 `src/hardware_wrapper_vulkan/hardware/execution.cpp` 里 `Dispatch`、`BeginRendering`、`DrawIndexed`、`Present` 对同一 image 的 layout transition。
- 如果移除或收窄 `frameSubmitMutexes` 后暴露 `VUID-vkCmdDraw-None-09600` / `VUID-vkCmdBeginRendering-pRenderingInfo-09592`，不要把示例锁加回去掩盖问题；优先确认 `ResourceSubmissionTracker` 的 guard 覆盖 `VulkanCommandEncoder::encode()` 到 `Queue::submit()`，使 `ImageWrap::image_layout` 按实际提交顺序更新。
- 若 diagnostics 同时出现 `SYNC-HAZARD-WRITE-RACING-READ` 或 `SYNC-HAZARD-WRITE-RACING-WRITE`，并指向 `vkCmdBlitImage` 和另一条 command buffer 的 `vkCmdPipelineBarrier2`，根因通常不是 Slang 绑定字段，而是 render/display submit 落在不同 `VkQueue` 时，同一输出 image 的 layout/read/write 没有被同一队列顺序或显式 GPU 依赖串住。优先把修复落在 executor/backend 的队列选择、资源依赖或 receipt 传递语义上；不要在 example 里改成 render 等 display 完成，也不要为了 default 引入三张图 frame-slot 调度。
- 当前 default 的 render/display 分离语义应理解为：render 负责渲染一张离屏输出图；display 在提交 present 前等待最近 render receipt，消费这张图，并负责把图像 blit/copy 到 swapchain。render 不应为了复用 `finalOutputImages[i]` 等待 display receipt；示例保持线程分离、一张输出图和可插入 DLSS 类处理的位置，同时注意输出 image 的 format / usage 必须满足 render target、storage/compute 和 display copy/present 路径。
- `frameSubmitMutexes` 只应用来保护 `latestRenderReceipts` 的读写；不要在持有该锁时执行 render submit、display present、`HardwareExecutor::wait()` 或任何 queue/device wait。
- 修复后至少覆盖 `baseline`、`default`、`edsl`、`glsl` 四个显式入口；`default` 要同时确认 EDSL / GLSL 两个窗口存在且没有 CRT 断言窗口。
- 多线程 `default` 修复后要做有边界的重复验证：至少连续多轮 16 窗口 10-20 秒启动/关闭 smoke，并做一次更长运行；成功条件是 WM_CLOSE 后自然退出、无强杀、无 CRT 窗口，diagnostics 中无 `VUID`、`SYNC-HAZARD`、`VK_ERROR`。

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
build\ninja-msvc\examples\Debug\HorizonExamples.exe baseline
build\ninja-msvc\examples\Debug\HorizonExamples.exe default
build\ninja-msvc\examples\Debug\HorizonExamples.exe edsl
build\ninja-msvc\examples\Debug\HorizonExamples.exe glsl

git diff --check
```

`HorizonTests` 的 CTest 属性应有合理 `TIMEOUT`；真实 Vulkan 不可用时测试应 skip 或失败返回，不应无限挂起。
