/* $Id$
 *
 * Variable-debugging for isdn4linux.
 *
 * Copyright 1994 by Fritz Elfert (fritz@wuemaus.franken.de)
 * Copyright 1995 Thinking Objects Software GmbH Wuerzburg
 *
 * This file is part of Isdn4Linux.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 * $Log$
 * Revision 1.3  1996/04/28 15:19:23  fritz
 * adapted to new ioctl names.
 *
 * Revision 1.2  1996/01/04 02:46:16  fritz
 * Changed copying policy to GPL.
 *
 * Revision 1.1  1995/12/18  18:22:52  fritz
 * Initial revision
 *
 */

#include <sys/types.h>
#include <sys/fcntl.h>
/* #include <sys/mman.h> */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <linux/isdn.h>
#include <linux/isdnif.h>

typedef unsigned char uchar;
int mem_fd;

#if 0 /* Weiss der Teufel, warum das nicht mit mmap geht */
uchar
*mapmem (ulong location, long size) {
  ulong mmseg;
  ulong mmsize;
  ulong	addr;
  ulong offset;
  
  mmseg  = location & 0xffffe000L;
  mmsize = (location - mmseg + size) + 0x1000;
  offset = location - mmseg;
  addr = (ulong)mmap(0,mmsize,PROT_READ,MAP_SHARED,mem_fd,mmseg);
  if ((int)addr == -1) {
    perror ("mmap");
    exit (1);
  }
  printf("mmap: loc=%08lx siz=%08lx mseg=%08lx msiz=%08lx ofs=%08lx adr=%08lx\n",location,size,mmseg,mmsize,offset,addr);
  return ((uchar*)(addr+offset));
}
#else
uchar
*mapmem (ulong location, long size) {
  uchar *buffer;

  if ((buffer = malloc(size))) {
    lseek(mem_fd,location,SEEK_SET);
    read(mem_fd,buffer,size);
  } else {
    perror ("malloc");
    exit (1);
  }
  return buffer;
}
#endif

char *
dumpIntArray(int *arr, int count) {
  static char buf[1024];
  char *p = buf;
  int i;

  for(i=0;i<count;i++)
    p += sprintf(p,"%d%s",arr[i],(i<(count-1))?", ":"\0");
  return(buf);
}

char *
dumpCharArray(char *arr, int count) {
  static char buf[1024];
  char *p = buf;
  int i;

  for(i=0;i<count;i++)
    p += sprintf(p,"%02x%s",(uchar)arr[i],(i<(count-1))?", ":"\0");
  return(buf);
}

char *
dumpStringArray(char *arr, int count, int len) {
  static char buf[1024];
  char *p = buf;
  char *s = arr;
  int i;

  for(i=0;i<count;s+=len,i++)
    p += sprintf(p,"\"%s\"%s",s,(i<(count-1))?", ":"\0");
  return(buf);
}

void
dumpDriver(ulong drvaddr) {
  driver *drv = (driver *)mapmem(drvaddr,sizeof(driver));
  isdn_if *ifc = (isdn_if *)mapmem((ulong)drv->interface,sizeof(isdn_if));

  printf("  flags      = %08lx\n",drv->flags);
  printf("  channels   = %d\n",drv->channels);
  printf("  reject_bus = %d\n",drv->reject_bus);
  printf("  maxbufsize = %d\n",drv->maxbufsize);
  printf("  pktcount   = %ld\n",drv->pktcount);
  printf("  running    = %d\n",drv->running);
  printf("  loaded     = %d\n",drv->loaded);
  printf("  stavail    = %d\n",drv->stavail);
  printf("  Interface @%08lx:\n",(ulong)drv->interface);
  printf("    Id(ch)     = %d\n",ifc->channels);
  printf("    maxbufsize = %d\n",ifc->maxbufsize);
  printf("    features   = %08lx\n",ifc->features);
}

