/* $Id$
 *
 * DIDD Interface module for Eicon active cards.
 * 
 * Functions are in dadapter.c 
 * 
 * Copyright 2000 by Armin Schindler (mac@melware.de) 
 * Copyright 2000 Cytronics & Melware (info@melware.de)
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
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>

#include "platform.h"
#include "di_defs.h"
#include "dadapter.h"
#include "divasync.h"
#include "did_vers.h"

#include <linux/isdn_compat.h>

static char *main_revision = "$Revision$";

static char *DRIVERNAME = "Eicon DIVA - DIDD table (http://www.melware.net)";
static char *DRIVERLNAME = "divadidd";
static char *DRIVERRELEASE = "1.0beta6";

static char *dir_in_proc_net = "isdn";
static char *main_proc_dir = "eicon";

MODULE_DESCRIPTION(             "DIDD table driver for diva drivers");
MODULE_AUTHOR(                  "Cytronics & Melware, Eicon Networks");
MODULE_SUPPORTED_DEVICE(        "Eicon diva drivers");

#define MAX_DESCRIPTORS  32

#define DBG_MINIMUM  (DL_LOG + DL_FTL + DL_ERR)
#define DBG_DEFAULT  (DBG_MINIMUM + DL_XLOG + DL_REG)


extern void DIVA_DIDD_Read (void *, int);
static dword notify_handle;
static DESCRIPTOR _DAdapter;

static struct proc_dir_entry *proc_net_isdn;
static struct proc_dir_entry *proc_didd;
struct proc_dir_entry *proc_net_isdn_eicon = NULL;

EXPORT_SYMBOL_NOVERS(DIVA_DIDD_Read);
EXPORT_SYMBOL_NOVERS(proc_net_isdn_eicon);

static char *
getrev(const char *revision)
{
	char *rev;
	char *p;
	if ((p = strchr(revision, ':'))) {
		rev = p + 2;
		p = strchr(rev, '$');
		*--p = 0;
	} else rev = "1.0";
	return rev;
}

static int
proc_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
  int len = 0;
  char tmprev[32];

  strcpy(tmprev, main_revision);
  len += sprintf(page+len, "%s\n", DRIVERNAME);
  len += sprintf(page+len, "name     : %s\n", DRIVERLNAME);
  len += sprintf(page+len, "release  : %s\n", DRIVERRELEASE);
  len += sprintf(page+len, "build    : %s(%s)\n",
            diva_didd_common_code_build, DIVA_BUILD);
  len += sprintf(page+len, "revision : %s\n", getrev(tmprev));

  if (off + count >= len)
    *eof = 1;
  if (len < off)
    return 0;
  *start = page + off;
  return((count < len-off) ? count : len-off);
}

static int __init create_proc(void)
{
  struct proc_dir_entry *pe;

  for (pe = proc_net->subdir; pe; pe = pe->next) {
    if (!memcmp(dir_in_proc_net, pe->name, pe->namelen)) {
        proc_net_isdn = pe;
        break;
    }
  }
  if (!proc_net_isdn) {
    proc_net_isdn = create_proc_entry(dir_in_proc_net, S_IFDIR, proc_net);
  }
  proc_net_isdn_eicon = create_proc_entry(main_proc_dir, S_IFDIR, proc_net_isdn);

  if(proc_net_isdn_eicon) {
    if((proc_didd = create_proc_entry(DRIVERLNAME, S_IFREG | S_IRUGO, proc_net_isdn_eicon))) {
      proc_didd->read_proc = proc_read;
    }
    return(1);
  }
  return(0);
}

static void __exit remove_proc(void)
{
  remove_proc_entry(DRIVERLNAME, proc_net_isdn_eicon);
  remove_proc_entry(main_proc_dir, proc_net_isdn);

  if ((proc_net_isdn) && (!proc_net_isdn->subdir)){
    remove_proc_entry(dir_in_proc_net, proc_net);
  }
}

static void *
didd_callback(void *context, DESCRIPTOR* adapter, int removal)
{
  if (adapter->type == IDI_DADAPTER)
  {
    printk(KERN_ERR "%s: Change in DAdapter ? Oops ?.\n", DRIVERLNAME);
    DBG_ERR(("Notification about IDI_DADAPTER change ! Oops."));
    return(NULL);
  }
  else if (adapter->type == IDI_DIMAINT)
  {
    if (!removal)
      DbgRegister("DIDD", DRIVERRELEASE, DBG_DEFAULT);
  }
  return(NULL);
}

static int __init
connect_didd(void)
{
  int x = 0;
  int dadapter = 0;
  IDI_SYNC_REQ req;
  DESCRIPTOR DIDD_Table[MAX_DESCRIPTORS];

  DIVA_DIDD_Read(DIDD_Table, sizeof(DIDD_Table));

  for (x = 0; x < MAX_DESCRIPTORS; x++)
  {
    if (DIDD_Table[x].type == IDI_DADAPTER)
    {  /* DADAPTER found */
      dadapter = 1;
      memcpy(&_DAdapter, &DIDD_Table[x], sizeof(_DAdapter));
      req.didd_notify.e.Req = 0;
      req.didd_notify.e.Rc = IDI_SYNC_REQ_DIDD_REGISTER_ADAPTER_NOTIFY;
      req.didd_notify.info.callback = didd_callback;
      req.didd_notify.info.context = 0;
      _DAdapter.request((ENTITY *)&req);
      if (req.didd_notify.e.Rc != 0xff)
        return(0);
      notify_handle = req.didd_notify.info.handle;
    }
    else if (DIDD_Table[x].type == IDI_DIMAINT)
    {  /* MAINT found */
      DbgRegister("DIDD", DRIVERRELEASE, DBG_DEFAULT);
    }
  }
  return(dadapter);
}

static void __exit
disconnect_didd(void)
{
  IDI_SYNC_REQ req;

  req.didd_notify.e.Req = 0;
  req.didd_notify.e.Rc = IDI_SYNC_REQ_DIDD_REMOVE_ADAPTER_NOTIFY;
  req.didd_notify.info.handle = notify_handle;
  _DAdapter.request((ENTITY *)&req);
}

static int __init
divadidd_init(void)
{
  char tmprev[32];
  int ret = 0;

  MOD_INC_USE_COUNT;

  printk(KERN_INFO "%s\n", DRIVERNAME);
  printk(KERN_INFO "%s: Rel:%s  Rev:", DRIVERLNAME, DRIVERRELEASE);
  strcpy(tmprev, main_revision);
  printk("%s  Build:%s(%s)\n", getrev(tmprev),
        diva_didd_common_code_build, DIVA_BUILD);

  if (!create_proc()) {
    printk(KERN_ERR "%s: could not create proc entry\n", DRIVERLNAME);
    ret = -EIO;
    goto out;
  }

  diva_didd_load_time_init();

  if(!connect_didd()) {
    printk(KERN_ERR "%s: failed to connect to DIDD.\n", DRIVERLNAME);
    diva_didd_load_time_finit();
    ret = -EIO;
    goto out;
  }

out:
  MOD_DEC_USE_COUNT;
  return (ret);
}

void __exit
divadidd_exit(void)
{
  DbgDeregister();
  disconnect_didd();
  diva_didd_load_time_finit();
  remove_proc();
  printk(KERN_INFO "%s: module unloaded.\n", DRIVERLNAME);
}

module_init(divadidd_init);
module_exit(divadidd_exit);

