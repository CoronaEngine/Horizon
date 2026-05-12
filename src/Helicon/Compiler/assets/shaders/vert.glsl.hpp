#pragma once
#include <Codegen/VariateProxy.h>
struct vert_glsl {
static inline std::vector<uint32_t> C__Users_12168_Documents_work_space_Horizon_examples_assets_shaders_vert_glsl {119734787,67072,524299,30,0,131089,1,131089,5302,393227,1,1280527431,1685353262,808793134,0,196622,0,1,589839,0,4,1852399981,0,12,17,27,28,196611,2,450,262149,4,1852399981,0,393221,10,1348430951,1700164197,2019914866,0,393222,10,0,1348430951,1953067887,7237481,458758,10,1,1348430951,1953393007,1702521171,0,458758,10,2,1130327143,1148217708,1635021673,6644590,458758,10,3,1130327143,1147956341,1635021673,6644590,196613,12,0,327685,17,1867542121,1769236851,28271,327685,27,1734439526,1869377347,114,262149,28,1866690153,7499628,196679,10,2,327752,10,0,11,0,327752,10,1,11,1,327752,10,2,11,3,327752,10,3,11,4,262215,17,30,0,262215,27,30,0,262215,28,30,1,131091,2,196641,3,2,196630,6,32,262167,7,6,4,196637,8,6,196637,9,6,393246,10,7,6,8,9,262176,11,3,10,262203,11,12,3,262165,13,32,1,262187,13,14,0,262167,15,6,3,262176,16,1,15,262203,16,17,1,262187,6,19,1065353216,262176,24,3,7,262176,26,3,15,262203,26,27,3,262203,16,28,1,327734,2,4,0,3,131320,5,262205,15,18,17,327761,6,20,18,0,327761,6,21,18,1,327761,6,22,18,2,458832,7,23,20,21,22,19,327745,24,25,12,14,196670,25,23,262205,15,29,28,196670,27,29,65789,65592,};
static inline auto& spirv = C__Users_12168_Documents_work_space_Horizon_examples_assets_shaders_vert_glsl;
static inline ::EmbeddedShader::BindingKey inPosition{0, 12, 1, 0};
static inline ::EmbeddedShader::BindingKey inColor{0, 12, 1, 1};
static inline ::EmbeddedShader::BindingKey fragColor{0, 12, 2, 0};
static inline ::EmbeddedShader::FunctionProxy<void()> main{"main","void",{},&C__Users_12168_Documents_work_space_Horizon_examples_assets_shaders_vert_glsl};
struct gl_PerVertex
{
	::EmbeddedShader::VariateProxy<::ktm::fvec4> gl_Position;
	::EmbeddedShader::VariateProxy<float> gl_PointSize;
	::EmbeddedShader::VariateProxy<float[]> gl_ClipDistance;
	::EmbeddedShader::VariateProxy<float[]> gl_CullDistance;
};
static constexpr size_t pushConstantBlockSize = 0;
static constexpr size_t uniformBufferBlockSize = 0;
template<typename P>
struct ResourceBindings {
ResourceBindings(P* p) { (void)p; }
};
template<typename P>
struct OutputBindings {
::EmbeddedShader::BoundField<P> fragColor;
OutputBindings(P* p) : fragColor(p, 0, 12, 2, 0) {}
};
template<typename P>
struct Bindings : ResourceBindings<P>, OutputBindings<P> {
Bindings(P* p) : ResourceBindings<P>(p), OutputBindings<P>(p) {}
};
}; // struct vert_glsl
