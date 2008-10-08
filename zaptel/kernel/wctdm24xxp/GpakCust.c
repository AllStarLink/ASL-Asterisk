/*
 * Copyright (c) 2005, Adaptive Digital Technologies, Inc.
 *
 * File Name: GpakCust.c
 *
 * Description:
 *   This file contains host system dependent functions to support generic
 *   G.PAK API functions. The file is integrated into the host processor
 *   connected to C55x G.PAK DSPs via a Host Port Interface.
 *
 *   Note: This file needs to be modified by the G.PAK system integrator.
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

#include <linux/version.h>
#include <linux/delay.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
#include <linux/semaphore.h>
#else
#include <asm/semaphore.h>
#endif

#include "zaptel.h"
#include "wctdm24xxp.h"
#include "GpakCust.h"

char vpm150mtone_to_zaptone(GpakToneCodes_t tone)
{
	switch (tone) {
		case DtmfDigit0:
			return '0';
		case DtmfDigit1:
			return '1';
		case DtmfDigit2:
			return '2';
		case DtmfDigit3:
			return '3';
		case DtmfDigit4:
			return '4';
		case DtmfDigit5:
			return '5';
		case DtmfDigit6:
			return '6';
		case DtmfDigit7:
			return '7';
		case DtmfDigit8:
			return '8';
		case DtmfDigit9:
			return '9';
		case DtmfDigitPnd:
			return '#';
		case DtmfDigitSt:
			return '*';
		case DtmfDigitA:
			return 'A';
		case DtmfDigitB:
			return 'B';
		case DtmfDigitC:
			return 'C';
		case DtmfDigitD:
			return 'D';
		case EndofCngDigit:
			return 'f';
		default:
			return 0;
	}
}

static inline struct wctdm * wc_find_iface(unsigned short dspid)
{
	int i;
	struct wctdm *ret = NULL;
	unsigned long flags;

	spin_lock_irqsave(&ifacelock, flags);
	for (i = 0; i < WC_MAX_IFACES; i++)
		if (ifaces[i] && ifaces[i]->vpm150m && (ifaces[i]->vpm150m->dspid == dspid))
			ret = ifaces[i];
	spin_unlock_irqrestore(&ifacelock, flags);

	return ret;
}

static inline struct vpm150m_cmd * vpm150m_empty_slot(struct wctdm *wc)
{
	int x;

	for (x = 0; x < VPM150M_MAX_COMMANDS; x++)
		if (!wc->vpm150m->cmdq[x].desc) {
			return &wc->vpm150m->cmdq[x];
		}
	return NULL;
}

/* Wait for any outstanding commands to be completed. */
static inline int vpm150m_io_wait(struct wctdm *wc)
{
	int x;
	int ret=0;
	for (x=0; x < VPM150M_MAX_COMMANDS;) {
		if (wc->vpm150m->cmdq[x].desc) {
			if ((ret=schluffen(&wc->regq))) {
				return ret;
			}
			x=0;
		}
		else {
			++x;
		}
	}
	return ret;
}

int wctdm_vpm150m_getreg_full_async(struct wctdm *wc, int pagechange, unsigned int len, 
	unsigned short addr, unsigned short *outbuf, struct vpm150m_cmd **hit_p)
{
	int ret=0;
	unsigned long flags;
	BUG_ON(!hit_p);
	spin_lock_irqsave(&wc->reglock, flags);
	(*hit_p) = vpm150m_empty_slot(wc);
	if (*hit_p) {
		(*hit_p)->desc = __VPM150M_RD;
		if (pagechange) {
			(*hit_p)->desc |= __VPM150M_RWPAGE;
		}
		(*hit_p)->datalen = len;
		(*hit_p)->addr = addr;
		memset((*hit_p)->data, 0, len*sizeof(outbuf[0]));
	}
	else {
		ret = -EBUSY;
	}
	spin_unlock_irqrestore(&wc->reglock, flags);
	return ret;
}

