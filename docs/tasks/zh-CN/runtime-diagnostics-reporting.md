# 运行时信息收集与崩溃上报任务说明

## 目标

- 面向未来成百上千台机器运行 Horizon / HorizonExamples / 下游应用的场景，建立可扩展的信息收集、崩溃报告和服务器分析系统。
- 先保证本地报告包完整、可复现、可离线带回，再做自动上传；不要把网络上传做成诊断系统的唯一出口。
- 崩溃定位要能回答：哪台机器、什么系统和驱动、哪个 Horizon 构建、运行到哪个示例/功能、最后的 Vulkan / Horizon 诊断是什么、崩溃线程和调用栈是什么、是否能用符号还原。

## 设计原则

- 默认先做 `Off` / `LocalOnly` / `UploadDiagnostics` / `UploadCrashMinidump` 等明确模式；是否默认上传必须由产品/发行策略决定，不要在库层偷偷联网。
- 上传不能阻塞渲染、提交、present 或崩溃处理路径。正常运行时写入本地队列，后台线程或下次启动再上传。
- 崩溃处理路径只能做最小、异步安全或平台推荐的动作：写 dump、刷已有环形日志、记录异常码；不要在崩溃 handler 中分配大量内存、拿复杂锁或发 HTTP。
- 数据采集必须可脱敏、可限流、可采样、可关闭。不要上传源码、用户文件、完整环境变量、明文路径、令牌、密钥或任意内存。
- 稳定匿名机器/安装 ID 应可重置，避免用真实用户名、机器名或硬件序列号作为主键。
- 公共 `include/` 不暴露 Vulkan、Windows、上传协议或服务器细节；先把实现放在内部诊断/工具层，等 contract 稳定后再考虑 public API。
- 支持多线程并发调用：report sink、环形 breadcrumbs、上传队列和崩溃状态都要有清晰锁边界、atomic 状态或 owner-thread 策略。

## 报告内容

- Report envelope：`schema_version`、`report_id`、时间戳、会话 ID、匿名安装 ID、Horizon 版本、git commit/build id、编译配置、目标程序名、示例 mode、命令行 allowlist。
- 机器信息：OS 版本、CPU 架构/核心数、内存容量、显卡列表、驱动版本、Vulkan loader/API 版本、可用 layer/extension、已选/跳过设备和跳过原因。
- Horizon 运行信息：启用的 feature flags、`HORIZON_ENABLE_VALIDATION`、关键环境变量 allowlist、resource/pipeline/queue 统计、最近提交 token、窗口/swapchain 状态。
- 诊断附件：`horizon-vulkan-diagnostics.txt`、stdout/stderr 摘要、Horizon validation 记录、Vulkan validation 记录、最近 breadcrumbs 环形日志、必要的配置快照。
- 崩溃附件：异常码/signal、崩溃线程、线程列表、调用栈、loaded modules、符号文件标识、minidump/crash dump、最后一次未处理错误和当前 report package 路径。
- 上传元数据：压缩格式、内容 hash、附件列表、采集模式、采样原因、脱敏版本、上传尝试次数、服务器响应 ID。

## 客户端架构

- 本地报告包优先：每次异常退出、手动请求或关键兼容性失败时生成一个目录或压缩包，包含 `report.json`、诊断 txt、日志片段和可选 dump。
- 后端诊断继续复用 `src/hardware_wrapper/diagnostics.*` 和 `horizon-vulkan-diagnostics.txt`；新的系统只聚合它，不把 Vulkan 细节上移到 public API。
- 崩溃捕获按平台拆分。Windows 首选 WER LocalDumps 或 `MiniDumpWriteDump` 风格的 minidump；其他平台后续再补 signal handler / core dump 集成。
- breadcrumbs 采用固定大小 ring buffer，记录高价值事件：设备选择、pipeline 创建、descriptor 分配、image layout transition、queue submit/present、swapchain resize、validation error、用户选择的示例 mode。
- 上传器和采集器解耦：采集器只落盘；上传器扫描本地队列，按退避重试、速率限制、最大磁盘占用和 kill switch 执行。
- 下游应用应能设置产品名、版本、用户同意状态、额外 key/value、额外附件和自定义 endpoint，但不能绕过脱敏和大小限制。

