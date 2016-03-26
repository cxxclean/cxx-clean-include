///<------------------------------------------------------------------------------
//< @file:   cxx_clean_tool.h
//< @author: 洪坤安
//< @date:   2016年2月22日
//< @brief:
//< Copyright (c) 2016. All rights reserved.
///<------------------------------------------------------------------------------

#ifndef _cxx_clean_tool_h_
#define _cxx_clean_tool_h_

#include <string>
#include <vector>

using namespace std;

namespace strtool
{
	// 是否为空白字符
	inline bool is_blank(char c)
	{
		return (c == ' ' || c == '\t');
	}

	// 是否为斜杠符号：\或/
	inline bool is_slash(char c)
	{
		return (c == '\\' || c == '/');
	}

	std::string itoa(int n);

	// 替换字符串，传入的字符串将被修改
	// 例如：replace("this is an expmple", "is", "") = "th  an expmple"
	// 又如: replace("acac", "ac", "ca") = "caca"
	string& replace(string &str, const char *old, const char* to);

	// 将字符串根据分隔符分割为字符串数组
	void split(const std::string src, std::vector<std::string> &strvec, char cut = ';');

	// 返回文件夹路径，返回结果末尾含/或\
	// 例如：get_dir(../../xxxx.txt) = ../../
	string get_dir(const string &path);

	// 移掉路径，只返回文件名称
	// 例如：../../xxxx.txt -> xxxx.txt
	string strip_dir(const string &path);

	// 从右数起直到指定分隔符的字符串
	// 例如：r_trip_at("123_456", '_') = 456
	string r_trip_at(const string &str, char delimiter);

	// 获取文件后缀
	// 例如：get_ext("../../abc.txt", '_') = txt
	string get_ext(const string &path);

	// 根据传入的格式文本和参数返回最终的文本串
	const char* get_text(const char* fmt, ...);

	// 是否以指定字符串开头
	inline bool start_with(const string &text, const char *prefix)
	{
		int prefix_len	= strlen(prefix);
		int text_len	= text.length();

		if (prefix_len > text_len)
		{
			return false;
		}

		return 0 == strncmp(text.c_str(), prefix, prefix_len);
	}

	// 是否以指定字符串结尾
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

	// 是否包含指定字符
	inline bool contain(const char *text, char x)
	{
		while (*text && *text != x) { ++text; }
		return *text == x;
	}

	// 是否包含指定字符串
	inline bool contain(const std::string &text, const char *x)
	{
		return text.find(x) != std::string::npos;
	}

	// 若以指定前缀开头，则移除前缀并返回剩下的字符串
	inline bool try_strip_left(string& str, const string& prefix)
	{
		if (strtool::start_with(str, prefix.c_str()))
		{
			str = str.substr(prefix.length());
			return true;
		}

		return false;
	}

	// 若以指定后缀开头，则移除后缀并返回剩下的字符串
	inline bool try_strip_right(string& str, const string& suffix)
	{
		if (strtool::end_with(str, suffix.c_str()))
		{
			str = str.substr(0, str.length() - suffix.length());
			return true;
		}

		return false;
	}
}

using namespace strtool;

namespace pathtool
{
	// 将路径转成linux路径格式：将路径中的每个'\'字符均替换为'/'
	string to_linux_path(const char *path);

	// 强制将路径以/结尾，将路径中的每个'\'字符均替换为'/'
	string fix_path(const string& path);

	// 简化路径
	// 例如：d:/a/b/c/../../d/ -> d:/d/
	std::string simplify_path(const char* path);

	/*
		令path_1为当前路径，返回path_2的相对路径
		例如：
			get_relative_path("d:/a/b/c/hello1.cpp", "d:/a/b/c/d/e/f/g/hello2.cpp") = d/e/f/g/hello2.cpp
			get_relative_path("d:/a/b/c/d/e/f/g/hello2.cpp", "d:/a/b/c/hello1.cpp") = ../../../../hello1.cpp

	*/
	std::string get_relative_path(const char *path_1, const char *path_2);

	/*
		返回简化后的绝对路径，若传入相对路径，则结果 = 简化（当前路径 + 相对路径），若传入绝对路径，结果 = 简化后的绝对路径
		例如：
			假设当前路径为：d:/a/b/c/
			get_absolute_path("../../d/e/hello2.cpp") = "d:/a/b/d/e/hello2.cpp"
			get_absolute_path("d:/a/b/c/../../d/") = "d:/a/d/"

	*/
	string get_absolute_path(const char *path);

	/*
		返回简化后的绝对路径，结果 = 简化（基础路径 + 相对路径）
		例如：
			get_absolute_path("d:/a/b/c/", "../../d/") = "d:/a/d/"
	*/
	string get_absolute_path(const char *base_path, const char* relative_path);

	// 返回当前路径
	std::string get_current_path();

	// 改变当前文件夹
	bool cd(const char *path);

	// 指定路径是否存在
	// 例如：dir = "../../example"
	bool is_dir_exist(const std::string &dir);

	// 指定路径是否存在，可为文件路径或者文件夹路径
	// 例如：path = "../../example"
	// 又如：path = "../../abc.xml"
	// 又如：path = "../../"
	bool exist(const std::string &path);

	// 列出指定文件夹下的文件名列表（子文件夹将被忽略），含义如windows命令行下的dir
	// 例如：path = ../../*.*,   则 files = { "a.txt", "b.txt", "c.exe" }
	// 又如：path = ../../*.txt, 则 files = { "a.txt", "b.txt" }
	typedef std::vector<string> filevec_t;
	bool dir(const std::string &path, /* out */filevec_t &files);

	// 文件是否在指定文件夹下（含子文件夹）
	bool is_at_folder(const char* folder, const char *file);

	// 列出指定文件夹下的文件名列表（含子文件夹下的文件）
	// 例如，假设../../下有文件"a", "b", "c", "a.txt", "b.txt", "c.exe"
	//     若path = ../../*.*,   则 files = { "a.txt", "b.txt", "c.exe" }
	//     若path = ../../*.txt, 则 files = { "a.txt", "b.txt" }
	typedef std::vector<string> FileVec;
	bool ls(const string &path, FileVec &files);
}

namespace cpptool
{
	// 是否是c++头文件后缀
	inline bool is_header(const std::string &ext)
	{
		// c++头文件的后缀：h、hpp、hh
		return (ext == "h" || ext == "hpp" || ext == "hh");
	}

	// 是否是c++源文件后缀
	inline bool is_cpp(const std::string &ext)
	{
		// c++源文件的后缀：c、cc、cpp、c++、cxx、m、mm
		return (ext == "c" || ext == "cc" || ext == "cpp" || ext == "c++" || ext == "cxx" || ext == "m" || ext == "mm");
	}
}

namespace htmltool
{
	std::string escape_html(const char* html);

	std::string escape_html(const std::string &html);

	std::string get_file_html(const std::string &filename);
	
	std::string get_include_html(const std::string &text);

	std::string get_number_html(int num);
}

namespace timetool
{
	std::string nowText();
}

#endif // _cxx_clean_tool_h_