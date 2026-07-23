// #include  <horizon.h>
// #include "Codegen/BuiltinVariate.h"
// #include "Codegen/ComputePipelineObject.h"
// #include "Codegen/CustomLibrary.h"
// #include "Codegen/TypeAlias.h"
// // #include "shaders/edsl_header.glsl.hpp"

// #include <example_branch_pruning/example_branch_pruning.h>
// #include <Codegen/ControlFlows.h>

// struct BaselineEdslVertexProxy
// {
//     EmbeddedShader::Float3 pos;
//     EmbeddedShader::Float3 color;
//     EmbeddedShader::Float2 tex_coord;
// };


// void run_example_branch_pruning()
// {
//     using namespace EmbeddedShader;
//     using namespace ktm;
//     Texture2D<fvec4> texture_proxy;
//     Texture2D<fvec4> final_output_proxy;
//     Float4x4 model;
//     Float4x4 view;
//     Float4x4 proj;

//     bool option = true;

//     auto vertex_shader = [&](Aggregate<BaselineEdslVertexProxy> vertex) -> Float4 {
//         position() = mul(proj, mul(view, mul(model, Float4(vertex->pos, 1.0f))));
//         Float color_weight = edsl_header_glsl.get_color_weight(vertex->color);
//         return Float4(vertex->tex_coord, color_weight, 1.0f);
//     };

//     auto fragment_shader = [&](Float4 input) {
//         $IF (option)
//         {
//             Float4 color = texture(texture_proxy, input->xy());
//             final_output_proxy << color * Float4(input->z, input->z, input->z, 1.0f);
//         }
//         $ELSE
//         {
//             final_output_proxy << Float4(input->x, input->y, input->z, 1.0f);
//         }
//     };

//     CompilerOption compilerOption;
//     compilerOption.compileDXBC = false;
//     compilerOption.compileDXIL = false;
//     compilerOption.compileGLSL = false;
//     compilerOption.compileSpirV = true;
//     compilerOption.compileHLSL = true;
//     compilerOption.enableBindless = false;

//     auto rasterization = RasterizedPipelineObject::compile(vertex_shader, fragment_shader, compilerOption);
//     std::cout << std::get<1>(rasterization.vertex->getShaderCode(ShaderLanguage::HLSL).shaderCode) << "\n";
//     std::cout << std::get<1>(rasterization.fragment->getShaderCode(ShaderLanguage::HLSL).shaderCode) << "\n";
// }
