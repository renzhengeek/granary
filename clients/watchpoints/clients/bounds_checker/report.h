/*************************************************************************
	> File Name: report.h
	> Author: Ren Zhen
	> Mail: renzhengeek.com 
	> Created Time: 2014年05月14日 星期三 13时47分13秒
 ************************************************************************/
#ifndef __REPORT_H__
#include "granary/client.h"

using namespace granary;


extern "C" {
	    extern int sprintf(char *buf, const char *fmt, ...);
}

namespace client{

	enum {
	    BUFF_SIZE = 1500,
		BUFF_FLUSH = 1000
	};


   // Output buffer.
      extern char BUFF[BUFF_SIZE];
      extern int buf_idx;
}
#endif	//__REPORT_H__