void
dumpModem(modem mdm) {
  int i;
  atemu *atm;
  modem_info *info;

  for (i=0;i<ISDN_MAX_CHANNELS;i++) {
    printf("mdm [%02d]\n",i);
    printf("  msr            = %08x\n",mdm.msr[i]);
    printf("  mlr            = %08x\n",mdm.mlr[i]);
    printf("  refcount       = %d\n",mdm.refcount);
    printf("  online         = %d\n",mdm.online[i]);
    printf("  dialing        = %d\n",mdm.dialing[i]);
    printf("  rcvsched       = %d\n",mdm.rcvsched[i]);
    printf("  ncarrier       = %d\n",mdm.ncarrier[i]);
    printf("  atmodem:\n");
    atm = &mdm.atmodem[i];
    printf("    profile      = \n      %s\n",dumpCharArray(atm->mdmreg,ISDN_MODEM_ANZREG));
    printf("    mdmreg       = \n      %s\n",dumpCharArray(atm->mdmreg,ISDN_MODEM_ANZREG));
    printf("    msn          = \"%s\"\n",atm->msn);
    printf("    mdmcmdl      = %d\n",atm->mdmcmdl);
    printf("    pluscount    = %d\n",atm->pluscount);
    printf("    lastplus     = %08x\n",atm->lastplus);
    printf("    mdmcmd       = \"%s\"\n",atm->mdmcmd);
    printf("  info:\n");
    info = &mdm.info[i];
    printf("    magic        = %08x\n",info->magic); 
    printf("    flags        = %08x\n",info->flags); 
    printf("    type         = %d\n",info->type); 
    printf("    x_char       = %02x\n",info->x_char); 
    printf("    close_delay  = %d\n",info->close_delay); 
    printf("    MCR          = %08x\n",info->MCR); 
    printf("    line         = %d\n",info->line); 
    printf("    count        = %d\n",info->count); 
    printf("    blocked_open = %d\n",info->blocked_open); 
    printf("    session      = %08lx\n",info->session); 
    printf("    pgrp         = %08lx\n",info->pgrp); 
    printf("    isdn_driver  = %d\n",info->isdn_driver); 
    printf("    isdn_channel = %d\n",info->isdn_channel); 
    printf("    drv_index    = %d\n",info->drv_index); 
    printf("    xmit_size    = %d\n",info->xmit_size); 
    printf("    xmit_count   = %d\n",info->xmit_count); 
  }
}

void
dumpNetPhone(ulong paddr) {
  ulong pa = paddr;
  isdn_net_phone *phone;
  
  while (pa) {
    phone = (isdn_net_phone *)mapmem(pa,sizeof(isdn_net_phone));
    printf("    @%08lx: \"%s\"\n",pa,phone->num);
    pa = (ulong)(phone->next);
  }
}

void
dumpNetDev(ulong devaddr) {
  ulong nda = devaddr;
  isdn_net_dev *ndev;

  while (nda) {
    ndev = (isdn_net_dev *)mapmem(nda,sizeof(isdn_net_dev));
    printf("Net-Device @%08lx:\n",nda);
    printf("dev. :\n");
    printf("  start        = %d\n",ndev->dev.start);
    printf("  tbusy        = %ld\n",ndev->dev.tbusy);
    printf("  interrupt    = %d\n",ndev->dev.interrupt);
    printf("local. :\n");
    printf("  name         = \"%s\"\n",ndev->local.name);
    printf("  isdn_device  = %d\n",ndev->local.isdn_device);
    printf("  isdn_channel = %d\n",ndev->local.isdn_channel);
    printf("  ppp_minor    = %d\n",ndev->local.ppp_minor);
    printf("  pre_device   = %d\n",ndev->local.pre_device);
    printf("  pre_channel  = %d\n",ndev->local.pre_channel);
    printf("  exclusive    = %d\n",ndev->local.exclusive);
    printf("  flags        = %d\n",ndev->local.flags);
    printf("  dialstate    = %d\n",ndev->local.dialstate);
    printf("  dialretry    = %d\n",ndev->local.dialretry);
    printf("  dialmax      = %d\n",ndev->local.dialmax);
    printf("  msn          = \"%s\"\n",ndev->local.msn);
    printf("  dtimer       = %d\n",ndev->local.dtimer);
    printf("  p_encap      = %d\n",ndev->local.p_encap);
    printf("  l2_proto     = %d\n",ndev->local.l2_proto);
    printf("  l3_proto     = %d\n",ndev->local.l3_proto);
    printf("  huptimer     = %d\n",ndev->local.huptimer);
    printf("  charge       = %d\n",ndev->local.charge);
    printf("  chargetime   = %08x\n",ndev->local.chargetime);
    printf("  hupflags     = %d\n",ndev->local.hupflags);
    printf("  outgoing     = %d\n",ndev->local.outgoing);
    printf("  onhtime      = %d\n",ndev->local.onhtime);
    printf("  chargeint    = %d\n",ndev->local.chargeint);
    printf("  onum         = %d\n",ndev->local.onum);
    printf("  sqfull       = %08x\n",ndev->local.sqfull);
    printf("  sqfull_stamp = %08lx\n",ndev->local.sqfull_stamp);
    printf("  master       = -> %08x\n",(unsigned int)ndev->local.master);
    printf("  slave        = -> %08x\n",(unsigned int)ndev->local.slave);
    if (ndev->local.phone[0]) {
      printf("  phone[in]:\n");
      dumpNetPhone((ulong)ndev->local.phone[0]);
    } else
      printf("  phone[in]    = NULL\n");
    if (ndev->local.phone[1]) {
      printf("  phone[out]:\n");
      dumpNetPhone((ulong)ndev->local.phone[1]);
    } else
      printf("  phone[out]   = NULL\n");
    printf("  dial         = @%08lx\n",(ulong)ndev->local.dial);
    nda = (ulong)(ndev->next);
  }
}

