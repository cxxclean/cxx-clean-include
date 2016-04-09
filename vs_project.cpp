///<------------------------------------------------------------------------------
//< @file:   vs_config.cpp
//< @author: 洪坤安
//< @date:   2016年2月22日
//< @brief:
//< Copyright (c) 2016. All rights reserved.
///<------------------------------------------------------------------------------

#include "vs_project.h"

#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>

#include "tool.h"
#include "project.h"
#include "parsing_cpp.h"
#include "rapidxml/rapidxml_utils.hpp"

namespace vstool
{
	// 取出vs2005项目配置中，单个xml结点下的所有文件列表
	void TakeVs2005NodeFiles(rapidxml::xml_node<char> *files_node, Vsproject &vs2005)
	{
		// 1. 依次取出File子结点文件
		for (rapidxml::xml_node<char> *file_node = files_node->first_node("File"); file_node != NULL; file_node = file_node->next_sibling("File"))
		{
			rapidxml::xml_attribute<char> *file_attr = file_node->first_attribute("RelativePath");
			if (nullptr == file_attr)
			{
				continue;
			}

			string file(file_attr->value());
			string ext = strtool::get_ext(file);

			// c++头文件的后缀：h、hpp、hh
			if (cpptool::is_header(ext))
			{
				vs2005.m_headers.push_back(file);
			}
			// c++源文件的后缀：c、cc、cpp、c++、cxx、m、mm
			else if (cpptool::is_cpp(ext))
			{
				vs2005.m_cpps.push_back(file);
			}
		}

		// 2. 依次找到Filter子结点文件夹，递归获取其子文件列表
		for (rapidxml::xml_node<char> *filter_node = files_node->first_node("Filter"); filter_node != NULL; filter_node = filter_node->next_sibling("Filter"))
		{
			TakeVs2005NodeFiles(filter_node, vs2005);
		}
	}
}

Vsproject Vsproject::instance;

void VsConfiguration::Fix()
{
	// 删除类似以下格式的选项：$(NOINHERIT)(vs2005格式)、%(AdditionalIncludeDirectories)(vs2008及以上版本格式)
	{
		if (!searchDirs.empty())
		{
			const std::string &last_dir = searchDirs.back();

			bool clean_last_option =
			    (last_dir.find_first_of('%') != string::npos) ||
			    (last_dir.find_first_of('$') != string::npos);

			if(clean_last_option)
			{
				searchDirs.erase(searchDirs.end() - 1);
			}
		}
	}

	// 仅保留有用的选项：以-D开头的预定义宏选项
	{
		std::vector<std::string> usefule_option;

		for (std::vector<std::string>::iterator itr = extraOptions.begin(), end = extraOptions.end(); itr != end; ++itr)
		{
			const std::string &option = *itr;

			bool is_useful = strtool::start_with(option, "-D");
			if(!is_useful)
			{
				continue;
			}

			usefule_option.push_back(option);
		}

		extraOptions = usefule_option;
	}

	// 清除掉类似于%(PreprocessorDefinitions)格式的预定义
	for (std::vector<std::string>::iterator itr = preDefines.begin(), end = preDefines.end(); itr != end; ++itr)
	{
		const std::string &key = *itr;
		if (key.find_first_of('%') != string::npos)
		{
			preDefines.erase(itr);
			break;
		}
	}
}

bool VsConfiguration::FindMode(const std::string text, std::string &mode)
{
	string::size_type right_pos	= text.find_last_not_of('\'');
	string::size_type left_pos	= text.find_last_of('\'', right_pos);

	if (left_pos == string::npos || right_pos == string::npos)
	{
		return false;
	}

	mode = text.substr(left_pos, right_pos - left_pos + 1);
	if (mode.empty())
	{
		return false;
	}

	return true;
}

VsConfiguration* Vsproject::GetVsconfigByMode(const std::string &mode_and_platform)
{
	for (int i = 0, size = m_configs.size(); i < size; i++)
	{
		VsConfiguration &vsconfig = m_configs[i];
		if (vsconfig.mode == mode_and_platform)
		{
			return &vsconfig;
		}
	}

	VsConfiguration config;
	config.mode = mode_and_platform;

	m_configs.push_back(config);
	return &m_configs.back();
}

