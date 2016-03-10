///<------------------------------------------------------------------------------
//< @file:   project.h
//< @author: 洪坤安
//< @date:   2016年3月7日
//< @brief:
//< Copyright (c) 2016. All rights reserved.
///<------------------------------------------------------------------------------

#ifndef _project_h_
#define _project_h_

#include <string>
#include <set>
#include <vector>

// 项目配置
class Project
{
public:
	Project()
		: m_isDeepClean(false)
		, m_onlyHas1File(false)
	{
	}

public:
	// 该文件是否允许被清理
	bool CanClean(const std::string &filename)
	{
		return CanClean(filename.c_str());
	}

	bool CanClean(const char* filename);

	void ToAbsolute();

	void GenerateAllowCleanList();

	void Fix();

	void Print();

public:
	static Project instance;

public:
	// 允许被清理的文件列表（只有属于本列表内的c++文件才允许被改动）
	std::set<std::string>		m_allowCleanList;

	// 允许被清理的文件夹（只有处于该文件夹下的c++文件才允许被改动），注意：当允许清理文件列表不为空时，本项无意义
	std::string					m_allowCleanDir;

	// c++源文件列表
	std::vector<std::string>	m_cpps;

	// 是否深层清理，建议为true
	bool						m_isDeepClean;

	// 是否只有一个文件
	bool						m_onlyHas1File;

	// 工作目录
	std::string					m_workingDir;
};

#endif // _project_h_