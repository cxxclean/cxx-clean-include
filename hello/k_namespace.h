#ifndef _k_namespace_h_
#define _k_namespace_h_

namespace k_ns
{
	namespace k_1
	{
		void k_function_in_namespace();
	}
}

using namespace k_ns::k_1;

#include "k.h"

// 这里要测试的情况是：假设本文件啥也没有，就一个using namespace，但取消包含本文件却会导致编译错误
using namespace k_ns;

#endif // _k_namespace_h_