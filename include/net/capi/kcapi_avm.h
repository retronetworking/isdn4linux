/*
 * $Id$
 *
 * AVM specific user space interface for loading
 * firmware / configuring the cards
 * 
 */

#define AVM_CARDTYPE_B1		0
#define AVM_CARDTYPE_T1		1
#define AVM_CARDTYPE_M1		2
#define AVM_CARDTYPE_M2		3

typedef struct avmb1_t4file {
        int len;
        unsigned char *data;
} avmb1_t4file;

// KCAPI_CMD_CONTR_AVMB1_LOAD_AND_CONFIG

typedef struct avmb1_loadandconfigdef {
        int contr;
        avmb1_t4file t4file;
        avmb1_t4file t4config; 
} avmb1_loadandconfigdef;
