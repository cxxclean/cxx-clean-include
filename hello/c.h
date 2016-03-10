#define upper(a)				a + 1000
#define expect(a, b)			a == b
#define max_2(a,b)				(((a) > (b)) ? (a) : (b))
#define max_3(a, b, c)			max_2(max_2(a,b), c)
#define max_4(a, b, c, d)		max_2(max_2(a,b), max_2(c,d))
#define max_5(a, b, c, d, e)	max_2(max_3(a,b,c), max_2(d,e))
