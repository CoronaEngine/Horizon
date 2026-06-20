#pragma once

#include <stack>

#include <Codegen/VariateProxy.h>

namespace EmbeddedShader
{
#define HELICON_MACRO_COMMA_ ,
#define GPU_PUSH_VARIATE_WITH_CONDITION(define,name,condition) \
    for (uint32_t varJ6hF4rT9mK2zV8cX5bN1pQ3{}; varJ6hF4rT9mK2zV8cX5bN1pQ3 < 1 && (condition); ++varJ6hF4rT9mK2zV8cX5bN1pQ3) \
        for (define; name.index < 1; ++name.index)
#define GPU_PUSH_VARIATE(define,name) for (define;name.index < 1; ++name.index)
#define GPU_IF_CONDITION TheIfElseStatementMustBeGuidedByIf.currentIndex++ == TheIfElseStatementMustBeGuidedByIf.maxCount ? (++TheIfElseStatementMustBeGuidedByIf.maxCount,true) : (++TheIfElseStatementMustBeGuidedByIf.lastMaxIndex,false)
#define GPU_IF_DETECTOR(condition) [&]() {\
    if(condition) return true;\
    return false;\
    }

	struct GPU_IF
	{
		int64_t currentIndex = 0;
		int64_t lastMaxIndex = -1;
		int64_t maxCount = 0;

		int8_t index = 0;
	    std::shared_ptr<Ast::IfStatement> node;
	};

	struct GPU_IF_BRANCH
	{
		GPU_IF_BRANCH(GPU_IF& gpuIf,const VariateProxy<bool>& condition, const std::function<bool()>&)
		{
			gpuIf.node = Ast::AST::beginIf(condition.node);
			//if begin pattern
		}

		GPU_IF_BRANCH(GPU_IF& gpuIf,bool condition, std::function<bool()> pruningDetector)
		{
			gpuIf.node = Ast::AST::beginIf(Ast::AST::createValue(condition), std::move(pruningDetector));
			//elseif begin pattern
		}

		~GPU_IF_BRANCH()
		{
			Ast::AST::endIf();
			//if end pattern
		}

		int8_t index = 0;
	};
#define $IF(condition) GPU_PUSH_VARIATE(GPU_IF TheIfElseStatementMustBeGuidedByIf,TheIfElseStatementMustBeGuidedByIf)\
while (TheIfElseStatementMustBeGuidedByIf.currentIndex = 0,TheIfElseStatementMustBeGuidedByIf.lastMaxIndex < TheIfElseStatementMustBeGuidedByIf.maxCount)\
if (GPU_IF_CONDITION)\
GPU_PUSH_VARIATE(GPU_IF_BRANCH gpuIfBranchJ6hF4rT9mK2zV8cX5bN1pQ3{TheIfElseStatementMustBeGuidedByIf HELICON_MACRO_COMMA_ condition HELICON_MACRO_COMMA_ GPU_IF_DETECTOR(condition)}, gpuIfBranchJ6hF4rT9mK2zV8cX5bN1pQ3)

	struct GPU_ELSE_BRANCH
	{
		GPU_ELSE_BRANCH(GPU_IF& gpuIf)
		{
			Ast::AST::beginElse(gpuIf.node);
			//else begin pattern
		}

		~GPU_ELSE_BRANCH()
		{
			Ast::AST::endElse();
			//else end pattern
		}
		int8_t index = 0;
	};
