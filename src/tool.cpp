//------------------------------------------------------------------------------
// 文件: tool.cpp
// 作者: 洪坤安
// 说明: 本工具用到的各种基础接口
// Copyright (c) 2016 game. All rights reserved.
//------------------------------------------------------------------------------

#include "tool.h"

#include <sys/stat.h>
#include <io.h>
#include <fstream>
#include <stdarg.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/raw_ostream.h>

#include "html_log.h"

#ifdef WIN32
	#include <direct.h>
	#define _X86_
	#include <profileapi.h>
#else
	#include <unistd.h>
#endif

namespace strtool
{
	std::string itoa(int n)
	{
		char buf[14];
		buf[0] = 0;

		::_itoa_s(n, buf, 10);
		return buf;
	}

	int atoi(const char* str)
	{
		int n = ::atoi(str);
		return n;
	}

	std::string tolower(const char* str)
	{
		std::string s(str);
		std::transform(s.begin(), s.end(), s.begin(), ::tolower);

		return s;
	}

	template <typename S /* 字符串类型 */, typename C /* 字符类型 */>
	S& replace(S &str, const C *old, int len_old, const C* to, int len_to)
	{
		S::size_type len_str = str.size();

		S out;
		out.reserve(2 * len_str);

		S::size_type pre_pos = 0;
		S::size_type find_pos = 0;

		while ((find_pos = str.find(old, pre_pos)) != S::npos)
		{
			out += str.substr(pre_pos, find_pos - pre_pos);
			out += to;

			pre_pos = find_pos + len_old;
		}

		if (pre_pos < len_str)
		{
			out += str.substr(pre_pos);
		}

		str = out;
		return str;
	}

	string& replace(string &str, const char *old, const char* to)
	{
		int len_old = strlen(old);
		int len_to = strlen(to);

		return replace(str, old, len_old, to, len_to);
	}

	wstring& wide_replace(wstring &str, const wchar_t *old, const wchar_t* to)
	{
		int len_old = wcslen(old);
		int len_to = wcslen(to);

		return replace(str, old, len_old, to, len_to);
	}

	void split(const std::string &src, std::vector<std::string> &strvec, char cut /* = ';' */)
	{
		std::string::size_type pos1 = 0;
		std::string::size_type pos2 = 0;
		std::string::size_type len = src.size();

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
				if (pos1 < len)
				{
					strvec.push_back(src.substr(pos1));
				}
			}
			else
			{
				strvec.push_back(src.substr(pos1, pos2 - pos1));
			}
		}
	}

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

	string strip_dir(const string &path)
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

		return path.c_str() + (i + 1);
	}

	string trip_at(const string &str, char delimiter)
	{
		string::size_type pos = str.find(delimiter);
		if(pos == string::npos)
		{
			return "";
		}

		return string(str.begin(), str.begin() + pos);
	}

	string r_trip_at(const string &str, char delimiter)
	{
		string::size_type pos = str.rfind(delimiter);
		if(pos == string::npos)
		{
			return "";
		}

		return string(str.begin() + pos + 1, str.end());
	}

	string get_ext(const string &path)
	{
		string file = strip_dir(path);
		if (file.empty())
		{
			return "";
		}

		return r_trip_at(file, '.');
	}

	std::string& trim(std::string &s)
	{
		if (s.empty())
		{
			return s;
		}

		s.erase(0, s.find_first_not_of(" "));
		s.erase(0, s.find_first_not_of("\t"));
		s.erase(s.find_last_not_of(" ") + 1);
		s.erase(s.find_last_not_of("\t") + 1);
		return s;
	}

	const char* get_text(const char* fmt, ...)
	{
		static char g_sprintfBuf[10 * 1024] = {0};

		va_list args;
		va_start(args, fmt);
		vsprintf_s(g_sprintfBuf, sizeof(g_sprintfBuf), fmt, args);
		va_end(args);
		return g_sprintfBuf;
	}

	// 获取指定格式的宽文本串
	const wchar_t* get_wide_text(const wchar_t* fmt, ...)
	{
		static wchar_t g_swprintfBuf[10 * 1024] = { 0 };

		va_list args;
		va_start(args, fmt);
		vswprintf_s(g_swprintfBuf, sizeof(g_swprintfBuf), fmt, args);
		va_end(args);
		return g_swprintfBuf;
	}

	std::wstring s2ws(const std::string& s)
	{
		std::locale old_loc = std::locale::global(std::locale(""));

		const size_t buffer_size = s.size() + 1;
		wchar_t* dst_wstr = new wchar_t[buffer_size];
		wmemset(dst_wstr, 0, buffer_size);
		mbstowcs(dst_wstr, s.c_str(), buffer_size);
		std::wstring ws = dst_wstr;
		delete[]dst_wstr;

		std::locale::global(old_loc);
		return ws;
	}

	std::string ws2s(const std::wstring& ws)
	{
		std::locale old_loc =std::locale::global(std::locale(""));

		size_t buffer_size = ws.size() * 4 + 1;
		char* dst_str = new char[buffer_size];
		memset(dst_str, 0, buffer_size);
		wcstombs(dst_str, ws.c_str(), buffer_size);
		std::string s = dst_str;
		delete[]dst_str;

		std::locale::global(old_loc);
		return s;
	}
}

