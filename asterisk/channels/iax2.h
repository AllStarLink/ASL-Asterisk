/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Implementation of Inter-Asterisk eXchange
 * 
 * Copyright (C) 2003, Digium
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */
 
#ifndef _IAX2_H
#define _IAX2_H

/* Max version of IAX protocol we support */
#define IAX_PROTO_VERSION 2

/* NOTE: IT IS CRITICAL THAT IAX_MAX_CALLS BE A POWER OF 2. */
#if defined(LOW_MEMORY)
#define IAX_MAX_CALLS 2048
#else
#define IAX_MAX_CALLS 32768
#endif

#define IAX_FLAG_FULL		0x8000

#define IAX_FLAG_RETRANS	0x8000

#define IAX_FLAG_SC_LOG		0x80

#define IAX_MAX_SHIFT		0x1F

#define IAX_WINDOW			64

/* Subclass for AST_FRAME_IAX */
#define IAX_COMMAND_NEW		1
#define IAX_COMMAND_PING	2
#define IAX_COMMAND_PONG	3
#define IAX_COMMAND_ACK		4
#define IAX_COMMAND_HANGUP	5
#define IAX_COMMAND_REJECT	6
#define IAX_COMMAND_ACCEPT	7
#define IAX_COMMAND_AUTHREQ	8
#define IAX_COMMAND_AUTHREP	9
#define IAX_COMMAND_INVAL	10
#define IAX_COMMAND_LAGRQ	11
#define IAX_COMMAND_LAGRP	12
#define IAX_COMMAND_REGREQ	13	/* Registration request */
#define IAX_COMMAND_REGAUTH	14	/* Registration authentication required */
#define IAX_COMMAND_REGACK	15	/* Registration accepted */
#define IAX_COMMAND_REGREJ	16	/* Registration rejected */
#define IAX_COMMAND_REGREL	17	/* Force release of registration */
#define IAX_COMMAND_VNAK	18	/* If we receive voice before valid first voice frame, send this */
#define IAX_COMMAND_DPREQ	19	/* Request status of a dialplan entry */
#define IAX_COMMAND_DPREP	20	/* Request status of a dialplan entry */
#define IAX_COMMAND_DIAL	21	/* Request a dial on channel brought up TBD */
#define IAX_COMMAND_TXREQ	22	/* Transfer Request */
#define IAX_COMMAND_TXCNT	23	/* Transfer Connect */
#define IAX_COMMAND_TXACC	24	/* Transfer Accepted */
#define IAX_COMMAND_TXREADY	25	/* Transfer ready */
#define IAX_COMMAND_TXREL	26	/* Transfer release */
#define IAX_COMMAND_TXREJ	27	/* Transfer reject */
#define IAX_COMMAND_QUELCH	28	/* Stop audio/video transmission */
#define IAX_COMMAND_UNQUELCH 29	/* Resume audio/video transmission */
#define IAX_COMMAND_POKE    30  /* Like ping, but does not require an open connection */
#define IAX_COMMAND_PAGE	31	/* Paging description */
#define IAX_COMMAND_MWI	32	/* Stand-alone message waiting indicator */
#define IAX_COMMAND_UNSUPPORT	33	/* Unsupported message received */
#define IAX_COMMAND_TRANSFER	34	/* Request remote transfer */
#define IAX_COMMAND_PROVISION	35	/* Provision device */
#define IAX_COMMAND_FWDOWNL	36	/* Download firmware */
#define IAX_COMMAND_FWDATA	37	/* Firmware Data */
#define IAX_COMMAND_TXMEDIA 38  /* Transfer media only */
#define IAX_COMMAND_CALLTOKEN 40  /*! Call number token */


#define IAX_DEFAULT_REG_EXPIRE  60	/* By default require re-registration once per minute */

#define IAX_LINGER_TIMEOUT		10 /* How long to wait before closing bridged call */

#define IAX_DEFAULT_PORTNO		4569

