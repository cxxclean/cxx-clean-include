///<------------------------------------------------------------------------------
//< @file:   cxx_clean_tool.h
//< @author: 洪坤安
//< @date:   2016年2月22日
//< @brief:
//< Copyright (c) 2015 game. All rights reserved.
///<------------------------------------------------------------------------------

#ifndef _cxx_clean_tool_h_
#define _cxx_clean_tool_h_

#include <string>
#include <vector>

using namespace std;

namespace filetool
{
	inline bool is_slash(char c)
	{
		return (c == '\\' || c == '/');
	}

	// 令path_1为当前路径，返回path_2的相对路径
	std::string get_relative_path(const char *path_1, const char *path_2);

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
}

namespace strtool
{
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

	// 是否以指定字符串开头
	inline bool start_with(const string &text, const char *prefix)
	{
		int prefix_len	= strlen(prefix);
		int text_len	= text.length();

		if (prefix_len > text_len)
		{
			return false;
		}

		for (int i = 0; i < prefix_len; ++i)
		{
			if (text[i] != prefix[i])
			{
				return false;
			}
		}

		return true;
	}

	// 是否以指定字符串结尾
	inline bool end_with(const string& text, const string& suffix)
	{
		int suffix_len	= suffix.length();
		int text_len	= text.length();

		if (suffix_len > text_len)
		{
			return false;
		}

		for (int i = text_len - suffix_len; i < text_len; ++i)
		{
			if (text[i] != suffix[i])
			{
				return false;
			}
		}

		return true;
	}

	// 是否包含指定字符
	inline bool contain(const char *text, char x)
	{
		while (*text && *text != x) { ++text; }
		return *text == x;
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
}

using namespace strtool;

#endif // _cxx_clean_tool_h_