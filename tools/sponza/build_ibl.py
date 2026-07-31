#!/usr/bin/env python3
"""Build Horizon RGBA16F IBL cubemaps from a Radiance equirectangular HDR.

The output layout matches examples/assets/env/*_{lod,irr}.dds:
DX10 DDS, RGBA16F, cubemap faces in +X/-X/+Y/-Y/+Z/-Z order, with all
radiance mips stored face-major.  The radiance chain is GGX prefiltered and
the irradiance cube stores cosine-weighted diffuse irradiance divided by pi,
which is what the Sponza combine shader consumes.

Example:
    python tools/sponza/build_ibl.py sky_1k.hdr examples/assets/env/day_clouds
"""

from __future__ import annotations

import argparse
import math
import struct
from pathlib import Path

import numpy as np


def load_radiance_hdr(path: Path) -> np.ndarray:
    """Read the modern scanline-RLE RGBE form used by Poly Haven HDR files."""
    with path.open("rb") as stream:
        if not stream.readline().startswith((b"#?RADIANCE", b"#?RGBE")):
            raise RuntimeError(f"Not a Radiance HDR file: {path}")

        while True:
            line = stream.readline()
            if not line:
                raise RuntimeError("Unexpected EOF in Radiance header")
            if line in (b"\n", b"\r\n"):
                break

        resolution = stream.readline().decode("ascii").strip().split()
        if len(resolution) != 4 or resolution[0] != "-Y" or resolution[2] != "+X":
            raise RuntimeError(
                "Only the standard '-Y height +X width' Radiance orientation is supported"
            )
        height = int(resolution[1])
        width = int(resolution[3])
        if width < 8 or width > 0x7FFF:
            raise RuntimeError(f"Unsupported Radiance scanline width: {width}")

        rgbe = np.empty((height, width, 4), dtype=np.uint8)
        for y in range(height):
            marker = stream.read(4)
            if (
                len(marker) != 4
                or marker[0] != 2
                or marker[1] != 2
                or ((marker[2] << 8) | marker[3]) != width
            ):
                raise RuntimeError(
                    "Only modern Radiance scanline RLE is supported "
                    f"(invalid marker on scanline {y})"
                )

            for channel in range(4):
                x = 0
                while x < width:
                    code_bytes = stream.read(1)
                    if not code_bytes:
                        raise RuntimeError("Unexpected EOF in Radiance pixel data")
                    code = code_bytes[0]
                    if code > 128:
                        count = code - 128
                        value = stream.read(1)
                        if not value or x + count > width:
                            raise RuntimeError("Invalid Radiance RLE run")
                        rgbe[y, x : x + count, channel] = value[0]
                    else:
                        count = code
                        values = stream.read(count)
                        if count == 0 or len(values) != count or x + count > width:
                            raise RuntimeError("Invalid Radiance RLE literal")
                        rgbe[y, x : x + count, channel] = np.frombuffer(
                            values, dtype=np.uint8
                        )
                    x += count

    exponent = rgbe[..., 3].astype(np.int32)
    scale = np.ldexp(
        np.ones((height, width), dtype=np.float32), exponent - (128 + 8)
    )
    scale[exponent == 0] = 0.0
    return rgbe[..., :3].astype(np.float32) * scale[..., None]


def cube_directions(face: int, size: int) -> np.ndarray:
    """Return normalized world directions for one Vulkan cubemap face."""
    axis = (np.arange(size, dtype=np.float32) + 0.5) * (2.0 / size) - 1.0
    u, v = np.meshgrid(axis, axis)

    if face == 0:       # +X
        direction = np.stack((np.ones_like(u), -v, -u), axis=-1)
    elif face == 1:     # -X
        direction = np.stack((-np.ones_like(u), -v, u), axis=-1)
    elif face == 2:     # +Y
        direction = np.stack((u, np.ones_like(u), v), axis=-1)
    elif face == 3:     # -Y
        direction = np.stack((u, -np.ones_like(u), -v), axis=-1)
    elif face == 4:     # +Z
        direction = np.stack((u, -v, np.ones_like(u)), axis=-1)
    elif face == 5:     # -Z
        direction = np.stack((-u, -v, -np.ones_like(u)), axis=-1)
    else:
        raise ValueError(face)

    return direction / np.linalg.norm(direction, axis=-1, keepdims=True)


