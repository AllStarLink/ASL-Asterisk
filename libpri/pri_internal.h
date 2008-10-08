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
 
#ifndef _PRI_INTERNAL_H
#define _PRI_INTERNAL_H

#include <stddef.h>
#include <sys/time.h>

#define DBGHEAD __FILE__ ":%d %s: "
#define DBGINFO __LINE__,__PRETTY_FUNCTION__

struct pri_sched {
	struct timeval when;
	void (*callback)(void *data);
	void *data;
};

struct q921_frame;
enum q931_state;
enum q931_mode;

/* No more than 128 scheduled events */
#define MAX_SCHED 128

#define MAX_TIMERS 32

struct pri {
	int fd;				/* File descriptor for D-Channel */
	pri_io_cb read_func;		/* Read data callback */
	pri_io_cb write_func;		/* Write data callback */
	void *userdata;
	struct pri *subchannel;	/* Sub-channel if appropriate */
	struct pri *master;		/* Master channel if appropriate */
	struct pri_sched pri_sched[MAX_SCHED];	/* Scheduled events */
	int debug;			/* Debug stuff */
	int state;			/* State of D-channel */
	int switchtype;		/* Switch type */
	int nsf;		/* Network-Specific Facility (if any) */
	int localtype;		/* Local network type (unknown, network, cpe) */
	int remotetype;		/* Remote network type (unknown, network, cpe) */

	int sapi;
	int tei;
	int protodisc;
	unsigned int bri:1;
	unsigned int acceptinbanddisconnect:1;	/* Should we allow inband progress after DISCONNECT? */
	
	/* Q.921 State */
	int q921_state;	
	int window;			/* Max window size */
	int windowlen;		/* Fullness of window */
	int v_s;			/* Next N(S) for transmission */
	int v_a;			/* Last acknowledged frame */
	int v_r;			/* Next frame expected to be received */
	int v_na;			/* What we've told our peer we've acknowledged */
	int solicitfbit;	/* Have we sent an I or S frame with the F-bit set? */
	int retrans;		/* Retransmissions */
	int sentrej;		/* Are we in reject state */
	
	int cref;			/* Next call reference value */
	
	int busy;			/* Peer is busy */

	/* Various timers */
	int sabme_timer;	/* SABME retransmit */
	int sabme_count;	/* SABME retransmit counter for BRI */
	int t203_timer;		/* Max idle time */
	int t202_timer;
	int n202_counter;
	int ri;
	int t200_timer;		/* T-200 retransmission timer */
	/* All ISDN Timer values */
	int timers[MAX_TIMERS];

	/* Used by scheduler */
	struct timeval tv;
	int schedev;
	pri_event ev;		/* Static event thingy */
	
	/* Q.921 Re-transmission queue */
	struct q921_frame *txqueue;
	
	/* Q.931 calls */
	q931_call **callpool;
	q931_call *localpool;

	/* do we do overlap dialing */
	int overlapdial;

#ifdef LIBPRI_COUNTERS
	/* q921/q931 packet counters */
	unsigned int q921_txcount;
	unsigned int q921_rxcount;
	unsigned int q931_txcount;
	unsigned int q931_rxcount;
#endif

	unsigned char last_invoke;	/* Last ROSE invoke ID */
	unsigned char sendfacility;
};

struct pri_sr {
	int transmode;
	int channel;
	int exclusive;
	int nonisdn;
	char *caller;
	int callerplan;
	char *callername;
	int callerpres;
	char *called;
	int calledplan;
	int userl1;
	int numcomplete;
	char *redirectingnum;
	int redirectingplan;
	int redirectingpres;
	int redirectingreason;
	int justsignalling;
	const char *useruserinfo;
	int transferable;
};

/* Internal switch types */
#define PRI_SWITCH_GR303_EOC_PATH	19
#define PRI_SWITCH_GR303_TMC_SWITCHING	20

struct apdu_event {
	int message;			/* What message to send the ADPU in */
	void (*callback)(void *data);	/* Callback function for when response is received */
	void *data;			/* Data to callback */
	unsigned char apdu[255];			/* ADPU to send */
	int apdu_len; 			/* Length of ADPU */
	int sent;  			/* Have we been sent already? */
	struct apdu_event *next;	/* Linked list pointer */
};

