# 快速优化实施方案

## 当前分辨率
- RSM: 1024×1024
- Sponza shadow: 2048×2048 ⚠️ 非常高
- edsl_sponza: (需检查)

## 优化1: 降低Sponza shadow map
从2048→1024，减少4倍像素

## 优化2: 降低RSM
从1024→512，减少4倍像素（间接光照低频）

## 优化3: assao预计算矩阵
存储121个静态transform

## 实施顺序
1. Sponza shadow (最简单，最明显)
2. RSM (简单)
3. assao预计算 (略复杂)
