///<------------------------------------------------------------------------------
//< @file:   html_log.h
//< @author: ������
//< @brief:  html��־�࣬����������ӡ��־��
///<------------------------------------------------------------------------------

#pragma once

#include <vector>
#include <string>

namespace llvm
{
	class raw_ostream;
}

// 1. ����
static const std::wstring cn_log_name_project = L"����";
static const std::wstring cn_log_name_folder1 = L"[ ";
static const std::wstring cn_log_name_folder2 = L" ] �ļ���";
static const std::wstring cn_log_name_cpp_file1 = L"[ ";
static const std::wstring cn_log_name_cpp_file2 = L" ] c++ �ļ�";
static const std::wstring cn_log1 = L"����";
static const std::wstring cn_log2 = L"����־-";
static const std::wstring cn_log3 = L".html";

static const char* cn_time							= "%04d��%02d��%02d��%02dʱ%02d��%02d��";
static const char* cn_project1						= "[ ";
static const char* cn_project2						= " ] visual studio����";
static const char* cn_clean							= "��ҳ���Ƕ� %s �ķ�����־�����ս���Ա�ҳ����ײ���ͳ�ƽ��Ϊ׼";
static const char* cn_project_text					= "���������c++�ļ��б��Լ���������c++Դ�ļ��б�";
static const char* cn_project_allow_files			= "���������c++�ļ��б��ļ����� = %s�������ڸ��б��c++�ļ��������Ķ���";
static const char* cn_project_allow_file			= "����������ļ� = %s";
static const char* cn_project_source_list			= "��������c++Դ�ļ��б��ļ����� = %s�������ڸ��б��c++�ļ����ᱻ������";
static const char* cn_project_source				= "��������c++Դ�ļ� = %s";

static const char* cn_file_history					= "��%s���ļ�%s�ɱ���������������£�";
static const char* cn_file_history_compile_error	= "��%s���ļ�%s���������ر�������޷�������һ������־���£�";
static const char* cn_file_history_title			= "%s/%s. ��������%s�ļ�����־";
static const char* cn_file_skip						= "ע�⣺��⵽���ļ�ΪԤ�����ļ������ļ������ᱻ�Ķ�";

static const char* cn_error							= "���󣺱��뱾�ļ�ʱ���������±������";
static const char* cn_error_num_tip					= "�����˵�%s��������󣬱������� = %s";
static const char* cn_fatal_error_num_tip			= "�����˵�%s��������󣬱������� = %s���������ر������";
static const char* cn_error_fatal					= "==> ע�⣺���ڷ������ش���[�����=%s]�����ļ��ķ��������������";
static const char* cn_error_too_many				= "==> ע�⣺���ٲ�����%s������������ڱ�����������࣬���ļ��ķ��������������";
static const char* cn_error_ignore					= "==> ����������������%s������������ڴ�����ٻ����أ����ļ��ķ�������Խ���ͳ��";

static const char* cn_file_unused_count				= "���ļ�����%s�ж����#include";
static const char* cn_file_unused_line				= "���Ƴ���%s��";
static const char* cn_file_unused_include			= "����ԭ����#include�ı� = %s";

static const char* cn_file_can_replace_num			= "���ļ�����%s��#include�ɱ��滻";
static const char* cn_file_can_replace_line			= "��%s�п��Ա��滻������ԭ�������� = %s";
static const char* cn_file_replace_same_text		= "���Ա��滻Ϊ�µ� = %s";
static const char* cn_file_replace_old_text			= "ԭ����#include = %s";
static const char* cn_file_replace_new_text			= "����·�������ó����µ�#include = %s";
static const char* cn_file_force_include_text		= " ==>  [ע��: �����滻������������Ϊ���п����ѱ�ǿ�ư���]";
static const char* cn_file_replace_in_file			= "��ע���µ�#include������%s�ļ��ĵ�%s�У�";

