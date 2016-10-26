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

// 初始化环境配置
bool Init(cxxclean::CxxCleanOptionsParser &optionParser, int argc, const char **argv)
{
	llvm::sys::PrintStackTraceOnErrorSignal("");

	llvm::InitializeNativeTarget();								// 初始化当前平台环境
	llvm::InitializeNativeTargetAsmParser();					// 支持解析asm

	// 解析命令行参数
	bool ok = optionParser.ParseOptions(argc, argv);
	return ok;
}

// 开始分析
void Run(const cxxclean::CxxCleanOptionsParser &optionParser)
{
	ClangTool tool(optionParser.getCompilations(), cxxclean::Project::instance.m_cpps);
	tool.clearArgumentsAdjusters();
	tool.appendArgumentsAdjuster(getClangSyntaxOnlyAdjuster());

	optionParser.AddCleanVsArgument(cxxclean::VsProject::instance, tool);

	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-fcxx-exceptions",			ArgumentInsertPosition::BEGIN));
	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-nobuiltininc",				ArgumentInsertPosition::BEGIN));	// 禁止使用clang内置的头文件
	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-w",						ArgumentInsertPosition::BEGIN));	// 禁用警告
	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-Wno-everything",			ArgumentInsertPosition::BEGIN));	// 禁用任何警告，比-w级别高
	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-ferror-limit=5",			ArgumentInsertPosition::BEGIN));	// 限制单个cpp产生的编译错误数，超过则不再编译
	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-fpermissive",				ArgumentInsertPosition::BEGIN));	// 对不某些不符合标准的行为，允许编译通过，即对标准做降级处理

	DiagnosticOptions diagnosticOptions;
	diagnosticOptions.ShowOptionNames = 1;
	tool.setDiagnosticConsumer(new cxxclean::CxxcleanDiagnosticConsumer(&diagnosticOptions)); // 注意：这里用new没关系，会被释放

	// 对每个文件进行语法分析
	std::unique_ptr<FrontendActionFactory> factory = newFrontendActionFactory<cxxclean::CxxCleanAction>();
	tool.run(factory.get());
}

int main(int argc, const char **argv)
{
	// 命令行解析器
	cxxclean::CxxCleanOptionsParser optionParser;

	// 初始化
	if (!Init(optionParser, argc, argv))
	{
		return 0;
	}

	Log("-- now = " << timetool::get_now() << " --!");

	// 1. 分析每个文件，并打印统计日志
	Run(optionParser);

	cxxclean::ProjectHistory::instance.Fix();
	cxxclean::ProjectHistory::instance.Print();

	// 2. 第2遍开始清理
	if (!cxxclean::Project::instance.m_isOnlyNeed1Step)
	{
		cxxclean::ProjectHistory::instance.m_isFirst	= false;
		cxxclean::ProjectHistory::instance.g_fileNum	= 0;

		Run(optionParser);
	}

	Log("-- now = " << timetool::get_now() << " --!");
	Log("-- finished --!");
	return 0;
}