/*
 * Written by Oron Peled <oron@actcom.co.il>
 * Copyright (C) 2004, Xorcom
 *
 * Derived from ztdummy
 *
 * Copyright (C) 2002, Hermes Softlab
 * Copyright (C) 2004, Digium, Inc.
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#  warning "This module is tested only with 2.6 kernels"
#endif

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/delay.h>	/* for udelay */
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <zaptel.h>
#include "xbus-core.h"
#include "xproto.h"
#include "xpp_zap.h"
#include "parport_debug.h"

static const char rcsid[] = "$Id: xpp_zap.c 4456 2008-08-04 15:52:49Z tzafrir $";

#ifdef CONFIG_PROC_FS
struct proc_dir_entry *xpp_proc_toplevel = NULL;
#define	PROC_DIR		"xpp"
#define	PROC_XPD_ZTREGISTER	"zt_registration"
#define	PROC_XPD_BLINK		"blink"
#define	PROC_XPD_SUMMARY	"summary"
#endif

#define	MAX_QUEUE_LEN		10000
#define	DELAY_UNTIL_DIALTONE	3000

DEF_PARM(int, debug, 0, 0644, "Print DBG statements");
static DEF_PARM_BOOL(zap_autoreg, 0, 0644, "Register spans automatically (1) or not (0)");
static DEF_PARM_BOOL(prefmaster, 0, 0644, "Do we want to be zaptel preferred sync master");
// DEF_ARRAY(int, pcmtx, 4, 0, "Forced PCM values to transmit");

#include "zap_debug.h"

#ifdef	DEBUG_SYNC_PARPORT
/*
 * Use parallel port to sample our PCM sync and diagnose quality and
 * potential problems. A logic analizer or a scope should be connected
 * to the data bits of the parallel port.
 *
 * Array parameter: Choose the two xbuses Id's to sample.
 *                  This can be changed on runtime as well. Example:
 *                    echo "3,5" > /sys/module/xpp/parameters/parport_xbuses
 */
static int parport_xbuses[2] = { 0, 1 };
unsigned int parport_xbuses_num_values;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,9)
module_param_array(parport_xbuses, int, &parport_xbuses_num_values, 0577);
#else
module_param_array(parport_xbuses, int, parport_xbuses_num_values, 0577);
#endif
MODULE_PARM_DESC(parport_xbuses, "Id's of xbuses to sample (1-2)");

/*
 * Flip a single bit in the parallel port:
 *   - The bit number is either bitnum0 or bitnum1
 *   - Bit is selected by xbus number from parport_xbuses[]
 */
void xbus_flip_bit(xbus_t *xbus, unsigned int bitnum0, unsigned int bitnum1)
{
	int		num = xbus->num;

	if(num == parport_xbuses[0])
		flip_parport_bit(bitnum0);
	if(num == parport_xbuses[1])
		flip_parport_bit(bitnum1);
}
EXPORT_SYMBOL(xbus_flip_bit);
#endif

static atomic_t num_registered_spans = ATOMIC_INIT(0);

int total_registered_spans(void)
{
	return atomic_read(&num_registered_spans);
}

static int zaptel_register_xpd(xpd_t *xpd);
static int zaptel_unregister_xpd(xpd_t *xpd);
static int xpd_read_proc(char *page, char **start, off_t off, int count, int *eof, void *data);
static int proc_xpd_ztregister_read(char *page, char **start, off_t off, int count, int *eof, void *data);
static int proc_xpd_ztregister_write(struct file *file, const char __user *buffer, unsigned long count, void *data);
static int proc_xpd_blink_read(char *page, char **start, off_t off, int count, int *eof, void *data);
static int proc_xpd_blink_write(struct file *file, const char __user *buffer, unsigned long count, void *data);

/*------------------------- XPD Management -------------------------*/

static void xpd_proc_remove(xbus_t *xbus, xpd_t *xpd)
{
#ifdef CONFIG_PROC_FS
	if(xpd->proc_xpd_dir) {
		chip_proc_remove(xbus, xpd);
		if(xpd->proc_xpd_summary) {
			XPD_DBG(PROC, xpd, "Removing proc '%s'\n", PROC_XPD_SUMMARY);
			remove_proc_entry(PROC_XPD_SUMMARY, xpd->proc_xpd_dir);
			xpd->proc_xpd_summary = NULL;
		}
		if(xpd->proc_xpd_ztregister) {
			XPD_DBG(PROC, xpd, "Removing proc '%s'\n", PROC_XPD_ZTREGISTER);
			remove_proc_entry(PROC_XPD_ZTREGISTER, xpd->proc_xpd_dir);
			xpd->proc_xpd_ztregister = NULL;
		}
		if(xpd->proc_xpd_blink) {
			XPD_DBG(PROC, xpd, "Removing proc '%s'\n", PROC_XPD_BLINK);
			remove_proc_entry(PROC_XPD_BLINK, xpd->proc_xpd_dir);
			xpd->proc_xpd_blink = NULL;
		}
		XPD_DBG(PROC, xpd, "Removing %s/%s proc directory\n",
				xbus->busname, xpd->xpdname);
		remove_proc_entry(xpd->xpdname, xbus->proc_xbus_dir);
		xpd->proc_xpd_dir = NULL;
	}
#endif
}

