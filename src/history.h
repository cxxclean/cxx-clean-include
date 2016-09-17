//------------------------------------------------------------------------------
// 文件: history.h
// 作者: 洪坤安
// 说明: 各文件的清理历史
// Copyright (c) 2016 game. All rights reserved.
//------------------------------------------------------------------------------

#ifndef _history_h_
#define _history_h_

#include <iterator>
#include <vector>
#include <set>
#include <map>

using namespace std;

namespace cxxclean
{
	class ParsingFile;

	// 各文件中应保留的行，[文件] -> [该文件中哪些行禁止被删]
	typedef std::map<string, std::set<int>> FileSkipLineMap;

	// 应删除的行（无用的#include行）
	struct DelLine
	{
		DelLine()
			: beg(0)
			, end(0)
		{
		}

		int							beg;				// 起始偏移
		int							end;				// 结束偏移
		string						text;				// 废弃的文本串
	};

	// 可新增前置声明的行
	struct ForwardLine
	{
		ForwardLine()
			: offset(0)
		{
		}

		int							offset;				// 本行首在文件内的偏移量
		string						oldText;			// 本行原来的文本
		std::set<string>			classes;			// 新增前置声明列表
	};

	struct BeAdd
	{
		string						fileName;			// 文件名
		string						text;				// 文本内容
	};

	// 新增行
	struct AddLine
	{
		AddLine()
			: offset(0)
		{
		}

		int							offset;				// 本行首在文件内的偏移量
		string						oldText;			// 本行原来的文本
		std::vector<BeAdd>			adds;				// 新增的内容
	};

	// 可替换的#include信息
	struct ReplaceTo
	{
		ReplaceTo()
			: line(0)
		{
		}

		string						fileName;			// 该#include对应的文件
		string						inFile;				// 该#include被哪个文件包含
		int							line;				// 该#include所在的行
		string						oldText;			// 该#include原串，如: #include "../b/../b/../a.h"
		string						newText;			// 原#include串经过路径搜索后计算出的新#include串，如: #include "../b/../b/../a.h" -> #include "../a.h"
		FileSkipLineMap				m_rely;				// 本行替换依赖于其他文件中的哪几行
	};

	// 可替换#include的行
	struct ReplaceLine
	{
		ReplaceLine()
			: isSkip(false)
			, beg(0)
			, end(0)
		{
		}

		bool						isSkip;				// 记录本条替换是否应被跳过，因为有些#include是被-include参数所引入的，并无法被替换，但仍然有打印的必要
		int							beg;				// 起始偏移
		int							end;				// 结束偏移
		string						oldText;			// 替换前的#include文本，如: #include "../b/../b/../a.h“
		string						oldFile;			// 替换前的#include对应的文件
		ReplaceTo					replaceTo;			// 替换后的#include串列表
	};

	// 新增的#include的来源
	struct MoveFrom
	{
		MoveFrom()
			: fromLine(0)
			, isSkip(false)
			, newTextLine(0)
		{
		}

		string						fromFile;			// 来源文件
		int							fromLine;			// 该文件的第几行
		string						oldText;			// 旧的#inlude文本
		string						newText;			// 新的#inlude文本
		string						newTextFile;		// 新的#include来自于哪个文件
		int							newTextLine;		// 新的#include所在的行
		bool						isSkip;				// 记录本条移动是否应被跳过，因为有些#include不允许被替换，但仍然有打印的必要
	};

	// 被移过去的#include的信息
	struct MoveTo
	{
		MoveTo()
			: toLine(0)
			, newTextLine(0)
		{
		}

		string						toFile;				// 移到的文件中
		int							toLine;				// 被移到第几行
		string						oldText;			// 旧的#inlude文本
		string						newText;			// 新的#inlude文本
		string						newTextFile;		// 新的#include来自于哪个文件
		int							newTextLine;		// 新的#include所在的行
	};

	// 可被移动#include的行
	struct MoveLine
	{
		MoveLine()
			: line_beg(0)
			, line_end(0)
		{
		}

		int							line_beg;			// 本行 - 起始位置
		int							line_end;			// 本行 - 结束位置
		string						oldText;			// 本行之前的#include文本，如: #include "../b/../b/../a.h“

		std::map<string, std::map<int, MoveFrom>>	moveFrom;	// 新增的#include来源于哪些文件
		std::map<string, MoveTo>					moveTo;		// 移除的#include移到哪些文件中
	};

	// 每个文件的编译错误历史
	struct CompileErrorHistory
	{
		CompileErrorHistory()
			: errNum(0)
			, hasTooManyError(false)
		{
		}

		// 是否有严重编译错误或编译错误数过多
		bool HaveFatalError() const
		{
			return !fatalErrors.empty();
		}

		// 打印
		void Print() const;

		int							errNum;				// 编译错误数
		int							hasTooManyError;	// 是否错误数过多[由clang库内置参数决定]
		std::set<int>				fatalErrors;		// 严重错误列表
		std::vector<std::string>	errTips;			// 编译错误提示列表
	};

