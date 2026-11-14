#pragma once
#include <Codegen/VariateProxy.h>
#include <Codegen/Generator/SlangGenerator.hpp>

namespace EmbeddedShader::BuiltinVariate
{
    // Word offsets address packed vertex attributes and per-draw parameters.
    template<class T>
    VariateProxy<T> rasterLoad(const char* name, uint32_t wordOffset)
    {
        auto type = Ast::AST::createType<T>();
        auto offset = VariateProxy<uint32_t>(wordOffset);
        return {Ast::AST::callFunc(std::string(name) + "<" + type->generate() + ">", type,
            Ast::Node::accessAll({proxy_wrap(offset)}, Ast::AccessPermissions::ReadOnly))};
    }
    template<class T> VariateProxy<T> rasterVertexAttribute(uint32_t wordOffset) {
        return rasterLoad<T>("hzVertexAttribute", wordOffset);
    }
    template<class T> VariateProxy<T> rasterDrawParameter(uint32_t wordOffset) {
        return rasterLoad<T>("hzDrawParameter", wordOffset);
    }
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

    // Vulkan 实例下标包含当前间接命令的 firstInstance。
    inline VariateProxy<uint32_t> instanceIndex()
    {
        return VariateProxy<uint32_t>{Ast::AST::getInstanceIndexInput()};
    }

    // 解码索引并加上当前间接命令的 baseVertex 后得到的顶点下标。
    inline VariateProxy<uint32_t> vertexIndex()
    {
        return VariateProxy<uint32_t>{Ast::AST::getVertexIndexInput()};
    }
}

namespace EmbeddedShader
{
    using namespace BuiltinVariate;
}
