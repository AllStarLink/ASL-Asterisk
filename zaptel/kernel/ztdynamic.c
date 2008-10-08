/*
 * Dynamic Span Interface for Zaptel
 *
 * Written by Mark Spencer <markster@digium.com>
 *
 * Copyright (C) 2001, Linux Support Services, Inc.
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

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/vmalloc.h>
#ifdef CONFIG_DEVFS_FS
#include <linux/devfs_fs_kernel.h>
#endif
#ifdef STANDALONE_ZAPATA
#include "zaptel.h"
#else
#include <zaptel/zaptel.h>
#endif
#ifdef LINUX26
#include <linux/moduleparam.h>
#endif

/*
 * Tasklets provide better system interactive response at the cost of the
 * possibility of losing a frame of data at very infrequent intervals.  If
 * you are more concerned with the performance of your machine, enable the
 * tasklets.  If you are strict about absolutely no drops, then do not enable
 * tasklets.
 */

#define ENABLE_TASKLETS

/*
 *  Dynamic spans implemented using TDM over X with standard message
 *  types.  Message format is as follows:
 *
 *         Byte #:          Meaning
 *         0                Number of samples per channel
 *         1                Current flags on span
 *				Bit    0: Yellow Alarm
 *	                        Bit    1: Sig bits present
 *				Bits 2-7: reserved for future use
 *         2-3		    16-bit counter value for detecting drops, network byte order.
 *         4-5		    Number of channels in the message, network byte order
 *         6...		    16-bit words, containing sig bits for each
 *                          four channels, least significant 4 bits being
 *                          the least significant channel, network byte order.
 *         the rest	    data for each channel, all samples per channel
                            before moving to the next.
 */

/* Arbitrary limit to the max # of channels in a span */
#define ZT_DYNAMIC_MAX_CHANS	256

#define ZTD_FLAG_YELLOW_ALARM		(1 << 0)
#define ZTD_FLAG_SIGBITS_PRESENT	(1 << 1)
#define ZTD_FLAG_LOOPBACK			(1 << 2)

#define ERR_NSAMP					(1 << 16)
#define ERR_NCHAN					(1 << 17)
#define ERR_LEN						(1 << 18)

EXPORT_SYMBOL(zt_dynamic_register);
EXPORT_SYMBOL(zt_dynamic_unregister);
EXPORT_SYMBOL(zt_dynamic_receive);

#ifdef ENABLE_TASKLETS
static int taskletrun;
static int taskletsched;
static int taskletpending;
static int taskletexec;
static int txerrors;
static struct tasklet_struct ztd_tlet;

static void ztd_tasklet(unsigned long data);
#endif


static struct zt_dynamic {
	char addr[40];
	char dname[20];
	int err;
	int usecount;
	int dead;
	long rxjif;
	unsigned short txcnt;
	unsigned short rxcnt;
	struct zt_span span;
	struct zt_chan *chans;
	struct zt_dynamic *next;
	struct zt_dynamic_driver *driver;
	void *pvt;
	int timing;
	int master;
	unsigned char *msgbuf;
} *dspans;

static struct zt_dynamic_driver *drivers =  NULL;

static int debug = 0;

static int hasmaster = 0;
#ifdef DEFINE_SPINLOCK
static DEFINE_SPINLOCK(dlock); 
#else
static spinlock_t dlock = SPIN_LOCK_UNLOCKED;
#endif

#ifdef DEFINE_RWLOCK
static DEFINE_RWLOCK(drvlock);
#else
static rwlock_t drvlock = RW_LOCK_UNLOCKED;
#endif

static void checkmaster(void)
{
	unsigned long flags;
	int newhasmaster=0;
	int best = 9999999;
	struct zt_dynamic *z, *master=NULL;
	spin_lock_irqsave(&dlock, flags);
	z = dspans;
	while(z) {
		if (z->timing) {
			z->master = 0;
			if (!(z->span.alarms & ZT_ALARM_RED) &&
			    (z->timing < best) && !z->dead) {
				/* If not in alarm and they're
				   a better timing source, use them */
				master = z;
				best = z->timing;
				newhasmaster = 1;
			}
		}
		z = z->next;
	}
	hasmaster = newhasmaster;
	/* Mark the new master if there is one */
	if (master)
		master->master = 1;
	spin_unlock_irqrestore(&dlock, flags);
	if (master)
		printk("TDMoX: New master: %s\n", master->span.name);
	else
		printk("TDMoX: No master.\n");
}

