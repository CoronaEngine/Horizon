#pragma once
#include <Codegen/VariateProxy.h>
struct frag_glsl {
static inline std::vector<uint32_t> C__Users_12168_Documents_work_space_Horizon_examples_assets_shaders_frag_glsl {119734787,67072,524299,19,0,131089,1,393227,1,1280527431,1685353262,808793134,0,196622,0,1,458767,4,4,1852399981,0,9,12,196624,4,7,196611,2,450,262149,4,1852399981,0,327685,9,1131705711,1919904879,0,327685,12,1734439526,1869377347,114,262215,9,30,0,262215,12,30,0,131091,2,196641,3,2,196630,6,32,262167,7,6,4,262176,8,3,7,262203,8,9,3,262167,10,6,3,262176,11,1,10,262203,11,12,1,262187,6,14,1065353216,327734,2,4,0,3,131320,5,262205,10,13,12,327761,6,15,13,0,327761,6,16,13,1,327761,6,17,13,2,458832,7,18,15,16,17,14,196670,9,18,65789,65592,};
static inline auto& spirv = C__Users_12168_Documents_work_space_Horizon_examples_assets_shaders_frag_glsl;
static inline ::EmbeddedShader::BindingKey fragColor{0, 12, 1, 0};
static inline ::EmbeddedShader::BindingKey outColor{0, 16, 2, 0};
static inline ::EmbeddedShader::FunctionProxy<void()> main{"main","void",{},&C__Users_12168_Documents_work_space_Horizon_examples_assets_shaders_frag_glsl};
static constexpr size_t pushConstantBlockSize = 0;
static constexpr size_t uniformBufferBlockSize = 0;
template<typename P>
struct ResourceBindings {
ResourceBindings(P* p) { (void)p; }
};
template<typename P>
struct OutputBindings {
::EmbeddedShader::BoundField<P> outColor;
OutputBindings(P* p) : outColor(p, 0, 16, 2, 0) {}
};
template<typename P>
struct Bindings : ResourceBindings<P>, OutputBindings<P> {
Bindings(P* p) : ResourceBindings<P>(p), OutputBindings<P>(p) {}
};
}; // struct frag_glsl