static int xpd_proc_create(xbus_t *xbus, xpd_t *xpd)
{
#ifdef	CONFIG_PROC_FS
	XPD_DBG(PROC, xpd, "Creating proc directory\n");
	xpd->proc_xpd_dir = proc_mkdir(xpd->xpdname, xbus->proc_xbus_dir);
	if(!xpd->proc_xpd_dir) {
		XPD_ERR(xpd, "Failed to create proc directory\n");
		goto err;
	}
	xpd->proc_xpd_summary = create_proc_read_entry(PROC_XPD_SUMMARY, 0444, xpd->proc_xpd_dir,
			xpd_read_proc, xpd);
	if(!xpd->proc_xpd_summary) {
		XPD_ERR(xpd, "Failed to create proc file '%s'\n", PROC_XPD_SUMMARY);
		goto err;
	}
	xpd->proc_xpd_summary->owner = THIS_MODULE;
	xpd->proc_xpd_ztregister = create_proc_entry(PROC_XPD_ZTREGISTER, 0644, xpd->proc_xpd_dir);
	if (!xpd->proc_xpd_ztregister) {
		XPD_ERR(xpd, "Failed to create proc file '%s'\n", PROC_XPD_ZTREGISTER);
		goto err;
	}
	xpd->proc_xpd_ztregister->owner = THIS_MODULE;
	xpd->proc_xpd_ztregister->data = xpd;
	xpd->proc_xpd_ztregister->read_proc = proc_xpd_ztregister_read;
	xpd->proc_xpd_ztregister->write_proc = proc_xpd_ztregister_write;
	xpd->proc_xpd_blink = create_proc_entry(PROC_XPD_BLINK, 0644, xpd->proc_xpd_dir);
	if (!xpd->proc_xpd_blink) {
		XPD_ERR(xpd, "Failed to create proc file '%s'\n", PROC_XPD_BLINK);
		goto err;
	}
	xpd->proc_xpd_blink->owner = THIS_MODULE;
	xpd->proc_xpd_blink->data = xpd;
	xpd->proc_xpd_blink->read_proc = proc_xpd_blink_read;
	xpd->proc_xpd_blink->write_proc = proc_xpd_blink_write;
	if(chip_proc_create(xbus, xpd) < 0)
		goto err;
#endif
	return 0;
err:
	xpd_proc_remove(xbus, xpd);
	return -EFAULT;
}

void xpd_free(xpd_t *xpd)
{
	xbus_t	*xbus = NULL;

	if(!xpd)
		return;
	if(xpd->xproto)
		xproto_put(xpd->xproto);	/* was taken in xpd_alloc() */
	xpd->xproto = NULL;
	xbus = xpd->xbus;
	if(!xbus)
		return;
	XPD_DBG(DEVICES, xpd, "\n");
	xpd_proc_remove(xbus, xpd);
	xbus_unregister_xpd(xbus, xpd);
	KZFREE(xpd);
}


__must_check int xpd_common_init(xbus_t *xbus, xpd_t *xpd, int unit, int subunit, int subtype, int subunits)
{
	int	ret;

	MKADDR(&xpd->addr, unit, subunit);
	xpd->xbus_idx = XPD_IDX(unit,subunit);
	snprintf(xpd->xpdname, XPD_NAMELEN, "XPD-%1d%1d", unit, subunit);
	xpd->subtype = subtype;
	xpd->subunits = subunits;
	xpd->offhook = 0;

	/* For USB-1 disable some channels */
	if(MAX_SEND_SIZE(xbus) < RPACKET_SIZE(GLOBAL, PCM_WRITE)) {
		xpp_line_t	no_pcm;

		no_pcm = 0x7F | xpd->digital_outputs | xpd->digital_inputs;
		xpd->no_pcm = no_pcm;
		XBUS_NOTICE(xbus, "max xframe size = %d, disabling some PCM channels. no_pcm=0x%04X\n",
				MAX_SEND_SIZE(xbus), xpd->no_pcm);
	}
	if((ret = xpd_proc_create(xbus, xpd)) < 0)
		return ret;
	xbus_register_xpd(xbus, xpd);
	return 0;
}

/*
 * Synchronous part of XPD detection.
 * Called from xbus_poll()
 */
int create_xpd(xbus_t *xbus, const xproto_table_t *proto_table,
		int unit,
		int subunit,
		byte type,
		byte subtype,
		int subunits,
		byte port_dir)
{
	xpd_t			*xpd = NULL;
	bool			to_phone;
	int			ret = -EINVAL;

	BUG_ON(type == XPD_TYPE_NOMODULE);
	to_phone = BIT(subunit) & port_dir;
	BUG_ON(!xbus);
	xpd = xpd_byaddr(xbus, unit, subunit);
	if(xpd) {
		XPD_NOTICE(xpd, "XPD at %d%d already exists\n",
			unit, subunit);
		goto out;
	}
	xpd = proto_table->xops.card_new(xbus, unit, subunit, proto_table, subtype, subunits, to_phone);
	if(!xpd) {
		XBUS_NOTICE(xbus, "card_new(%d,%d,%d,%d,%d) failed. Ignored.\n",
			unit, subunit, proto_table->type, subtype, to_phone);
		goto err;
	}
out:
	return 0;
err:
	if(xpd)
		xpd_free(xpd);
	return ret;
}

