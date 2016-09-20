#ifndef _f_forwarddecl_h_
#define _f_forwarddecl_h_

#include "f_4.h"
#include "f_3.h"
#include "f_2.h"
#include "f_1.h"

class F_Fowward
{
public:
	F_Fowward(F_1& f)
		: m_f1(f)
	{
	}

public:
	F_1 m_f1;
};

#endif // _f_forwarddecl_h_