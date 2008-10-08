/*
 * libpri: An implementation of Primary Rate ISDN
 *
 * Written by Matthew Fredrickson <creslin@digium.com>
 *
 * Copyright (C) 2004-2005, Digium, Inc.
 * All Rights Reserved.
 */

/*
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2 as published by the
 * Free Software Foundation. See the LICENSE file included with
 * this program for more details.
 *
 * In addition, when this program is distributed with Asterisk in
 * any form that would qualify as a 'combined work' or as a
 * 'derivative work' (but not mere aggregation), you can redistribute
 * and/or modify the combination under the terms of the license
 * provided with that copy of Asterisk, instead of the license
 * terms granted here.
 */

#include "compat.h"
#include "libpri.h"
#include "pri_internal.h"
#include "pri_q921.h"
#include "pri_q931.h"
#include "pri_facility.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static char *asn1id2text(int id)
{
	static char data[32];
	static char *strings[] = {
		"none",
		"Boolean",
		"Integer",
		"Bit String",
		"Octet String",
		"NULL",
		"Object Identifier",
		"Object Descriptor",
		"External Reference",
		"Real Number",
		"Enumerated",
		"Embedded PDV",
		"UTF-8 String",
		"Relative Object ID",
		"Reserved (0e)",
		"Reserved (0f)",
		"Sequence",
		"Set",
		"Numeric String",
		"Printable String",
		"Tele-Text String",
		"IA-5 String",
		"UTC Time",
		"Generalized Time",
	};
	if (id > 0 && id <= 0x18) {
		return strings[id];
	} else {
		sprintf(data, "Unknown (%02x)", id);
		return data;
	}
}

static int asn1_dumprecursive(struct pri *pri, void *comp_ptr, int len, int level)
{
	unsigned char *vdata = (unsigned char *)comp_ptr;
	struct rose_component *comp;
	int i = 0;
	int j, k, l;
	int clen = 0;

	while (len > 0) {
		GET_COMPONENT(comp, i, vdata, len);
		pri_message(pri, "%*s%02X %04X", 2 * level, "", comp->type, comp->len);
		if ((comp->type == 0) && (comp->len == 0))
			return clen + 2;
		if ((comp->type & ASN1_PC_MASK) == ASN1_PRIMITIVE) {
			for (j = 0; j < comp->len; ++j)
				pri_message(pri, " %02X", comp->data[j]);
		}
		if ((comp->type & ASN1_CLAN_MASK) == ASN1_UNIVERSAL) {
			switch (comp->type & ASN1_TYPE_MASK) {
			case 0:
				pri_message(pri, " (none)");
				break;
			case ASN1_BOOLEAN:
				pri_message(pri, " (BOOLEAN: %d)", comp->data[0]);
				break;
			case ASN1_INTEGER:
				for (k = l = 0; k < comp->len; ++k)
					l = (l << 8) | comp->data[k];
				pri_message(pri, " (INTEGER: %d)", l);
				break;
			case ASN1_BITSTRING:
				pri_message(pri, " (BITSTRING:");
				for (k = 0; k < comp->len; ++k)
				pri_message(pri, " %02x", comp->data[k]);
				pri_message(pri, ")");
				break;
			case ASN1_OCTETSTRING:
				pri_message(pri, " (OCTETSTRING:");
				for (k = 0; k < comp->len; ++k)
					pri_message(pri, " %02x", comp->data[k]);
				pri_message(pri, ")");
				break;
			case ASN1_NULL:
				pri_message(pri, " (NULL)");
				break;
			case ASN1_OBJECTIDENTIFIER:
				pri_message(pri, " (OBJECTIDENTIFIER:");
				for (k = 0; k < comp->len; ++k)
					pri_message(pri, " %02x", comp->data[k]);
				pri_message(pri, ")");
				break;
			case ASN1_ENUMERATED:
				for (k = l = 0; k < comp->len; ++k)
					l = (l << 8) | comp->data[k];
				pri_message(pri, " (ENUMERATED: %d)", l);
				break;
			case ASN1_SEQUENCE:
				pri_message(pri, " (SEQUENCE)");
				break;
			default:
				pri_message(pri, " (component %02x - %s)", comp->type, asn1id2text(comp->type & ASN1_TYPE_MASK));
				break;
			}
		}
		else if ((comp->type & ASN1_CLAN_MASK) == ASN1_CONTEXT_SPECIFIC) {
			pri_message(pri, " (CONTEXT SPECIFIC [%d])", comp->type & ASN1_TYPE_MASK);
		}
		else {
			pri_message(pri, " (component %02x)", comp->type);
		}
		pri_message(pri, "\n");
		if ((comp->type & ASN1_PC_MASK) == ASN1_CONSTRUCTOR)
			j = asn1_dumprecursive(pri, comp->data, (comp->len ? comp->len : INT_MAX), level+1);
		else
			j = comp->len;
		j += 2;
		len -= j;
		vdata += j;
		clen += j;
	}
	return clen;
}

int asn1_dump(struct pri *pri, void *comp, int len)
{
	return asn1_dumprecursive(pri, comp, len, 0);
}

static unsigned char get_invokeid(struct pri *pri)
{
	return ++pri->last_invoke;
}

struct addressingdataelements_presentednumberunscreened {
	char partyaddress[21];
	char partysubaddress[21];
	int  npi;       /* Numbering Plan Indicator */
	int  ton;       /* Type Of Number */
	int  pres;      /* Presentation */
};

struct addressingdataelements_presentednumberscreened {
	char partyaddress[21];
	char partysubaddress[21];
	int  npi;       /* Numbering Plan Indicator */
	int  ton;       /* Type Of Number */
	int  pres;      /* Presentation */
	int  scrind;    /* Screening Indicator */
};

#define PRI_CHECKOVERFLOW(size) \
		if (msgptr - message + (size) >= sizeof(message)) { \
			*msgptr = '\0'; \
			pri_message(pri, "%s", message); \
			msgptr = message; \
		}

static void dump_apdu(struct pri *pri, unsigned char *c, int len) 
{
	#define MAX_APDU_LENGTH	255
	static char hexs[16] = "0123456789ABCDEF";
	int i;
	char message[(2 + MAX_APDU_LENGTH * 3 + 6 + MAX_APDU_LENGTH + 3)] = "";	/* please adjust here, if you make changes below! */
	char *msgptr;
	
	msgptr = message;
	*msgptr++ = ' ';
	*msgptr++ = '[';
	for (i=0; i<len; i++) {
		PRI_CHECKOVERFLOW(3);
		*msgptr++ = ' ';
		*msgptr++ = hexs[(c[i] >> 4) & 0x0f];
		*msgptr++ = hexs[(c[i]) & 0x0f];
	}
	PRI_CHECKOVERFLOW(6);
	strcpy(msgptr, " ] - [");
	msgptr += strlen(msgptr);
	for (i=0; i<len; i++) {
		PRI_CHECKOVERFLOW(1);
		*msgptr++ = ((c[i] < ' ') || (c[i] > '~')) ? '.' : c[i];
	}
	PRI_CHECKOVERFLOW(2);
	*msgptr++ = ']';
	*msgptr++ = '\n';
	*msgptr = '\0';
	pri_message(pri, "%s", message);
}
#undef PRI_CHECKOVERFLOW

int redirectingreason_from_q931(struct pri *pri, int redirectingreason)
{
	switch(pri->switchtype) {
		case PRI_SWITCH_QSIG:
			switch(redirectingreason) {
				case PRI_REDIR_UNKNOWN:
					return QSIG_DIVERT_REASON_UNKNOWN;
				case PRI_REDIR_FORWARD_ON_BUSY:
					return QSIG_DIVERT_REASON_CFB;
				case PRI_REDIR_FORWARD_ON_NO_REPLY:
					return QSIG_DIVERT_REASON_CFNR;
				case PRI_REDIR_UNCONDITIONAL:
					return QSIG_DIVERT_REASON_CFU;
				case PRI_REDIR_DEFLECTION:
				case PRI_REDIR_DTE_OUT_OF_ORDER:
				case PRI_REDIR_FORWARDED_BY_DTE:
					pri_message(pri, "!! Don't know how to convert Q.931 redirection reason %d to Q.SIG\n", redirectingreason);
					/* Fall through */
				default:
					return QSIG_DIVERT_REASON_UNKNOWN;
			}
		default:
			switch(redirectingreason) {
				case PRI_REDIR_UNKNOWN:
					return Q952_DIVERT_REASON_UNKNOWN;
				case PRI_REDIR_FORWARD_ON_BUSY:
					return Q952_DIVERT_REASON_CFB;
				case PRI_REDIR_FORWARD_ON_NO_REPLY:
					return Q952_DIVERT_REASON_CFNR;
				case PRI_REDIR_DEFLECTION:
					return Q952_DIVERT_REASON_CD;
				case PRI_REDIR_UNCONDITIONAL:
					return Q952_DIVERT_REASON_CFU;
				case PRI_REDIR_DTE_OUT_OF_ORDER:
				case PRI_REDIR_FORWARDED_BY_DTE:
					pri_message(pri, "!! Don't know how to convert Q.931 redirection reason %d to Q.952\n", redirectingreason);
					/* Fall through */
				default:
					return Q952_DIVERT_REASON_UNKNOWN;
			}
	}
}

static int redirectingreason_for_q931(struct pri *pri, int redirectingreason)
{
	switch(pri->switchtype) {
		case PRI_SWITCH_QSIG:
			switch(redirectingreason) {
				case QSIG_DIVERT_REASON_UNKNOWN:
					return PRI_REDIR_UNKNOWN;
				case QSIG_DIVERT_REASON_CFU:
					return PRI_REDIR_UNCONDITIONAL;
				case QSIG_DIVERT_REASON_CFB:
					return PRI_REDIR_FORWARD_ON_BUSY;
				case QSIG_DIVERT_REASON_CFNR:
					return PRI_REDIR_FORWARD_ON_NO_REPLY;
				default:
					pri_message(pri, "!! Unknown Q.SIG diversion reason %d\n", redirectingreason);
					return PRI_REDIR_UNKNOWN;
			}
		default:
			switch(redirectingreason) {
				case Q952_DIVERT_REASON_UNKNOWN:
					return PRI_REDIR_UNKNOWN;
				case Q952_DIVERT_REASON_CFU:
					return PRI_REDIR_UNCONDITIONAL;
				case Q952_DIVERT_REASON_CFB:
					return PRI_REDIR_FORWARD_ON_BUSY;
				case Q952_DIVERT_REASON_CFNR:
					return PRI_REDIR_FORWARD_ON_NO_REPLY;
				case Q952_DIVERT_REASON_CD:
					return PRI_REDIR_DEFLECTION;
				case Q952_DIVERT_REASON_IMMEDIATE:
					pri_message(pri, "!! Dont' know how to convert Q.952 diversion reason IMMEDIATE to PRI analog\n");
					return PRI_REDIR_UNKNOWN;	/* ??? */
				default:
					pri_message(pri, "!! Unknown Q.952 diversion reason %d\n", redirectingreason);
					return PRI_REDIR_UNKNOWN;
			}
	}
}

int typeofnumber_from_q931(struct pri *pri, int ton)
{
	switch(ton) {
		case PRI_TON_INTERNATIONAL:
			return Q932_TON_INTERNATIONAL;
		case PRI_TON_NATIONAL:
			return Q932_TON_NATIONAL;
		case PRI_TON_NET_SPECIFIC:
			return Q932_TON_NET_SPECIFIC;
		case PRI_TON_SUBSCRIBER:
			return Q932_TON_SUBSCRIBER;
		case PRI_TON_ABBREVIATED:
			return Q932_TON_ABBREVIATED;
		case PRI_TON_RESERVED:
		default:
			pri_message(pri, "!! Unsupported Q.931 TypeOfNumber value (%d)\n", ton);
			/* fall through */
		case PRI_TON_UNKNOWN:
			return Q932_TON_UNKNOWN;
	}
}

