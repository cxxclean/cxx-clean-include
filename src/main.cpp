//------------------------------------------------------------------------------
// 文件: main.cpp
// 作者: 洪坤安
// 说明: 入口文件
// Copyright (c) 2016 game. All rights reserved.
//------------------------------------------------------------------------------

#include "cxx_clean.h"

#include <llvm/Support/Signals.h>
#include <llvm/Support/TargetSelect.h>

#include "project.h"
#include "vs.h"
#include "history.h"
#include "tool.h"

using namespace cxxclean;

// 初始化环境配置
bool Init(CxxCleanOptionsParser &optionParser, int argc, const char **argv)
{
	llvm::sys::PrintStackTraceOnErrorSignal("");

	llvm::InitializeNativeTarget();				// 初始化当前平台环境
	llvm::InitializeNativeTargetAsmParser();	// 支持解析asm

	// 解析命令行参数
	bool ok = optionParser.ParseOptions(argc, argv);
	return ok;
}

// 开始分析
void Run(const CxxCleanOptionsParser &optionParser)
{
	ClangTool tool(optionParser.getCompilations(), Project::instance.m_cpps);
	tool.clearArgumentsAdjusters();
	tool.appendArgumentsAdjuster(getClangSyntaxOnlyAdjuster());

	optionParser.AddCleanVsArgument(VsProject::instance, tool);
	optionParser.AddArgument(tool, "-fcxx-exceptions");
	optionParser.AddArgument(tool, "-nobuiltininc");		// 禁止使用clang内置的头文件
	optionParser.AddArgument(tool, "-w");					// 禁用警告
	optionParser.AddArgument(tool, "-Wno-everything");		// 禁用任何警告，比-w级别高
	optionParser.AddArgument(tool, "-ferror-limit=5");		// 限制单个cpp产生的编译错误数，超过则不再编译
	optionParser.AddArgument(tool, "-fpermissive");			// 对不某些不符合标准的行为，允许编译通过，即对标准做降级处理

	DiagnosticOptions diagnosticOptions;
	diagnosticOptions.ShowOptionNames = 1;
	tool.setDiagnosticConsumer(new CxxcleanDiagnosticConsumer(&diagnosticOptions)); // 注意：这里用new没关系，会被释放

	// 对每个文件进行语法分析
	std::unique_ptr<FrontendActionFactory> factory = newFrontendActionFactory<CxxCleanAction>();
	tool.run(factory.get());
}

// 第1步：分析每个文件
void Run1(const CxxCleanOptionsParser &optionParser)
{
	Run(optionParser);

	ProjectHistory::instance.Fix();
	ProjectHistory::instance.Print();
}

// 第2步：开始清理
void Run2(const CxxCleanOptionsParser &optionParser)
{
	if (!Project::instance.m_isOnlyNeed1Step)
	{
		ProjectHistory::instance.m_isFirst	= false;
		ProjectHistory::instance.g_fileNum	= 0;

		Run(optionParser);
	}
}

int main(int argc, const char **argv)
{
	Log("-- now = " << timetool::get_now() << " --!");

	// 命令行解析器
	CxxCleanOptionsParser optionParser;

	// 初始化
	if (!Init(optionParser, argc, argv))
	{
		return 0;
	}

	// 开始分析并清理
	Run1(optionParser);
	Run2(optionParser);

	Log("-- now = " << timetool::get_now() << " --!");
	Log("-- finished --!");
	return 0;
}