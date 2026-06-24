# origin/merge 合入 main 审查计划

## 目标

把 `origin/merge` 分支中确实有价值的关键修复吸收到当前 `main`，同时保持 `main` 已经大改后的 API、CMake、Vulkan 后端和验证路径不被旧代码回退。

这个文档供其他 AI 或人工审查者继续审判、讨论和补充风险点。它不是合并提交记录，也不是要求直接执行 `git merge origin/merge`。

## 当前事实

- 当前工作分支：`main`。
- 当前工作区：写入本文档前为干净状态。
- 差异计数：`git rev-list --left-right --count main...origin/merge` 输出 `347 9`，说明 `origin/merge` 相对 `main` 落后很多，只超前 9 个提交。
- merge-base：`9a72a4691f653ce03b38264c5615c3b780f674f0`。
- 预测合并命令：`git merge-tree --write-tree main origin/merge`（需 Git ≥ 2.38）。
- 预测合并结果：会冲突，不能直接无脑合并。
- 本仓库 `git config core.ignorecase` = `true`，且工作平台为 Windows（大小写不敏感文件系统）。
- `git log --oneline main..origin/merge` 实际 9 个提交：`930f73a`(merge) / `f4ffc01`(destroyImage GPU sync) / `2afbb93`(invalid/missing queues before submit) / `67258a6`(添加遗留头文件) / `d947b71`(迁移 vision hotfix) / `4199435`(fix fix) / `6a5b93a`(修改导出接口) / `ba7cea1`(Update DisplayManager) / `dafbb54`(修复窗口缩放假死)。

## 大小写文件系统前提（必须先读）

`include/Horizon.h`（大写）与 `include/horizon.h`（小写）在本仓库 **是磁盘上的同一个文件**，不是两个可并存的入口。原因：`core.ignorecase=true` + Windows。

影响：

- `merge-tree` 报出的 `include/Horizon.h` modify/delete 冲突，根因就是这次大小写重命名，而不是"两个文件各自演进"。
- 工作树无法同时持有 `Horizon.h` 和 `horizon.h`。下文凡是"保留 horizon.h、不恢复 Horizon.h"的措辞，应理解为"保持该文件的小写命名与新内容，不要被旧提交改回大写或旧布局"。
- 真实可移植性 bug：代码中任何 `#include <Horizon.h>` / `#include "Horizon.h"`（大写）在本机能编过，但迁到 Linux/macOS（大小写敏感）会编译失败。下游 CoronaEngine 的兼容桥问题本质卡在这里——优先让下游改成小写 include，而不是加大写 shim。

## 直接 merge 的冲突点

`git merge-tree --write-tree main origin/merge` 暴露的冲突：

- `CMakeLists.txt`：content conflict。
- `include/Horizon.h`：main 已删除该旧入口，`origin/merge` 仍修改该文件，属于 modify/delete conflict。
- `src/hotfix/CMakeLists.txt`：add/add conflict。

这些冲突都位于高风险边界：根构建入口、公共 API 入口、hotfix 子构建。因此合入策略必须是人工/AI 审查后移植关键修复意图，而不是冲突中选择某一侧。

## 总体结论

不要全量合并 `origin/merge`。

安全策略：

- 保持 `main` 的新 API：`include/horizon.h`、`include/format.h`、`include/horizon_execution.h`。
- 不恢复旧公共入口 `include/Horizon.h`。
- 保持当前 CMake 选项、FetchContent、tools/examples/tests/hotfix 结构。
- 不把 `src/HardwareWrapperVulkan/` 的旧后端实现原样迁回当前编译后端。
- 只把确认为关键 bug fix 的意图，移植到当前 `src/hardware_wrapper_vulkan/`、`modules/ocarina/` 等现行路径。

## 建议合入的关键修复

### 1. CUDA exported memory free

来源意图：

- `modules/ocarina/backends/cuda/cuda_device.cpp`
- `origin/merge` 在 `CUDADevice::memory_free()` 中，erase `exported_resources` 之前取出 `alloc_handle` 和 `size`。

建议：

- 合入这个小修复。
- 目的：释放 exported CUDA allocation 时使用真实 `CUmemGenericAllocationHandle` 和 aligned size，避免用默认的 0 handle / 0 size 做 unmap/release。

验收重点：

- `memory_free()` 中 `alloc_handle = iter->second.handle;`
- `memory_free()` 中 `size = iter->second.size;`
- 赋值发生在 `exported_resources.erase(iter);` 之前。

