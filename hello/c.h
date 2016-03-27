#define C_MacroRedefine(a, b)				a == b

#define C_Macro_1(a)				a + 1000
#define C_Macro_2(a,b)				(((a) > (b)) ? (a) : (b))
#define C_Macro_3(a, b, c)			C_Macro_2(C_Macro_2(a,b), c)
#define C_Macro_4(a, b, c, d)		C_Macro_2(C_Macro_2(a,b),	C_Macro_2(c,d))
#define C_Macro_5(a, b, c, d, e)	C_Macro_2(C_Macro_4(a,b,c,d),e)
