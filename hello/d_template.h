#include <set>

template <typename T>
std::set<T> split_str_to_int_set()
{
	typedef std::set<T> intset_t;
	intset_t intset;

	return intset;
}

template<typename T>
class D_Color
{
	enum Color
	{
		Green,
		Yellow,
		Blue,
		Red
	};

private:
	static const Color color = (Color)2;
};
