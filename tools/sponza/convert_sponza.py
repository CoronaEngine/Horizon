#!/usr/bin/env python3
"""glTF 2.0 -> HZMS v3 converter for the Sponza examples.

用法:
    python convert_sponza.py <scene.gltf> <output_dir> [--max-tex 2048] [--texconv path/to/texconv.exe]

输入:  标准 glTF 2.0(外部 .bin + 外部贴图;Intel Main Sponza 即此格式)
输出:  <output_dir>/sponza.bin   HZMS v3 容器(布局见下)
       <output_dir>/sponza.json  元数据(人类可读,加载器不读)
       <output_dir>/textures/*.ktx  BC7 KTX1(带全 mip 链;无 texconv 时退化为
                                     未压缩 RGBA8/SRGB8 KTX1)

HZMS v3 布局(与 v2 逐字段同尺寸,仅材质槽位语义变化):
    u32 magic 'HZMS'  u32 version=3
    u32 vertexCount  u32 indexCount  u32 submeshCount  u32 materialCount
    u32 textureCount u32 stringBytes
    SceneBlock(25 floats): sceneMin3 sceneMax3 camPos3 camTarget3 camUp3
                           yfov znear zfar sunDir3 sunColor3 sunIntensity
    Vertex[vertexCount]  48B: pos3f normal3f tangent4f uv2f(节点变换已烘焙)
    u32 Index[indexCount]
    Submesh[submeshCount] 40B: firstIndex indexCount material(i32) nameOffset min3 max3
    Material[materialCount] 48B:
        i32 baseColorTexture   (v2: 同)
        i32 normalTexture      (v2: bump)
        i32 metalRoughTexture  (v2: specular —— 语义变更,glTF G=roughness B=metallic)
        i32 maskTexture        (alpha mask;可与 baseColor 同图,见 flags bit2)
        f32 baseColorFactor[4]
        f32 metallicFactor
        f32 roughnessFactor
        u32 nameOffset
        u32 flags              bit0 双面  bit1 alpha mask  bit2 mask 采 alpha 通道
    u32 textureNameOffset[textureCount]
    char strings[stringBytes]
"""

import argparse
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

HZMS_MAGIC = 0x534D5A48
HZMS_VERSION = 3

FLAG_DOUBLE_SIDED = 1 << 0
FLAG_ALPHA_MASK = 1 << 1
FLAG_MASK_USES_ALPHA = 1 << 2

COMPONENT_DTYPE = {
    5120: np.int8, 5121: np.uint8, 5122: np.int16,
    5123: np.uint16, 5125: np.uint32, 5126: np.float32,
}
TYPE_COUNT = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


# ============================================================================
# glTF 解析
# ============================================================================

class Gltf:
    def __init__(self, path: Path):
        self.root_dir = path.parent
        with open(path, "r", encoding="utf-8") as f:
            self.doc = json.load(f)
        self.buffers = []
        for buf in self.doc.get("buffers", []):
            uri = buf["uri"]
            with open(self.root_dir / uri, "rb") as f:
                self.buffers.append(f.read())

    def accessor(self, index: int) -> np.ndarray:
        """按 accessor 读出 (count, components) 的 numpy 数组(处理 byteStride)。"""
        acc = self.doc["accessors"][index]
        if "sparse" in acc:
            raise RuntimeError("sparse accessor is not supported")
        count = acc["count"]
        comps = TYPE_COUNT[acc["type"]]
        dtype = COMPONENT_DTYPE[acc["componentType"]]
        elem_size = np.dtype(dtype).itemsize * comps

        view = self.doc["bufferViews"][acc["bufferView"]]
        buf = self.buffers[view.get("buffer", 0)]
        offset = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
        stride = view.get("byteStride", elem_size)

        if stride == elem_size:
            arr = np.frombuffer(buf, dtype=dtype, count=count * comps, offset=offset)
            return arr.reshape(count, comps).copy()
        # interleaved:按 stride 取
        raw = np.frombuffer(buf, dtype=np.uint8)
        idx = offset + np.arange(count)[:, None] * stride + np.arange(elem_size)[None, :]
        return raw[idx].copy().view(dtype).reshape(count, comps)


