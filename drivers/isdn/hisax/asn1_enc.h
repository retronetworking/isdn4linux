#include "asn1.h"

int encodeNull(__u8 *dest);
int encodeInt(__u8 *dest, __u32 i);
int encodeEnum(__u8 *dest, __u32 i);
int encodeNumberDigits(__u8 *dest, __u8 *nd, __u8 len);
int encodePublicPartyNumber(__u8 *dest, __u8 *facilityPartyNumber);
int encodePartyNumber(__u8 *dest, __u8 *facilityPartyNumber);
int encodeServedUserNumber(__u8 *dest, __u8 *servedUserNumber);
int encodeAddress(__u8 *dest, __u8 *facilityPartyNumber, __u8 *calledPartySubaddress);
int encodeActivationDiversion(__u8 *dest, __u16 procedure, __u16 basicService, 
			      __u8 *forwardedToNumber, __u8 *forwardedToSubaddress, 
			      __u8 *servedUserNumber);
int encodeDeactivationDiversion(__u8 *dest, __u16 procedure, __u16 basicService, 
				__u8 *servedUserNumber);
