#ifndef __ST5481_HDLC__
#define __ST5481_HDLC__

struct hdlc_vars {
  	int bit_shift; 
	int hdlc_bits1;
	int data_bits;
	int ffbit_shift; // encoding only
	int state;
	int dstpos;

	int data_received:1; // set if transferring data
	int dchannel:1; // set if D channel (send idle instead of flags)
	int do_adapt56:1; // set if 56K adaptation
        int do_closing:1; // set if in closing phase (need to send CRC + flag

	unsigned short crc;

	unsigned char cbin; 
	unsigned char shift_reg;
	unsigned char ffvalue;
	
};


/*
  The return value from hdlc_decode is
  the frame length, 0 if no complete frame was decoded,
  or a negative error number
*/

#define HDLC_FRAMING_ERROR     1
#define HDLC_CRC_ERROR         2
#define HDLC_LENGTH_ERROR      3

void 
hdlc_rcv_init(struct hdlc_vars *hdlc, int do_adapt56);

int
hdlc_decode(struct hdlc_vars *hdlc, const unsigned char *src, int slen,int *count, 
	    unsigned char *dst, int dsize);

void 
hdlc_out_init(struct hdlc_vars *hdlc,int is_d_channel,int do_adapt56);

int 
hdlc_encode(struct hdlc_vars *hdlc,const unsigned char *src,unsigned short slen,int *count,
	    unsigned char *dst,int dsize);

#endif
