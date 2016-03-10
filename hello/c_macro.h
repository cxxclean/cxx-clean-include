#include "c.h"

#define max_6(a, b, c, d, e, f)	max_2(max_5(a,b,c,d,e), f)

int test_in_c()
{
	int c1 = upper('a');
	int c3 = max_3(1, 2, 3);
	int c4 = max_4(1, 2, 3, 4);
	int c5 = max_6(1, 2, 3, 4, 5, 6);
	return 0;
}

#ifdef expect
	#undef expect
	#define expect(a, b) a == b * 100
#endif

typedef double Double_Typedef;