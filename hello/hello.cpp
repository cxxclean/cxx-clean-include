#define _CRT_SECURE_NO_WARNINGS
#include <time.h>						// 测试localtime未成功引用的情况
#include <sstream>
#include <fstream>
#include <sys/stat.h>
#include <io.h>
#include <windows.h>
#include <math.h>

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
#include "k_namespace.h"				// 测试命名空间		:k_namespace.h

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
	int n = 0;
	
	n = A_TopFunc(100);
	n = A_TemplateFunc(100, 200);
	n = A::A_StaticClassMemberFunc();
	
	auto func = A::A_FuncPointer;

	A a;
	a.A_ClassMemberFunc();
}

template<typename T>
const typename D_Color<T>::Color D_Color<T>::color;

///////////////////// 2. 测试b_class.h：某个文件内的类如果被使用到，则对应的#include应该被保留 /////////////////////

B_Class		b_Class;
B_Struct	b_Struct;
B_Union		b_Union;

class B_DerivedClass : public B_BaseClass
{
	void print(){}

private:
	int m_num;
	int not;
};

B_ReturnClass GetClass()
{
	return B_ReturnClass();
}

B_ReturnReferenceClass& GetClassReference()
{
	return *(new B_ReturnReferenceClass);
}

void B_Class_Test()
{
	new B_NewClass;
	B_ImplicitConstructorClass b_ImplicitConstructorClass;
	B_ExplicitConstructorClass b_ExplicitConstructorClass(100, 200);
	B_NoNameClass();
	B_ClassPointer *b_ClassPointer;
}

///////////////////// 3. 测试c_macro.h：处理宏的展开和使用 /////////////////////

#undef C_MacroUndefine

void C_Macro_Test()
{
	int c1 = C_Macro_1('a');
	int c3 = C_Macro_3(1, 2, 3);
	int c4 = C_Macro_4(1, 2, 3, 4);
	int c5 = C_Macro_6(1, 2, 3, 4, 5, 6);

	bool c = C_MacroRedefine(100, 200);
}

///////////////////// 5. 测试e_typedef.h：处理typedef /////////////////////


E_TypedefInt GetTypedefInt()
{
	return 0;
}

void E_Typedef_Test()
{
	E_TypedefTemplateClass::type j = 0;
}

///////////////////// 6. 测试f_forwarddecl.h：处理前置声明 /////////////////////

F_Fowward *f_Fowward;

///////////////////// 7. 测试g_default_arg.h：处理默认参数 /////////////////////

G_DefaultArgument g_DefaultArgument;


///////////////////// 8. 测试h_use_forwarddecl.h：处理重复声明的情况 /////////////////////

H* h_Redeclare;

///////////////////// 9. 测试i_deeply_include.h： 处理真正有用的文件被#include在最深层的情况 /////////////////////

I i_DeeplyInclude;

///////////////////// 10. 测试j_enumeration.h： 处理真正有用的文件被#include在最深层的情况 /////////////////////

int j_Enum = J_Enum_4;

///////////////////// 11. 测试k_namespace.h：如果某个文件就using namespace有用，其他啥都没用，可以考虑把using namespace挪出来 /////////////////////

K k_Namespace;
K1 k1_Namespace;
k_ns::k_2::K2 k2_Namespace;

///////////////////// 12. 测试nil.h：nil.h中的内容不应被识别为被引用 /////////////////////

int nil1_func_has_implementation()
{
	return 0;
}

/////////////////////  main /////////////////////

int main(int argc, char **argv)
{
	return 0;
}