static const char* cn_file_add_forward_num			= "���ļ��п�������%s��ǰ������";
static const char* cn_file_add_forward_line			= "���ڵ�%s������ǰ������������ԭ�������� = %s";
static const char* cn_file_add_forward_old_text		= "����ԭ�������� = %s";
static const char* cn_file_add_forward_new_text		= "����ǰ������ = %s";
static const char* cn_file_add_line_num				= "���ļ��п�������%s��";
static const char* cn_file_add_line					= "���ڵ�%s�������У�����ԭ�������� = %s";
static const char* cn_file_add_line_new				= "������ = %s(��Ӧ�ļ� = %s)";

static const char* cn_file_min_use					= "%s. ���ļ����ս�Ӧ�������ļ���Ӧ������ǰ���������ļ��� = %s";
static const char* cn_file_min_kid					= "%s. ���ļ����յ���С����ļ������ļ��� = %s";
static const char* cn_file_sys_ancestor				= "%s. ��ϵͳ�ļ��������ļ����ļ��� = %s";
static const char* cn_file_user_use					= "%s. ���û��ļ������ü�¼���ļ��� = %s";

static const char* cn_project_history_title			= "ͳ�ƽ��";
static const char* cn_project_history_clean_count	= "������������%s��c++�ļ��ɱ�����";
static const char* cn_project_history_src_count		= "���ι�������%s��cpp����cxx��cc��Դ�ļ�";

static const char* cn_parent_file_debug_text		= "�ļ� = %s, ���ļ��� = %s";
static const char* cn_file_debug_text				= "%s[%s](�ļ�ID = %d)(���� = %s){���ļ�������%s}";
static const char* cn_file_include_line				= "[%s��%s��%s]";
static const char* cn_main_file_debug_text			= "[%s](�ļ�ID = %d)(���� = %s)";
static const char* cn_outer_file_type				= "[�ⲿ�ļ�]";
static const char* cn_system_file_type				= "[ϵͳ�ļ�]";

// ������
enum RowType
{
	Row_None	= 0,	// ����
	Row_Warn	= 1,	// ����
	Row_Error	= 2,	// ����
};

// ��������
enum GridType
{
	Grid_None	= 0,	// ����
	Grid_Ok		= 1,	// ��ȷ
	Grid_Error	= 2,	// ����
};

struct DivGrid
{
	DivGrid()
		: width(0)
		, gridType(Grid_None)
	{}

	std::string text;
	int			width;
	GridType	gridType;	// �����ӵ�����
};

struct DivRow
{
	DivRow()
		: tabCount(0)
		, rowType(Row_None)
	{}

	RowType					rowType;	// ���е�����
	int						tabCount;
	std::vector<DivGrid>	grids;
};

struct HtmlDiv
{
	HtmlDiv()
		: hasErrorTip(false)
	{}

	void Clear()
	{
		titles.clear();
		rows.clear();
		hasErrorTip = false;
	}

	void AddTitle(const char* title, int width = 100);

	void AddTitle(const std::string &title, int width = 100);

	void AddRow(const char* text, int tabCount = 0, int width = 100, bool needEscape = false, RowType rowType = Row_None, GridType gridType = Grid_None);

	void AddRow(const std::string &text, int tabCount = 0 /* ����tab�� */, int width = 100, bool needEscape = false, RowType rowType = Row_None, GridType gridType = Grid_None);

	void AddGrid(const char* text, int width = 0, bool needEscape = false, GridType gridType = Grid_None);

	void AddGrid(const std::string &text, int width = 0, bool needEscape = false, GridType gridType = Grid_None);

	std::vector<DivGrid>	titles;
	std::vector<DivRow>		rows;
	bool					hasErrorTip;
};

// ���ڽ���־ת��html��ʽ������鿴
class HtmlLog
{
public:
	HtmlLog();

	bool Init(const std::wstring &htmlPath, const std::string &htmlTitle, const std::string &tip);

	void Open();

	void Close();

	void AddDiv(const HtmlDiv &div);

	// ��Ӵ����
	void AddBigTitle(const std::string &title);

public:
	static HtmlLog* instance;

public:
	std::wstring		m_htmlPath;

	// ��ҳ�ļ�����
	std::string			m_htmlTitle;

	// ��ҳ�ڵ���ʾ
	std::string			m_tip;

	// ��ǰ��div
	HtmlDiv				m_newDiv;

	llvm::raw_ostream*	m_log;
};