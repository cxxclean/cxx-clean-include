namespace ns1
{
	namespace ns2
	{
		namespace ns3
		{
			class B_NewClass{};

			class B_ImplicitConstructorClass{};

			class B_ExplicitConstructorClass
			{
			public:
				B_ExplicitConstructorClass(int x, int y)
				{
					m_sum = x + y;
				}

				int m_sum;
			};

			class B_NoNameClass{};
		}
	}
}

using ns1::ns2::ns3::B_NewClass;
using ns1::ns2::ns3::B_ImplicitConstructorClass;
using ns1::ns2::ns3::B_ExplicitConstructorClass;
using ns1::ns2::ns3::B_NoNameClass;

class B_Class{};

union B_Union
{
	int a;
	int b;
	char c;
};

struct B_Struct{};

class B_BaseClass{
public:
	static void Print(){}
};

class B_ClassPointer{};
class B_ReturnClass{};
class B_ReturnReferenceClass{};

class B_1{
public:
	void test(){}
};

typedef B* BBBB;