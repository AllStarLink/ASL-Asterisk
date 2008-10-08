/*
 * Copyright (c) 2005, Adaptive Digital Technologies, Inc.
 *
 * File Name: GpakCust.h
 *
 * Description:
 *   This file contains host system dependent definitions and prototypes of
 *   functions to support generic G.PAK API functions. The file is used when
 *   integrating G.PAK API functions in a specific host processor environment.
 *
 *   Note: This file may need to be modified by the G.PAK system integrator.
 *
 * Version: 1.0
 *
 * Revision History:
 *   06/15/05 - Initial release.
 *
 * This program has been released under the terms of the GPL version 2 by
 * permission of Adaptive Digital Technologies, Inc.
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

#ifndef _GPAKCUST_H  /* prevent multiple inclusion */
#define _GPAKCUST_H

#include "wctdm24xxp.h"
#ifdef VPM150M_SUPPORT
#include <linux/device.h>
#include <linux/firmware.h>
#endif
#include "gpakenum.h"


struct wctdm_firmware {
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
typedef struct wctdm_firmware* GPAK_FILE_ID; /* G.PAK Download file identifier */

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * gpakReadDspMemory - Read DSP memory.
 *
 * FUNCTION
 *  This function reads a contiguous block of words from DSP memory starting at
 *  the specified address.
 *
 * RETURNS
 *  nothing
 *
 */
extern void gpakReadDspMemory(
    unsigned short int DspId,   /* DSP Identifier (0 to MAX_DSP_CORES-1) */
    DSP_ADDRESS DspAddress,     /* DSP's memory address of first word */
    unsigned int NumWords,      /* number of contiguous words to read */
    DSP_WORD *pWordValues       /* pointer to array of word values variable */
    );


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * gpakWriteDspMemory - Write DSP memory.
 *
 * FUNCTION
 *  This function writes a contiguous block of words to DSP memory starting at
 *  the specified address.
 *
 * RETURNS
 *  nothing
 *
 */
extern void gpakWriteDspMemory(
    unsigned short int DspId,   /* DSP Identifier (0 to MAX_DSP_CORES-1) */
    DSP_ADDRESS DspAddress,     /* DSP's memory address of first word */
    unsigned int NumWords,      /* number of contiguous words to write */
    DSP_WORD *pWordValues       /* pointer to array of word values to write */
    );


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * gpakHostDelay - Delay for a fixed time interval.
 *
 * FUNCTION
 *  This function delays for a fixed time interval before returning. The time
 *  interval is the Host Port Interface sampling period when polling a DSP for
 *  replies to command messages.
 *
 * RETURNS
 *  nothing
 *
 */
extern void gpakHostDelay(void);


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * gpakLockAccess - Lock access to the specified DSP.
 *
 * FUNCTION
 *  This function aquires exclusive access to the specified DSP.
 *
 * RETURNS
 *  nothing
 *
 */
extern void gpakLockAccess(
    unsigned short int DspId      /* DSP Identifier (0 to MAX_DSP_CORES-1) */
    );


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * gpakUnlockAccess - Unlock access to the specified DSP.
 *
 * FUNCTION
 *  This function releases exclusive access to the specified DSP.
 *
 * RETURNS
 *  nothing
 *
 */
extern void gpakUnlockAccess(
    unsigned short int DspId       /* DSP Identifier (0 to MAX_DSP_CORES-1) */
    );


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * gpakReadFile - Read a block of bytes from a G.PAK Download file.
 *
 * FUNCTION
 *  This function reads a contiguous block of bytes from a G.PAK Download file
 *  starting at the current file position.
 *
 * RETURNS
 *  The number of bytes read from the file.
 *   -1 indicates an error occurred.
 *    0 indicates all bytes have been read (end of file)
 *
 */
extern int gpakReadFile(
    GPAK_FILE_ID FileId,        /* G.PAK Download File Identifier */
    unsigned char *pBuffer,	    /* pointer to buffer for storing bytes */
    unsigned int NumBytes       /* number of bytes to read */
    );


unsigned char wctdm_vpm150m_getpage(struct wctdm *wc);

int wctdm_vpm150m_setpage(struct wctdm *wc, unsigned short addr);

int wctdm_vpm150m_setreg(struct wctdm *wc, unsigned int len, unsigned int addr, unsigned short *data);

unsigned short wctdm_vpm150m_getreg(struct wctdm *wc, unsigned int len, unsigned int addr, unsigned short *data);

char vpm150mtone_to_zaptone(GpakToneCodes_t tone);

#endif  /* prevent multiple inclusion */


