# 0002：Runtime 模块边界

> 状态：目标设计，迁移中，尚未完整实现
>
> 范围：DSL、Shader compiler、RHI 与 Runtime 的集成边界

## 背景

资源自动捕获、typed host 资源、参数绑定、Printer、Debugger、managed resource
和 polymorphic resource 需要组合语言构造、编译与 GPU 资源管理。把这些集成功能
放入 DSL 或 RHI，会让语言层与设备层相互依赖。

## 决策

由独立的 Runtime 模块承接集成功能，作为唯一允许同时依赖 DSL/compiler 与 RHI
的集成模块。目标依赖方向为：

```text
horizon-runtime -> horizon-dsl
horizon-runtime -> horizon-shader-compiler
horizon-runtime -> horizon-rhi
```

DSL、compiler 和 RHI 不得反向依赖 Runtime。纯 Shader 表达保留在语言层，
需要物理资源、上传、回读和执行的部分由 Runtime 负责。

## 取舍与迁移影响

独立集成层使语言层和设备层可以分别使用与验证，但混合类型需要拆分，不能直接
保留跨层依赖。例如 `PolymorphicRef` 的纯 DSL 部分需要与运行时资源管理分离。

迁移顺序为：先接入新 RHI，再完成上述拆分，随后将 Runtime 独立接入构建和测试。

整体拓扑见 [Horizon GPU 编程架构](../gpu-programming-stack.md)。
