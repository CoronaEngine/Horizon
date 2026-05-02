#include <iostream>

#include <SPIRV/GlslangToSpv.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
// #include <glslang/Include/ResourceLimits.h>

#include <spirv_cross.hpp>
#include <spirv_glsl.hpp>
#include <spirv_hlsl.hpp>
#include <spirv_msl.hpp>
#include <spirv_parser.hpp>

#include "ShaderLanguageConverter.h"

#include "spirv-tools/linker.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <set>
#include <utility>

#ifdef WIN32
#include <atlbase.h>
#include <dxcapi.h>
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace EmbeddedShader
{

	class Includer : public glslang::TShader::Includer
	{
	public:
		Includer() = default;

		IncludeResult* includeLocal(const char* includeName,const char* includerName,size_t inclusionDepth) override
		{
			std::string content;
			for (const auto& path: includePaths)
			{
				std::ifstream f(path / includeName);
				if (!f.is_open()) continue;
				content = std::string((std::istreambuf_iterator(f)),
								 std::istreambuf_iterator<char>());
			}
			if (content.empty()) return nullptr;

			auto storage = new std::string(std::move(content));
			return new IncludeResult(includeName, storage->data(), storage->size(), storage);
		}
		IncludeResult* includeSystem(const char* n, const char* i, size_t d) override
		{
			return includeLocal(n,i,d);
		}
		void releaseInclude(IncludeResult* r) override
		{
			if (!r) return;
			delete static_cast<std::string*>(r->userData);
			delete r;
		}

		std::vector<std::filesystem::path> includePaths;
	};

	std::vector<uint32_t> ShaderLanguageConverter::glslangSpirvCompiler(
		const std::string& shaderCode, ShaderLanguage inputLanguage, ShaderStage inputStage, const std::vector<std::filesystem::
		path>& includePaths, bool isLink)
	{
		// GLSL version is default by 460
		// Higher versions are compatible with lower versions
		// Version in HLSL is disabled
		Includer a;
		std::vector<uint32_t> resultSpirvCode;

		glslang::EShSource shaderLang;
		switch (inputLanguage)
		{
			case ShaderLanguage::GLSL:
				shaderLang = glslang::EShSourceGlsl;
				break;
			case ShaderLanguage::HLSL:
				shaderLang = glslang::EShSourceHlsl;
				break;
			default:
				return resultSpirvCode;
		}

		EShLanguage stage = EShLangVertex;
		switch (inputStage)
		{
			case ShaderStage::VertexShader:
				stage = EShLangVertex;
				break;
			case ShaderStage::FragmentShader:
				stage = EShLangFragment;
				break;
			case ShaderStage::ComputeShader:
				stage = EShLangCompute;
				break;
			default:
				return resultSpirvCode;
		}

		std::vector<const char*> shaderSources;
		shaderSources.push_back(shaderCode.c_str());

		glslang::InitializeProcess();

		glslang::TShader shader(stage);
		shader.setStrings(shaderSources.data(), 1);
		shader.setEnvInput(shaderLang, stage, glslang::EShClientVulkan, 460);
		shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_4);
		shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);

		if (isLink) shader.setEntryPoint("main");

		Includer includer;
		includer.includePaths = includePaths;
		if (!shader.parse(GetDefaultResources(), 460, false, EShMsgDefault, includer))
		{
			std::cerr << shader.getInfoLog();
			return resultSpirvCode;
		}

		glslang::TIntermediate* intermediate = nullptr;
		if (isLink)
		{
		    glslang::TProgram program;
			program.addShader(&shader);
			if (!program.link(EShMsgVulkanRules))
			{
				std::cerr << program.getInfoLog();
				return resultSpirvCode;
			}

			if (!program.buildReflection(EShReflectionAllBlockVariables | EShReflectionIntermediateIO))
			{
				// std::cout << "build Reflection Error" << std::endl;
			} else
			{
				// std::cout << program.getNumLiveUniformBlocks() << std::endl;
				// program.dumpReflection();
			}

			intermediate = program.getIntermediate(stage);

		    glslang::GlslangToSpv(*intermediate, resultSpirvCode);

		}
	    else
		{
		    intermediate = shader.getIntermediate();

		    glslang::GlslangToSpv(*intermediate, resultSpirvCode);
		}

		glslang::FinalizeProcess();

		return resultSpirvCode;
	}

	std::string ShaderLanguageConverter::spirvCrossConverter(std::vector<uint32_t> spirv_file,
	                                                         ShaderLanguage targetLanguage, int32_t targetVersion)
	{
		std::string resultCode = "";

		try
		{
			switch (targetLanguage)
			{
				case ShaderLanguage::GLSL:
					// case ShaderLanguage::ESSL:
				{
					spirv_cross::CompilerGLSL compiler(spirv_file);

					spirv_cross::CompilerGLSL::Options opts = compiler.get_common_options();
					opts.enable_420pack_extension = false;
					opts.version = 460;
					opts.vulkan_semantics = true;
					opts.es = false;
					compiler.set_common_options(opts);

					resultCode = compiler.compile();
					break;
				}
				case ShaderLanguage::HLSL:
				{
					spirv_cross::CompilerHLSL compiler(spirv_file);

					spirv_cross::CompilerHLSL::Options opts = compiler.get_hlsl_options();
					opts.shader_model = 67;
					compiler.set_hlsl_options(opts);

					resultCode = compiler.compile();
					break;
				}
				default:
					break;
			}
		} catch (const spirv_cross::CompilerError& error)
		{
			std::cout << error.what();
		}

		return resultCode;
	}

	// 辅助函数：将SPIRType转换为类型名字符串
	static std::string spirTypeToString(const spirv_cross::Compiler& compiler, const spirv_cross::SPIRType& type)
	{
		std::string result;

		switch (type.basetype)
		{
			case spirv_cross::SPIRType::Void:
				result = "void";
				break;
			case spirv_cross::SPIRType::Boolean:
				result = "bool";
				break;
			case spirv_cross::SPIRType::SByte:
				result = "int8_t";
				break;
			case spirv_cross::SPIRType::UByte:
				result = "uint8_t";
				break;
			case spirv_cross::SPIRType::Short:
				result = "int16_t";
				break;
			case spirv_cross::SPIRType::UShort:
				result = "uint16_t";
				break;
			case spirv_cross::SPIRType::Int:
				result = "int";
				break;
			case spirv_cross::SPIRType::UInt:
				result = "uint";
				break;
			case spirv_cross::SPIRType::Int64:
				result = "int64_t";
				break;
			case spirv_cross::SPIRType::UInt64:
				result = "uint64_t";
				break;
			case spirv_cross::SPIRType::Half:
				result = "half";
				break;
			case spirv_cross::SPIRType::Float:
				result = "float";
				break;
			case spirv_cross::SPIRType::Double:
				result = "double";
				break;
			case spirv_cross::SPIRType::Struct:
				result = compiler.get_name(type.self);
				if (result.empty())
					result = "struct_" + std::to_string(type.self);
				break;
			case spirv_cross::SPIRType::Image:
			{
				const auto& imgTexelType = compiler.get_type(type.image.type);
				std::string prefix;
				if (imgTexelType.basetype == spirv_cross::SPIRType::Int ||
				    imgTexelType.basetype == spirv_cross::SPIRType::Short ||
				    imgTexelType.basetype == spirv_cross::SPIRType::SByte)
					prefix = "i";
				else if (imgTexelType.basetype == spirv_cross::SPIRType::UInt ||
				         imgTexelType.basetype == spirv_cross::SPIRType::UShort ||
				         imgTexelType.basetype == spirv_cross::SPIRType::UByte)
					prefix = "u";

				switch (type.image.dim)
				{
					case spv::Dim2D:   result = prefix + (type.image.arrayed ? "image2DArray" : "image2D"); break;
					case spv::Dim3D:   result = prefix + "image3D"; break;
					case spv::DimCube: result = prefix + (type.image.arrayed ? "imageCubeArray" : "imageCube"); break;
					case spv::Dim1D:   result = prefix + "image1D"; break;
					default:           result = prefix + "image2D"; break;
				}
				break;
			}
			case spirv_cross::SPIRType::SampledImage:
			{
				const auto& imgTexelType = compiler.get_type(type.image.type);
				std::string prefix;
				if (imgTexelType.basetype == spirv_cross::SPIRType::Int ||
				    imgTexelType.basetype == spirv_cross::SPIRType::Short ||
				    imgTexelType.basetype == spirv_cross::SPIRType::SByte)
					prefix = "i";
				else if (imgTexelType.basetype == spirv_cross::SPIRType::UInt ||
				         imgTexelType.basetype == spirv_cross::SPIRType::UShort ||
				         imgTexelType.basetype == spirv_cross::SPIRType::UByte)
					prefix = "u";

				switch (type.image.dim)
				{
					case spv::Dim2D:   result = prefix + (type.image.arrayed ? "sampler2DArray" : "sampler2D"); break;
					case spv::Dim3D:   result = prefix + "sampler3D"; break;
					case spv::DimCube: result = prefix + (type.image.arrayed ? "samplerCubeArray" : "samplerCube"); break;
					case spv::Dim1D:   result = prefix + "sampler1D"; break;
					default:           result = prefix + "sampler2D"; break;
				}
				break;
			}
			case spirv_cross::SPIRType::Sampler:
				result = "sampler";
				break;
			case spirv_cross::SPIRType::AccelerationStructure:
				result = "acceleration_structure";
				break;
			case spirv_cross::SPIRType::RayQuery:
				result = "ray_query";
				break;
			default:
				result = "unknown";
				break;
		}

		// 处理向量类型
		if (type.vecsize > 1 && type.columns == 1)
		{
			result = result + std::to_string(type.vecsize);
		}
		// 处理矩阵类型
		else if (type.columns > 1)
		{
			result = result + std::to_string(type.columns) + "x" + std::to_string(type.vecsize);
		}

		// 处理数组类型
		for (auto& dim: type.array)
		{
			if (dim == 0)
				result += "[]";
			else
				result += "[" + std::to_string(dim) + "]";
		}

		return result;
	}

	std::vector<IRReflection> ShaderLanguageConverter::spirvCrossGetIRReflection(
		const std::vector<uint32_t>& spirv_file)
	{
		std::vector<IRReflection> irReflections;

		if (spirv_file.empty())
			return irReflections;

		try
		{
			// 使用spirv_cross::Parser来解析SPIR-V并获取IR
			spirv_cross::Parser parser(spirv_file);
			parser.parse();

			// 获取解析后的IR
			spirv_cross::ParsedIR& ir = parser.get_parsed_ir();

			// 使用Compiler来获取更多信息（类型名称等）
			spirv_cross::Compiler compiler(ir);

			// 获取入口点列表
			/*           auto entryPoints = compiler.get_entry_points_and_stages();
			           std::set<std::string> entryPointNames;
			           for (const auto& ep : entryPoints)
			           {
			               entryPointNames.insert(ep.name);
			           }*/

			// 重新解析以获取IR（因为之前的ir被move了）
			// spirv_cross::Parser parser2(spirv_file);
			// parser2.parse();

			// 遍历所有ID，查找函数
			for (size_t i = 0; i < ir.ids.size(); ++i)
			{
				auto& idHolder = ir.ids[i];
				auto type = idHolder.get_type();
				if (type == spirv_cross::TypeFunction)
				{
					const auto& func = idHolder.get<spirv_cross::SPIRFunction>();
					FunctionSignature sig;

					// 获取函数名称
					auto nameIt = ir.meta.find(func.self);
					if (nameIt != ir.meta.end() && !nameIt->second.decoration.alias.empty())
						sig.name = nameIt->second.decoration.alias.substr(0,nameIt->second.decoration.alias.find('('));
					else continue;

					// 获取返回类型
					const auto& funcType = ir.ids[func.function_type].get<spirv_cross::SPIRFunctionPrototype>();
					sig.returnTypeId = funcType.return_type;

					if (sig.returnTypeId != 0 && ir.ids[sig.returnTypeId].get_type() == spirv_cross::TypeType)
					{
						const auto& returnType = ir.ids[sig.returnTypeId].get<spirv_cross::SPIRType>();
						sig.returnTypeName = spirTypeToString(compiler, returnType);
					} else
					{
						sig.returnTypeName = "void";
					}

					// 获取函数参数
					for (size_t j = 0; j < func.arguments.size(); ++j)
					{
						const auto& arg = func.arguments[j];
						VariableInfo param;

						// 获取参数名称
						auto argNameIt = ir.meta.find(arg.id);
						if (argNameIt != ir.meta.end() && !argNameIt->second.decoration.alias.empty())
							param.name = argNameIt->second.decoration.alias;
						else
							param.name = "param_" + std::to_string(j);

						// 获取参数类型
						param.typeId = arg.type;
						if (ir.ids[arg.type].get_type() == spirv_cross::TypeType)
						{
							const auto& argType = ir.ids[arg.type].get<spirv_cross::SPIRType>();
							param.typeName = spirTypeToString(compiler, argType);
						} else
						{
							param.typeName = "unknown";
						}

						sig.parameters.push_back(std::move(param));
					}

					IRReflection reflection;
					reflection.type = IRReflection::Type::FunctionSignature;
					reflection.info = std::move(sig);

					irReflections.push_back(std::move(reflection));
				}

				if (type == spirv_cross::TypeType)
				{
					const auto& spvType = idHolder.get<spirv_cross::SPIRType>();
					if (spvType.basetype != spirv_cross::SPIRType::Struct) continue;

					StructInfo inf;
					auto it = ir.meta.find(spvType.self);
					if (it != ir.meta.end() && !it->second.decoration.alias.empty())
						inf.name = it->second.decoration.alias;
					else continue;

					inf.members.resize(spvType.member_types.size());
					for (size_t memberIndex = 0; memberIndex < spvType.member_types.size(); ++memberIndex)
					{
						auto& member = inf.members[memberIndex];
						member.name = compiler.get_member_name(spvType.self, memberIndex);
						member.typeId = spvType.member_types[memberIndex];
						member.typeName = spirTypeToString(compiler, compiler.get_type(member.typeId));
					}

					IRReflection reflection;
					reflection.type = IRReflection::Type::Struct;
					reflection.info = std::move(inf);

					irReflections.push_back(std::move(reflection));
				}
			}
		} catch (const spirv_cross::CompilerError& error)
		{
			std::cerr << "SPIRV-Cross error while getting function signatures: " << error.what() << std::endl;
		}

		//查重
		for (auto comp1 = irReflections.begin(); comp1 != irReflections.end(); ++comp1)
		{
			for (auto comp2 = comp1 + 1; comp2 != irReflections.end();)
			{
				if (comp1->type == comp2->type)
				{
					switch (comp1->type)
					{
						case IRReflection::Type::Unknown:
							break;
						case IRReflection::Type::FunctionSignature:
						{
							auto& sig1 = std::get<FunctionSignature>(comp1->info);
							auto& sig2 = std::get<FunctionSignature>(comp2->info);
							if (sig1.name == sig2.name)
							{
								comp2 = irReflections.erase(comp2);
								continue;
							}
						}
						break;
						case IRReflection::Type::Struct:
						{
							auto& struct1 = std::get<StructInfo>(comp1->info);
							auto& struct2 = std::get<StructInfo>(comp2->info);
							if (struct1.name == struct2.name)
							{
								comp2 = irReflections.erase(comp2);
								continue;
							}
						}
						break;
					}
				}
				++comp2;
			}
		}

		return irReflections;
	}

	void diagnoseIfNeeded(slang::IBlob* diagnosticsBlob)
	{
		if (diagnosticsBlob != nullptr)
		{
			std::cout << static_cast<const char*>(diagnosticsBlob->getBufferPointer()) << std::endl;
		}
	}

	std::string ShaderLanguageConverter::slangCompiler(std::string shaderCode, ShaderLanguage targetLanguage,
	                                                   Slang::ComPtr<slang::IComponentType>& program)
	{
		std::string result;
		Slang::ComPtr<slang::IGlobalSession> slangGlobalSession;
		slang::createGlobalSession(slangGlobalSession.writeRef());
		slang::SessionDesc sessionDesc = {};
		slang::TargetDesc targetDesc = {};
		switch (targetLanguage)
		{
			case ShaderLanguage::GLSL:
				// case ShaderLanguage::ESSL:
			{
				targetDesc.format = SLANG_GLSL;
				slangGlobalSession->findProfile("glsl_460");
				break;
			}
			case ShaderLanguage::HLSL:
			{
				targetDesc.format = SLANG_HLSL;
				slangGlobalSession->findProfile("sm_6_7");
				break;
			}
			// case ShaderLanguage::SpirV: {
			//     targetDesc.format = SLANG_SPIRV;
			//     slangGlobalSession->findProfile("spirv_1_6");
			//     targetDesc.flags = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;
			//     break;
			// }
			// case ShaderLanguage::MSL:
			//	targetDesc.format = SLANG_METAL; break;
			// case ShaderLanguage::DXIL:
			//	targetDesc.format = SLANG_DXIL; break;
			default:
				return result;
				break;
		}
		sessionDesc.targets = &targetDesc;
		sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
		sessionDesc.targetCount = 1;
		Slang::ComPtr<slang::ISession> session;
		(slangGlobalSession->createSession(sessionDesc, session.writeRef()));
		slang::IModule* slangModule = nullptr; {
			Slang::ComPtr<slang::IBlob> diagnosticBlob;
			// slangModule = session->loadModule(shaderCode.c_str(), diagnosticBlob.writeRef());
			slangModule = session->loadModuleFromSourceString(
				std::to_string(std::hash<std::string>()(shaderCode)).c_str(), "", shaderCode.c_str(),
				diagnosticBlob.writeRef());
		}
		Slang::ComPtr<slang::IEntryPoint> entryPoint;
		slangModule->findEntryPointByName("main", entryPoint.writeRef());
		std::vector<slang::IComponentType*> componentTypes;
		componentTypes.push_back(slangModule);
		componentTypes.push_back(entryPoint);
		Slang::ComPtr<slang::IComponentType> composedProgram; {
			Slang::ComPtr<slang::IBlob> diagnosticsBlob;
			SlangResult result = session->createCompositeComponentType(
				componentTypes.data(), componentTypes.size(), composedProgram.writeRef(), diagnosticsBlob.writeRef());
		}
		Slang::ComPtr<slang::IComponentType> linkedProgram; {
			Slang::ComPtr<slang::IBlob> diagnosticsBlob;
			SlangResult result = composedProgram->link(
				linkedProgram.writeRef(),
				diagnosticsBlob.writeRef());
			if (SLANG_FAILED(result))
				return {};
		}
		Slang::ComPtr<slang::IBlob> spirvCode; {
			Slang::ComPtr<slang::IBlob> diagnosticsBlob;
			SlangResult result = linkedProgram->getEntryPointCode(0, 0, spirvCode.writeRef(),
			                                                      diagnosticsBlob.writeRef());
			if (SLANG_FAILED(result))
				return {};
		}
		result.resize(spirvCode->getBufferSize() / sizeof(char));
		memcpy(result.data(), spirvCode->getBufferPointer(), spirvCode->getBufferSize());
		program = linkedProgram;
		return result;
	}

	std::vector<ShaderCodeModule::ShaderResources> ShaderLanguageConverter::slangCompiler(const std::string &shaderCode,
                                                                                          const std::vector<ShaderLanguage> &targetBinary, const std::vector<ShaderLanguage> &targetLanguage,
                                                                                          std::vector<std::vector<uint32_t>> &binaryTargetsOutput,
                                                                                          std::vector<std::string> &targetsOutput, bool isEnabledReflection, bool isEnabledLink)
	{
	    if (targetBinary.empty() && targetLanguage.empty())
	    {
	        throw std::logic_error("No target language specified for Slang compilation.");
	    }
	    Slang::ComPtr<slang::IGlobalSession> globalSession;
	    createGlobalSession(globalSession.writeRef());

	    slang::SessionDesc sessionDesc = {};

	    std::vector<slang::TargetDesc> targets(targetLanguage.size() + targetBinary.size());

	    for (size_t i = 0; i < targetBinary.size(); ++i)
	    {
	        auto& target = targets[i];
	        auto language = targetBinary[i];
	        switch (language)
	        {
	        case ShaderLanguage::SpirV:
	            target.format = SLANG_SPIRV;
	            target.flags = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;
	            break;
	        case ShaderLanguage::DXIL:
	            target.format = SLANG_DXIL;
	            target.profile = globalSession->findProfile("sm_6_6");
	            break;
	        case ShaderLanguage::DXBC:
	            target.format = SLANG_DXBC;
	            break;
	        default:
	            throw std::logic_error("Unsupported binary target for Slang compilation.");
	        }
	    }

	    for (size_t i = 0; i < targetLanguage.size(); ++i)
	    {
	        auto& target = targets[i + targetBinary.size()];
	        auto language = targetLanguage[i];
	        switch (language)
	        {
	        case ShaderLanguage::GLSL:
	            // case ShaderLanguage::ESSL:
	        {
	            target.format = SLANG_GLSL;
	            break;
	        }
	        case ShaderLanguage::HLSL:
	        {
	            target.format = SLANG_HLSL;
	            break;
	        }
	            // case ShaderLanguage::MSL:
	            //	targetDesc.format = SLANG_METAL; break;
	        case ShaderLanguage::DXIL:
	            target.format = SLANG_DXIL;
	            break;
	        case ShaderLanguage::DXBC:
	            target.format = SLANG_DXBC;
	            break;
	        default:
	            throw std::logic_error("Unsupported target language for Slang compilation.");
	        }
	    }

	    sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
	    sessionDesc.targets = targets.data();
	    sessionDesc.targetCount = static_cast<SlangInt>(targets.size());

	    std::array options =
	    {
	        slang::CompilerOptionEntry{
	            slang::CompilerOptionName::EmitSpirvDirectly,
                {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
	        },
            slang::CompilerOptionEntry{
                slang::CompilerOptionName::BindlessSpaceIndex,
                {slang::CompilerOptionValueKind::Int, 0, 0, nullptr, nullptr}
            },
            slang::CompilerOptionEntry{
                slang::CompilerOptionName::NoMangle,
                {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
            },
	        slang::CompilerOptionEntry{
	            slang::CompilerOptionName::IncompleteLibrary,
                {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
	        },
        };
	    sessionDesc.compilerOptionEntries = options.data();
	    sessionDesc.compilerOptionEntryCount = options.size();

	    Slang::ComPtr<slang::ISession> session;
	    globalSession->createSession(sessionDesc, session.writeRef());

	    // 3. Load module
	    Slang::ComPtr<slang::IModule> slangModule; {
	        auto hashStr = std::to_string(std::hash<std::string>()(shaderCode));
	        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
	        slangModule = session->loadModuleFromSourceString(hashStr.c_str(), (hashStr + ".slang").c_str(),
                                                              shaderCode.c_str(),
                                                              diagnosticsBlob.writeRef());
	        // Optional diagnostic container
	        diagnoseIfNeeded(diagnosticsBlob);
	        if (!slangModule)
	        {
	            throw std::runtime_error("Failed to load Slang module.");
	        }
	    }

	    // 4. Query Entry Points
	    Slang::ComPtr<slang::IEntryPoint> entryPoint; {
	        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
	        slangModule->findEntryPointByName("main", entryPoint.writeRef());
	        if (!entryPoint)
	        {
	            std::cout << "Error getting entry point" << std::endl;
	            throw std::runtime_error("Failed to find entry point 'main' in Slang module.");
	        }
	    }

	    // 5. Compose Modules + Entry Points
	    std::array<slang::IComponentType*, 2> componentTypes =
	    {
	        slangModule,
            entryPoint
        };

	    Slang::ComPtr<slang::IComponentType> composedProgram; {
	        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
	        SlangResult result = session->createCompositeComponentType(
                componentTypes.data(),
                componentTypes.size(),
                composedProgram.writeRef(),
                diagnosticsBlob.writeRef());
	        diagnoseIfNeeded(diagnosticsBlob);
	        if (SLANG_FAILED(result))
	            throw std::runtime_error("Failed to create composite component type in Slang.");
	    }

	    Slang::ComPtr<slang::IComponentType> program = composedProgram;
	    if (isEnabledLink) {
	        // 6. Link
	        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
	        SlangResult result = composedProgram->link(
            program.writeRef(),
            diagnosticsBlob.writeRef());
	        diagnoseIfNeeded(diagnosticsBlob);
	        if (SLANG_FAILED(result))
	            throw std::runtime_error("Failed to link Slang program.");
	    }

		binaryTargetsOutput.resize(targetBinary.size());
		for (size_t i = 0; i < binaryTargetsOutput.size(); ++i)
		{
			Slang::ComPtr<slang::IBlob> targetCodeBlob;
			Slang::ComPtr<slang::IBlob> diagnosticsBlob;
			SlangResult result = program->getEntryPointCode(
			    0,
				static_cast<SlangInt>(i),
				targetCodeBlob.writeRef(),
				diagnosticsBlob.writeRef());
			diagnoseIfNeeded(diagnosticsBlob);
			if (SLANG_FAILED(result))
				throw std::runtime_error("Failed to get target code from Slang program.");
			if (targetCodeBlob)
			{
				binaryTargetsOutput[i].resize(targetCodeBlob->getBufferSize() / sizeof(uint32_t));
				memcpy(binaryTargetsOutput[i].data(), targetCodeBlob->getBufferPointer(),
				       targetCodeBlob->getBufferSize());
			}
		}

		targetsOutput.resize(targetLanguage.size());
		for (size_t i = 0; i < targetsOutput.size(); ++i)
		{
			Slang::ComPtr<slang::IBlob> targetCodeBlob;
			Slang::ComPtr<slang::IBlob> diagnosticsBlob;
		    SlangResult result = program->getEntryPointCode(
                0,
				static_cast<SlangInt>(i + targetBinary.size()),
				// Skip the first entry point if there are binary targets
				targetCodeBlob.writeRef(),
				diagnosticsBlob.writeRef());
			diagnoseIfNeeded(diagnosticsBlob);
			if (SLANG_FAILED(result))
				throw std::runtime_error("Failed to get target code from Slang program.");
			if (targetCodeBlob)
			{
				targetsOutput[i].resize(targetCodeBlob->getBufferSize() / sizeof(char));
				memcpy(targetsOutput[i].data(), targetCodeBlob->getBufferPointer(), targetCodeBlob->getBufferSize());
			}
		}

		if (!isEnabledReflection)
			return {};

		std::vector<ShaderCodeModule::ShaderResources> reflectedResources(targets.size());
		for (size_t i = 0; i < targets.size(); ++i)
		{
			auto& reflection = reflectedResources[i];
			auto programLayout = composedProgram->getLayout(static_cast<SlangInt>(i));

			slangReflectParameterBlock(programLayout, "global_ubo", reflection);

			for (int ii = 0; ii < programLayout->getEntryPointCount(); ++ii)
			{
				auto input = programLayout->getEntryPointByIndex(ii)->getVarLayout();
				auto inputType = input->getTypeLayout();
				//仅针对Helicon Shader特殊处理，简化反射流程
				inputType->getFieldCount();
				for (int j = 0; j < inputType->getFieldCount(); ++j)
				{
					auto param = inputType->getFieldByIndex(j);
					slangReflectField(param, "", 0, reflection);
				}
			}
		}
		return reflectedResources;
	}

	std::vector<uint32_t> ShaderLanguageConverter::slangSpirvCompiler(const std::string& shaderCode,
	                                                                  Slang::ComPtr<slang::IComponentType>& program)
	{
		std::vector<uint32_t> result;
		// 1. Create Global Session
		Slang::ComPtr<slang::IGlobalSession> globalSession;
		createGlobalSession(globalSession.writeRef());

		// 2. Create Session
		slang::SessionDesc sessionDesc = {};
		slang::TargetDesc targetDesc = {};
		targetDesc.format = SLANG_SPIRV;
		// targetDesc.profile = globalSession->findProfile("spirv_1_6");
		targetDesc.flags = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;

		sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
		sessionDesc.targets = &targetDesc;
		sessionDesc.targetCount = 1;

		std::array options =
		{
			slang::CompilerOptionEntry{
				slang::CompilerOptionName::EmitSpirvDirectly,
				{slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
			},
			slang::CompilerOptionEntry{
				slang::CompilerOptionName::BindlessSpaceIndex,
				{slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
			}
		};
		sessionDesc.compilerOptionEntries = options.data();
		sessionDesc.compilerOptionEntryCount = options.size();

		Slang::ComPtr<slang::ISession> session;
		globalSession->createSession(sessionDesc, session.writeRef());

		// 3. Load module
		Slang::ComPtr<slang::IModule> slangModule; {
			auto hashStr = std::to_string(std::hash<std::string>()(shaderCode));
			Slang::ComPtr<slang::IBlob> diagnosticsBlob;
			slangModule = session->loadModuleFromSourceString(hashStr.c_str(), (hashStr + ".slang").c_str(),
			                                                  shaderCode.c_str(),
			                                                  diagnosticsBlob.writeRef());
			// Optional diagnostic container
			diagnoseIfNeeded(diagnosticsBlob);
			if (!slangModule)
			{
				return {};
			}
		}

		// 4. Query Entry Points
		Slang::ComPtr<slang::IEntryPoint> entryPoint; {
			Slang::ComPtr<slang::IBlob> diagnosticsBlob;
			slangModule->findEntryPointByName("main", entryPoint.writeRef());
			if (!entryPoint)
			{
				std::cout << "Error getting entry point" << std::endl;
				return {};
			}
		}

		// 5. Compose Modules + Entry Points
		std::array<slang::IComponentType*, 2> componentTypes =
		{
			slangModule,
			entryPoint
		};

		Slang::ComPtr<slang::IComponentType> composedProgram; {
			Slang::ComPtr<slang::IBlob> diagnosticsBlob;
			SlangResult result = session->createCompositeComponentType(
				componentTypes.data(),
				componentTypes.size(),
				composedProgram.writeRef(),
				diagnosticsBlob.writeRef());
			diagnoseIfNeeded(diagnosticsBlob);
			if (SLANG_FAILED(result))
				return {};
		}

		// 6. Link
		Slang::ComPtr<slang::IComponentType> linkedProgram; {
			Slang::ComPtr<slang::IBlob> diagnosticsBlob;
			SlangResult result = composedProgram->link(
				linkedProgram.writeRef(),
				diagnosticsBlob.writeRef());
			diagnoseIfNeeded(diagnosticsBlob);
			if (SLANG_FAILED(result))
				return {};
		}

		// 7. Get Target Kernel Code
		Slang::ComPtr<slang::IBlob> spirvCode; {
			Slang::ComPtr<slang::IBlob> diagnosticsBlob;
			SlangResult result = linkedProgram->getEntryPointCode(
				0,
				0,
				spirvCode.writeRef(),
				diagnosticsBlob.writeRef());
			diagnoseIfNeeded(diagnosticsBlob);
			if (SLANG_FAILED(result))
				return {};
		}
		result.resize(spirvCode->getBufferSize() / sizeof(uint32_t));
		memcpy(result.data(), spirvCode->getBufferPointer(), spirvCode->getBufferSize());
		program = linkedProgram;
		return result;
	}
#ifdef WIN32
	std::vector<uint32_t> ShaderLanguageConverter::dxilCompiler(const std::string& hlslShader, ShaderStage stage)
	{
		CComPtr<IDxcUtils> pUtils;
		CComPtr<IDxcCompiler3> pCompiler;
		DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&pUtils));
		DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&pCompiler));

		std::wstring_view targetName;
		switch (stage)
		{
			case ShaderStage::VertexShader:
				targetName = L"vs_6_6";
				break;
			case ShaderStage::FragmentShader:
				targetName = L"ps_6_6";
				break;
			case ShaderStage::ComputeShader:
				targetName = L"cs_6_6";
				break;
			default:
				throw std::runtime_error("Unknown shader stage");
		}
		LPCWSTR args[] =
		{
			L"Helicon", // Optional shader source file name for error reporting
			// and for PIX shader source view.
			L"-E", L"main", // Entry point.
			L"-T", targetName.data(), // Target.
#ifdef CABBAGE_ENGINE_DEBUG
			L"-Zs", // Enable debug information (slim format)
#endif
		};

		//
		// Open source file.
		//
		CComPtr<IDxcBlobEncoding> pSource = nullptr;
		pUtils->CreateBlob(hlslShader.data(), hlslShader.size() * sizeof(char), DXC_CP_ACP, &pSource);
		DxcBuffer Source;
		Source.Ptr = pSource->GetBufferPointer();
		Source.Size = pSource->GetBufferSize();
		Source.Encoding = DXC_CP_ACP; // Assume BOM says UTF8 or UTF16 or this is ANSI text.

		//
		// Compile it with specified arguments.
		//
		CComPtr<IDxcResult> pResults;
		pCompiler->Compile(
			&Source, // Source buffer.
			args, // Array of pointers to arguments.
			std::size(args), // Number of arguments.
			nullptr, // User-provided interface to handle #include directives (optional).
			IID_PPV_ARGS(&pResults) // Compiler output status, buffer, and errors.
		);

		//
		// Print errors if present.
		//
		CComPtr<IDxcBlobUtf8> pErrors = nullptr;
		pResults->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&pErrors), nullptr);
		// Note that d3dcompiler would return null if no errors or warnings are present.
		// IDxcCompiler3::Compile will always return an error buffer, but its length
		// will be zero if there are no warnings or errors.
		if (pErrors != nullptr && pErrors->GetStringLength() != 0)
			wprintf(L"DXC Warnings and Errors:\n%S\n", pErrors->GetStringPointer());

		HRESULT hrStatus;
		pResults->GetStatus(&hrStatus);
		if (FAILED(hrStatus))
			throw std::runtime_error("Dxc Compilation Failed");

		CComPtr<IDxcBlob> pShader = nullptr;
		CComPtr<IDxcBlobUtf16> pShaderName = nullptr;
		pResults->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pShader), &pShaderName);
		if (!pShader)
			throw std::runtime_error("Dxc Compilation Failed");

		std::vector<uint32_t> result(pShader->GetBufferSize() / sizeof(uint32_t));
		memcpy(result.data(), pShader->GetBufferPointer(), pShader->GetBufferSize());
		return result;
	}

	std::vector<uint32_t> ShaderLanguageConverter::dxbcCompiler(const std::string& hlslShader, ShaderStage stage)
	{
		UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef CABBAGE_ENGINE_DEBUG
		flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES;
#endif

		std::string_view targetName;
		switch (stage)
		{
			case ShaderStage::VertexShader:
				targetName = "vs_5_1";
				break;
			case ShaderStage::FragmentShader:
				targetName = "ps_5_1";
				break;
			case ShaderStage::ComputeShader:
				targetName = "cs_5_1";
				break;
			default:
				throw std::runtime_error("Unknown shader stage");
		}

		CComPtr<ID3DBlob> code;
		CComPtr<ID3DBlob> error;
		D3DCompile(hlslShader.data(), hlslShader.size() * sizeof(char), "Helicon", nullptr, nullptr, "main",
		           targetName.data(),
		           flags, 0, &code, &error);
		if (error && error->GetBufferSize() != 0)
		{
			printf("D3DCompiler Warnings and Errors:\n%s\n", static_cast<const char*>(error->GetBufferPointer()));
			throw std::runtime_error("D3DCompiler Compilation Failed");
		}

		std::vector<uint32_t> result(code->GetBufferSize() / sizeof(uint32_t));
		memcpy(result.data(), code->GetBufferPointer(), code->GetBufferSize());
		return result;
	}
