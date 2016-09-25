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

	// 新增行的内容
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
			return !fatalErrorIds.empty();
		}

		// 打印
		void Print() const;

		int							errNum;				// 编译错误数
		bool						hasTooManyError;	// 是否错误数过多[由clang库内置参数决定]
		std::set<int>				fatalErrorIds;		// 严重错误列表
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

		// 打印本文件的清理历史
		void Print(int id /* 文件序号 */, bool isPrintCompiliError = true) const;

		// 打印本文件的可被删#include记录
		void PrintUnusedInclude() const;

		// 打印本文件的可新增前置声明记录
		void PrintForwardClass() const;

		// 打印本文件的可被替换#include记录
		void PrintReplace() const;

		// 打印本文件内的新增行
		void PrintAdd() const;

		const char* GetNewLineWord() const
		{
			return (m_isWindowFormat ? "\r\n" : "\n");
		}

		bool IsNeedClean() const
		{
			return !(m_delLines.empty() && m_replaces.empty() && m_forwards.empty() && m_adds.empty());
		}

		bool IsLineUnused(int line) const
		{
			return m_delLines.find(line) != m_delLines.end();
		}

		bool IsLineBeReplaced(int line) const
		{
			return m_replaces.find(line) != m_replaces.end();
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
			, g_fileNum(0)
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

		// 对所有c++文件的分析历史（注：其中也包含c++头文件）
		FileHistoryMap		m_files;

		// 各文件中应保留的行（不允许删除这些行）
		FileSkipLineMap		m_skips;

		// 已清理过的文件（注：已被清理的文件将不再重复清理）
		std::set<string>	m_cleanedFiles;

		// 仅用于打印：当前正在处理第几个文件
		int					g_fileNum;

	private:
		// 当前内部打印索引，仅用于日志打印
		mutable int			m_printIdx;
	};
}

#endif // _history_h_