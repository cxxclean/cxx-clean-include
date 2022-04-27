//------------------------------------------------------------------------------
// �ļ�: history.h
// ����: ������
// ˵��: ���ļ���������ʷ
//------------------------------------------------------------------------------

#pragma once

#include <iterator>
#include <vector>
#include <set>
#include <map>
#include <string>

using namespace std;

class ParsingFile;

// Ӧɾ�����У����õ�#include�У�
struct DelLine
{
	DelLine()
		: beg(0)
		, end(0)
	{}

	int							beg;				// ��ʼƫ��
	int							end;				// ����ƫ��
	string						text;				// �������ı���
};

// ������ǰ����������
struct ForwardLine
{
	ForwardLine()
		: offset(0)
	{}

	int							offset;				// ���������ļ��ڵ�ƫ����
	string						oldText;			// ����ԭ�����ı�
	std::set<string>			classes;			// ����ǰ�������б�
};

// �����е�����
struct BeAdd
{
	string						fileName;			// �ļ���
	string						text;				// �ı�����
};

// ������
struct AddLine
{
	AddLine()
		: offset(0)
	{}

	int							offset;				// ���������ļ��ڵ�ƫ����
	string						oldText;			// ����ԭ�����ı�
	std::vector<BeAdd>			adds;				// ����������
};

// ���滻��#include��Ϣ
struct ReplaceTo
{
	ReplaceTo()
		: line(0)
	{}

	string						fileName;			// ��#include��Ӧ���ļ�
	string						inFile;				// ��#include���ĸ��ļ�����
	int							line;				// ��#include���ڵ���
	string						oldText;			// ��#includeԭ������: #include "../b/../b/../a.h"
	string						newText;			// ԭ#include������·����������������#include������: #include "../b/../b/../a.h" -> #include "../a.h"
};

// ���滻#include����
struct ReplaceLine
{
	ReplaceLine()
		: isSkip(false)
		, beg(0)
		, end(0)
	{}

	bool						isSkip;				// ��¼�����滻�Ƿ�Ӧ����������Ϊ��Щ#include�Ǳ�-include����������ģ����޷����滻������Ȼ�д�ӡ�ı�Ҫ
	int							beg;				// ��ʼƫ��
	int							end;				// ����ƫ��
	string						oldText;			// �滻ǰ��#include�ı�����: #include "../b/../b/../a.h��
	string						oldFile;			// �滻ǰ��#include��Ӧ���ļ�
	ReplaceTo					replaceTo;			// �滻���#include���б�
};

// ÿ���ļ��ı��������ʷ
struct CompileErrorHistory
{
	CompileErrorHistory()
		: errNum(0)
		, hasTooManyError(false)
	{}

	// �Ƿ������ر�������������������
	bool HaveFatalError() const
	{
		return !fatalErrorIds.empty();
	}

	// ��ӡ
	void Print() const;

	int							errNum;				// ���������
	bool						hasTooManyError;	// �Ƿ����������[��clang�����ò�������]
	std::set<int>				fatalErrorIds;		// ���ش����б�
	std::vector<std::string>	errors;				// ��������б�
};

// �ļ���ʷ����¼����c++�ļ��Ĵ������¼
class FileHistory
{
public:
	FileHistory()
		: m_isWindowFormat(false)
		, m_isSkip(false)
	{}

	// ��ӡ���ļ���������ʷ
	void Print(int id /* �ļ���� */, bool isPrintCompiliError = true) const;

	// ��ӡ���ļ��Ŀɱ�ɾ#include��¼
	void PrintUnusedInclude() const;

	// ��ӡ���ļ��Ŀ�����ǰ��������¼
	void PrintForwardClass() const;

	// ��ӡ���ļ��Ŀɱ��滻#include��¼
	void PrintReplace() const;

	// ��ӡ���ļ��ڵ�������
	void PrintAdd() const;

	const char* GetNewLineWord() const
	{
		return (m_isWindowFormat ? "\r\n" : "\n");
	}

	bool IsNeedClean() const
	{
		return !(m_delLines.empty() && m_replaces.empty() && m_forwards.empty() && m_adds.empty());
	}

	bool IsLineUnused(int line) const
	{
		return m_delLines.find(line) != m_delLines.end();
	}

	bool IsLineBeReplaced(int line) const
	{
		return m_replaces.find(line) != m_replaces.end();
	}

	bool HaveFatalError() const
	{
		return m_compileErrorHistory.HaveFatalError();
	}

	typedef std::map<int, AddLine> AddLineMap;
	typedef std::map<int, DelLine> DelLineMap;
	typedef std::map<int, ForwardLine> ForwardLineMap;
	typedef std::map<int, ReplaceLine> ReplaceLineMap;

	std::string			m_filename;
	bool				m_isSkip;				// ��¼���ļ��Ƿ��ֹ�Ķ���������Щ�ļ�������stdafx.h��stdafx.cpp�����־Ͳ�Ҫ����
	bool				m_isWindowFormat;		// ���ļ��Ƿ���Windows��ʽ�Ļ��з�[\r\n]������Ϊ��Unix��ʽ[\n]��ͨ���ļ���һ�л��з����жϣ�

	CompileErrorHistory m_compileErrorHistory;	// ���ļ������ı������
	DelLineMap			m_delLines;
	ForwardLineMap		m_forwards;
	ReplaceLineMap		m_replaces;
	AddLineMap			m_adds;
};

// ���ļ�����������[�ļ�] -> [���ļ���������]
typedef std::map<string, FileHistory> FileHistoryMap;

// ���ڴ洢ͳ�ƽ���������Ը���c++�ļ�����ʷ��־
class ProjectHistory
{
	ProjectHistory()
		: g_fileNum(0)
	{}

public:
	void OnCleaned(const string &file)
	{
		m_cleanedFiles.insert(file);
	}

	bool HasCleaned(const string &file) const
	{
		return m_cleanedFiles.find(file) != m_cleanedFiles.end();
	}

	bool HasFile(const string &file) const
	{
		return m_files.find(file) != m_files.end();
	}

	// ����
	void Fix();

	// ��ӡ��־
	void Print() const;

public:
	static ProjectHistory instance;

public:
	// ������c++�ļ��ķ�����ʷ��ע������Ҳ����c++ͷ�ļ���
	FileHistoryMap		m_files;

	// ����������ļ���ע���ѱ�������ļ��������ظ�����
	std::set<string>	m_cleanedFiles;

	// �����ڴ�ӡ����ǰ���ڴ���ڼ����ļ�
	int					g_fileNum;
};