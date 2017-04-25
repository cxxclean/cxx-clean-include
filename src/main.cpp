//------------------------------------------------------------------------------
// 文件: main.cpp
// 作者: 洪坤安
// 说明: 入口文件
//------------------------------------------------------------------------------

#include "cxx_clean.h"

#include <llvm/Support/Signals.h>
#include <llvm/Support/TargetSelect.h>
#include "project.h"
#include "vs.h"
#include "history.h"
#include "tool.h"
#include "html_log.h"

// 初始化环境配置
bool Init(CxxCleanOptionsParser &optionParser, int argc, const char **argv)
{
	llvm::sys::PrintStackTraceOnErrorSignal("");

	llvm::InitializeNativeTarget();				// 初始化当前平台环境
	llvm::InitializeNativeTargetAsmParser();	// 支持解析asm

	locale &loc=locale::global(locale(locale(),"",LC_CTYPE));  // 不论以输出文件流还是输入文件流，此操作应放在其两边
	locale::global(loc);
	
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

	optionParser.AddClangArgumentByOption(tool);
	optionParser.AddClangArgument(tool, "-fcxx-exceptions");	
	optionParser.AddClangArgument(tool, "-nobuiltininc");		// 禁止使用clang内置的头文件
	optionParser.AddClangArgument(tool, "-w");					// 禁用警告
	optionParser.AddClangArgument(tool, "-Wno-everything");		// 禁用任何警告，比-w级别高
	optionParser.AddClangArgument(tool, "-ferror-limit=5");		// 限制单个cpp产生的编译错误数，超过则不再编译
	optionParser.AddClangArgument(tool, "-fpermissive");		// 对不某些不符合标准的行为，允许编译通过，即对标准做降级处理

	DiagnosticOptions diagnosticOptions;
	diagnosticOptions.ShowOptionNames = 1;
	tool.setDiagnosticConsumer(new CxxcleanDiagnosticConsumer(&diagnosticOptions)); // 注意：这里用new没关系，会被释放

	// 对每个文件进行语法分析
	tool.run(newFrontendActionFactory<CxxCleanAction>().get());

	ProjectHistory::instance.Print();
	HtmlLog::instance.Close();
}

int main(int argc, const char **argv)
{
	Log("-- now = " << timetool::get_now() << " --!");

	// 命令行解析器
	CxxCleanOptionsParser optionParser;

	// 初始化
	if (!Init(optionParser, argc, argv))
	{
		Log("error: option is invalid!");
		return 0;
	}

	// 开始分析并清理
	Run(optionParser);

	Log("-- now = " << timetool::get_now() << " --!");
	Log("-- finished --!");

	// 打开日志
	const std::wstring open_html = L"start " + HtmlLog::instance.m_htmlPath;
	_wsystem(open_html.c_str());
	return 0;
}