### 2. swapchain `VK_SUBOPTIMAL_KHR` 重建

来源意图：

- `origin/merge` 的旧 `DisplayManager` 在 present 返回 `VK_ERROR_OUT_OF_DATE_KHR` 或 `VK_SUBOPTIMAL_KHR` 时触发 swapchain recreation。

当前 main 状态：

- 当前 main 的 `src/hardware_wrapper_vulkan/display/display_manager.cpp` 已有 `prepare_present()` / `present()` 分离结构。
- 当前 main 对 `VK_ERROR_OUT_OF_DATE_KHR` 会销毁 swapchain。
- 当前 main 对 `VK_SUBOPTIMAL_KHR` 仍按可继续 present 处理。

建议：

- 在当前 main 的 `DisplayManager` 中适配该意图。
- **acquire 与 present 两条路径都要处理**，否则行为不一致：
  - present 侧（`display_manager.cpp:954` 附近）：`vkQueuePresentKHR` 返回 `VK_SUBOPTIMAL_KHR` 时，走与 `VK_ERROR_OUT_OF_DATE_KHR` **完全相同**的 retire/destroy 路径让下一帧重建，不要新写一段 ad-hoc destroy（见验收重点的 validation 注意）。
  - acquire 侧（`display_manager.cpp:765` 附近）：当前 main 把 `VK_SUBOPTIMAL_KHR` 和 `VK_SUCCESS` 一起当成功直接返回（吞掉）。需决定是否在此也标记重建，使两侧一致。只改 present 侧会导致 resize 恢复时仍卡在旧 swapchain。
- 不立即在 present 收到 SUBOPTIMAL 后裸调 `destroy_swapchain()`：此刻 present 的 wait semaphore / image 可能仍在使用，可能触发 VUID。必须复用 `OUT_OF_DATE` 已有的 idle/retire 路径。
- 保留 main 当前的 pending frame、present token、nonblocking acquire、native window drawable 检查。

验收重点：

- 不恢复旧 `DisplayManager` 类。
- 不改回旧 `displayFrame()` 单体流程。
- resize 后程序不假死，窗口恢复后能继续 present。
- acquire 与 present 两侧对 SUBOPTIMAL 的处理一致。
- destroy 走的是与 OUT_OF_DATE 相同的资源 retire 路径，无新增 VUID。

> 注意验证可达性：见「验证方案」中对 SUBOPTIMAL 分支无法被普通 resize 稳定触发的说明。在能确定性触发该分支前，不得把修复 #2 标为"已通过"。

### 3. color attachment blend capability 检查

来源意图：

- `origin/merge` 在旧 `RasterizerPipelineVulkan::createGraphicsPipeline()` 中，按 render target format 查询 `VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT`，不支持 blend 的格式禁用 blend。

当前 main 状态：

- 当前 main 的 `src/hardware_wrapper_vulkan/pipeline/vulkan_rasterizer_pipeline.cpp` 根据 `BlendAttachmentDesc` 设置 `blendEnable`。
- 默认 `BlendStateDesc` 是 alpha blend。
- Integer render target 或不支持 blend 的格式若启用 blend，可能触发 Vulkan validation/runtime 错误。

建议：

- 在当前 main 的 pipeline 创建逻辑中保留用户的 blend desc，但创建 Vulkan attachment state 前检查 `key.color_format` 是否支持 `VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT`。
- 若不支持，则强制 `blendEnable = VK_FALSE`，其他 blend factor/op 字段可保持原值。
- 强制关闭 blend 时输出一条 warning 日志（含 format 名），否则用户期望的混合被静默吞掉会变成隐蔽视觉 bug。
- 只处理当前 main 已支持的单 color attachment pipeline key，不引入旧后端的 framebuffer/render pass 缓存模型。

验收重点：

- `RGBA32_UINT`、`R32_UINT` 等 integer render target 不再因为默认 alpha blend 触发错误。
- `RGBA8_UNORM`、`RGBA16_FLOAT` 等支持 blend 的格式仍可按 desc 启用 blend。

### 4. submit 前的无效/缺失队列校验（`2afbb93`，计划原文遗漏）

来源意图：

- `origin/merge` 的 `2afbb93 fix: Handle invalid/missing queues before submit`。
- 改动 `HardwareExecutorVulkan.cpp`（+38 行）与旧 `DisplayManager.cpp`（+4 行），在 submit 前对无效/缺失队列做防御性校验。

当前 main 状态：

