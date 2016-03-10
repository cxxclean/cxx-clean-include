enum Fruit
{
	Apple,
	Orange,
	Banana
};

void a_top_echo(const char *text)
{
}

static inline void a_top_print()
{
}

class A
{
public:
	void A_func1()
	{

	}

	void A_func2();

private:
	char m_name[128];
};

void A::A_func2()
{

}

int hello1()   ;

void hello2()   ;

const char* hello3();

template <typename T>
bool is_equal(T &x, T &y)
{
	return x == y;
}

int sum(int n)
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

Fruit getFruit()
{
	return Banana;
}

template<typename T>
class A_Color
{
	enum Color
	{
		// constants for file positioning options
		Green,
		Yellow,
		Blue,
		Red
	};

private:
	static const Color color = (Color)2;
};