static int typeofnumber_for_q931(struct pri *pri, int ton)
{
	switch (ton) {
		case Q932_TON_UNKNOWN:
			return PRI_TON_UNKNOWN;
		case Q932_TON_INTERNATIONAL:
			return PRI_TON_INTERNATIONAL;
		case Q932_TON_NATIONAL:
			return PRI_TON_NATIONAL;
		case Q932_TON_NET_SPECIFIC:
			return PRI_TON_NET_SPECIFIC;
		case Q932_TON_SUBSCRIBER:
			return PRI_TON_SUBSCRIBER;
		case Q932_TON_ABBREVIATED:
			return PRI_TON_ABBREVIATED;
		default:
			pri_message(pri, "!! Invalid Q.932 TypeOfNumber %d\n", ton);
			return PRI_TON_UNKNOWN;
	}
}

int asn1_name_decode(void * data, int len, char *namebuf, int buflen)
{
	struct rose_component *comp = (struct rose_component*)data;
	int datalen = 0, res = 0;

	if (comp->len == ASN1_LEN_INDEF) {
		datalen = strlen((char *)comp->data);
		res = datalen + 2;
	} else
		datalen = res = comp->len;

	if (datalen > buflen) {
		/* Truncate */
		datalen = buflen;
	}
	memcpy(namebuf, comp->data, datalen);
	return res + 2;
}

int asn1_string_encode(unsigned char asn1_type, void *data, int len, int max_len, void *src, int src_len)
{
	struct rose_component *comp = NULL;
	
	if (len < 2 + src_len)
		return -1;

	if (max_len && (src_len > max_len))
		src_len = max_len;

	comp = (struct rose_component *)data;
	comp->type = asn1_type;
	comp->len = src_len;
	memcpy(comp->data, src, src_len);
	
	return 2 + src_len;
}

int asn1_copy_string(char * buf, int buflen, struct rose_component *comp)
{
	int res;
	int datalen;

	if ((comp->len > buflen) && (comp->len != ASN1_LEN_INDEF))
		return -1;

	if (comp->len == ASN1_LEN_INDEF) {
		datalen = strlen((char*)comp->data);
		res = datalen + 2;
	} else
		res = datalen = comp->len;

	memcpy(buf, comp->data, datalen);
	buf[datalen] = 0;

	return res;
}

static int rose_number_digits_decode(struct pri *pri, q931_call *call, unsigned char *data, int len, struct addressingdataelements_presentednumberunscreened *value)
{
	int i = 0;
	struct rose_component *comp = NULL;
	unsigned char *vdata = data;
	int datalen = 0;
	int res = 0;

	do {
		GET_COMPONENT(comp, i, vdata, len);
		CHECK_COMPONENT(comp, ASN1_NUMERICSTRING, "Don't know what to do with PublicPartyNumber ROSE component type 0x%x\n");
		if(comp->len > 20 && comp->len != ASN1_LEN_INDEF) {
			pri_message(pri, "!! Oversized NumberDigits component (%d)\n", comp->len);
			return -1;
		}
		if (comp->len == ASN1_LEN_INDEF) {
			datalen = strlen((char *)comp->data);
			res = datalen + 2;
		} else
			res = datalen = comp->len;
			
		memcpy(value->partyaddress, comp->data, datalen);
		value->partyaddress[datalen] = '\0';

		return res + 2;
	}
	while(0);
	
	return -1;
}

static int rose_public_party_number_decode(struct pri *pri, q931_call *call, unsigned char *data, int len, struct addressingdataelements_presentednumberunscreened *value)
{
	int i = 0;
	struct rose_component *comp = NULL;
	unsigned char *vdata = data;
	int ton;
	int res = 0;

	if (len < 2)
		return -1;

	do {
		GET_COMPONENT(comp, i, vdata, len);
		CHECK_COMPONENT(comp, ASN1_ENUMERATED, "Don't know what to do with PublicPartyNumber ROSE component type 0x%x\n");
		ASN1_GET_INTEGER(comp, ton);
		NEXT_COMPONENT(comp, i);
		ton = typeofnumber_for_q931(pri, ton);

		res = rose_number_digits_decode(pri, call, &vdata[i], len-i, value);
		if (res < 0)
			return -1;
		value->ton = ton;

		return res + 3;

	} while(0);
	return -1;
}

static int rose_private_party_number_decode(struct pri *pri, q931_call *call, unsigned char *data, int len, struct addressingdataelements_presentednumberunscreened *value)
{
	int i = 0;
	struct rose_component *comp = NULL;
	unsigned char *vdata = data;
	int ton;
	int res = 0;

	if (len < 2)
	return -1;

	do {
		GET_COMPONENT(comp, i, vdata, len);
		CHECK_COMPONENT(comp, ASN1_ENUMERATED, "Don't know what to do with PrivatePartyNumber ROSE component type 0x%x\n");
		ASN1_GET_INTEGER(comp, ton);
		NEXT_COMPONENT(comp, i);
		ton = typeofnumber_for_q931(pri, ton);

		res = rose_number_digits_decode(pri, call, &vdata[i], len-i, value);
		if (res < 0)
		  return -1;
		value->ton = ton;

		return res + 3;

	} while(0);
	return -1;
}

static int rose_address_decode(struct pri *pri, q931_call *call, unsigned char *data, int len, struct addressingdataelements_presentednumberunscreened *value)
{
	int i = 0;
	struct rose_component *comp = NULL;
	unsigned char *vdata = data;
	int res = 0;

	do {
		GET_COMPONENT(comp, i, vdata, len);

		switch(comp->type) {
		case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_0):	/* [0] unknownPartyNumber */
			res = rose_number_digits_decode(pri, call, comp->data, comp->len, value);
			if (res < 0)
				return -1;
			value->npi = PRI_NPI_UNKNOWN;
			value->ton = PRI_TON_UNKNOWN;
			break;
		case (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_0):	/* [0] unknownPartyNumber */
			res = asn1_copy_string(value->partyaddress, sizeof(value->partyaddress), comp);
			if (res < 0)
				return -1;
			value->npi = PRI_NPI_UNKNOWN;
			value->ton = PRI_TON_UNKNOWN;
			break;
		case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_1):	/* [1] publicPartyNumber */
			res = rose_public_party_number_decode(pri, call, comp->data, comp->len, value);
			if (res < 0)
				return -1;
			value->npi = PRI_NPI_E163_E164;
			break;
		case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_2):	/* [2] nsapEncodedNumber */
			pri_message(pri, "!! NsapEncodedNumber isn't handled\n");
			return -1;
		case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_3):	/* [3] dataPartyNumber */
			if(rose_number_digits_decode(pri, call, comp->data, comp->len, value))
				return -1;
			value->npi = PRI_NPI_X121 /* ??? */;
			value->ton = PRI_TON_UNKNOWN /* ??? */;
			pri_message(pri, "!! dataPartyNumber isn't handled\n");
			return -1;
		case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_4):	/* [4] telexPartyNumber */
			res = rose_number_digits_decode(pri, call, comp->data, comp->len, value);
			if (res < 0)
				return -1;
			value->npi = PRI_NPI_F69 /* ??? */;
			value->ton = PRI_TON_UNKNOWN /* ??? */;
			pri_message(pri, "!! telexPartyNumber isn't handled\n");
			return -1;
		case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_5):	/* [5] priavePartyNumber */
			res = rose_private_party_number_decode(pri, call, comp->data, comp->len, value);
			if (res < 0)
			return -1;
			value->npi = PRI_NPI_PRIVATE;
			break;
		case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_8):	/* [8] nationalStandardPartyNumber */
			res = rose_number_digits_decode(pri, call, comp->data, comp->len, value);
			if (res < 0)
				return -1;
			value->npi = PRI_NPI_NATIONAL;
			value->ton = PRI_TON_NATIONAL;
			break;
		default:
			pri_message(pri, "!! Unknown Party number component received 0x%X\n", comp->type);
			return -1;
		}
		ASN1_FIXUP_LEN(comp, res);
		NEXT_COMPONENT(comp, i);
		if(i < len)
			pri_message(pri, "!! not all information is handled from Address component\n");
		return res + 2;
	}
	while (0);

	return -1;
}

static int rose_presented_number_unscreened_decode(struct pri *pri, q931_call *call, unsigned char *data, int len, struct addressingdataelements_presentednumberunscreened *value)
{
	int i = 0;
	int size = 0;
	struct rose_component *comp = NULL;
	unsigned char *vdata = data;

	/* Fill in default values */
	value->ton = PRI_TON_UNKNOWN;
	value->npi = PRI_NPI_E163_E164;
	value->pres = -1;	/* Data is not available */

	do {
		GET_COMPONENT(comp, i, vdata, len);

		switch(comp->type) {
		case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_0):		/* [0] presentationAllowedNumber */
			value->pres = PRES_ALLOWED_USER_NUMBER_NOT_SCREENED;
			size = rose_address_decode(pri, call, comp->data, comp->len, value);
			ASN1_FIXUP_LEN(comp, size);
			return size + 2;
		case (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_1):		/* [1] IMPLICIT presentationRestricted */
			if (comp->len != 0) { /* must be NULL */
				pri_error(pri, "!! Invalid PresentationRestricted component received (len != 0)\n");
				return -1;
			}
			value->pres = PRES_PROHIB_USER_NUMBER_NOT_SCREENED;
			return 2;
		case (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_2):		/* [2] IMPLICIT numberNotAvailableDueToInterworking */
			if (comp->len != 0) { /* must be NULL */
				pri_error(pri, "!! Invalid NumberNotAvailableDueToInterworking component received (len != 0)\n");
				return -1;
			}
			value->pres = PRES_NUMBER_NOT_AVAILABLE;
			return 2;
		case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_3):		/* [3] presentationRestrictedNumber */
			value->pres = PRES_PROHIB_USER_NUMBER_NOT_SCREENED;
			size = rose_address_decode(pri, call, comp->data, comp->len, value) + 2;
			ASN1_FIXUP_LEN(comp, size);
			return size + 2;
		default:
			pri_message(pri, "Invalid PresentedNumberUnscreened component 0x%X\n", comp->type);
		}
		return -1;
	}
	while (0);

	return -1;
}

