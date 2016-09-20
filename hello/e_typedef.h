#ifndef _e_typedef_h_
#define _e_typedef_h_

#include "e.h"

E_TypedefInt GetTypedefInt();

typedef E_Typedef3 E_Typedef4;

typedef E<E_Typedef4> E_TypedefTemplateClass;

// 下面这段拷贝自c:\program files (x86)\microsoft visual studio 12.0\vc\include\crtdefs.h
extern "C"
{
	typedef struct E_1
	{
		int refcount;
		unsigned int lc_codepage;
		unsigned int lc_collate_cp;
		unsigned long lc_handle[6]; /* LCID */
		struct
		{
			char *locale;
			wchar_t *wlocale;
			int *refcount;
			int *wrefcount;
		} lc_category[6];
		int lc_clike;
		int mb_cur_max;
		int * lconv_intl_refcount;
		int * lconv_num_refcount;
		int * lconv_mon_refcount;
		struct lconv * lconv;
		int * ctype1_refcount;
		unsigned short * ctype1;
		const unsigned short * pctype;
		const unsigned char * pclmap;
		const unsigned char * pcumap;
		struct __lc_time_data * lc_time_curr;
	} E_1_Struct;

	struct E_1;
	struct E_2;
	typedef struct E_1 * E_1_Typedef;
	typedef struct E_2 * E_2_Typedef;

	typedef struct E_3
	{
		E_1_Typedef locinfo;
		E_2_Typedef mbcinfo;
	} E_3_Struct_1, *E_3_Struct_2;
}

#endif // _e_typedef_h_