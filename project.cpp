///<------------------------------------------------------------------------------
//< @file:   project.cpp
//< @author: 洪坤安
//< @date:   2016年3月7日
//< @brief:
//< Copyright (c) 2016. All rights reserved.
///<------------------------------------------------------------------------------

#include "project.h"

#include <llvm/Support/raw_ostream.h>

#include "tool.h"
#include "parsing_cpp.h"

Project Project::instance;

void Project::Print()
{
	llvm::outs() << "\n////////////////////////////////\n";
	llvm::outs() << "print project \n";
	llvm::outs() << "////////////////////////////////\n";

	if (!m_allowCleanList.empty())
	{
		llvm::outs() << "\n";
		llvm::outs() << "allow clean file in project:\n";
		for (const string &file : m_allowCleanList)
		{
			llvm::outs() << "    file = " << file << "\n";
		}
	}

	if (!m_allowCleanDir.empty())
	{
		llvm::outs() << "\n";
		llvm::outs() << "allow clean directory = " << m_allowCleanDir << "\n";
	}

	if (!m_cpps.empty())
	{
		llvm::outs() << "\n";
		llvm::outs() << "source list in project:\n";
		for (const string &file : m_cpps)
		{
			llvm::outs() << "    file = " << file << "\n";
		}
	}
}

void Project::ToAbsolute()
{

}

void Project::GenerateAllowCleanList()
{
	if (!m_allowCleanDir.empty())
	{
		return;
	}

	// 将指定的.cpp文件存入可清理列表
	{
		for (const string &cpp : m_cpps)
		{
			string absolutePath = ParsingFile::GetAbsoluteFileName(cpp.c_str());
			m_allowCleanList.insert(absolutePath);
		}
	}
}

bool Project::CanClean(const char* filename)
{
	if (!m_allowCleanDir.empty())
	{
		return filetool::is_at_folder(m_allowCleanDir.c_str(), filename);
	}
	else
	{
		return m_allowCleanList.find(filename) != m_allowCleanList.end();
	}

	return false;
}

void Project::Fix()
{
	for (int i = 0, size = m_cpps.size(); i < size;)
	{
		std::string &cpp = m_cpps[i];
		string ext = strtool::get_ext(cpp);

		if (cpptool::is_cpp(ext))
		{
			++i;
		}
		else
		{
			m_cpps[i] = m_cpps[--size];
			m_cpps.pop_back();
		}
	}

	/*
	for (auto itr = m_allowCleanList.begin(), end = m_allowCleanList.end(); itr != end;)
	{
		const std::string &file = *itr;
		string ext = strtool::get_ext(file);

		if (cpptool::is_header(ext) || cpptool::is_cpp(ext))
		{
			++itr;
		}
		else
		{
			m_allowCleanList.erase(itr++);
		}
	}
	*/
}