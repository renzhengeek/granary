/* Copyright 2012-2013 Peter Goodman, all rights reserved. */
/*
 * report.cc
 *
 *  Created on: 2013-10-22
 *      Author: Peter Goodman
 */
#include "report.h"

using namespace granary;

namespace client {

   /// Output buffer.
   char BUFF[BUFF_SIZE] = {'\0'};
   int buf_idx = 0;


   /// Report on watchpoints statistics.
   void report(void) throw() {
       if(0 < buf_idx) {
           granary::log(BUFF, buf_idx);
		   buf_idx = 0;
       }
   }
}