static void ztd_sendmessage(struct zt_dynamic *z)
{
	unsigned char *buf = z->msgbuf;
	unsigned short bits;
	int msglen = 0;
	int x;
	int offset;

	/* Byte 0: Number of samples per channel */
	*buf = ZT_CHUNKSIZE;
	buf++; msglen++;

	/* Byte 1: Flags */
	*buf = 0;
	if (z->span.alarms & ZT_ALARM_RED)
		*buf |= ZTD_FLAG_YELLOW_ALARM;
	*buf |= ZTD_FLAG_SIGBITS_PRESENT;
	buf++; msglen++;

	/* Bytes 2-3: Transmit counter */
	*((unsigned short *)buf) = htons((unsigned short)z->txcnt);
	z->txcnt++;
	buf++; msglen++;
	buf++; msglen++;

	/* Bytes 4-5: Number of channels */
	*((unsigned short *)buf) = htons((unsigned short)z->span.channels);
	buf++; msglen++;
	buf++; msglen++;
	bits = 0;
	offset = 0;
	for (x=0;x<z->span.channels;x++) {
		offset = x % 4;
		bits |= (z->chans[x].txsig & 0xf) << (offset << 2);
		if (offset == 3) {
			/* Write the bits when we have four channels */
			*((unsigned short *)buf) = htons(bits);
			buf++; msglen++;
			buf++; msglen++;
			bits = 0;
		}
	}

	if (offset != 3) {
		/* Finish it off if it's not done already */
		*((unsigned short *)buf) = htons(bits);
		buf++; msglen++;
		buf++; msglen++;
	}
	
	for (x=0;x<z->span.channels;x++) {
		memcpy(buf, z->chans[x].writechunk, ZT_CHUNKSIZE);
		buf += ZT_CHUNKSIZE;
		msglen += ZT_CHUNKSIZE;
	}
	
	z->driver->transmit(z->pvt, z->msgbuf, msglen);
	
}

static void __ztdynamic_run(void)
{
	unsigned long flags;
	struct zt_dynamic *z;
	struct zt_dynamic_driver *drv;
	int y;
	spin_lock_irqsave(&dlock, flags);
	z = dspans;
	while(z) {
		if (!z->dead) {
			/* Ignore dead spans */
			for (y=0;y<z->span.channels;y++) {
				/* Echo cancel double buffered data */
				zt_ec_chunk(&z->span.chans[y], z->span.chans[y].readchunk, z->span.chans[y].writechunk);
			}
			zt_receive(&z->span);
			zt_transmit(&z->span);
			/* Handle all transmissions now */
			ztd_sendmessage(z);
		}
		z = z->next;
	}
	spin_unlock_irqrestore(&dlock, flags);

	read_lock(&drvlock);
	drv = drivers;
	while(drv) {
		/* Flush any traffic still pending in the driver */
		if (drv->flush) {
			drv->flush();
		}
		drv = drv->next;
	}
	read_unlock(&drvlock);
}

#ifdef ENABLE_TASKLETS
static void ztdynamic_run(void)
{
	if (!taskletpending) {
		taskletpending = 1;
		taskletsched++;
		tasklet_hi_schedule(&ztd_tlet);
	} else {
		txerrors++;
	}
}
#else
#define ztdynamic_run __ztdynamic_run
#endif

