///<------------------------------------------------------------------------------
//< @file:   whole_project.h
//< @author: 洪坤安
//< @date:   2016年2月22日
//< @brief:
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

	struct UselessLineInfo
	{
		int							m_beg;			// 起始偏移
		int							m_end;			// 结束偏移
		string						m_text;			// 废弃的文本串
	};

	struct ForwardLine
	{
		int							m_offsetAtFile; // 本行首在文件内的偏移量
		string						m_oldText;		// 本行原来的文本
		std::set<string>			m_classes;		// 新增前置声明列表
	};

	struct ReplaceInfo
	{
		string						m_fileName;		// 该#include对应的文件
		string						m_inFile;		// 该#include被哪个文件包含
		int							m_line;			// 该#include所在的行
		string						m_oldText;		// 替换前的#include串，如: #include "../b/../b/../a.h“
		string						m_newText;		// 替换后的#include串，如: #include "../a.h"
	};

	struct ReplaceLine
	{
		bool						m_isSkip;		// 记录本条替换是否应被跳过，因为有些#include是被-include参数所引入的，并无法被替换，但仍然有打印的必要
		int							m_beg;			// 起始偏移
		int							m_end;			// 结束偏移
		string						m_oldText;		// 替换前的#include文本，如: #include "../b/../b/../a.h“
		string						m_oldFile;		// 替换前的#include对应的文件
		std::vector<ReplaceInfo>	m_newInclude;	// 替换后的#include串列表
	};

	// 项目历史，记录各c++文件的待清理记录
	class FileHistory
	{
	public:
		FileHistory()
			: m_oldBeIncludeCount(0)
			, m_newBeIncludeCount(0)
			, m_beUseCount(0)
			, m_isWindowFormat(false)
		{
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

		typedef std::map<int, UselessLineInfo> UnusedLineMap;
		typedef std::map<int, ForwardLine> ForwardLineMap;
		typedef std::map<int, ReplaceLine> ReplaceLineMap;

		bool				m_isWindowFormat;	// 本文件是否是Windows格式的换行符[\r\n]，否则为类Unix格式[\n]（通过文件第一行换行符来判断）

		UnusedLineMap		m_unusedLines;
		ForwardLineMap		m_forwards;
		ReplaceLineMap		m_replaces;
		std::string			m_filename;

		int					m_newBeIncludeCount;
		int					m_oldBeIncludeCount;
		int					m_beUseCount;
	};

	typedef std::map<string, FileHistory> FileHistoryMap;

	class ProjectHistory
	{
		ProjectHistory()
			: m_isFirst(true)
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

		void PrintUnusedLine() const;

		void PrintCanForwarddeclClass() const;

		void PrintReplace() const;

		// 打印文件被使用次数（未完善）
		void PrintCount() const;

		void Print() const;

	public:
		static ProjectHistory	instance;

	public:
		bool				m_isFirst;
		FileHistoryMap		m_files;
		std::set<string>	m_cleanedFiles;
	};
}

#endif // _whole_project_h_