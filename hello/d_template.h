#include <set>

#include <algorithm>
#include <cctype>
#include <string>

static std::string &ltrim(std::string &s)
{
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::isspace));
	return s;
}

class D_1{};
class D_2{};
class D_3{};
class D_4{};

template <typename T>
std::set<T> split_str_to_int_set()
{
	typedef std::set<T> intset_t;
	intset_t intset;

	return intset;
}

template <typename T>
class D_Class
{
};

template <typename T>
class D_Class1
{
};

template <typename T>
class D_Class2
{
};

template <typename T>
class D_Class3
{
};