void xpd_post_init(xpd_t *xpd)
{
	XPD_DBG(DEVICES, xpd, "\n");
	if(zap_autoreg)
		zaptel_register_xpd(xpd);
}

#ifdef CONFIG_PROC_FS

/**
 * Prints a general procfs entry for the bus, under xpp/BUSNAME/summary
 * @page TODO: figure out procfs
 * @start TODO: figure out procfs
 * @off TODO: figure out procfs
 * @count TODO: figure out procfs
 * @eof TODO: figure out procfs
 * @data an xbus_t pointer with the bus data.
 */
static int xpd_read_proc(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int		len = 0;
	xpd_t		*xpd = data;
	xbus_t		*xbus;
	int		i;

	if(!xpd)
		goto out;

	xbus = xpd->xbus;
	len += sprintf(page + len, "%s (%s, card %s, span %d)\n"
			"timing_priority: %d\n"
			"timer_count: %d span->mainttimer=%d\n"
			,
			xpd->xpdname, xpd->type_name,
			(xpd->card_present) ? "present" : "missing",
			(SPAN_REGISTERED(xpd)) ? xpd->span.spanno : 0,
			xpd->timing_priority,
			xpd->timer_count, xpd->span.mainttimer
			);
	len += sprintf(page + len, "Address: U=%d S=%d\n", xpd->addr.unit, xpd->addr.subunit);
	len += sprintf(page + len, "Subunits: %d\n", xpd->subunits);
	len += sprintf(page + len, "Type: %d.%d\n\n", xpd->type, xpd->subtype);
	len += sprintf(page + len, "pcm_len=%d\n\n", xpd->pcm_len);
	len += sprintf(page + len, "wanted_pcm_mask=0x%04X\n\n", xpd->wanted_pcm_mask);
	len += sprintf(page + len, "mute_dtmf=0x%04X\n\n", xpd->mute_dtmf);
	len += sprintf(page + len, "STATES:");
	len += sprintf(page + len, "\n\t%-17s: ", "output_relays");
	for_each_line(xpd, i) {
		len += sprintf(page + len, "%d ", IS_SET(xpd->digital_outputs, i));
	}
	len += sprintf(page + len, "\n\t%-17s: ", "input_relays");
	for_each_line(xpd, i) {
		len += sprintf(page + len, "%d ", IS_SET(xpd->digital_inputs, i));
	}
	len += sprintf(page + len, "\n\t%-17s: ", "offhook");
	for_each_line(xpd, i) {
		len += sprintf(page + len, "%d ", IS_SET(xpd->offhook, i));
	}
	len += sprintf(page + len, "\n\t%-17s: ", "cid_on");
	for_each_line(xpd, i) {
		len += sprintf(page + len, "%d ", IS_SET(xpd->cid_on, i));
	}
	len += sprintf(page + len, "\n\t%-17s: ", "msg_waiting");
	for_each_line(xpd, i) {
		len += sprintf(page + len, "%d ", IS_SET(xpd->msg_waiting, i));
	}
	len += sprintf(page + len, "\n\t%-17s: ", "ringing");
	for_each_line(xpd, i) {
		len += sprintf(page + len, "%d ", xpd->ringing[i]);
	}
	len += sprintf(page + len, "\n\t%-17s: ", "no_pcm");
	for_each_line(xpd, i) {
		len += sprintf(page + len, "%d ", IS_SET(xpd->no_pcm, i));
	}
#if 1
	if(SPAN_REGISTERED(xpd)) {
		len += sprintf(page + len, "\nPCM:\n            |         [readchunk]       |         [writechunk]      | W D");
		for_each_line(xpd, i) {
			struct zt_chan	*chans = xpd->span.chans;
			byte	rchunk[ZT_CHUNKSIZE];
			byte	wchunk[ZT_CHUNKSIZE];
			byte	*rp;
			byte	*wp;
			int j;

			if(IS_SET(xpd->digital_outputs, i))
				continue;
			if(IS_SET(xpd->digital_inputs, i))
				continue;
			if(IS_SET(xpd->digital_signalling, i))
				continue;
			rp = chans[i].readchunk;
			wp = chans[i].writechunk;
			memcpy(rchunk, rp, ZT_CHUNKSIZE);
			memcpy(wchunk, wp, ZT_CHUNKSIZE);
			len += sprintf(page + len, "\n  port %2d>  |  ", i);
			for(j = 0; j < ZT_CHUNKSIZE; j++) {
				len += sprintf(page + len, "%02X ", rchunk[j]);
			}
			len += sprintf(page + len, " |  ");
			for(j = 0; j < ZT_CHUNKSIZE; j++) {
				len += sprintf(page + len, "%02X ", wchunk[j]);
			}
			len += sprintf(page + len, " | %c",
				(IS_SET(xpd->wanted_pcm_mask, i))?'+':' ');
			len += sprintf(page + len, " %c",
				(IS_SET(xpd->mute_dtmf, i))?'-':' ');
		}
	}
#endif
#if 0
	if(SPAN_REGISTERED(xpd)) {
		len += sprintf(page + len, "\nSignalling:\n");
		for_each_line(xpd, i) {
			struct zt_chan *chan = &xpd->span.chans[i];
			len += sprintf(page + len, "\t%2d> sigcap=0x%04X sig=0x%04X\n", i, chan->sigcap, chan->sig);
		}
	}
#endif
	len += sprintf(page + len, "\nCOUNTERS:\n");
	for(i = 0; i < XPD_COUNTER_MAX; i++) {
		len += sprintf(page + len, "\t\t%-20s = %d\n",
				xpd_counters[i].name, xpd->counters[i]);
	}
	len += sprintf(page + len, "<-- len=%d\n", len);
out:
	if (len <= off+count)
		*eof = 1;
	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;
	return len;

}