#define $ELSE else GPU_PUSH_VARIATE_WITH_CONDITION(GPU_ELSE_BRANCH gpuElseBranchJ6hF4rT9mK2zV8cX5bN1pQ3{TheIfElseStatementMustBeGuidedByIf},gpuElseBranchJ6hF4rT9mK2zV8cX5bN1pQ3,GPU_IF_CONDITION)

	struct GPU_WHILE_INFO
	{
		bool isDoWhile = false; // 是否为do while循环
		int loopCount = 0; // 循环次数计数
		bool isDoWhileInside = false; // 是否在do while循环内部
		static std::stack<GPU_WHILE_INFO> gpuWhileStack;
	};

	// TODO: 链接错误，防止会被多个 .cpp include
	inline std::stack<GPU_WHILE_INFO> GPU_WHILE_INFO::gpuWhileStack{};

	struct GPU_WHILE
	{
		template<typename Type>
		static bool conditionProcess(VariateProxy<Type> condition)
		{
			auto& gpuWhileStack = GPU_WHILE_INFO::gpuWhileStack;
			if (gpuWhileStack.empty() || gpuWhileStack.top().isDoWhileInside)
			{
				//说明这是一个新的while
				gpuWhileStack.push({});
			}

			auto& top = gpuWhileStack.top();
			if (top.isDoWhile)
			{
				gpuWhileStack.pop();
				/////////////////////do while end/////////////////////
				//xxx
				return false;
			}

			//说明是while
			//while 时，第一次条件为true
			//while 时，第一次条件为false

			++top.loopCount;
			if (top.loopCount == 2)
			{
				gpuWhileStack.pop();
				/////////////////////while end/////////////////////
				return false;
			}
			/////////////////////while begin/////////////////////
			//xxx
			return true;
		}
	};

	struct GPU_DO_WHILE
	{
		GPU_DO_WHILE()
		{
			GPU_WHILE_INFO::gpuWhileStack.push({true, 0, true});
			/////////////////////do while begin/////////////////////
		}

		~GPU_DO_WHILE()
		{
			GPU_WHILE_INFO::gpuWhileStack.top().isDoWhileInside = false;
		}
		int8_t index = 0;
	};

#define $DO do GPU_PUSH_VARIATE(GPU_DO_WHILE gpuDoWhileL8kM3qW5xG1vY7dR4nP9tS2,gpuDoWhileL8kM3qW5xG1vY7dR4nP9tS2)
#define $WHILE(condition) while (GPU_WHILE::conditionProcess(condition))

	struct GPU_FOR
	{
		static void beginCatchCondition()
		{
            //for begin pattern
        }

		GPU_FOR(const std::function<void()>& condition)
		{
			condition();
		}

		~GPU_FOR()
		{
			//...
		}

		struct BREAK_FLAG
		{
			bool breakFlag = true;
			int8_t index = 0;
		};

		struct BREAK_FLAG_PROCESS
		{
			explicit BREAK_FLAG_PROCESS(bool& breakFlag)
            {
                if (breakFlag)
                {
                    beginCatchCondition();
                    breakFlag = false; // Reset the break flag
                }
            }

			int8_t index = 0;
		};
		int8_t index = 0;
	};
#define $FOR(condition) \
	GPU_PUSH_VARIATE(GPU_FOR gpuForL8kM3qW5xG1vY7dR4nP9tS2{[&]{condition;}},gpuForL8kM3qW5xG1vY7dR4nP9tS2)\
		GPU_PUSH_VARIATE(BREAK_FLAG breakFlagJ6hF4rT9mK2zV8cX5bN1pQ3,breakFlagJ6hF4rT9mK2zV8cX5bN1pQ3)\
			for (condition)\
				GPU_PUSH_VARIATE_WITH_CONDITION(GPU_FOR::BREAK_FLAG_PROCESS gpuForBreakFlagProcessU7gD2sH5nB9yR4vM8kL3wZ6{breakFlagJ6hF4rT9mK2zV8cX5bN1pQ3.breakFlag},gpuForBreakFlagProcessU7gD2sH5nB9yR4vM8kL3wZ6,breakFlagJ6hF4rT9mK2zV8cX5bN1pQ3.breakFlag)
#define HELICON_STRINGIZE_(X) #X
#define HLSL(path) HELICON_STRINGIZE_(path.hpp)
#define GLSL(path) HELICON_STRINGIZE_(path.hpp)
}
