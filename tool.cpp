///<------------------------------------------------------------------------------
//< @file:   cxx_clean_tool.cpp
//< @author: 洪坤安
//< @date:   2016年2月22日
//< @brief:	 
//< Copyright (c) 2015 game. All rights reserved.
///<------------------------------------------------------------------------------

#include "tool.h"

#include <sys/stat.h>
#include <io.h>

#ifdef WIN32
	#include <direct.h>
#else
	#include <unistd.h>
#endif

namespace filetool
{
	// 令path_1为当前路径，返回path_2的相对路径
	std::string get_relative_path(const char *path_1, const char *path_2)
	{
		if (nullptr == path_1 || nullptr == path_2)
		{
			return "";
		}

		int diff1_pos = 0;
		int diff2_pos = 0;

		int last_same_slash = 0;

		for (char c1 = 0, c2 = 0; (c1 = path_1[diff1_pos]) && (c2 = path_2[diff1_pos]);)
		{
			if (is_slash(c1) && is_slash(c2))
			{
				last_same_slash = diff1_pos;
			}
			else if (c1 == c2)
			{
			}
			else
			{
				break;
			}

			++diff1_pos;
		}

		diff1_pos = diff2_pos = last_same_slash;

		while (is_slash(path_1[diff1_pos])) { ++diff1_pos; }
		while (is_slash(path_2[diff2_pos])) { ++diff2_pos; }

		int path1_len	= diff1_pos;
		int depth_1		= 0;

		for (; path_1[path1_len]; ++path1_len)
		{
			if (is_slash(path_1[path1_len]))
			{
				++depth_1;
			}
		}

		std::string relative_path;
		relative_path.reserve(2 * depth_1);

		for (int i = 0; i < depth_1; ++i)
		{
			relative_path.append("../");
		}

		relative_path.append(&path_2[diff2_pos]);

		for (int i = depth_1 * 3, len = relative_path.size(); i < len; ++i)
		{
			if (relative_path[i] == '\\')
			{
				relative_path[i] = '/';
			}
		}

		return relative_path;
	}

	bool cd(const char *path)
	{
		return 0 == _chdir(path);
	}

	// 指定路径是否存在
	// 例如：dir = "../../example"
	bool is_dir_exist(const std::string &dir)
	{
		struct _stat fileStat;
		if ((_stat(dir.c_str(), &fileStat) == 0) && (fileStat.st_mode & _S_IFDIR))
		{
			return true;
		}

		return false;
	}

	// 指定路径是否存在，可为文件路径或者文件夹路径
	// 例如：path = "../../example"
	// 又如：path = "../../abc.xml"
	// 又如：path = "../../"
	bool exist(const std::string &path)
	{
		return _access(path.c_str(), 0) != -1;
	}

	// 列出指定文件夹下的文件名列表（子文件夹将被忽略），含义如windows命令行下的dir
	// 例如：path = ../../*.*,   则 files = { "a.txt", "b.txt", "c.exe" }
	// 又如：path = ../../*.txt, 则 files = { "a.txt", "b.txt" }
	typedef std::vector<string> filevec_t;
	bool dir(const std::string &path, /* out */filevec_t &files)
	{
		struct _finddata_t filefind;

		int handle = 0;
		if(-1 == (handle = _findfirst(path.c_str(), &filefind)))
		{
			return false;
		}

		do
		{
			if(_A_SUBDIR != filefind.attrib)
			{
				// 不是目录，是文件
				files.push_back(filefind.name);
			}
		}
		while(!_findnext(handle, &filefind));

		_findclose(handle);
		return true;
	}
}

namespace strtool
{
	std::string itoa(int n)
	{
		char buf[14];
		buf[0] = 0;

		::_itoa_s(n, buf, 10);
		return buf;
	}

	// 替换字符串，传入的字符串将被修改
	// 例如：replace("this is an expmple", "is", "") = "th  an expmple"
	// 又如: replace("acac", "ac", "ca") = "caca"
	string& replace(string &str, const char *old, const char* to)
	{
		string::size_type pos = 0;
		int len_old = strlen(old);
		int len_new = strlen(to);

		while((pos = str.find(old, pos)) != string::npos)
		{
			str.replace(pos, len_old, to);
			pos += len_new;
		}

		return str;
	}

	// 将字符串根据分隔符分割为字符串数组
	void split(const std::string src, std::vector<std::string> &strvec, char cut /* = ';' */)
	{
		std::string::size_type pos1 = 0, pos2 = 0;
		while (pos2 != std::string::npos)
		{
			pos1 = src.find_first_not_of(cut, pos2);
			if (pos1 == std::string::npos)
			{
				break;
			}

			pos2 = src.find_first_of(cut, pos1 + 1);
			if (pos2 == std::string::npos)
			{
				if (pos1 != src.size())
				{
					strvec.push_back(src.substr(pos1));
				}

				break;

			}

			strvec.push_back(src.substr(pos1, pos2 - pos1));
		}
	}

	// 返回文件夹路径，返回结果末尾含/或\
	// 例如：get_dir(../../xxxx.txt) = ../../
	string get_dir(const string &path)
	{
		if(path.empty())
		{
			return path;
		}

		int i = path.size() - 1;
		for(; i >= 0; i--)
		{
			if('\\' == path[i] || '/' == path[i])
			{
				break;
			}
		}

		if(i < 0)
		{
			return "";
		}

		if (i <= 0)
		{
			return "";
		}

		return string(path.begin(), path.begin() + i);
	}

	// 移掉路径，只返回文件名称
	// 例如：../../xxxx.txt -> xxxx.txt
	string strip_dir(const string &path)
	{
		if(path.empty())
		{
			return path;
		}

		int i = path.size();
		for(; i >= 0; i--)
		{
			if('\\' == path[i] || '/' == path[i])
			{
				break;
			}
		}

		return path.c_str() + (i + 1);
	}

	// 从右数起直到指定分隔符的字符串
	// 例如：r_trip_at("123_456", '_') = 456
	string r_trip_at(const string &str, char delimiter)
	{
		string::size_type pos = str.rfind(delimiter);
		if(pos == string::npos)
		{
			return "";
		}

		return string(str.begin() + pos + 1, str.end());
	}

	// 获取文件后缀
	// 例如：get_ext("../../abc.txt", '_') = txt
	string get_ext(const string &path)
	{
		string file = strip_dir(path);
		if (file.empty())
		{
			return "";
		}

		return r_trip_at(file, '.');
	}
}
