#include "hisax_capi.h"
#include "asn1.h"

int capiEncodeWord(__u8 *p, __u16 i)
{
	*p++ = i;
	*p++ = i >> 8;
	return 2;
}

int capiEncodeDWord(__u8 *p, __u32 i)
{
	*p++ = i;
	*p++ = i >> 8;
	*p++ = i >> 16;
	*p++ = i >> 24;
	return 4;
}

int capiEncodeFacIndCFact(__u8 *dest, __u16 SupplementaryServiceReason, __u32 Handle)
{
	__u8 *p;

	p = &dest[1];
	p += capiEncodeWord(p, SupplementaryServiceReason);
	p += capiEncodeDWord(p, Handle);
	dest[0] = p - &dest[1];
	return p - dest;
}

int capiEncodeFacilityPartyNumber(__u8 *dest, struct PartyNumber *partyNumber)
{
	__u8 *p;

	p = &dest[1];
	switch (partyNumber->type) {
	case 0: // unknown
		*p++ = 0;
		*p++ = 0;
		*p++ = 0;
	        strcpy(p, partyNumber->p.unknown); p += strlen(partyNumber->p.unknown);
		break;
	case 1: // publicPartyNumber
		*p++ = 1;
		*p++ = partyNumber->p.publicPartyNumber.publicTypeOfNumber << 4;
		*p++ = 0;
	        strcpy(p, partyNumber->p.publicPartyNumber.numberDigits);
		p += strlen(partyNumber->p.publicPartyNumber.numberDigits);
		break;
	default: 
		int_error();
	}
	dest[0] = p - &dest[1];
	return p - dest;
}

int capiEncodeFacilityPartyNumber2(__u8 *dest, struct ServedUserNr *servedUserNr)
{
	if (servedUserNr->all) {
		*dest++ = 0; // empty struct;
		return 1;
	}
	return capiEncodeFacilityPartyNumber(dest, &servedUserNr->partyNumber);
}

int capiEncodeFacIndCFNotAct(__u8 *dest, struct ActDivNotification *actNot)
{
	__u8 *p;

	p = &dest[1];
	p += capiEncodeWord(p, actNot->procedure);
	p += capiEncodeWord(p, actNot->basicService);
	p += capiEncodeFacilityPartyNumber2(p, &actNot->servedUserNr);
	p += capiEncodeFacilityPartyNumber(p, &actNot->address.partyNumber);
	*p++ = 0; // sub
	dest[0] = p - &dest[1];
	return p - dest;
}

int capiEncodeFacIndCFNotDeact(__u8 *dest, struct DeactDivNotification *deactNot)
{
	__u8 *p;

	p = &dest[1];
	p += capiEncodeWord(p, deactNot->procedure);
	p += capiEncodeWord(p, deactNot->basicService);
	p += capiEncodeFacilityPartyNumber2(p, &deactNot->servedUserNr);
	dest[0] = p - &dest[1];
	return p - dest;
}

