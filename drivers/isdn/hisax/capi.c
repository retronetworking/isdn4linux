#include "hisax.h"
#include "callc.h"
#include "hisax_capi.h"

const char *capi_revision = "$Revision$";

// ---------------------------------------------------------------------------
// registration to kernelcapi

int hisax_load_firmware(struct capi_ctr *ctrl, capiloaddata *data)
{
	struct Contr *contr = ctrl->driverdata;

	printk(KERN_INFO __FUNCTION__ "\n");
	contrLoadFirmware(contr);
	return 0;
}

void hisax_reset_ctr(struct capi_ctr *ctrl)
{
	struct Contr *contr = ctrl->driverdata;

	printk(KERN_INFO __FUNCTION__ "\n");
	contrReset(contr);
}

void hisax_remove_ctr(struct capi_ctr *ctrl)
{
	printk(KERN_INFO __FUNCTION__ "\n");
	int_error();
}

static char *hisax_procinfo(struct capi_ctr *ctrl)
{
	struct Contr *contr = (ctrl->driverdata);

	printk(KERN_INFO __FUNCTION__ "\n");
	if (!contr)
		return "";
	sprintf(contr->infobuf, "-");
	return contr->infobuf;
}

void hisax_register_appl(struct capi_ctr *ctrl,
			 __u16 ApplId, capi_register_params *rp)
{
	struct Contr *contr = ctrl->driverdata;

	printk(KERN_INFO __FUNCTION__ "\n");
	contrRegisterAppl(contr, ApplId, rp);
}

void hisax_release_appl(struct capi_ctr *ctrl, __u16 ApplId)
{
	struct Contr *contr = ctrl->driverdata;

	printk(KERN_INFO __FUNCTION__ "\n");
	contrReleaseAppl(contr, ApplId);
}

void hisax_send_message(struct capi_ctr *ctrl, struct sk_buff *skb)
{
	struct Contr *contr = ctrl->driverdata;

	contrSendMessage(contr, skb);
}

static int hisax_read_proc(char *page, char **start, off_t off,
		int count, int *eof, struct capi_ctr *ctrl)
{
       int len = 0;

       len += sprintf(page+len, "hisax_read_proc\n");
       if (off+count >= len)
          *eof = 1;
       if (len < off)
           return 0;
       *start = page + off;
       return ((count < len-off) ? count : len-off);
};

struct capi_driver_interface *di;                  

struct capi_driver hisax_driver = {
       "hisax",
       "0.01",
       hisax_load_firmware,
       hisax_reset_ctr,
       hisax_remove_ctr,
       hisax_register_appl,
       hisax_release_appl,
       hisax_send_message,
       hisax_procinfo,
       hisax_read_proc,
       0,
       0,
};

int CapiNew(void)
{
	char tmp[64];

	strcpy(tmp, capi_revision);
	printk(KERN_INFO "HiSax: CAPI Revision %s\n", HiSax_getrev(tmp));

	di = attach_capi_driver(&hisax_driver);
	
	if (!di) {
		printk(KERN_ERR "hisax: failed to attach capi_driver\n");
		return -EIO;
	}

	init_listen();
	init_cplci();
	init_ncci();
	return 0;
}

void CapiFree(void)
{
	detach_capi_driver(&hisax_driver);
}