def sample_equirect(image: np.ndarray, direction: np.ndarray) -> np.ndarray:
    """Bilinearly sample an equirectangular map for an arbitrary direction grid."""
    height, width, _ = image.shape
    direction = direction / np.maximum(
        np.linalg.norm(direction, axis=-1, keepdims=True), 1e-12
    )
    longitude = np.arctan2(direction[..., 2], direction[..., 0])
    latitude = np.arccos(np.clip(direction[..., 1], -1.0, 1.0))

    fx = ((longitude / (2.0 * math.pi) + 0.5) % 1.0) * width - 0.5
    fy = latitude / math.pi * height - 0.5

    x0_unwrapped = np.floor(fx).astype(np.int32)
    y0 = np.floor(fy).astype(np.int32)
    tx = (fx - x0_unwrapped)[..., None]
    ty = (fy - y0)[..., None]
    x0 = x0_unwrapped % width
    x1 = (x0_unwrapped + 1) % width
    y0 = np.clip(y0, 0, height - 1)
    y1 = np.clip(y0 + 1, 0, height - 1)

    top = image[y0, x0] * (1.0 - tx) + image[y0, x1] * tx
    bottom = image[y1, x0] * (1.0 - tx) + image[y1, x1] * tx
    return top * (1.0 - ty) + bottom * ty


