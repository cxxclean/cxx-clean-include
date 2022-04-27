//------------------------------------------------------------------------------
// 文件: vs.h
// 作者: 洪坤安
// 说明: visual studio有关的类和接口
//------------------------------------------------------------------------------

#pragma once

#include <iterator>
#include <vector>
#include <set>
#include <string>

class Project;

using namespace std;

// vs配置选项
struct VsConfig
{
	// 编译模式及平台，一般来说有4种：Debug|Win32、Debug|Win64、Release|Win32、Release|Win64
	std::string					mode;

	std::vector<std::string>	forceIncludes;	// 强制include的文件列表
	std::vector<std::string>	preDefines;		// 预先#define的宏列表
	std::vector<std::string>	searchDirs;		// 搜索路径列表
	std::vector<std::string>	extraOptions;	// 额外选项

	// 简单修正一些需要额外计算的搜索路径
	void Fix();

	void Print() const;

	static bool FindMode(const std::string text, std::string &mode);
};

// vs工程文件，对应于.vcproj、.vcxproj的配置
class VsProject
{
public:
	VsProject()
		: m_version(0)
	{}

public:
	// 解析vs2005版本的工程文件（vcproj后缀）
	static bool ParseVs2005(const std::string &vcproj, VsProject &vs2005);

	// 解析vs2008及vs2008以上版本的工程文件（vcxproj后缀）
	static bool ParseVs2008AndUppper(const std::string &vcxproj, VsProject &vs2008);

	// 解析visual studio工程文件
	bool ParseVs(const std::string &vsproj_path);

public:
	VsConfig* GetVsconfigByMode(const std::string &modeAndPlatform);

	void GenerateMembers();

	// 打印vs工程配置
	void Print() const;

public:
	static VsProject				instance;

	int								m_version;				// vs版本
	std::string						m_project_dir;			// 工程文件所在路径
	std::string						m_project_full_path;	// 工程文件全部路径，如:../../hello.vcproj

	std::vector<VsConfig>			m_configs;
	std::vector<std::string>		m_headers;				// 工程内的h、hpp、hh、hxx等头文件列表
	std::vector<std::string>		m_cpps;					// 工程内的cpp、cc、cxx源文件列表

	std::set<std::string>			m_all;					// 工程内所有c++文件
};