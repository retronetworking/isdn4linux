/* $Id$
 * 
 * (c) 2000 Cytronics & Melware
 *
 *  This file is (c) under GNU PUBLIC LICENSE
 *  For changes and modifications please read
 *  ../../../Documentation/isdn/README.eicon
 *
 * $Log$
 *
 *
 */
 
#include <linux/kernel.h>

char *
file_check(void) {

#ifdef FILECHECK
#if FILECHECK == 0
	return("verified");
#endif
#if FILECHECK == 1 
	return("modified");
#endif
#if FILECHECK == 127 
	return("verification failed");
#endif
#else
	return("not verified");
#endif
}
