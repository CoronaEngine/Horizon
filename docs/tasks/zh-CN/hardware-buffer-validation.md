# Hardware Buffer 验证任务说明

## 边界

- `HardwareBufferDesc` 负责数据默认值和纯派生属性，例如 `byte_size()`。
- 不要添加 `HardwareBufferDesc::validate()`；策略检查保留在 `src/hardware_wrapper/validation`。
- 硬安全不变量始终开启：元素大小为零、byte-size 溢出、上传数据超过 buffer byte size 都不能依赖可选验证。
- 构造期 `upload_data` 为空指针或 `CpuAccessMode::None` + 非空 `upload_data` 也属于硬错误；device-local buffer 的数据上传应通过显式 `HardwareBuffer::upload(...)` 命令批次完成。
- `HORIZON_ENABLE_HARDWARE_VALIDATION` 是 Horizon 自身可选验证诊断的编译期开关，应通过 `target_compile_definitions(... PUBLIC ...)` 导出。
- `HardwareValidationMode` 是运行时策略开关：`Disabled`、`Log` 或 `Throw`。

## 放置位置

- buffer desc、显式 upload、copy、host read、host write 策略检查放在 `src/hardware_wrapper/validation` 下。
- 公共模板或禁用验证的构建仍然需要纯 size 计算时，不要把它放进 validation 模块。
- 如果 `HardwareBufferDesc::byte_size()` 变成非 inline，把它移到始终参与编译的实现文件，而不是可选 validation 模块。
- 除非公共 API 明确暴露相关概念，否则不要把 Vulkan、VMA、Windows 或其他后端专属细节放进公共 descriptor 检查。

## 验证

文档或仅注释更新：

```powershell
git diff --check
```

C++ 或 CMake 改动：

```powershell
cmd.exe /d /s /c "`"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat`" -arch=x64 -host_arch=x64 && cmake --build --preset msvc-debug --target Horizon"
```