#endif

/*
 * xpd_alloc - Allocator for new XPD's
 *
 */
xpd_t *xpd_alloc(size_t privsize, const xproto_table_t *proto_table, int channels)
{
	xpd_t		*xpd = NULL;
	size_t		alloc_size = sizeof(xpd_t) + privsize;
	int		type = proto_table->type;

	BUG_ON(!proto_table);
	DBG(DEVICES, "type=%d channels=%d (alloc_size=%zd)\n",
		type, channels, alloc_size);
	if(channels > CHANNELS_PERXPD) {
		ERR("%s: type=%d: too many channels %d\n",
			__FUNCTION__, type, channels);
		goto err;
	}

	if((xpd = KZALLOC(alloc_size, GFP_KERNEL)) == NULL) {
		ERR("%s: type=%d: Unable to allocate memory\n",
			__FUNCTION__, type);
		goto err;
	}
	xpd->priv = (byte *)xpd + sizeof(xpd_t);
	spin_lock_init(&xpd->lock);
	xpd->xbus = NULL;
	xpd->xbus_idx = -1;
	xpd->channels = channels;
	xpd->chans = NULL;
	xpd->card_present = 0;
	xpd->offhook = 0x0;	/* ONHOOK */
	xpd->type = proto_table->type;
	xpd->xproto = proto_table;
	xpd->xops = &proto_table->xops;
	xpd->digital_outputs = 0;
	xpd->digital_inputs = 0;

	atomic_set(&xpd->zt_registered, 0);
	atomic_set(&xpd->open_counter, 0);

	xpd->chans = kmalloc(sizeof(struct zt_chan)*xpd->channels, GFP_KERNEL);
	if (xpd->chans == NULL) {
		ERR("%s: Unable to allocate channels\n", __FUNCTION__);
		goto err;
	}
	xproto_get(type);	/* will be returned in xpd_free() */
	return xpd;
err:
	if(xpd) {
		if(xpd->chans)
			kfree((void *)xpd->chans);
		kfree(xpd);
	}
	return NULL;
}

/* FIXME: this should be removed once digium patch their zaptel.h
 * I simply wish to avoid changing zaptel.h in the xpp patches.
 */
#ifndef ZT_EVENT_REMOVED
#define ZT_EVENT_REMOVED (20)
#endif

void xpd_disconnect(xpd_t *xpd)
{
	unsigned long	flags;

	BUG_ON(!xpd);

	spin_lock_irqsave(&xpd->lock, flags);
	XPD_DBG(DEVICES, xpd, "(%p)\n", xpd->xproto);
	if(!xpd->card_present)	/* Multiple reports */
		goto out;
	xpd->card_present = 0;
	if(SPAN_REGISTERED(xpd)) {
		int i;

		update_xpd_status(xpd, ZT_ALARM_NOTOPEN);
		/* TODO: Should this be done before releasing the spinlock? */
		XPD_DBG(DEVICES, xpd, "Queuing ZT_EVENT_REMOVED on all channels to ask user to release them\n");
		for (i=0; i<xpd->span.channels; i++)
			zt_qevent_lock(&xpd->chans[i],ZT_EVENT_REMOVED);
	}
out:
	spin_unlock_irqrestore(&xpd->lock, flags);
}

void xpd_remove(xpd_t *xpd)
{
	xbus_t	*xbus;

	BUG_ON(!xpd);
	xbus = xpd->xbus;
	XPD_INFO(xpd, "Remove\n");
	zaptel_unregister_xpd(xpd);
	CALL_XMETHOD(card_remove, xbus, xpd);
	xpd_free(xpd);
}

void update_xpd_status(xpd_t *xpd, int alarm_flag)
{
	struct zt_span *span = &xpd->span;

	if(!SPAN_REGISTERED(xpd)) {
		// XPD_NOTICE(xpd, "%s: XPD is not registered. Skipping.\n", __FUNCTION__);
		return;
	}
	switch (alarm_flag) {
		case ZT_ALARM_NONE:
			xpd->last_response = jiffies;
			break;
		default:
			// Nothing
			break;
	}
	if(span->alarms == alarm_flag)
		return;
	span->alarms = alarm_flag;
	zt_alarm_notify(span);
	XPD_DBG(GENERAL, xpd, "Update XPD alarms: %s -> %02X\n", xpd->span.name, alarm_flag);
}