void zt_dynamic_receive(struct zt_span *span, unsigned char *msg, int msglen)
{
	struct zt_dynamic *ztd = span->pvt;
	int newerr=0;
	unsigned long flags;
	int sflags;
	int xlen;
	int x, bits, sig;
	int nchans, master;
	int newalarm;
	unsigned short rxpos, rxcnt;
	
	
	spin_lock_irqsave(&dlock, flags);
	if (msglen < 6) {
		spin_unlock_irqrestore(&dlock, flags);
		newerr = ERR_LEN;
		if (newerr != ztd->err) {
			printk("Span %s: Insufficient samples for header (only %d)\n", span->name, msglen);
		}
		ztd->err = newerr;
		return;
	}
	
	/* First, check the chunksize */
	if (*msg != ZT_CHUNKSIZE) {
		spin_unlock_irqrestore(&dlock, flags);
		newerr = ERR_NSAMP | msg[0];
		if (newerr != 	ztd->err) {
			printk("Span %s: Expected %d samples, but receiving %d\n", span->name, ZT_CHUNKSIZE, msg[0]);
		}
		ztd->err = newerr;
		return;
	}
	msg++;
	sflags = *msg;
	msg++;
	
	rxpos = ntohs(*((unsigned short *)msg));
	msg++;
	msg++;
	
	nchans = ntohs(*((unsigned short *)msg));
	if (nchans != span->channels) {
		spin_unlock_irqrestore(&dlock, flags);
		newerr = ERR_NCHAN | nchans;
		if (newerr != ztd->err) {
			printk("Span %s: Expected %d channels, but receiving %d\n", span->name, span->channels, nchans);
		}
		ztd->err = newerr;
		return;
	}
	msg++;
	msg++;
	
	/* Okay now we've accepted the header, lets check our message
	   length... */

	/* Start with header */
	xlen = 6;
	/* Add samples of audio */
	xlen += nchans * ZT_CHUNKSIZE;
	/* If RBS info is there, add that */
	if (sflags & ZTD_FLAG_SIGBITS_PRESENT) {
		/* Account for sigbits -- one short per 4 channels*/
		xlen += ((nchans + 3) / 4) * 2;
	}
	
	if (xlen != msglen) {
		spin_unlock_irqrestore(&dlock, flags);
		newerr = ERR_LEN | xlen;
		if (newerr != ztd->err) {
			printk("Span %s: Expected message size %d, but was %d instead\n", span->name, xlen, msglen);
		}
		ztd->err = newerr;
		return;
	}
	
	bits = 0;
	
	/* Record sigbits if present */
	if (sflags & ZTD_FLAG_SIGBITS_PRESENT) {
		for (x=0;x<nchans;x++) {
			if (!(x%4)) {
				/* Get new bits */
				bits = ntohs(*((unsigned short *)msg));
				msg++;
				msg++;
			}
			
			/* Pick the right bits */
			sig = (bits >> ((x % 4) << 2)) & 0xff;
			
			/* Update signalling if appropriate */
			if (sig != span->chans[x].rxsig)
				zt_rbsbits(&span->chans[x], sig);
				
		}
	}
	
	/* Record data for channels */
	for (x=0;x<nchans;x++) {
		memcpy(span->chans[x].readchunk, msg, ZT_CHUNKSIZE);
		msg += ZT_CHUNKSIZE;
	}

	master = ztd->master;
	
	rxcnt = ztd->rxcnt;
	ztd->rxcnt = rxpos+1;

	spin_unlock_irqrestore(&dlock, flags);
	
	/* Check for Yellow alarm */
	newalarm = span->alarms & ~(ZT_ALARM_YELLOW | ZT_ALARM_RED);
	if (sflags & ZTD_FLAG_YELLOW_ALARM)
		newalarm |= ZT_ALARM_YELLOW;

	if (newalarm != span->alarms) {
		span->alarms = newalarm;
		zt_alarm_notify(span);
		checkmaster();
	}
	
	/* Keep track of last received packet */
	ztd->rxjif = jiffies;

	/* note if we had a missing packet */
	if (rxpos != rxcnt)
		printk("Span %s: Expected seq no %d, but received %d instead\n", span->name, rxcnt, rxpos);

	/* If this is our master span, then run everything */
	if (master)
		ztdynamic_run();
	
}

static void dynamic_destroy(struct zt_dynamic *z)
{
	/* Unregister span if appropriate */
	if (z->span.flags & ZT_FLAG_REGISTERED)
		zt_unregister(&z->span);

	/* Destroy the pvt stuff if there */
	if (z->pvt)
		z->driver->destroy(z->pvt);

	/* Free message buffer if appropriate */
	if (z->msgbuf)
		kfree(z->msgbuf);

	/* Free channels */
	if (z->chans)
		vfree(z->chans);
	/* Free z */
	kfree(z);

	checkmaster();
}

static struct zt_dynamic *find_dynamic(ZT_DYNAMIC_SPAN *zds)
{
	struct zt_dynamic *z;
	z = dspans;
	while(z) {
		if (!strcmp(z->dname, zds->driver) &&
		    !strcmp(z->addr, zds->addr))
			break;
		z = z->next;
	}
	return z;
}

