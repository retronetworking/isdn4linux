#ifndef _ST5481__H_
#define _ST5481__H_

/* 
   If you have 4 LEDs on your adapter, set this compile flag to 4. 
*/
#ifndef NUMBER_OF_LEDS
#define NUMBER_OF_LEDS 2
#endif

#define ST_VENDOR_ID 0x0483
#define ST5481_PRODUCT_ID 0x4810 /* The Product Id is in the range 0x4810-0x481F */
#define ST5481_PRODUCT_ID_MASK 0xFFF0

/*
  ST5481 endpoints when using alternative setting 3 (2B+D).
  To get the endpoint address, OR with 0x80 for IN endpoints.
*/ 
#define EP_CTRL   0x00U /* Control endpoint */
#define EP_INT    0x01U /* Interrupt endpoint */
#define EP_B1_OUT 0x02U /* B1 channel out */
#define EP_B1_IN  0x03U /* B1 channel in */
#define EP_B2_OUT 0x04U /* B2 channel out */
#define EP_B2_IN  0x05U /* B2 channel in */
#define EP_D_OUT  0x06U /* D channel out */
#define EP_D_IN   0x07U /* D channel in */
  
/* 
   Number of isochronous packets. With 20 packets we get
   50 interrupts/sec for each endpoint.
*/ 
#define NUM_ISO_PACKETS_D    20
#define NUM_ISO_PACKETS_B    20

/*
  Size of each isochronous packet.
*/
#define SIZE_ISO_PACKETS_D   16
#define SIZE_ISO_PACKETS_B   32

/* 
   Registers that are written using vendor specific device request
   on endpoint 0. 
*/
#define LBA			0x02 /* S loopback */
#define SET_DEFAULT		0x06 /* Soft reset */
#define LBB			0x1D /* S maintenance loopback */
#define STT			0x1e /* S force transmission signals */
#define SDA_MIN			0x20 /* SDA-sin minimal value */
#define SDA_MAX			0x21 /* SDA-sin maximal value */
#define SDELAY_VALUE		0x22 /* Delay between Tx and Rx clock */
#define IN_D_COUNTER		0x36 /* D receive channel fifo counter */
#define OUT_D_COUNTER		0x37 /* D transmit channel fifo counter */
#define IN_B1_COUNTER		0x38 /* B1 receive channel fifo counter */
#define OUT_B1_COUNTER		0x39 /* B1 transmit channel fifo counter */
#define IN_B2_COUNTER		0x3a /* B2 receive channel fifo counter */
#define OUT_B2_COUNTER		0x3b /* B2 transmit channel fifo counter */
#define FFCTRL_IN_D		0x3C /* D receive channel fifo threshold low */
#define FFCTRH_IN_D		0x3D /* D receive channel fifo threshold high */
#define FFCTRL_OUT_D		0x3E /* D transmit channel fifo threshold low */
#define FFCTRH_OUT_D		0x3F /* D transmit channel fifo threshold high */
#define FFCTRL_IN_B1		0x40 /* B1 receive channel fifo threshold low */
#define FFCTRH_IN_B1		0x41 /* B1 receive channel fifo threshold high */
#define FFCTRL_OUT_B1		0x42 /* B1 transmit channel fifo threshold low */
#define FFCTRH_OUT_B1		0x43 /* B1 transmit channel fifo threshold high */
#define FFCTRL_IN_B2		0x44 /* B2 receive channel fifo threshold low */
#define FFCTRH_IN_B2		0x45 /* B2 receive channel fifo threshold high */
#define FFCTRL_OUT_B2		0x46 /* B2 transmit channel fifo threshold low */
#define FFCTRH_OUT_B2		0x47 /* B2 transmit channel fifo threshold high */
#define MPMSK			0x4A /* Multi purpose interrupt MASK register */
#define	FFMSK_D			0x4c /* D fifo interrupt MASK register */
#define	FFMSK_B1		0x4e /* B1 fifo interrupt MASK register */
#define	FFMSK_B2		0x50 /* B2 fifo interrupt MASK register */
#define GPIO_DIR		0x52 /* GPIO pins direction registers */
#define GPIO_OUT		0x53 /* GPIO pins output register */
#define GPIO_IN			0x54 /* GPIO pins input register */ 
#define TXCI			0x56 /* CI command to be transmitted */


/*
  Format of the interrupt packet received on endpoint 1:

 +--------+--------+--------+--------+--------+--------+
 !MPINT   !FFINT_D !FFINT_B1!FFINT_B2!CCIST   !GPIO_INT!
 +--------+--------+--------+--------+--------+--------+

*/