/* IAX Information elements */
#define IAX_IE_CALLED_NUMBER		1		/* Number/extension being called - string */
#define IAX_IE_CALLING_NUMBER		2		/* Calling number - string */
#define IAX_IE_CALLING_ANI			3		/* Calling number ANI for billing  - string */
#define IAX_IE_CALLING_NAME			4		/* Name of caller - string */
#define IAX_IE_CALLED_CONTEXT		5		/* Context for number - string */
#define IAX_IE_USERNAME				6		/* Username (peer or user) for authentication - string */
#define IAX_IE_PASSWORD				7		/* Password for authentication - string */
#define IAX_IE_CAPABILITY			8		/* Actual codec capability - unsigned int */
#define IAX_IE_FORMAT				9		/* Desired codec format - unsigned int */
#define IAX_IE_LANGUAGE				10		/* Desired language - string */
#define IAX_IE_VERSION				11		/* Protocol version - short */
#define IAX_IE_ADSICPE				12		/* CPE ADSI capability - short */
#define IAX_IE_DNID					13		/* Originally dialed DNID - string */
#define IAX_IE_AUTHMETHODS			14		/* Authentication method(s) - short */
#define IAX_IE_CHALLENGE			15		/* Challenge data for MD5/RSA - string */
#define IAX_IE_MD5_RESULT			16		/* MD5 challenge result - string */
#define IAX_IE_RSA_RESULT			17		/* RSA challenge result - string */
#define IAX_IE_APPARENT_ADDR		18		/* Apparent address of peer - struct sockaddr_in */
#define IAX_IE_REFRESH				19		/* When to refresh registration - short */
#define IAX_IE_DPSTATUS				20		/* Dialplan status - short */
#define IAX_IE_CALLNO				21		/* Call number of peer - short */
#define IAX_IE_CAUSE				22		/* Cause - string */
#define IAX_IE_IAX_UNKNOWN			23		/* Unknown IAX command - byte */
#define IAX_IE_MSGCOUNT				24		/* How many messages waiting - short */
#define IAX_IE_AUTOANSWER			25		/* Request auto-answering -- none */
#define IAX_IE_MUSICONHOLD			26		/* Request musiconhold with QUELCH -- none or string */
#define IAX_IE_TRANSFERID			27		/* Transfer Request Identifier -- int */
#define IAX_IE_RDNIS				28		/* Referring DNIS -- string */
#define IAX_IE_PROVISIONING			29		/* Provisioning info */
#define IAX_IE_AESPROVISIONING		30		/* AES Provisioning info */
#define IAX_IE_DATETIME				31		/* Date/Time */
#define IAX_IE_DEVICETYPE			32		/* Device Type -- string */
#define IAX_IE_SERVICEIDENT			33		/* Service Identifier -- string */
#define IAX_IE_FIRMWAREVER			34		/* Firmware revision -- u16 */
#define IAX_IE_FWBLOCKDESC			35		/* Firmware block description -- u32 */
#define IAX_IE_FWBLOCKDATA			36		/* Firmware block of data -- raw */
#define IAX_IE_PROVVER				37		/* Provisioning Version (u32) */
#define IAX_IE_CALLINGPRES			38		/* Calling presentation (u8) */
#define IAX_IE_CALLINGTON			39		/* Calling type of number (u8) */
#define IAX_IE_CALLINGTNS			40		/* Calling transit network select (u16) */
#define IAX_IE_SAMPLINGRATE			41		/* Supported sampling rates (u16) */
#define IAX_IE_CAUSECODE			42		/* Hangup cause (u8) */
#define IAX_IE_ENCRYPTION			43		/* Encryption format (u16) */
#define IAX_IE_ENCKEY				44		/* Encryption key (raw) */
#define IAX_IE_CODEC_PREFS          45      /* Codec Negotiation */

#define IAX_IE_RR_JITTER			46		/* Received jitter (as in RFC1889) u32 */
#define IAX_IE_RR_LOSS				47		/* Received loss (high byte loss pct, low 24 bits loss count, as in rfc1889 */
#define IAX_IE_RR_PKTS				48		/* Received frames (total frames received) u32 */
#define IAX_IE_RR_DELAY				49		/* Max playout delay for received frames (in ms) u16 */
#define IAX_IE_RR_DROPPED			50		/* Dropped frames (presumably by jitterbuf) u32 */
#define IAX_IE_RR_OOO				51		/* Frames received Out of Order u32 */
#define IAX_IE_CALLTOKEN			54		/* Call number security token */


#define IAX_AUTH_PLAINTEXT			(1 << 0)
#define IAX_AUTH_MD5				(1 << 1)
#define IAX_AUTH_RSA				(1 << 2)

#define IAX_ENCRYPT_AES128			(1 << 0)

#define IAX_META_TRUNK				1		/* Trunk meta-message */
#define IAX_META_VIDEO				2		/* Video frame */