static struct zt_dynamic_driver *find_driver(char *name)
{
	struct zt_dynamic_driver *ztd;
	ztd = drivers;
	while(ztd) {
		/* here's our driver */
		if (!strcmp(name, ztd->name))
			break;
		ztd = ztd->next;
	}
	return ztd;
}

static int destroy_dynamic(ZT_DYNAMIC_SPAN *zds)
{
	unsigned long flags;
	struct zt_dynamic *z, *cur, *prev=NULL;
	spin_lock_irqsave(&dlock, flags);
	z = find_dynamic(zds);
	if (!z) {
		spin_unlock_irqrestore(&dlock, flags);
		return -EINVAL;
	}
	/* Don't destroy span until it is in use */
	if (z->usecount) {
		spin_unlock_irqrestore(&dlock, flags);
		printk("Attempt to destroy dynamic span while it is in use\n");
		return -EBUSY;
	}
	/* Unlink it */
	cur = dspans;
	while(cur) {
		if (cur == z) {
			if (prev)
				prev->next = z->next;
			else
				dspans = z->next;
			break;
		}
		prev = cur;
		cur = cur->next;
	}
	spin_unlock_irqrestore(&dlock, flags);

	/* Destroy it */
	dynamic_destroy(z);
	
	return 0;
}

static int ztd_rbsbits(struct zt_chan *chan, int bits)
{
	/* Don't have to do anything */
	return 0;
}

static int ztd_open(struct zt_chan *chan)
{
	struct zt_dynamic *z;
	z = chan->span->pvt;
	if (z) {
		if (z->dead)
			return -ENODEV;
		z->usecount++;
	}
#ifndef LINUX26
	MOD_INC_USE_COUNT;
#else
	if(!try_module_get(THIS_MODULE))
		printk("TDMoX: Unable to increment module use count\n");
#endif	
	return 0;
}

static int ztd_chanconfig(struct zt_chan *chan, int sigtype)
{
	return 0;
}

static int ztd_close(struct zt_chan *chan)
{
	struct zt_dynamic *z;
	z = chan->span->pvt;
	if (z) 
		z->usecount--;
	if (z->dead && !z->usecount)
		dynamic_destroy(z);
#ifndef LINUX26
	MOD_DEC_USE_COUNT;
#else
	module_put(THIS_MODULE);
#endif	
	return 0;
}

