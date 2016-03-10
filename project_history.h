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
		int		m_beg;
		int		m_end;
		string	m_text;
	};

	struct ForwardLine
	{
		int					m_offsetAtFile; // 本行末在文件内的偏移量
		string				m_oldText;
		std::set<string>	m_classes;

	};

	struct ReplaceInfo
	{
		string				m_fileName;		// 该#include对应的文件
		string				m_inFile;		// 该#include被哪个文件包含
		int					m_line;			// 该#include所在的行
		string				m_oldText;		// 替换前的#include串，如: #include "../b/../b/../a.h“
		string				m_newText;		// 替换后的#include串，如: #include "../a.h"
	};

	struct ReplaceLine
	{
		bool						m_isSkip;		// 记录本条替换是否应被跳过，因为有些#include是被-include参数所引入的，并无法被替换，但仍然有打印的必要
		int							m_beg;
		int							m_end;
		string						m_oldText;		// 替换前的#include文本，如: #include "../b/../b/../a.h“
		string						m_oldFile;		// 替换前的#include对应的文件
		std::vector<ReplaceInfo>	m_newInclude;	// 替换后的#include串列表
	};

	struct FileHistory
	{
		FileHistory()
			: m_oldBeIncludeCount(0)
			, m_newBeIncludeCount(0)
			, m_beUseCount(0)
			, m_isWindowFormat(false)
		{
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

		bool HasCleaned(const string &file)
		{
			return m_cleanedFiles.find(file) != m_cleanedFiles.end();
		}

		bool HasFile(const string &file)
		{
			return m_files.find(file) != m_files.end();
		}

		void AddFile(ParsingFile *file);

		void PrintUnusedLine();

		void PrintCanForwarddeclClass();

		void PrintReplace();

		// 打印文件被使用次数（未完善）
		void PrintCount();

		void Print();

	public:
		static ProjectHistory	instance;

	public:
		bool				m_isFirst;
		FileHistoryMap		m_files;
		std::set<string>	m_cleanedFiles;
	};
}

#endif // _whole_project_h_