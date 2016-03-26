#define _CRT_SECURE_NO_WARNINGS
#include <sstream>
#include <fstream>
#include <sys/stat.h>
#include <io.h>
#include <time.h>						// 测试localtime未成功引用的情况


#include <windows.h>
#include <math.h>

#include <stdio.h>
#include <stdio.h>
#include <stdio.h>
#include <stdio.h>
#include <stdio.h>
#include <stdio.h>
#include <stdio.h>
#include <stdio.h>
#include <stdio.h>
#include <stdio.h>
#include <stdio.h>
#include "a_func.h"						// 测试函数			:a_func.h
#include "b_class.h"					// 测试类			:b_class.h
#include "c_macro.h"					// 测试宏			:c_macro.h
#include "d_template.h"					// 测试模板			:d_template.h
#include "e_typedef.h"					// 测试自定义		:e_typedef.h
#include "f_forwarddecl.h"				// 测试前置声明		:f_forwarddecl.h
#include "g_default_arg.h"				// 测试默认参数		:g_default_arg.h
#include "h_use_forwarddecl.h"			// 测试使用前置声明	:h_use_forwarddecl.h
#include "i_deeply_include.h"			// 测试多层#include	:i_deeply_include.h
#include "j_enumeration.h"				// 测试枚举			:j_enumeration.h

#include "../hello/../hello/nil1.h"		// 测试无用#include	:j_enumeration.h
#include "nil1.h"
#include "nil1.h"
#include "nil1.h"
#include "nil1.h"
#include "nil1.h"
#include "nil1.h"
#include "nil1.h"
#include "nil1.h"
#include "nil1.h"
#include "nil1.h"
#include "nil1.h"
#include "nil1.h"
#include "nil1.h"
#include "nil1.h"
#include "nil2.h"

#include <iostream>

using namespace std;

class C
{
	C()
	{
	}

	F_Fowward *g_f1;
};

int nil1_func_has_implementation()
{
	return 0;
}

socket_t createSocket()
{
	return 0;
}

Tutorial* hello4();

Tutorial g_t;

I g_i;

template <typename T>
bool not_equal(T &x, T &y)
{
	return x != y;
}

A* createA()
{
	return new A;
}

template<typename T>
const typename A_Color<T>::Color A_Color<T>::color;

B1* createB1()
{
	return new B1;
}

class BB_Derived : public BB
{
	void print()
	{

	}

private:
	int m_sum;
	int m_max;
};

void func_test()
{
	int s = sum(100);

	if (is_equal(s, s))
	{
		s = 100;
	}

	//bool n = not_equal(s, s);
}

void macro_test()
{
	bool equal = expect(100, 200);
	Double_Typedef *pi = new Double_Typedef;
}

inline bool is_slash(char c)
{
	return (c == '\\' || c == '/');
}

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

namespace strtool
{
	// 替换字符串，传入的字符串将被修改
	// 例如：replace("this is an expmple", "is", "") = "th  an expmple"
	// 又如: replace("acac", "ac", "ca") = "caca"
	string& replace(string &str, const char *old, const char* to)
	{
		string::size_type pos = 0;
		int len_old = strlen(old);
		int len_new = strlen(to);

		while ((pos = str.find(old, pos)) != string::npos)
		{
			str.replace(pos, len_old, to);
			pos += len_new;
		}

		return str;
	}
}

string native_platform_path(const char *path)
{
	string result = path;

#ifdef _WIN32
	// Canonicalise directory separators (forward slashes considered canonical.)
	for (size_t i = 0; i < result.size(); ++i)
	{
		if (result[i] == '\\')
			result[i] = '/';
	}
#endif

	// We may also want to collapse ../ here.

	return result;
}

std::string simplify_path(const char* path)
{
	string native_path = native_platform_path(path);
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

void test_simplify_path()
{
	const char* strs[] =
	{
		"D:\\proj\\dummygit\\server\\src\\server\\.\\net/serverlink.h",
		"d:/a/b/c/../../././../../d/a/b/c/d/../../../",
		"D:\\proj\\dummygit\\server\\src\\server\\../../3rd/protobuf/protobuf-2.5.0/src/",
		"D:\\proj\\dummygit\\server\\src\\server\\../../3rd/glog-0.3.3/src/windows/",
		"D:\\proj\\dummygit\\server\\src\\server\\../../3rd/rapidjson/include/",
		"C:/Program Files (x86)/Microsoft Visual Studio 12.0/VC/include/",
		"D:\\proj\\dummygit\\server\\src\\server\\../../3rd/mysql/include/",
		"D:\\proj\\dummygit\\server\\src\\server\\../../3rd/curl/include/",
		"C:/Program Files (x86)/Windows Kits/8.1/include/shared/",
		"C:/Program Files (x86)/Windows Kits/8.1/include/winrt/",
		"C:/Program Files (x86)/Windows Kits/8.1/include/um/",
		"D:\\proj\\dummygit\\server\\src\\server\\../protocol/",
		"D:\\proj\\dummygit\\server\\src\\server\\../../3rd/",
		"D:\\proj\\dummygit\\server\\src\\server\\./"
	};

	for (int i = 0; i < sizeof(strs) / sizeof(char*); ++i)
	{
		const char* str = strs[i];
		string out = simplify_path(str);
		std::cout << "old = " << str << ", new = " << out << std::endl;
	}
}

void test_relative_path()
{
	{
		const char *path_1 = "D:\\proj\\linux\\llvm\\build\\tools\\clang\\utils\\TableGen\\INSTALL.vcxproj";
		const char *path_2 = "D:\\proj\\linux\\llvm\\hello\\hello.cpp";

		std::string relative_path1 = get_relative_path(path_1, path_2);
		std::string relative_path2 = get_relative_path(path_2, path_1);
	}

	{
		const char *path_1 = "/home/tmp/a.h";
		const char *path_2 = "/include/3rd/hello.h";

		std::string relative_path1 = get_relative_path(path_1, path_2);
		std::string relative_path2 = get_relative_path(path_2, path_1);
	}

	{
		const char *path_1 = "D:\\proj\\linux\\llvm\\hello\\hello1.cpp";
		const char *path_2 = "D:\\proj\\linux\\llvm\\hello\\hello2.cpp";

		std::string relative_path1 = get_relative_path(path_1, path_2);
		std::string relative_path2 = get_relative_path(path_2, path_1);
	}
}

void test_bug1_localtime()
{
	time_t now;
    //time(&now);

	struct tm *local_now = localtime(&now); //取得当地时间
}

void test()
{
	test_relative_path();
	test_simplify_path();
}

int main(int argc, char **argv)
{
	test();
	return 0;
}