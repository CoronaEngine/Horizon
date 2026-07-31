# Sponza 资产转换(glTF → HZMS)

`convert_sponza.py` 把标准 glTF 2.0 场景转成 `example_sponza` 加载的 HZMS 容器。

## 用法

```bash
# 依赖:python3 + numpy + PIL;texconv.exe(可选,BC7 压缩,否则退化为未压缩 RGBA)
# texconv 下载:https://github.com/microsoft/DirectXTex/releases (单文件)
python tools/sponza/convert_sponza.py <scene.gltf> examples/assets/sponza2 \
    --max-tex 2048 --texconv path/to/texconv.exe
```

当前 `examples/assets/sponza2` 由 **Intel Main Sponza**(Sponza 2022 重制版,CC BY 4.0)转换而来:

- 下载页:https://www.intel.com/content/www/us/en/developer/topic-technology/graphics-research/samples.html
- 包名 `main_sponza.zip`(≈3.7 GB),取其中 `NewSponza_Main_glTF_003.gltf`
- 2M 顶点 / 11.2M 索引 / 401 子网格 / 28 材质 / 72 张贴图(4K 源,转 2K BC7 KTX1)

`example_sponza` 优先加载 `assets/sponza2`(HZMS v3),缺失时回退旧的
`assets/sponza`(HZMS v2,Khronos 版,材质退化为常量 metallic/roughness)。

## HZMS v3 布局

v3 与 v2 的记录字节布局完全一致,仅材质槽位语义变化(详见脚本 docstring):

- 材质槽位 2:v2 = specular 灰度图 → v3 = glTF metallicRoughness(G=roughness, B=metallic)
- flags bit2(v3 新增):alpha mask 采 baseColor 的 alpha 通道(glTF MASK 模式)

## 转换规则

- 节点变换烘焙进顶点(法线用逆转置),世界空间即物体空间
- 无切线的图元按 uv 梯度生成(Lengyel 法 + Gram-Schmidt)
- alphaMode=BLEND 的子网格整个剔除(不透明管线渲成黑斑;贴花之类丢了无碍)
- 玻璃类材质(albedo 纯黑且无贴图)替换为高金属低粗糙的浅色反射面
- baseColor 用 BC7_UNORM_SRGB,normal/metallicRoughness 用 BC7_UNORM,全 mip 链
- 相机机位是合成的(中殿西端看向东端),不用 glTF 里的 PhysCamera
