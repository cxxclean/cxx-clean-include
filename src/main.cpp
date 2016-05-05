///<------------------------------------------------------------------------------
//< @file:   main.cpp
//< @author: 洪坤安
//< @date:   2016年1月16日
//< @brief:	 
//< Copyright (c) 2016 game. All rights reserved.
///<------------------------------------------------------------------------------

#include "cxx_clean.h"

#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"

#include "project.h"
#include "vs_project.h"
#include "project_history.h"

int main(int argc, const char **argv)
{
	llvm::sys::PrintStackTraceOnErrorSignal();

	llvm::InitializeNativeTarget();								// 初始化当前平台环境
	llvm::InitializeNativeTargetAsmParser();					// 支持解析asm

	// 解析命令行参数
	cxxcleantool::CxxCleanOptionsParser optionParser;

	bool ok = optionParser.ParseOptions(argc, argv, cxxcleantool::g_optionCategory);
	if (!ok)
	{
		return 0;
	}

	ClangTool tool(optionParser.getCompilations(), cxxcleantool::Project::instance.m_cpps);
	tool.clearArgumentsAdjusters();
	tool.appendArgumentsAdjuster(getClangSyntaxOnlyAdjuster());

	optionParser.AddCleanVsArgument(cxxcleantool::Vsproject::instance, tool);

	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-fcxx-exceptions",			ArgumentInsertPosition::BEGIN));
	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-Winvalid-source-encoding", ArgumentInsertPosition::BEGIN));
	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-Wdeprecated-declarations", ArgumentInsertPosition::BEGIN));
	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-nobuiltininc",				ArgumentInsertPosition::BEGIN));	// 禁止使用clang内置的头文件
	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-w",						ArgumentInsertPosition::BEGIN));	// 禁用警告
	tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-ferror-limit=5",			ArgumentInsertPosition::BEGIN));	// 限制单个cpp产生的编译错误数，超过则不再编译
	
	DiagnosticOptions diagnosticOptions;
	diagnosticOptions.ShowOptionNames = 1;
	tool.setDiagnosticConsumer(new cxxcleantool::CxxcleanDiagnosticConsumer(&diagnosticOptions)); // 注意：这里用new没关系，会被释放

	// 第1遍对每个文件进行分析，然后汇总并打印统计日志
	std::unique_ptr<FrontendActionFactory> factory = newFrontendActionFactory<cxxcleantool::CxxCleanAction>();
	tool.run(factory.get());

	// 打印统计日志
	cxxcleantool::ProjectHistory::instance.Print();

	// 第2遍才开始清理，第2遍就不打印html日志了
	if (cxxcleantool::Project::instance.m_need2Step)
	{
		cxxcleantool::ProjectHistory::instance.m_isFirst		= false;
		cxxcleantool::ProjectHistory::instance.g_printFileNo	= 0;
		tool.run(factory.get());
	}

	// 这里故意打印到err输出
	llvm::errs() << "-- finished --!\n";
	return 0;
}