void update_line_status(xpd_t *xpd, int pos, bool to_offhook)
{
	zt_rxsig_t	rxsig;

	BUG_ON(!xpd);
	if(to_offhook) {
		BIT_SET(xpd->offhook, pos);
		rxsig = ZT_RXSIG_OFFHOOK;
	} else {
		BIT_CLR(xpd->offhook, pos);
		BIT_CLR(xpd->cid_on, pos);
		rxsig = ZT_RXSIG_ONHOOK;
		/*
		 * To prevent latest PCM to stay in buffers
		 * indefinitely, mark this channel for a
		 * single silence transmittion.
		 *
		 * This bit will be cleared on the next tick.
		 */
		BIT_SET(xpd->silence_pcm, pos);
	}
	/*
	 * We should not spinlock before calling zt_hooksig() as
	 * it may call back into our xpp_hooksig() and cause
	 * a nested spinlock scenario
	 */
	LINE_DBG(SIGNAL, xpd, pos, "rxsig=%s\n", (rxsig == ZT_RXSIG_ONHOOK) ? "ONHOOK" : "OFFHOOK");
	if(SPAN_REGISTERED(xpd))
		zt_hooksig(&xpd->chans[pos], rxsig);
}

#ifdef CONFIG_PROC_FS
static int proc_xpd_ztregister_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int		len = 0;
	unsigned long	flags;
	xpd_t		*xpd = data;

	BUG_ON(!xpd);
	spin_lock_irqsave(&xpd->lock, flags);

	len += sprintf(page + len, "%d\n", SPAN_REGISTERED(xpd) ? xpd->span.spanno : 0);
	spin_unlock_irqrestore(&xpd->lock, flags);
	if (len <= off+count)
		*eof = 1;
	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;
	return len;
}

static int proc_xpd_ztregister_write(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
	xpd_t		*xpd = data;
	char		buf[MAX_PROC_WRITE];
	int		zt_reg;
	int		ret;

	BUG_ON(!xpd);
	if(count >= MAX_PROC_WRITE)
		return -EINVAL;
	if(copy_from_user(buf, buffer, count))
		return -EFAULT;
	buf[count] = '\0';
	ret = sscanf(buf, "%d", &zt_reg);
	if(ret != 1)
		return -EINVAL;
	XPD_DBG(GENERAL, xpd, "%s\n", (zt_reg) ? "register" : "unregister");
	if(zt_reg)
		ret = zaptel_register_xpd(xpd);
	else
		ret = zaptel_unregister_xpd(xpd);
	return (ret < 0) ? ret : count;
}

static int proc_xpd_blink_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int		len = 0;
	unsigned long	flags;
	xpd_t		*xpd = data;

	BUG_ON(!xpd);
	spin_lock_irqsave(&xpd->lock, flags);

	len += sprintf(page + len, "0x%lX\n", xpd->blink_mode);
	spin_unlock_irqrestore(&xpd->lock, flags);
	if (len <= off+count)
		*eof = 1;
	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;
	return len;
}

static int proc_xpd_blink_write(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
	xpd_t		*xpd = data;
	char		buf[MAX_PROC_WRITE];
	char		*endp;
	unsigned long	blink;


	BUG_ON(!xpd);
	if(count >= MAX_PROC_WRITE)
		return -EINVAL;
	if(copy_from_user(buf, buffer, count))
		return -EFAULT;
	buf[count] = '\0';
	if(count > 0 && buf[count-1] == '\n')	/* chomp */
		buf[count-1] = '\0';
	blink = simple_strtoul(buf, &endp, 0);
	if(*endp != '\0' || blink > 0xFFFF)
		return -EINVAL;
	XPD_DBG(GENERAL, xpd, "BLINK channels: 0x%lX\n", blink);
	xpd->blink_mode = blink;
	return count;
}

#endif


#define	XPP_MAX_LEN	512

/*------------------------- Zaptel Interfaces ----------------------*/


/*
 * Called from zaptel with spinlock held on chan. Must not call back
 * zaptel functions.
 */
int xpp_open(struct zt_chan *chan)
{
#if 0
	xpd_t		*xpd = chan->pvt;
	xbus_t		*xbus = xpd->xbus;
	int		pos = chan->chanpos - 1;
	unsigned long	flags;
#else
	xpd_t		*xpd;
	xbus_t		*xbus;
	int		pos;
	unsigned long	flags;

	if (!chan) {
		NOTICE("open called on a null chan\n");
		return -EINVAL;
	}
	xpd = chan->pvt;
	if (!xpd) {
		NOTICE("open called on a chan with no pvt (xpd)\n");
		return -EINVAL;
	}
	xbus = xpd->xbus;
	if (!xbus) {
		NOTICE("open called on a chan with no xbus\n");
		return -EINVAL;
	}
	pos = chan->chanpos - 1;
#endif

	spin_lock_irqsave(&xbus->lock, flags);
	atomic_inc(&xbus->xbus_ref_count);
	atomic_inc(&xpd->open_counter);
	if(IS_SET(xpd->digital_signalling, pos))	/* D-chan offhook */
		BIT_SET(xpd->offhook, pos);
	DBG(DEVICES, "chan=%d (xbus_ref_count=%d)\n",
		pos, atomic_read(&xbus->xbus_ref_count));
	spin_unlock_irqrestore(&xbus->lock, flags);
	if(xpd->xops->card_open)
		xpd->xops->card_open(xpd, pos);
	return 0;
}

