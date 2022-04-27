//------------------------------------------------------------------------------
// �ļ�: tool.cpp
// ����: ������
// ˵��: �������õ��ĸ��ֻ����ӿ�
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
#include <stdlib.h>
#include <direct.h>

namespace strtool
{
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

	// ��Сд
	std::string tolower(const char *str)
	{
		std::string s(str);
		std::transform(s.begin(), s.end(), s.begin(), ::tolower);

		return s;
	}

	template <typename S /* �ַ������� */, typename C /* �ַ����� */>
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

	// ���ַ������ݷָ����ָ�Ϊ�ַ�������
	void split(const std::string &src, std::vector<std::string> &strvec, char cut /* = ';' */)
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

	// �����ļ���·�������ؽ��ĩβ��'/'��'\'
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

	// �Ƶ�·����ֻ�����ļ�����
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

	// ��������ֱ��ָ���ָ������ַ���
	string trip_at(const string &str, char delimiter)
	{
		string::size_type pos = str.find(delimiter);
		if(pos == string::npos)
		{
			return "";
		}

		return string(str.begin(), str.begin() + pos);
	}

	// ��������ֱ��ָ���ָ������ַ���
	string r_trip_at(const string &str, char delimiter)
	{
		string::size_type pos = str.rfind(delimiter);
		if(pos == string::npos)
		{
			return "";
		}

		return string(str.begin() + pos + 1, str.end());
	}

	// ��ȡ�ļ���׺
	string get_ext(const string &path)
	{
		string file = strip_dir(path);
		if (file.empty())
		{
			return "";
		}

		return r_trip_at(file, '.');
	}

	// ��ӡ
	char g_sprintfBuf[10 * 1024] = {0};
	wchar_t g_swprintfBuf[10 * 1024] = { 0 };

	const char* get_text(const char* fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		vsprintf_s(g_sprintfBuf, sizeof(g_sprintfBuf), fmt, args);
		va_end(args);
		return g_sprintfBuf;
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

	// ��·��ת��linux·����ʽ����·���е�ÿ��'\'�ַ����滻Ϊ'/'
	string to_linux_path(const char *path)
	{
		string ret = path;

		// ��'\'�滻Ϊ'/'
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

	// ǿ�ƽ�·����/��β����·���е�ÿ��'\'�ַ����滻Ϊ'/'
	string fix_path(const string &path)
	{
		string ret = to_linux_path(path.c_str());

		if (!end_with(ret, "/"))
		{
			ret += "/";
		}

		return ret;
	}

	// ����·����ȡ�ļ���
	string get_file_name(const char *path)
	{
        auto f = llvm::sys::path::filename(path);
        return f.str();
	}

	// ��·��
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

		string simplifyFilepath = simplify_path(filepath.c_str());
        return simplifyFilepath;
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

	// ��ȡСд���ļ�·��
	string get_lower_absolute_path(const char *path)
	{
		return tolower(get_absolute_path(path));
	}

	string get_lower_absolute_path(const char *base_path, const char* relative_path)
	{
		return tolower(get_absolute_path(base_path, relative_path));
	}

	// �ı䵱ǰ�ļ���
	bool cd(const char *path)
	{
		return 0 == _chdir(path);
	}

	// ָ��·���Ƿ����
	bool is_dir_exist(const std::string &dir)
	{
		struct _stat fileStat;
		return  ((_stat(dir.c_str(), &fileStat) == 0) && (fileStat.st_mode & _S_IFDIR));
	}

	// ָ��·���Ƿ���ڣ���Ϊ�ļ�·�������ļ���·��
	bool exist(const std::string &path)
	{
		return _access(path.c_str(), 0) != -1;
	}

	// �г�ָ���ļ����µ��ļ����б����ļ��н������ԣ���������windows�������µ�dir
	bool dir(const std::string &path, FileNameVec &files)
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
				// ����Ŀ¼�����ļ�
				files.push_back(filefind.name);
			}
		}
		while(!_findnext(handle, &filefind));

		_findclose(handle);
		return true;
	}

	// �ļ��Ƿ���ָ���ļ����£������ļ��У�
	bool is_at_folder(const char *folder, const char *file)
	{
		return start_with(file, folder);
	}

	// �г�ָ���ļ����µ��ļ����б������ļ����µ��ļ���
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
		if (-1 == handle)
		{
			return false;
		}

		do
		{
			//�ж��Ƿ�����Ŀ¼
			if (fileinfo.attrib & _A_SUBDIR)
			{
				// ���Ե�ǰĿ¼���ϲ�Ŀ¼
				if( (strcmp(fileinfo.name,".") != 0) &&(strcmp(fileinfo.name,"..") != 0))
				{
					string subdir = folder + "/" + fileinfo.name + "/" + pattern;
					ls(subdir, files);
				}
			}
			else
			{
				// ����Ŀ¼�����ļ�
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
		return err.value() > 0 ? "" : path.c_str();
	}
}

namespace htmltool
{
	std::string escape_html(const char* html)
	{
		return escape_html(std::string(html));
	}

	std::string escape_html(const std::string &html)
	{
		std::string ret(html);

		strtool::replace(ret, "<", "&lt;");
		strtool::replace(ret, ">", "&gt;");

		return ret;
	}

	std::string get_file_html(const char *filename)
	{
		std::string html = R"--(<a href="#{file}">#{file}</a>)--";
		strtool::replace(html, "#{file}", filename);

		return html;
	}

	std::string get_short_file_name_html(const char *filename)
	{
		std::string html = R"--(<a href="#{filepath}">#{filename}</a>)--";
		strtool::replace(html, "#{filepath}", filename);
		strtool::replace(html, "#{filename}", pathtool::get_file_name(filename).c_str());

		return html;
	}

	std::string get_include_html(const std::string &text)
	{
		return strtool::get_text(R"--(<span class="src">%s</span>)--", htmltool::escape_html(text).c_str());
	}

	std::string get_number_html(int num)
	{
		return strtool::get_text(R"--(<span class="num">%s</span>)--", strtool::itoa(num).c_str());
	}

	std::string get_warn_html(const char *text)
	{
		return strtool::get_text(R"--(<span class="num">%s</span>)--", text);
	}
}

namespace timetool
{
	std::string get_now(const char* format)
	{
		if (strtool::is_empty(format))
		{
			return "";
		}

		time_t now;
		time(&now);

		// תΪ��ʱ��ʱ��
		tm *localnow = localtime(&now);

		char buf[128] = { 0 };
		sprintf_s(buf, sizeof(buf), format,
				  1900 + localnow->tm_year, 1 + localnow->tm_mon, localnow->tm_mday,
				  localnow->tm_hour, localnow->tm_min, localnow->tm_sec
				 );

		return buf;
	}
}

namespace logtool
{
	llvm::raw_ostream &log()
	{
		if (HtmlLog::instance->m_log)
		{
            return *HtmlLog::instance->m_log;
		}

		return llvm::errs();
	}
}