static int rose_diverting_leg_information2_decode(struct pri *pri, q931_call *call, struct rose_component *sequence, int len)
{
	int i = 0;
	int diversion_counter;
	int diversion_reason;
	char origcalledname[50] = "", redirectingname[50] = "";
	struct addressingdataelements_presentednumberunscreened divertingnr;
 	struct addressingdataelements_presentednumberunscreened originalcallednr;
	struct rose_component *comp = NULL;
	unsigned char *vdata = sequence->data;
	int res = 0;
	memset(&divertingnr, 0, sizeof(divertingnr));
	memset(&originalcallednr, 0, sizeof(originalcallednr));

	/* Data checks */
	if (sequence->type != (ASN1_CONSTRUCTOR | ASN1_SEQUENCE)) { /* Constructed Sequence */
		pri_message(pri, "Invalid DivertingLegInformation2Type argument\n");
		return -1;
	}

	if (sequence->len == ASN1_LEN_INDEF) {
		len -= 4; /* For the 2 extra characters at the end
                           * and two characters of header */
	} else
		len -= 2;

	do {
		/* diversionCounter stuff */
		GET_COMPONENT(comp, i, vdata, len);
		CHECK_COMPONENT(comp, ASN1_INTEGER, "Don't know what to do it diversionCounter is of type 0x%x\n");
		ASN1_GET_INTEGER(comp, diversion_counter);
		NEXT_COMPONENT(comp, i);

		/* diversionReason stuff */
		GET_COMPONENT(comp, i, vdata, len);
		CHECK_COMPONENT(comp, ASN1_ENUMERATED, "Invalid diversionReason type 0x%X of ROSE divertingLegInformation2 component received\n");
		ASN1_GET_INTEGER(comp, diversion_reason);
		NEXT_COMPONENT(comp, i);

		diversion_reason = redirectingreason_for_q931(pri, diversion_reason);
	
		if(pri->debug & PRI_DEBUG_APDU)
			pri_message(pri, "    Redirection reason: %d, total diversions: %d\n", diversion_reason, diversion_counter);
		pri_message(NULL, "Length of message is %d\n", len);

		for(; i < len; NEXT_COMPONENT(comp, i)) {
			GET_COMPONENT(comp, i, vdata, len);
			switch(comp->type) {
			case (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_0):
				call->origredirectingreason = redirectingreason_for_q931(pri, comp->data[0]);
				if (pri->debug & PRI_DEBUG_APDU)
					pri_message(pri, "    Received reason for original redirection %d\n", call->origredirectingreason);
				break;
			case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_1):
				res = rose_presented_number_unscreened_decode(pri, call, comp->data, comp->len, &divertingnr);
				/* TODO: Fix indefinite length form hacks */
				ASN1_FIXUP_LEN(comp, res);
				comp->len = res;
				if (res < 0)
					return -1;
				if (pri->debug & PRI_DEBUG_APDU) {
					pri_message(pri, "    Received divertingNr '%s'\n", divertingnr.partyaddress);
					pri_message(pri, "      ton = %d, pres = %d, npi = %d\n", divertingnr.ton, divertingnr.pres, divertingnr.npi);
				}
				break;
			case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_2):
				res = rose_presented_number_unscreened_decode(pri, call, comp->data, comp->len, &originalcallednr);
				if (res < 0)
					return -1;
				ASN1_FIXUP_LEN(comp, res);
				comp->len = res;
				if (pri->debug & PRI_DEBUG_APDU) {
					pri_message(pri, "    Received originalcallednr '%s'\n", originalcallednr.partyaddress);
					pri_message(pri, "      ton = %d, pres = %d, npi = %d\n", originalcallednr.ton, originalcallednr.pres, originalcallednr.npi);
				}
				break;
			case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_3):
				res = asn1_name_decode(comp->data, comp->len, redirectingname, sizeof(redirectingname));
				if (res < 0)
					return -1;
				ASN1_FIXUP_LEN(comp, res);
				comp->len = res;
				if (pri->debug & PRI_DEBUG_APDU)
					pri_message(pri, "    Received RedirectingName '%s'\n", redirectingname);
				break;
			case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_4):
				res = asn1_name_decode(comp->data, comp->len, origcalledname, sizeof(origcalledname));
				if (res < 0)
					return -1;
				ASN1_FIXUP_LEN(comp, res);
				comp->len = res;
				if (pri->debug & PRI_DEBUG_APDU)
					pri_message(pri, "    Received Originally Called Name '%s'\n", origcalledname);
				break;
			case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_5):
				pri_message(pri, "!! Ignoring DivertingLegInformation2 component 0x%X\n", comp->type);
				break;
			default:
				if (comp->type == 0 && comp->len == 0) {
					break; /* Found termination characters */
				}
				pri_message(pri, "!! Invalid DivertingLegInformation2 component received 0x%X\n", comp->type);
				return -1;
			}
		}

		if (divertingnr.pres >= 0) {
			call->redirectingplan = divertingnr.npi;
			call->redirectingpres = divertingnr.pres;
			call->redirectingreason = diversion_reason;
			libpri_copy_string(call->redirectingnum, divertingnr.partyaddress, sizeof(call->redirectingnum));
			pri_message(pri, "    Received redirectingnum '%s' (%d)\n", call->redirectingnum, (int)call->redirectingnum[0]);
		}
		if (originalcallednr.pres >= 0) {
			call->origcalledplan = originalcallednr.npi;
			call->origcalledpres = originalcallednr.pres;
			libpri_copy_string(call->origcallednum, originalcallednr.partyaddress, sizeof(call->origcallednum));
			pri_message(pri, "    Received origcallednum '%s' (%d)\n", call->origcallednum, (int)call->origcallednum[0]);
		}
		libpri_copy_string(call->redirectingname, redirectingname, sizeof(call->redirectingname));
		libpri_copy_string(call->origcalledname, origcalledname, sizeof(call->origcalledname));
		return 0;
	}
	while (0);

	return -1;
}
				
static int rose_diverting_leg_information2_encode(struct pri *pri, q931_call *call)
{
	int i = 0, j, compsp = 0;
	struct rose_component *comp, *compstk[10];
	unsigned char buffer[256];
	int len = 253;
	
#if 0	/* This is not required by specifications */
	if (!strlen(call->callername)) {
		return -1;
	}
#endif

	buffer[i] = (ASN1_CONTEXT_SPECIFIC | Q932_PROTOCOL_EXTENSIONS);
	i++;
	/* Interpretation component */
	ASN1_ADD_BYTECOMP(comp, COMP_TYPE_INTERPRETATION, buffer, i, 0x00 /* Discard unrecognized invokes */);
	
	ASN1_ADD_SIMPLE(comp, COMP_TYPE_INVOKE, buffer, i);
	
	ASN1_PUSH(compstk, compsp, comp);
	/* Invoke component contents */
	/*	Invoke ID */
	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, get_invokeid(pri));
	/*	Operation Tag */
	
	/* ROSE operationId component */
	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, ROSE_DIVERTING_LEG_INFORMATION2);

	/* ROSE ARGUMENT component */
	ASN1_ADD_SIMPLE(comp, (ASN1_CONSTRUCTOR | ASN1_SEQUENCE), buffer, i);
	ASN1_PUSH(compstk, compsp, comp);
	/* ROSE DivertingLegInformation2.diversionCounter component */
	/* Always is 1 because other isn't available in the current design */
	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, 1);
	
	/* ROSE DivertingLegInformation2.diversionReason component */
	ASN1_ADD_BYTECOMP(comp, ASN1_ENUMERATED, buffer, i, redirectingreason_from_q931(pri, call->redirectingreason));
		
	/* ROSE DivertingLegInformation2.divertingNr component */
	ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_1), buffer, i);
	
	ASN1_PUSH(compstk, compsp, comp);
		/* Redirecting information always not screened */
	
	switch(call->redirectingpres) {
		case PRES_ALLOWED_USER_NUMBER_NOT_SCREENED:
		case PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN:
			if (call->redirectingnum && strlen(call->redirectingnum)) {
				ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_0), buffer, i);
				ASN1_PUSH(compstk, compsp, comp);
					/* NPI of redirected number is not supported in the current design */
				ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_1), buffer, i);
				ASN1_PUSH(compstk, compsp, comp);
					ASN1_ADD_BYTECOMP(comp, ASN1_ENUMERATED, buffer, i, typeofnumber_from_q931(pri, call->redirectingplan >> 4));
					j = asn1_string_encode(ASN1_NUMERICSTRING, &buffer[i], len - i, 20, call->redirectingnum, strlen(call->redirectingnum));
				if (j < 0)
					return -1;
					
				i += j;
				ASN1_FIXUP(compstk, compsp, buffer, i);
				ASN1_FIXUP(compstk, compsp, buffer, i);
				break;
			}
			/* fall through */
		case PRES_PROHIB_USER_NUMBER_PASSED_SCREEN:
		case PRES_PROHIB_USER_NUMBER_NOT_SCREENED:
			ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_1), buffer, i);
			break;
		/* Don't know how to handle this */
		case PRES_ALLOWED_NETWORK_NUMBER:
		case PRES_PROHIB_NETWORK_NUMBER:
		case PRES_ALLOWED_USER_NUMBER_FAILED_SCREEN:
		case PRES_PROHIB_USER_NUMBER_FAILED_SCREEN:
			ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_1), buffer, i);
			break;
		default:
			pri_message(pri, "!! Undefined presentation value for redirecting number: %d\n", call->redirectingpres);
		case PRES_NUMBER_NOT_AVAILABLE:
			ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_2), buffer, i);
			break;
	}
	ASN1_FIXUP(compstk, compsp, buffer, i);

	/* ROSE DivertingLegInformation2.originalCalledNr component */
	/* This information isn't supported by current design - duplicate divertingNr */
	ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_2), buffer, i);
	ASN1_PUSH(compstk, compsp, comp);
		/* Redirecting information always not screened */
	switch(call->redirectingpres) {
		case PRES_ALLOWED_USER_NUMBER_NOT_SCREENED:
		case PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN:
			if (call->redirectingnum && strlen(call->redirectingnum)) {
				ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_0), buffer, i);
				ASN1_PUSH(compstk, compsp, comp);
				ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_1), buffer, i);
				ASN1_PUSH(compstk, compsp, comp);
				ASN1_ADD_BYTECOMP(comp, ASN1_ENUMERATED, buffer, i, typeofnumber_from_q931(pri, call->redirectingplan >> 4));
	
				j = asn1_string_encode(ASN1_NUMERICSTRING, &buffer[i], len - i, 20, call->redirectingnum, strlen(call->redirectingnum));
				if (j < 0)
					return -1;
				
				i += j;
				ASN1_FIXUP(compstk, compsp, buffer, i);
				ASN1_FIXUP(compstk, compsp, buffer, i);
				break;
			}
				/* fall through */
		case PRES_PROHIB_USER_NUMBER_PASSED_SCREEN:
		case PRES_PROHIB_USER_NUMBER_NOT_SCREENED:
			ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_1), buffer, i);
			break;
		/* Don't know how to handle this */
		case PRES_ALLOWED_NETWORK_NUMBER:
		case PRES_PROHIB_NETWORK_NUMBER:
		case PRES_ALLOWED_USER_NUMBER_FAILED_SCREEN:
		case PRES_PROHIB_USER_NUMBER_FAILED_SCREEN:
			ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_1), buffer, i);
			break;
		default:
			pri_message(pri, "!! Undefined presentation value for redirecting number: %d\n", call->redirectingpres);
		case PRES_NUMBER_NOT_AVAILABLE:
			ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_2), buffer, i);
			break;
	}
	ASN1_FIXUP(compstk, compsp, buffer, i);
		
	/* Fix length of stacked components */
	while(compsp > 0) {
		ASN1_FIXUP(compstk, compsp, buffer, i);
	}
	
	if (pri_call_apdu_queue(call, Q931_SETUP, buffer, i, NULL, NULL))
		return -1;
		
	return 0;
}

/* Send the rltThirdParty: Invoke */
int rlt_initiate_transfer(struct pri *pri, q931_call *c1, q931_call *c2)
{
	int i = 0;
	unsigned char buffer[256];
	struct rose_component *comp = NULL, *compstk[10];
	const unsigned char rlt_3rd_pty = RLT_THIRD_PARTY;
	q931_call *callwithid = NULL, *apdubearer = NULL;
	int compsp = 0;

	if (c2->transferable) {
		apdubearer = c1;
		callwithid = c2;
	} else if (c1->transferable) {
		apdubearer = c2;
		callwithid = c1;
	} else
		return -1;

	buffer[i++] = (Q932_PROTOCOL_ROSE);
	buffer[i++] = (0x80 | RLT_SERVICE_ID); /* Service Identifier octet */

	ASN1_ADD_SIMPLE(comp, COMP_TYPE_INVOKE, buffer, i);
	ASN1_PUSH(compstk, compsp, comp);

	/* Invoke ID is set to the operation ID */
	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, rlt_3rd_pty);

	/* Operation Tag */
	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, rlt_3rd_pty);

	/* Additional RLT invoke info - Octet 12 */
	ASN1_ADD_SIMPLE(comp, (ASN1_CONSTRUCTOR | ASN1_SEQUENCE), buffer, i);
	ASN1_PUSH(compstk, compsp, comp);

	ASN1_ADD_WORDCOMP(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_0), buffer, i, callwithid->rlt_call_id & 0xFFFFFF); /* Length is 3 octets */
	/* Reason for redirect - unused, set to 129 */
	ASN1_ADD_BYTECOMP(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_1), buffer, i, 0);
	ASN1_FIXUP(compstk, compsp, buffer, i);
	ASN1_FIXUP(compstk, compsp, buffer, i);

	if (pri_call_apdu_queue(apdubearer, Q931_FACILITY, buffer, i, NULL, NULL))
		return -1;

	if (q931_facility(apdubearer->pri, apdubearer)) {
		pri_message(pri, "Could not schedule facility message for call %d\n", apdubearer->cr);
		return -1;
	}
	return 0;
}