int xpp_close(struct zt_chan *chan)
{
	xpd_t		*xpd = chan->pvt;
	xbus_t		*xbus = xpd->xbus;
	int		pos = chan->chanpos - 1;
	unsigned long	flags;

	spin_lock_irqsave(&xbus->lock, flags);
	atomic_dec(&xpd->open_counter);
	if(IS_SET(xpd->digital_signalling, pos))	/* D-chan onhook */
		BIT_CLR(xpd->offhook, pos);
	spin_unlock_irqrestore(&xbus->lock, flags);
	if(xpd->xops->card_close)
		xpd->xops->card_close(xpd, pos);
	XPD_DBG(GENERAL, xpd, "pid=%d: chan=%d (xbus_ref_count=%d)\n",
		current->pid, pos, atomic_read(&xbus->xbus_ref_count));
	if(atomic_dec_and_test(&xbus->xbus_ref_count))
		xbus_remove(xbus);
	return 0;
}

void report_bad_ioctl(const char *msg, xpd_t *xpd, int pos, unsigned int cmd)
{
	char	*extra_msg = "";

	if(_IOC_TYPE(cmd) == 0xDA)
		extra_msg = " (for DAHDI driver)";
	XPD_NOTICE(xpd, "%s: Bad ioctl%s\n", msg, extra_msg);
	XPD_NOTICE(xpd, "ENOTTY: chan=%d cmd=0x%x\n", pos, cmd);
	XPD_NOTICE(xpd, "        IOC_TYPE=0x%02X\n", _IOC_TYPE(cmd));
	XPD_NOTICE(xpd, "        IOC_DIR=0x%02X\n", _IOC_DIR(cmd));
	XPD_NOTICE(xpd, "        IOC_NR=%d\n", _IOC_NR(cmd));
	XPD_NOTICE(xpd, "        IOC_SIZE=0x%02X\n", _IOC_SIZE(cmd));
}

int xpp_ioctl(struct zt_chan *chan, unsigned int cmd, unsigned long arg)
{
	xpd_t	*xpd = chan->pvt;
	int	pos = chan->chanpos - 1;

	if(!xpd) {
		ERR("%s: channel in pos %d, was already closed. Ignore.\n",
			__FUNCTION__, pos);
		return -ENODEV;
	}
	switch (cmd) {
		default:
			/* Some span-specific commands before we give up: */
			if (xpd->xops->card_ioctl != NULL) {
				return xpd->xops->card_ioctl(xpd, pos, cmd, arg);
			}
			report_bad_ioctl(THIS_MODULE->name, xpd, pos, cmd);
			return -ENOTTY;
	}
	return 0;
}

static int xpp_hooksig(struct zt_chan *chan, zt_txsig_t txsig)
{
	xpd_t	*xpd = chan->pvt;
	xbus_t	*xbus;
	int pos = chan->chanpos - 1;

	if(!xpd) {
		ERR("%s: channel in pos %d, was already closed. Ignore.\n",
			__FUNCTION__, pos);
		return -ENODEV;
	}
	xbus = xpd->xbus;
	BUG_ON(!xbus);
	DBG(SIGNAL, "Setting %s to %s (%d)\n", chan->name, txsig2str(txsig), txsig);
	return CALL_XMETHOD(card_hooksig, xbus, xpd, pos, txsig);
}

/* Req: Set the requested chunk size.  This is the unit in which you must
   report results for conferencing, etc */
int xpp_setchunksize(struct zt_span *span, int chunksize);

/* Enable maintenance modes */
int xpp_maint(struct zt_span *span, int cmd)
{
	xpd_t		*xpd = span->pvt;
	int		ret = 0;
#if 0
	char		loopback_data[] = "THE-QUICK-BROWN-FOX-JUMPED-OVER-THE-LAZY-DOG";
#endif

	DBG(GENERAL, "span->mainttimer=%d\n", span->mainttimer);
	switch(cmd) {
		case ZT_MAINT_NONE:
			printk("XXX Turn off local and remote loops XXX\n");
			break;
		case ZT_MAINT_LOCALLOOP:
			printk("XXX Turn on local loopback XXX\n");
			break;
		case ZT_MAINT_REMOTELOOP:
			printk("XXX Turn on remote loopback XXX\n");
			break;
		case ZT_MAINT_LOOPUP:
			printk("XXX Send loopup code XXX\n");
			// CALL_XMETHOD(LOOPBACK_AX, xpd->xbus, xpd, loopback_data, ARRAY_SIZE(loopback_data));
			break;
		case ZT_MAINT_LOOPDOWN:
			printk("XXX Send loopdown code XXX\n");
			break;
		case ZT_MAINT_LOOPSTOP:
			printk("XXX Stop sending loop codes XXX\n");
			break;
		default:
			ERR("XPP: Unknown maint command: %d\n", cmd);
			ret = -EINVAL;
			break;
	}
	if (span->mainttimer || span->maintstat) 
		update_xpd_status(xpd, ZT_ALARM_LOOPBACK);
	return ret;
}