namespace pathtool
{
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
			if (strtool::is_slash(c1) && strtool::is_slash(c2))
			{
				last_same_slash = diff1_pos;
			}
			else if (!is_same_ignore_case(c1, c2))
			{
				break;
			}

			++diff1_pos;
		}

		diff1_pos = diff2_pos = last_same_slash;

		while (strtool::is_slash(path_1[diff1_pos])) { ++diff1_pos; }
		while (strtool::is_slash(path_2[diff2_pos])) { ++diff2_pos; }

		int path1_len	= diff1_pos;
		int depth_1		= 0;

		for (; path_1[path1_len]; ++path1_len)
		{
			if (strtool::is_slash(path_1[path1_len]))
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

		return to_linux_path(relative_path.c_str());
	}

	string to_linux_path(const char *path)
	{
		string ret = path;

		// 将'\'替换为'/'
		for (char &c : ret)
		{
			if (c == '\\')
			{
				c = '/';
			}
		}

		strtool::replace(ret, "//", "/");
		return ret;
	}

	string fix_path(const string& path)
	{
		string ret = to_linux_path(path.c_str());

		if (!end_with(ret, "/"))
		{
			ret += "/";
		}

		return ret;
	}

	// 根据路径获取文件名
	string get_file_name(const char *path)
	{
		return llvm::sys::path::filename(path);
	}

	// 简化路径
	std::string simplify_path(const char* path)
	{
		string native_path = to_linux_path(path);
		if (native_path.empty())
		{
			return "";
		}

		if (start_with(native_path, "../") || start_with(native_path, "./"))
		{
			return native_path;
		}

		strtool::replace(native_path, "/./", "/");

		string out(native_path.size(), '\0');

		int o = 0;

		const char up_dir[] = "/../";
		int up_dir_len = strlen(up_dir);

		for (int i = 0, len = native_path.size(); i < len;)
		{
			char c = native_path[i];

			if (c == '/')
			{
				if (i + up_dir_len - 1 >= len || i == 0)
				{
					out[o++] = c;
					++i;
					continue;
				}

				if(0 == strncmp(&native_path[i], "/../", up_dir_len))
				{
					if (out[o] == '/')
					{
						--o;
					}

					while (o >= 0)
					{
						if (out[o] == '/')
						{
							break;
						}
						else if (out[o] == ':')
						{
							++o;
							break;
						}

						--o;
					}

					if (o < 0)
					{
						o = 0;
					}

					i += up_dir_len - 1;
					continue;
				}
				else
				{
					out[o++] = c;
					++i;
				}
			}
			else
			{
				out[o++] = c;
				++i;
			}
		}

		out[o] = '\0';
		out.erase(out.begin() + o, out.end());
		return out;
	}

	std::string append_path(const char* a, const char* b)
	{
		llvm::SmallString<512> path(a);
		llvm::sys::path::append(path, b);

		return path.c_str();
	}

	string get_absolute_path(const char *path)
	{
		if (nullptr == path)
		{
			return "";
		}

		if (path[0] == 0x0)
		{
			return "";
		}

		llvm::SmallString<2048> filepath(path);
		std::error_code error = llvm::sys::fs::make_absolute(filepath);
		if (error)
		{
			return "";
		}

		filepath = simplify_path(filepath.c_str());
		return filepath.str();
	}

	string get_absolute_path(const char *base_path, const char* relative_path)
	{
		if (nullptr == base_path || nullptr == relative_path)
		{
			return "";
		}

		if (llvm::sys::path::is_absolute(relative_path))
		{
			return get_absolute_path(relative_path);
		}
		else
		{
			std::string path = append_path(base_path, relative_path);
			return get_absolute_path(path.c_str());
		}

		return "";
	}

	string get_lower_absolute_path(const char *path)
	{
		return tolower(get_absolute_path(path));
	}

	string get_lower_absolute_path(const char *base_path, const char* relative_path)
	{
		return tolower(get_absolute_path(base_path, relative_path));
	}

	bool cd(const char *path)
	{
		return 0 == _chdir(path);
	}

	bool is_dir_exist(const std::string &dir)
	{
		struct _stat fileStat;
		return  ((_stat(dir.c_str(), &fileStat) == 0) && (fileStat.st_mode & _S_IFDIR));
	}

	bool exist(const std::string &path)
	{
		return _access(path.c_str(), 0) != -1;
	}

	typedef std::vector<string> filevec_t;
	bool dir(const std::string &path, /* out */filevec_t &files)
	{
		struct _finddata_t filefind;

		int handle = _findfirst(path.c_str(), &filefind);
		if(-1 == handle)
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

	// 列出指定文件夹下的文件名列表（含子文件夹下的文件）
	bool ls(const string &path, FileNameVec &files)
	{
		std::string folder	= get_dir(path);
		std::string pattern	= strip_dir(path);

		if (pattern.empty())
		{
			pattern = "*";
		}

		string fixedPath = folder + "/" + pattern;

		struct _finddata_t fileinfo;

		int handle = _findfirst(fixedPath.c_str(), &fileinfo);
		if(-1 == handle)
		{
			return false;
		}

		do
		{
			//判断是否有子目录
			if (fileinfo.attrib & _A_SUBDIR)
			{
				// 忽略当前目录和上层目录
				if( (strcmp(fileinfo.name,".") != 0) &&(strcmp(fileinfo.name,"..") != 0))
				{
					string subdir = folder + "/" + fileinfo.name + "/" + pattern;
					ls(subdir, files);
				}
			}
			else
			{
				// 不是目录，是文件
				files.push_back(folder + "/" + fileinfo.name);
			}
		}
		while (_findnext(handle, &fileinfo) == 0);

		_findclose(handle);
		return true;
	}

	std::string get_current_path()
	{
		llvm::SmallString<2048> path;
		std::error_code err = llvm::sys::fs::current_path(path);
		return err ? "" : path.str();
	}
}

