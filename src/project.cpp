///<------------------------------------------------------------------------------
//< @file:   project.cpp
//< @author: ������
//< @brief:  ��������c++����������
///<------------------------------------------------------------------------------

#include "project.h"
#include <llvm/Support/raw_ostream.h>
#include "tool.h"
#include "parser.h"
#include "html_log.h"

Project Project::instance;

// ��ӡ����������ļ��б�
void Project::Print() const
{
	if (Project::instance.m_logLvl < LogLvl_Max)
	{
		return;
	}

	HtmlDiv div;
	div.AddTitle(cn_project_text);

	// ���������c++�ļ��б�
	if (!m_canCleanFiles.empty())
	{
		div.AddRow(AddPrintIdx() + ". " + strtool::get_text(cn_project_allow_files, get_number_html(m_canCleanFiles.size()).c_str()));

		for (const string &file : m_canCleanFiles)
		{
			div.AddRow(strtool::get_text(cn_project_allow_file, get_file_html(file.c_str()).c_str()), 2);
		}

		div.AddRow("");
	}

	// �������c++Դ�ļ��б�
	if (!m_cpps.empty())
	{
		div.AddRow(AddPrintIdx() + ". " + strtool::get_text(cn_project_source_list, get_number_html(m_cpps.size()).c_str()));

		for (const string &file : m_cpps)
		{
			const string absoluteFile = pathtool::get_absolute_path(file.c_str());
			div.AddRow(strtool::get_text(cn_project_source, get_file_html(absoluteFile.c_str()).c_str()), 2);
		}

		div.AddRow("");
	}

	HtmlLog::instance->AddDiv(div);
}

// ��ӡ���� + 1
std::string Project::AddPrintIdx() const
{
	return strtool::itoa(++m_printIdx);
}

// ���ļ��Ƿ���������
bool Project::CanClean(const char* filename)
{
	return Has(instance.m_canCleanFiles, tolower(filename));
}

// �Ƿ�Ӧ���Ը��ļ�
bool Project::IsSkip(const char* filename)
{
	for (const std::string &skip : instance.m_skips)
	{
		if (strtool::contain(skip.c_str(), filename))
		{
			return true;
		}
	}

	return false;
}

// �Ƴ���c++��׺��Դ�ļ�
void Project::Fix()
{
	if (m_cpps.size() <= 1)
	{
		// ֻ��һ���ļ�ʱ�������������׺
		return;
	}

	// ��������Ŀ�꣬����c++��׺���ļ����б����Ƴ�
	FileNameVec cpps;

	for (const std::string &cpp : m_cpps)
	{
		if (cpptool::is_cpp(cpp) && CanClean(cpp.c_str()))
		{
			cpps.push_back(cpp);
		}
	}

	m_cpps = cpps;
}