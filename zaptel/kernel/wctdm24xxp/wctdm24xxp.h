/*
 * Wildcard TDM2400P TDM FXS/FXO Interface Driver for Zapata Telephony interface
 *
 * Written by Mark Spencer <markster@digium.com>
 * Support for TDM800P and VPM150M by Matthew Fredrickson <creslin@digium.com>
 *
 * Copyright (C) 2005, 2006, Digium, Inc.
 *
 * All rights reserved.
 *
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
 */

#ifndef _WCTDM24XXP_H
#define _WCTDM24XXP_H

#include "../zaptel.h"
#include "../voicebus.h"
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
#include <linux/semaphore.h>
#else
#include <asm/semaphore.h>
#endif

#define NUM_FXO_REGS 60

#define WC_MAX_IFACES 128

/*!
 * \brief Default ringer debounce (in ms)
 */
#define DEFAULT_RING_DEBOUNCE	1024

#define POLARITY_DEBOUNCE	64		/* Polarity debounce (in ms) */

#define OHT_TIMER		6000	/* How long after RING to retain OHT */

#define FLAG_3215	(1 << 0)
#define FLAG_EXPRESS	(1 << 1)

#define EFRAME_SIZE	108
#define ERING_SIZE 16		/* Maximum ring size */
#define EFRAME_GAP 20
#define SFRAME_SIZE ((EFRAME_SIZE * ZT_CHUNKSIZE) + (EFRAME_GAP * (ZT_CHUNKSIZE - 1)))

#define MAX_ALARMS 10

#define MOD_TYPE_NONE		0
#define MOD_TYPE_FXS		1
#define MOD_TYPE_FXO		2
#define MOD_TYPE_FXSINIT	3	
#define MOD_TYPE_VPM		4
#define MOD_TYPE_QRV		5
#define MOD_TYPE_VPM150M	6

#define MINPEGTIME	10 * 8		/* 30 ms peak to peak gets us no more than 100 Hz */
#define PEGTIME		50 * 8		/* 50ms peak to peak gets us rings of 10 Hz or more */
#define PEGCOUNT	5		/* 5 cycles of pegging means RING */

#define SDI_CLK		(0x00010000)
#define SDI_DOUT	(0x00020000)
#define SDI_DREAD	(0x00040000)
#define SDI_DIN		(0x00080000)

#define PCI_WINDOW_SIZE ((2 * 2 * 2 * SFRAME_SIZE) + (2 * ERING_SIZE * 4))

#define __CMD_RD   (1 << 20)		/* Read Operation */
#define __CMD_WR   (1 << 21)		/* Write Operation */
#define __CMD_FIN  (1 << 22)		/* Has finished receive */
#define __CMD_TX   (1 << 23)		/* Has been transmitted */

#define CMD_WR(a,b) (((a) << 8) | (b) | __CMD_WR)
#define CMD_RD(a) (((a) << 8) | __CMD_RD)

#if 0
#define CMD_BYTE(card,bit,altcs) (((((card) & 0x3) * 3 + (bit)) * 7) \
			+ ((card) >> 2) + (altcs) + ((altcs) ? -21 : 0))
#endif
#define NUM_CARDS 24
#define NUM_EC	  4
#define NUM_SLOTS 6
#define MAX_TDM_CHAN 31

#define NUM_CAL_REGS 12

#define USER_COMMANDS 8
#define ISR_COMMANDS  2
#define	QRV_DEBOUNCETIME 20

#define MAX_COMMANDS (USER_COMMANDS + ISR_COMMANDS)

#define __VPM150M_RWPAGE	(1 << 4)
#define __VPM150M_RD		(1 << 3)
#define __VPM150M_WR		(1 << 2)
#define __VPM150M_FIN		(1 << 1)
#define __VPM150M_TX		(1 << 0)

#define VPM150M_HPI_CONTROL 0x00
#define VPM150M_HPI_ADDRESS 0x02
#define VPM150M_HPI_DATA 0x03

#define VPM150M_MAX_COMMANDS 8

/* Some Bit ops for different operations */
#define VPM150M_SPIRESET		0
#define VPM150M_HPIRESET		1
#define VPM150M_SWRESET			2
#define VPM150M_DTMFDETECT		3
#define VPM150M_ACTIVE			4

#define VPM150M_MAX_DATA		1

#define VPM_SUPPORT

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#define VPM150M_SUPPORT
#endif

#ifdef VPM_SUPPORT