#endif

	// get Reflected Bind Info
	ShaderCodeModule::ShaderResources ShaderLanguageConverter::spirvCrossReflectedBindInfo(
		std::vector<uint32_t> spirv_file, ShaderLanguage targetLanguage, int32_t targetVersion)
	{
		ShaderCodeModule::ShaderResources result = {};
		spirv_cross::ShaderResources res{};

		spirv_cross::CompilerGLSL* compiler{};
		switch (targetLanguage)
		{
			case ShaderLanguage::GLSL:
			{
				compiler = new spirv_cross::CompilerGLSL(std::move(spirv_file));

				spirv_cross::CompilerGLSL::Options opts = compiler->get_common_options();
				opts.enable_420pack_extension = false;
				if (targetVersion > 0)
				{
					opts.version = targetVersion;
				}
				// opts.es = (targetLanguage == ShaderLanguage::ESSL);
				opts.es = false;
				compiler->set_common_options(opts);
				res = compiler->get_shader_resources();
				break;
			}
			case ShaderLanguage::HLSL:
			{
				auto hlsl_compiler = new spirv_cross::CompilerHLSL{std::move(spirv_file)};
				compiler = hlsl_compiler;
				auto hlsl_options = hlsl_compiler->get_hlsl_options();
				hlsl_options.shader_model = 67;
				hlsl_compiler->set_hlsl_options(hlsl_options);

				res = compiler->get_shader_resources();
				break;
			}
			default:
				throw std::runtime_error("unsupported shader language");
		}

		for (auto& item: res.uniform_buffers)
		{
			ShaderCodeModule::ShaderResources::ShaderBindInfo bindInfo = {};

			bindInfo.variateName = item.name;
			bindInfo.typeName = "uniform";
			bindInfo.elementCount = compiler->get_type((uint64_t) item.base_type_id).member_types.size();
			bindInfo.typeSize = (uint32_t) compiler->get_declared_struct_size(compiler->get_type(item.base_type_id));

			bindInfo.set = compiler->get_decoration(item.id, spv::DecorationDescriptorSet);
			bindInfo.binding = compiler->get_decoration(item.id, spv::DecorationBinding);
			bindInfo.location = compiler->get_decoration(item.id, spv::DecorationLocation);

			bindInfo.bindType = ShaderCodeModule::ShaderResources::uniformBuffers;
			result.bindInfoPool.push_back(bindInfo);

			// 记录 UBO 的总大小和名称
			result.uniformBufferSize = bindInfo.typeSize;
			result.uniformBufferName = item.name;

			// 为 UBO 的每个成员生成独立的 ShaderBindInfo（类似 pushConstantMembers）
			const auto& uboType = compiler->get_type(item.base_type_id);
			for (size_t memberIdx = 0; memberIdx < uboType.member_types.size(); ++memberIdx)
			{
				ShaderCodeModule::ShaderResources::ShaderBindInfo memberInfo = {};
				memberInfo.variateName = compiler->get_member_name(item.base_type_id, memberIdx);
				memberInfo.byteOffset = compiler->type_struct_member_offset(uboType, memberIdx);
				memberInfo.typeSize = (uint32_t) compiler->get_declared_struct_member_size(uboType, memberIdx);
				memberInfo.bindType = ShaderCodeModule::ShaderResources::uniformBufferMembers;
				memberInfo.set = bindInfo.set;
				memberInfo.binding = bindInfo.binding;
				result.bindInfoPool.push_back(memberInfo);
			}
		}

		auto appendDescriptorBindInfo = [&](const spirv_cross::Resource& item,
		                                    ShaderCodeModule::ShaderResources::BindType bindType)
		{
			ShaderCodeModule::ShaderResources::ShaderBindInfo bindInfo = {};

			bindInfo.variateName = item.name.empty() ? compiler->get_name(item.id) : item.name;
			bindInfo.typeName = spirTypeToString(*compiler, compiler->get_type(item.base_type_id));

			bindInfo.set = compiler->get_decoration(item.id, spv::DecorationDescriptorSet);
			bindInfo.binding = compiler->get_decoration(item.id, spv::DecorationBinding);
			bindInfo.location = compiler->get_decoration(item.id, spv::DecorationLocation);

			bindInfo.bindType = bindType;
			result.bindInfoPool.push_back(bindInfo);
		};

		for (auto& item: res.sampled_images)
		{
			appendDescriptorBindInfo(item, ShaderCodeModule::ShaderResources::sampledImages);
		}

		for (auto& item: res.separate_images)
		{
			appendDescriptorBindInfo(item, ShaderCodeModule::ShaderResources::texture);
		}

		for (auto& item: res.separate_samplers)
		{
			appendDescriptorBindInfo(item, ShaderCodeModule::ShaderResources::sampler);
		}

		for (auto& item: res.storage_images)
		{
			appendDescriptorBindInfo(item, ShaderCodeModule::ShaderResources::storageTexture);
		}

		for (auto& item: res.storage_buffers)
		{
			appendDescriptorBindInfo(item, ShaderCodeModule::ShaderResources::storageBuffer);
		}

		for (auto& item: res.stage_inputs)
		{
			ShaderCodeModule::ShaderResources::ShaderBindInfo bindInfo = {};

			bindInfo.variateName = item.name;

			const spirv_cross::SPIRType& base_type = compiler->get_type(item.base_type_id);
			bindInfo.elementCount = base_type.vecsize * base_type.columns;
			bindInfo.typeSize = 4 * base_type.vecsize * base_type.columns;

			switch (base_type.basetype)
			{
				case spirv_cross::SPIRType::Float:
					bindInfo.typeName = "float";
					break;
				case spirv_cross::SPIRType::UInt:
					bindInfo.typeName = "uint";
					break;
				case spirv_cross::SPIRType::Int:
					bindInfo.typeName = "int";
					break;
				default:
					break;
			}

			bindInfo.set = compiler->get_decoration(item.id, spv::DecorationDescriptorSet);
			bindInfo.binding = compiler->get_decoration(item.id, spv::DecorationBinding);
			bindInfo.location = compiler->get_decoration(item.id, spv::DecorationLocation);

			bindInfo.bindType = ShaderCodeModule::ShaderResources::stageInputs;

			result.bindInfoPool.push_back(bindInfo);
		}

		for (auto& item: res.stage_outputs)
		{
			ShaderCodeModule::ShaderResources::ShaderBindInfo bindInfo = {};

			bindInfo.variateName = item.name;

			const spirv_cross::SPIRType& base_type = compiler->get_type(item.base_type_id);
			bindInfo.elementCount = base_type.vecsize * base_type.columns;
			bindInfo.typeSize = 4 * base_type.vecsize * base_type.columns;

			switch (base_type.basetype)
			{
				case spirv_cross::SPIRType::Float:
					bindInfo.typeName = "float";
					break;
				case spirv_cross::SPIRType::UInt:
					bindInfo.typeName = "uint";
					break;
				case spirv_cross::SPIRType::Int:
					bindInfo.typeName = "int";
					break;
				default:
					break;
			}

			bindInfo.set = compiler->get_decoration(item.id, spv::DecorationDescriptorSet);
			bindInfo.binding = compiler->get_decoration(item.id, spv::DecorationBinding);
			bindInfo.location = compiler->get_decoration(item.id, spv::DecorationLocation);

			bindInfo.bindType = ShaderCodeModule::ShaderResources::stageOutputs;

			result.bindInfoPool.push_back(bindInfo);
		}

		for (auto& item: res.push_constant_buffers)
		{
			result.pushConstantName = item.name;
			result.pushConstantSize = (uint32_t) compiler->get_declared_struct_size(
				compiler->get_type((uint64_t) item.base_type_id));

			// obtain all the push constant member
			spirv_cross::SmallVector<spirv_cross::BufferRange> ranges = compiler->get_active_buffer_ranges(item.id);
			for (auto& range: ranges)
			{
				ShaderCodeModule::ShaderResources::ShaderBindInfo bindInfo = {};
				bindInfo.typeSize = (uint32_t) range.range;
				bindInfo.byteOffset = (uint32_t) range.offset;
				bindInfo.variateName = compiler->get_member_name(item.base_type_id, range.index);

				bindInfo.bindType = ShaderCodeModule::ShaderResources::pushConstantMembers;

				result.bindInfoPool.push_back(bindInfo);
			}
		}

		delete compiler;
		return result;
	}

    std::vector<uint32_t> ShaderLanguageConverter::spirvLinker(const std::vector<std::vector<uint32_t>> &binaries)
    {
	    std::vector<uint32_t> result;
	    spvtools::Link(spvToolContext,binaries, &result);
	    return result;
    }

    void ShaderLanguageConverter::slangReflectField(slang::VariableLayoutReflection* field, std::string_view accessPath,
	                                                size_t varBaseOffset, ShaderCodeModule::ShaderResources& reflection)
	{
		auto type = field->getTypeLayout();
		auto name = accessPath.empty() ? field->getName() : accessPath.data() + std::string(".") + field->getName();

		int set = 0;

		if (type->getKind() == slang::TypeReflection::Kind::Struct ||
		    type->getKind() == slang::TypeReflection::Kind::ParameterBlock)
		{
			for (uint32_t i = 0; i < type->getFieldCount(); ++i)
			{
				auto innerField = type->getFieldByIndex(i);
				slangReflectField(innerField, name, varBaseOffset + field->getOffset(), reflection);
			}
		} else slangReflectDescriptor(field, set, name, varBaseOffset, reflection);
	}

	void ShaderLanguageConverter::slangReflectParameterBlock(slang::ProgramLayout* program, std::string_view uboName,
	                                                         ShaderCodeModule::ShaderResources& reflection)
	{
		for (uint32_t i = 0; i < program->getParameterCount(); ++i)
		{
			auto type = program->getParameterByIndex(i)->getTypeLayout()->getElementTypeLayout();
			auto index = type->findFieldIndexByName(uboName.data());
			if (index == -1)
				continue;
			auto ubo = type->getFieldByIndex(index);
			auto uboType = ubo->getTypeLayout()->getElementTypeLayout();
			slangReflectDescriptor(ubo, 0, uboName, 0, reflection);
			for (uint32_t j = 0; j < uboType->getFieldCount(); ++j)
			{
				auto field = uboType->getFieldByIndex(j);
				slangReflectField(field, "", 0, reflection);
			}
		}
	}

	void ShaderLanguageConverter::slangReflectDescriptor(slang::VariableLayoutReflection* var,
	                                                     int set, std::string_view name, size_t varBaseOffset,
	                                                     ShaderCodeModule::ShaderResources& resource)
	{
		auto type = var->getTypeLayout();
		auto rangeCount = type->getDescriptorSetDescriptorRangeCount(set);
		if (rangeCount > 0)
		{
			ShaderCodeModule::ShaderResources::ShaderBindInfo bindInfo;
			bindInfo.set = set;
			bindInfo.binding = type->getBindingRangeFirstDescriptorRangeIndex(0);
			bindInfo.typeName = type->getName();
			bindInfo.typeSize = type->getSize();
			bindInfo.variateName = var->getName();
			switch (type->getDescriptorSetDescriptorRangeType(set, 0))
			{
				case slang::BindingType::ConstantBuffer:
					bindInfo.bindType = ShaderCodeModule::ShaderResources::uniformBuffers;
					bindInfo.typeSize = type->getElementTypeLayout()->getSize();
					break;
				case slang::BindingType::Texture:
					bindInfo.bindType = ShaderCodeModule::ShaderResources::texture;
					break;
				case slang::BindingType::Sampler:
					bindInfo.bindType = ShaderCodeModule::ShaderResources::sampler;
					break;
				case slang::BindingType::MutableTexture:
					bindInfo.bindType = ShaderCodeModule::ShaderResources::storageTexture;
					break;
				case slang::BindingType::MutableRawBuffer:
					bindInfo.bindType = ShaderCodeModule::ShaderResources::storageBuffer;
					break;
				case slang::BindingType::RawBuffer:
					bindInfo.bindType = ShaderCodeModule::ShaderResources::rawBuffer;
					break;
				case slang::BindingType::CombinedTextureSampler:
					bindInfo.bindType = ShaderCodeModule::ShaderResources::sampledImages;
					break;
				case slang::BindingType::Unknown:
				default: return;
			}
			resource.bindInfoPool.push_back(bindInfo);
			return;
		}

		ShaderCodeModule::ShaderResources::ShaderBindInfo bindInfo;
		bindInfo.bindType = ShaderCodeModule::ShaderResources::none;
		bindInfo.byteOffset = var->getOffset(var->getCategory()) + varBaseOffset;
		bindInfo.typeName = type->getName();
		bindInfo.typeSize = type->getSize(var->getCategory());
		bindInfo.variateName = var->getName();
		bindInfo.location = var->getSemanticName() ? var->getSemanticIndex() : 0;
		bindInfo.semantic = var->getSemanticName() ? var->getSemanticName() : "";
		resource.bindInfoPool.push_back(bindInfo);
	}
}
