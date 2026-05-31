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

namespace
{
    std::string_view testShaderPath = "D:/SlangTest/test.glsl";
    std::string_view outPath = "D:/SlangTest/test-via-api.slang-module";
}

void testSlangModuleGenerating()
{
    std::fstream file(testShaderPath.data(),std::ios::in);
    auto shaderCode = (std::stringstream{} << file.rdbuf()).str();
    auto data = EmbeddedShader::ShaderLanguageConverter::slangModuleCompiler(shaderCode, EmbeddedShader::ShaderLanguage::GLSL);
    std::fstream outModuleFile(outPath.data(),std::ios::out | std::ios::binary);
    outModuleFile.write(reinterpret_cast<const char *>(data.data()),data.size());
    EmbeddedShader::ShaderLanguageConverter::testSlangModule(data);
}

void testSlangModuleLoading()
{
    std::fstream file(outPath.data(),std::ios::in | std::ios::binary);
    file.seekg(0, std::ios_base::end);
    std::streampos fileSize = file.tellg();
    file.seekg(0, std::ios_base::beg);
    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);

    EmbeddedShader::ShaderLanguageConverter::testSlangModule(data);
}

void run_example_include()
{
    testSlangModuleLoading();
}