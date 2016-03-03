///<------------------------------------------------------------------------------
//< @file:   vs_config.h
//< @author: 洪坤安
//< @date:   2016年2月22日
//< @brief:	 
//< Copyright (c) 2015 game. All rights reserved.
///<------------------------------------------------------------------------------

#ifndef _vs_config_h_
#define _vs_config_h_

#include <string>
#include <vector>
#include <set>

using namespace std;

// vs配置选项
struct VsConfiguration
{
	std::string mode; // 编译模式及平台，一般来说有4种：Debug|Win32、Debug|Win64、Release|Win32、Release|Win64

	std::vector<std::string>	force_includes;	// 强制include的文件列表
	std::vector<std::string>	pre_defines;	// 预先#define的宏列表
	std::vector<std::string>	search_dirs;	// 搜索路径列表
	std::vector<std::string>	extra_options;	// 额外选项

	void fix_wrong_option();

	void dump();

	static bool get_mode(const std::string text, std::string &mode);	
};

// vs工程文件，对应于.vcproj、.vcxproj的配置
struct Vsproject
{
	static Vsproject				instance;

	float							version;
	std::string						project_dir;		// 工程文件所在路径
	std::string						project_full_path;	// 工程文件全部路径，如:../../hello.vcproj

	std::vector<VsConfiguration>	configs;
	std::vector<std::string>		hs;					// 工程内的h、hpp、hh、hxx等头文件列表
	std::vector<std::string>		cpps;				// 工程内的cpp、cc、cxx源文件列表

	std::set<std::string>			all;				// 工程内所有c++文件

	bool has_file(const char* file)
	{
		return all.find(file) != all.end();
	}

	bool has_file(const string &file)
	{
		return all.find(file) != all.end();
	}

	VsConfiguration* get_vsconfig(const std::string &mode_and_platform);

	void update();

	void dump();
};


bool parse_vs2005(const char* vcproj, Vsproject &vs2005);

// 解析vs2008及vs2008以上版本
bool parse_vs2008_and_uppper(const char* vcxproj, Vsproject &vs2008);

bool parse_vs(std::string &vsproj_path, Vsproject &vs);

void add_source_file(const Vsproject &vs, std::vector<std::string> &source_list);

#endif // _vs_config_h_