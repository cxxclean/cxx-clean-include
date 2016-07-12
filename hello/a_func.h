// #include <stdio.h>

#include "a.h"

void A::A_ClassMemberDelayImplementFunc3()
{

}

void A_Func(int a, const char* b)
{
}

#define  Macro_A_Func(a, b) A_Func(a, b)

//------------ 测试clang无法识别下面格式的重载函数问题 ------------//

class IBase
{
};

class A_Derived : public IBase
{
};

class A_OverloadBug
{
public:
	void func(const char* c, IBase&){}

	void func(unsigned int c, IBase&){}

public:
	static A_OverloadBug* m_a;
};

A_OverloadBug* A_OverloadBug::m_a = new A_OverloadBug;