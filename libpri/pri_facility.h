/*
   This file contains all data structures and definitions associated
   with facility message usage and the ROSE components included
   within those messages.

   by Matthew Fredrickson <creslin@digium.com>
   Copyright (C) Digium, Inc. 2004-2005
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

#ifndef _PRI_FACILITY_H
#define _PRI_FACILITY_H
#include "pri_q931.h"

/* Protocol Profile field */
#define Q932_PROTOCOL_ROSE			0x11	/* X.219 & X.229 */
#define Q932_PROTOCOL_CMIP			0x12	/* Q.941 */
#define Q932_PROTOCOL_ACSE			0x13	/* X.217 & X.227 */
#define Q932_PROTOCOL_GAT			0x16
#define Q932_PROTOCOL_EXTENSIONS	0x1F

/* Argument values */
#define ROSE_NAME_PRESENTATION_ALLOWED_SIMPLE	0x80
#define ROSE_NAME_PRESENTATION_RESTRICTED_NULL	0x87
#define ROSE_NAME_NOT_AVAIL						0x84

/* Component types */
#define COMP_TYPE_INTERPRETATION			0x8B
#define COMP_TYPE_NETWORK_PROTOCOL_PROFILE	0x92
#define COMP_TYPE_INVOKE					0xA1
#define COMP_TYPE_RETURN_RESULT				0xA2
#define COMP_TYPE_RETURN_ERROR				0xA3
#define COMP_TYPE_REJECT					0xA4
#define COMP_TYPE_NFE						0xAA

/* Operation ID values */
/* Q.952.7 (ECMA-178) ROSE operations (Transfer) */
#define ROSE_CALL_TRANSFER_IDENTIFY			7
#define ROSE_CALL_TRANSFER_ABANDON			8
#define ROSE_CALL_TRANSFER_INITIATE			9
#define ROSE_CALL_TRANSFER_SETUP			10
#define ROSE_CALL_TRANSFER_ACTIVE			11
#define ROSE_CALL_TRANSFER_COMPLETE			12
#define ROSE_CALL_TRANSFER_UPDATE			13
#define ROSE_SUBADDRESS_TRANSFER 			14
/* Q.952 ROSE operations (Diverting) */
#define ROSE_DIVERTING_LEG_INFORMATION1		18
#define ROSE_DIVERTING_LEG_INFORMATION2		0x15
#define ROSE_DIVERTING_LEG_INFORMATION3		19
/* Q.956 ROSE operations (Advice Of Charge) */
#define ROSE_AOC_NO_CHARGING_INFO_AVAILABLE	26
#define ROSE_AOC_CHARGING_REQUEST			30
#define ROSE_AOC_AOCS_CURRENCY				31
#define ROSE_AOC_AOCS_SPECIAL_ARR			32
#define ROSE_AOC_AOCD_CURRENCY				33
#define ROSE_AOC_AOCD_CHARGING_UNIT			34
#define ROSE_AOC_AOCE_CURRENCY				35
#define ROSE_AOC_AOCE_CHARGING_UNIT			36
#define ROSE_AOC_IDENTIFICATION_OF_CHARGE	37
/* Q.SIG operations */
#define SS_CNID_CALLINGNAME					0
#define SS_ANFPR_PATHREPLACEMENT                                4
#define SS_DIVERTING_LEG_INFORMATION2		21
#define SS_MWI_ACTIVATE						80
#define SS_MWI_DEACTIVATE					81
#define SS_MWI_INTERROGATE					82

/* ROSE definitions and data structures */
#define INVOKE_IDENTIFIER			0x02
#define INVOKE_LINKED_IDENTIFIER	0x80
#define INVOKE_NULL_IDENTIFIER		__USE_ASN1_NULL

/* ASN.1 Identifier Octet - Data types */
#define ASN1_TYPE_MASK			0x1f
#define ASN1_BOOLEAN			0x01
#define ASN1_INTEGER			0x02
#define ASN1_BITSTRING			0x03
#define ASN1_OCTETSTRING		0x04
#define ASN1_NULL				0x05
#define ASN1_OBJECTIDENTIFIER	0x06
#define ASN1_OBJECTDESCRIPTOR	0x07
#define ASN1_EXTERN				0x08
#define ASN1_REAL				0x09
#define ASN1_ENUMERATED			0x0a
#define ASN1_EMBEDDEDPDV		0x0b
#define ASN1_UTF8STRING			0x0c
#define ASN1_RELATIVEOBJECTID	0x0d
/* 0x0e & 0x0f are reserved for future ASN.1 editions */
#define ASN1_SEQUENCE			0x10
#define ASN1_SET				0x11
#define ASN1_NUMERICSTRING		0x12
#define ASN1_PRINTABLESTRING	0x13
#define ASN1_TELETEXSTRING		0x14
#define ASN1_IA5STRING			0x16
#define ASN1_UTCTIME			0x17
#define ASN1_GENERALIZEDTIME	0x18

