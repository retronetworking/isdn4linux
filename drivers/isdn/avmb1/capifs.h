/*
 * $Id$
 * 
 * (c) Copyright 2000 by Carsten Paeth (calle@calle.de)
 *
 * $Log$
 *
 */

void capifs_new_ncci(__u16 applid, __u32 ncci, char *type, kdev_t device);
void capifs_free_ncci(__u16 applid, __u32 ncci, char *type);
