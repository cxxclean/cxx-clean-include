///<------------------------------------------------------------------------------
//< @file:   whole_project.cpp
//< @author: 洪坤安
//< @date:   2016年2月22日
//< @brief:
//< Copyright (c) 2016. All rights reserved.
///<------------------------------------------------------------------------------

#include "project_history.h"

#include "parsing_cpp.h"
#include "project.h"
#include "html_log.h"
#include "tool.h"

namespace cxxcleantool
{
	ProjectHistory ProjectHistory::instance;

	void ProjectHistory::Print() const
	{
		if (m_files.empty())
		{
			return;
		}

		int canCleanFileCount = 0;

		for (auto itr : m_files)
		{
			const FileHistory &fileHistory = itr.second;
			canCleanFileCount += (fileHistory.IsNeedClean() ? 1 : 0);
		}

		HtmlLog::instance.AddBigTitle(cn_project_history_title);

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.Clear();

		div.AddTitle(strtool::get_text(cn_project_history_clean_count,	htmltool::get_number_html(canCleanFileCount).c_str()), 50);
		div.AddTitle(strtool::get_text(cn_project_history_src_count,	htmltool::get_number_html(Project::instance.m_cpps.size()).c_str()), 49);

		PrintUnusedLine();
		PrintCanForwarddeclClass();
		PrintReplace();

		HtmlLog::instance.AddDiv(div);
		HtmlLog::instance.EndLog();
	}

	// 打印索引 + 1
	std::string ProjectHistory::AddPrintIdx() const
	{
		return strtool::itoa(++m_printIdx);
	}

	void ProjectHistory::AddFile(ParsingFile *newFile)
	{
		newFile->MergeTo(m_files);
	}

	void ProjectHistory::PrintUnusedLine() const
	{
		if (m_files.empty())
		{
			return;
		}

		int num = 0;

		for (auto itr : m_files)
		{
			const FileHistory &eachFile = itr.second;
			num += eachFile.m_unusedLines.empty() ? 0 : 1;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". " + strtool::get_text(cn_file_count_unused, htmltool::get_number_html(num).c_str()), 1);

		ParsingFile::PrintUnusedIncludeOfFiles(m_files);
	}

	void ProjectHistory::PrintCanForwarddeclClass() const
	{
		if (m_files.empty())
		{
			return;
		}

		int num = 0;

		for (auto itr : m_files)
		{
			const FileHistory &eachFile = itr.second;
			num += eachFile.m_forwards.empty() ? 0 : 1;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". " + strtool::get_text(cn_file_count_add_forward, htmltool::get_number_html(num).c_str()), 1);

		ParsingFile::PrintCanForwarddeclOfFiles(m_files);
	}

	void ProjectHistory::PrintReplace() const
	{
		if (m_files.empty())
		{
			return;
		}

		int num = 0;

		for (auto itr : m_files)
		{
			const FileHistory &eachFile = itr.second;
			num += eachFile.m_replaces.empty() ? 0 : 1;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(AddPrintIdx() + ". " + strtool::get_text(cn_file_count_can_replace, htmltool::get_number_html(num).c_str()), 1);

		ParsingFile::PrintCanReplaceOfFiles(m_files);
	}

	// 打印文件被使用次数（未完善）
	void ProjectHistory::PrintCount() const
	{
		if (m_files.empty())
		{
			return;
		}

		llvm::outs() << "\n    " << 3 << ". file be #include and be use count : file count = " << m_files.size() << "";

		for (auto fileItr : m_files)
		{
			const string &file		= fileItr.first;
			FileHistory &eachFile	= fileItr.second;

			if (!Project::instance.CanClean(file))
			{
				continue;
			}

			llvm::outs() << "\n        file = {" << file << "} be use num = " << eachFile.m_beUseCount
			             << ", old be include num = " << eachFile.m_oldBeIncludeCount
			             << ", new be include num = " << eachFile.m_newBeIncludeCount;

			if (eachFile.m_beUseCount * 2 < eachFile.m_newBeIncludeCount)
			{
				llvm::outs() << " -------> can move to other c++ file\n";
			}
			else
			{
				llvm::outs() << "\n";
			}
		}
	}
}