int wctdm_vpm150m_getreg_full_return(struct wctdm *wc, int pagechange, unsigned int len,
	unsigned short addr, unsigned short *outbuf, struct vpm150m_cmd **hit_p)
{
	int ret = 0;
	unsigned long flags;
	BUG_ON(!hit_p);
	spin_lock_irqsave(&wc->reglock, flags);
	do {
		if ((*hit_p)->desc & __VPM150M_FIN) {
			memcpy(outbuf, (*hit_p)->data, len*(sizeof(outbuf[0])));
			(*hit_p)->desc = 0;
			(*hit_p) = NULL;
			ret = 0;
		}
		else {
			spin_unlock_irqrestore(&wc->reglock, flags);
			if ((ret=schluffen(&wc->regq))) {
				return ret;
			}
			spin_lock_irqsave(&wc->reglock, flags);
			ret = -EBUSY;
		}
	} while (-EBUSY == ret);
	spin_unlock_irqrestore(&wc->reglock, flags);
	return ret;
}

int wctdm_vpm150m_getreg_full(struct wctdm *wc, int pagechange, unsigned int len, unsigned short addr, unsigned short *outbuf)
{
	struct vpm150m_cmd *hit = 0;
	int ret = 0;
	do {
		ret = wctdm_vpm150m_getreg_full_async(wc, pagechange, len, addr, outbuf, &hit);
		if (!hit) {
			if ( -EBUSY == ret ) {
				if ((ret = schluffen(&wc->regq))) 
					return ret;
			}
			BUG_ON(0 != ret);
		}
	} while (!hit);
	ret = wctdm_vpm150m_getreg_full_return(wc, pagechange, len, addr, outbuf, &hit);
	return ret;
}

int wctdm_vpm150m_setreg_full(struct wctdm *wc, int pagechange, unsigned int len, unsigned int addr, unsigned short *data)
{
	unsigned long flags;
	struct vpm150m_cmd *hit;
	int ret, i;
	do {
		spin_lock_irqsave(&wc->reglock, flags);
		hit = vpm150m_empty_slot(wc);
		if (hit) {
			hit->desc = __VPM150M_WR;
			if (pagechange)
				hit->desc |= __VPM150M_RWPAGE;
			hit->addr = addr;
			hit->datalen = len;
			for (i = 0; i < len; i++)
				hit->data[i] = data[i];
		}
		spin_unlock_irqrestore(&wc->reglock, flags);
		if (!hit) {
			if ((ret = schluffen(&wc->regq)))
				return ret;
		}
	} while (!hit);
	return (hit) ? 0 : -1;
}

int wctdm_vpm150m_setpage(struct wctdm *wc, unsigned short addr)
{
	addr &= 0xf;
	/* Let's optimize this a little bit */
	if (wc->vpm150m->curpage == addr)
		return 0;
	else {
		wc->vpm150m->curpage = addr;
	}

	return wctdm_vpm150m_setreg_full(wc, 1, 1, 0, &addr);
}

unsigned char wctdm_vpm150m_getpage(struct wctdm *wc)
{
	unsigned short res;
	wctdm_vpm150m_getreg_full(wc, 1, 1, 0, &res);
	return res;
}

unsigned short wctdm_vpm150m_getreg(struct wctdm *wc, unsigned int len, unsigned int addr, unsigned short *data)
{
	unsigned short res;
	wctdm_vpm150m_setpage(wc, addr >> 16);
	if ((addr >> 16) != ((addr + len) >> 16))
		printk("getreg: You found it!\n");
	res = wctdm_vpm150m_getreg_full(wc, 0, len, addr & 0xffff, data);
 	return res;
}