static int add_dms100_transfer_ability_apdu(struct pri *pri, q931_call *c)
{
	int i = 0;
	unsigned char buffer[256];
	struct rose_component *comp = NULL, *compstk[10];
	const unsigned char rlt_op_ind = RLT_OPERATION_IND;
	int compsp = 0;

	buffer[i++] = (Q932_PROTOCOL_ROSE);  /* Note to self: DON'T set the EXT bit */
	buffer[i++] = (0x80 | RLT_SERVICE_ID); /* Service Identifier octet */

	ASN1_ADD_SIMPLE(comp, COMP_TYPE_INVOKE, buffer, i);
	ASN1_PUSH(compstk, compsp, comp);

	/* Invoke ID is set to the operation ID */
	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, rlt_op_ind);
	
	/* Operation Tag - basically the same as the invoke ID tag */
	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, rlt_op_ind);
	ASN1_FIXUP(compstk, compsp, buffer, i);

	if (pri_call_apdu_queue(c, Q931_SETUP, buffer, i, NULL, NULL))
		return -1;
	else
		return 0;
}

/* Sending callername information functions */
static int add_callername_facility_ies(struct pri *pri, q931_call *c, int cpe)
{
	int res = 0;
	int i = 0;
	unsigned char buffer[256];
	unsigned char namelen = 0;
	struct rose_component *comp = NULL, *compstk[10];
	int compsp = 0;
	int mymessage = 0;
	static unsigned char op_tag[] = { 
		0x2a, /* informationFollowing 42 */
		0x86,
		0x48,
		0xce,
		0x15,
		0x00,
		0x04
	};
		
	if (!strlen(c->callername)) {
		return -1;
	}

	buffer[i++] = (ASN1_CONTEXT_SPECIFIC | Q932_PROTOCOL_EXTENSIONS);
	/* Interpretation component */

	if (pri->switchtype == PRI_SWITCH_QSIG) {
		ASN1_ADD_SIMPLE(comp, COMP_TYPE_NFE, buffer, i);
		ASN1_PUSH(compstk, compsp, comp);
		ASN1_ADD_BYTECOMP(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_0), buffer, i, 0);
		ASN1_ADD_BYTECOMP(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_2), buffer, i, 0);
		ASN1_FIXUP(compstk, compsp, buffer, i);
	}

	ASN1_ADD_BYTECOMP(comp, COMP_TYPE_INTERPRETATION, buffer, i, 0);

	ASN1_ADD_SIMPLE(comp, COMP_TYPE_INVOKE, buffer, i);
	ASN1_PUSH(compstk, compsp, comp);
	/* Invoke ID */
	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, get_invokeid(pri));

	/* Operation Tag */
	res = asn1_string_encode(ASN1_OBJECTIDENTIFIER, &buffer[i], sizeof(buffer)-i, sizeof(op_tag), op_tag, sizeof(op_tag));
	if (res < 0)
		return -1;
	i += res;

	ASN1_ADD_BYTECOMP(comp, ASN1_ENUMERATED, buffer, i, 0);
	ASN1_FIXUP(compstk, compsp, buffer, i);

	if (!cpe) {
		if (pri_call_apdu_queue(c, Q931_SETUP, buffer, i, NULL, NULL))
			return -1;
	}


	/* Now the APDU that contains the information that needs sent.
	 * We can reuse the buffer since the queue function doesn't
	 * need it. */

	i = 0;
	namelen = strlen(c->callername);
	if (namelen > 50) {
		namelen = 50; /* truncate the name */
	}

	buffer[i++] = (ASN1_CONTEXT_SPECIFIC | Q932_PROTOCOL_EXTENSIONS);
	/* Interpretation component */

	if (pri->switchtype == PRI_SWITCH_QSIG) {
		ASN1_ADD_SIMPLE(comp, COMP_TYPE_NFE, buffer, i);
		ASN1_PUSH(compstk, compsp, comp);
		ASN1_ADD_BYTECOMP(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_0), buffer, i, 0);
		ASN1_ADD_BYTECOMP(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_2), buffer, i, 0);
		ASN1_FIXUP(compstk, compsp, buffer, i);
	}

	ASN1_ADD_BYTECOMP(comp, COMP_TYPE_INTERPRETATION, buffer, i, 0);

	ASN1_ADD_SIMPLE(comp, COMP_TYPE_INVOKE, buffer, i);
	ASN1_PUSH(compstk, compsp, comp);

	/* Invoke ID */
	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, get_invokeid(pri));

	/* Operation ID: Calling name */
	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, SS_CNID_CALLINGNAME);

	res = asn1_string_encode((ASN1_CONTEXT_SPECIFIC | ASN1_TAG_0), &buffer[i], sizeof(buffer)-i,  50, c->callername, namelen);
	if (res < 0)
		return -1;
	i += res;
	ASN1_FIXUP(compstk, compsp, buffer, i);

	if (cpe) 
		mymessage = Q931_SETUP;
	else
		mymessage = Q931_FACILITY;

	if (pri_call_apdu_queue(c, mymessage, buffer, i, NULL, NULL))
		return -1;
	
	return 0;
}
/* End Callername */

/* MWI related encode and decode functions */
static void mwi_activate_encode_cb(void *data)
{
	return;
}

int mwi_message_send(struct pri* pri, q931_call *call, struct pri_sr *req, int activate)
{
	int i = 0;
	unsigned char buffer[255] = "";
	int destlen = strlen(req->called);
	struct rose_component *comp = NULL, *compstk[10];
	int compsp = 0;
	int res;

	if (destlen <= 0) {
		return -1;
	} else if (destlen > 20)
		destlen = 20;  /* Destination number cannot be greater then 20 digits */

	buffer[i++] = (ASN1_CONTEXT_SPECIFIC | Q932_PROTOCOL_EXTENSIONS);
	/* Interpretation component */

	ASN1_ADD_SIMPLE(comp, COMP_TYPE_NFE, buffer, i);
	ASN1_PUSH(compstk, compsp, comp);
	ASN1_ADD_BYTECOMP(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_0), buffer, i, 0);
	ASN1_ADD_BYTECOMP(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_2), buffer, i, 0);
	ASN1_FIXUP(compstk, compsp, buffer, i);

	ASN1_ADD_BYTECOMP(comp, COMP_TYPE_INTERPRETATION, buffer, i, 0);

	ASN1_ADD_SIMPLE(comp, COMP_TYPE_INVOKE, buffer, i);
	ASN1_PUSH(compstk, compsp, comp);

	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, get_invokeid(pri));

	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, (activate) ? SS_MWI_ACTIVATE : SS_MWI_DEACTIVATE);
	ASN1_ADD_SIMPLE(comp, (ASN1_CONSTRUCTOR | ASN1_SEQUENCE), buffer, i);
	ASN1_PUSH(compstk, compsp, comp);
	/* PartyNumber */
	res = asn1_string_encode((ASN1_CONTEXT_SPECIFIC | ASN1_TAG_0), &buffer[i], sizeof(buffer)-i, destlen, req->called, destlen);
	
	if (res < 0)
		return -1;
	i += res;

	/* Enumeration: basicService */
	ASN1_ADD_BYTECOMP(comp, ASN1_ENUMERATED, buffer, i, 1 /* contents: Voice */);
	ASN1_FIXUP(compstk, compsp, buffer, i);
	ASN1_FIXUP(compstk, compsp, buffer, i);

	return pri_call_apdu_queue(call, Q931_SETUP, buffer, i, mwi_activate_encode_cb, NULL);
}
/* End MWI */

/* EECT functions */
int eect_initiate_transfer(struct pri *pri, q931_call *c1, q931_call *c2)
{
	int i = 0;
	int res = 0;
	unsigned char buffer[255] = "";
	short call_reference = c2->cr ^ 0x8000;  /* Let's do the trickery to make sure the flag is correct */
	struct rose_component *comp = NULL, *compstk[10];
	int compsp = 0;
	static unsigned char op_tag[] = {
		0x2A,
		0x86,
		0x48,
		0xCE,
		0x15,
		0x00,
		0x08,
	};

	buffer[i++] = (ASN1_CONTEXT_SPECIFIC | Q932_PROTOCOL_ROSE);

	ASN1_ADD_SIMPLE(comp, COMP_TYPE_INVOKE, buffer, i);
	ASN1_PUSH(compstk, compsp, comp);

	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, get_invokeid(pri));

	res = asn1_string_encode(ASN1_OBJECTIDENTIFIER, &buffer[i], sizeof(buffer)-i, sizeof(op_tag), op_tag, sizeof(op_tag));
	if (res < 0)
		return -1;
	i += res;

	ASN1_ADD_SIMPLE(comp, (ASN1_SEQUENCE | ASN1_CONSTRUCTOR), buffer, i);
	ASN1_PUSH(compstk, compsp, comp);
	ASN1_ADD_WORDCOMP(comp, ASN1_INTEGER, buffer, i, call_reference);
	ASN1_FIXUP(compstk, compsp, buffer, i);
	ASN1_FIXUP(compstk, compsp, buffer, i);

	res = pri_call_apdu_queue(c1, Q931_FACILITY, buffer, i, NULL, NULL);
	if (res) {
		pri_message(pri, "Could not queue APDU in facility message\n");
		return -1;
	}

	/* Remember that if we queue a facility IE for a facility message we
	 * have to explicitly send the facility message ourselves */

	res = q931_facility(c1->pri, c1);
	if (res) {
		pri_message(pri, "Could not schedule facility message for call %d\n", c1->cr);
		return -1;
	}

	return 0;
}
/* End EECT */

