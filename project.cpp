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

// 打印本次清理的文件列表
void Project::Print() const
{
	llvm::outs() << "\n////////////////////////////////\n";
	llvm::outs() << "allow clean c++ files and c++ source list \n";
	llvm::outs() << "////////////////////////////////\n";

	// 允许清理的c++文件列表
	if (!m_allowCleanList.empty())
	{
		llvm::outs() << "\n";
		llvm::outs() << "    allow clean file in project:\n";
		for (const string &file : m_allowCleanList)
		{
			llvm::outs() << "        file = " << file << "\n";
		}
	}

	// 允许清理的文件夹路径
	if (!m_allowCleanDir.empty())
	{
		llvm::outs() << "\n";
		llvm::outs() << "    allow clean directory = " << m_allowCleanDir << "\n";
	}

	// 待清理的c++源文件列表
	if (!m_cpps.empty())
	{
		llvm::outs() << "\n";
		llvm::outs() << "    source list in project:\n";
		for (const string &file : m_cpps)
		{
			llvm::outs() << "        file = " << file << "\n";
		}
	}
}

// 生成允许清理文件列表
void Project::GenerateAllowCleanList()
{
	if (!m_allowCleanDir.empty())
	{
		return;
	}

	// 将待清理的.cpp文件存入可清理列表
	{
		for (const string &cpp : m_cpps)
		{
			string absolutePath = pathtool::get_absolute_path(cpp.c_str());
			m_allowCleanList.insert(absolutePath);
		}
	}
}

// 该文件是否允许被清理
bool Project::CanClean(const char* filename) const
{
	if (!m_allowCleanDir.empty())
	{
		return pathtool::is_at_folder(m_allowCleanDir.c_str(), filename);
	}
	else
	{
		return m_allowCleanList.find(filename) != m_allowCleanList.end();
	}

	return false;
}

// 移除非c++后缀的源文件
void Project::Fix()
{
	// 遍历清理目标，将非c++后缀的文件从列表中移除
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

	// 附：
	//     对于允许清理的文件列表，则不检测后缀，因为很可能在某个cpp文件内有这样的语句：
	//         #include "common.def"
	//     甚至是:
	//		   #include "common.cpp"
}