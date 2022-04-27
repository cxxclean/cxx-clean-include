//------------------------------------------------------------------------------
// �ļ�: vs.cpp
// ����: ������
// ˵��: visual studio�йص���ͽӿ�
//------------------------------------------------------------------------------

#include "vs.h"
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include "tool.h"
#include "project.h"
#include "parser.h"
#include "html_log.h"
#include "rapidxml/rapidxml_utils.hpp"

namespace vstool
{
	// ȡ��vs2005��Ŀ�����У�����xml����µ������ļ��б�
	void TakeVs2005NodeFiles(rapidxml::xml_node<char> *files_node, VsProject &vs2005)
	{
		// 1. ����ȡ��File�ӽ���ļ�
		for (rapidxml::xml_node<char> *file_node = files_node->first_node("File"); file_node != NULL; file_node = file_node->next_sibling("File"))
		{
			rapidxml::xml_attribute<char> *file_attr = file_node->first_attribute("RelativePath");
			if (nullptr == file_attr)
			{
				continue;
			}

			string file(file_attr->value());

			// c++ͷ�ļ��ĺ�׺��h��hpp��hh
			if (cpptool::is_header(file))
			{
				vs2005.m_headers.push_back(file);
			}
			// c++Դ�ļ��ĺ�׺��c��cc��cpp��c++��cxx��m��mm
			else if (cpptool::is_cpp(file))
			{
				vs2005.m_cpps.push_back(file);
			}
		}

		// 2. �����ҵ�Filter�ӽ���ļ��У��ݹ��ȡ�����ļ��б�
		for (rapidxml::xml_node<char> *filter_node = files_node->first_node("Filter"); filter_node != NULL; filter_node = filter_node->next_sibling("Filter"))
		{
			TakeVs2005NodeFiles(filter_node, vs2005);
		}
	}
}

VsProject VsProject::instance;

