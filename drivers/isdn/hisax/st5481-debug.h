#if ST5481_DEBUG

/*
  DEBUG flags. Set compile option  ST5481_DEBUG with the following bits set to trace
  the given subsections:

  0x01: USB
  0x02: D
  0x04: B
  0x08: PH
  0x10: PACKET_DUMP
  0x20: ISO_DUMP
*/

#define DBG(level, format, arg...) \
if (level &  ST5481_DEBUG) \
printk(KERN_DEBUG __FUNCTION__ ": " format "\n" , ## arg) \

static const char *
D_EVENT_string(unsigned int evt)
{ 
	static char s[16];

	switch(evt) {
	case DNONE_EVENT: return "DNONE_EVENT";
	case DXMIT_INITED: return "DXMIT_INITED";
	case DXMIT_STOPPED: return "DXMIT_STOPPED";
	case DEN_EVENT: return "DEN_EVENT";
	case DCOLL_EVENT: return "DCOLL_EVENT";
	case DUNDERRUN_EVENT: return "DUNDERRUN_EVENT";
	case DXMIT_NOT_BUSY: return "DXMIT_NOT_BUSY";
	case DXSHORT_EVENT: return "DXSHORT_EVENT";
	case DXRESET_EVENT: return "DXRESET_EVENT";
	}
	
	sprintf(s,"%d",evt);
	return s;
}

static const char *
D_STATE_string(unsigned int state)
{
	static char s[16];

	switch(state) {
	case DOUT_NONE: return "DOUT_NONE";
	case DOUT_STOP: return "DOUT_STOP";
	case DOUT_INIT: return "DOUT_INIT";
	case DOUT_INIT_SHORT_FRAME: return "DOUT_INIT_SHORT_FRAME";
	case DOUT_INIT_LONG_FRAME: return "DOUT_INIT_LONG_FRAME";
	case DOUT_SHORT_WAIT_DEN: return "DOUT_SHORT_WAIT_DEN";
	case DOUT_WAIT_DEN: return "DOUT_WAIT_DEN";
	case DOUT_NORMAL: return "DOUT_NORMAL";
	case DOUT_END_OF_FRAME_BUSY: return "DOUT_END_OF_FRAME_BUSY";
	case DOUT_END_OF_FRAME_NOT_BUSY: return "DOUT_END_OF_FRAME_NOT_BUSY";
	case DOUT_END_OF_SHORT_FRAME: return "DOUT_END_OF_SHORT_FRAME";
	case DOUT_WAIT_FOR_NOT_BUSY: return "DOUT_WAIT_FOR_NOT_BUSY";
	case DOUT_WAIT_FOR_STOP: return "DOUT_WAIT_FOR_STOP";
	case DOUT_WAIT_FOR_RESET: return "DOUT_WAIT_FOR_RESET";
	case DOUT_WAIT_FOR_RESET_IDLE: return "DOUT_WAIT_FOR_RESET_IDLE";
	case DOUT_IDLE: return "DOUT_IDLE";
	}
		
	sprintf(s,"%d",state);
	return s;
}

static const char *
ST5481_IND_string(int evt)
{
	static char s[16];

	switch(evt) {
	case ST5481_IND_DP: return "DP";
	case ST5481_IND_RSY: return "RSY";
	case ST5481_IND_AP: return "AP";
	case ST5481_IND_AI8: return "AI8";
	case ST5481_IND_AI10: return "AI10";
	case ST5481_IND_AIL: return "AIL";
	case ST5481_IND_DI: return "DI";
	}
	
	sprintf(s,"0x%x",evt);
	return s;
}

static const char *
ST5481_CMD_string(int evt)
{
	static char s[16];

	switch (evt) {
	case ST5481_CMD_DR: return "DR";
	case ST5481_CMD_RES: return "RES";
	case ST5481_CMD_TM1: return "TM1";
	case ST5481_CMD_TM2: return "TM2";
	case ST5481_CMD_PUP: return "PUP";
	case ST5481_CMD_AR8: return "AR8";
	case ST5481_CMD_AR10: return "AR10";
	case ST5481_CMD_ARL: return "ARL";
	case ST5481_CMD_PDN: return "PDN";
	};
	
	sprintf(s,"0x%x",evt);
	return s;
}	

static void 
dump_packet(const char *name,const u_char *data,int pkt_len)
{
#define DUMP_HDR_SIZE 20
#define DUMP_TLR_SIZE 8
	if (pkt_len) {
		int i,len1,len2;

		printk(KERN_DEBUG "%s: length=%d,data=",name,pkt_len);

		if (pkt_len >  DUMP_HDR_SIZE+ DUMP_TLR_SIZE) {
			len1 = DUMP_HDR_SIZE;
			len2 = DUMP_TLR_SIZE;
		} else {
			len1 = pkt_len > DUMP_HDR_SIZE ? DUMP_HDR_SIZE : pkt_len;
			len2 = 0;			
		}
		for (i = 0; i < len1; ++i) {
		 	printk ("%.2x", data[i]);
		}
		if (len2) {
		 	printk ("..");
			for (i = pkt_len-DUMP_TLR_SIZE; i < pkt_len; ++i) {
				printk ("%.2x", data[i]);
			}
		}
		printk ("\n");
	}
#undef DUMP_HDR_SIZE
#undef DUMP_TLR_SIZE
}

static void 
dump_iso_packet(const char *name,urb_t *urb)
{
	int i,j;
	int len,ofs;
	u_char *data;

	printk(KERN_DEBUG "%s: packets=%d,errors=%d,data=\n",
	       name,urb->number_of_packets,urb->error_count);
	for (i = 0; i  < urb->number_of_packets; ++i) {
		if (urb->pipe & USB_DIR_IN) {
			len = urb->iso_frame_desc[i].actual_length;
		} else {
			len = urb->iso_frame_desc[i].length;
		}
		ofs = urb->iso_frame_desc[i].offset;
		printk("len=%.2d,ofs=%.3d ",len,ofs);
		if (len) {
			data = urb->transfer_buffer+ofs;
			for (j=0; j < len; j++) {
				printk ("%.2x", data[j]);
			}
		}
		printk("\n");
	}
}

#define DUMP_PACKET(level,data,count) \
  if (level & ST5481_DEBUG) dump_packet(__FUNCTION__,data,count)
#define DUMP_SKB(level,skb) \
  if ((level & ST5481_DEBUG) && skb) dump_packet(__FUNCTION__,skb->data,skb->len)
#define DUMP_ISO_PACKET(level,urb) \
  if (level & ST5481_DEBUG) dump_iso_packet(__FUNCTION__,urb)

#else

#define DBG(level,format, arg...) do {} while (0)
#define DUMP_PACKET(level,data,count) do {} while (0)
#define DUMP_SKB(level,skb) do {} while (0)
#define DUMP_ISO_PACKET(level,urb) do {} while (0)

#endif



