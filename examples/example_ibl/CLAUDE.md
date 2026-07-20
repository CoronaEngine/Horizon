# example_ibl

用 orbs 默认：运行/演示本示例时默认使用 orbs 模式（5x5 球体阵列，逐球 glossiness / reflectivity 渐变），而不是 bunny。

- 运行中按 `M` 可在 bunny / orbs 间切换（代码里 `Settings::mesh_selection`，0 = bunny，1 = orbs）。
- 若修改代码默认值，改 `example_ibl.cpp` 中 `Settings::mesh_selection` 的初始值为 `1`。