static int anfpr_pathreplacement_respond(struct pri *pri, q931_call *call, q931_ie *ie)
{
	int res;
	
	res = pri_call_apdu_queue_cleanup(call->bridged_call);
	if (res) {
	        pri_message(pri, "Could not Clear queue ADPU\n");
	        return -1;
	}
	
	/* Send message */
	res = pri_call_apdu_queue(call->bridged_call, Q931_FACILITY, ie->data, ie->len, NULL, NULL);
	if (res) {
	        pri_message(pri, "Could not queue ADPU in facility message\n");
	        return -1;
	}
	
	/* Remember that if we queue a facility IE for a facility message we
	 * have to explicitly send the facility message ourselves */
	
	res = q931_facility(call->bridged_call->pri, call->bridged_call);
	if (res) {
		pri_message(pri, "Could not schedule facility message for call %d\n", call->bridged_call->cr);
		return -1;
	}

	return 0;
}
/* AFN-PR */
extern int anfpr_initiate_transfer(struct pri *pri, q931_call *c1, q931_call *c2)
{
	/* Did all the tests to see if we're on the same PRI and
	 * are on a compatible switchtype */
	/* TODO */
	int i = 0;
	int res = 0;
	unsigned char buffer[255] = "";
	unsigned short call_reference = c2->cr;
	struct rose_component *comp = NULL, *compstk[10];
	unsigned char buffer2[255] = "";
	int compsp = 0;
	static unsigned char op_tag[] = {
		0x0C,
	};
	
	/* Channel 1 */
	buffer[i++] = (ASN1_CONTEXT_SPECIFIC | Q932_PROTOCOL_EXTENSIONS);
	/* Interpretation component */
	
	ASN1_ADD_SIMPLE(comp, COMP_TYPE_NFE, buffer, i);
	ASN1_PUSH(compstk, compsp, comp);
	ASN1_ADD_BYTECOMP(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_0), buffer, i, 0);
	ASN1_ADD_BYTECOMP(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_2), buffer, i, 0);
	ASN1_FIXUP(compstk, compsp, buffer, i);
	
	ASN1_ADD_BYTECOMP(comp, COMP_TYPE_INTERPRETATION, buffer, i, 2);    /* reject - to get feedback from QSIG switch */
	
	ASN1_ADD_SIMPLE(comp, COMP_TYPE_INVOKE, buffer, i);
	ASN1_PUSH(compstk, compsp, comp);
	
	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, get_invokeid(pri));
	
	res = asn1_string_encode(ASN1_INTEGER, &buffer[i], sizeof(buffer)-i, sizeof(op_tag), op_tag, sizeof(op_tag));
	if (res < 0)
		return -1;
	i += res;
	
	ASN1_ADD_SIMPLE(comp, (ASN1_SEQUENCE | ASN1_CONSTRUCTOR), buffer, i);
	ASN1_PUSH(compstk, compsp, comp);
	buffer[i++] = (0x0a);
	buffer[i++] = (0x01);
	buffer[i++] = (0x00);
	buffer[i++] = (0x81);
	buffer[i++] = (0x00);
	buffer[i++] = (0x0a);
	buffer[i++] = (0x01);
	buffer[i++] = (0x01);
	ASN1_ADD_WORDCOMP(comp, ASN1_INTEGER, buffer, i, call_reference);
	ASN1_FIXUP(compstk, compsp, buffer, i);
	ASN1_FIXUP(compstk, compsp, buffer, i);
	
	res = pri_call_apdu_queue(c1, Q931_FACILITY, buffer, i, NULL, NULL);
	if (res) {
		pri_message(pri, "Could not queue ADPU in facility message\n");
		return -1;
	}
	
	/* Remember that if we queue a facility IE for a facility message we
	 * have to explicitly send the facility message ourselves */
	
	res = q931_facility(c1->pri, c1);
	if (res) {
		pri_message(pri, "Could not schedule facility message for call %d\n", c1->cr);
		return -1;
	}
	
	/* Channel 2 */
	i = 0;
	res = 0;
	compsp = 0;
	
	buffer2[i++] = (ASN1_CONTEXT_SPECIFIC | Q932_PROTOCOL_EXTENSIONS);
	/* Interpretation component */
	
	ASN1_ADD_SIMPLE(comp, COMP_TYPE_NFE, buffer2, i);
	ASN1_PUSH(compstk, compsp, comp);
	ASN1_ADD_BYTECOMP(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_0), buffer2, i, 0);
	ASN1_ADD_BYTECOMP(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_2), buffer2, i, 0);
	ASN1_FIXUP(compstk, compsp, buffer2, i);
	
	ASN1_ADD_BYTECOMP(comp, COMP_TYPE_INTERPRETATION, buffer2, i, 2);  /* reject */
	
	ASN1_ADD_SIMPLE(comp, COMP_TYPE_INVOKE, buffer2, i);
	ASN1_PUSH(compstk, compsp, comp);
	
	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer2, i, get_invokeid(pri));
	
	res = asn1_string_encode(ASN1_INTEGER, &buffer2[i], sizeof(buffer2)-i, sizeof(op_tag), op_tag, sizeof(op_tag));
	if (res < 0)
		return -1;
	i += res;
	
	ASN1_ADD_SIMPLE(comp, (ASN1_SEQUENCE | ASN1_CONSTRUCTOR), buffer2, i);
	ASN1_PUSH(compstk, compsp, comp);
	buffer2[i++] = (0x0a);
	buffer2[i++] = (0x01);
	buffer2[i++] = (0x01);
	buffer2[i++] = (0x81);
	buffer2[i++] = (0x00);
	buffer2[i++] = (0x0a);
	buffer2[i++] = (0x01);
	buffer2[i++] = (0x01);
	ASN1_ADD_WORDCOMP(comp, ASN1_INTEGER, buffer2, i, call_reference);
	ASN1_FIXUP(compstk, compsp, buffer2, i);
	ASN1_FIXUP(compstk, compsp, buffer2, i);
	
	
	res = pri_call_apdu_queue(c2, Q931_FACILITY, buffer2, i, NULL, NULL);
	if (res) {
		pri_message(pri, "Could not queue ADPU in facility message\n");
		return -1;
	}
	
	/* Remember that if we queue a facility IE for a facility message we
	 * have to explicitly send the facility message ourselves */
	
	res = q931_facility(c2->pri, c2);
	if (res) {
		pri_message(pri, "Could not schedule facility message for call %d\n", c1->cr);
		return -1;
	}
	
	return 0;
}
/* End AFN-PR */

/* AOC */
static int aoc_aoce_charging_request_decode(struct pri *pri, q931_call *call, unsigned char *data, int len) 
{
	int chargingcase = -1;
	unsigned char *vdata = data;
	struct rose_component *comp = NULL;
	int pos1 = 0;

	if (pri->debug & PRI_DEBUG_AOC)
		dump_apdu (pri, data, len);

	do {
		GET_COMPONENT(comp, pos1, vdata, len);
		CHECK_COMPONENT(comp, ASN1_ENUMERATED, "!! Invalid AOC Charging Request argument. Expected Enumerated (0x0A) but Received 0x%02X\n");
		ASN1_GET_INTEGER(comp, chargingcase);				
		if (chargingcase >= 0 && chargingcase <= 2) {
			if (pri->debug & PRI_DEBUG_APDU)
				pri_message(pri, "Channel %d/%d, Call %d  - received AOC charging request - charging case: %i\n", 
					call->ds1no, call->channelno, call->cr, chargingcase);
		} else {
			pri_message(pri, "!! unkown AOC ChargingCase: 0x%02X", chargingcase);
			chargingcase = -1;
		}
		NEXT_COMPONENT(comp, pos1);
	} while (pos1 < len);
	if (pos1 < len) {
		pri_message(pri, "!! Only reached position %i in %i bytes long AOC-E structure:", pos1, len );
		dump_apdu (pri, data, len);
		return -1;	/* Aborted before */
	}
	return 0;
}
	

static int aoc_aoce_charging_unit_decode(struct pri *pri, q931_call *call, unsigned char *data, int len) 
{
	long chargingunits = 0, chargetype = -1, temp, chargeIdentifier = -1;
	unsigned char *vdata = data;
	struct rose_component *comp1 = NULL, *comp2 = NULL, *comp3 = NULL;
	int pos1 = 0, pos2, pos3, sublen2, sublen3;
	struct addressingdataelements_presentednumberunscreened chargednr;

	if (pri->debug & PRI_DEBUG_AOC)
		dump_apdu (pri, data, len);

	do {
		GET_COMPONENT(comp1, pos1, vdata, len);	/* AOCEChargingUnitInfo */
		CHECK_COMPONENT(comp1, ASN1_SEQUENCE, "!! Invalid AOC-E Charging Unit argument. Expected Sequence (0x30) but Received 0x%02X\n");
		SUB_COMPONENT(comp1, pos1);
		GET_COMPONENT(comp1, pos1, vdata, len);
		switch (comp1->type) {
			case (ASN1_SEQUENCE | ASN1_CONSTRUCTOR):	/* specificChargingUnits */
				sublen2 = comp1->len; 
				pos2 = pos1;
				comp2 = comp1;
				SUB_COMPONENT(comp2, pos2);
				do {
					GET_COMPONENT(comp2, pos2, vdata, len);
					switch (comp2->type) {
						case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_1):	/* RecordedUnitsList (0xA1) */
							SUB_COMPONENT(comp2, pos2);
							GET_COMPONENT(comp2, pos2, vdata, len);
							CHECK_COMPONENT(comp2, ASN1_SEQUENCE, "!! Invalid AOC-E Charging Unit argument. Expected Sequence (0x30) but received 0x02%X\n");	/* RecordedUnits */
							sublen3 = pos2 + comp2->len;
							pos3 = pos2;
							comp3 = comp2;
							SUB_COMPONENT(comp3, pos3);
							do {
								GET_COMPONENT(comp3, pos3, vdata, len);
								switch (comp3->type) {
									case ASN1_INTEGER:	/* numberOfUnits */
										ASN1_GET_INTEGER(comp3, temp);
										chargingunits += temp;
									case ASN1_NULL:		/* notAvailable */
										break;
									default:
										pri_message(pri, "!! Don't know how to handle 0x%02X in AOC-E RecordedUnits\n", comp3->type);
								}
								NEXT_COMPONENT(comp3, pos3);
							} while (pos3 < sublen3);
							if (pri->debug & PRI_DEBUG_AOC)
								pri_message(pri, "Channel %d/%d, Call %d - received AOC-E charging: %i unit%s\n", 
									call->ds1no, call->channelno, call->cr, chargingunits, (chargingunits == 1) ? "" : "s");
							break;
						case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_2):	/* AOCEBillingID (0xA2) */
							SUB_COMPONENT(comp2, pos2);
							GET_COMPONENT(comp2, pos2, vdata, len);
							ASN1_GET_INTEGER(comp2, chargetype);
							pri_message(pri, "!! not handled: Channel %d/%d, Call %d - received AOC-E billing ID: %i\n", 
								call->ds1no, call->channelno, call->cr, chargetype);
							break;
						default:
							pri_message(pri, "!! Don't know how to handle 0x%02X in AOC-E RecordedUnitsList\n", comp2->type);
					}
					NEXT_COMPONENT(comp2, pos2);
				} while (pos2 < sublen2);
				break;
			case (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_1): /* freeOfCharge (0x81) */
				if (pri->debug & PRI_DEBUG_AOC)
					pri_message(pri, "Channel %d/%d, Call %d - received AOC-E free of charge\n", call->ds1no, call->channelno, call->cr);
				chargingunits = 0;
				break;
			default:
				pri_message(pri, "!! Invalid AOC-E specificChargingUnits. Expected Sequence (0x30) or Object Identifier (0x81/0x01) but received 0x%02X\n", comp1->type);
		}
		NEXT_COMPONENT(comp1, pos1);
		GET_COMPONENT(comp1, pos1, vdata, len); /* get optional chargingAssociation. will 'break' when reached end of structure */
		switch (comp1->type) {
			/* TODO: charged number is untested - please report! */
			case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_0): /* chargedNumber (0xA0) */
				if(rose_presented_number_unscreened_decode(pri, call, comp1->data, comp1->len, &chargednr) != 0)
					return -1;
				pri_message(pri, "!! not handled: Received ChargedNr '%s' \n", chargednr.partyaddress);
				pri_message(pri, "  ton = %d, pres = %d, npi = %d\n", chargednr.ton, chargednr.pres, chargednr.npi);
				break;
			case ASN1_INTEGER:
				ASN1_GET_INTEGER(comp1, chargeIdentifier);
				break;
			default:
				pri_message(pri, "!! Invalid AOC-E chargingAssociation. Expected Object Identifier (0xA0) or Integer (0x02) but received 0x%02X\n", comp1->type);
		}
		NEXT_COMPONENT(comp1, pos1);
	} while (pos1 < len);

	if (pos1 < len) {
		pri_message(pri, "!! Only reached position %i in %i bytes long AOC-E structure:", pos1, len );
		dump_apdu (pri, data, len);
		return -1;	/* oops - aborted before */
	}
	call->aoc_units = chargingunits;
	
	return 0;
}