static int create_dynamic(ZT_DYNAMIC_SPAN *zds)
{
	struct zt_dynamic *z;
	struct zt_dynamic_driver *ztd;
	unsigned long flags;
	int x;
	int bufsize;

	if (zds->numchans < 1) {
		printk("Can't be less than 1 channel (%d)!\n", zds->numchans);
		return -EINVAL;
	}
	if (zds->numchans >= ZT_DYNAMIC_MAX_CHANS) {
		printk("Can't create dynamic span with greater than %d channels.  See ztdynamic.c and increase ZT_DYNAMIC_MAX_CHANS\n", zds->numchans);
		return -EINVAL;
	}

	spin_lock_irqsave(&dlock, flags);
	z = find_dynamic(zds);
	spin_unlock_irqrestore(&dlock, flags);
	if (z)
		return -EEXIST;

	/* XXX There is a silly race here.  We check it doesn't exist, but
	       someone could create it between now and then and we'd end up
	       with two of them.  We don't want to hold the spinlock
	       for *too* long though, especially not if there is a possibility
	       of kmalloc.  XXX */


	/* Allocate memory */
	z = (struct zt_dynamic *)kmalloc(sizeof(struct zt_dynamic), GFP_KERNEL);
	if (!z) 
		return -ENOMEM;

	/* Zero it out */
	memset(z, 0, sizeof(struct zt_dynamic));

	/* Allocate other memories */
	z->chans = vmalloc(sizeof(struct zt_chan) * zds->numchans);
	if (!z->chans) {
		dynamic_destroy(z);
		return -ENOMEM;
	}

	/* Zero out channel stuff */
	memset(z->chans, 0, sizeof(struct zt_chan) * zds->numchans);

	/* Allocate message buffer with sample space and header space */
	bufsize = zds->numchans * ZT_CHUNKSIZE + zds->numchans / 4 + 48;

	z->msgbuf = kmalloc(bufsize, GFP_KERNEL);

	if (!z->msgbuf) {
		dynamic_destroy(z);
		return -ENOMEM;
	}
	
	/* Zero out -- probably not needed but why not */
	memset(z->msgbuf, 0, bufsize);

	/* Setup parameters properly assuming we're going to be okay. */
	zap_copy_string(z->dname, zds->driver, sizeof(z->dname));
	zap_copy_string(z->addr, zds->addr, sizeof(z->addr));
	z->timing = zds->timing;
	sprintf(z->span.name, "ZTD/%s/%s", zds->driver, zds->addr);
	sprintf(z->span.desc, "Dynamic '%s' span at '%s'", zds->driver, zds->addr);
	z->span.channels = zds->numchans;
	z->span.pvt = z;
	z->span.deflaw = ZT_LAW_MULAW;
	z->span.flags |= ZT_FLAG_RBS;
	z->span.chans = z->chans;
	z->span.rbsbits = ztd_rbsbits;
	z->span.open = ztd_open;
	z->span.close = ztd_close;
	z->span.chanconfig = ztd_chanconfig;
	for (x=0;x<zds->numchans;x++) {
		sprintf(z->chans[x].name, "ZTD/%s/%s/%d", zds->driver, zds->addr, x+1);
		z->chans[x].sigcap = ZT_SIG_EM | ZT_SIG_CLEAR | ZT_SIG_FXSLS |
				     ZT_SIG_FXSKS | ZT_SIG_FXSGS | ZT_SIG_FXOLS |
				     ZT_SIG_FXOKS | ZT_SIG_FXOGS | ZT_SIG_SF | 
				     ZT_SIG_DACS_RBS | ZT_SIG_CAS;
		z->chans[x].chanpos = x + 1;
		z->chans[x].pvt = z;
	}
	
	spin_lock_irqsave(&dlock, flags);
	ztd = find_driver(zds->driver);
	if (!ztd) {
		/* Try loading the right module */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,70)
		char fn[80];
#endif
		spin_unlock_irqrestore(&dlock, flags);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,70)
		request_module("ztd-%s", zds->driver);
#else
		sprintf(fn, "ztd-%s", zds->driver);
		request_module(fn);
#endif
		spin_lock_irqsave(&dlock, flags);
		ztd = find_driver(zds->driver);
	}
	spin_unlock_irqrestore(&dlock, flags);


	/* Another race -- should let the module get unloaded while we
	   have it here */
	if (!ztd) {
		printk("No such driver '%s' for dynamic span\n", zds->driver);
		dynamic_destroy(z);
		return -EINVAL;
	}

	/* Create the stuff */
	z->pvt = ztd->create(&z->span, z->addr);
	if (!z->pvt) {
		printk("Driver '%s' (%s) rejected address '%s'\n", ztd->name, ztd->desc, z->addr);
		/* Creation failed */
		return -EINVAL;
	}

	/* Remember the driver */
	z->driver = ztd;

	/* Whee!  We're created.  Now register the span */
	if (zt_register(&z->span, 0)) {
		printk("Unable to register span '%s'\n", z->span.name);
		dynamic_destroy(z);
		return -EINVAL;
	}

	/* Okay, created and registered. add it to the list */
	spin_lock_irqsave(&dlock, flags);
	z->next = dspans;
	dspans = z;
	spin_unlock_irqrestore(&dlock, flags);

	checkmaster();

	/* All done */
	return z->span.spanno;

}

#ifdef ENABLE_TASKLETS
static void ztd_tasklet(unsigned long data)
{
	taskletrun++;
	if (taskletpending) {
		taskletexec++;
		__ztdynamic_run();
	}
	taskletpending = 0;
}
#endif

static int ztdynamic_ioctl(unsigned int cmd, unsigned long data)
{
	ZT_DYNAMIC_SPAN zds;
	int res;
	switch(cmd) {
	case 0:
		/* This is called just before rotation.  If none of our
		   spans are pulling timing, then now is the time to process
		   them */
		if (!hasmaster)
			ztdynamic_run();
		return 0;
	case ZT_DYNAMIC_CREATE:
		if (copy_from_user(&zds, (ZT_DYNAMIC_SPAN *)data, sizeof(zds)))
			return -EFAULT;
		if (debug)
			printk("Dynamic Create\n");
		res = create_dynamic(&zds);
		if (res < 0)
			return res;
		zds.spanno = res;
		/* Let them know the new span number */
		if (copy_to_user((ZT_DYNAMIC_SPAN *)data, &zds, sizeof(zds)))
			return -EFAULT;
		return 0;
	case ZT_DYNAMIC_DESTROY:
		if (copy_from_user(&zds, (ZT_DYNAMIC_SPAN *)data, sizeof(zds)))
			return -EFAULT;
		if (debug)
			printk("Dynamic Destroy\n");
		return destroy_dynamic(&zds);
	}

	return -ENOTTY;
}