def node_matrix(node: dict) -> np.ndarray:
    if "matrix" in node:
        return np.array(node["matrix"], dtype=np.float64).reshape(4, 4).T  # 列主序→行主序矩阵
    m = np.eye(4)
    if "translation" in node:
        t = node["translation"]
        tm = np.eye(4); tm[:3, 3] = t
        m = m @ tm
    if "rotation" in node:
        x, y, z, w = node["rotation"]
        rm = np.eye(4)
        rm[:3, :3] = np.array([
            [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
        ])
        m = m @ rm
    if "scale" in node:
        s = node["scale"]
        sm = np.diag([s[0], s[1], s[2], 1.0])
        m = m @ sm
    return m


def compute_tangents(pos: np.ndarray, normal: np.ndarray, uv: np.ndarray,
                     indices: np.ndarray) -> np.ndarray:
    """无切线时按 uv 梯度累计生成(Lengyel 法),返回 (N,4) float32。"""
    tan = np.zeros((len(pos), 3), dtype=np.float64)
    bitan = np.zeros((len(pos), 3), dtype=np.float64)
    tri = indices.reshape(-1, 3)
    p0, p1, p2 = pos[tri[:, 0]], pos[tri[:, 1]], pos[tri[:, 2]]
    u0, u1, u2 = uv[tri[:, 0]], uv[tri[:, 1]], uv[tri[:, 2]]
    e1, e2 = p1 - p0, p2 - p0
    d1, d2 = u1 - u0, u2 - u0
    det = d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]
    det = np.where(np.abs(det) < 1e-12, 1.0, det)
    r = 1.0 / det
    t = (e1 * d2[:, 1:2] - e2 * d1[:, 1:2]) * r[:, None]
    b = (e2 * d1[:, 0:1] - e1 * d2[:, 0:1]) * r[:, None]
    for c in range(3):
        np.add.at(tan, tri[:, c], t)
        np.add.at(bitan, tri[:, c], b)
    # Gram-Schmidt 对法线正交化
    ndott = np.sum(normal * tan, axis=1, keepdims=True)
    t_ortho = tan - normal * ndott
    lens = np.linalg.norm(t_ortho, axis=1, keepdims=True)
    fallback = np.tile(np.array([1.0, 0.0, 0.0]), (len(pos), 1))
    t_unit = np.where(lens > 1e-8, t_ortho / np.maximum(lens, 1e-12), fallback)
    sign = np.where(np.sum(np.cross(normal, t_unit) * bitan, axis=1) < 0.0, -1.0, 1.0)
    return np.concatenate([t_unit, sign[:, None]], axis=1).astype(np.float32)


# ============================================================================
# 贴图转换:texconv → DDS(BC7+mips) → 重打包 KTX1
# ============================================================================

DXGI_BC7_UNORM = 98
DXGI_BC7_SRGB = 99
GL_BC7_UNORM = 0x8E8C
GL_BC7_SRGB = 0x8E8D
GL_RGBA8 = 0x8058
GL_SRGB8_ALPHA8 = 0x8C43
KTX1_IDENT = bytes([0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A])


def write_ktx1(path: Path, width: int, height: int, gl_internal: int,
               mips: list, gl_format=0, gl_type=0):
    """mips: list of bytes,mip0 在前。压缩格式 gl_format/gl_type=0。"""
    with open(path, "wb") as f:
        f.write(KTX1_IDENT)
        f.write(struct.pack("<13I", 0x04030201, gl_type, 1, gl_format, gl_internal,
                            0x1908, width, height, 0, 0, 1, len(mips), 0))
        for data in mips:
            f.write(struct.pack("<I", len(data)))
            f.write(data)
            pad = (4 - (len(data) % 4)) % 4
            f.write(b"\x00" * pad)