/* ASN.1 Identifier Octet - Tags */
#define ASN1_TAG_0				0x00
#define ASN1_TAG_1				0x01
#define ASN1_TAG_2				0x02
#define ASN1_TAG_3				0x03
#define ASN1_TAG_4				0x04
#define ASN1_TAG_5				0x05
#define ASN1_TAG_6				0x06
#define ASN1_TAG_7				0x07
#define ASN1_TAG_8				0x08
#define ASN1_TAG_9				0x09

/* ASN.1 Identifier Octet - Primitive/Constructor Bit */
#define ASN1_PC_MASK			0x20
#define ASN1_PRIMITIVE			0x00
#define ASN1_CONSTRUCTOR		0x20

/* ASN.1 Identifier Octet - Clan Bits */
#define ASN1_CLAN_MASK			0xc0
#define ASN1_UNIVERSAL			0x00
#define ASN1_APPLICATION		0x40
#define ASN1_CONTEXT_SPECIFIC		0x80
#define ASN1_PRIVATE			0xc0

/* ASN.1 Length masks */
#define ASN1_LEN_INDEF			0x80


#define INVOKE_OPERATION_INT	__USE_ASN1_INTEGER
#define INVOKE_OBJECT_ID		__USE_ASN1_OBJECTIDENTIFIER

/* Q.952 Divert cause */
#define Q952_DIVERT_REASON_UNKNOWN		0x00
#define Q952_DIVERT_REASON_CFU			0x01
#define Q952_DIVERT_REASON_CFB			0x02
#define Q952_DIVERT_REASON_CFNR			0x03
#define Q952_DIVERT_REASON_CD			0x04
#define Q952_DIVERT_REASON_IMMEDIATE	0x05
/* Q.SIG Divert cause. Listed in ECMA-174 */
#define QSIG_DIVERT_REASON_UNKNOWN		0x00	/* Call forward unknown reason */
#define QSIG_DIVERT_REASON_CFU			0x01	/* Call Forward Unconditional (other reason) */
#define QSIG_DIVERT_REASON_CFB			0x02	/* Call Forward Busy */
#define QSIG_DIVERT_REASON_CFNR			0x03	/* Call Forward No Reply */

/* Q.932 Type of number */
#define Q932_TON_UNKNOWN				0x00
#define Q932_TON_INTERNATIONAL			0x01
#define Q932_TON_NATIONAL				0x02
#define Q932_TON_NET_SPECIFIC			0x03
#define Q932_TON_SUBSCRIBER				0x04
#define Q932_TON_ABBREVIATED			0x06

/* RLT related Operations */
#define RLT_SERVICE_ID		0x3e
#define RLT_OPERATION_IND	0x01
#define RLT_THIRD_PARTY		0x02

struct rose_component {
	u_int8_t type;
	u_int8_t len;
	u_int8_t data[0];
};

#if 1
	#define GET_COMPONENT(component, idx, ptr, length) \
	if ((idx)+2 > (length)) \
		break; \
	(component) = (struct rose_component*)&((ptr)[idx]); \
	if ((idx)+(component)->len+2 > (length)) { \
		if ((component)->len != ASN1_LEN_INDEF) \
			pri_message(pri, "Length (%d) of 0x%X component is too long\n", (component)->len, (component)->type); \
	}
#else /* Debugging */
	#define GET_COMPONENT(component, idx, ptr, length) \
	if ((idx)+2 > (length)) \
		break; \
	(component) = (struct rose_component*)&((ptr)[idx]); \
	if ((idx)+(component)->len+2 > (length)) { \
		if ((component)->len != 128) \
			pri_message(pri, "Length (%d) of 0x%X component is too long\n", (component)->len, (component)->type); \
	} \
	pri_message(pri, "XX  %s:%d  Got component %d (0x%02X), length %d\n", __FUNCTION__, __LINE__, (component)->type, (component)->type, (component)->len); \
	if ((component)->len > 0) { \
		int zzz; \
		pri_message(pri, "XX  Data:"); \
		for (zzz = 0; zzz < (component)->len; ++zzz) \
			pri_message(pri, " %02X", (component)->data[zzz]); \
		pri_message(pri, "\n"); \
	}
#endif

#define NEXT_COMPONENT(component, idx) \
	(idx) += (component)->len + 2

#define SUB_COMPONENT(component, idx) \
	(idx) += 2

