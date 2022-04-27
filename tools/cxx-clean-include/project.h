///<------------------------------------------------------------------------------
//< @file:   project.h
//< @author: ������
//< @brief:  ��������c++����������
///<------------------------------------------------------------------------------

#pragma once

#include <iterator>
#include <set>
#include <vector>
#include <string>

enum LogLvl
{
	LogLvl_0 = 0,		// ����ӡ���յ�ͳ�ƽ��
	LogLvl_1 = 1,		// Ĭ�ϣ���ӡ���ļ���������������յ�ͳ�ƽ��
	LogLvl_2,			// ���ڵ��ԣ���ӡ���ļ���ɾ���������ӡ���ļ����õ��������ļ�������������������������Ŀ��Ա�ļ���
	LogLvl_3,			// ���ڵ��ԣ������ӡ���ļ�ֱ�ӻ��߼���������ļ���
	LogLvl_Max			// ���ڵ��ԣ������ӡ�쳣�������ӡ�﷨��
};

typedef std::set<std::string> FileNameSet;
typedef std::vector<std::string> FileNameVec;

// ��Ŀ����
class Project
{
public:
	Project()
		: m_isOverWrite(false)
		, m_logLvl(LogLvl_0)
		, m_printIdx(0)
	{
	}

public:
	// ���ļ��Ƿ���������
	static inline bool CanClean(const std::string &filename)
	{
		return CanClean(filename.c_str());
	}

	// ���ļ��Ƿ���������
	static bool CanClean(const char* filename);

	// �Ƿ�Ӧ���Ը��ļ�
	static bool IsSkip(const char* filename);

	// �Ƴ���c++��׺��Դ�ļ�
	void Fix();

	// ��ӡ���� + 1
	std::string AddPrintIdx() const;

	// ��ӡ����������ļ��б�
	void Print() const;

public:
	static Project instance;

public:
	// ����������ļ��б�ֻ�����ڱ��б��ڵ�c++�ļ��������Ķ���
	FileNameSet					m_canCleanFiles;

	// �������c++Դ�ļ��б�ֻ����c++��׺���ļ�����cpp��cxx��
	FileNameVec					m_cpps;

	// �����ļ��б�
	FileNameSet					m_skips;

	// ����Ŀ¼
	std::string					m_workingDir;

	// ������ѡ��Ƿ񸲸�ԭ����c++�ļ�������ѡ��ر�ʱ����Ŀ�ڵ�c++�ļ��������κθĶ���
	bool						m_isOverWrite;

	// ������ѡ���ӡ����ϸ�̶ȣ�0 ~ 9��0��ʾ����ӡ��Ĭ��Ϊ1������ϸ����9
	LogLvl						m_logLvl;

	// ��ǰ��ӡ��������������־��ӡ
	mutable int					m_printIdx;
};