def dds_to_ktx(dds_path: Path, ktx_path: Path):
    data = dds_path.read_bytes()
    if data[:4] != b"DDS ":
        raise RuntimeError(f"not a DDS: {dds_path}")
    height, width = struct.unpack_from("<II", data, 12)
    mip_count = max(1, struct.unpack_from("<I", data, 28)[0])
    fourcc = data[84:88]
    if fourcc != b"DX10":
        raise RuntimeError(f"expected DX10 DDS: {dds_path}")
    dxgi = struct.unpack_from("<I", data, 128)[0]
    if dxgi == DXGI_BC7_UNORM:
        gl_internal = GL_BC7_UNORM
    elif dxgi == DXGI_BC7_SRGB:
        gl_internal = GL_BC7_SRGB
    else:
        raise RuntimeError(f"unexpected DXGI format {dxgi}: {dds_path}")
    payload = data[148:]
    mips = []
    cursor = 0
    w, h = width, height
    for _ in range(mip_count):
        size = ((w + 3) // 4) * ((h + 3) // 4) * 16
        mips.append(payload[cursor:cursor + size])
        cursor += size
        w, h = max(1, w // 2), max(1, h // 2)
    write_ktx1(ktx_path, width, height, gl_internal, mips)


def convert_texture_texconv(texconv: str, src: Path, dst_ktx: Path, srgb: bool, max_size: int):
    from PIL import Image
    with Image.open(src) as probe:
        w, h = probe.size
    scale = min(1.0, max_size / max(w, h))
    tw = max(4, int(w * scale)) & ~3  # BC7 需要 4 的倍数
    th = max(4, int(h * scale)) & ~3
    with tempfile.TemporaryDirectory() as tmp:
        # sRGB 语义由输出格式携带(_SRGB 后缀),不传 -srgbi/-srgbo,
        # 避免 texconv 做数值层面的色彩空间转换。
        fmt = "BC7_UNORM_SRGB" if srgb else "BC7_UNORM"
        args = [texconv, "-f", fmt, "-m", "0", "-y", "-nologo",
                "-w", str(tw), "-h", str(th),
                "-o", tmp, str(src)]
        r = subprocess.run(args, capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError(f"texconv failed on {src}:\n{r.stdout}\n{r.stderr}")
        dds = Path(tmp) / (src.stem + ".dds")
        dds_to_ktx(dds, dst_ktx)


def convert_texture_pil(src: Path, dst_ktx: Path, srgb: bool, max_size: int):
    """无 texconv 的退路:PIL 缩放 + 手工 mip,未压缩 RGBA8/SRGB8 KTX1。"""
    from PIL import Image
    img = Image.open(src).convert("RGBA")
    w, h = img.size
    scale = min(1.0, max_size / max(w, h))
    if scale < 1.0:
        img = img.resize((max(1, int(w * scale)), max(1, int(h * scale))), Image.LANCZOS)
    # 缩到 2 的幂,mip 链才是整除的
    def pot(v):
        return 1 << max(0, int(math.floor(math.log2(v))))
    img = img.resize((pot(img.width), pot(img.height)), Image.LANCZOS)
    mips = []
    cur = img
    while True:
        mips.append(cur.tobytes())
        if cur.width == 1 and cur.height == 1:
            break
        cur = cur.resize((max(1, cur.width // 2), max(1, cur.height // 2)), Image.LANCZOS)
    gl_internal = GL_SRGB8_ALPHA8 if srgb else GL_RGBA8
    write_ktx1(dst_ktx, img.width, img.height, gl_internal, mips,
               gl_format=0x1908, gl_type=0x1401)  # GL_RGBA / GL_UNSIGNED_BYTE


# ============================================================================
# 主转换
# ============================================================================

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("gltf")
    ap.add_argument("outdir")
    ap.add_argument("--max-tex", type=int, default=2048)
    ap.add_argument("--texconv", default="texconv.exe")
    args = ap.parse_args()

    gltf_path = Path(args.gltf)
    outdir = Path(args.outdir)
    texdir = outdir / "textures"
    outdir.mkdir(parents=True, exist_ok=True)
    texdir.mkdir(parents=True, exist_ok=True)

    has_texconv = shutil.which(args.texconv) is not None
    print(f"[convert] texconv: {'yes -> BC7' if has_texconv else 'NO -> uncompressed RGBA'}")

    g = Gltf(gltf_path)
    doc = g.doc

    # ---- 场景图遍历,收集 (mesh, world_matrix) ----
    instances = []
    scene = doc["scenes"][doc.get("scene", 0)]

    def walk(node_index, parent):
        node = doc["nodes"][node_index]
        world = parent @ node_matrix(node)
        if "mesh" in node:
            instances.append((node["mesh"], world))
        for child in node.get("children", []):
            walk(child, world)

    for root in scene["nodes"]:
        walk(root, np.eye(4))
    print(f"[convert] {len(instances)} mesh instances")

    # ---- 合并 VB/IB,烘焙变换 ----
    all_vertices = []   # (N,12) float32: pos3 normal3 tangent4 uv2
    all_indices = []
    submeshes = []      # (first_index, index_count, material, name, min3, max3)
    vertex_base = 0
    index_base = 0

    for mesh_index, world in instances:
        mesh = doc["meshes"][mesh_index]
        normal_mtx = np.linalg.inv(world[:3, :3]).T
        for prim in mesh.get("primitives", []):
            if prim.get("mode", 4) != 4:
                continue  # 只支持三角形
            attrs = prim["attributes"]
            pos = g.accessor(attrs["POSITION"]).astype(np.float64)
            count = len(pos)
            normal = (g.accessor(attrs["NORMAL"]).astype(np.float64)
                      if "NORMAL" in attrs else np.tile([0.0, 1.0, 0.0], (count, 1)))
            uv = (g.accessor(attrs["TEXCOORD_0"]).astype(np.float32)
                  if "TEXCOORD_0" in attrs else np.zeros((count, 2), dtype=np.float32))
            indices = (g.accessor(prim["indices"]).astype(np.uint32).reshape(-1)
                       if "indices" in prim else np.arange(count, dtype=np.uint32))

            # 烘焙世界变换
            pos_w = (pos @ world[:3, :3].T) + world[:3, 3]
            nrm_w = normal @ normal_mtx.T
            nrm_len = np.linalg.norm(nrm_w, axis=1, keepdims=True)
            nrm_w = nrm_w / np.maximum(nrm_len, 1e-12)

            if "TANGENT" in attrs:
                tan4 = g.accessor(attrs["TANGENT"]).astype(np.float64)
                tan_w = tan4[:, :3] @ world[:3, :3].T
                tan_len = np.linalg.norm(tan_w, axis=1, keepdims=True)
                tan_w = tan_w / np.maximum(tan_len, 1e-12)
                handed = tan4[:, 3:4]
                # 变换含镜像时翻转手性
                if np.linalg.det(world[:3, :3]) < 0.0:
                    handed = -handed
                tangent = np.concatenate([tan_w, handed], axis=1).astype(np.float32)
            else:
                tangent = compute_tangents(pos_w, nrm_w, uv.astype(np.float64), indices)

            vertex = np.concatenate([
                pos_w.astype(np.float32), nrm_w.astype(np.float32),
                tangent.astype(np.float32), uv.astype(np.float32)], axis=1)
            assert vertex.shape[1] == 12

            mat = prim.get("material", -1)
            name = mesh.get("name", f"mesh{mesh_index}")
            mn = pos_w.min(axis=0)
            mx = pos_w.max(axis=0)
            submeshes.append([index_base, len(indices), mat, name,
                              mn.astype(np.float32), mx.astype(np.float32)])
            all_vertices.append(vertex)
            all_indices.append(indices + vertex_base)
            vertex_base += count
            index_base += len(indices)

    vertices = np.concatenate(all_vertices, axis=0)
    indices = np.concatenate(all_indices, axis=0)
    scene_min = vertices[:, 0:3].min(axis=0)
    scene_max = vertices[:, 0:3].max(axis=0)
    print(f"[convert] {len(vertices)} vertices, {len(indices)} indices, "
          f"{len(submeshes)} submeshes, bounds {scene_min} .. {scene_max}")

    # ---- 材质与贴图 ----
    images = doc.get("images", [])
    textures = doc.get("textures", [])

    def image_of_texture(tex_index):
        if tex_index is None or tex_index < 0:
            return -1
        return textures[tex_index].get("source", -1)

    used = {}          # image index -> (ktx name, srgb)
    def use_image(img_index, srgb):
        if img_index < 0:
            return -1
        if img_index in used:
            return list(used.keys()).index(img_index)
        uri = images[img_index].get("uri")
        if uri is None:
            raise RuntimeError("embedded images are not supported")
        stem = Path(uri).stem
        used[img_index] = (f"{stem}.ktx", srgb)
        return len(used) - 1

    materials = []
    skip_material = set()  # BLEND 贴花等:不透明管线渲染会成黑斑,整个子网格跳过
    for i, mat in enumerate(doc.get("materials", [])):
        pbr = mat.get("pbrMetallicRoughness", {})
        base_tex = image_of_texture(pbr.get("baseColorTexture", {}).get("index", -1))
        mr_tex = image_of_texture(pbr.get("metallicRoughnessTexture", {}).get("index", -1))
        normal_tex = image_of_texture(mat.get("normalTexture", {}).get("index", -1))

        base_slot = use_image(base_tex, srgb=True)
        mr_slot = use_image(mr_tex, srgb=False)
        normal_slot = use_image(normal_tex, srgb=False)

        flags = 0
        mask_slot = -1
        if mat.get("doubleSided", False):
            flags |= FLAG_DOUBLE_SIDED
        if mat.get("alphaMode", "OPAQUE") == "MASK":
            flags |= FLAG_ALPHA_MASK | FLAG_MASK_USES_ALPHA
            mask_slot = base_slot  # glTF 的 mask 在 baseColor.a
        if mat.get("alphaMode", "OPAQUE") == "BLEND":
            skip_material.add(i)

        factor = list(pbr.get("baseColorFactor", [1.0, 1.0, 1.0, 1.0]))
        metallic = pbr.get("metallicFactor", 1.0)
        roughness = pbr.get("roughnessFactor", 1.0)

        # 玻璃类材质(albedo 纯黑、无贴图)在不透明管线里就是黑板,换成
        # 高金属低粗糙的浅色反射面,SSR/IBL 会给出像样的玻璃观感。
        if base_slot < 0 and max(factor[:3]) < 0.05:
            factor = [0.60, 0.70, 0.75, 1.0]
            metallic = 1.0
            roughness = 0.08

        materials.append({
            "base": base_slot, "normal": normal_slot, "mr": mr_slot, "mask": mask_slot,
            "factor": factor,
            "metallic": metallic,
            "roughness": roughness,
            "name": mat.get("name", "material"),
            "flags": flags,
        })
    print(f"[convert] {len(materials)} materials, {len(used)} unique textures, "
          f"skip {sorted(skip_material)}")

    # ---- 贴图转换 ----
    tex_names = []
    for i, (img_index, (ktx_name, srgb)) in enumerate(used.items()):
        src = g.root_dir / images[img_index]["uri"]
        dst = texdir / ktx_name
        tex_names.append(ktx_name)
        if dst.exists():
            print(f"[tex {i + 1}/{len(used)}] {ktx_name} (cached)")
            continue
        print(f"[tex {i + 1}/{len(used)}] {src.name} -> {ktx_name} ({'srgb' if srgb else 'linear'})")
        if has_texconv:
            convert_texture_texconv(args.texconv, src, dst, srgb, args.max_tex)
        else:
            convert_texture_pil(src, dst, srgb, args.max_tex)

    # ---- 相机与太阳(白天、斜射平行光) ----
    center = (scene_min + scene_max) * 0.5
    extent = scene_max - scene_min
    # 合成机位:中殿西端、视线沿 X 长轴看向东端(长廊大景构图)。
    # 不用 glTF 里的 PhysCamera(有的贴墙、有的在高处,不适合默认视角)。
    cam_pos = np.array([scene_min[0] + extent[0] * 0.18,
                        scene_min[1] + extent[1] * 0.16,
                        center[2]])
    cam_target = np.array([scene_max[0],
                           scene_min[1] + extent[1] * 0.24,
                           center[2]])

    sun_dir = np.array([0.35, -1.0, 0.28])  # 白天斜射,穿过天窗
    sun_dir = sun_dir / np.linalg.norm(sun_dir)

    scene_block = struct.pack(
        "<25f",
        *scene_min.tolist(), *scene_max.tolist(),
        *cam_pos.tolist(), *cam_target.tolist(),
        0.0, 1.0, 0.0,
        math.radians(60.0), 0.1, float(np.linalg.norm(extent)) * 2.0,
        *sun_dir.tolist(),
        1.0, 0.97, 0.92,
        3.0)

    # ---- 字符串表 ----
    strings = bytearray()
    def add_string(s):
        off = len(strings)
        strings.extend(s.encode("utf-8") + b"\x00")
        return off

    submesh_records = b""
    for first, count, mat, name, mn, mx in submeshes:
        submesh_records += struct.pack("<IIiI3f3f", first, count, mat, add_string(name),
                                       *mn.tolist(), *mx.tolist())
    material_records = b""
    for m in materials:
        material_records += struct.pack(
            "<4i4f2fII", m["base"], m["normal"], m["mr"], m["mask"],
            *m["factor"], m["metallic"], m["roughness"], add_string(m["name"]), m["flags"])
    texture_offsets = b"".join(struct.pack("<I", add_string(n)) for n in tex_names)

    # ---- 剔除 skip 材质的子网格(BLEND 贴花)----
    if skip_material:
        kept = [s for s in submeshes if s[2] not in skip_material]
        print(f"[convert] dropped {len(submeshes) - len(kept)} BLEND submeshes")
        submeshes = kept
        submesh_records = b""
        for first, count, mat, name, mn, mx in submeshes:
            submesh_records += struct.pack("<IIiI3f3f", first, count, mat, add_string(name),
                                           *mn.tolist(), *mx.tolist())

    # ---- 写 HZMS ----
    with open(outdir / "sponza.bin", "wb") as f:
        f.write(struct.pack("<8I", HZMS_MAGIC, HZMS_VERSION,
                            len(vertices), len(indices), len(submeshes),
                            len(materials), len(tex_names), len(strings)))
        f.write(scene_block)
        f.write(vertices.astype(np.float32).tobytes())
        f.write(indices.astype(np.uint32).tobytes())
        f.write(submesh_records)
        f.write(material_records)
        f.write(texture_offsets)
        f.write(bytes(strings))

    meta = {
        "format": "HZMS", "version": HZMS_VERSION,
        "source": str(gltf_path),
        "vertexCount": int(len(vertices)), "indexCount": int(len(indices)),
        "submeshCount": len(submeshes), "materialCount": len(materials),
        "textureCount": len(tex_names),
        "textures": "BC7 KTX1" if has_texconv else "RGBA8 KTX1",
        "maxTextureSize": args.max_tex,
        "materialSemantics": "slot2 = glTF metallicRoughness (G=roughness, B=metallic)",
    }
    with open(outdir / "sponza.json", "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2)

    size_mb = os.path.getsize(outdir / "sponza.bin") / (1024 * 1024)
    print(f"[convert] done: sponza.bin {size_mb:.1f} MB, {len(tex_names)} textures")


if __name__ == "__main__":
    sys.exit(main())
