#include "d_template.h"

///////////////////// 4. ²âÊÔd_template.h£º´¦ÀíÄ£°å /////////////////////

template <typename T1 = D_1, typename T2 = D_2, typename T3 = D_3, typename T4 = D_Class1<D_Class2<D_Class3<D_4>>>, typename T5 = int>
          char* tostring()
{
	return "";
}

void d_template_test()
{
	std::set<int> nums = split_str_to_int_set<int>();
	tostring<D_1>();
	D_Class<D_4> d;
}