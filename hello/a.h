int A_TopFunc(int n)
{
	int s = 0;

	for(int i = 0; i < n; ++i)
	{
		if (i % 3 == 0)
		{
			s += i;
		}
		else if(i < 500)
		{
			s += i /2;
		}
	}

	return s;
}

static inline void A_TopStaticInlineFunc()
{
}

class A
{
public:
	void A_ClassMemberFunc()
	{

	}

	void A_ClassMemberDelayImplementFunc3();

	static int A_StaticClassMemberFunc()
	{
		return 0;
	}

	static int A_FuncPointer()
	{
		return 0;
	}

private:
	char m_name[128];
};

const char* A_DeclareFunc()   ;

template <typename T>
int A_TemplateFunc(T x, T y)
{
	return x > y ? x : y;
}

class A1
{
public:
	int aaaaaaa;
};

class A2
{
public:
	int bbbbbbb;
};

template<typename T1, typename T2>
void convert(const T1& t1, T2& t2);

template<typename T1, typename T2>
void convert2(const T1& t1, T2& t2)
{
	convert(t1, t2);
}