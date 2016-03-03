///<------------------------------------------------------------------------------
//< @file:   whole_project.cpp
//< @author: 洪坤安
//< @date:   2016年2月22日
//< @brief:
//< Copyright (c) 2015 game. All rights reserved.
///<------------------------------------------------------------------------------

#include "whole_project.h"

#include "parsing_cpp.h"
#include "vs_config.h"

#include <llvm/Support/raw_ostream.h>

namespace cxxcleantool
{
	WholeProject WholeProject::instance;

	void WholeProject::print()
	{
		if (m_files.empty())
		{
			return;
		}

		llvm::outs() << "\n\n////////////////////////////////////////////////////////////////\n";
		llvm::outs() << "[whole project]";
		llvm::outs() << "\n////////////////////////////////////////////////////////////////\n";

		print_unused_line();
		print_can_forwarddecl_class();
		print_replace();
		print_count();
	}


	void WholeProject::AddFile(ParsingFile *newFile)
	{
		newFile->merge_to(m_files);
	}

	void WholeProject::print_unused_line()
	{
		if (m_files.empty())
		{
			return;
		}

		int num = 0;

		for (auto itr : m_files)
		{
			const EachFile &eachFile = itr.second;
			num += eachFile.m_unusedLines.empty() ? 0 : 1;
		}

		llvm::outs() << "\n    " << 1 << ". list of unused line : file count = " << num << "";

		ParsingFile::print_unused_include_of_files(m_files);
	}

	void WholeProject::print_can_forwarddecl_class()
	{
		if (m_files.empty())
		{
			return;
		}

		int num = 0;

		for (auto itr : m_files)
		{
			const EachFile &eachFile = itr.second;
			num += eachFile.m_forwards.empty() ? 0 : 1;
		}

		llvm::outs() << "\n    " << 2 << ". list of can forward decl class : file count = " << num << "";

		ParsingFile::print_can_forwarddecl_of_files(m_files);
	}

	void WholeProject::print_replace()
	{
		if (m_files.empty())
		{
			return;
		}

		int num = 0;

		for (auto itr : m_files)
		{
			const EachFile &eachFile = itr.second;
			num += eachFile.m_replaces.empty() ? 0 : 1;
		}

		llvm::outs() << "\n    " << 3 << ". list of can replace #include : file count = " << num << "";

		ParsingFile::print_can_replace_of_files(m_files);
	}

	void WholeProject::print_count()
	{
		if (m_files.empty())
		{
			return;
		}

		llvm::outs() << "\n    " << 3 << ". file be #include and be use count : file count = " << m_files.size() << "";

		for (auto fileItr : m_files)
		{
			const string &file	= fileItr.first;
			EachFile &eachFile	= fileItr.second;

			if (!Vsproject::instance.has_file(file))
			{
				continue;
			}

			llvm::outs() << "\n        file = {" << file << "} be use num = " << eachFile.m_beUseCount
			             << ", old be include num = " << eachFile.m_oldBeIncludeCount
						 << ", new be include num = " << eachFile.m_newBeIncludeCount;

			if (eachFile.m_beUseCount * 2 < eachFile.m_oldBeIncludeCount)
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