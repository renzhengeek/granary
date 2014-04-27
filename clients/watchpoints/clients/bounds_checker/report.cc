/* Copyright 2012-2013 Peter Goodman, all rights reserved. */
/*
 * report.cc
 *
 *  Created on: 2013-10-22
 *      Author: Peter Goodman
 */

#include "clients/instr_dist/instrument.h"

extern "C" {
    extern int sprintf(char *buf, const char *fmt, ...);
}

using namespace granary;

namespace client {
   enum {
       BUFF_SIZE = 1500,
	   BUFF_FLUSH = 1000
   };


   /// Output buffer.
   char BUFF[BUFF_SIZE] = {'\0'};
   int n = 0;


   /// Report on watchpoints statistics.
   void report(void) throw() {
        if(BUFF_FLUSH <= n) {
               granary::log(BUFF, n);
               n = 0;
        }

       if(0 < n) {
           granary::log(BUFF, n);
       }
   }
}


