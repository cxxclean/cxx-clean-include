#include "c.h"

#define C_Macro_6(a, b, c, d, e, f)	C_Macro_2(C_Macro_5(a,b,c,d,e), f)

#ifdef C_MacroRedefine
	#undef C_MacroRedefine
	#define C_MacroRedefine(a, b) a == b * 100
#endif

#define C_MacroUndefine