#define CHECK_COMPONENT(component, comptype, message) \
	if ((component)->type && ((component)->type & ASN1_TYPE_MASK) != (comptype)) { \
		pri_message(pri, (message), (component)->type); \
		asn1_dump(pri, (component), (component)->len+2); \
		break; \
	}
	
#define ASN1_GET_INTEGER(component, variable) \
	do { \
		int comp_idx; \
		(variable) = 0; \
		for (comp_idx = 0; comp_idx < (component)->len; ++comp_idx) \
			(variable) = ((variable) << 8) | (component)->data[comp_idx]; \
	} while (0)

#define ASN1_FIXUP_LEN(component, size) \
	do { \
		if ((component)->len == ASN1_LEN_INDEF) \
			size += 2; \
	} while (0)

#define ASN1_ADD_SIMPLE(component, comptype, ptr, idx) \
	do { \
		(component) = (struct rose_component *)&((ptr)[(idx)]); \
		(component)->type = (comptype); \
		(component)->len = 0; \
		(idx) += 2; \
	} while (0)

#define ASN1_ADD_BYTECOMP(component, comptype, ptr, idx, value) \
	do { \
		(component) = (struct rose_component *)&((ptr)[(idx)]); \
		(component)->type = (comptype); \
		(component)->len = 1; \
		(component)->data[0] = (value); \
		(idx) += 3; \
	} while (0)

#define ASN1_ADD_WORDCOMP(component, comptype, ptr, idx, value) \
	do { \
		int __val = (value); \
		int __i = 0; \
		(component) = (struct rose_component *)&((ptr)[(idx)]); \
		(component)->type = (comptype); \
		if ((__val >> 24)) \
			(component)->data[__i++] = (__val >> 24) & 0xff; \
		if ((__val >> 16)) \
			(component)->data[__i++] = (__val >> 16) & 0xff; \
		if ((__val >> 8)) \
			(component)->data[__i++] = (__val >> 8) & 0xff; \
		(component)->data[__i++] = __val & 0xff; \
		(component)->len = __i; \
		(idx) += 2 + __i; \
	} while (0)

#define ASN1_PUSH(stack, stackpointer, component) \
	(stack)[(stackpointer)++] = (component)

#define ASN1_FIXUP(stack, stackpointer, data, idx) \
	do { \
		--(stackpointer); \
		(stack)[(stackpointer)]->len = (unsigned char *)&((data)[(idx)]) - (unsigned char *)(stack)[(stackpointer)] - 2; \
	} while (0)

/* Decoder for the invoke ROSE component */
int rose_invoke_decode(struct pri *pri, struct q931_call *call, q931_ie *ie, unsigned char *data, int len);

/* Decoder for the return result ROSE component */
int rose_return_result_decode(struct pri *pri, struct q931_call *call, q931_ie *ie, unsigned char *data, int len);

/* Decoder for the return error ROSE component */
int rose_return_error_decode(struct pri *pri, struct q931_call *call, q931_ie *ie, unsigned char *data, int len);

/* Decoder for the reject ROSE component */
int rose_reject_decode(struct pri *pri, struct q931_call *call, q931_ie *ie, unsigned char *data, int len);

int asn1_copy_string(char * buf, int buflen, struct rose_component *comp);

int asn1_string_encode(unsigned char asn1_type, void *data, int len, int max_len, void *src, int src_len);

/* Get Name types from ASN.1 */
int asn1_name_decode(void * data, int len, char *namebuf, int buflen);

int typeofnumber_from_q931(struct pri *pri, int ton);

int redirectingreason_from_q931(struct pri *pri, int redirectingreason);

/* Queues an MWI apdu on a the given call */
int mwi_message_send(struct pri *pri, q931_call *call, struct pri_sr *req, int activate);

/* starts a 2BCT */
int eect_initiate_transfer(struct pri *pri, q931_call *c1, q931_call *c2);

int rlt_initiate_transfer(struct pri *pri, q931_call *c1, q931_call *c2);

/* starts a QSIG Path Replacement */
extern int anfpr_initiate_transfer(struct pri *pri, q931_call *c1, q931_call *c2);

/* Use this function to queue a facility-IE born APDU onto a call
 * call is the call to use, messagetype is any one of the Q931 messages,
 * apdu is the apdu data, apdu_len is the length of the apdu data  */
int pri_call_apdu_queue(q931_call *call, int messagetype, void *apdu, int apdu_len, void (*function)(void *data), void *data);

/* Used by q931.c to cleanup the apdu queue upon destruction of a call */
int pri_call_apdu_queue_cleanup(q931_call *call);

/* Adds the "standard" APDUs to a call */
int pri_call_add_standard_apdus(struct pri *pri, q931_call *call);

int asn1_dump(struct pri *pri, void *comp, int len);

#endif /* _PRI_FACILITY_H */
