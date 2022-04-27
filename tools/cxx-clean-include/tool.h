//------------------------------------------------------------------------------
// �ļ�: tool.h
// ����: ������
// ˵��: �������õ��ĸ��ֻ����ӿ�
//------------------------------------------------------------------------------

#pragma once

#include <iterator>
#include <vector>
#include <string>

namespace llvm
{
	class raw_ostream;
}

using namespace std;

#define LogInfo(text)	llvm::errs() << "==>[Info][" << __FUNCTION__ << "][" << __LINE__<< "] " << text << "\n"
#define LogError(text)	llvm::errs() << "==>[Error][" << __FUNCTION__ << "][" << __LINE__<< "] " << text << "\n"
#define LogInfoByLvl(logLvl, text)	if (Project::instance.m_logLvl >= logLvl) { LogInfo(text); }
#define LogErrorByLvl(logLvl, text)	if (Project::instance.m_logLvl >= logLvl) { LogError(text); }
#define Log(text)		llvm::errs() << text << "\n"
#define GetNameForLog(name, text) if (Project::instance.m_logLvl >= LogLvl_2) { std::stringstream ss; ss << text << "[" << __FUNCTION__ << "][line=" << __LINE__<< "]"; name = ss.str(); }

namespace strtool
{
	// �Ƿ�Ϊ�հ��ַ�
	inline bool is_blank(char c)
	{
		return (c == ' ' || c == '\t');
	}

	// �Ƿ�Ϊб�ܷ��ţ�\��/
	inline bool is_slash(char c)
	{
		return (c == '\\' || c == '/');
	}

	inline bool is_empty(const char *str)
	{
		return (str == nullptr) || (str[0] == 0x00);
	}

	inline bool is_same_ignore_case(char a, char b)
	{
		return ::tolower(a) == ::tolower(b);
	}

	inline bool is_same_ignore_case(const std::string &a, const char *b)
	{
		return 0 == strnicmp(a.c_str(), b, a.size());
	}

	inline bool is_same_ignore_case(const char *a, const char *b)
	{
		return 0 == strnicmp(a, b, strlen(a));
	}

	std::string& trim(std::string &s);
	std::string itoa(int n);

	int atoi(const char*);

	// ��Сд
	std::string tolower(const char*);

	// ��Сд
	inline std::string tolower(const std::string &s) { return tolower(s.c_str()); }

	// �滻�ַ�����������ַ��������޸�
	// ���磺replace("this is an expmple", "is", "") = "th  an expmple"
	// ����: replace("acac", "ac", "ca") = "caca"
	string& replace(string &str, const char *old, const char* to);
	wstring& wide_replace(wstring &str, const wchar_t *old, const wchar_t* to);

	// ���ַ������ݷָ����ָ�Ϊ�ַ�������
	void split(const std::string &src, std::vector<std::string> &strvec, char cut = ';');

	// �����ļ���·�������ؽ��ĩβ��'/'��'\'
	// ���磺get_dir(../../xxxx.txt) = ../../
	string get_dir(const string &path);

	// �Ƶ�·����ֻ�����ļ�����
	// ���磺../../xxxx.txt -> xxxx.txt
	string strip_dir(const string &path);

	// ��������ֱ��ָ���ָ������ַ���
	// ���磺r_trip_at("123_456", '_') = 123
	string trip_at(const string &str, char delimiter);

	// ��������ֱ��ָ���ָ������ַ���
	// ���磺r_trip_at("123_456", '_') = 456
	string r_trip_at(const string &str, char delimiter);

	// ��ȡ�ļ���׺
	// ���磺get_ext("../../abc.txt", '_') = txt
	string get_ext(const string &path);

	// ��ȡָ����ʽ���ı���
	const char* get_text(const char* fmt, ...);

	// �Ƿ���ָ���ַ�����ͷ�������ִ�Сд��
	inline bool start_with(const string &text, const char *prefix)
	{
		int prefix_len	= strlen(prefix);
		int text_len	= text.length();

		if (prefix_len > text_len)
		{
			return false;
		}

		return 0 == strnicmp(text.c_str(), prefix, prefix_len);
	}

	// �Ƿ���ָ���ַ�����β
	inline bool end_with(const string& text, const char* suffix)
	{
		int suffix_len	= strlen(suffix);
		int text_len	= text.length();

		if (suffix_len > text_len)
		{
			return false;
		}

		return 0 == strncmp(text.c_str() + text_len - suffix_len, suffix, suffix_len);
	}

	// �Ƿ����ָ���ַ�
	inline bool contain(const char *text, char x)
	{
		while (*text && *text != x) { ++text; }
		return *text == x;
	}

	// �Ƿ����ָ���ַ�
	inline bool contain(const char *text, const char *pattern)
	{
		return (strstr(text, pattern) != nullptr);
	}
	
	// �Ƿ����ָ���ַ���
	inline bool contain(const std::string &text, const char *x)
	{
		return text.find(x) != std::string::npos;
	}

	// ����ָ��ǰ׺��ͷ�����Ƴ�ǰ׺������ʣ�µ��ַ���
	inline bool try_strip_left(string& str, const string& prefix)
	{
		if (strtool::start_with(str, prefix.c_str()))
		{
			str = str.substr(prefix.length());
			return true;
		}

		return false;
	}