/* Define to get more attention-grabbing but slightly more CPU using echocan status */
#define FANCY_ECHOCAN

#endif

#ifdef VPM150M_SUPPORT
#include "adt_lec.h"
#endif

struct vpm150m_cmd {
	unsigned int addr;
	unsigned char datalen;
	unsigned char desc;
	unsigned char txident;
	unsigned short data[VPM150M_MAX_DATA];
};

struct vpm150m {
#ifdef VPM150M_SUPPORT
	struct workqueue_struct *wq;
	struct work_struct work;
#endif
	struct wctdm *wc;

	int dspid;
	struct semaphore sem;
	unsigned long control;
	unsigned char curpage;
	unsigned short version;
	struct adt_lec_params curecstate[24];
	struct adt_lec_params desiredecstate[24];
	unsigned long curdtmfmutestate;
	unsigned long desireddtmfmutestate;
	struct vpm150m_cmd cmdq[VPM150M_MAX_COMMANDS];
	unsigned char curtone[24];
};

struct calregs {
	unsigned char vals[NUM_CAL_REGS];
};

struct cmdq {
	unsigned int cmds[MAX_COMMANDS];
	unsigned char isrshadow[ISR_COMMANDS];
};

enum battery_state {
	BATTERY_UNKNOWN = 0,
	BATTERY_PRESENT,
	BATTERY_LOST,
};

struct wctdm {
	char *variety;
	char board_name[80];
	struct zt_span span;
	unsigned char ios;
	unsigned int sdi;
	int usecount;
	unsigned int intcount;
	unsigned int rxints;
	unsigned int txints;
	unsigned char txident;
	unsigned char rxident;
	int dead;
	int pos;
	int flags[NUM_CARDS];
	int alt;
	int curcard;
	unsigned char ctlreg;
	int cards;
	int cardflag;		/* Bit-map of present cards */
 	int altcs[NUM_CARDS + NUM_EC];
	char qrvhook[NUM_CARDS];
	unsigned short qrvdebtime[NUM_CARDS];
	int radmode[NUM_CARDS];
#define	RADMODE_INVERTCOR 1
#define	RADMODE_IGNORECOR 2
#define	RADMODE_EXTTONE 4
#define	RADMODE_EXTINVERT 8
#define	RADMODE_IGNORECT 16
#define	RADMODE_PREEMP	32
#define	RADMODE_DEEMP 64
	unsigned short debouncetime[NUM_CARDS];
	signed short rxgain[NUM_CARDS];
	signed short txgain[NUM_CARDS];
	spinlock_t reglock;
	wait_queue_head_t regq;
	/* FXO Stuff */
	union {
		struct fxo {
			int wasringing;
			int lastrdtx;
			int lastrdtx_count;
			int ringdebounce;
			int offhook;
			int battdebounce;
			int battalarm;
			enum battery_state battery;
			int lastpol;
			int polarity;
			int polaritydebounce;
			int neonmwi_state;
			int neonmwi_last_voltage;
			unsigned int neonmwi_debounce;
			unsigned int neonmwi_offcounter;
		} fxo;
		struct fxs {
			int oldrxhook;
			int debouncehook;
			int lastrxhook;
			int debounce;
			int ohttimer;
			int idletxhookstate;		/* IDLE changing hook state */
			int lasttxhook;
			int palarms;
			struct calregs calregs;
		} fxs;
	} mods[NUM_CARDS];
	struct cmdq cmdq[NUM_CARDS + NUM_EC];
	/* Receive hook state and debouncing */
	int modtype[NUM_CARDS + NUM_EC];
	/* Set hook */
	int sethook[NUM_CARDS + NUM_EC];
 	int dacssrc[NUM_CARDS];
 	int type;

#ifdef VPM_SUPPORT
	int vpm;
	unsigned long dtmfactive;
	unsigned long dtmfmask;
	unsigned long dtmfmutemask;
	short dtmfenergy[NUM_CARDS];
	short dtmfdigit[NUM_CARDS];

	struct  vpm150m *vpm150m;
#ifdef FANCY_ECHOCAN
	int echocanpos;
	int blinktimer;
#endif	
#endif
	struct voicebus *vb;
	struct zt_chan chans[NUM_CARDS];
	int initialized;
};


int schluffen(wait_queue_head_t *q);

extern spinlock_t ifacelock;
extern struct wctdm *ifaces[WC_MAX_IFACES];

#endif