/* q931_call datastructure */

struct q931_call {
	struct pri *pri;	/* PRI */
	int cr;				/* Call Reference */
	int forceinvert;	/* Force inversion of call number even if 0 */
	q931_call *next;
	/* Slotmap specified (bitmap of channels 31/24-1) (Channel Identifier IE) (-1 means not specified) */
	int slotmap;
	/* An explicit channel (Channel Identifier IE) (-1 means not specified) */
	int channelno;
	/* An explicit DS1 (-1 means not specified) */
	int ds1no;
	/* Whether or not the ds1 is explicitly identified or implicit.  If implicit
	   the bchan is on the same span as the current active dchan (NFAS) */
	int ds1explicit;
	/* Channel flags (0 means none retrieved) */
	int chanflags;
	
	int alive;			/* Whether or not the call is alive */
	int acked;			/* Whether setup has been acked or not */
	int sendhangupack;	/* Whether or not to send a hangup ack */
	int proc;			/* Whether we've sent a call proceeding / alerting */
	
	int ri;				/* Restart Indicator (Restart Indicator IE) */

	/* Bearer Capability */
	int transcapability;
	int transmoderate;
	int transmultiple;
	int userl1;
	int userl2;
	int userl3;
	int rateadaption;
	
	int sentchannel;
	int justsignalling;		/* for a signalling-only connection */

	int progcode;			/* Progress coding */
	int progloc;			/* Progress Location */	
	int progress;			/* Progress indicator */
	int progressmask;		/* Progress Indicator bitmask */
	
	int notify;				/* Notification */
	
	int causecode;			/* Cause Coding */
	int causeloc;			/* Cause Location */
	int cause;				/* Cause of clearing */
	
	int peercallstate;		/* Call state of peer as reported */
	int ourcallstate;		/* Our call state */
	int sugcallstate;		/* Status call state */
	
	int callerplan;
	int callerplanani;
	int callerpres;			/* Caller presentation */
	char callerani[256];	/* Caller */
	char callernum[256];
	char callername[256];

	char keypad_digits[64];		/* Buffer for digits that come in KEYPAD_FACILITY */

	int ani2;               /* ANI II */
	
	int calledplan;
	int nonisdn;
	char callednum[256];	/* Called Number */
	int complete;			/* no more digits coming */
	int newcall;			/* if the received message has a new call reference value */

	int retranstimer;		/* Timer for retransmitting DISC */
	int t308_timedout;		/* Whether t308 timed out once */

	int redirectingplan;
	int redirectingpres;
	int redirectingreason;	      
	char redirectingnum[256];	/* Number of redirecting party */
	char redirectingname[256];	/* Name of redirecting party */

	/* Filled in cases of multiple diversions */
	int origcalledplan;
	int origcalledpres;
	int origredirectingreason;	/* Original reason for redirect (in cases of multiple redirects) */
	char origcalledname[256];	/* Original name of person being called */
	char origcallednum[256];	/* Orignal number of person being called */

	int useruserprotocoldisc;
	char useruserinfo[256];
	char callingsubaddr[256];	/* Calling parties sub address */
	
	long aoc_units;				/* Advice of Charge Units */

	struct apdu_event *apdus;	/* APDU queue for call */

	int transferable;
	unsigned int rlt_call_id;	/* RLT call id */

	/* Bridged call info */
	q931_call *bridged_call;        /* Pointer to other leg of bridged call */
};

extern int pri_schedule_event(struct pri *pri, int ms, void (*function)(void *data), void *data);

extern pri_event *pri_schedule_run(struct pri *pri);

extern void pri_schedule_del(struct pri *pri, int ev);

extern pri_event *pri_mkerror(struct pri *pri, char *errstr);

extern void pri_message(struct pri *pri, char *fmt, ...);

extern void pri_error(struct pri *pri, char *fmt, ...);

void libpri_copy_string(char *dst, const char *src, size_t size);

struct pri *__pri_new_tei(int fd, int node, int switchtype, struct pri *master, pri_io_cb rd, pri_io_cb wr, void *userdata, int tei, int bri);

void __pri_free_tei(struct pri *p);

#endif