	// �ַ��� -> ���ַ���
	std::wstring s2ws(const std::string& s);
	
	// ���ַ��� -> �ַ���
	std::string ws2s(const std::wstring& ws);
}

using namespace strtool;

namespace pathtool
{
	// ��·��ת��linux·����ʽ����·���е�ÿ��'\'�ַ����滻Ϊ'/'
	string to_linux_path(const char *path);

	// ǿ�ƽ�·����/��β����·���е�ÿ��'\'�ַ����滻Ϊ'/'
	string fix_path(const string& path);

	// ����·����ȡ�ļ���
	// ���磺/a/b/foo.txt    => foo.txt
	string get_file_name(const char *path);

	// ��·��
	// ���磺d:/a/b/c/../../d/ -> d:/d/
	std::string simplify_path(const char* path);

	std::string append_path(const char* a, const char* b);

	/*
		��path_1Ϊ��ǰ·��������path_2�����·��
		���磺
			get_relative_path("d:/a/b/c/hello1.cpp", "d:/a/b/c/d/e/f/g/hello2.cpp") = d/e/f/g/hello2.cpp
			get_relative_path("d:/a/b/c/d/e/f/g/hello2.cpp", "d:/a/b/c/hello1.cpp") = ../../../../hello1.cpp

	*/
	std::string get_relative_path(const char *path_1, const char *path_2);
	
	// ���ؼ򻯺�ľ���·�������������·�������� = �򻯣���ǰ·�� + ���·���������������·������� = �򻯺�ľ���·��
	// ���磺���赱ǰ·��Ϊ��d:/a/b/c/����
	//		get_absolute_path("../../d/e/hello2.cpp") = "d:/a/b/d/e/hello2.cpp"
	//		get_absolute_path("d:/a/b/c/../../d/") = "d:/a/d/"
	string get_absolute_path(const char *path);

	// ���ؼ򻯺�ľ���·������� = �򻯣�����·�� + ���·����
	// ���磺get_absolute_path("d:/a/b/c/", "../../d/") = "d:/a/d/"
	string get_absolute_path(const char *base_path, const char* relative_path);

	// ��ȡСд���ļ�·��
	string get_lower_absolute_path(const char *path);

	// ��ȡСд���ļ�·��
	string get_lower_absolute_path(const char *base_path, const char* relative_path);

	// ���ص�ǰ·��
	std::string get_current_path();

	// �ı䵱ǰ�ļ���
	bool cd(const char *path);

	// ָ��·���Ƿ����
	// ���磺dir = "../../example"
	bool is_dir_exist(const std::string &dir);

	// ָ��·���Ƿ���ڣ���Ϊ�ļ�·�������ļ���·��
	// ���磺path = "../../example"
	// ���磺path = "../../abc.xml"
	// ���磺path = "../../"
	bool exist(const std::string &path);

	// �г�ָ���ļ����µ��ļ����б����ļ��н������ԣ���������windows�������µ�dir
	// ���磺path = ../../*.*,   �� files = { "a.txt", "b.txt", "c.exe" }
	// ���磺path = ../../*.txt, �� files = { "a.txt", "b.txt" }
	typedef std::vector<string> FileNameVec;
	bool dir(const std::string &path, FileNameVec &files);

	// �ļ��Ƿ���ָ���ļ����£������ļ��У�
	bool is_at_folder(const char *folder, const char *file);

	// �г�ָ���ļ����µ��ļ����б������ļ����µ��ļ���
	// ���磬����../../�����ļ�"a", "b", "c", "a.txt", "b.txt", "c.exe"
	//     ��path = ../../*.*,   �� files = { "a.txt", "b.txt", "c.exe" }
	//     ��path = ../../*.txt, �� files = { "a.txt", "b.txt" }
	bool ls(const string &path, FileNameVec &files);
}

namespace cpptool
{
	// �Ƿ���c++ͷ�ļ���׺
	inline bool is_header(const std::string &file)
	{
		string ext = strtool::get_ext(file);

		// c++ͷ�ļ��ĺ�׺��h��hpp��hh
		return (ext == "h" || ext == "hpp" || ext == "hh");
	}

	// �Ƿ���c++Դ�ļ���׺
	inline bool is_cpp(const std::string &file)
	{
		string ext = strtool::get_ext(file);

		// c++Դ�ļ��ĺ�׺��c��cc��cpp��c++��cxx��m��mm
		return (ext == "c" || ext == "cc" || ext == "cpp" || ext == "c++" || ext == "cxx" || ext == "m" || ext == "mm");
	}
}

namespace htmltool
{
	std::string escape_html(const char *html);

	std::string escape_html(const std::string &html);

	std::string get_file_html(const char *filename);

	std::string get_short_file_name_html(const char *filename);

	std::string get_include_html(const std::string &text);

	std::string get_number_html(int num);

	std::string get_warn_html(const char *text);
}

namespace timetool
{
	std::string get_now(const char* format = "%04d/%02d/%02d-%02d:%02d:%02d");
}

namespace logtool
{
	llvm::raw_ostream &log();
}

using namespace htmltool;
using namespace logtool;