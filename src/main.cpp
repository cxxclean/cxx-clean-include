//------------------------------------------------------------------------------
// �ļ�: main.cpp
// ����: ������
// ˵��: ����ļ�
//------------------------------------------------------------------------------

#include "cxx_clean.h"

#include <locale>
#include <llvm/Support/Signals.h>
#include <llvm/Support/TargetSelect.h>
#include "project.h"
#include "vs.h"
#include "history.h"
#include "tool.h"
#include "html_log.h"

// ��ʼ����������
bool Init(CxxCleanOptionsParser &optionParser, int argc, const char **argv)
{
	llvm::sys::PrintStackTraceOnErrorSignal("");

	llvm::InitializeNativeTarget();				// ��ʼ����ǰƽ̨����
	llvm::InitializeNativeTargetAsmParser();	// ֧�ֽ���asm

	locale loc=locale::global(locale(locale(),"",LC_CTYPE));  // ����������ļ������������ļ������˲���Ӧ����������
	locale::global(loc);
	
	// ���������в���
	bool ok = optionParser.ParseOptions(argc, argv);
	return ok;
}

// ��ʼ����
void Run(const CxxCleanOptionsParser &optionParser)
{
	ClangTool tool(optionParser.getCompilations(), Project::instance.m_cpps);
	tool.clearArgumentsAdjusters();
	tool.appendArgumentsAdjuster(getClangSyntaxOnlyAdjuster());

	optionParser.AddClangArgumentByOption(tool);
	optionParser.AddClangArgument(tool, "-fcxx-exceptions");	
	optionParser.AddClangArgument(tool, "-nobuiltininc");		// ��ֹʹ��clang���õ�ͷ�ļ�
	optionParser.AddClangArgument(tool, "-w");					// ���þ���
	optionParser.AddClangArgument(tool, "-Wno-everything");		// �����κξ��棬��-w�����
	optionParser.AddClangArgument(tool, "-ferror-limit=5");		// ���Ƶ���cpp�����ı�����������������ٱ���
	optionParser.AddClangArgument(tool, "-fpermissive");		// �Բ�ĳЩ�����ϱ�׼����Ϊ���������ͨ�������Ա�׼����������

	DiagnosticOptions diagnosticOptions;
	diagnosticOptions.ShowOptionNames = 1;
	tool.setDiagnosticConsumer(new CxxcleanDiagnosticConsumer(&diagnosticOptions)); // ע�⣺������newû��ϵ���ᱻ�ͷ�

	// ��ÿ���ļ������﷨����
	tool.run(newFrontendActionFactory<CxxCleanAction>().get());

	ProjectHistory::instance.Print();
	HtmlLog::instance->Close();
}

int main(int argc, const char **argv)
{
	Log("-- now = " << timetool::get_now() << " --!");

	// �����н�����
	CxxCleanOptionsParser optionParser;

	// ��ʼ��
	if (!Init(optionParser, argc, argv))
	{
		Log("error: option is invalid!");
		return 0;
	}

	// ��ʼ����������
	Run(optionParser);

	Log("-- now = " << timetool::get_now() << " --!");
	Log("-- finished --!");

	// ����־
	const std::wstring open_html = L"start " + HtmlLog::instance->m_htmlPath;
	_wsystem(open_html.c_str());
	return 0;
}