/*
 * $Id$
 *
 * CAPI4Linux
 *
 * (c) Copyright 2000 by Carsten Paeth (calle@calle.de)
 *
 *
 * 2001-02-06 : Module moved from avmb1 directory.
 *              Armin Schindler (mac@melware.de)
 *
 *
 *
 */

void capifs_new_ncci(char type, unsigned int num, kdev_t device);
void capifs_free_ncci(char type, unsigned int num);