- 改动落在旧后端 `src/HardwareWrapperVulkan/*`，不能原样合入。
- 需确认当前 `src/hardware_wrapper_vulkan/` 的 executor 提交路径是否已有等价的队列有效性检查。

建议：

- 评估其意图，只把"提交前校验队列句柄有效、缺失时安全跳过而非崩溃"的最小逻辑移植到当前 executor。
- 这是防御性修复，优先级低于 #1/#2/#3，但不应像原文那样完全不提。

### 5. destroyImage 空指针早返回（从 `f4ffc01` 单独摘出）

`f4ffc01` 整体的全队列 timeline 等待**不合入**（见下文「destroyImage 旧同步代码」）。但同一提交里有一处独立、干净的安全修复值得单独 cherry-pick：

- 早返回条件从 `vmaAllocator == VK_NULL_HANDLE` 扩展为 `vmaAllocator == VK_NULL_HANDLE || image.imageHandle == VK_NULL_HANDLE`。
- 与"全局 stall"无关，纯粹避免对空 image 句柄做销毁。
- 若当前 main 的 `destroy*` 路径尚无此守卫，可移植；已有则跳过。

## 暂不合入或禁止合入的内容

### 旧公共 API

不要恢复：

- `include/Horizon.h`
- `include/HardwareCommands.h`
- 任何把当前新 API 回退到旧命名/旧布局的改动。

原因：

- 当前 `main` 的公共入口已经迁到 `include/horizon.h`。
- `include/horizon_execution.h` 是执行 IR / advanced 层，普通用户不直接包含。
- 恢复旧入口会重新引入 API 重复、认知负担和编译冲突。

### 旧 Vulkan 后端

不要原样合入：

- `src/HardwareWrapperVulkan/*`
- `src/HardwareWrapper/*`

原因：

- 当前 CMake 编译的是 `src/hardware_wrapper_vulkan/*` 小写路径的新后端。
- `origin/merge` 的多数 Vulkan 改动落在旧后端，直接合入不会修复当前运行路径，反而可能造成双后端混乱。

### 根 CMake 回退

不要使用 `origin/merge` 的根 `CMakeLists.txt` 覆盖当前 main。

原因：

- `origin/merge` 的根 CMake 会删除或回退 main 里的多个选项和结构，例如 tools/tests/benchmarks/dependency install/validation/hardcode shader 等。
- 当前 main 的 hotfix/ocarina 接线更接近现有仓库规则。

### hotfix CMake 覆盖

不要直接把 `origin/merge` 的 `src/hotfix/CMakeLists.txt` 覆盖 main。

原因：

- main 已有 `src/hotfix`。
- main 当前版本包含额外 include/link 处理，例如 `ocarina-include` 接线。
- 若要改 hotfix，应逐项审查目标类型和 link 传播，不做整体替换。

### destroyImage 旧同步代码

不要机械移植旧 `ResourceManager::destroyImage()` 中的全队列 timeline 等待逻辑。

原因：

- 当前 main 已有 `SubmissionKeepAlive`、`TrackedCommandBuffer`、`ResourceSubmissionTracker` 和 `wait_idle(last_receipt())` 等新资源生命周期机制。
- 旧代码的全队列等待可能遮蔽真正的生命周期缺口，也可能引入不必要的全局 stall。

建议：

- 先用验证确认当前 keep-alive 是否已经保证 in-flight image 不会早销毁。
- 如果仍有漏洞，应在当前 main 的资源追踪模型内补最小等待或 retire 逻辑，而不是恢复旧全局队列方案。

## 建议执行顺序

1. 保持工作区干净，创建一个专门分支，例如 `codex/review-origin-merge-key-fixes`。
2. 不运行 `git merge origin/merge`。
3. 先移植 `CUDADevice::memory_free()` 的两行修复（#1）。
4. 再移植 `DisplayManager` 对 `VK_SUBOPTIMAL_KHR` 的处理（#2，acquire + present 两侧）。
5. 再移植 rasterizer pipeline 的 blend capability 检查（#3，含 warning 日志）。
6. 评估并按需移植 submit 前队列校验（#4）与 destroyImage 空指针守卫（#5）；若 main 已覆盖则记录"已覆盖+代码位置"。
7. 每个修复完成后单独看 diff，确认没有触碰旧后端或旧公共 API。
8. 最后一次性运行构建和 runtime 验证。
9. 收尾：吸收完成后，为避免 `origin/merge` 永久 9 ahead、这 3 个冲突在未来每次 merge 反复出现，记一个"已处理"标记——例如在专门分支上 `git merge -s ours origin/merge`（仅记录已合，不引入旧代码），或经团队确认后删除 `origin/merge`。本步骤需人工拍板，不要自动执行。

