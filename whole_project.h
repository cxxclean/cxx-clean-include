///<------------------------------------------------------------------------------
//< @file:   whole_project.h
//< @author: 洪坤安
//< @date:   2016年2月22日
//< @brief:
//< Copyright (c) 2015 game. All rights reserved.
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
		string				m_newFile;		// 该#include对应的文件
		string				m_inFile;		// 该#include原来所在的文件
		int					m_line;			// 该#include所在的行
		string				m_oldText;		// 替换前的#include串，如: #include "../b/../b/../a.h“
		string				m_newText;		// 替换后的#include串，如: #include "../a.h“
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

	struct EachFile
	{
		EachFile()
			: m_oldBeIncludeCount(0)
			, m_newBeIncludeCount(0)
			, m_beUseCount(0)
			, m_isWindowFormat(false)
		{
		}

		bool is_line_unused(int line) const
		{
			return m_unusedLines.find(line) != m_unusedLines.end();
		}

		bool is_line_be_replaced(int line) const
		{
			return m_replaces.find(line) != m_replaces.end();
		}

		bool is_line_add_forward(int line) const
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

	typedef std::map<string, EachFile> FileMap;

	class WholeProject
	{
		WholeProject()
			: m_isFirst(true)
			, m_isOptimized(0)
			, m_onlyHas1File(false)
		{
		}

	public:
		// 清除已被使用的include，仅保留从未被使用过的include
		void clear_used_include(const FileMap &newFiles)
		{
			for (auto fileItr : newFiles)
			{
				const string &fileName	= fileItr.first;
				const EachFile &newFile	= fileItr.second;

				auto findItr = m_files.find(fileName);

				bool found = (findItr != m_files.end());
				if (!found)
				{
					m_files[fileName] = newFile;
				}
				else
				{
					EachFile &oldFile = findItr->second;

					EachFile::UnusedLineMap &unusedLines = oldFile.m_unusedLines;

					for (EachFile::UnusedLineMap::iterator lineItr = unusedLines.begin(), end = unusedLines.end(); lineItr != end; )
					{
						int line = lineItr->first;

						if (newFile.is_line_unused(line))
						{
							++lineItr;
						}
						else
						{
							unusedLines.erase(lineItr++);
						}
					}
				}
			}
		}

		void on_cleaned(const string &file)
		{
			// llvm::outs() << "on_cleaned file: " << file << " ...\n";

			m_cleanedFiles.insert(file);
		}

		bool has_cleaned(const string &file)
		{
			return m_cleanedFiles.find(file) != m_cleanedFiles.end();
		}

		bool has_file(const string &file)
		{
			return m_files.find(file) != m_files.end();
		}

		bool need_clean(const char* file)
		{
			auto itr = m_files.find(file);

			bool found = (itr != m_files.end());
			if (!found)
			{
				return false;
			}

			EachFile &eachFile = itr->second;
			return !eachFile.m_unusedLines.empty();
		}

		void AddFile(ParsingFile *file);
		void print_unused_line();
		void print_can_forwarddecl_class();
		void print_replace();
		void print_count();
		void print();

	public:
		static WholeProject instance;

		bool m_isOptimized;
		bool m_isFirst;
		bool m_onlyHas1File;
		FileMap m_files;
		std::set<string> m_cleanedFiles;
	};
}

#endif // _whole_project_h_