#ifdef	CONFIG_ZAPTEL_WATCHDOG
/*
 * If the watchdog detects no received data, it will call the
 * watchdog routine
 */
static int xpp_watchdog(struct zt_span *span, int cause)
{
	static	int rate_limit = 0;

	if((rate_limit++ % 1000) == 0)
		DBG(GENERAL, "\n");
	return 0;
}
#endif

/**
 * Unregister an xpd from zaptel and release related resources
 * @xpd The xpd to be unregistered
 * @returns 0 on success, errno otherwise
 * 
 * Checks that nobody holds an open channel.
 *
 * Called by:
 * 	- User action through /proc
 * 	- During xpd_remove()
 */
static int zaptel_unregister_xpd(xpd_t *xpd)
{
	unsigned long	flags;

	BUG_ON(!xpd);
	spin_lock_irqsave(&xpd->lock, flags);

	if(!SPAN_REGISTERED(xpd)) {
		XPD_NOTICE(xpd, "Already unregistered\n");
		spin_unlock_irqrestore(&xpd->lock, flags);
		return -EIDRM;
	}
	update_xpd_status(xpd, ZT_ALARM_NOTOPEN);
	if(atomic_read(&xpd->open_counter)) {
		XPD_NOTICE(xpd, "Busy (open_counter=%d). Skipping.\n", atomic_read(&xpd->open_counter));
		spin_unlock_irqrestore(&xpd->lock, flags);
		return -EBUSY;
	}
	mdelay(2);	// FIXME: This is to give chance for transmit/receiveprep to finish.
	spin_unlock_irqrestore(&xpd->lock, flags);
	if(xpd->card_present)
		xpd->xops->card_zaptel_preregistration(xpd, 0);
	atomic_dec(&xpd->zt_registered);
	atomic_dec(&num_registered_spans);
	zt_unregister(&xpd->span);
	if(xpd->card_present)
		xpd->xops->card_zaptel_postregistration(xpd, 0);
	return 0;
}

static int zaptel_register_xpd(xpd_t *xpd)
{
	struct zt_span	*span;
	xbus_t		*xbus;
	int		cn;
	const xops_t	*xops;

	BUG_ON(!xpd);
	xops = xpd->xops;
	xbus = xpd->xbus;

	if (SPAN_REGISTERED(xpd)) {
		XPD_ERR(xpd, "Already registered\n");
		return -EEXIST;
	}
	cn = xpd->channels;
	XPD_DBG(DEVICES, xpd, "Initializing span: %d channels.\n", cn);
	memset(xpd->chans, 0, sizeof(struct zt_chan)*cn);
	memset(&xpd->span, 0, sizeof(struct zt_span));

	span = &xpd->span;
	snprintf(span->name, MAX_SPANNAME, "%s/%s", xbus->busname, xpd->xpdname);
	span->deflaw = ZT_LAW_MULAW;	/* default, may be overriden by card_* drivers */
	init_waitqueue_head(&span->maintq);
	span->pvt = xpd;
	span->channels = cn;
	span->chans = xpd->chans;

	span->open = xpp_open;
	span->close = xpp_close;
	span->flags = ZT_FLAG_RBS;
	span->hooksig = xpp_hooksig;	/* Only with RBS bits */
	span->ioctl = xpp_ioctl;
	span->maint = xpp_maint;
#ifdef ZT_SPANSTAT_V2 
	/*
	 * This actually describe the zt_spaninfo version 3
	 * A bunch of unrelated data exported via a modified ioctl()
	 * What a bummer...
	 */
	span->manufacturer = "Xorcom Inc.";	/* OK, that's obvious */
	/* span->spantype = "...."; set in card_zaptel_preregistration() */
	/*
	 * Yes, this basically duplicates information available
	 * from the description field. If some more is needed
	 * why not add it there?
	 * OK, let's add to the kernel more useless info.
	 */
	snprintf(span->devicetype, sizeof(span->devicetype) - 1,
		"Astribank: Unit %x Subunit %x: %s",
		XBUS_UNIT(xpd->xbus_idx), XBUS_SUBUNIT(xpd->xbus_idx),
		xpd->type_name);
	/*
	 * location is the only usefull new data item.
	 * For our devices it was available for ages via:
	 *  - The legacy "/proc/xpp/XBUS-??/summary" (CONNECTOR=...)
	 *  - The same info in "/proc/xpp/xbuses"
	 *  - The modern "/sys/bus/astribanks/devices/xbus-??/connector" attribute
	 * So let's also export it via the newfangled "location" field.
	 */
	snprintf(span->location, sizeof(span->location) - 1, "%s", xbus->location); 
	/*
	 * Who said a span and irq have 1-1 relationship?
	 * Also exporting this low-level detail isn't too wise.
	 * No irq's for you today!
	 */
	span->irq = 0;
#endif 
#ifdef	ZAPTEL_SYNC_TICK
	span->sync_tick = zaptel_sync_tick;
#endif
	if (xpp_ec)
		span->echocan = xpp_echocan;
#ifdef	CONFIG_ZAPTEL_WATCHDOG
	span->watchdog = xpp_watchdog;
#endif

	snprintf(xpd->span.desc, MAX_SPANDESC, "Xorcom XPD #%02d/%1d%1d: %s",
			xbus->num, xpd->addr.unit, xpd->addr.subunit, xpd->type_name);
	XPD_DBG(GENERAL, xpd, "Registering span '%s'\n", xpd->span.desc);
	xpd->xops->card_zaptel_preregistration(xpd, 1);
	if(zt_register(&xpd->span, prefmaster)) {
		XPD_ERR(xpd, "Failed to zt_register span\n");
		return -ENODEV;
	}
	atomic_inc(&num_registered_spans);
	atomic_inc(&xpd->zt_registered);
	xpd->xops->card_zaptel_postregistration(xpd, 1);
	/*
	 * Update zaptel about our state
	 */
#if 0
	/*
	 * FIXME: since asterisk didn't open the channel yet, the report
	 * is discarded anyway. OTOH, we cannot report in xpp_open or
	 * xpp_chanconfig since zaptel call them with a spinlock on the channel
	 * and zt_hooksig tries to acquire the same spinlock, resulting in
	 * double spinlock deadlock (we are lucky that RH/Fedora kernel are
	 * compiled with spinlock debugging).... tough.
	 */
	for_each_line(xpd, cn) {
		struct zt_chan	*chans = xpd->span.chans;

		if(IS_SET(xpd->offhook, cn)) {
			LINE_NOTICE(xpd, cn, "Report OFFHOOK to zaptel\n");
			zt_hooksig(&chans[cn], ZT_RXSIG_OFFHOOK);
		}
	}
#endif
	return 0;
}

