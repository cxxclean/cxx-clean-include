//------------------------------------------------------------------------------
// 文件: history.cpp
// 作者: 洪坤安
// 说明: 各文件的清理历史
//------------------------------------------------------------------------------

#include "history.h"
#include "parser.h"
#include "project.h"
#include "html_log.h"
#include "tool.h"

ProjectHistory ProjectHistory::instance;

// 打印
void CompileErrorHistory::Print() const
{
	if (errNum <= 0)
	{
		return;
	}

	HtmlDiv &div = HtmlLog::instance.m_newDiv;
	div.AddRow(" ", 1, 100, false, Row_Error);

	div.AddRow(cn_error, 1, 100, false, Row_Error, Grid_Error);

	for (const std::string& errTip : errors)
	{
		div.AddRow(errTip, 2, 100, false, Row_Error);
		div.AddRow(" ", 1, 100, false, Row_Error);
	}

	if (!fatalErrorIds.empty())
	{
		std::string errTexts;

		int i		= 0;
		int size	= fatalErrorIds.size();

		for (int err : fatalErrorIds)
		{
			errTexts += get_number_html(err);

			if (++i < size)
			{
				errTexts += ",";
			}
		}

		std::string tip = get_text(cn_error_fatal, errTexts.c_str());
		div.AddRow(tip, 1, 100, false, Row_Error);
	}

	if (hasTooManyError)
	{
		std::string tip = get_text(cn_error_too_many, get_number_html(errNum).c_str());
		div.AddRow(tip, 1, 100, false, Row_Error);
	}
	else if (fatalErrorIds.empty())
	{
		if (errNum > 0)
		{
			std::string tip = get_text(cn_error_ignore, get_number_html(errNum).c_str());
			div.AddRow(tip, 1, 100, false, Row_Error);
		}
	}

	div.AddRow("");
}

// 打印本文件的清理历史
void FileHistory::Print(int id /* 文件序号 */, bool isPrintCompiliError /* = true */) const
{
	if (!Project::CanClean(m_filename))
	{
		return;
	}

	HtmlDiv &div = HtmlLog::instance.m_newDiv;

	bool isError	= m_compileErrorHistory.HaveFatalError();
	const char *tip = (isError ? cn_file_history_compile_error : cn_file_history);

	div.AddRow(strtool::get_text(tip, get_number_html(id).c_str(), get_file_html(m_filename.c_str()).c_str()),
	           1, 100, false, Row_None, isError ? Grid_Error : Grid_Ok);

	if (isPrintCompiliError)
	{
		m_compileErrorHistory.Print();
	}

	if (m_isSkip)
	{
		// 打印：本文件为预编译头文件，禁止被改动
		div.AddRow(get_warn_html(cn_file_skip), 1);
	}

	PrintUnusedInclude();
	PrintForwardClass();
	PrintReplace();
	PrintAdd();
}

// 打印本文件的可被删#include记录
void FileHistory::PrintUnusedInclude() const
{
	if (m_delLines.empty())
	{
		return;
	}

	HtmlDiv &div = HtmlLog::instance.m_newDiv;

	div.AddRow(strtool::get_text(cn_file_unused_count, get_number_html(m_delLines.size()).c_str()), 2);

	for (auto &delLineItr : m_delLines)
	{
		int line = delLineItr.first;

		const DelLine &delLine = delLineItr.second;

		div.AddRow(strtool::get_text(cn_file_unused_line, get_number_html(line).c_str()), 3, 25);
		div.AddGrid(strtool::get_text(cn_file_unused_include, get_include_html(delLine.text).c_str()), 0, false);
	}

	div.AddRow("");
}

// 打印本文件的可新增前置声明记录
void FileHistory::PrintForwardClass() const
{
	if (m_forwards.empty())
	{
		return;
	}

	HtmlDiv &div = HtmlLog::instance.m_newDiv;

	div.AddRow(strtool::get_text(cn_file_add_forward_num, get_number_html(m_forwards.size()).c_str()), 2);

	for (auto &forwardItr : m_forwards)
	{
		int line = forwardItr.first;

		const ForwardLine &forwardLine = forwardItr.second;

		div.AddRow(strtool::get_text(cn_file_add_forward_line, get_number_html(line).c_str(), get_include_html(forwardLine.oldText).c_str()), 3);

		for (const string &name : forwardLine.classes)
		{
			div.AddRow(strtool::get_text(cn_file_add_forward_new_text, get_include_html(name).c_str()), 4);
		}

		div.AddRow("");
	}
}