static int aoc_aoce_charging_unit_encode(struct pri *pri, q931_call *c, long chargedunits)
{
	/* sample data: [ 91 a1 12 02 02 3a 78 02 01 24 30 09 30 07 a1 05 30 03 02 01 01 ] */
	int i = 0, res = 0, compsp = 0;
	unsigned char buffer[255] = "";
	struct rose_component *comp = NULL, *compstk[10];

	/* ROSE protocol (0x91)*/
	buffer[i++] = (ASN1_CONTEXT_SPECIFIC | Q932_PROTOCOL_ROSE);

	/* ROSE Component (0xA1,len)*/
	ASN1_ADD_SIMPLE(comp, COMP_TYPE_INVOKE, buffer, i);
	ASN1_PUSH(compstk, compsp, comp); 

	/* ROSE invokeId component (0x02,len,id)*/
	ASN1_ADD_WORDCOMP(comp, INVOKE_IDENTIFIER, buffer, i, ++pri->last_invoke);

	/* ROSE operationId component (0x02,0x01,0x24)*/
	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, ROSE_AOC_AOCE_CHARGING_UNIT);

	/* AOCEChargingUnitInfo (0x30,len) */
	ASN1_ADD_SIMPLE(comp, (ASN1_CONSTRUCTOR | ASN1_SEQUENCE), buffer, i);
	ASN1_PUSH(compstk, compsp, comp);

	if (chargedunits > 0) {
		/* SpecificChargingUnits (0x30,len) */
		ASN1_ADD_SIMPLE(comp, (ASN1_CONSTRUCTOR | ASN1_SEQUENCE), buffer, i);
		ASN1_PUSH(compstk, compsp, comp);

		/* RecordedUnitsList (0xA1,len) */
		ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_1), buffer, i);
		ASN1_PUSH(compstk, compsp, comp);
		
		/* RecordedUnits (0x30,len) */
		ASN1_ADD_SIMPLE(comp, (ASN1_CONSTRUCTOR | ASN1_SEQUENCE), buffer, i);
		ASN1_PUSH(compstk, compsp, comp);
		
		/* NumberOfUnits (0x02,len,charge) */
		ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, chargedunits);

		ASN1_FIXUP(compstk, compsp, buffer, i);
		ASN1_FIXUP(compstk, compsp, buffer, i);
		ASN1_FIXUP(compstk, compsp, buffer, i);
	} else {
		/* freeOfCharge (0x81,0) */
		ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_1), buffer, i);
	}
	ASN1_FIXUP(compstk, compsp, buffer, i);
	ASN1_FIXUP(compstk, compsp, buffer, i); 
	
	if (pri->debug & PRI_DEBUG_AOC)
		dump_apdu (pri, buffer, i);
		
	/* code below is untested */
	res = pri_call_apdu_queue(c, Q931_FACILITY, buffer, i, NULL, NULL);
	if (res) {
		pri_message(pri, "Could not queue APDU in facility message\n");
		return -1;
	}

	/* Remember that if we queue a facility IE for a facility message we
	 * have to explicitly send the facility message ourselves */
	res = q931_facility(c->pri, c);
	if (res) {
		pri_message(pri, "Could not schedule facility message for call %d\n", c->cr);
		return -1;
	}

	return 0;
}
/* End AOC */

/* ===== Call Transfer Supplementary Service (ECMA-178) ===== */

static int rose_party_number_decode(struct pri *pri, q931_call *call, unsigned char *data, int len, struct addressingdataelements_presentednumberunscreened *value)
{
	int i = 0;
	int size = 0;
	struct rose_component *comp = NULL;
	unsigned char *vdata = data;


	do {
		GET_COMPONENT(comp, i, vdata, len);

		switch(comp->type) {
			case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_0):   /* [0] IMPLICIT NumberDigits -- default: unknownPartyNumber */
				if (pri->debug & PRI_DEBUG_APDU)
					pri_message(pri, "     PartyNumber: UnknownPartyNumber len=%d\n", len);
				size = rose_number_digits_decode(pri, call, comp->data, comp->len, value);
				if (size < 0)
					return -1;
				value->npi = PRI_NPI_UNKNOWN;
				value->ton = PRI_TON_UNKNOWN;
				break;

			case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_1):   /* [1] IMPLICIT PublicPartyNumber */
				if (pri->debug & PRI_DEBUG_APDU)
					pri_message(pri, "     PartyNumber: PublicPartyNumber len=%d\n", len);
				size = rose_public_party_number_decode(pri, call, comp->data, comp->len, value);
				if (size < 0)
					return -1;
				value->npi = PRI_NPI_E163_E164;
				break;

			case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_3):   /* [3] IMPLICIT NumberDigits -- not used: dataPartyNumber */
				pri_message(pri, "!! PartyNumber: dataPartyNumber is reserved!\n");
				size = rose_number_digits_decode(pri, call, comp->data, comp->len, value);
				if (size < 0)
					return -1;
				value->npi = PRI_NPI_X121 /* ??? */;
				value->ton = PRI_TON_UNKNOWN /* ??? */;
				break;

			case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_4):   /* [4] IMPLICIT NumberDigits -- not used: telexPartyNumber */
				pri_message(pri, "!! PartyNumber: telexPartyNumber is reserved!\n");
				size = rose_number_digits_decode(pri, call, comp->data, comp->len, value);
				if (size < 0)
					return -1;
				value->npi = PRI_NPI_F69 /* ??? */;
				value->ton = PRI_TON_UNKNOWN /* ??? */;
				break;

			case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_5):   /* [5] IMPLICIT PrivatePartyNumber */
				if (pri->debug & PRI_DEBUG_APDU)
					pri_message(pri, "     PartyNumber: PrivatePartyNumber len=%d\n", len);
				size = rose_private_party_number_decode(pri, call, comp->data, comp->len, value);
				if (size < 0)
					return -1;
 				value->npi = PRI_NPI_PRIVATE;
				break;

			case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_8):   /* [8] IMPLICIT NumberDigits -- not used: nationalStandatdPartyNumber */
				pri_message(pri, "!! PartyNumber: nationalStandardPartyNumber is reserved!\n");
				size = rose_number_digits_decode(pri, call, comp->data, comp->len, value);
				if (size < 0)
					return -1;
				value->npi = PRI_NPI_NATIONAL;
				value->ton = PRI_TON_NATIONAL;
				break;

			default:
				pri_message(pri, "Invalid PartyNumber component 0x%X\n", comp->type);
				return -1;
		}
		ASN1_FIXUP_LEN(comp, size);
		if (pri->debug & PRI_DEBUG_APDU)
			pri_message(pri, "     PartyNumber: '%s' size=%d len=%d\n", value->partyaddress, size, len);
		return size;
	}
	while (0);

	return -1;
}


static int rose_number_screened_decode(struct pri *pri, q931_call *call, unsigned char *data, int len, struct addressingdataelements_presentednumberscreened *value)
{
	int i = 0;
	int size = 0;
	struct rose_component *comp = NULL;
	unsigned char *vdata = data;

	int scrind = -1;

	do {
		/* Party Number */
		GET_COMPONENT(comp, i, vdata, len);
		size = rose_party_number_decode(pri, call, (u_int8_t *)comp, comp->len + 2, (struct addressingdataelements_presentednumberunscreened*) value);
		if (size < 0)
			return -1;
		comp->len = size;
		NEXT_COMPONENT(comp, i);

		/* Screening Indicator */
		GET_COMPONENT(comp, i, vdata, len);
		CHECK_COMPONENT(comp, ASN1_ENUMERATED, "Don't know what to do with NumberScreened ROSE component type 0x%x\n");
		ASN1_GET_INTEGER(comp, scrind);
		// Todo: scrind = screeningindicator_for_q931(pri, scrind);
		NEXT_COMPONENT(comp, i);

		value->scrind = scrind;

		if (pri->debug & PRI_DEBUG_APDU)
			pri_message(pri, "     NumberScreened: '%s' ScreeningIndicator=%d  i=%d  len=%d\n", value->partyaddress, scrind, i, len);

		return i-2;  // We do not have a sequence header here.
	}
	while (0);

	return -1;
}


static int rose_presented_number_screened_decode(struct pri *pri, q931_call *call, unsigned char *data, int len, struct addressingdataelements_presentednumberscreened *value)
{
	int i = 0;
	int size = 0;
	struct rose_component *comp = NULL;
	unsigned char *vdata = data;

	/* Fill in default values */
	value->ton = PRI_TON_UNKNOWN;
	value->npi = PRI_NPI_UNKNOWN;
	value->pres = -1; /* Data is not available */

	do {
		GET_COMPONENT(comp, i, vdata, len);

		switch(comp->type) {
			case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_0):   /* [0] IMPLICIT presentationAllowedNumber */
				if (pri->debug & PRI_DEBUG_APDU)
					pri_message(pri, "     PresentedNumberScreened: presentationAllowedNumber comp->len=%d\n", comp->len);
				value->pres = PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN;
				size = rose_number_screened_decode(pri, call, comp->data, comp->len, value);
				if (size < 0)
					return -1;
				ASN1_FIXUP_LEN(comp, size);
				return size + 2;

			case (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_1):    /* [1] IMPLICIT presentationRestricted */
				if (pri->debug & PRI_DEBUG_APDU)
					pri_message(pri, "     PresentedNumberScreened: presentationRestricted comp->len=%d\n", comp->len);
				if (comp->len != 0) { /* must be NULL */
					pri_error(pri, "!! Invalid PresentationRestricted component received (len != 0)\n");
					return -1;
				}
				value->pres = PRES_PROHIB_USER_NUMBER_PASSED_SCREEN;
				return 2;

			case (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_2):    /* [2] IMPLICIT numberNotAvailableDueToInterworking */
				if (pri->debug & PRI_DEBUG_APDU)
					pri_message(pri, "     PresentedNumberScreened: NumberNotAvailableDueToInterworking comp->len=%d\n", comp->len);
				if (comp->len != 0) { /* must be NULL */
					pri_error(pri, "!! Invalid NumberNotAvailableDueToInterworking component received (len != 0)\n");
					return -1;
				}
				value->pres = PRES_NUMBER_NOT_AVAILABLE;
				if (pri->debug & PRI_DEBUG_APDU)
					pri_message(pri, "     PresentedNumberScreened: numberNotAvailableDueToInterworking Type=0x%X  i=%d len=%d size=%d\n", comp->type, i, len);
				return 2;

			case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_3):    /* [3] IMPLICIT presentationRestrictedNumber */
				if (pri->debug & PRI_DEBUG_APDU)
					pri_message(pri, "     PresentedNumberScreened: presentationRestrictedNumber comp->len=%d\n", comp->len);
				value->pres = PRES_PROHIB_USER_NUMBER_PASSED_SCREEN;
				size = rose_number_screened_decode(pri, call, comp->data, comp->len, value);
				if (size < 0)
					return -1;
				ASN1_FIXUP_LEN(comp, size);
				return size + 2;

			default:
				pri_message(pri, "Invalid PresentedNumberScreened component 0x%X\n", comp->type);
		}
		return -1;
	}
	while (0);

	return -1;
}