## 服务器与分析

- 入口服务只负责鉴权、限流、schema 校验、大小限制、病毒/格式检查、对象存储落盘和返回 ingest id。
- 分析服务按 fingerprint 聚合：异常码 + 归一化调用栈 + Horizon build id + GPU/driver/Vulkan API + validation message。
- Dashboard 第一批关注：Top crash、unsupported GPU/driver/API、validation message 热点、特定 build 回归、特定硬件/驱动聚集、上传失败率。
- 符号系统要保存 build id 到 PDB/debug symbols 的映射；没有符号时报告仍可按模块/偏移分组，但不能视为定位完成。
- 保留策略要明确：诊断 JSON、日志、minidump/full dump 的保存期不同；删除请求和项目下线要能清理对象存储。

## 分阶段实施

- P0 本地报告包：定义 `report.json` schema、采集机器/Vulkan/Horizon build 信息、聚合现有 `horizon-vulkan-diagnostics.txt`、支持手动生成报告包。
- P1 崩溃证据：Windows minidump/WER 集成、PDB/build id 记录、强制崩溃测试程序、符号还原验证、崩溃时不死锁。
- P2 上传队列：本地 pending/sent/failed 队列、压缩、hash、退避重试、离线恢复、全局关闭开关、上传 CLI 或后台 uploader。
- P3 服务器 ingest：最小 HTTPS endpoint、鉴权、schema 校验、对象存储、report id、基本查询和下载。
- P4 聚合分析：fingerprint、dashboard、按 build/GPU/driver/validation 聚类、去重、告警。
- P5 产品化策略：同意/隐私文案、采样、数据保留、full dump 策略、下游应用接入指南。

## 非目标

- 第一阶段不要做全量远程日志系统或持续性能监控平台；先保证崩溃和兼容性诊断闭环。
- 不要在 Horizon 核心渲染路径中直接发网络请求。
- 不要默认上传 full memory dump；full dump 可能包含用户数据，只能在明确同意和受控环境下启用。
- 不要为了上报方便把 Windows / Vulkan / HTTP 类型放进 public header。
- 不要收集用户项目源码、素材内容、访问令牌、完整路径、完整环境变量或任意剪贴板/窗口内容。

## 验收

- 在无网络环境运行示例并触发兼容性失败时，会生成本地报告包，包含机器信息、Horizon build 信息和 `horizon-vulkan-diagnostics.txt`。
- 强制崩溃测试能生成 minidump 和 `report.json`，并能用匹配 PDB 还原到函数/文件/行或至少模块/偏移。
- 上传端点不可达时程序不阻塞、不崩溃；报告留在本地队列，下次网络恢复后可上传。
- 脱敏测试确认用户名、绝对用户路径、环境变量 secret、令牌不会出现在默认报告中。
- 多线程压力下 breadcrumbs 和 report sink 无数据竞争、死锁或无界内存增长。
- 服务器能按 crash fingerprint、GPU、driver、Vulkan API、Horizon build 聚合报告，并能下载单个报告包复查。

## 相关入口

- Vulkan 本地诊断规则：`docs/agents/zh-CN/vulkan.md`。
- Vulkan 后端任务说明：`docs/tasks/zh-CN/vulkan-backend.md`。
- 示例可见窗口和运行时崩溃 smoke：`docs/tasks/zh-CN/examples-new-api-visible-window.md`。
- 当前本地诊断实现：`src/hardware_wrapper/diagnostics.*`。
- 当前诊断输出：`horizon-vulkan-diagnostics.txt`，可由 `HORIZON_VULKAN_DIAGNOSTICS_PATH` 覆盖。
