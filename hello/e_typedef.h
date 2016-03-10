///<------------------------------------------------------------------------------
//< @file:   e_typedef.h
//< @author: 洪坤安
//< @date:   2016年2月8日
//< @brief:	 
//< Copyright (c) 2015 game. All rights reserved.
///<------------------------------------------------------------------------------

#ifndef _e_typedef_h_
#define _e_typedef_h_

#include "e.h"

// 申请新的socket
socket_t createSocket();

typedef E<socket_t> E_Typedef;

#endif // _e_typedef_h_