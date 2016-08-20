//------------------------------------------------------------------------------
// 文件: history.cpp
// 作者: 洪坤安
// 说明: 各文件的清理历史
// Copyright (c) 2016 game. All rights reserved.
//------------------------------------------------------------------------------

#include "history.h"

#include "parser.h"
#include "project.h"
#include "html_log.h"
#include "tool.h"

namespace cxxclean
{
	ProjectHistory ProjectHistory::instance;

	// 打印
	void CompileErrorHistory::Print() const
	{
		if (errNum <= 0)
		{
			return;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;
		div.AddRow(" ", 1, 100, false, true);

		div.AddRow(cn_error, 1, 100, false, true);

		for (const std::string& errTip : errTips)
		{
			div.AddRow(errTip, 2, 100, true, true);
			div.AddRow(" ", 1, 100, false, true);
		}

		// div.AddRow(" ", 1, 100, false, true);

		if (!fatalErrors.empty())
		{
			std::string errTexts;

			int i		= 0;
			int size	= fatalErrors.size();

			for (int err : fatalErrors)
			{
				errTexts += htmltool::get_number_html(err);

				if (++i < size)
				{
					errTexts += ",";
				}
			}

			std::string tip = get_text(cn_error_fatal, errTexts.c_str());
			div.AddRow(tip, 1, 100, true, true);
		}

		if (hasTooManyError)
		{
			std::string tip = get_text(cn_error_too_many, htmltool::get_number_html(errNum).c_str());
			div.AddRow(tip, 1, 100, true, true);
		}
		else if (fatalErrors.empty())
		{
			if (errNum > 0)
			{
				std::string tip = get_text(cn_error_ignore, htmltool::get_number_html(errNum).c_str());
				div.AddRow(tip, 1, 100, true, true);
			}
		}

		div.AddRow("");
	}

	// 打印单个文件的清理历史
	void FileHistory::Print(int id /* 文件序号 */, bool print_err /* = true */) const
	{
		if (!Project::instance.CanClean(m_filename))
		{
			return;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;

		const char *tip = (m_compileErrorHistory.HaveFatalError() ? cn_file_history_compile_error : cn_file_history);

		div.AddRow(strtool::get_text(tip, htmltool::get_number_html(id).c_str(), htmltool::get_file_html(m_filename).c_str()), 1);

		if (print_err)
		{
			m_compileErrorHistory.Print();
		}

		if (m_isSkip)
		{
			// 打印：本文件为预编译头文件，禁止被改动
			div.AddRow(htmltool::get_warn_html(cn_file_skip), 1);
		}

		PrintUnusedInclude();
		PrintForwardClass();
		PrintReplace();
		PrintMove();
	}

	// 打印单个文件内的可被删#include记录
	void FileHistory::PrintUnusedInclude() const
	{
		if (m_unusedLines.empty())
		{
			return;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;

		div.AddRow(strtool::get_text(cn_file_unused_count, htmltool::get_number_html(m_unusedLines.size()).c_str()), 2);

		for (auto &unusedLineItr : m_unusedLines)
		{
			int line = unusedLineItr.first;

			const UnUsedLine &unusedLine = unusedLineItr.second;

			div.AddRow(strtool::get_text(cn_file_unused_line, htmltool::get_number_html(line).c_str()), 3, 25);
			div.AddGrid(strtool::get_text(cn_file_unused_include, htmltool::get_include_html(unusedLine.text).c_str()), 0, true);

			for (auto &itr : unusedLine.usingNamespace)
			{
				const std::string &ns_name = itr.first;
				const std::string &ns_decl = itr.second;

				div.AddRow(strtool::get_text(cn_file_add_using_namespace, htmltool::get_include_html(ns_decl).c_str()), 4);
				div.AddRow(strtool::get_text(cn_file_add_using_namespace, htmltool::get_include_html(ns_name).c_str()), 4);
			}
		}

		div.AddRow("");
	}

	// 打印单个文件内的可新增前置声明记录
	void FileHistory::PrintForwardClass() const
	{
		if (m_forwards.empty())
		{
			return;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;

		div.AddRow(strtool::get_text(cn_file_add_forward_num, htmltool::get_number_html(m_forwards.size()).c_str()), 2);

		for (auto &forwardItr : m_forwards)
		{
			int line = forwardItr.first;

			const ForwardLine &forwardLine = forwardItr.second;

			div.AddRow(strtool::get_text(cn_file_add_forward_line, htmltool::get_number_html(line).c_str(), htmltool::get_include_html(forwardLine.oldText).c_str()), 3);

			for (const string &name : forwardLine.classes)
			{
				div.AddRow(strtool::get_text(cn_file_add_forward_new_text, htmltool::get_include_html(name).c_str()), 4);
			}

			div.AddRow("");
		}
	}

	// 打印单个文件内的可被替换#include记录
	void FileHistory::PrintReplace() const
	{
		if (m_replaces.empty())
		{
			return;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;

		div.AddRow(strtool::get_text(cn_file_can_replace_num, htmltool::get_number_html(m_replaces.size()).c_str()), 2);

		for (auto & replaceItr : m_replaces)
		{
			int line = replaceItr.first;

			const ReplaceLine &replaceLine = replaceItr.second;

			std::string oldText = strtool::get_text(cn_file_can_replace_line, htmltool::get_number_html(line).c_str(), htmltool::get_include_html(replaceLine.oldText).c_str());
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
					div.AddRow(strtool::get_text(cn_file_replace_same_text, htmltool::get_include_html(replaceInfo.oldText).c_str()), 4, 40, true);
				}
				// 否则，打印替换前和替换后的#include整串
				else
				{
					div.AddRow(strtool::get_text(cn_file_replace_old_text, htmltool::get_include_html(replaceInfo.oldText).c_str()), 4, 40, true);
					div.AddGrid(strtool::get_text(cn_file_replace_new_text, htmltool::get_include_html(replaceInfo.newText).c_str()));
				}

				// 在行尾添加[in 所处的文件 : line = xx]
				div.AddRow(strtool::get_text(cn_file_replace_in_file, htmltool::get_file_html(replaceInfo.inFile).c_str(), htmltool::get_number_html(replaceInfo.line).c_str()), 5);
			}

			for (auto & itr : replaceLine.frontNamespace)
			{
				const std::string &ns_name = itr.first;
				const std::string &ns_decl = itr.second;

				div.AddRow(strtool::get_text(cn_file_add_front_using_ns, htmltool::get_include_html(ns_decl).c_str()), 5);
				div.AddRow(strtool::get_text(cn_file_add_front_using_ns, htmltool::get_include_html(ns_name).c_str()), 5);
			}

			for (auto & itr : replaceLine.backNamespace)
			{
				const std::string &ns_name = itr.first;
				const std::string &ns_decl = itr.second;

				div.AddRow(strtool::get_text(cn_file_add_back_using_ns, htmltool::get_include_html(ns_decl).c_str()), 5);
				div.AddRow(strtool::get_text(cn_file_add_back_using_ns, htmltool::get_include_html(ns_name).c_str()), 5);
			}

			div.AddRow("");
		}
	}

	// 打印单个文件内的#include被移动到其他文件的记录
	void FileHistory::PrintMove() const
	{
		if (m_moves.empty())
		{
			return;
		}

		HtmlDiv &div = HtmlLog::instance.m_newDiv;

		div.AddRow(strtool::get_text(cn_file_move_num, htmltool::get_number_html(m_moves.size()).c_str()), 2);

		for (auto & itr : m_moves)
		{
			int line					= itr.first;
			const MoveLine &moveLine	= itr.second;

			if (!moveLine.moveFrom.empty())
			{
				div.AddRow(strtool::get_text(cn_file_move_from_line, htmltool::get_number_html(line).c_str(), htmltool::get_include_html(moveLine.oldText).c_str()), 3);

				for (auto & itr : moveLine.moveFrom)
				{
					const std::map<int, MoveFrom> &froms = itr.second;

					for (auto & innerItr : froms)
					{
						const MoveFrom &from = innerItr.second;

						if (from.isSkip)
						{
							div.AddRow(htmltool::get_warn_html(cn_file_move_skip), 4);
						}

						div.AddRow(strtool::get_text(cn_file_move_from, htmltool::get_include_html(from.fromFile).c_str(), htmltool::get_number_html(from.fromLine).c_str()), 4);
						div.AddRow(strtool::get_text(cn_file_move_from_old, htmltool::get_include_html(from.oldText).c_str()), 5);
						div.AddRow(strtool::get_text(cn_file_move_from_new, htmltool::get_include_html(from.newText).c_str()), 5);

						if (from.newText != moveLine.oldText)
						{
							div.AddRow(strtool::get_text(cn_file_move_to_replace, htmltool::get_include_html(from.newTextFile).c_str(), htmltool::get_number_html(from.newTextLine).c_str()), 5);
						}
					}
				}
			}

			if (!moveLine.moveTo.empty())
			{
				div.AddRow(strtool::get_text(cn_file_move_to_line, htmltool::get_number_html(line).c_str(), htmltool::get_include_html(moveLine.oldText).c_str()), 3);

				for (auto & itr : moveLine.moveTo)
				{
					const MoveTo &to = itr.second;

					div.AddRow(strtool::get_text(cn_file_move_to, htmltool::get_include_html(to.toFile).c_str(), htmltool::get_number_html(to.toLine).c_str()), 4);
					div.AddRow(strtool::get_text(cn_file_move_to_old, htmltool::get_include_html(to.oldText).c_str()), 5);
					div.AddRow(strtool::get_text(cn_file_move_to_new, htmltool::get_include_html(to.newText).c_str()), 5);

					if (to.newText != moveLine.oldText)
					{
						div.AddRow(strtool::get_text(cn_file_move_to_replace, htmltool::get_include_html(to.newTextFile).c_str(), htmltool::get_number_html(to.newTextLine).c_str()), 5);
					}
				}
			}

			div.AddRow("");
		}
	}

	// 修复本文件中一些互相冲突的修改
	void FileHistory::Fix()
	{
		enum LineType
		{
			LineType_Del		= 1,	// 删除
			LineType_Replace	= 2,	// 被替换
			LineType_Move		= 3,	// 被转移到其他文件中
		};

		typedef std::map<int, std::vector<LineType>> LineMap;

		// 1. 先收集每行的信息
		LineMap lines;

		for (auto & itr : m_unusedLines)
		{
			int line = itr.first;
			lines[line].push_back(LineType_Del);
		}

		for (auto & itr : m_replaces)
		{
			int line = itr.first;
			lines[line].push_back(LineType_Replace);
		}

		for (auto & itr : m_moves)
		{
			int line = itr.first;
			const MoveLine &moveLine = itr.second;

			if (!moveLine.moveTo.empty())
			{
				lines[line].push_back(LineType_Move);
			}
		}

		// 2. 修复有冲突的行
		for (auto & itr : lines)
		{
			int line	= itr.first;
			auto &types = itr.second;

			int n		= (int)types.size();
			for (int i = 0; i + 1 < n; ++i)
			{
				LineType lineType = types[i];
				switch (lineType)
				{
				case LineType_Del:
					llvm::errs() << "LineType_Del\n";

					m_unusedLines.erase(line);
					break;

				case LineType_Replace:
					llvm::errs() << "LineType_Replace\n";

					m_replaces.erase(line);
					break;

				default:
					break;
				}
			}
		}

		// 3. 取消对某些行的修改（因为这些行已经被标记为不可更改）
		if (ProjectHistory::instance.IsAnyLineSkip(m_filename))
		{
			const std::set<int> &skipLines = ProjectHistory::instance.m_skips[m_filename];
			for (int line : skipLines)
			{
				m_unusedLines.erase(line);
				m_replaces.erase(line);

				auto & moveItr = m_moves.find(line);
				if (moveItr != m_moves.end())
				{					
					MoveLine &moveLine = moveItr->second;
					moveLine.moveTo.clear();
				}
			}
		}
	}

	// 修正
	void ProjectHistory::Fix()
	{
		for (auto &itr : m_files)
		{
			FileHistory &history = itr.second;
			history.Fix();
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
		div.Clear();

		div.AddTitle(strtool::get_text(cn_project_history_clean_count,	htmltool::get_number_html(canCleanFileCount).c_str()), 50);
		div.AddTitle(strtool::get_text(cn_project_history_src_count,	htmltool::get_number_html(Project::instance.m_cpps.size()).c_str()), 49);

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
		HtmlLog::instance.EndLog();
	}

	// 打印索引 + 1
	std::string ProjectHistory::AddPrintIdx() const
	{
		return strtool::itoa(++m_printIdx);
	}

	// 将指定文件中指定行标记成禁止被改动
	void ProjectHistory::AddSkipLine(const string &file, int line)
	{
		m_skips[file].insert(line);
	}

	// 指定文件中是否有一些行禁止被改动
	bool ProjectHistory::IsAnyLineSkip(const string &file)
	{
		return m_skips.find(file) != m_skips.end();
	}

	void ProjectHistory::AddFile(ParsingFile *newFile)
	{
		newFile->MergeTo(m_files);
	}
}