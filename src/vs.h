//------------------------------------------------------------------------------
// �ļ�: vs.h
// ����: ������
// ˵��: visual studio�йص���ͽӿ�
//------------------------------------------------------------------------------

#pragma once

#include <iterator>
#include <vector>
#include <set>
#include <string>

class Project;

using namespace std;

// vs����ѡ��
struct VsConfig
{
	// ����ģʽ��ƽ̨��һ����˵��4�֣�Debug|Win32��Debug|Win64��Release|Win32��Release|Win64
	std::string					mode;

	std::vector<std::string>	forceIncludes;	// ǿ��include���ļ��б�
	std::vector<std::string>	preDefines;		// Ԥ��#define�ĺ��б�
	std::vector<std::string>	searchDirs;		// ����·���б�
	std::vector<std::string>	extraOptions;	// ����ѡ��

	// ������һЩ��Ҫ������������·��
	void Fix();

	void Print() const;

	static bool FindMode(const std::string text, std::string &mode);
};

// vs�����ļ�����Ӧ��.vcproj��.vcxproj������
class VsProject
{
public:
	VsProject()
		: m_version(0)
	{}

public:
	// ����vs2005�汾�Ĺ����ļ���vcproj��׺��
	static bool ParseVs2005(const std::string &vcproj, VsProject &vs2005);

	// ����vs2008��vs2008���ϰ汾�Ĺ����ļ���vcxproj��׺��
	static bool ParseVs2008AndUppper(const std::string &vcxproj, VsProject &vs2008);

	// ����visual studio�����ļ�
	bool ParseVs(const std::string &vsproj_path);

public:
	VsConfig* GetVsconfigByMode(const std::string &modeAndPlatform);

	void GenerateMembers();

	// ��ӡvs��������
	void Print() const;

public:
	static VsProject				instance;

	int								m_version;				// vs�汾
	std::string						m_project_dir;			// �����ļ�����·��
	std::string						m_project_full_path;	// �����ļ�ȫ��·������:../../hello.vcproj

	std::vector<VsConfig>			m_configs;
	std::vector<std::string>		m_headers;				// �����ڵ�h��hpp��hh��hxx��ͷ�ļ��б�
	std::vector<std::string>		m_cpps;					// �����ڵ�cpp��cc��cxxԴ�ļ��б�

	std::set<std::string>			m_all;					// ����������c++�ļ�
};