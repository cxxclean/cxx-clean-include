#include "b_class.h"

///////////////////// 2. 测试b_class.h：某个文件内的类如果被使用到，则对应的#include应该被保留 /////////////////////

B_Ctor::B_Ctor()
	: m_class(NULL)
{
}

B_Class		b_Class;
B_Struct	b_Struct;
B_Union		b_Union;
BBBB*		bbbbb;

class B_DerivedClass : public B_BaseClass
{
	void print() {}

private:
	int m_num;
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

	B_TempClass b = GetB_TempClass();
	B_DerivedFunction::Print();

	B_5 b5;
	b5.m_b4->m_b3->m_b2->m_b1->test();
}

template<typename T>
const typename B_Color<T>::Color B_Color<T>::color;