## 验证方案

### 静态检查

```powershell
git status --short --branch
git diff --check
# 注意 -i：本仓库 core.ignorecase=true，include/Horizon.h 与 include/horizon.h
# 是同一文件，大小写敏感的 rg 反而会漏掉真正危险的大写 include。
rg -ni "include/Horizon\.h|#include\s+[<\"]Horizon\.h[>\"]|HardwareCommands\.h|waitForDeferredResources|cleanupDeferredResources|setDepthImage" include src examples modules
git diff --name-status
```

期望：

- 没有冲突标记。
- 没有旧 API 残留被重新引入（含 `HardwareCommands.h`、大写 `Horizon.h`、双引号/尖括号两种 include 形式）。
- 没有修改 `src/HardwareWrapperVulkan/*` 或旧 `include/Horizon.h`。
- 最终 diff 只包含计划内文件。

### 构建验证

普通 PowerShell 可能没有 MSVC 环境，建议使用 VS 2026 Developer Command Prompt 包裹：

```powershell
cmd.exe /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset msvc-debug --target Horizon'
cmd.exe /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset msvc-debug --target HorizonExamples'
```

当前基线已验证：

- `Horizon` 构建通过。
- `HorizonExamples` 构建通过。

### runtime 验证

构建后运行：

```powershell
.\build\ninja-msvc\examples\Debug\HorizonExamples.exe glsl
.\build\ninja-msvc\examples\Debug\HorizonExamples.exe edsl
.\build\ninja-msvc\examples\Debug\HorizonExamples.exe default
```

重点观察：

- `glsl`、`edsl`、`default` 均能打开窗口并渲染。
- resize 后不假死。
- present/swapchain recreation 后能继续显示。
- integer 或不支持 blend 的 render target 不触发 blend validation/runtime 错误。

> SUBOPTIMAL 分支可达性警告：`VK_SUBOPTIMAL_KHR` 依赖合成器/旋转/分数缩放，Windows 桌面合成器下普通 resize 多半返回 `VK_ERROR_OUT_OF_DATE_KHR` 而非 SUBOPTIMAL。上面的 resize 步骤大概率**走不到**修复 #2 的 SUBOPTIMAL 分支。要验证 #2，必须强制复现：临时把 acquire/present 的 `VK_SUCCESS` 当作 SUBOPTIMAL 处理跑一遍，或用屏幕旋转/分数 DPI 缩放制造真实 SUBOPTIMAL。在确定性命中该分支前，#2 不得标"已通过"。

### hotfix/ocarina 可选验证

只有当本次实际改动触及 hotfix 或 ocarina 构建接线时才需要：

```powershell
cmake --preset ninja-msvc -DHORIZON_BUILD_OCARINA=ON -DHORIZON_BUILD_VISION_HOTFIX=ON
cmd.exe /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset msvc-debug --target vision-hotfix-all'
```

如果本机 CUDA 或 ocarina 依赖不可用，需要明确记录跳过原因，不要把未验证说成已通过。

## 给审查 AI 的问题清单

请重点审判：

- 是否还有 `origin/merge` 的关键修复意图遗漏？
  - 已知答案：`2afbb93`（submit 前队列校验，见修复 #4）原计划遗漏，已补入。其余 8 个提交中 `67258a6`(遗留头) / `d947b71`(vision hotfix 迁移) / `6a5b93a`(导出接口) 属架构/接口性改动，需按"禁止合入旧 API/旧后端"原则单独裁定，不默认吸收。
- `VK_SUBOPTIMAL_KHR` 处理是否应该立即 destroy swapchain，还是只标记下一帧重建？
  - 倾向：标记/延迟到下一帧 `ensure_swapchain()` 重建，避免 present 资源在用时被销毁触发 VUID；且 acquire 与 present 两侧需一致（见修复 #2）。
- blend capability 检查是否应按 `VkFormatProperties::optimalTilingFeatures` 判断，还是要结合 image tiling/usage 的实际创建方式？
  - 倾向：render target 实际几乎都是 OPTIMAL tiling，用 `optimalTilingFeatures` 即可收口；强制关 blend 时务必输出 warning（见修复 #3）。
