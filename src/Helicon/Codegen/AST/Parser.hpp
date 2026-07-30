#pragma once
#include <functional>
#include <memory>
#include <stack>
#include <string>
#include "Struct.hpp"

#include <set>

namespace EmbeddedShader::Generator
{
    class SlangGenerator;
}
namespace EmbeddedShader::Ast
{
    struct DefineLocalVariate;
	struct Variate;
	struct Statement;

    struct ExternBranchVariateCollection
    {
        std::set<const Variate*> variateRefs;
        std::vector<const DefineLocalVariate*> localDefinitions;
    };

	struct ParseOutput
	{
		std::string output;
	    std::unordered_set<SlangModule*> sourceModule;
		ShaderStage stage;
	    std::vector<BranchOutput> branches;
	    std::string typeHeader;
	};

	class Parser
	{
		friend class AST;
		friend class Generator::SlangGenerator;
	public:
		static void beginShaderParse(ShaderStage stage);
		static std::vector<ParseOutput> endPipelineParse();
		static void setBindless(bool bindless);
        static bool getBindless();
        static void setInputPush(bool push);
        static bool getInputPush();
    private:
	    static void endShaderParse();
		Parser() = default;

		void reset();

		EmbeddedShaderStructure structure;
		std::stack<std::vector<std::shared_ptr<Statement>>*> localStatementStack;

		size_t currentVariateIndex = 0;
		size_t currentGlobalVariateIndex = 0;
		size_t currentAggregateTypeIndex = 0;
		size_t nextRenderTargetLocation = 0;
	    size_t currentBranchIndex = 0;

	    bool bInputPush = true;

		std::shared_ptr<Variate> vsPositionOutput;
	    std::shared_ptr<Variate> fsPositionOutput;
		std::shared_ptr<Variate> isFrontFaceOutput;
		std::shared_ptr<Variate> dispatchThreadIDInput;

		std::shared_ptr<Variate> globalUBO;
		std::shared_ptr<Variate> globalParameterBlock;
		std::shared_ptr<Variate> globalPushConstant;

	    std::shared_ptr<Variate> stageInput;
	    std::shared_ptr<Variate> stageOutput;

		std::vector<ParseOutput> parseOutputs;

	    //////////////Branch Pruning///////////////
        std::vector<BranchOutput> branchOutputs;
	    std::stack<ExternBranchVariateCollection> externBranchVar;
	    std::stack<std::vector<size_t>> branchReferences;
	    std::string typeHeader;
	    bool bIsEnabledTypeHeader = false;
	    //////////////Branch Pruning///////////////

		bool isInShaderParse = false;

		bool bindless = false;

        static thread_local std::unique_ptr<Parser> currentParser;
	public:
		static std::string getUniqueVariateName();
		static std::string getUniqueAggregateTypeName();
		static std::string getUniqueGlobalVariateName();
		static const std::vector<std::shared_ptr<Statement>>& getGlobalStatements();
		static size_t getNextRenderTargetLocation();
		static size_t getCurrentBranchIndex();

	    static bool isCollectExternBranchVariate();
	    static void beginCollectExternBranchVariate();
	    static void endCollectExternBranchVariate();
	    static void pushBranchVariateReference(const Variate* var);
	    static void pushBranchLocalVariateDefinition(const DefineLocalVariate* definition);
	    static ExternBranchVariateCollection getCurrentExternBranchVariateCollection();
	    static void resetBranchOutputs();
	    static std::vector<BranchOutput>& getBranchOutputs();
	    static std::string& getTypeHeader();
	    static std::stack<std::vector<size_t>>& getBranchReferences();
	    static bool isEnabledTypeHeader();
	    static void setEnabledTypeHeader(bool enabled);
	};
}
