//
// Created by Zero on 21/04/2022.
//

#pragma once

#include "math/basic_types.h"

namespace horizon::ast {
using namespace horizon::core;
using namespace horizon::math;

enum struct UnaryOp : uint32_t {
    Positive,
    Negative,
    Not,
    BitNot
};

enum struct CastOp : uint32_t {
    Static,
    Bitwise
};

enum struct BinaryOp : uint32_t {
    // arithmetic
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    BitAnd,
    BitOr,
    BitXor,
    Shl,
    Shr,
    And,
    Or,

    // relational
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
    Equal,
    NotEqual
};

enum struct CallOp : uint32_t {
    Custom,

    All,
    Any,
    None,

    Select,
    Clamp,
    Lerp,
    InverseLerp,

    Abs,
    Min,
    Max,

    IsInf,
    IsNan,

    Acos,
    Asin,
    Atan,
    Acosh,
    Asinh,
    Atanh,
    Atan2,
    Copysign,

    Cos,
    Sin,
    Tan,
    Cosh,
    Sinh,
    Tanh,

    Exp,
    Exp2,
    Exp10,
    Log,
    Log2,
    Log10,
    Pow,
    Fmod,
    Mod,
    Fract,

    Sqrt,
    Rsqrt,
    Sqr,
    Rcp,
    Sign,

    Ceil,
    Floor,
    Round,

    Degrees,
    Radians,
    Saturate,

    Fma,

    Cross,
    Dot,
    Distance,
    DistanceSquared,
    Length,
    LengthSquared,
    Normalize,
    FaceForward,
    CoordinateSystem,
    MakeNormalTangent,

    Determinant,
    Transpose,
    Inverse,

    IsNullBuffer,
    IsNullTexture,

    BufferSize,

    Tex3DSample,
    Tex3DRead,
    Tex3DWrite,

    Tex2DSample,
    Tex2DRead,
    Tex2DWrite,

    ByteBufferRead,
    ByteBufferWrite,
    ByteBufferSize,

    BindlessArrayBufferRead,
    BindlessArrayBufferWrite,
    BindlessArrayBufferSize,
    BindlessArrayByteBufferRead,
    BindlessArrayByteBufferWrite,
    BindlessArrayTex3DSample,
    BindlessArrayTex2DSample,

    MakeBool2,
    MakeBool3,
    MakeBool4,

    MakeInt2,
    MakeInt3,
    MakeInt4,

    MakeUint2,
    MakeUint3,
    MakeUint4,

    MakeUchar2,
    MakeUchar3,
    MakeUchar4,

    MakeFloat2,
    MakeFloat3,
    MakeFloat4,

    MakeReal2,
    MakeReal3,
    MakeReal4,

    MakeHalf2,
    MakeHalf3,
    MakeHalf4,

    MakeUlong2,
    MakeUlong3,
    MakeUlong4,

    MakeFloat2x2,
    MakeFloat2x3,
    MakeFloat2x4,

    MakeFloat3x2,
    MakeFloat3x3,
    MakeFloat3x4,

    MakeFloat4x2,
    MakeFloat4x3,
    MakeFloat4x4,

    MakeReal2x2,
    MakeReal2x3,
    MakeReal2x4,

    MakeReal3x2,
    MakeReal3x3,
    MakeReal3x4,

    MakeReal4x2,
    MakeReal4x3,
    MakeReal4x4,

    MakeHalf2x2,
    MakeHalf2x3,
    MakeHalf2x4,

    MakeHalf3x2,
    MakeHalf3x3,
    MakeHalf3x4,

    MakeHalf4x2,
    MakeHalf4x3,
    MakeHalf4x4,

    Gemm,

    Unreachable,

    AtomicExch,
    AtomicAdd,
    AtomicSub,
    AtomicCas,

    Half2Float,
    Float2Half,

    // ray tracing
    MakeRay,
    RayOffsetOrigin,
    TraceClosest,
    TraceOcclusion,

    SynchronizeBlock,
    WarpActiveCountBits, //WaveActiveCountBits
    WarpActiveBitMask,  //WaveActiveBallot
    WarpPrefixCountBits, //WavePrefixCountBits
    WarpLaneId,
    WarpSize,
    WarpFirstActiveLane,
    WarpIsFirstActiveLane,

    Count
};

}// namespace horizon::ast