static int rose_call_transfer_complete_decode(struct pri *pri, q931_call *call, struct rose_component *sequence, int len)
{
	int i = 0;
	struct rose_component *comp = NULL;
	unsigned char *vdata = sequence->data;
	int res = 0;

	int end_designation = 0;
	struct addressingdataelements_presentednumberscreened redirection_number;
	char redirection_name[50] = "";
	int call_status = 0;
	redirection_number.partyaddress[0] = 0;
	redirection_number.partysubaddress[0] = 0;
	call->callername[0] = 0;
	call->callernum[0] = 0;


	/* Data checks */
	if (sequence->type != (ASN1_CONSTRUCTOR | ASN1_SEQUENCE)) { /* Constructed Sequence */
		pri_message(pri, "Invalid callTransferComplete argument. (Not a sequence)\n");
		return -1;
	}

	if (sequence->len == ASN1_LEN_INDEF) {
		len -= 4; /* For the 2 extra characters at the end
					   * and two characters of header */
	} else
		len -= 2;

	if (pri->debug & PRI_DEBUG_APDU)
		pri_message(pri, "     CT-Complete: len=%d\n", len);

	do {
		/* End Designation */
		GET_COMPONENT(comp, i, vdata, len);
		CHECK_COMPONENT(comp, ASN1_ENUMERATED, "Invalid endDesignation type 0x%X of ROSE callTransferComplete component received\n");
		ASN1_GET_INTEGER(comp, end_designation);
		NEXT_COMPONENT(comp, i);
		if (pri->debug & PRI_DEBUG_APDU)
			pri_message(pri, "     CT-Complete: Received endDesignation=%d\n", end_designation);


		/* Redirection Number */
		GET_COMPONENT(comp, i, vdata, len);
		res = rose_presented_number_screened_decode(pri, call, (u_int8_t *)comp, comp->len + 2, &redirection_number);
		if (res < 0)
			return -1;
		comp->len = res;
		if (res > 2) {
			if (pri->debug & PRI_DEBUG_APDU)
				pri_message(pri, "     CT-Complete: Received redirectionNumber=%s\n", redirection_number.partyaddress);
			strncpy(call->callernum, redirection_number.partyaddress, 20);
			call->callernum[20] = 0;
		}
		NEXT_COMPONENT(comp, i);


#if 0 /* This one is optional. How do we check if it is there? */
		/* Basic Call Info Elements */
		GET_COMPONENT(comp, i, vdata, len);
		NEXT_COMPONENT(comp, i);
#endif


		/* Redirection Name */
		GET_COMPONENT(comp, i, vdata, len);
		res = asn1_name_decode((u_int8_t *)comp, comp->len + 2, redirection_name, sizeof(redirection_name));
		if (res < 0)
			return -1;
		memcpy(call->callername, comp->data, comp->len);
		call->callername[comp->len] = 0;
		ASN1_FIXUP_LEN(comp, res);
		comp->len = res;
		NEXT_COMPONENT(comp, i);
		if (pri->debug & PRI_DEBUG_APDU)
			pri_message(pri, "     CT-Complete: Received redirectionName '%s'\n", redirection_name);


		/* Call Status */
		GET_COMPONENT(comp, i, vdata, len);
		CHECK_COMPONENT(comp, ASN1_ENUMERATED, "Invalid callStatus type 0x%X of ROSE callTransferComplete component received\n");
		ASN1_GET_INTEGER(comp, call_status);
		NEXT_COMPONENT(comp, i);
		if (pri->debug & PRI_DEBUG_APDU)
			pri_message(pri, "     CT-Complete: Received callStatus=%d\n", call_status);


		/* Argument Extension */
#if 0 /* Not supported */
		GET_COMPONENT(comp, i, vdata, len);
		switch (comp->type) {
			case (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_9):   /* [9] IMPLICIT Extension */
				res = rose_extension_decode(pri, call, comp->data, comp->len, &redirection_number);
				if (res < 0)
					return -1;
				ASN1_FIXUP_LEN(comp, res);
				comp->len = res;

			case (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_10):    /* [10] IMPLICIT SEQUENCE OF Extension */
				res = rose_sequence_of_extension_decode(pri, call, comp->data, comp->len, &redirection_number);
				if (res < 0)
					return -1;
				ASN1_FIXUP_LEN(comp, res);
				comp->len = res;

			default:
			pri_message(pri, "     CT-Complete: !! Unknown argumentExtension received 0x%X\n", comp->type);
			return -1;
		}
#else
		GET_COMPONENT(comp, i, vdata, len);
		ASN1_FIXUP_LEN(comp, res);
		NEXT_COMPONENT(comp, i);
#endif

		if(i < len)
			pri_message(pri, "     CT-Complete: !! not all information is handled !! i=%d / len=%d\n", i, len);

		return 0;
	}
	while (0);

	return -1;
}


static int rose_call_transfer_update_decode(struct pri *pri, q931_call *call, struct rose_component *sequence, int len)
{
	int i = 0;
	struct rose_component *comp = NULL;
	unsigned char *vdata = sequence->data;
	int res = 0;

	struct addressingdataelements_presentednumberscreened redirection_number;
	redirection_number.partyaddress[0] = 0;
	redirection_number.partysubaddress[0] = 0;
	char redirection_name[50] = "";
	call->callername[0] = 0;
	call->callernum[0] = 0;


	/* Data checks */
	if (sequence->type != (ASN1_CONSTRUCTOR | ASN1_SEQUENCE)) { /* Constructed Sequence */
		pri_message(pri, "Invalid callTransferComplete argument. (Not a sequence)\n");
		return -1;
	}

	if (sequence->len == ASN1_LEN_INDEF) {
		len -= 4; /* For the 2 extra characters at the end
					   * and two characters of header */
	} else
		len -= 2;

	if (pri->debug & PRI_DEBUG_APDU)
		pri_message(pri, "     CT-Complete: len=%d\n", len);

	do {
		/* Redirection Number */
		GET_COMPONENT(comp, i, vdata, len);
		res = rose_presented_number_screened_decode(pri, call, (u_int8_t *)comp, comp->len + 2, &redirection_number);
		if (res < 0)
			return -1;
		comp->len = res;
		if (res > 2) {
			if (pri->debug & PRI_DEBUG_APDU)
				pri_message(pri, "     CT-Complete: Received redirectionNumber=%s\n", redirection_number.partyaddress);
			strncpy(call->callernum, redirection_number.partyaddress, 20);
			call->callernum[20] = 0;
		}
		NEXT_COMPONENT(comp, i);

		/* Redirection Name */
		GET_COMPONENT(comp, i, vdata, len);
		res = asn1_name_decode((u_int8_t *)comp, comp->len + 2, redirection_name, sizeof(redirection_name));
		if (res < 0)
			return -1;
		memcpy(call->callername, comp->data, comp->len);
		call->callername[comp->len] = 0;
		ASN1_FIXUP_LEN(comp, res);
		comp->len = res;
		NEXT_COMPONENT(comp, i);
		if (pri->debug & PRI_DEBUG_APDU)
			pri_message(pri, "     CT-Complete: Received redirectionName '%s'\n", redirection_name);


#if 0 /* This one is optional. How do we check if it is there? */
		/* Basic Call Info Elements */
		GET_COMPONENT(comp, i, vdata, len);
		NEXT_COMPONENT(comp, i);
#endif


		/* Argument Extension */
#if 0 /* Not supported */
		GET_COMPONENT(comp, i, vdata, len);
		switch (comp->type) {
			case (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_9):   /* [9] IMPLICIT Extension */
				res = rose_extension_decode(pri, call, comp->data, comp->len, &redirection_number);
				if (res < 0)
					return -1;
				ASN1_FIXUP_LEN(comp, res);
				comp->len = res;

			case (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_10):    /* [10] IMPLICIT SEQUENCE OF Extension */
				res = rose_sequence_of_extension_decode(pri, call, comp->data, comp->len, &redirection_number);
				if (res < 0)
					return -1;
				ASN1_FIXUP_LEN(comp, res);
				comp->len = res;

			default:
				pri_message(pri, "     CT-Complete: !! Unknown argumentExtension received 0x%X\n", comp->type);
				return -1;
		}
#else
		GET_COMPONENT(comp, i, vdata, len);
		ASN1_FIXUP_LEN(comp, res);
		NEXT_COMPONENT(comp, i);
#endif

		if(i < len)
			pri_message(pri, "     CT-Complete: !! not all information is handled !! i=%d / len=%d\n", i, len);

		return 0;
	}
	while (0);

	return -1;
}


/* ===== End Call Transfer Supplementary Service (ECMA-178) ===== */



int rose_reject_decode(struct pri *pri, q931_call *call, q931_ie *ie, unsigned char *data, int len)
{
	int i = 0;
	int problemtag = -1;
	int problem = -1;
	int invokeidvalue = -1;
	unsigned char *vdata = data;
	struct rose_component *comp = NULL;
	char *problemtagstr, *problemstr;
	
	do {
		/* Invoke ID stuff */
		GET_COMPONENT(comp, i, vdata, len);
		CHECK_COMPONENT(comp, INVOKE_IDENTIFIER, "Don't know what to do if first ROSE component is of type 0x%x\n");
		ASN1_GET_INTEGER(comp, invokeidvalue);
		NEXT_COMPONENT(comp, i);

		GET_COMPONENT(comp, i, vdata, len);
		problemtag = comp->type;
		problem = comp->data[0];

		if (pri->switchtype == PRI_SWITCH_DMS100) {
			switch (problemtag) {
			case 0x80:
				problemtagstr = "General problem";
				break;
			case 0x81:
				problemtagstr = "Invoke problem";
				break;
			case 0x82:
				problemtagstr = "Return result problem";
				break;
			case 0x83:
				problemtagstr = "Return error problem";
				break;
			default:
				problemtagstr = "Unknown";
			}

			switch (problem) {
			case 0x00:
				problemstr = "Unrecognized component";
				break;
			case 0x01:
				problemstr = "Mistyped component";
				break;
			case 0x02:
				problemstr = "Badly structured component";
				break;
			default:
				problemstr = "Unknown";
			}

			pri_error(pri, "ROSE REJECT:\n");
			pri_error(pri, "\tINVOKE ID: 0x%X\n", invokeidvalue);
			pri_error(pri, "\tPROBLEM TYPE: %s (0x%x)\n", problemtagstr, problemtag);
			pri_error(pri, "\tPROBLEM: %s (0x%x)\n", problemstr, problem);

			return 0;
		} else {
			pri_message(pri, "Unable to handle return result on switchtype %d!\n", pri->switchtype);
			return -1;
		}

	} while(0);
	
	return -1;
}
int rose_return_error_decode(struct pri *pri, q931_call *call, q931_ie *ie, unsigned char *data, int len)
{
	int i = 0;
	int errorvalue = -1;
	int invokeidvalue = -1;
	unsigned char *vdata = data;
	struct rose_component *comp = NULL;
	char *invokeidstr, *errorstr;
	
	do {
		/* Invoke ID stuff */
		GET_COMPONENT(comp, i, vdata, len);
		CHECK_COMPONENT(comp, INVOKE_IDENTIFIER, "Don't know what to do if first ROSE component is of type 0x%x\n");
		ASN1_GET_INTEGER(comp, invokeidvalue);
		NEXT_COMPONENT(comp, i);

		GET_COMPONENT(comp, i, vdata, len);
		CHECK_COMPONENT(comp, ASN1_INTEGER, "Don't know what to do if second component in return error is 0x%x\n");
		ASN1_GET_INTEGER(comp, errorvalue);

		if (pri->switchtype == PRI_SWITCH_DMS100) {
			switch (invokeidvalue) {
			case RLT_OPERATION_IND:
				invokeidstr = "RLT_OPERATION_IND";
				break;
			case RLT_THIRD_PARTY:
				invokeidstr = "RLT_THIRD_PARTY";
				break;
			default:
				invokeidstr = "Unknown";
			}

			switch (errorvalue) {
			case 0x10:
				errorstr = "RLT Bridge Fail";
				break;
			case 0x11:
				errorstr = "RLT Call ID Not Found";
				break;
			case 0x12:
				errorstr = "RLT Not Allowed";
				break;
			case 0x13:
				errorstr = "RLT Switch Equip Congs";
				break;
			default:
				errorstr = "Unknown";
			}

			pri_error(pri, "ROSE RETURN ERROR:\n");
			pri_error(pri, "\tOPERATION: %s\n", invokeidstr);
			pri_error(pri, "\tERROR: %s\n", errorstr);

			return 0;
		} else {
			pri_message(pri, "Unable to handle return result on switchtype %d!\n", pri->switchtype);
			return -1;
		}

	} while(0);
	
	return -1;
}

int rose_return_result_decode(struct pri *pri, q931_call *call, q931_ie *ie, unsigned char *data, int len)
{
	int i = 0;
	int operationidvalue = -1;
	int invokeidvalue = -1;
	unsigned char *vdata = data;
	struct rose_component *comp = NULL;
	
	do {
		/* Invoke ID stuff */
		GET_COMPONENT(comp, i, vdata, len);
		CHECK_COMPONENT(comp, INVOKE_IDENTIFIER, "Don't know what to do if first ROSE component is of type 0x%x\n");
		ASN1_GET_INTEGER(comp, invokeidvalue);
		NEXT_COMPONENT(comp, i);

		if (pri->switchtype == PRI_SWITCH_DMS100) {
			switch (invokeidvalue) {
			case RLT_THIRD_PARTY:
				if (pri->debug & PRI_DEBUG_APDU) pri_message(pri, "Successfully completed RLT transfer!\n");
				return 0;
			case RLT_OPERATION_IND:
				if (pri->debug & PRI_DEBUG_APDU) pri_message(pri, "Received RLT_OPERATION_IND\n");
				/* Have to take out the rlt_call_id */
				GET_COMPONENT(comp, i, vdata, len);
				CHECK_COMPONENT(comp, ASN1_SEQUENCE, "Protocol error detected in parsing RLT_OPERATION_IND return result!\n");

				/* Traverse the contents of this sequence */
				/* First is the Operation Value */
				SUB_COMPONENT(comp, i);
				GET_COMPONENT(comp, i, vdata, len);
				CHECK_COMPONENT(comp, ASN1_INTEGER, "RLT_OPERATION_IND should be of type ASN1_INTEGER!\n");
				ASN1_GET_INTEGER(comp, operationidvalue);

				if (operationidvalue != RLT_OPERATION_IND) {
					pri_message(pri, "Invalid Operation ID value (0x%x) in return result!\n", operationidvalue);
					return -1;
				}

				/*  Next is the Call ID */
				NEXT_COMPONENT(comp, i);
				GET_COMPONENT(comp, i, vdata, len);
				CHECK_COMPONENT(comp, ASN1_TAG_0, "Error check failed on Call ID!\n");
				ASN1_GET_INTEGER(comp, call->rlt_call_id);
				/* We have enough data to transfer the call */
				call->transferable = 1;

				return 0;
				
			default:
				pri_message(pri, "Could not parse invoke of type 0x%x!\n", invokeidvalue);
				return -1;
			}
		} else {
			pri_message(pri, "Unable to handle return result on switchtype %d!\n", pri->switchtype);
			return -1;
		}

	} while(0);
	
	return -1;
}

