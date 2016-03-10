///<------------------------------------------------------------------------------
//< @file:   vs_config.h
//< @author: 洪坤安
//< @date:   2016年2月22日
//< @brief:
//< Copyright (c) 2016. All rights reserved.
///<------------------------------------------------------------------------------

#ifndef _vs_config_h_
#define _vs_config_h_

#include <string>
#include <vector>
#include <set>

class Project;

using namespace std;

// vs配置选项
struct VsConfiguration
{
	std::string mode; // 编译模式及平台，一般来说有4种：Debug|Win32、Debug|Win64、Release|Win32、Release|Win64

	std::vector<std::string>	forceIncludes;	// 强制include的文件列表
	std::vector<std::string>	preDefines;	// 预先#define的宏列表
	std::vector<std::string>	searchDirs;	// 搜索路径列表
	std::vector<std::string>	extraOptions;	// 额外选项

	void FixWrongOption();

	void Print();

	static bool FindMode(const std::string text, std::string &mode);
};

// vs工程文件，对应于.vcproj、.vcxproj的配置
struct Vsproject
{
	static Vsproject				instance;

	float							m_version;
	std::string						m_project_dir;		// 工程文件所在路径
	std::string						m_project_full_path;	// 工程文件全部路径，如:../../hello.vcproj

	std::vector<VsConfiguration>	m_configs;
	std::vector<std::string>		m_headers;			// 工程内的h、hpp、hh、hxx等头文件列表
	std::vector<std::string>		m_cpps;				// 工程内的cpp、cc、cxx源文件列表

	std::set<std::string>			m_all;				// 工程内所有c++文件

	bool HasFile(const char* file)
	{
		return m_all.find(file) != m_all.end();
	}

	bool HasFile(const string &file)
	{
		return m_all.find(file) != m_all.end();
	}

	VsConfiguration* GetVsconfigByMode(const std::string &modeAndPlatform);

	void GenerateMembers();

	void TakeSourceListTo(Project &project) const;

	void Print();
};


bool ParseVs2005(const char* vcproj, Vsproject &vs2005);

// 解析vs2008及vs2008以上版本
bool ParseVs2008AndUppper(const char* vcxproj, Vsproject &vs2008);

bool ParseVs(std::string &vsproj_path, Vsproject &vs);

#endif // _vs_config_h_