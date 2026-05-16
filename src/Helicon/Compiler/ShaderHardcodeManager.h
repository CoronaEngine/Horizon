#pragma once

#include "ShaderCodeCompiler.h"
#include <filesystem>

// 非 Debug 模式下，且编译器支持 __has_include，
// 并且能找到生成的 HardcodeShaders.h 时，才启用 hardcode shader
#if !defined(CABBAGE_ENGINE_DEBUG) && defined(__has_include) && __has_include(<Compiler/HardcodeShaders/HardcodeShaders.h>)
#include <Compiler/HardcodeShaders/HardcodeShaders.h>
#define HELICON_HAS_HARDCODE_SHADERS 1
#else
// 其他情况都不启用
#define HELICON_HAS_HARDCODE_SHADERS 0
#endif

namespace EmbeddedShader
{
	struct ShaderHardcodeManager
	{
		static std::string getItemName(const std::source_location& sourceLocation, ShaderStage inputStage);
		static std::string getItemName(const std::source_location& sourceLocation, ShaderLanguage language);
		static std::string getItemName(const std::string& sourceLocationFormatString, const std::string& prefix);

#ifdef CABBAGE_ENGINE_DEBUG
		static void addTarget(const std::string& shaderCode, const std::string& targetName, const std::string& itemName);
		static void addTarget(const std::vector<uint32_t>& shaderCode, const std::string& targetName, const std::string& itemName);
		static void addTarget(const ShaderCodeModule::ShaderResources& shaderResource, const std::string& targetName, const std::string& itemName);
#endif

		static std::variant<ShaderCodeModule::ShaderResources,std::variant<std::vector<uint32_t>,std::string>> getHardcodeShader(const std::string& targetName, const std::string& itemName);
		static std::string getSourceLocationString(const std::source_location& sourceLocation);
	private:
		static bool hardcodeFileOpened;

		static void createTarget(const std::string& name);
		static void clearOldHardcode();

		struct TargetInfo
		{
			bool isExistTargetItem = false;
			bool isExistTargetFile = false;
		};

		static inline std::unordered_map<std::string, TargetInfo> targetInfos;
        static inline std::filesystem::path hardcodePath = std::string(HELICON_ROOT_PATH) + "/src/Helicon/Compiler/HardcodeShaders/";
		static inline bool isClearOldHardcodeFiles = false;
	};
}