void VsConfiguration::Print() const
{
	llvm::outs() << "\n////////////////////////////////\n";
	llvm::outs() << "\nvs configuration of: " << mode << "\n";
	llvm::outs() << "////////////////////////////////\n";

	llvm::outs() << "\n    predefine: " << mode << "\n";
	for (auto pre_define : preDefines)
	{
		llvm::outs() << "        #define " << pre_define << "\n";
	}

	llvm::outs() << "\n    force includes: \n";
	for (auto force_include : forceIncludes)
	{
		llvm::outs() << "        #include \"" << force_include << "\"\n";
	}

	llvm::outs() << "\n    search directorys: \n";
	for (auto search_dir : searchDirs)
	{
		llvm::outs() << "        search = \"" << search_dir << "\"\n";
	}

	llvm::outs() << "\n    additional options: \n";
	for (auto extra_option : extraOptions)
	{
		llvm::outs() << "        option = \"" << extra_option << "\"\n";
	}
}

// 打印vs工程配置
void Vsproject::Print() const
{
	llvm::outs() << "\n////////////////////////////////\n";
	llvm::outs() << "print vs configuration of: " << m_project_full_path << "\n";
	llvm::outs() << "////////////////////////////////\n";

	if (m_configs.empty())
	{
		llvm::outs() << "can not print vs configuration,  configuration is empty!\n";
		return;
	}

	for (int i = 0, size = m_configs.size(); i < size; ++i)
	{
		m_configs[i].Print();
	}

	llvm::outs() << "all file in project:\n";
	for (const string &file : m_all)
	{
		llvm::outs() << "    file = " << file << "\n";
	}
}

void Vsproject::GenerateMembers()
{
	for (int i = 0, size = m_headers.size(); i < size; ++i)
	{
		string &file = m_headers[i];

		string absolutePath = pathtool::get_absolute_path(m_project_dir.c_str(), file.c_str());
		m_all.insert(absolutePath);
	}

	for (int i = 0, size = m_cpps.size(); i < size; ++i)
	{
		string &file = m_cpps[i];
		string absolutePath = pathtool::get_absolute_path(m_project_dir.c_str(), file.c_str());
		m_all.insert(absolutePath);
	}
}


