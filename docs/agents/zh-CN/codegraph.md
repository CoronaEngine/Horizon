# Horizon Codegraph 上下文

只在 codegraph 工具可用，且任务涉及代码定位、符号流、调用链、架构追踪、bug 定位或重构影响面时加载本文件。

## 使用原则

- 先用 codegraph 获取代码地图，再用源码、CMake、`git diff` 和验证命令确认事实。
- 对“X 怎么工作”、“哪里调用了 X”、“改 X 会影响谁”、“这个 bug 的路径在哪里”这类问题，优先用 `codegraph_explore` 建立上下文。
- 对重名、重载、镜像目录或模糊符号，用 `codegraph_node` 搭配 `file` 或 `line` 收窄到具体定义。
- 跨层重构前使用 `codegraph_callers`、`codegraph_callees` 或 `codegraph_impact` 检查影响面，尤其是 public API、Vulkan backend、resource lifetime、descriptor、Queue、Executor、Helicon reflection。
- codegraph 是定位和影响面分析工具，不是最终事实来源；如果索引结果与源码、CMake 或构建结果冲突，以当前仓库事实为准。
- codegraph 是可选本地增强工具；缺失时不能阻塞普通仓库工作，Agent 应说明正在降级处理并继续使用 `rg`、源码阅读、CMake 归属确认和针对性验证。
- 如果 codegraph 不可用或索引缺失，退回 `rg`、源码阅读和最小相关构建验证。
- 当用户只是询问 codegraph 是否需要更新、查看当前 dirty diff、或要求“再确认一遍”时，默认保持只读：用 `git status`、`git diff`、codegraph status/search/explore 和源码核对回答；这不等于请求编译验证，除非用户明确要求验证、发布检查、提交，或任务已经进入实现后的验收阶段。

## Horizon 注意点

- 查询 Vulkan 后端时优先带路径或术语约束，默认目标是当前 CMake 编译的 `src/hardware_wrapper_vulkan/`。
- 不要因为 codegraph 能看到 `src/HardwareWrapperVulkan/`、`src/hardware_wrapper/`、`src/HardwareWrapper/` 就修改历史镜像目录；除非用户明确指定。
- 追踪 public API 时从 `include/horizon.h` 和调用方一起看，避免把 internal Vulkan / VMA / Windows 类型带入公共边界。
- 追踪执行路径时把 compute / dispatch、graphics / present、Queue 序列化、resource lifetime 和 keep-alive 分开看，不要把 present 特例当作通用执行语义。
- `.codegraph/` 是本地索引目录，保持不提交。