/* Offsets in the interrupt packet */
#define MPINT			0
#define FFINT_D			1
#define FFINT_B1		2
#define FFINT_B2		3
#define CCIST			4
#define GPIO_INT		5
#define INT_PKT_SIZE            6

/* MPINT */
#define LSD_INT                 0x80 /* S line activity detected */
#define RXCI_INT		0x40 /* Indicate primitive arrived */
#define	DEN_INT			0x20 /* Signal enabling data out of D Tx fifo */
#define DCOLL_INT		0x10 /* D channel collision */
#define AMIVN_INT		0x04 /* AMI violation number reached 2 */
#define INFOI_INT		0x04 /* INFOi changed */
#define DRXON_INT               0x02 /* Reception channel active */
#define GPCHG_INT               0x01 /* GPIO pin value changed */

/* FFINT_x */
#define IN_OVERRUN		0x80 /* In fifo overrun */
#define OUT_UNDERRUN		0x40 /* Out fifo underrun */
#define IN_UP			0x20 /* In fifo thresholdh up-crossed */
#define IN_DOWN			0x10 /* In fifo thresholdl down-crossed */
#define OUT_UP			0x08 /* Out fifo thresholdh up-crossed */
#define OUT_DOWN		0x04 /* Out fifo thresholdl down-crossed */
#define IN_COUNTER_ZEROED	0x02 /* In down-counter reached 0 */
#define OUT_COUNTER_ZEROED	0x01 /* Out down-counter reached 0 */

#define ANY_REC_INT	(IN_OVERRUN+IN_UP+IN_DOWN+IN_COUNTER_ZEROED)
#define ANY_XMIT_INT	(OUT_UNDERRUN+OUT_UP+OUT_DOWN+OUT_COUNTER_ZEROED)



/* Level 1 indications that are found at offset 4 (CCIST)
   in the interrupt packet */
#define ST5481_IND_DP		 0x0  /* Deactivation Pending */
#define	ST5481_IND_RSY		 0x4  /* ReSYnchronizing */
#define ST5481_IND_AP		 0x8  /* Activation Pending */
#define	ST5481_IND_AI8		 0xC  /* Activation Indication class 8 */
#define	ST5481_IND_AI10		 0xD  /* Activation Indication class 10 */
#define	ST5481_IND_AIL		 0xE  /* Activation Indication Loopback */
#define	ST5481_IND_DI		 0xF  /* Deactivation Indication */

/* Level 1 commands that are sent using the TXCI device request */
#define ST5481_CMD_DR		 0x0 /* Deactivation Request */
#define ST5481_CMD_RES		 0x1 /* state machine RESet */
#define ST5481_CMD_TM1		 0x2 /* Test Mode 1 */
#define ST5481_CMD_TM2		 0x3 /* Test Mode 2 */
#define ST5481_CMD_PUP		 0x7 /* Power UP */
#define ST5481_CMD_AR8		 0x8 /* Activation Request class 1 */
#define ST5481_CMD_AR10		 0x9 /* Activation Request class 2 */
#define ST5481_CMD_ARL		 0xA /* Activation Request Loopback */
#define ST5481_CMD_PDN		 0xF /* Power DoWn */


/* Turn on/off the LEDs using the GPIO device request.
   To use the B LEDs, NUMBER_OF_LEDS must be set to 4 */
#define B1_LED		0x10U
#define B2_LED		0x20U
#define GREEN_LED	0x40U
#define RED_LED	        0x80U


/* D channel out state machine */
enum {
	DOUT_NONE,

	DOUT_STOP,
	DOUT_INIT,
	DOUT_INIT_SHORT_FRAME,
	DOUT_INIT_LONG_FRAME,
	DOUT_SHORT_WAIT_DEN,

	DOUT_WAIT_DEN,
	DOUT_NORMAL,
	DOUT_END_OF_FRAME_BUSY,
	DOUT_END_OF_FRAME_NOT_BUSY,
	DOUT_END_OF_SHORT_FRAME,

        DOUT_WAIT_FOR_NOT_BUSY,
	DOUT_WAIT_FOR_STOP,
	DOUT_WAIT_FOR_RESET,
	DOUT_WAIT_FOR_RESET_IDLE,
	DOUT_IDLE
};

/* D channel out events */
enum {
	DNONE_EVENT,
	DXMIT_INITED,
	DXMIT_STOPPED,
	DEN_EVENT,
	DCOLL_EVENT,
	DUNDERRUN_EVENT,
	DXMIT_NOT_BUSY,
	DXSHORT_EVENT,
	DXRESET_EVENT
}; 

#endif 
