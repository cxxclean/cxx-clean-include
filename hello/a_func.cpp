#include "a_func.h"

void A::A_ClassMemberDelayImplementFunc3()
{
}

A_OverloadBug* A_OverloadBug::m_a = new A_OverloadBug;

///////////////////// 1. 测试a_func.h：某个文件内的函数如果被使用到，则对应的#include应该被保留 /////////////////////

int a = 99999;

void A_Func_Test(A_Derived *derived)
{
	derived->test();

	int n = 0;

	n = A_TopFunc(100);
	n = A_TemplateFunc(100, 200);
	n = A::A_StaticClassMemberFunc();
	n = unsigned int(0); // 这里会有编译错误，疑似clang的bug，clang仅支持int(0)，却不支持unsigned int(0)，这里要对clang\lib\Parse\ParseExprCXX.cpp中的Parser::ParseCXXSimpleTypeSpecifier方法作修改使其while循环解析才不会有编译错误

	auto func = A::A_FuncPointer;

	A a;
	a.A_ClassMemberFunc();

	Macro_A_Func(100, "abcdefg");

	A_OverloadBug::m_a->func("", A_Derived());

	size_t s = sizeof(A_Func(10, ""));

	A1 a1;
	A2 a2;
	convert2(a1, a2);
}