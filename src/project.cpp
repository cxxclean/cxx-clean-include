///<------------------------------------------------------------------------------
//< @file:   project.cpp
//< @author: 洪坤安
//< @brief:  本次清理c++的任务内容
///<------------------------------------------------------------------------------

#include "project.h"
#include <llvm/Support/raw_ostream.h>
#include "tool.h"
#include "parser.h"
#include "html_log.h"

Project Project::instance;

// 打印本次清理的文件列表
void Project::Print() const
{
	if (Project::instance.m_logLvl < LogLvl_Max)
	{
		return;
	}

	HtmlDiv div;
	div.AddTitle(cn_project_text);

	// 允许清理的c++文件列表
	if (!m_canCleanFiles.empty())
	{
		div.AddRow(AddPrintIdx() + ". " + strtool::get_text(cn_project_allow_files, get_number_html(m_canCleanFiles.size()).c_str()));

		for (const string &file : m_canCleanFiles)
		{
			div.AddRow(strtool::get_text(cn_project_allow_file, get_file_html(file.c_str()).c_str()), 2);
		}

		div.AddRow("");
	}

	// 待清理的c++源文件列表
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

	HtmlLog::instance.AddDiv(div);
}

// 打印索引 + 1
std::string Project::AddPrintIdx() const
{
	return strtool::itoa(++m_printIdx);
}

// 该文件是否允许被清理
bool Project::CanClean(const char* filename)
{
	return Has(instance.m_canCleanFiles, tolower(filename));
}

// 是否应忽略该文件
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

// 移除非c++后缀的源文件
void Project::Fix()
{
	if (m_cpps.size() <= 1)
	{
		// 只有一个文件时，允许有任意后缀
		return;
	}

	// 遍历清理目标，将非c++后缀的文件从列表中移除
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