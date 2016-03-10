namespace b1
{
	class B1
	{
	private:
		char m_name[128];
	};

	void echo()
	{
	}
}

namespace ns1
{
	namespace ns2
	{
		namespace ns3
		{
			class B1
			{

			private:
				int m_num;
			};

			class B2
			{

			private:
				int m_num;
			};
		}
	}
}

using ns1::ns2::ns3::B1;
using ns1::ns2::ns3::B2;

class Tutorial
{
private:
	int m_sum;
};

union HelloUnion
{
	int a;
	int b;
	char c;
};

struct HelloStruct
{
	int a;
	int b;
	char c;
};
