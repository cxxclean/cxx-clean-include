#define _CRT_SECURE_NO_WARNINGS
#include <time.h>						// 测试localtime未成功引用的情况
#include <sstream>
#include <fstream>
#include <sys/stat.h>
#include <io.h>
#include <windows.h>
#include <math.h>
#include <atlcomcli.h>

#define TEXT_C_MACRO_H	"c_macro.h"
#define INCLUDE_C_MACRO_H TEXT_C_MACRO_H

#include "a_func.h"						// 测试函数
#include "b_class.h"					// 测试类
#include INCLUDE_C_MACRO_H				// 测试宏
#include "d_template.h"					// 测试模板
#include "e_typedef.h"					// 测试自定义
#include "f_forwarddecl.h"				// 测试前置声明
#include "g_default_arg.h"				// 测试默认参数
#include "h_use_forwarddecl.h"			// 测试使用前置声明
#include "i_deeply_include.h"			// 测试多层#include
#include "j_enumeration.h"				// 测试枚举
#include "k_namespace.h"				// 测试命名空间
#include "l_use_template.h"				// 模板参数类型
#include "m_using_namespace.h"			// 测试using命名空间
#include "n_same_forward.h"				// 测试前置声明被多次包含
#include "o_nested_class.h"				// 测试前置声明分布在各个文件的情况
#include "p_same_file.h"				// 测试文件被重复包含
#include "q_using_class.h"				// 测试using类

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


#include "../hello/../hello/nil.h"		// 测试无用#include	:j_enumeration.h
#include "nil.h"
#include "nil.h"
#include "nil.h"
#include "nil.h"
#include "nil.h"
#include "nil.h"
#include "nil.h"
#include "nil.h"
#include "nil.h"
#include "nil.h"
#include "nil.h"
#include "nil.h"
#include "nil.h"
#include "nil.h"

#include <iostream>

using namespace std;

///////////////////// 1. 测试a_func.h：某个文件内的函数如果被使用到，则对应的#include应该被保留 /////////////////////

void A_Func_Test()
{
	A_Func(0, "");
}

///////////////////// 2. 测试b_class.h：某个文件内的类如果被使用到，则对应的#include应该被保留 /////////////////////

B_Ctor b;

///////////////////// 3. 测试c_macro.h：处理宏的展开和使用 /////////////////////

int c = C_Macro_1('a');

///////////////////// 4. 测试d_template.h：处理模板 /////////////////////

void d_test()
{
	D_Class<D_4> d;
}

///////////////////// 5. 测试e_typedef.h：处理typedef /////////////////////

void e_test()
{
	E_TypedefTemplateClass::type e = 0;
}

///////////////////// 6. 测试f_forwarddecl.h：处理前置声明 /////////////////////

F_Fowward *f = nullptr;

///////////////////// 7. 测试g_default_arg.h：处理默认参数 /////////////////////

G_DefaultArgument g;


///////////////////// 8. 测试h_use_forwarddecl.h：处理重复声明的情况 /////////////////////

H* h;

///////////////////// 9. 测试i_deeply_include.h： 处理真正有用的文件被#include在最深层的情况 /////////////////////

I i;

///////////////////// 10. 测试j_enumeration.h： 处理枚举 /////////////////////

J_Enum j;

///////////////////// 11. 测试k_namespace.h：如果某个文件就using namespace有用，其他啥都没用，可以考虑把using namespace挪出来 /////////////////////

K k;

///////////////////// 13. 测试l_use_template.h：应能识别模板参数类型 /////////////////////

L5_HashMap l;


///////////////////// 测试nil.h：nil.h不应被重复包含 /////////////////////

int nil1_func_has_implementation()
{
	return 0;
}

/////////////////////  main /////////////////////

int main(int argc, char **argv)
{
	return 0;
}