int rose_invoke_decode(struct pri *pri, q931_call *call, q931_ie *ie, unsigned char *data, int len)
{
	int i = 0;
	int res = 0;
	int operation_tag;
	unsigned char *vdata = data;
	struct rose_component *comp = NULL, *invokeid = NULL, *operationid = NULL;
	
	do {
		/* Invoke ID stuff */
		GET_COMPONENT(comp, i, vdata, len);
#if 0
		CHECK_COMPONENT(comp, INVOKE_IDENTIFIER, "Don't know what to do if first ROSE component is of type 0x%x\n");
#endif
		invokeid = comp;
		NEXT_COMPONENT(comp, i);

		/* Operation Tag */
		GET_COMPONENT(comp, i, vdata, len);
#if 0
		CHECK_COMPONENT(comp, ASN1_INTEGER, "Don't know what to do if second ROSE component is of type 0x%x\n");
#endif
		operationid = comp;
		ASN1_GET_INTEGER(comp, operation_tag);
		NEXT_COMPONENT(comp, i);

		/* No argument - return with error */
		if (i >= len) 
			return -1;

		/* Arguement Tag */
		GET_COMPONENT(comp, i, vdata, len);
		if (!comp->type)
			return -1;

		if (pri->debug & PRI_DEBUG_APDU)
			pri_message(pri, "  [ Handling operation %d ]\n", operation_tag);
		switch (operation_tag) {
		case SS_CNID_CALLINGNAME:
			if (pri->debug & PRI_DEBUG_APDU)
				pri_message(pri, "  Handle Name display operation\n");
			switch (comp->type) {
				case ROSE_NAME_PRESENTATION_ALLOWED_SIMPLE:
					memcpy(call->callername, comp->data, comp->len);
					call->callername[comp->len] = 0;
					if (pri->debug & PRI_DEBUG_APDU)
						pri_message(pri, "    Received caller name '%s'\n", call->callername);
					return 0;
				default:
					if (pri->debug & PRI_DEBUG_APDU)
						pri_message(pri, "Do not handle argument of type 0x%X\n", comp->type);
					return -1;
			}
			break;
		case ROSE_CALL_TRANSFER_IDENTIFY:
			if (pri->debug & PRI_DEBUG_APDU)
				pri_message(pri, "ROSE %i:   CallTransferIdentify - not handled!\n", operation_tag);
			dump_apdu (pri, (u_int8_t *)comp, comp->len + 2);
			return -1;
		case ROSE_CALL_TRANSFER_ABANDON:
			if (pri->debug & PRI_DEBUG_APDU)
				pri_message(pri, "ROSE %i:   CallTransferAbandon - not handled!\n", operation_tag);
			dump_apdu (pri, (u_int8_t *)comp, comp->len + 2);
			return -1;
		case ROSE_CALL_TRANSFER_INITIATE:
			if (pri->debug & PRI_DEBUG_APDU)
				pri_message(pri, "ROSE %i:   CallTransferInitiate - not handled!\n", operation_tag);
			dump_apdu (pri, (u_int8_t *)comp, comp->len + 2);
			return -1;
		case ROSE_CALL_TRANSFER_SETUP:
			if (pri->debug & PRI_DEBUG_APDU)
				pri_message(pri, "ROSE %i:   CallTransferSetup - not handled!\n", operation_tag);
			dump_apdu (pri, (u_int8_t *)comp, comp->len + 2);
			return -1;
		case ROSE_CALL_TRANSFER_ACTIVE:
			if (pri->debug & PRI_DEBUG_APDU)
				pri_message(pri, "ROSE %i:   CallTransferActive - not handled!\n", operation_tag);
			dump_apdu (pri, (u_int8_t *)comp, comp->len + 2);
			return -1;
		case ROSE_CALL_TRANSFER_COMPLETE:
			if (pri->debug & PRI_DEBUG_APDU)
			{
				pri_message(pri, "ROSE %i:   Handle CallTransferComplete\n", operation_tag);
				dump_apdu (pri, (u_int8_t *)comp, comp->len + 2);
			}
			return rose_call_transfer_complete_decode(pri, call, comp, len-i);
		case ROSE_CALL_TRANSFER_UPDATE:
			if (pri->debug & PRI_DEBUG_APDU)
			{
				pri_message(pri, "ROSE %i:    Handle CallTransferUpdate\n", operation_tag);
				dump_apdu (pri, (u_int8_t *)comp, comp->len + 2);
			}
			return rose_call_transfer_update_decode(pri, call, comp, len-i);
		case ROSE_SUBADDRESS_TRANSFER:
			if (pri->debug & PRI_DEBUG_APDU)
				pri_message(pri, "ROSE %i:   SubaddressTransfer - not handled!\n", operation_tag);
			dump_apdu (pri, (u_int8_t *)comp, comp->len + 2);
			return -1;
		case ROSE_DIVERTING_LEG_INFORMATION2:
			if (pri->debug & PRI_DEBUG_APDU)
				pri_message(pri, "  Handle DivertingLegInformation2\n");
			return rose_diverting_leg_information2_decode(pri, call, comp, len-i);
		case ROSE_AOC_NO_CHARGING_INFO_AVAILABLE:
			if (pri->debug & PRI_DEBUG_APDU) {
				pri_message(pri, "ROSE %i: AOC No Charging Info Available - not handled!", operation_tag);
				dump_apdu (pri, comp->data, comp->len);
			}
			return -1;
		case ROSE_AOC_CHARGING_REQUEST:
			return aoc_aoce_charging_request_decode(pri, call, (u_int8_t *)comp, comp->len + 2);
		case ROSE_AOC_AOCS_CURRENCY:
			if (pri->debug & PRI_DEBUG_APDU) {
				pri_message(pri, "ROSE %i: AOC-S Currency - not handled!", operation_tag);
				dump_apdu (pri, (u_int8_t *)comp, comp->len + 2);
			}
			return -1;
		case ROSE_AOC_AOCS_SPECIAL_ARR:
			if (pri->debug & PRI_DEBUG_APDU) {
				pri_message(pri, "ROSE %i: AOC-S Special Array - not handled!", operation_tag);
				dump_apdu (pri, (u_int8_t *)comp, comp->len + 2);
			}
			return -1;
		case ROSE_AOC_AOCD_CURRENCY:
			if (pri->debug & PRI_DEBUG_APDU) {
				pri_message(pri, "ROSE %i: AOC-D Currency - not handled!", operation_tag);
				dump_apdu (pri, (u_int8_t *)comp, comp->len + 2);
			}
			return -1;
		case ROSE_AOC_AOCD_CHARGING_UNIT:
			if (pri->debug & PRI_DEBUG_APDU) {
				pri_message(pri, "ROSE %i: AOC-D Charging Unit - not handled!", operation_tag);
				dump_apdu (pri, (u_int8_t *)comp, comp->len + 2);
			}
			return -1;
		case ROSE_AOC_AOCE_CURRENCY:
			if (pri->debug & PRI_DEBUG_APDU) {
				pri_message(pri, "ROSE %i: AOC-E Currency - not handled!", operation_tag);
				dump_apdu (pri, (u_int8_t *)comp, comp->len + 2);
			}
			return -1;
		case ROSE_AOC_AOCE_CHARGING_UNIT:
			return aoc_aoce_charging_unit_decode(pri, call, (u_int8_t *)comp, comp->len + 2);
			if (0) { /* the following function is currently not used - just to make the compiler happy */
				aoc_aoce_charging_unit_encode(pri, call, call->aoc_units); /* use this function to forward the aoc-e on a bridged channel */ 
				return 0;
			}
		case ROSE_AOC_IDENTIFICATION_OF_CHARGE:
			if (pri->debug & PRI_DEBUG_APDU) {
				pri_message(pri, "ROSE %i: AOC Identification Of Charge - not handled!", operation_tag);
				dump_apdu (pri, (u_int8_t *)comp, comp->len + 2);
			}
			return -1;
                case SS_ANFPR_PATHREPLACEMENT:
			/* Clear Queue */
			res = pri_call_apdu_queue_cleanup(call->bridged_call);
			if (res) {
			        pri_message(pri, "Could not Clear queue ADPU\n");
			        return -1;
			}
			anfpr_pathreplacement_respond(pri, call, ie);
                        break;
		default:
			if (pri->debug & PRI_DEBUG_APDU) {
				pri_message(pri, "!! Unable to handle ROSE operation %d", operation_tag);
				dump_apdu (pri, (u_int8_t *)comp, comp->len + 2);
			}
			return -1;
		}
	} while(0);
	
	return -1;
}

int pri_call_apdu_queue(q931_call *call, int messagetype, void *apdu, int apdu_len, void (*function)(void *data), void *data)
{
	struct apdu_event *cur = NULL;
	struct apdu_event *new_event = NULL;

	if (!call || !messagetype || !apdu || (apdu_len < 1) || (apdu_len > 255))
		return -1;

	if (!(new_event = calloc(1, sizeof(*new_event)))) {
		pri_error(call->pri, "!! Malloc failed!\n");
		return -1;
	}

	new_event->message = messagetype;
	new_event->callback = function;
	new_event->data = data;
	memcpy(new_event->apdu, apdu, apdu_len);
	new_event->apdu_len = apdu_len;
	
	if (call->apdus) {
		cur = call->apdus;
		while (cur->next) {
			cur = cur->next;
		}
		cur->next = new_event;
	} else
		call->apdus = new_event;

	return 0;
}

int pri_call_apdu_queue_cleanup(q931_call *call)
{
	struct apdu_event *cur_event = NULL, *free_event = NULL;

	if (call && call->apdus) {
		cur_event = call->apdus;
		while (cur_event) {
			/* TODO: callbacks, some way of giving return res on status of apdu */
			free_event = cur_event;
			cur_event = cur_event->next;
			free(free_event);
		}
		call->apdus = NULL;
	}

	return 0;
}

int pri_call_add_standard_apdus(struct pri *pri, q931_call *call)
{
	if (!pri->sendfacility)
		return 0;

	if (pri->switchtype == PRI_SWITCH_QSIG) { /* For Q.SIG it does network and cpe operations */
		if (call->redirectingnum[0]) 
			rose_diverting_leg_information2_encode(pri, call);
		add_callername_facility_ies(pri, call, 1);
		return 0;
	}

#if 0
	if (pri->localtype == PRI_NETWORK) {
		switch (pri->switchtype) {
			case PRI_SWITCH_NI2:
				add_callername_facility_ies(pri, call, 0);
				break;
			default:
				break;
		}
		return 0;
	} else if (pri->localtype == PRI_CPE) {
		switch (pri->switchtype) {
			case PRI_SWITCH_NI2:
				add_callername_facility_ies(pri, call, 1);
				break;
			default:
				break;
		}
		return 0;
	}
#else
	if (pri->switchtype == PRI_SWITCH_NI2)
		add_callername_facility_ies(pri, call, (pri->localtype == PRI_CPE));
#endif

	if ((pri->switchtype == PRI_SWITCH_DMS100) && (pri->localtype == PRI_CPE)) {
		add_dms100_transfer_ability_apdu(pri, call);
	}



	return 0;
}