void
main(int argc, char *argv[], char *envp[]) {
  int f;
  int i;
  static isdn_dev *my_isdndev;
  ulong kaddr;
  
  printf ("\nDebugger for isdn and icn Modules\n");
  f = open("/dev/isdnctrl",O_RDONLY);
  if (ioctl(f,IIOCDBGVAR,&kaddr)) {
    perror("ioctl IIOCDBGVAR");
    exit(-1);
  }
  close(f);
  if ((mem_fd = open ("/dev/kmem",O_RDWR)) < 0) {
    perror ("open /dev/kmem");
    exit (1);
  }
  printf("isdn-main-struct at %08lx (%d):\n",kaddr,sizeof(isdn_dev));
  my_isdndev = (isdn_dev *)mapmem(kaddr,sizeof(isdn_dev));
  printf("isdndev.flags        = %d\n",my_isdndev->flags);
  printf("isdndev.drivers      = %d\n",my_isdndev->drivers);
  printf("isdndev.channels     = %d\n",my_isdndev->channels);
  printf("isdndev.net_verbose  = %d\n",my_isdndev->net_verbose);
  printf("isdndev.modempoll    = %d\n",my_isdndev->modempoll);
  printf("isdndev.tflags       = %d\n",my_isdndev->tflags);
  printf("isdndev.global_flags = %d\n",my_isdndev->global_flags);
  if (my_isdndev->infochain) {
    printf("isdndev.infochain    = @%08lx:\n",(ulong)my_isdndev->infochain);
  } else
    printf("isdndev.infochain    = NULL\n");
  if (my_isdndev->info_waitq) {
    printf("isdndev.info_waitq   = @%08lx:\n",(ulong)my_isdndev->info_waitq);
  } else
    printf("isdndev.info_waitq   = NULL\n");
  printf("isdndev.chanmap      = %s\n",
	 dumpIntArray(my_isdndev->chanmap,ISDN_MAX_CHANNELS));
  printf("isdndev.drvmap       = %s\n",
	 dumpIntArray(my_isdndev->drvmap,ISDN_MAX_CHANNELS));
  printf("isdndev.usage        = %s\n",
	 dumpIntArray(my_isdndev->usage,ISDN_MAX_CHANNELS));
  printf("isdndev.num          = %s\n",
	 dumpStringArray(my_isdndev->num[0],ISDN_MAX_CHANNELS,20));
  printf("isdndev.m_idx        = %s\n",
	 dumpIntArray(my_isdndev->m_idx,ISDN_MAX_CHANNELS));
  dumpModem(my_isdndev->mdm);
  for (i=0;i<ISDN_MAX_DRIVERS;i++)
    if (my_isdndev->drv[i]) {
      printf("isdndev.drv[%02d]      = @%08lx:\n",i,(ulong)my_isdndev->drv[i]);
      dumpDriver((ulong)(my_isdndev->drv[i]));
    }
  if (my_isdndev->netdev) {
    printf("isdndev.netdev       = @%08lx:\n",(ulong)my_isdndev->netdev);
    dumpNetDev((ulong)my_isdndev->netdev);
  } else
    printf("isdndev.netdev       = NULL\n");
  close(mem_fd);
}