// 打印本文件的可被替换#include记录
void FileHistory::PrintReplace() const
{
	if (m_replaces.empty())
	{
		return;
	}

	HtmlDiv &div = HtmlLog::instance.m_newDiv;

	div.AddRow(strtool::get_text(cn_file_can_replace_num, get_number_html(m_replaces.size()).c_str()), 2);

	for (auto & replaceItr : m_replaces)
	{
		int line = replaceItr.first;

		const ReplaceLine &replaceLine = replaceItr.second;

		std::string oldText = strtool::get_text(cn_file_can_replace_line, get_number_html(line).c_str(), get_include_html(replaceLine.oldText).c_str());
		if (replaceLine.isSkip)
		{
			oldText += cn_file_force_include_text;
		}

		div.AddRow(oldText.c_str(), 3);

		// 输出替换列表，比如，输出: replace to = #include <../1.h> in [./2.h : line = 3]
		{
			const ReplaceTo &replaceInfo = replaceLine.replaceTo;

			// 若替换的串内容不变，则只打印一个
			if (replaceInfo.newText == replaceInfo.oldText)
			{
				div.AddRow(strtool::get_text(cn_file_replace_same_text, get_include_html(replaceInfo.oldText).c_str()), 4, 40, true);
			}
			// 否则，打印替换前和替换后的#include整串
			else
			{
				div.AddRow(strtool::get_text(cn_file_replace_old_text, get_include_html(replaceInfo.oldText).c_str()), 4, 40, true);
				div.AddGrid(strtool::get_text(cn_file_replace_new_text, get_include_html(replaceInfo.newText).c_str()));
			}

			// 在行尾添加[in 所处的文件 : line = xx]
			div.AddRow(strtool::get_text(cn_file_replace_in_file, get_file_html(replaceInfo.inFile.c_str()).c_str(), get_number_html(replaceInfo.line).c_str()), 5);
		}

		div.AddRow("");
	}
}

// 打印本文件内的新增行
void FileHistory::PrintAdd() const
{
	if (m_adds.empty())
	{
		return;
	}

	HtmlDiv &div = HtmlLog::instance.m_newDiv;

	div.AddRow(strtool::get_text(cn_file_add_line_num, get_number_html(m_adds.size()).c_str()), 2);

	for (auto &addItr : m_adds)
	{
		int line = addItr.first;

		const AddLine &addLine = addItr.second;

		div.AddRow(strtool::get_text(cn_file_add_line, get_number_html(line).c_str(), get_include_html(addLine.oldText).c_str()), 3);

		for (const BeAdd &beAdd : addLine.adds)
		{
			div.AddRow(strtool::get_text(cn_file_add_line_new, get_include_html(beAdd.text).c_str(), get_file_html(beAdd.fileName.c_str()).c_str()), 4);
		}

		div.AddRow("");
	}
}

// 打印日志
void ProjectHistory::Print() const
{
	if (m_files.empty())
	{
		return;
	}

	int canCleanFileCount = 0;

	for (auto & itr : m_files)
	{
		const FileHistory &fileHistory = itr.second;

		if (fileHistory.IsNeedClean() && !fileHistory.m_compileErrorHistory.HaveFatalError())
		{
			canCleanFileCount += 1;
		}
	}

	HtmlLog::instance.AddBigTitle(cn_project_history_title);

	HtmlDiv &div = HtmlLog::instance.m_newDiv;
	div.AddTitle(strtool::get_text(cn_project_history_clean_count,	get_number_html(canCleanFileCount).c_str()), 40);
	div.AddTitle(strtool::get_text(cn_project_history_src_count,	get_number_html(Project::instance.m_cpps.size()).c_str()), 59);

	int i = 0;

	for (auto & itr : m_files)
	{
		const FileHistory &history = itr.second;
		if (!history.IsNeedClean())
		{
			continue;
		}

		history.Print(++i);
	}

	HtmlLog::instance.AddDiv(div);
}