#define IAX_META_TRUNK_SUPERMINI		0	/* This trunk frame contains classic supermini frames */
#define IAX_META_TRUNK_MINI			1	/* This trunk frame contains trunked mini frames */

#define IAX_RATE_8KHZ				(1 << 0) /* 8khz sampling (default if absent) */
#define IAX_RATE_11KHZ				(1 << 1) /* 11.025khz sampling */
#define IAX_RATE_16KHZ				(1 << 2) /* 16khz sampling */
#define IAX_RATE_22KHZ				(1 << 3) /* 22.05khz sampling */
#define IAX_RATE_44KHZ				(1 << 4) /* 44.1khz sampling */
#define IAX_RATE_48KHZ				(1 << 5) /* 48khz sampling */

#define IAX_DPSTATUS_EXISTS			(1 << 0)
#define IAX_DPSTATUS_CANEXIST		(1 << 1)
#define IAX_DPSTATUS_NONEXISTENT	(1 << 2)
#define IAX_DPSTATUS_IGNOREPAT		(1 << 14)
#define IAX_DPSTATUS_MATCHMORE		(1 << 15)

/* Full frames are always delivered reliably */
struct ast_iax2_full_hdr {
	unsigned short scallno;	/* Source call number -- high bit must be 1 */
	unsigned short dcallno;	/* Destination call number -- high bit is 1 if retransmission */
	unsigned int ts;		/* 32-bit timestamp in milliseconds (from 1st transmission) */
	unsigned char oseqno;	/* Packet number (outgoing) */
	unsigned char iseqno;	/* Packet number (next incoming expected) */
	unsigned char type;		/* Frame type */
	unsigned char csub;		/* Compressed subclass */
	unsigned char iedata[0];
} __attribute__ ((__packed__));

/* Full frames are always delivered reliably */
struct ast_iax2_full_enc_hdr {
	unsigned short scallno;	/* Source call number -- high bit must be 1 */
	unsigned short dcallno;	/* Destination call number -- high bit is 1 if retransmission */
	unsigned char encdata[0];
} __attribute__ ((__packed__));

/* Mini header is used only for voice frames -- delivered unreliably */
struct ast_iax2_mini_hdr {
	unsigned short callno;	/* Source call number -- high bit must be 0, rest must be non-zero */
	unsigned short ts;		/* 16-bit Timestamp (high 16 bits from last ast_iax2_full_hdr) */
							/* Frametype implicitly VOICE_FRAME */
							/* subclass implicit from last ast_iax2_full_hdr */
	unsigned char data[0];
} __attribute__ ((__packed__));

/* Mini header is used only for voice frames -- delivered unreliably */
struct ast_iax2_mini_enc_hdr {
	unsigned short callno;	/* Source call number -- high bit must be 0, rest must be non-zero */
	unsigned char encdata[0];
} __attribute__ ((__packed__));

struct ast_iax2_meta_hdr {
	unsigned short zeros;			/* Zeros field -- must be zero */
	unsigned char metacmd;			/* Meta command */
	unsigned char cmddata;			/* Command Data */
	unsigned char data[0];
} __attribute__ ((__packed__));

struct ast_iax2_video_hdr {
	unsigned short zeros;			/* Zeros field -- must be zero */
	unsigned short callno;			/* Video call number */
	unsigned short ts;				/* Timestamp and mark if present */
	unsigned char data[0];
} __attribute__ ((__packed__));

struct ast_iax2_meta_trunk_hdr {
	unsigned int ts;				/* 32-bit timestamp for all messages */
	unsigned char data[0];
} __attribute__ ((__packed__));

struct ast_iax2_meta_trunk_entry {
	unsigned short callno;			/* Call number */
	unsigned short len;				/* Length of data for this callno */
} __attribute__ ((__packed__));

/* When trunktimestamps are used, we use this format instead */
struct ast_iax2_meta_trunk_mini {
	unsigned short len;
	struct ast_iax2_mini_hdr mini;		/* this is an actual miniframe */
} __attribute__ ((__packed__));

#define IAX_FIRMWARE_MAGIC 0x69617879

struct ast_iax2_firmware_header {
	unsigned int magic;		/* Magic number */
	unsigned short version;		/* Software version */
	unsigned char devname[16];	/* Device */
	unsigned int datalen;		/* Data length of file beyond header */
	unsigned char chksum[16];	/* Checksum of all data */
	unsigned char data[0];
} __attribute__ ((__packed__));
#endif