// ������һЩ��Ҫ������������·��
void VsConfig::Fix()
{
	// ɾ���������¸�ʽ��ѡ�$(NOINHERIT)(vs2005��ʽ)��%(AdditionalIncludeDirectories)(vs2008�����ϰ汾��ʽ)
	if (!searchDirs.empty())
	{
		for (int i = 0, n = searchDirs.size(); i < n; ++i)
		{
			std::string &dir = searchDirs[i];

			// �滻$(ProjectDir)Ϊ�գ�$(ProjectDir)�Ǳ���Ŀ�ļ���·��
			strtool::replace(dir, "$(ProjectDir)", "");

			// �滻$(SolutionDir)Ϊ�գ�$(SolutionDir)�ǽ��������·��
			strtool::replace(dir, "$(SolutionDir)", "");
		}

		const std::string &last_dir = searchDirs.back();

		bool clean_last_option =
			(last_dir.find_first_of('%') != string::npos) ||
			(last_dir.find_first_of('$') != string::npos);

		if (clean_last_option)
		{
			searchDirs.erase(searchDirs.end() - 1);
		}
	}

	// ���������õ�ѡ���-D��ͷ��Ԥ�����ѡ��
	{
		std::vector<std::string> usefule_option;

		for (std::vector<std::string>::iterator itr = extraOptions.begin(), end = extraOptions.end(); itr != end; ++itr)
		{
			const std::string &option = *itr;

			bool is_useful = strtool::start_with(option, "-D");
			if (!is_useful)
			{
				continue;
			}

			usefule_option.push_back(option);
		}

		extraOptions = usefule_option;
	}

	// �����������%(PreprocessorDefinitions)��ʽ��Ԥ����
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

bool VsConfig::FindMode(const std::string text, std::string &mode)
{
	string::size_type right_pos = text.find_last_not_of('\'');
	string::size_type left_pos = text.find_last_of('\'', right_pos);

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

VsConfig* VsProject::GetVsconfigByMode(const std::string &mode_and_platform)
{
	for (int i = 0, size = m_configs.size(); i < size; i++)
	{
		VsConfig &vsconfig = m_configs[i];
		if (vsconfig.mode == mode_and_platform)
		{
			return &vsconfig;
		}
	}

	VsConfig config;
	config.mode = mode_and_platform;

	m_configs.push_back(config);
	return &m_configs.back();
}

void VsConfig::Print() const
{
	log() << "\n////////////////////////////////\n";
	log() << "\nvs configuration of: " << mode << "\n";
	log() << "////////////////////////////////\n";

	log() << "\n    predefine: " << mode << "\n";
	for (auto & pre_define : preDefines)
	{
		log() << "        #define " << pre_define << "\n";
	}

	log() << "\n    force includes: \n";
	for (auto & force_include : forceIncludes)
	{
		log() << "        #include \"" << force_include << "\"\n";
	}

	log() << "\n    search directorys: \n";
	for (auto & search_dir : searchDirs)
	{
		log() << "        search = \"" << search_dir << "\"\n";
	}

	log() << "\n    additional options: \n";
	for (auto & extra_option : extraOptions)
	{
		log() << "        option = \"" << extra_option << "\"\n";
	}
}

// ��ӡvs��������
void VsProject::Print() const
{
	log() << "\n////////////////////////////////\n";
	log() << "print vs configuration of: " << m_project_full_path << "\n";
	log() << "////////////////////////////////\n";

	if (m_configs.empty())
	{
		log() << "can not print vs configuration,  configuration is empty!\n";
		return;
	}

	for (int i = 0, size = m_configs.size(); i < size; ++i)
	{
		m_configs[i].Print();
	}

	log() << "all file in project:\n";
	for (const string &file : m_all)
	{
		log() << "    file = " << file << "\n";
	}
}

void VsProject::GenerateMembers()
{
	// ת��Ϊ����·��
	for (string &header : m_headers)
	{
		header = pathtool::get_lower_absolute_path(m_project_dir.c_str(), header.c_str());
	}

	for (string &cpp : m_cpps)
	{
		cpp = pathtool::get_lower_absolute_path(m_project_dir.c_str(), cpp.c_str());
	}

	m_all.insert(m_headers.begin(), m_headers.end());
	m_all.insert(m_cpps.begin(), m_cpps.end());
}

bool VsProject::ParseVs2005(const std::string &vcproj, VsProject &vs2005)
{
	rapidxml::file<> xml_file(vcproj.c_str());
	rapidxml::xml_document<> doc;
	if (!xml_file.data())
	{
		log() << "err: load vs2005 project " << vcproj << " failed, please check the file path\n";
		return false;
	}

	doc.parse<0>(xml_file.data());

	rapidxml::xml_node<>* root = doc.first_node("VisualStudioProject");
	if (!root)
	{
		log() << "err: parse vs2005 project <" << vcproj << "> failed, not found <VisualStudioProject> node\n";
		return false;
	}

	rapidxml::xml_node<>* configs_node = root->first_node("Configurations");
	if (!configs_node)
	{
		log() << "err: parse vs2005 project <" << vcproj << "> failed, not found <Configurations> node\n";
		return false;
	}

	rapidxml::xml_node<>* files_node = root->first_node("Files");
	if (!files_node)
	{
		log() << "err: parse vs2005 project <" << vcproj << "> failed, not found <Files> node\n";
		return false;
	}

	// ���������ڳ�Ա�ļ�
	vstool::TakeVs2005NodeFiles(files_node, vs2005);

	for (rapidxml::xml_node<char> *node = configs_node->first_node("Configuration"); node != NULL; node = node->next_sibling("Configuration"))
	{
		rapidxml::xml_attribute<char> *config_name_attr = node->first_attribute("Name");
		if (nullptr == config_name_attr)
		{
			continue;
		}

		std::string mode = config_name_attr->value();

		VsConfig *vsconfig = vs2005.GetVsconfigByMode(mode);
		if (nullptr == vsconfig)
		{
			continue;
		}

		for (rapidxml::xml_node<char> *tool_node = node->first_node("Tool"); tool_node != NULL; tool_node = tool_node->next_sibling("Tool"))
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

			// ���磺<Tool Name="VCCLCompilerTool" AdditionalIncludeDirectories="&quot;..\..\&quot;;&quot;..\&quot;$(NOINHERIT)"/>
			rapidxml::xml_attribute<char> *search_dir_attr = tool_node->first_attribute("AdditionalIncludeDirectories");
			if (search_dir_attr)
			{
				std::string include_dirs = search_dir_attr->value();
				strtool::replace(include_dirs, "&quot;", "");
				strtool::replace(include_dirs, "\"", "");

				strtool::split(include_dirs, vsconfig->searchDirs);
			}

			// ���磺<Tool Name="VCCLCompilerTool" PreprocessorDefinitions="WIN32;_DEBUG;_CONSOLE;GLOG_NO_ABBREVIATED_SEVERITIES"/>
			rapidxml::xml_attribute<char> *predefine_attr = tool_node->first_attribute("PreprocessorDefinitions");
			if (predefine_attr)
			{
				strtool::split(predefine_attr->value(), vsconfig->preDefines);
			}

			// ���磺<Tool Name="VCCLCompilerTool" ForcedIncludeFiles="stdafx.h"/>
			rapidxml::xml_attribute<char> *force_include_attr = tool_node->first_attribute("ForcedIncludeFiles");
			if (force_include_attr)
			{
				strtool::split(force_include_attr->value(), vsconfig->forceIncludes);	// ����ǿ��#include���ļ�
			}

			// ���磺<Tool Name="VCCLCompilerTool" AdditionalOptions="-D_SCL_SECURE_NO_WARNINGS"/>
			rapidxml::xml_attribute<char> *extra_option_attr = tool_node->first_attribute("AdditionalOptions");
			if (extra_option_attr)
			{
				strtool::split(extra_option_attr->value(), vsconfig->extraOptions, ' ');
			}

			vsconfig->Fix();
		}
	}

	vs2005.GenerateMembers();
	vs2005.m_version = 2005;
	return true;
}

// ����vs2008��vs2008���ϰ汾
bool VsProject::ParseVs2008AndUppper(const std::string &vcxproj, VsProject &vs2008)
{
	rapidxml::file<> xml_file(vcxproj.c_str());
	rapidxml::xml_document<> doc;
	if (!xml_file.data())
	{
		log() << "err: load " << vcxproj << " failed, please check the file path\n";
		return false;
	}

	doc.parse<0>(xml_file.data());

	rapidxml::xml_node<>* root = doc.first_node("Project");
	if (!root)
	{
		log() << "err: parse <" << vcxproj << "> failed, not found <Project> node\n";
		return false;
	}

	// ����������hͷ�ļ�
	for (rapidxml::xml_node<char> *node = root->first_node("ItemGroup"); node != NULL; node = node->next_sibling("ItemGroup"))
	{
		for (rapidxml::xml_node<char> *file = node->first_node("ClInclude"); file != NULL; file = file->next_sibling("ClInclude"))
		{
			rapidxml::xml_attribute<char> *filename = file->first_attribute("Include");
			if (nullptr == filename)
			{
				continue;
			}

			vs2008.m_headers.push_back(filename->value());
		}
	}

	// ����������cppԴ�ļ�
	for (rapidxml::xml_node<char> *node = root->first_node("ItemGroup"); node != NULL; node = node->next_sibling("ItemGroup"))
	{
		for (rapidxml::xml_node<char> *file = node->first_node("ClCompile"); file != NULL; file = file->next_sibling("ClCompile"))
		{
			rapidxml::xml_attribute<char> *filename = file->first_attribute("Include");
			if (nullptr == filename)
			{
				continue;
			}

			vs2008.m_cpps.push_back(filename->value());
		}
	}

	// ����include����·���б�
	for (rapidxml::xml_node<char> *node = root->first_node("ItemDefinitionGroup"); node != NULL; node = node->next_sibling("ItemDefinitionGroup"))
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
		if (!VsConfig::FindMode(condition_attr->value(), mode))
		{
			continue;
		}

		VsConfig *vsconfig = vs2008.GetVsconfigByMode(mode);
		if (nullptr == vsconfig)
		{
			continue;
		}

		// ����Ԥ�����
		// ���磺<PreprocessorDefinitions>WIN32;_DEBUG;_CONSOLE;%(PreprocessorDefinitions)</PreprocessorDefinitions>
		rapidxml::xml_node<char> *predefine_node = compile_node->first_node("PreprocessorDefinitions");
		if (predefine_node)
		{
			strtool::split(predefine_node->value(), vsconfig->preDefines);
		}

		// ��������·��
		// ���磺<AdditionalIncludeDirectories>./;../../;../../3rd/mysql/include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
		rapidxml::xml_node<char> *search_dir_node = compile_node->first_node("AdditionalIncludeDirectories");
		if (search_dir_node)
		{
			strtool::split(search_dir_node->value(), vsconfig->searchDirs);
		}

		// ����ǿ��#include���ļ�
		// ���磺<ForcedIncludeFiles>stdafx.h</ForcedIncludeFiles>
		rapidxml::xml_node<char> *force_include_node = compile_node->first_node("ForcedIncludeFiles");
		if (force_include_node)
		{
			strtool::split(force_include_node->value(), vsconfig->forceIncludes);	// ����ǿ��#include���ļ�
		}

		// ���������ѡ��
		// ���磺<AdditionalOptions>-D_SCL_SECURE_NO_WARNINGS %(AdditionalOptions)</AdditionalOptions>
		rapidxml::xml_node<char> *extra_option_node = compile_node->first_node("AdditionalOptions");
		if (extra_option_node)
		{
			strtool::split(extra_option_node->value(), vsconfig->extraOptions, ' ');
		}

		vsconfig->Fix();
	}

	vs2008.GenerateMembers();
	vs2008.m_version = 2008;
	return true;
}

bool VsProject::ParseVs(const std::string &vsproj_path)
{
	if (!llvm::sys::fs::exists(vsproj_path))
	{
		Log("could not find vs project<" << vsproj_path << ">, aborted!");
		return false;
	}

	m_project_full_path = vsproj_path;
	m_project_dir = strtool::get_dir(vsproj_path);

	std::string ext = strtool::get_ext(vsproj_path);

	if (m_project_dir.empty())
	{
		m_project_dir = "./";
	}

	if (ext == "vcproj")
	{
		return ParseVs2005(vsproj_path, *this);
	}
	else if (ext == "vcxproj")
	{
		return ParseVs2008AndUppper(vsproj_path, *this);
	}

	return false;
}