int zt_dynamic_register(struct zt_dynamic_driver *dri)
{
	unsigned long flags;
	int res = 0;
	write_lock_irqsave(&drvlock, flags);
	if (find_driver(dri->name))
		res = -1;
	else {
		dri->next = drivers;
		drivers = dri;
	}
	write_unlock_irqrestore(&drvlock, flags);
	return res;
}

void zt_dynamic_unregister(struct zt_dynamic_driver *dri)
{
	struct zt_dynamic_driver *cur, *prev=NULL;
	struct zt_dynamic *z, *zp, *zn;
	unsigned long flags;
	write_lock_irqsave(&drvlock, flags);
	cur = drivers;
	while(cur) {
		if (cur == dri) {
			if (prev)
				prev->next = cur->next;
			else
				drivers = cur->next;
			break;
		}
		prev = cur;
		cur = cur->next;
	}
	write_unlock_irqrestore(&drvlock, flags);
	spin_lock_irqsave(&dlock, flags);
	z = dspans;
	zp = NULL;
	while(z) {
		zn = z->next;
		if (z->driver == dri) {
			/* Unlink */
			if (zp)
				zp->next = z->next;
			else
				dspans = z->next;
			if (!z->usecount)
				dynamic_destroy(z);
			else
				z->dead = 1;
		} else {
			zp = z;
		}
		z = zn;
	}
	spin_unlock_irqrestore(&dlock, flags);
}

struct timer_list alarmcheck;

static void check_for_red_alarm(unsigned long ignored)
{
	unsigned long flags;
	int newalarm;
	int alarmchanged = 0;
	struct zt_dynamic *z;
	spin_lock_irqsave(&dlock, flags);
	z = dspans;
	while(z) {
		newalarm = z->span.alarms & ~ZT_ALARM_RED;
		/* If nothing received for a second, consider that RED ALARM */
		if ((jiffies - z->rxjif) > 1 * HZ) {
			newalarm |= ZT_ALARM_RED;
			if (z->span.alarms != newalarm) {
				z->span.alarms = newalarm;
				zt_alarm_notify(&z->span);
				alarmchanged++;
			}
		}
		z = z->next;
	}
	spin_unlock_irqrestore(&dlock, flags);
	if (alarmchanged)
		checkmaster();

	/* Do the next one */
	mod_timer(&alarmcheck, jiffies + 1 * HZ);
	
}

int ztdynamic_init(void)
{
	zt_set_dynamic_ioctl(ztdynamic_ioctl);
	/* Start process to check for RED ALARM */
	init_timer(&alarmcheck);
	alarmcheck.expires = 0;
	alarmcheck.data = 0;
	alarmcheck.function = check_for_red_alarm;
	/* Check once per second */
	mod_timer(&alarmcheck, jiffies + 1 * HZ);
#ifdef ENABLE_TASKLETS
	tasklet_init(&ztd_tlet, ztd_tasklet, 0);
#endif
	printk("Zaptel Dynamic Span support LOADED\n");
	return 0;
}

void ztdynamic_cleanup(void)
{
#ifdef ENABLE_TASKLETS
	if (taskletpending) {
		tasklet_disable(&ztd_tlet);
		tasklet_kill(&ztd_tlet);
	}
#endif
	zt_set_dynamic_ioctl(NULL);
	del_timer(&alarmcheck);
	printk("Zaptel Dynamic Span support unloaded\n");
}

#ifdef LINUX26
module_param(debug, int, 0600);
#else
MODULE_PARM(debug, "i");
#endif
MODULE_DESCRIPTION("Zaptel Dynamic Span Support");
MODULE_AUTHOR("Mark Spencer <markster@digium.com>");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

module_init(ztdynamic_init);
module_exit(ztdynamic_cleanup);
