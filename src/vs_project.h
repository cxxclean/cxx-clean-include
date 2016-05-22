///<------------------------------------------------------------------------------
//< @file:   vs_config.h
//< @author: 洪坤安
//< @date:   2016年2月22日
//< @brief:  vs项目的配置
//< Copyright (c) 2016. All rights reserved.
///<------------------------------------------------------------------------------

#ifndef _vs_config_h_
#define _vs_config_h_

#include <iterator>
#include <vector>
#include <set>

namespace cxxcleantool
{
	class Project;

	using namespace std;

	// vs配置选项
	struct VsConfiguration
	{
		// 编译模式及平台，一般来说有4种：Debug|Win32、Debug|Win64、Release|Win32、Release|Win64
		std::string					mode;

		std::vector<std::string>	forceIncludes;	// 强制include的文件列表
		std::vector<std::string>	preDefines;		// 预先#define的宏列表
		std::vector<std::string>	searchDirs;		// 搜索路径列表
		std::vector<std::string>	extraOptions;	// 额外选项

		void Fix();

		void Print() const;

		static bool FindMode(const std::string text, std::string &mode);
	};

	// vs工程文件，对应于.vcproj、.vcxproj的配置
	class Vsproject
	{
	public:
		// 解析vs2005版本的工程文件（vcproj后缀）
		static bool ParseVs2005(const std::string &vcproj, Vsproject &vs2005);

		// 解析vs2008及vs2008以上版本的工程文件（vcxproj后缀）
		static bool ParseVs2008AndUppper(const std::string &vcxproj, Vsproject &vs2008);

		// 解析visual studio工程文件
		bool ParseVs(const std::string &vsproj_path);

	public:
		VsConfiguration* GetVsconfigByMode(const std::string &modeAndPlatform);

		void GenerateMembers();

		void TakeSourceListTo(Project &project) const;

		// 打印vs工程配置
		void Print() const;

	public:
		static Vsproject				instance;

		float							m_version;				// 版本号
		std::string						m_project_dir;			// 工程文件所在路径
		std::string						m_project_full_path;	// 工程文件全部路径，如:../../hello.vcproj

		std::vector<VsConfiguration>	m_configs;
		std::vector<std::string>		m_headers;				// 工程内的h、hpp、hh、hxx等头文件列表
		std::vector<std::string>		m_cpps;					// 工程内的cpp、cc、cxx源文件列表

		std::set<std::string>			m_all;					// 工程内所有c++文件
	};
}

#endif // _vs_config_h_