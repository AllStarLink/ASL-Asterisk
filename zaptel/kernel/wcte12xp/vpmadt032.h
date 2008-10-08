/*
 * Digium, Inc.  Wildcard TE12xP T1/E1 card Driver
 *
 * Written by Michael Spiceland <mspiceland@digium.com>
 *
 * Adapted from the wctdm24xxp and wcte11xp drivers originally
 * written by Mark Spencer <markster@digium.com>
 *            Matthew Fredrickson <creslin@digium.com>
 *            William Meadows <wmeadows@digium.com>
 *
 * Copyright (C) 2007, Digium, Inc.
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

#ifndef _VPM150M_H
#define _VPM150M_H

#include "wcte12xp.h"
#include "adt_lec.h"

struct t1_firmware {
	const struct firmware *fw;
	unsigned int offset;
};

/* Host and DSP system dependent related definitions. */
#define MAX_DSP_CORES 128                 /* maximum number of DSP cores */
//#define MAX_CONFS 1                     /* maximum number of conferences */
//#define MAX_PKT_CHANNELS 8              /* maximum number of packet channels */
#define MAX_CHANNELS 32                  /* maximum number of channels */
#define MAX_WAIT_LOOPS 50               /* max number of wait delay loops */
#define DSP_IFBLK_ADDRESS 0x0100        /* DSP address of I/F block pointer */
#define DOWNLOAD_BLOCK_SIZE 512	        /* download block size (DSP words) */
//#define MAX_CIDPAYLOAD_BYTES 512        /* max size of a CID payload (octets) */
typedef unsigned short DSP_WORD;    /* 16 bit DSP word */
typedef unsigned int DSP_ADDRESS;  /* 32 bit DSP address */
typedef struct t1_firmware* GPAK_FILE_ID; /* G.PAK Download file identifier */

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

struct vpm150m_cmd {
	unsigned short address;
	unsigned short data[VPM150M_MAX_DATA];
	unsigned char ident;
	unsigned char datalen;
	unsigned int flags;
	unsigned char cs_slot;
};

struct vpm150m {
	unsigned short dspid;
	unsigned long control;
	unsigned char curpage;
	unsigned short version;
	struct vpm150m_cmd cmdq[VPM150M_MAX_COMMANDS];
	spinlock_t lock; /* control access to list of bottom half tasks */
	struct semaphore sem;
	struct workqueue_struct *wq;
	struct work_struct work_dtmf;
	struct work_struct work_debug;
	struct work_struct work_echocan;
	struct list_head worklist;
	unsigned char curtone[32];
	unsigned long curdtmfmutestate;
	unsigned long desireddtmfmutestate;
	struct adt_lec_params chan_params[32];
	struct t1 *wc;
};

/* linked list for vpm echocan workqueue*/
struct vpm150m_workentry {
	struct list_head list;
	struct t1 *wc; /* what card are we dealing with? */
	struct zt_chan *chan; /* what channels are we going to deal with? */
	struct adt_lec_params params; /* how should we behave? */
};

extern int debug;
extern int vpmsupport;
extern int vpmdtmfsupport;
extern struct pci_driver te12xp_driver;

void t1_vpm150m_init(struct t1 *wc);
void vpm150m_cmd_dequeue(struct t1 *wc, volatile unsigned char *writechunk, int whichframe);
void vpm150m_cmd_decipher(struct t1 *wc, volatile unsigned char *readchunk);
int vpm150m_config_hw(struct t1 *wc);

/* gpak API functions */
void gpakReadDspMemory(
    unsigned short int DspId,   /* DSP Identifier (0 to MAX_DSP_CORES-1) */
    DSP_ADDRESS DspAddress,     /* DSP's memory address of first word */
    unsigned int NumWords,      /* number of contiguous words to read */
    DSP_WORD *pWordValues       /* pointer to array of word values variable */
    );
void gpakWriteDspMemory(
    unsigned short int DspId,   /* DSP Identifier (0 to MAX_DSP_CORES-1) */
    DSP_ADDRESS DspAddress,     /* DSP's memory address of first word */
    unsigned int NumWords,      /* number of contiguous words to write */
    DSP_WORD *pWordValues       /* pointer to array of word values to write */
    );
void gpakHostDelay(void);
void gpakLockAccess(
    unsigned short int DspId      /* DSP Identifier (0 to MAX_DSP_CORES-1) */
    );
void gpakUnlockAccess(
    unsigned short int DspId       /* DSP Identifier (0 to MAX_DSP_CORES-1) */
    );
int gpakReadFile(
    GPAK_FILE_ID FileId,        /* G.PAK Download File Identifier */
    unsigned char *pBuffer,	    /* pointer to buffer for storing bytes */
    unsigned int NumBytes       /* number of bytes to read */
    );

#endif