/*------------------------- Proc debugging interface ---------------*/

#ifdef CONFIG_PROC_FS

#if 0
static int xpp_zap_write_proc(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
}
#endif

#endif

/*------------------------- Initialization -------------------------*/

static void do_cleanup(void)
{
#ifdef CONFIG_PROC_FS
	if(xpp_proc_toplevel) {
		DBG(GENERAL, "Removing '%s' from proc\n", PROC_DIR);
		remove_proc_entry(PROC_DIR, NULL);
		xpp_proc_toplevel = NULL;
	}
#endif
}

static int __init xpp_zap_init(void)
{
	int			ret = 0;

	INFO("revision %s MAX_XPDS=%d (%d*%d)\n", XPP_VERSION,
			MAX_XPDS, MAX_UNIT, MAX_SUBUNIT);
#ifdef CONFIG_ZAPATA_BRI_DCHANS
	INFO("FEATURE: with BRISTUFF support\n");
#else
	INFO("FEATURE: without BRISTUFF support\n");
#endif
#ifdef CONFIG_PROC_FS
	xpp_proc_toplevel = proc_mkdir(PROC_DIR, NULL);
	if(!xpp_proc_toplevel) {
		ret = -EIO;
		goto err;
	}
#endif
	ret = xbus_core_init();
	if(ret) {
		ERR("xbus_core_init failed (%d)\n", ret);
		goto err;
	}
	ret = xbus_pcm_init(xpp_proc_toplevel);
	if(ret) {
		ERR("xbus_pcm_init failed (%d)\n", ret);
		xbus_core_shutdown();
		goto err;
	}
	return 0;
err:
	do_cleanup();
	return ret;
}

static void __exit xpp_zap_cleanup(void)
{
	xbus_pcm_shutdown();
	xbus_core_shutdown();
	do_cleanup();
}

EXPORT_SYMBOL(debug);
EXPORT_SYMBOL(xpd_common_init);
EXPORT_SYMBOL(create_xpd);
EXPORT_SYMBOL(xpd_post_init);
EXPORT_SYMBOL(xpd_alloc);
EXPORT_SYMBOL(xpd_free);
EXPORT_SYMBOL(xpd_disconnect);
EXPORT_SYMBOL(update_xpd_status);
EXPORT_SYMBOL(update_line_status);
EXPORT_SYMBOL(xpp_open);
EXPORT_SYMBOL(xpp_close);
EXPORT_SYMBOL(xpp_ioctl);
EXPORT_SYMBOL(xpp_maint);
EXPORT_SYMBOL(report_bad_ioctl);

MODULE_DESCRIPTION("XPP Zaptel Driver");
MODULE_AUTHOR("Oron Peled <oron@actcom.co.il>");
MODULE_LICENSE("GPL");
MODULE_VERSION(XPP_VERSION);

module_init(xpp_zap_init);
module_exit(xpp_zap_cleanup);
