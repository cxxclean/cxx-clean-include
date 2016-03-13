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


namespace cxxcleantool
{
	ProjectHistory ProjectHistory::instance;

	void ProjectHistory::Print() const
	{
		if (m_files.empty())
		{
			return;
		}

		llvm::outs() << "\n\n////////////////////////////////////////////////////////////////\n";
		llvm::outs() << "[project summary: cpp file count = " << Project::instance.m_cpps.size() << ", all c++ file count = " << m_files.size() << "]";
		llvm::outs() << "\n////////////////////////////////////////////////////////////////\n";

		PrintUnusedLine();
		PrintCanForwarddeclClass();
		PrintReplace();
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

		llvm::outs() << "\n    " << 1 << ". list of unused line : file count = " << num << "";

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

		llvm::outs() << "\n    " << 2 << ". list of can forward decl class : file count = " << num << "";

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

		llvm::outs() << "\n    " << 3 << ". list of can replace #include : file count = " << num << "";

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