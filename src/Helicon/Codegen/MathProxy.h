#pragma once
#include <Codegen/VariateProxy.h>

namespace EmbeddedShader
{
    template<typename T> requires ktm::is_vector_v<T>
    struct VecProxyMap;

    template<typename Type>
    struct SwizzleProxy : VariateProxy<Type>
    {
        using base_type = VariateProxy<Type>;
        using base_type::operator=;
        typename VecProxyMap<Type>::proxy_type operator->() requires (ktm::is_vector_v<Type>) = delete;

        SwizzleProxy(std::shared_ptr<Ast::Value> parent, std::string name) : base_type(Ast::AST::access(std::move(parent),std::move(name),Ast::AST::createType<Type>()))
        {

        }
    };

#define SWIZZLE_DECLARE(dimension,name) SwizzleProxy<ktm::vec<dimension,T>> name();
#define SWIZZLE_DEFINED(className,dimension,name) template<typename T> SwizzleProxy<ktm::vec<dimension,T>> className<T>::name() { return {this->parent,#name}; }

    template<typename T>
    struct Vec2Proxy
    {
        template<typename Type>
        friend struct VariateProxy;

        using value_type = T;
        static constexpr uint32_t dimension = 2;
        VariateProxy<T> x;
        VariateProxy<T> y;

        SWIZZLE_DECLARE(2, xy)
        SWIZZLE_DECLARE(2, yx)
    protected:
        std::shared_ptr<Ast::Value> parent;
    };

    SWIZZLE_DEFINED(Vec2Proxy,2,xy)
    SWIZZLE_DEFINED(Vec2Proxy,2,yx)

    template<typename T>
    struct Vec3Proxy : Vec2Proxy<T>
    {
        static constexpr uint32_t dimension = 3;
        VariateProxy<T> z;
        SWIZZLE_DECLARE(2, xz)
        SWIZZLE_DECLARE(2, yz)
        SWIZZLE_DECLARE(2, zx)
        SWIZZLE_DECLARE(2, zy)

        SWIZZLE_DECLARE(3, xyz)
        SWIZZLE_DECLARE(3, xzy)
        SWIZZLE_DECLARE(3, yxz)
        SWIZZLE_DECLARE(3, yzx)
        SWIZZLE_DECLARE(3, zxy)
        SWIZZLE_DECLARE(3, zyx)
    };

    SWIZZLE_DEFINED(Vec3Proxy,2, xz)
    SWIZZLE_DEFINED(Vec3Proxy,2, yz)
    SWIZZLE_DEFINED(Vec3Proxy,2, zx)
    SWIZZLE_DEFINED(Vec3Proxy,2, zy)

    SWIZZLE_DEFINED(Vec3Proxy,3, xyz)
    SWIZZLE_DEFINED(Vec3Proxy,3, xzy)
    SWIZZLE_DEFINED(Vec3Proxy,3, yxz)
    SWIZZLE_DEFINED(Vec3Proxy,3, yzx)
    SWIZZLE_DEFINED(Vec3Proxy,3, zxy)
    SWIZZLE_DEFINED(Vec3Proxy,3, zyx)

    template<typename T>
    struct Vec4Proxy : Vec3Proxy<T>
    {
        static constexpr uint32_t dimension = 4;
        VariateProxy<T> w;

        SWIZZLE_DECLARE(2, xw)
        SWIZZLE_DECLARE(2, yw)
        SWIZZLE_DECLARE(2, zw)
        SWIZZLE_DECLARE(2, wx)
        SWIZZLE_DECLARE(2, wy)
        SWIZZLE_DECLARE(2, wz)

        SWIZZLE_DECLARE(3, xyw)
        SWIZZLE_DECLARE(3, xwy)
        SWIZZLE_DECLARE(3, xzw)
        SWIZZLE_DECLARE(3, xwz)
        SWIZZLE_DECLARE(3, yxw)
        SWIZZLE_DECLARE(3, ywx)
        SWIZZLE_DECLARE(3, yzw)
        SWIZZLE_DECLARE(3, ywz)
        SWIZZLE_DECLARE(3, zxw)
        SWIZZLE_DECLARE(3, zwx)
        SWIZZLE_DECLARE(3, zyw)
        SWIZZLE_DECLARE(3, zwy)

        SWIZZLE_DECLARE(4, xyzw)
        SWIZZLE_DECLARE(4, xywz)
        SWIZZLE_DECLARE(4, xzyw)
        SWIZZLE_DECLARE(4, xzwy)
        SWIZZLE_DECLARE(4, xwyz)
        SWIZZLE_DECLARE(4, xwzy)

        SWIZZLE_DECLARE(4, yxzw)
        SWIZZLE_DECLARE(4, yxwz)
        SWIZZLE_DECLARE(4, yzxw)
        SWIZZLE_DECLARE(4, yzwx)
        SWIZZLE_DECLARE(4, ywxz)
        SWIZZLE_DECLARE(4, ywzx)