int wctdm_vpm150m_setreg(struct wctdm *wc, unsigned int len, unsigned int addr, unsigned short *data)
{
	int res;
	wctdm_vpm150m_setpage(wc, addr >> 16);
	if ((addr >> 16) != ((addr + len) >> 16))
		printk("getreg: You found it!\n");
	res = wctdm_vpm150m_setreg_full(wc, 0, len, addr & 0xffff, data);
	return res;
}

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
void gpakReadDspMemory(
    unsigned short int DspId,   /* DSP Identifier (0 to MAX_DSP_CORES-1) */
    DSP_ADDRESS DspAddress,     /* DSP's memory address of first word */
    unsigned int NumWords,      /* number of contiguous words to read */
    DSP_WORD *pWordValues       /* pointer to array of word values variable */
    )
{
	struct wctdm *wc = wc_find_iface(DspId);
	int i;
	int transcount;
	int ret;

	vpm150m_io_wait(wc);
	if ( NumWords < VPM150M_MAX_COMMANDS ) {
		struct vpm150m_cmd* cmds[VPM150M_MAX_COMMANDS] = {0};
		wctdm_vpm150m_setpage(wc, DspAddress >> 16);
		DspAddress &= 0xffff;
		for (i=0; i < NumWords; ++i) {
			ret = wctdm_vpm150m_getreg_full_async(wc,0,1,DspAddress+i,&pWordValues[i],
				&cmds[i]);
			if (0 != ret) {
				return;
			}
		}
		for (i=NumWords-1; i >=0; --i) {
			ret = wctdm_vpm150m_getreg_full_return(wc,0,1,DspAddress+i,&pWordValues[i],
				&cmds[i]);
			if (0 != ret) {
				return;
			}
		}
	}
	else {
		for (i = 0; i < NumWords;) {
			if ((NumWords - i) > VPM150M_MAX_DATA)
				transcount = VPM150M_MAX_DATA;
			else
				transcount = NumWords - i;
			wctdm_vpm150m_getreg(wc, transcount, DspAddress + i, &pWordValues[i]);
			i += transcount;
		}
	}
	return;
}

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
void gpakWriteDspMemory(
    unsigned short int DspId,   /* DSP Identifier (0 to MAX_DSP_CORES-1) */
    DSP_ADDRESS DspAddress,     /* DSP's memory address of first word */
    unsigned int NumWords,      /* number of contiguous words to write */
    DSP_WORD *pWordValues       /* pointer to array of word values to write */
    )
{

	struct wctdm *wc = wc_find_iface(DspId);
	int i;
	int transcount;

	//printk("Writing %d words to memory\n", NumWords);
	if (wc && wc->vpm150m) {
		for (i = 0; i < NumWords;) {
			if ((NumWords - i) > VPM150M_MAX_DATA)
				transcount = VPM150M_MAX_DATA;
			else
				transcount = NumWords - i;

			wctdm_vpm150m_setreg(wc, transcount, DspAddress + i, &pWordValues[i]);
			i += transcount;
		}
#if 0
		for (i = 0; i < NumWords; i++) {
			if (wctdm_vpm150m_getreg(wc, DspAddress + i) != pWordValues[i]) {
				printk("Error in write.  Address %x is not %x\n", DspAddress + i, pWordValues[i]);
			}
		}
#endif
	}
	return;

}


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
void gpakHostDelay(void)
{
}


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
void gpakLockAccess(unsigned short DspId)
{
	struct wctdm *wc;

	wc = wc_find_iface(DspId);

	if (wc) {
		struct vpm150m *vpm = wc->vpm150m;

		if (vpm)
			down_interruptible(&vpm->sem);
	}
}


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
void gpakUnlockAccess(unsigned short DspId)
{
	struct wctdm *wc;

	wc = wc_find_iface(DspId);

	if (wc) {
		struct vpm150m *vpm = wc->vpm150m;

		if (vpm)
			up(&vpm->sem);
	}
}


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
int gpakReadFile(
    GPAK_FILE_ID FileId,        /* G.PAK Download File Identifier */
    unsigned char *pBuffer,	/* pointer to buffer for storing bytes */
    unsigned int NumBytes       /* number of bytes to read */
    )
{
#ifdef VPM150M_SUPPORT
	struct wctdm_firmware *fw = FileId;
	unsigned int i, count;

	if (!fw || !fw->fw)
		return -1;

	if (NumBytes > (fw->fw->size - fw->offset))
		count = fw->fw->size - fw->offset;
	else
		count = NumBytes;

	for (i = 0; i < count; i++)
		pBuffer[i] = fw->fw->data[fw->offset + i];

	fw->offset += count;

	return count;
#endif
}