bool Vsproject::ParseVs2005(const char* vcproj, Vsproject &vs2005)
{
	rapidxml::file<> xml_file(vcproj);
	rapidxml::xml_document<> doc;
	if(!xml_file.data())
	{
		llvm::outs() << "err: load vs2005 project " << vcproj << " failed, please check the file path\n";
		return false;
	}

	doc.parse<0>(xml_file.data());

	rapidxml::xml_node<>* root = doc.first_node("VisualStudioProject");
	if(!root)
	{
		llvm::outs() << "err: parse vs2005 project <" << vcproj << "> failed, not found <VisualStudioProject> node\n";
		return false;
	}

	rapidxml::xml_node<>* configs_node = root->first_node("Configurations");
	if(!configs_node)
	{
		llvm::outs() << "err: parse vs2005 project <" << vcproj << "> failed, not found <Configurations> node\n";
		return false;
	}

	rapidxml::xml_node<>* files_node = root->first_node("Files");
	if(!files_node)
	{
		llvm::outs() << "err: parse vs2005 project <" << vcproj << "> failed, not found <Files> node\n";
		return false;
	}

	// 解析工程内成员文件
	vstool::TakeVs2005NodeFiles(files_node, vs2005);

	for(rapidxml::xml_node<char> *node = configs_node->first_node("Configuration"); node != NULL; node = node->next_sibling("Configuration"))
	{
		rapidxml::xml_attribute<char> *config_name_attr = node->first_attribute("Name");
		if (nullptr == config_name_attr)
		{
			continue;
		}

		std::string mode = config_name_attr->value();

		VsConfiguration *vsconfig = vs2005.GetVsconfigByMode(mode);
		if (nullptr == vsconfig)
		{
			continue;
		}

		for(rapidxml::xml_node<char> *tool_node = node->first_node("Tool"); tool_node != NULL; tool_node = tool_node->next_sibling("Tool"))
		{
			rapidxml::xml_attribute<char> *tool_name_attr = tool_node->first_attribute("Name");
			if (nullptr == tool_name_attr)
			{
				continue;
			}

			std::string tool_name = tool_name_attr->value();

			if (tool_name != "VCCLCompilerTool")
			{
				continue;
			}

			// 例如：<Tool Name="VCCLCompilerTool" AdditionalIncludeDirectories="&quot;..\..\&quot;;&quot;..\&quot;$(NOINHERIT)"/>
			rapidxml::xml_attribute<char> *search_dir_attr = tool_node->first_attribute("AdditionalIncludeDirectories");
			if (search_dir_attr)
			{
				std::string include_dirs = search_dir_attr->value();
				strtool::replace(include_dirs, "&quot;", "");
				strtool::replace(include_dirs, "\"", "");

				strtool::split(include_dirs, vsconfig->searchDirs);
			}

			// 例如：<Tool Name="VCCLCompilerTool" PreprocessorDefinitions="WIN32;_DEBUG;_CONSOLE;GLOG_NO_ABBREVIATED_SEVERITIES"/>
			rapidxml::xml_attribute<char> *predefine_attr = tool_node->first_attribute("PreprocessorDefinitions");
			if (predefine_attr)
			{
				strtool::split(predefine_attr->value(),	vsconfig->preDefines);
			}

			// 例如：<Tool Name="VCCLCompilerTool" ForcedIncludeFiles="stdafx.h"/>
			rapidxml::xml_attribute<char> *force_include_attr = tool_node->first_attribute("ForcedIncludeFiles");
			if (force_include_attr)
			{
				strtool::split(force_include_attr->value(),	vsconfig->forceIncludes);	// 解析强制#include的文件
			}

			// 例如：<Tool Name="VCCLCompilerTool" AdditionalOptions="-D_SCL_SECURE_NO_WARNINGS"/>
			rapidxml::xml_attribute<char> *extra_option_attr = tool_node->first_attribute("AdditionalOptions");
			if (extra_option_attr)
			{
				strtool::split(extra_option_attr->value(),	vsconfig->extraOptions, ' ');
			}

			vsconfig->Fix();
		}
	}

	vs2005.GenerateMembers();
	return true;
}