        SWIZZLE_DECLARE(4, zxyw)
        SWIZZLE_DECLARE(4, zxwy)
        SWIZZLE_DECLARE(4, zyxw)
        SWIZZLE_DECLARE(4, zywx)
        SWIZZLE_DECLARE(4, zwxy)
        SWIZZLE_DECLARE(4, zwyx)

        SWIZZLE_DECLARE(4, wxyz)
        SWIZZLE_DECLARE(4, wxzy)
        SWIZZLE_DECLARE(4, wyxz)
        SWIZZLE_DECLARE(4, wyzx)
        SWIZZLE_DECLARE(4, wzxy)
        SWIZZLE_DECLARE(4, wzyx)
    };

    SWIZZLE_DEFINED(Vec4Proxy,2, xw)
    SWIZZLE_DEFINED(Vec4Proxy,2, yw)
    SWIZZLE_DEFINED(Vec4Proxy,2, zw)
    SWIZZLE_DEFINED(Vec4Proxy,2, wx)
    SWIZZLE_DEFINED(Vec4Proxy,2, wy)
    SWIZZLE_DEFINED(Vec4Proxy,2, wz)

    SWIZZLE_DEFINED(Vec4Proxy,3, xyw)
    SWIZZLE_DEFINED(Vec4Proxy,3, xwy)
    SWIZZLE_DEFINED(Vec4Proxy,3, xzw)
    SWIZZLE_DEFINED(Vec4Proxy,3, xwz)
    SWIZZLE_DEFINED(Vec4Proxy,3, yxw)
    SWIZZLE_DEFINED(Vec4Proxy,3, ywx)
    SWIZZLE_DEFINED(Vec4Proxy,3, yzw)
    SWIZZLE_DEFINED(Vec4Proxy,3, ywz)
    SWIZZLE_DEFINED(Vec4Proxy,3, zxw)
    SWIZZLE_DEFINED(Vec4Proxy,3, zwx)
    SWIZZLE_DEFINED(Vec4Proxy,3, zyw)
    SWIZZLE_DEFINED(Vec4Proxy,3, zwy)

    SWIZZLE_DEFINED(Vec4Proxy,4, xyzw)
    SWIZZLE_DEFINED(Vec4Proxy,4, xywz)
    SWIZZLE_DEFINED(Vec4Proxy,4, xzyw)
    SWIZZLE_DEFINED(Vec4Proxy,4, xzwy)
    SWIZZLE_DEFINED(Vec4Proxy,4, xwyz)
    SWIZZLE_DEFINED(Vec4Proxy,4, xwzy)

    SWIZZLE_DEFINED(Vec4Proxy,4, yxzw)
    SWIZZLE_DEFINED(Vec4Proxy,4, yxwz)
    SWIZZLE_DEFINED(Vec4Proxy,4, yzxw)
    SWIZZLE_DEFINED(Vec4Proxy,4, yzwx)
    SWIZZLE_DEFINED(Vec4Proxy,4, ywxz)
    SWIZZLE_DEFINED(Vec4Proxy,4, ywzx)

    SWIZZLE_DEFINED(Vec4Proxy,4, zxyw)
    SWIZZLE_DEFINED(Vec4Proxy,4, zxwy)
    SWIZZLE_DEFINED(Vec4Proxy,4, zyxw)
    SWIZZLE_DEFINED(Vec4Proxy,4, zywx)
    SWIZZLE_DEFINED(Vec4Proxy,4, zwxy)
    SWIZZLE_DEFINED(Vec4Proxy,4, zwyx)

    SWIZZLE_DEFINED(Vec4Proxy,4, wxyz)
    SWIZZLE_DEFINED(Vec4Proxy,4, wxzy)
    SWIZZLE_DEFINED(Vec4Proxy,4, wyxz)
    SWIZZLE_DEFINED(Vec4Proxy,4, wyzx)
    SWIZZLE_DEFINED(Vec4Proxy,4, wzxy)
    SWIZZLE_DEFINED(Vec4Proxy,4, wzyx)

#undef SWIZZLE_DECLARE
#undef SWIZZLE_DEFINED

    template<typename T>
    struct IsVecProxy
    {
        static constexpr bool value = false;
    };

    template<typename T>
    struct IsVecProxy<Vec2Proxy<T>>
    {
        static constexpr bool value = true;
    };

    template<typename T>
    struct IsVecProxy<Vec3Proxy<T>>
    {
        static constexpr bool value = true;
    };

    template<typename T>
    struct IsVecProxy<Vec4Proxy<T>>
    {
        static constexpr bool value = true;
    };

    template<size_t N,typename T>
    struct VecProxy
    {

    };

    template<typename T>
    struct VecProxy<2,T>
    {
        using proxy_type = Vec2Proxy<T>;
    };

    template<typename T>
    struct VecProxy<3,T>
    {
        using proxy_type = Vec3Proxy<T>;
    };

    template<typename T>
    struct VecProxy<4,T>
    {
        using proxy_type = Vec4Proxy<T>;
    };

    template<typename T> requires ktm::is_vector_v<T>
    struct VecProxyMap
    {
        using proxy_type = typename VecProxy<ktm::vec_traits_len<T>,ktm::vec_traits_base_t<T>>::proxy_type;
    };
}