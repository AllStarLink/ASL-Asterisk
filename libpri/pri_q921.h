/*
 * libpri: An implementation of Primary Rate ISDN
 *
 * Written by Mark Spencer <markster@digium.com>
 *
 * Copyright (C) 2001, Digium, Inc.
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
 
#ifndef _PRI_Q921_H
#define _PRI_Q921_H

#include <sys/types.h>
#if defined(__linux__)
#include <endian.h>
#elif defined(__FreeBSD__)
#include <sys/endian.h>
#define __BYTE_ORDER _BYTE_ORDER
#define __BIG_ENDIAN _BIG_ENDIAN
#define __LITTLE_ENDIAN _LITTLE_ENDIAN
#endif

/* Timer values */

#define T_WAIT_MIN	2000
#define T_WAIT_MAX	10000

#define Q921_FRAMETYPE_MASK	0x3

#define Q921_FRAMETYPE_U	0x3
#define Q921_FRAMETYPE_I	0x0
#define Q921_FRAMETYPE_S	0x1

#define Q921_TEI_GROUP					127
#define Q921_TEI_PRI					0
#define Q921_TEI_GR303_EOC_PATH			0
#define Q921_TEI_GR303_EOC_OPS			4
#define Q921_TEI_GR303_TMC_SWITCHING	0
#define Q921_TEI_GR303_TMC_CALLPROC		0

#define Q921_SAPI_CALL_CTRL		0
#define Q921_SAPI_GR303_EOC		1
#define Q921_SAPI_GR303_TMC_SWITCHING	1
#define Q921_SAPI_GR303_TMC_CALLPROC	0


#define Q921_SAPI_PACKET_MODE		1
#define Q921_SAPI_X25_LAYER3      	16
#define Q921_SAPI_LAYER2_MANAGEMENT	63


#define Q921_TEI_IDENTITY_REQUEST			1
#define Q921_TEI_IDENTITY_ASSIGNED			2
#define Q921_TEI_IDENTITY_DENIED			3
#define Q921_TEI_IDENTITY_CHECK_REQUEST		4
#define Q921_TEI_IDENTITY_CHECK_RESPONSE	5
#define Q921_TEI_IDENTITY_REMOVE			6
#define Q921_TEI_IDENTITY_VERIFY			7

typedef struct q921_header {
#if __BYTE_ORDER == __BIG_ENDIAN
	u_int8_t	sapi:6;	/* Service Access Point Indentifier (always 0 for PRI) (0) */
	u_int8_t	c_r:1;		/* Command/Response (0 if CPE, 1 if network) */
	u_int8_t	ea1:1;		/* Extended Address (0) */
	u_int8_t	tei:7;		/* Terminal Endpoint Identifier (0) */
	u_int8_t	ea2:1;		/* Extended Address Bit (1) */
#else	
	u_int8_t	ea1:1;		/* Extended Address (0) */
	u_int8_t	c_r:1;		/* Command/Response (0 if CPE, 1 if network) */
	u_int8_t	sapi:6;	/* Service Access Point Indentifier (always 0 for PRI) (0) */
	u_int8_t	ea2:1;		/* Extended Address Bit (1) */
	u_int8_t	tei:7;		/* Terminal Endpoint Identifier (0) */
#endif
	u_int8_t	data[0];	/* Further data */
} __attribute__ ((packed)) q921_header;

/* A Supervisory Format frame */
typedef struct q921_s {
	struct q921_header h;	/* Header */
#if __BYTE_ORDER == __BIG_ENDIAN
	u_int8_t x0:4;			/* Unused */
	u_int8_t ss:2;			/* Supervisory frame bits */
	u_int8_t ft:2;			/* Frame type bits (01) */
	u_int8_t n_r:7;			/* Number Received */
	u_int8_t p_f:1;			/* Poll/Final bit */
#else	
	u_int8_t ft:2;			/* Frame type bits (01) */
	u_int8_t ss:2;			/* Supervisory frame bits */
	u_int8_t x0:4;			/* Unused */
	u_int8_t p_f:1;			/* Poll/Final bit */
	u_int8_t n_r:7;			/* Number Received */
#endif
	u_int8_t data[0];		/* Any further data */
	u_int8_t fcs[2];		/* At least an FCS */
} __attribute__ ((packed)) q921_s;

/* An Unnumbered Format frame */
typedef struct q921_u {
	struct q921_header h;	/* Header */
#if __BYTE_ORDER == __BIG_ENDIAN
	u_int8_t m3:3;			/* Top 3 modifier bits */
	u_int8_t p_f:1;			/* Poll/Final bit */
	u_int8_t m2:2;			/* Two more modifier bits */
	u_int8_t ft:2;			/* Frame type bits (11) */
#else	
	u_int8_t ft:2;			/* Frame type bits (11) */
	u_int8_t m2:2;			/* Two more modifier bits */
	u_int8_t p_f:1;			/* Poll/Final bit */
	u_int8_t m3:3;			/* Top 3 modifier bits */
#endif
	u_int8_t data[0];		/* Any further data */
	u_int8_t fcs[2];		/* At least an FCS */
} __attribute__ ((packed)) q921_u;

/* An Information frame */
typedef struct q921_i {
	struct q921_header h;	/* Header */
#if __BYTE_ORDER == __BIG_ENDIAN
	u_int8_t n_s:7;			/* Number sent */
	u_int8_t ft:1;			/* Frame type (0) */
	u_int8_t n_r:7;			/* Number received */
	u_int8_t p_f:1;			/* Poll/Final bit */	
#else	
	u_int8_t ft:1;			/* Frame type (0) */
	u_int8_t n_s:7;			/* Number sent */
	u_int8_t p_f:1;			/* Poll/Final bit */	
	u_int8_t n_r:7;			/* Number received */
#endif
	u_int8_t data[0];		/* Any further data */
	u_int8_t fcs[2];		/* At least an FCS */
} q921_i;

typedef union {
	u_int8_t raw[0];
	q921_u u;
	q921_s s;
	q921_i i;
	struct q921_header h;
} q921_h;

typedef struct q921_frame {
	struct q921_frame *next;	/* Next in list */
	int len;					/* Length of header + body */
	int transmitted;			/* Have we been transmitted */
	q921_i h;
} q921_frame;

#define Q921_INC(j) (j) = (((j) + 1) % 128)

typedef enum q921_state {
	Q921_DOWN = 0,
	Q921_TEI_UNASSIGNED,
	Q921_TEI_AWAITING_ESTABLISH,
	Q921_TEI_AWAITING_ASSIGN,
	Q921_TEI_ASSIGNED,
	Q921_NEGOTIATION,
	Q921_LINK_CONNECTION_RELEASED,	/* Also known as TEI_ASSIGNED */
	Q921_LINK_CONNECTION_ESTABLISHED,
	Q921_AWAITING_ESTABLISH,
	Q921_AWAITING_RELEASE
} q921_state;

/* Dumps a *known good* Q.921 packet */
extern void q921_dump(struct pri *pri, q921_h *h, int len, int showraw, int txrx);

/* Bring up the D-channel */
extern void q921_start(struct pri *pri, int now);

extern void q921_reset(struct pri *pri);

extern pri_event *q921_receive(struct pri *pri, q921_h *h, int len);

extern int q921_transmit_iframe(struct pri *pri, void *buf, int len, int cr);

#endif
