#pragma once
#include <Codegen/VariateProxy.h>
#include <Codegen/Generator/SlangGenerator.hpp>

namespace EmbeddedShader::BuiltinVariate
{
    inline VariateProxy<ktm::fvec4> position()
    {
        return VariateProxy<ktm::fvec4>{Ast::AST::getPositionOutput()};
    }

    inline VariateProxy<bool> isFrontFace()
    {
        return VariateProxy<bool>{Ast::AST::getIsFrontFaceOutput()};
    }

    inline VariateProxy<ktm::uvec3> dispatchThreadID()
    {
        return VariateProxy<ktm::uvec3>{Ast::AST::getDispatchThreadIDInput()};
    }

    inline VariateProxy<uint32_t> drawIndex()
    {
        return VariateProxy<uint32_t>{Ast::AST::getDrawIndexInput()};
    }

    // 逐-draw 数据下标：instance_count 恒为 1 时等于 draw 的 first_instance。
    // 相比 drawIndex(),它在 indirect 与非 indirect 路径下语义一致
    // (SV_DrawIndex 在直接 draw 下恒为 0)。
    inline VariateProxy<uint32_t> instanceIndex()
    {
        return VariateProxy<uint32_t>{Ast::AST::getInstanceIndexInput()};
    }

    // vertex pulling 用的全局顶点下标(indexed draw 下含 vertexOffset)。
    inline VariateProxy<uint32_t> vertexIndex()
    {
        return VariateProxy<uint32_t>{Ast::AST::getVertexIndexInput()};
    }
}

namespace EmbeddedShader
{
    using namespace BuiltinVariate;
}