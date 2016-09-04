#include "c_macro.h"

///////////////////// 3. 测试c_macro.h：处理宏的展开和使用 /////////////////////

#if defined C_IfDefined
	#define ok
#endif

#undef C_MacroUndefine

void C_Macro_Test()
{
	int c1 = C_Macro_1('a');
	int c3 = C_Macro_3(1, 2, 3);
	int c4 = C_Macro_4(1, 2, 3, 4);
	int c5 = C_Macro_6(1, 2, 3, 4, 5, 6);

	bool c = C_MacroRedefine(100, 200);
}
