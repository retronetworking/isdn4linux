/* $Id$
 *
 * header for Linux ISDN subsystem, functions for synchronous PPP (linklevel).
 *
 * Copyright 1995,96 by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
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
 */

extern void isdn_ppp_timer_timeout(void);
extern int  isdn_ppp_read(int , struct file *, char *, int);
extern int  isdn_ppp_write(int , struct file *, FOPS_CONST char *, int);
extern int  isdn_ppp_open(int , struct file *);
extern int  isdn_ppp_init(void);
extern void isdn_ppp_cleanup(void);
extern int  isdn_ppp_free(isdn_net_local *);
extern int  isdn_ppp_bind(isdn_net_local *);
extern int  isdn_ppp_xmit(struct sk_buff *, struct device *);
extern void isdn_ppp_receive(isdn_net_dev *, isdn_net_local *, u_char *, int);
extern int  isdn_ppp_dev_ioctl(struct device *, struct ifreq *, int);
extern void isdn_ppp_free_mpqueue(isdn_net_dev *);
extern int  isdn_ppp_select(int, struct file *, int, select_table *);
extern int  isdn_ppp_ioctl(int, struct file *, unsigned int, unsigned long);
extern void isdn_ppp_release(int, struct file *);
