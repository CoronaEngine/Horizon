#include <fstream>
#include <iostream>
#include <sstream>
#include <slang.h>
#include <Compiler/ShaderLanguageConverter.h>
void diagnoseIfNeeded(slang::IBlob* diagnosticsBlob)
{
    if (diagnosticsBlob != nullptr)
    {
        std::cout << static_cast<const char*>(diagnosticsBlob->getBufferPointer()) << std::endl;
    }
}

void run_example_include()
{
    std::string_view testShaderPath = "D:/SlangTest/test.glsl";
    std::string_view outPath = "D:/SlangTest/test-via-api.slang-module";
    std::fstream file(testShaderPath.data(),std::ios::in);
    auto shaderCode = (std::stringstream{} << file.rdbuf()).str();
    Slang::ComPtr<slang::ISession> session;
    auto data = EmbeddedShader::ShaderLanguageConverter::slangModuleCompiler(shaderCode, EmbeddedShader::ShaderLanguage::GLSL);
    // Slang::ComPtr<slang::IBlob> irBlob;
    // mod->serialize(irBlob.writeRef());
    // if (!irBlob)
    // {
    //     throw std::runtime_error("Failed to load slang module.");
    // }
    //
    std::fstream outModuleFile(outPath.data(),std::ios::out | std::ios::binary);
    outModuleFile.write(reinterpret_cast<const char *>(data.data()),data.size());
}