def radical_inverse_vdc(bits: int) -> float:
    bits = ((bits << 16) | (bits >> 16)) & 0xFFFFFFFF
    bits = ((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1)
    bits = ((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2)
    bits = ((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4)
    bits = ((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8)
    return bits * 2.3283064365386963e-10


def tangent_frame(normal: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    use_x = np.abs(normal[..., 1]) > 0.999
    up = np.zeros_like(normal)
    up[..., 1] = 1.0
    up[use_x] = (1.0, 0.0, 0.0)
    tangent = np.cross(up, normal)
    tangent /= np.maximum(np.linalg.norm(tangent, axis=-1, keepdims=True), 1e-12)
    bitangent = np.cross(normal, tangent)
    return tangent, bitangent


def local_to_world(
    local: np.ndarray,
    tangent: np.ndarray,
    bitangent: np.ndarray,
    normal: np.ndarray,
) -> np.ndarray:
    return (
        tangent * local[0]
        + bitangent * local[1]
        + normal * local[2]
    )


def prefilter_radiance_face(
    source: np.ndarray,
    face: int,
    size: int,
    roughness: float,
    sample_count: int,
    convolution_clamp: float,
) -> np.ndarray:
    normal = cube_directions(face, size)
    if roughness <= 1e-6:
        return sample_equirect(source, normal)

    tangent, bitangent = tangent_frame(normal)
    accumulated = np.zeros_like(normal)
    weight_sum = np.zeros(normal.shape[:-1] + (1,), dtype=np.float32)

    # The Disney/UE convention maps perceptual roughness to GGX alpha=r^2.
    alpha = max(roughness * roughness, 1e-4)
    alpha2 = alpha * alpha
    for i in range(sample_count):
        xi1 = (i + 0.5) / sample_count
        xi2 = radical_inverse_vdc(i)
        phi = 2.0 * math.pi * xi1
        cos_theta = math.sqrt((1.0 - xi2) / (1.0 + (alpha2 - 1.0) * xi2))
        sin_theta = math.sqrt(max(0.0, 1.0 - cos_theta * cos_theta))
        half_vector = np.array(
            [math.cos(phi) * sin_theta, math.sin(phi) * sin_theta, cos_theta],
            dtype=np.float32,
        )
        h_world = local_to_world(half_vector, tangent, bitangent, normal)
        # V=N for split-sum environment prefiltering.
        light = 2.0 * np.sum(normal * h_world, axis=-1, keepdims=True) * h_world - normal
        ndotl = np.maximum(np.sum(normal * light, axis=-1, keepdims=True), 0.0)
        radiance = np.minimum(sample_equirect(source, light), convolution_clamp)
        accumulated += radiance * ndotl
        weight_sum += ndotl

    return accumulated / np.maximum(weight_sum, 1e-6)


def convolve_irradiance_face(
    source: np.ndarray,
    face: int,
    size: int,
    sample_count: int,
    convolution_clamp: float,
) -> np.ndarray:
    normal = cube_directions(face, size)
    tangent, bitangent = tangent_frame(normal)
    accumulated = np.zeros_like(normal)

    # Cosine-weighted sampling: the estimator directly yields irradiance / pi.
    for i in range(sample_count):
        xi1 = (i + 0.5) / sample_count
        xi2 = radical_inverse_vdc(i)
        radius = math.sqrt(xi1)
        phi = 2.0 * math.pi * xi2
        local = np.array(
            [
                math.cos(phi) * radius,
                math.sin(phi) * radius,
                math.sqrt(max(0.0, 1.0 - xi1)),
            ],
            dtype=np.float32,
        )
        direction = local_to_world(local, tangent, bitangent, normal)
        accumulated += np.minimum(
            sample_equirect(source, direction), convolution_clamp
        )

    return accumulated / sample_count


def rgba16f(rgb: np.ndarray) -> bytes:
    alpha = np.ones(rgb.shape[:-1] + (1,), dtype=np.float32)
    return np.concatenate((rgb, alpha), axis=-1).astype("<f2").tobytes()


def write_dds_cube(
    path: Path, size: int, mip_count: int, face_mips: list[list[np.ndarray]]
) -> None:
    ddsd_caps = 0x1
    ddsd_height = 0x2
    ddsd_width = 0x4
    ddsd_pitch = 0x8
    ddsd_pixel_format = 0x1000
    ddsd_mipmap_count = 0x20000
    flags = ddsd_caps | ddsd_height | ddsd_width | ddsd_pitch | ddsd_pixel_format
    if mip_count > 1:
        flags |= ddsd_mipmap_count

    ddpf_fourcc = 0x4
    dds_caps_texture = 0x1000
    dds_caps_complex = 0x8
    dds_caps_mipmap = 0x400000
    caps = dds_caps_texture
    if mip_count > 1:
        caps |= dds_caps_complex | dds_caps_mipmap
    dds_caps2_cubemap_all_faces = 0xFE00

    header = bytearray()
    header += b"DDS "
    header += struct.pack(
        "<7I11I",
        124,
        flags,
        size,
        size,
        size * 8,
        0,
        mip_count,
        *([0] * 11),
    )
    header += struct.pack("<II4sIIIII", 32, ddpf_fourcc, b"DX10", 0, 0, 0, 0, 0)
    header += struct.pack("<5I", caps, dds_caps2_cubemap_all_faces, 0, 0, 0)
    # DXGI_FORMAT_R16G16B16A16_FLOAT, TEXTURE2D, RESOURCE_MISC_TEXTURECUBE.
    header += struct.pack("<5I", 10, 3, 4, 1, 0)
    if len(header) != 148:
        raise AssertionError(len(header))

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as stream:
        stream.write(header)
        for face in range(6):
            for mip in range(mip_count):
                stream.write(rgba16f(face_mips[face][mip]))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path, help="Equirectangular Radiance .hdr")
    parser.add_argument("output_prefix", type=Path, help="Output path without _lod/_irr")
    parser.add_argument("--radiance-size", type=int, default=256)
    parser.add_argument("--irradiance-size", type=int, default=64)
    parser.add_argument("--specular-samples", type=int, default=64)
    parser.add_argument("--diffuse-samples", type=int, default=256)
    parser.add_argument(
        "--convolution-clamp",
        type=float,
        default=32.0,
        help="Clamp only convolved samples to avoid Monte Carlo sun fireflies",
    )
    args = parser.parse_args()

    if args.radiance_size <= 0 or args.radiance_size & (args.radiance_size - 1):
        parser.error("--radiance-size must be a positive power of two")
    if args.irradiance_size <= 0 or args.irradiance_size & (args.irradiance_size - 1):
        parser.error("--irradiance-size must be a positive power of two")

    source = load_radiance_hdr(args.input)
    mip_count = int(math.log2(args.radiance_size)) + 1
    radiance: list[list[np.ndarray]] = [[] for _ in range(6)]
    for face in range(6):
        for mip in range(mip_count):
            size = max(1, args.radiance_size >> mip)
            roughness = mip / max(1, mip_count - 1)
            print(
                f"[radiance] face {face + 1}/6 mip {mip + 1}/{mip_count} "
                f"{size}x{size} roughness={roughness:.3f}",
                flush=True,
            )
            radiance[face].append(
                prefilter_radiance_face(
                    source,
                    face,
                    size,
                    roughness,
                    args.specular_samples,
                    args.convolution_clamp,
                )
            )

    irradiance: list[list[np.ndarray]] = [[] for _ in range(6)]
    for face in range(6):
        print(
            f"[irradiance] face {face + 1}/6 "
            f"{args.irradiance_size}x{args.irradiance_size}",
            flush=True,
        )
        irradiance[face].append(
            convolve_irradiance_face(
                source,
                face,
                args.irradiance_size,
                args.diffuse_samples,
                args.convolution_clamp,
            )
        )

    lod_path = args.output_prefix.with_name(args.output_prefix.name + "_lod.dds")
    irr_path = args.output_prefix.with_name(args.output_prefix.name + "_irr.dds")
    write_dds_cube(lod_path, args.radiance_size, mip_count, radiance)
    write_dds_cube(irr_path, args.irradiance_size, 1, irradiance)
    print(f"[done] {lod_path}")
    print(f"[done] {irr_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