namespace timetool
{
	std::string get_now(const char* format /* = "%04d/%02d/%02d-%02d:%02d:%02d" */)
	{
		if (strtool::is_empty(format))
		{
			return "";
		}

		time_t now;
		time(&now);

		tm *localnow = localtime(&now); // 转为本时区时间

		char buf[128] = { 0 };
		sprintf_s(buf, sizeof(buf), format, 1900 + localnow->tm_year, 1 + localnow->tm_mon, localnow->tm_mday,
		          localnow->tm_hour, localnow->tm_min, localnow->tm_sec);
		return buf;
	}
}

namespace ticktool
{
	// 获取CPU每秒的滴答次数
	uint64_t GetTickFrequency()
	{
		static LARGE_INTEGER static_perfFreq = { 0 };
		if (0 == static_perfFreq.QuadPart) {
			QueryPerformanceFrequency(&static_perfFreq);
		}

		return static_perfFreq.QuadPart;
	}

	uint64_t tick()
	{
		LARGE_INTEGER tick_now;

		QueryPerformanceCounter(&tick_now);
		return tick_now.QuadPart;
	}

	// 返回两次时钟周期的秒差
	double tickDiff(uint64_t old_tick)
	{
		uint64_t tick_now = tick();
		uint64_t diff = tick_now - old_tick;

		double s = (double)diff / GetTickFrequency();
		return s;
	}
}