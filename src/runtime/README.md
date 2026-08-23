# Horizon Runtime

> 状态：迁移占位，尚未接入 CMake target

Runtime 承接必须同时使用 DSL/compiler 与 RHI 的运行时集成功能，包括资源自动捕获、
typed host 资源、参数绑定、Printer、Debugger、managed resource 和 polymorphic
resource。

当前第一批迁入：

- `data/encodable.h`
- `resources/registrable.h`
- `resources/polymorphic.h`

这些头文件已经归入 `horizon::runtime` 命名空间，但仍依赖尚未迁入 `src/rhi` 的资源
接口，因此本阶段不创建 `horizon-runtime` target。新 RHI 接入后再拆分纯 DSL 的
`PolymorphicRef`，并将 Runtime 独立接入构建和测试。

目标依赖方向：

```text
horizon-runtime -> horizon-dsl
horizon-runtime -> horizon-shader-compiler
horizon-runtime -> horizon-rhi
```

DSL、compiler 和 RHI 不得反向依赖 Runtime。