- 当前 `SubmissionKeepAlive` 是否足以覆盖 image release while in flight？如果不足，最小修复点在哪里？
- `src/hotfix/CMakeLists.txt` 的 `vision-hotfix-all` 应保持 STATIC anchor，还是改为 INTERFACE 更合适？
- 是否需要把 `origin/merge` 中任何 hotfix 目标改动单独吸收？
- 是否存在下游 CoronaEngine 仍需要 `<Horizon.h>` 的兼容桥？如果需要，应是迁移 shim 还是下游改 include？（结合「大小写文件系统前提」：本机大小写不敏感会掩盖该问题，优先推动下游改小写 include。）

## 合并收尾（避免 origin/merge 永久 9 ahead）

手工移植不会改变 git 的祖先关系：完成后 `origin/merge` 仍会显示超前 9 个提交，这 3 个冲突点会在未来每次 merge 尝试时反复出现，后人会重复审查。

收尾选项（执行前与维护者确认）：

- 吸收完成并验证通过后，用 `git merge -s ours origin/merge` 记一个"已逻辑合入"的标记提交，让 git 不再把这 9 个提交视为待合入；或
- 明确废弃并删除 `origin/merge` 分支；或
- 至少在本文档/提交信息中记录"已逐项吸收的提交哈希"，供后续比对。

### 已执行的收尾决策（2026-06-24）

本次**不**在特性分支 `codex/review-origin-merge-key-fixes` 上执行 `git merge -s ours`，原因：

1. `-s ours` 标记只在该分支以"真实 merge commit"方式进 `main` 时才生效；若 PR 用 squash/rebase 落地，标记会被丢弃。
2. "`origin/merge` 已全部逻辑合入"是对主线的强声明，归属地应是 `main`，由合并本 PR 的人执行，而非预写进在途分支污染 PR diff 与 merge-base。
3. 删除 `origin/merge` 是不可逆的共享操作，应在 PR 落地后由维护者决定。

因此采用非破坏性收尾：在此记录"本分支新提交 ↔ origin/merge 源提交"的映射，供未来合 `main` 的人据此选择 `-s ours` 或删分支。

| 本分支提交 | 修复 | origin/merge 源提交（意图来源）|
|---|---|---|
| `b16f4dd` fix(cuda) | #1 CUDA exported memory free | `4199435`(fix: fix fix) |
| `1fe3a9e` fix(vulkan) | #2 swapchain VK_SUBOPTIMAL_KHR 重建 | `dafbb54`(修复窗口缩放假死) + `ba7cea1`(Update DisplayManager.cpp) |
| `87e4ffb` fix(vulkan) | #3 blend capability 检查 | `4199435`(fix: fix fix，含 COLOR_ATTACHMENT_BLEND_BIT，merge 侧独有) |

origin/merge 其余 6 个提交的处置：

- `2afbb93`(invalid/missing queues before submit)：意图已被 main 新后端覆盖（`resolve_queue` 抛错 / present 路径跳过 / `Queue::submit` 守 `queue_ != VK_NULL_HANDLE`），无需移植，详见修复 #4。
- `f4ffc01`(destroyImage GPU sync)：全队列 timeline 等待**不合入**；其空指针守卫意图已被 main 新 `destroy_image` 的逐句柄 null 检查覆盖，详见修复 #5。
- `67258a6`(添加遗留头文件) / `d947b71`(迁移 vision hotfix) / `6a5b93a`(修改导出接口) / `930f73a`(merge commit)：属架构/接口/历史性改动，按"禁止合入旧 API/旧后端"原则不吸收。

待办（PR 落地后由维护者执行其一）：

```powershell
# 选项 A：在 main 上记"已逻辑合入"标记（不改文件内容）
git checkout main
git merge -s ours origin/merge -m "merge: mark origin/merge as logically absorbed (see MERGE_BRANCH_REVIEW_PLAN.md)"

# 选项 B：确认废弃后删除远端分支
git push origin --delete merge
```

## 明确假设

- `origin/merge` 是本次要审查的 merge 分支。
- 目标是吸收关键修复，而不是保留 `origin/merge` 的提交历史或旧架构。
- 当前 main 的 API 大改是目标状态，不能为了合并方便回退。
- 合并完成标准是构建和运行验收通过，不是冲突解决完成。
- 本文档只记录计划和审查结论，不执行实际代码合并。
- 环境假设：`git merge-tree --write-tree` 需 Git ≥ 2.38；构建命令中 VS 路径硬编码为 `Visual Studio\18\Community`，非 Community 版或不同安装路径需自行调整。
