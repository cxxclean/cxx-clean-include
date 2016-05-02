///<------------------------------------------------------------------------------
//< @file:   project_history.h
//< @author: 洪坤安
//< @date:   2016年2月22日
//< @brief:  本次清理工具的历史统计
//< Copyright (c) 2016. All rights reserved.
///<------------------------------------------------------------------------------

#ifndef _whole_project_h_
#define _whole_project_h_

#include <string>
#include <vector>
#include <set>
#include <map>

using namespace std;

namespace cxxcleantool
{
	class ParsingFile;

	// 无用的#include行
	struct UselessLine
	{
		UselessLine()
			: m_beg(0)
			, m_end(0)
		{
		}

		int							m_beg;				// 起始偏移
		int							m_end;				// 结束偏移
		string						m_text;				// 废弃的文本串
		std::map<string, string>	m_usingNamespace;	// 本行应添加的using namespace
	};

	// 可新增前置声明的行
	struct ForwardLine
	{
		ForwardLine()
			: m_offsetAtFile(0)
		{
		}

		int							m_offsetAtFile;		// 本行首在文件内的偏移量
		string						m_oldText;			// 本行原来的文本
		std::set<string>			m_classes;			// 新增前置声明列表
	};

	// 被替换到的#include的信息
	struct ReplaceTo
	{
		ReplaceTo()
			: m_line(0)
		{
		}

		string						m_fileName;			// 该#include对应的文件
		string						m_inFile;			// 该#include被哪个文件包含
		int							m_line;				// 该#include所在的行
		string						m_oldText;			// 该#include原串，如: #include "../b/../b/../a.h"
		string						m_newText;			// 原#include串经过路径搜索后计算出的新#include串，如: #include "../b/../b/../a.h" -> #include "../a.h"
	};

	// 可替换#include的行
	struct ReplaceLine
	{
		ReplaceLine()
			: m_isSkip(false)
			, m_beg(0)
			, m_end(0)
		{
		}

		bool						m_isSkip;			// 记录本条替换是否应被跳过，因为有些#include是被-include参数所引入的，并无法被替换，但仍然有打印的必要
		int							m_beg;				// 起始偏移
		int							m_end;				// 结束偏移
		string						m_oldText;			// 替换前的#include文本，如: #include "../b/../b/../a.h“
		string						m_oldFile;			// 替换前的#include对应的文件
		std::vector<ReplaceTo>		m_newInclude;		// 替换后的#include串列表
		std::map<string, string>	m_frontNamespace;	// 本行首应添加的using namespace
		std::map<string, string>	m_backNamespace;	// 本行末应添加的using namespace
	};

	// 每个文件的编译错误历史
	struct CompileErrorHistory
	{
		CompileErrorHistory()
			: m_errNum(0)
			, m_hasTooManyError(false)
		{
		}

		// 是否有严重编译错误或编译错误数过多
		bool HaveFatalError() const
		{
			return !m_fatalErrors.empty();
		}

		int							m_errNum;			// 编译错误数
		int							m_hasTooManyError;	// 是否错误数过多[由clang库内置参数决定]
		std::set<int>				m_fatalErrors;		// 严重错误列表
		std::vector<std::string>	m_errTips;			// 编译错误提示列表
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

		const char* GetNewLineWord() const
		{
			return (m_isWindowFormat ? "\r\n" : "\n");
		}

		bool IsNeedClean() const
		{
			return !m_unusedLines.empty() || !m_replaces.empty() || !m_forwards.empty();
		}

		bool IsLineUnused(int line) const
		{
			return m_unusedLines.find(line) != m_unusedLines.end();
		}

		bool IsLineBeReplaced(int line) const
		{
			return m_replaces.find(line) != m_replaces.end();
		}

		bool IsLineAddForward(int line) const
		{
			return m_forwards.find(line) != m_forwards.end();
		}

		typedef std::map<int, UselessLine> UnusedLineMap;
		typedef std::map<int, ForwardLine> ForwardLineMap;
		typedef std::map<int, ReplaceLine> ReplaceLineMap;

		CompileErrorHistory m_compileErrorHistory;	// 本文件产生的编译错误
		UnusedLineMap		m_unusedLines;
		ForwardLineMap		m_forwards;
		ReplaceLineMap		m_replaces;
		std::string			m_filename;

		bool				m_isSkip;				// 记录本文件是否禁止改动，比如有些文件名就是stdafx.h和stdafx.cpp，这种就不要动了
		bool				m_isWindowFormat;		// 本文件是否是Windows格式的换行符[\r\n]，否则为类Unix格式[\n]（通过文件第一行换行符来判断）
		int					m_newBeIncludeCount;
		int					m_oldBeIncludeCount;
		int					m_beUseCount;
	};

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
			// llvm::outs() << "on_cleaned file: " << file << " ...\n";
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

		void AddFile(ParsingFile *file);

		// 打印文件被使用次数（未完善）
		void PrintCount() const;

		// 打印索引 + 1
		std::string AddPrintIdx() const;

		// 打印日志
		void Print() const;

	public:
		static ProjectHistory	instance;

	public:
		// 当前是第几次分析所有源文件
		bool				m_isFirst;

		// 对所有c++文件的分析历史（注：其中也包含头文件）
		FileHistoryMap		m_files;

		// 已清理过的文件（注：列表中的文件将不再重复清理）
		std::set<string>	m_cleanedFiles;

		// 当前正在打印第几个文件
		int					g_printFileNo;

	private:
		// 当前打印索引，仅用于日志打印
		mutable int			m_printIdx;
	};
}

#endif // _whole_project_h_