	// 文件历史，记录单个c++文件的待清理记录
	class FileHistory
	{
	public:
		FileHistory()
			: m_oldBeIncludeCount(0)
			, m_newBeIncludeCount(0)
			, m_beUseCount(0)
			, m_isWindowFormat(false)
			, m_isSkip(false)
		{
		}

		// 打印单个文件的清理历史
		void Print(int id /* 文件序号 */, bool print_err = true) const;

		// 打印单个文件内的可被删#include记录
		void PrintUnusedInclude() const;

		// 打印单个文件内的可新增前置声明记录
		void PrintForwardClass() const;

		// 打印单个文件内的可被替换#include记录
		void PrintReplace() const;

		// 打印单个文件内的#include被移动到其他文件的记录
		void PrintMove() const;

		void PrintAdd() const;

		const char* GetNewLineWord() const
		{
			return (m_isWindowFormat ? "\r\n" : "\n");
		}

		bool IsNeedClean() const
		{
			return !(m_delLines.empty() && m_replaces.empty() && m_forwards.empty() && m_moves.empty() && m_adds.empty());
		}

		bool IsLineUnused(int line) const
		{
			return m_delLines.find(line) != m_delLines.end();
		}

		bool IsLineBeReplaced(int line) const
		{
			return m_replaces.find(line) != m_replaces.end();
		}

		bool IsLineAddForward(int line) const
		{
			return m_forwards.find(line) != m_forwards.end();
		}

		bool IsMoved(int line) const
		{
			auto & itr = m_moves.find(line);
			if (itr == m_moves.end())
			{
				return false;
			}

			const MoveLine &moveLine = itr->second;
			return !moveLine.moveTo.empty();
		}

		bool HaveFatalError() const
		{
			return m_compileErrorHistory.HaveFatalError();
		}

		// 修复本文件中一些互相冲突的修改
		void Fix();

		typedef std::map<int, DelLine> DelLineMap;
		typedef std::map<int, ForwardLine> ForwardLineMap;
		typedef std::map<int, ReplaceLine> ReplaceLineMap;
		typedef std::map<int, MoveLine> MoveLineMap;
		typedef std::map<int, AddLine> AddLineMap;

		std::string			m_filename;
		bool				m_isSkip;				// 记录本文件是否禁止改动，比如有些文件名就是stdafx.h和stdafx.cpp，这种就不要动了
		bool				m_isWindowFormat;		// 本文件是否是Windows格式的换行符[\r\n]，否则为类Unix格式[\n]（通过文件第一行换行符来判断）
		int					m_newBeIncludeCount;
		int					m_oldBeIncludeCount;
		int					m_beUseCount;

		CompileErrorHistory m_compileErrorHistory;	// 本文件产生的编译错误
		DelLineMap			m_delLines;
		ForwardLineMap		m_forwards;
		ReplaceLineMap		m_replaces;
		MoveLineMap			m_moves;
		AddLineMap			m_adds;
	};

	// 各文件的清理结果，[文件] -> [该文件的清理结果]
	typedef std::map<string, FileHistory> FileHistoryMap;

	// 用于存储统计结果，包含对各个c++文件的历史日志
	class ProjectHistory
	{
		ProjectHistory()
			: m_isFirst(true)
			, m_printIdx(0)
			, g_printFileNo(0)
		{
		}

	public:
		void OnCleaned(const string &file)
		{
			// cxx::log() << "on_cleaned file: " << file << " ...\n";
			m_cleanedFiles.insert(file);
		}

		bool HasCleaned(const string &file) const
		{
			return m_cleanedFiles.find(file) != m_cleanedFiles.end();
		}

		bool HasFile(const string &file) const
		{
			return m_files.find(file) != m_files.end();
		}

		// 将指定文件中指定行标记成禁止被改动
		void AddSkipLine(const string &file, int line);

		// 指定文件中是否有一些行禁止被改动
		bool IsAnyLineSkip(const string &file);

		void AddFile(ParsingFile *file);

		// 打印索引 + 1
		std::string AddPrintIdx() const;

		// 修正
		void Fix();

		// 打印日志
		void Print() const;

		// 打印各文件被标记为不可删除的行
		void PrintSkip() const;

	public:
		static ProjectHistory	instance;

	public:
		// 当前是第几次分析所有源文件
		bool				m_isFirst;

		// 对所有c++文件的分析历史（注：其中也包含头文件）
		FileHistoryMap		m_files;

		// 各文件中应保留的行（不允许删除这些行）
		FileSkipLineMap		m_skips;

		// 已清理过的文件（注：列表中的文件将不再重复清理）
		std::set<string>	m_cleanedFiles;

		// 当前正在打印第几个文件
		int					g_printFileNo;

	private:
		// 当前打印索引，仅用于日志打印
		mutable int			m_printIdx;
	};
}

#endif // _history_h_