// 解析vs2008及vs2008以上版本
bool Vsproject::ParseVs2008AndUppper(const char* vcxproj, Vsproject &vs2008)
{
	rapidxml::file<> xml_file(vcxproj);
	rapidxml::xml_document<> doc;
	if(!xml_file.data())
	{
		llvm::outs() << "err: load " << vcxproj << " failed, please check the file path\n";
		return false;
	}

	doc.parse<0>(xml_file.data());

	rapidxml::xml_node<>* root = doc.first_node("Project");
	if(!root)
	{
		llvm::outs() << "err: parse <" << vcxproj << "> failed, not found <Project> node\n";
		return false;
	}

	// 解析工程内h头文件
	for(rapidxml::xml_node<char> *node = root->first_node("ItemGroup"); node != NULL; node = node->next_sibling("ItemGroup"))
	{
		for(rapidxml::xml_node<char> *file = node->first_node("ClInclude"); file != NULL; file = file->next_sibling("ClInclude"))
		{
			rapidxml::xml_attribute<char> *filename = file->first_attribute("Include");
			if (nullptr == filename)
			{
				continue;
			}

			vs2008.m_headers.push_back(filename->value());
		}
	}

	// 解析工程内cpp源文件
	for(rapidxml::xml_node<char> *node = root->first_node("ItemGroup"); node != NULL; node = node->next_sibling("ItemGroup"))
	{
		for(rapidxml::xml_node<char> *file = node->first_node("ClCompile"); file != NULL; file = file->next_sibling("ClCompile"))
		{
			rapidxml::xml_attribute<char> *filename = file->first_attribute("Include");
			if (nullptr == filename)
			{
				continue;
			}

			vs2008.m_cpps.push_back(filename->value());
		}
	}

	// 解析include搜索路径列表
	for(rapidxml::xml_node<char> *node = root->first_node("ItemDefinitionGroup"); node != NULL; node = node->next_sibling("ItemDefinitionGroup"))
	{
		rapidxml::xml_node<char> *compile_node = node->first_node("ClCompile");
		if (nullptr == compile_node)
		{
			continue;
		}

		rapidxml::xml_attribute<char> *condition_attr = node->first_attribute("Condition");
		if (nullptr == condition_attr)
		{
			continue;
		}

		std::string mode;
		if (!VsConfiguration::FindMode(condition_attr->value(), mode))
		{
			continue;
		}

		VsConfiguration *vsconfig = vs2008.GetVsconfigByMode(mode);
		if (nullptr == vsconfig)
		{
			continue;
		}

		// 解析预定义宏
		// 例如：<PreprocessorDefinitions>WIN32;_DEBUG;_CONSOLE;%(PreprocessorDefinitions)</PreprocessorDefinitions>
		rapidxml::xml_node<char> *predefine_node = compile_node->first_node("PreprocessorDefinitions");
		if (predefine_node)
		{
			strtool::split(predefine_node->value(),		vsconfig->preDefines);
		}

		// 解析搜索路径
		// 例如：<AdditionalIncludeDirectories>./;../../;../../3rd/mysql/include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
		rapidxml::xml_node<char> *search_dir_node = compile_node->first_node("AdditionalIncludeDirectories");
		if (search_dir_node)
		{
			strtool::split(search_dir_node->value(),	vsconfig->searchDirs);
		}

		// 解析强制#include的文件
		// 例如：<ForcedIncludeFiles>stdafx.h</ForcedIncludeFiles>
		rapidxml::xml_node<char> *force_include_node = compile_node->first_node("ForcedIncludeFiles");
		if (force_include_node)
		{
			strtool::split(force_include_node->value(),	vsconfig->forceIncludes);	// 解析强制#include的文件
		}

		// 解析额外的选项
		// 例如：<AdditionalOptions>-D_SCL_SECURE_NO_WARNINGS %(AdditionalOptions)</AdditionalOptions>
		rapidxml::xml_node<char> *extra_option_node = compile_node->first_node("AdditionalOptions");
		if (extra_option_node)
		{
			strtool::split(extra_option_node->value(),	vsconfig->extraOptions, ' ');
		}

		vsconfig->Fix();
	}

	vs2008.GenerateMembers();
	return true;
}

bool Vsproject::ParseVs(std::string &vsproj_path)
{
	if (!llvm::sys::fs::exists(vsproj_path))
	{
		llvm::errs() << "could not find vs project<" << vsproj_path << ">, aborted!\n";
		return false;
	}

	m_project_full_path	= vsproj_path;
	m_project_dir		= strtool::get_dir(vsproj_path);

	std::string ext		= strtool::get_ext(vsproj_path);

	if (m_project_dir.empty())
	{
		m_project_dir = "./";
	}

	if(ext == "vcproj")
	{
		return ParseVs2005(vsproj_path.c_str(), *this);
	}
	else if(ext == "vcxproj")
	{
		return ParseVs2008AndUppper(vsproj_path.c_str(), *this);
	}

	return false;
}

void Vsproject::TakeSourceListTo(Project &project) const
{
	// 若参数中未指定清理哪些.cpp文件，则清理整个vs项目
	if (project.m_cpps.empty())
	{
		for (const string &cpp : m_cpps)
		{
			std::string absolute_path = pathtool::get_absolute_path(m_project_dir.c_str(), cpp.c_str());
			project.m_cpps.push_back(absolute_path);
		}
	}
	// 否则，仅清理指定的.cpp文件（待完善）
	else
	{
	}

	// 若开启深度清理选项
	if (project.m_isDeepClean)
	{
		// 允许清理vs项目内的所有成员文件（包括头文件）
		project.m_allowCleanList = m_all;
	}
	// 否则，表明仅允许清理.cpp文件
	else
	{
		project.GenerateAllowCleanList();
	}
}