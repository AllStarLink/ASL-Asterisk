/*
 * Written by Oron Peled <oron@actcom.co.il>
 * Copyright (C) 2004-2006, Xorcom
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
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#ifdef	PROTOCOL_DEBUG
#include <linux/ctype.h>
#endif
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/delay.h>	/* for msleep() to debug */
#include "xpd.h"
#include "xpp_zap.h"
#include "xbus-core.h"
#include "card_global.h"
#ifdef	XPP_DEBUGFS
#include "xpp_log.h"
#endif
#include "zap_debug.h"

static const char rcsid[] = "$Id: xbus-core.c 4474 2008-08-11 14:00:30Z tzafrir $";

/* Defines */
#define	INITIALIZATION_TIMEOUT	(90*HZ)		/* in jiffies */
#define	PROC_XBUSES		"xbuses"
#define	PROC_XBUS_SUMMARY	"summary"
#define	PROC_XBUS_WAITFOR_XPDS	"waitfor_xpds"

#ifdef	PROTOCOL_DEBUG
#define	PROC_XBUS_COMMAND	"command"
static int proc_xbus_command_write(struct file *file, const char __user *buffer, unsigned long count, void *data);
#endif

/* Command line parameters */
extern int debug;
static DEF_PARM(uint, poll_timeout, 1000, 0644, "Timeout (in jiffies) waiting for units to reply");
static DEF_PARM_BOOL(rx_tasklet, 0, 0644, "Use receive tasklets");

static int xbus_read_proc(char *page, char **start, off_t off, int count, int *eof, void *data);
static int xbus_read_waitfor_xpds(char *page, char **start, off_t off, int count, int *eof, void *data);
static void transport_init(xbus_t *xbus, struct xbus_ops *ops, ushort max_send_size, void *priv);
static void transport_destroy(xbus_t *xbus);

/* Data structures */
static spinlock_t		xbuses_lock = SPIN_LOCK_UNLOCKED;
static int			bus_count = 0;
static struct proc_dir_entry	*proc_xbuses = NULL;

static struct xbus_desc {
	xbus_t			*xbus;
	atomic_t		xbus_refcount;
	wait_queue_head_t	can_release_xbus;
} xbuses_array[MAX_BUSES];

static void init_xbus(uint num, xbus_t *xbus)
{
	struct xbus_desc	*desc;

	BUG_ON(num >= ARRAY_SIZE(xbuses_array));
	desc = &xbuses_array[num];
	desc->xbus = xbus;
	atomic_set(&desc->xbus_refcount, 0);
	init_waitqueue_head(&desc->can_release_xbus);
}

static int refcount_xbus(uint num)
{
	BUG_ON(num >= ARRAY_SIZE(xbuses_array));
	return atomic_read(&xbuses_array[num].xbus_refcount);
}

xbus_t *get_xbus(uint num)
{
	struct xbus_desc	*desc;

	if(num >= ARRAY_SIZE(xbuses_array))
		return NULL;
	desc = &xbuses_array[num];
	atomic_inc(&desc->xbus_refcount);
	if(!desc->xbus)
		atomic_dec(&desc->xbus_refcount);
	return desc->xbus;
}

void put_xbus(xbus_t *xbus)
{
	struct xbus_desc	*desc;
	int			num;

	BUG_ON(!xbus);
	num = xbus->num;
	BUG_ON(num >= ARRAY_SIZE(xbuses_array));
	desc = &xbuses_array[num];
	BUG_ON(desc->xbus != xbus);
	if(atomic_dec_and_test(&desc->xbus_refcount)) {
		static int	rate_limit;

		if((rate_limit++ % 1003) == 0)
			XBUS_DBG(DEVICES, xbus,
				"wake_up(can_release_xbus) (%d)\n", rate_limit);
		wake_up(&desc->can_release_xbus);
	}
}

static bool __must_check wait_for_xbus_release(uint xbus_num)
{
	xbus_t	*xbus;
	int	ret;

	xbus = get_xbus(xbus_num);
	if(!xbus) {
		ERR("%s: xbus #%d is already removed. Skip.\n",
			__FUNCTION__, xbus_num);
		return 0;
	}
	put_xbus(xbus);
	DBG(DEVICES, "Waiting... refcount_xbus=%d\n", refcount_xbus(xbus_num));
	ret = wait_event_interruptible(xbuses_array[xbus_num].can_release_xbus,
				refcount_xbus(xbus_num) == 0);
	if(ret) {
		ERR("%s: waiting for xbus #%d interrupted!!!\n",
			__FUNCTION__, xbus_num);
	} else
		DBG(DEVICES, "Waiting for refcount_xbus done.\n");
	return 1;
}

static void initialize_xbuses_array(void)
{
	int	i;

	for(i = 0; i < ARRAY_SIZE(xbuses_array); i++)
		init_xbus(i, NULL);
}

static void finalize_xbuses_array(void)
{
	int	i;

	for(i = 0; i < ARRAY_SIZE(xbuses_array); i++) {
		if(xbuses_array[i].xbus != NULL) {
			ERR("%s: xbus #%d is not NULL\n", __FUNCTION__, i);
			BUG();
		}
	}
}

/*------------------------- Debugfs Handling -----------------------*/
#ifdef	XPP_DEBUGFS

#define DEBUGFS_BUFSIZ		4096	/* must be power of two, otherwise POS_IN_BUF will have to use '%' instead of '&' */
#define POS_IN_BUF(x)		((x) & (DEBUGFS_BUFSIZ-1))

struct debugfs_data {
	spinlock_t lock;
	xbus_t *xbus;
	char buffer[DEBUGFS_BUFSIZ];
	unsigned long head, tail;	/* reading and writing are performed at position (head % BUF_SIZ) and (tail % BUF_SIZ) */
	wait_queue_head_t queue;
};

static unsigned long add_to_buf(struct debugfs_data *d, unsigned long tail, const void *buf, unsigned long len)
{
	unsigned long count = min(len, (unsigned long)(DEBUGFS_BUFSIZ - POS_IN_BUF(tail)));
	memcpy(d->buffer + POS_IN_BUF(tail), buf, count);		/* fill starting at position tail */
	memcpy(d->buffer, (u_char *)buf + count, len - count);		/* fill leftover */
	return len;
}

int xbus_log(xbus_t *xbus, xpd_t *xpd, int direction, const void *buf, unsigned long len)
{
	unsigned long tail;
	unsigned long flags;
	struct debugfs_data *d;
	struct log_header header;
	int ret = 0;
	
	BUG_ON(!xbus);
	BUG_ON(!xpd);
	BUG_ON(sizeof(struct log_header) + len > DEBUGFS_BUFSIZ);
	d = xbus->debugfs_data;
	if (!d)			/* no consumer process */
		return ret;
	spin_lock_irqsave(&d->lock, flags);
	if (sizeof(struct log_header) + len > DEBUGFS_BUFSIZ - (d->tail - d->head)) {
		ret = -ENOSPC;
		XPD_DBG(GENERAL, xpd, "Dropping debugfs data of len %lu, free space is %lu\n", sizeof(struct log_header) + len,
				DEBUGFS_BUFSIZ - (d->tail - d->head));
		goto out;
	}
	header.len = sizeof(struct log_header) + len;
	header.time = jiffies_to_msecs(jiffies);
	header.xpd_num = xpd->xbus_idx;
	header.direction = (char)direction;
	tail = d->tail;
	tail += add_to_buf(d, tail, &header, sizeof(header));
	tail += add_to_buf(d, tail, buf, len);
	d->tail = tail;
	wake_up_interruptible(&d->queue);
out:
	spin_unlock_irqrestore(&d->lock, flags);
	return ret;
}

static struct dentry	*debugfs_root = NULL;
static int debugfs_open(struct inode *inode, struct file *file);
static ssize_t debugfs_read(struct file *file, char __user *buf, size_t nbytes, loff_t *ppos);
static int debugfs_release(struct inode *inode, struct file *file);

static struct file_operations debugfs_operations = {
	.open		= debugfs_open,
	.read		= debugfs_read,
	.release	= debugfs_release,
};

/*
 * As part of the "inode diet" the private data member of struct inode
 * has changed in 2.6.19. However, Fedore Core 6 adopted this change
 * a bit earlier (2.6.18). If you use such a kernel, Change the 
 * following test from 2,6,19 to 2,6,18.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
#define	I_PRIVATE(inode)	((inode)->u.generic_ip)
#else
#define	I_PRIVATE(inode)	((inode)->i_private)
#endif

static int debugfs_open(struct inode *inode, struct file *file)
{
	xbus_t	*xbus = I_PRIVATE(inode);
	struct debugfs_data *d;
	struct log_global_header gheader;

	BUG_ON(!xbus);
	XBUS_DBG(GENERAL, xbus, "\n");
	if (xbus->debugfs_data)
		return -EBUSY;
	d = KZALLOC(sizeof(struct debugfs_data), GFP_KERNEL);
	if (!d)
		return -ENOMEM;
	try_module_get(THIS_MODULE);
	spin_lock_init(&d->lock);
	d->xbus = xbus;
	d->head = d->tail = 0;
	init_waitqueue_head(&d->queue);
	file->private_data = d;

	gheader.magic = XPP_LOG_MAGIC;
	gheader.version = 1;
	d->tail += add_to_buf(d, d->tail, &gheader, sizeof(gheader));

	xbus->debugfs_data = d;
	return 0;
}

static ssize_t debugfs_read(struct file *file, char __user *buf, size_t nbytes, loff_t *ppos)
{
	struct debugfs_data *d = file->private_data;
	size_t len;

	BUG_ON(!d);
	BUG_ON(!d->xbus);
	XBUS_DBG(GENERAL, d->xbus, "\n");
	while (d->head == d->tail) {
		if (wait_event_interruptible(d->queue, d->head != d->tail))
			return -EAGAIN;
	}
	len = min(nbytes, (size_t)(d->tail - d->head));
	if (copy_to_user(buf, d->buffer + POS_IN_BUF(d->head), len))
		return -EFAULT;
	d->head += len;
	/* optimization to avoid future buffer wraparound */
	if (d->head == d->tail) {
		unsigned long flags;
		spin_lock_irqsave(&d->lock, flags);
		if (d->head == d->tail)
			d->head = d->tail = 0;
		spin_unlock_irqrestore(&d->lock, flags);
	}
	return len;
}

static int debugfs_release(struct inode *inode, struct file *file)
{
	struct debugfs_data *d = file->private_data;

	BUG_ON(!d);
	BUG_ON(!d->xbus);
	XBUS_DBG(GENERAL, d->xbus, "\n");
	d->xbus->debugfs_data = NULL;
	kfree(d);
	module_put(THIS_MODULE);
	return 0;
}
#endif

/*------------------------- Frame  Handling ------------------------*/

void xframe_init(xbus_t *xbus, xframe_t *xframe, void *buf, size_t maxsize, void *priv)
{
	memset(xframe, 0, sizeof(*xframe));
	INIT_LIST_HEAD(&xframe->frame_list);
	xframe->priv = priv;
	xframe->xbus = xbus;
	xframe->packets = xframe->first_free = buf;
	xframe->frame_maxlen = maxsize;
	atomic_set(&xframe->frame_len, 0);
	do_gettimeofday(&xframe->tv_created);
	xframe->xframe_magic = XFRAME_MAGIC;
}

/*
 * Return pointer to next packet slot in the frame
 * or NULL if the frame is full.
 *
 * FIXME: we do not use atomic_add_return() because kernel-2.6.8
 *        does not have it. This make this code a little racy,
 *        but we currently call xframe_next_packet() only in the
 *        PCM loop (xbus_tick() etc.)
 */
xpacket_t *xframe_next_packet(xframe_t *frm, int len)
{
	int newlen = XFRAME_LEN(frm);

	newlen += len;
//	DBG(GENERAL, "len=%d, newlen=%d, frm->frame_len=%d\n", len, newlen, XFRAME_LEN(frm));
	if (newlen > XFRAME_DATASIZE) {
		return NULL;
	}
	atomic_add(len, &frm->frame_len);
	return (xpacket_t *)(frm->packets + newlen - len);
}

static spinlock_t serialize_dump_xframe = SPIN_LOCK_UNLOCKED;

static void do_hexdump(const char msg[], byte *data, uint16_t len)
{
	int	i;
	int	debug = DBG_ANY;	/* mask global debug */

	for(i = 0; i < len; i++)
		DBG(ANY, "%s: %3d> %02X\n", msg, i, data[i]);
}

void dump_xframe(const char msg[], const xbus_t *xbus, const xframe_t *xframe, int debug)
{
	const uint16_t	frm_len = XFRAME_LEN(xframe);
	xpacket_t	*pack;
	uint16_t	pos = 0;
	uint16_t	nextpos;
	int 		num = 1;
	bool		do_print;
	unsigned long	flags;
	
	if(xframe->xframe_magic != XFRAME_MAGIC) {
		XBUS_ERR(xbus, "%s: bad xframe_magic %lX\n",
			__FUNCTION__, xframe->xframe_magic);
		return;
	}
	spin_lock_irqsave(&serialize_dump_xframe, flags);
	do {
		if(pos >= xbus->transport.max_send_size) {
			if(printk_ratelimit()) {
				XBUS_NOTICE(xbus, "%s: xframe overflow (%d bytes)\n",
				    msg, frm_len);
				do_hexdump(msg, xframe->packets, frm_len);
			}
			break;
		}
		if(pos > frm_len) {
			if(printk_ratelimit()) {
				XBUS_NOTICE(xbus, "%s: packet overflow pos=%d frame_len=%d\n",
				    msg, pos, frm_len);
				do_hexdump(msg, xframe->packets, frm_len);
			}
			break;
		}
		pack = (xpacket_t *)&xframe->packets[pos];
		if(XPACKET_LEN(pack) <= 0) {
			if(printk_ratelimit()) {
				XBUS_NOTICE(xbus, "%s: xframe -- bad packet_len=%d pos=%d frame_len=%d\n",
				    msg, XPACKET_LEN(pack), pos, frm_len);
				do_hexdump(msg, xframe->packets, frm_len);
			}
			break;
		}
		nextpos = pos + XPACKET_LEN(pack);
		if(nextpos > frm_len) {
			if(printk_ratelimit()) {
				XBUS_NOTICE(xbus, "%s: packet overflow nextpos=%d frame_len=%d\n",
				    msg, nextpos, frm_len);
				do_hexdump(msg, xframe->packets, frm_len);
			}
			break;
		}
		do_print = 0;
		if(debug == DBG_ANY)
			do_print = 1;
		else if(XPACKET_OP(pack) != XPROTO_NAME(GLOBAL,PCM_READ) &&
			XPACKET_OP(pack) != XPROTO_NAME(GLOBAL,PCM_WRITE))
			do_print = 1;
		else if(debug & DBG_PCM) {
			static int	rate_limit;

			if((rate_limit++ % 1003) == 0)
				do_print = 1;
		}
		if(do_print) {
			if(num == 1) {
				XBUS_DBG(ANY, xbus, "%s: frame_len=%d. %s\n",
						msg, frm_len,
						(XPACKET_IS_PCM(pack))
							? "(IS_PCM)"
							: "");
			}
			XBUS_DBG(ANY, xbus, "  %3d. DATALEN=%d pcm=%d slot=%d OP=0x%02X XPD-%d%d (pos=%d)\n",
				num, XPACKET_LEN(pack),
				XPACKET_IS_PCM(pack), XPACKET_PCMSLOT(pack),
				XPACKET_OP(pack),
				XPACKET_ADDR_UNIT(pack), XPACKET_ADDR_SUBUNIT(pack),
				pos);
			dump_packet("     ", pack, debug);
		}
		num++;
		pos = nextpos;
		if(pos >= frm_len)
			break;
	} while(1);
	spin_unlock_irqrestore(&serialize_dump_xframe, flags);
}

static bool xbus_ready(const xbus_t *xbus, const char msg[])
{
	if(!xbus) {
		ERR("null xbus: %s\n", msg);
		return 0;
	}
	if (!TRANSPORT_RUNNING(xbus)) {
		XBUS_ERR(xbus, "%s -- hardware is not ready.", msg);
		return 0;
	}
	if(!xbus->transport.ops) {
		XBUS_ERR(xbus, "%s -- hardware is gone.", msg);
		return 0;
	}
	return 1;
}

/**
 *
 * Frame is freed:
 * 	- In case of error, by this function.
 * 	- Otherwise, by the underlying sending mechanism
 */
int send_pcm_frame(xbus_t *xbus, xframe_t *xframe)
{
	struct xbus_ops	*ops;
	int		ret = -ENODEV;

	BUG_ON(!xframe);
	if(!xbus_ready(xbus, "Dropped a pcm frame")) {
		ret = -ENODEV;
		goto error;
	}
	ops = transportops_get(xbus);
	BUG_ON(!ops);
	ret = ops->xframe_send_pcm(xbus, xframe);
	transportops_put(xbus);
	if(ret)
		XBUS_COUNTER(xbus, TX_BYTES) += XFRAME_LEN(xframe);
	return ret;

error:	
	FREE_SEND_XFRAME(xbus, xframe);
	return ret;
}

static int really_send_cmd_frame(xbus_t *xbus, xframe_t *xframe)
{
	struct xbus_ops	*ops;
	int		ret;

	BUG_ON(!xbus);
	BUG_ON(!xframe);
	BUG_ON(xframe->xframe_magic != XFRAME_MAGIC);
	if(!xbus_ready(xbus, "Dropped command before sending")) {
		FREE_SEND_XFRAME(xbus, xframe);
		return -ENODEV;
	}
	ops = transportops_get(xbus);
	BUG_ON(!ops);
	if(debug & DBG_COMMANDS)
		dump_xframe("TX-CMD", xbus, xframe, DBG_ANY);
	ret = ops->xframe_send_cmd(xbus, xframe);
	transportops_put(xbus);
	if(ret == 0) {
		XBUS_COUNTER(xbus, TX_CMD)++;
		XBUS_COUNTER(xbus, TX_BYTES) += XFRAME_LEN(xframe);
	}
	return ret;
}

int xbus_command_queue_tick(xbus_t *xbus)
{
	xframe_t	*frm;
	int		ret = 0;

	frm = xframe_dequeue(&xbus->command_queue);
	if(frm) {
		BUG_ON(frm->xframe_magic != XFRAME_MAGIC);
		ret = really_send_cmd_frame(xbus, frm);
		if(ret < 0)
			XBUS_ERR(xbus,
				"Failed to send from command_queue (ret=%d)\n",
				ret);
		XBUS_PUT(xbus);
	} else
		wake_up(&xbus->command_queue_empty);
	return ret;
}

static void xbus_command_queue_clean(xbus_t *xbus)
{
	xframe_t	*frm;

	XBUS_DBG(DEVICES, xbus, "count=%d\n", xbus->command_queue.count);
	xframe_queue_disable(&xbus->command_queue);
	while((frm = xframe_dequeue(&xbus->command_queue)) != NULL) {
		FREE_SEND_XFRAME(xbus, frm);
		XBUS_PUT(xbus);
	}
}

static int xbus_command_queue_waitempty(xbus_t *xbus)
{
	int		ret;

	XBUS_DBG(DEVICES, xbus, "Waiting for command_queue to empty\n");
	ret = wait_event_interruptible(xbus->command_queue_empty,
				xframe_queue_count(&xbus->command_queue) == 0);
	if(ret) {
		XBUS_ERR(xbus, "waiting for command_queue interrupted!!!\n");
	}
	return ret;
}

int send_cmd_frame(xbus_t *xbus, xframe_t *xframe)
{
	static int	rate_limit;
	int		ret = 0;


	BUG_ON(xframe->xframe_magic != XFRAME_MAGIC);
	if(!xbus_ready(xbus, "Dropped command before queueing")) {
		ret = -ENODEV;
		goto err;
	}
	if(!XBUS_GET(xbus)) {
		/* shutting down */
		ret = -ENODEV;
		goto err;
	}
	if(!xframe_enqueue(&xbus->command_queue, xframe)) {
		if((rate_limit++ % 1003) == 0) {
			XBUS_ERR(xbus,
				"Dropped command xframe. Cannot enqueue (%d)\n",
				rate_limit);
			dump_xframe("send_cmd_frame", xbus, xframe, DBG_ANY);
		}
		XBUS_PUT(xbus);
		ret = -E2BIG;
		goto err;
	}
	return 0;
err:
	FREE_SEND_XFRAME(xbus, xframe);
	return ret;
}

/*------------------------- Receive Tasklet Handling ---------------*/

static void xframe_enqueue_recv(xbus_t *xbus, xframe_t *xframe)
{
	int	cpu = smp_processor_id();

	BUG_ON(!xbus);
	xbus->cpu_rcv_intr[cpu]++;
	if(!xframe_enqueue(&xbus->receive_queue, xframe)) {
		static int	rate_limit;

		if((rate_limit++ % 1003) == 0)
			XBUS_ERR(xbus, "Failed to enqueue for receive_tasklet (%d)\n", rate_limit);
		FREE_RECV_XFRAME(xbus, xframe);	/* return to receive_pool */
		return;
	}
	tasklet_schedule(&xbus->receive_tasklet);
}

/*
 * process frames in the receive_queue in a tasklet
 */
static void receive_tasklet_func(unsigned long data)
{
	xbus_t		*xbus = (xbus_t *)data;
	xframe_t	*xframe = NULL;
	int		cpu = smp_processor_id();

	BUG_ON(!xbus);
	xbus->cpu_rcv_tasklet[cpu]++;
	while((xframe = xframe_dequeue(&xbus->receive_queue)) != NULL) {
		xframe_receive(xbus, xframe);
	}
}

void xbus_receive_xframe(xbus_t *xbus, xframe_t *xframe)
{
	BUG_ON(!xbus);
	if(rx_tasklet) {
		xframe_enqueue_recv(xbus, xframe);
	} else {
		if (likely(TRANSPORT_RUNNING(xbus)))
			xframe_receive(xbus, xframe);
		else
			FREE_RECV_XFRAME(xbus, xframe);	/* return to receive_pool */
	}
}

/*------------------------- Bus Management -------------------------*/
xpd_t	*xpd_of(const xbus_t *xbus, int xpd_num)
{
	if(!VALID_XPD_NUM(xpd_num))
		return NULL;
	return xbus->xpds[xpd_num];
}

xpd_t	*xpd_byaddr(const xbus_t *xbus, uint unit, uint subunit)
{
	if(unit > MAX_UNIT || subunit > MAX_SUBUNIT)
		return NULL;
	return xbus->xpds[XPD_IDX(unit,subunit)];
}

int xbus_register_xpd(xbus_t *xbus, xpd_t *xpd)
{
	unsigned int	xpd_num = xpd->xbus_idx;
	unsigned long	flags;
	int		ret = 0;

	xbus = get_xbus(xbus->num);	/* until unregister */
	BUG_ON(!xbus);
	XBUS_DBG(DEVICES, xbus, "XPD #%d (xbus_refcount=%d)\n",
		xpd_num, refcount_xbus(xbus->num));
	spin_lock_irqsave(&xbus->lock, flags);
	if(!VALID_XPD_NUM(xpd_num)) {
		XBUS_ERR(xbus, "Bad xpd_num = %d\n", xpd_num);
		ret = -EINVAL;
		goto out;
	}
	if(xbus->xpds[xpd_num] != NULL) {
		xpd_t	*other = xbus->xpds[xpd_num];

		XBUS_ERR(xbus, "xpd_num=%d is occupied by %p (%s)\n",
				xpd_num, other, other->xpdname);
		ret = -EINVAL;
		goto out;
	}
	xbus->xpds[xpd_num] = xpd;
	xpd->xbus = xbus;
	xbus->num_xpds++;
out:
	spin_unlock_irqrestore(&xbus->lock, flags);
	return ret;
}

int xbus_unregister_xpd(xbus_t *xbus, xpd_t *xpd)
{
	unsigned int	xpd_num = xpd->xbus_idx;
	unsigned long	flags;
	int		ret = -EINVAL;

	spin_lock_irqsave(&xbus->lock, flags);
	XBUS_DBG(DEVICES, xbus, "XPD #%d (xbus_refcount=%d)\n",
		xpd_num, refcount_xbus(xbus->num));
	if(!VALID_XPD_NUM(xpd_num)) {
		XBUS_ERR(xbus, "%s: Bad xpd_num = %d\n", __FUNCTION__, xpd_num);
		goto out;
	}
	if(xbus->xpds[xpd_num] == NULL) {
		XBUS_ERR(xbus, "%s: slot xpd_num=%d is empty\n", __FUNCTION__, xpd_num);
		goto out;
	}
	if(xbus->xpds[xpd_num] != xpd) {
		xpd_t	*other = xbus->xpds[xpd_num];

		XBUS_ERR(xbus, "%s: slot xpd_num=%d is occupied by %p (%s)\n",
				__FUNCTION__, xpd_num, other, other->xpdname);
		goto out;
	}
	xbus->xpds[xpd_num] = NULL;
	xbus->num_xpds--;
	xpd->xbus = NULL;
	put_xbus(xbus);		/* we got it in xbus_register_xpd() */
	ret = 0;
out:
	spin_unlock_irqrestore(&xbus->lock, flags);
	return ret;
}

/*
 * Called with xbus->worker locked.
 */
static int new_card(xbus_t *xbus,
		int unit,
		byte type,
		byte subtype,
		byte numchips,
		byte ports_per_chip,
		byte ports,
		byte port_dir)
{
	const xproto_table_t	*proto_table;
	const xops_t		*xops;
	int			i;
	int			subunits;
	int			ret = 0;

	proto_table = xproto_get(type);
	if(!proto_table) {
		XBUS_NOTICE(xbus,
			"CARD %d: missing protocol table for type %d. Ignored.\n",
			unit, type);
		return -EINVAL;
	}
	subunits = (ports + proto_table->ports_per_subunit - 1) /
			proto_table->ports_per_subunit;
	XBUS_DBG(DEVICES, xbus, "CARD %d type=%d.%d ports=%d (%dx%d), %d subunits, port-dir=0x%02X\n",
			unit,
			type,
			subtype,
			ports,
			numchips,
			ports_per_chip,
			subunits,
			port_dir
		);
	xops = &proto_table->xops;
	BUG_ON(!xops);
	xbus->worker->num_units += subunits - 1;
	for(i = 0; i < subunits; i++) {
		if(!TRANSPORT_RUNNING(xbus)) {
			ret = -ENODEV;
			goto out;
		}
		XBUS_DBG(DEVICES, xbus, "Creating XPD=%d%d type=%d.%d\n",
				unit,
				i,
				type,
				subtype);
		if(!XBUS_GET(xbus)) {
			XBUS_ERR(xbus, "Aborting creation. Is shutting down.\n");
			ret = -ENODEV;
			goto out;
		}
		ret = create_xpd(xbus, proto_table, unit, i, type, subtype, subunits, port_dir);
		XBUS_PUT(xbus);
		if(ret < 0) {
			XBUS_ERR(xbus, "Creation of XPD=%d%d failed %d\n",
				unit, i, ret);
			goto out;
		}
		xbus->worker->num_units_initialized++;
	}
out:
	xproto_put(proto_table);	/* ref count is inside the xpds now */
	return ret;
}

static int xbus_initialize(xbus_t *xbus)
{
	int	unit;
	int	subunit;
	xpd_t	*xpd;

	for(unit = 0; unit < MAX_UNIT; unit++) {
		xpd = xpd_byaddr(xbus, unit, 0);
		if(!xpd)
			continue;
		if(run_initialize_registers(xpd) < 0) {
			XPD_ERR(xpd, "Register Initialization failed\n");
			goto err;
		}
		for(subunit = 0; subunit < MAX_SUBUNIT; subunit++) {
			xpd = xpd_byaddr(xbus, unit, subunit);
			if(!xpd)
				continue;
			if(CALL_XMETHOD(card_init, xpd->xbus, xpd) < 0) {
				XPD_ERR(xpd, "Card Initialization failed\n");
				goto err;
			}
			//CALL_XMETHOD(XPD_STATE, xpd->xbus, xpd, 0);	/* Turn off all channels */
			xpd->card_present = 1;
			CALL_XMETHOD(XPD_STATE, xpd->xbus, xpd, 1);		/* Turn on all channels */
			XPD_INFO(xpd, "Initialized: %s\n", xpd->type_name);
			xpd_post_init(xpd);
		}
	}
	return 0;
err:
	for(unit = 0; unit < MAX_UNIT; unit++) {
		for(subunit = 0; subunit < MAX_SUBUNIT; subunit++) {
			xpd = xpd_byaddr(xbus, unit, subunit);
			if(!xpd)
				xpd_free(xpd);
		}
	}
	return -EINVAL;
}

/*
 * This must be called from synchronous (non-interrupt) context
 * it returns only when all XPD's on the bus are detected and
 * initialized.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
void xbus_populate(struct work_struct *work)
{
	struct xbus_workqueue	*worker = container_of(work, struct xbus_workqueue, xpds_init_work);
#else
void xbus_populate(void *data)
{
	struct xbus_workqueue	*worker = data;
#endif
	xbus_t			*xbus;
	struct list_head	*card;
	struct list_head	*next_card;
	unsigned long		flags;
	int			ret = 0;

	xbus = worker->xbus;
	if(!XBUS_GET(xbus)) {
		XBUS_NOTICE(xbus, "Shutting down, aboring initialization\n");
		return;
	}
	spin_lock_irqsave(&worker->worker_lock, flags);
	list_for_each_safe(card, next_card, &worker->card_list) {
		struct card_desc_struct	*card_desc = list_entry(card, struct card_desc_struct, card_list);

		list_del(card);
		BUG_ON(card_desc->magic != CARD_DESC_MAGIC);
		/* Release/Reacquire locks around blocking calls */
		spin_unlock_irqrestore(&xbus->worker->worker_lock, flags);
		ret = new_card(xbus,
			card_desc->xpd_addr.unit,
			card_desc->type,
			card_desc->subtype,
			card_desc->numchips,
			card_desc->ports_per_chip,
			card_desc->ports,
			card_desc->port_dir);
		spin_lock_irqsave(&xbus->worker->worker_lock, flags);
		KZFREE(card_desc);
		if(ret)
			break;
	}
	spin_unlock_irqrestore(&worker->worker_lock, flags);
	xbus_initialize(xbus);
	worker->xpds_init_done = 1;
	ret = xbus_sysfs_create(xbus);
	if(ret) {
		XBUS_ERR(xbus, "SYSFS creation failed: %d\n", ret);
	}
	wake_up(&worker->wait_for_xpd_initialization);
	/*
	 * Now request Astribank to start self_ticking.
	 * This is the last initialization command. So
	 * all others will reach the device before it.
	 */
	xbus_request_sync(xbus, SYNC_MODE_PLL);
	elect_syncer("xbus_poll(end)");	/* FIXME: try to do it later */
	put_xbus(xbus);	/* taken in AB_DESCRIPTION */
	XBUS_PUT(xbus);
}

static void worker_destroy(struct xbus_workqueue *worker)
{
	xbus_t			*xbus;
	struct list_head	*card;
	struct list_head	*next_card;
	unsigned long		flags;

	if(!worker)
		return;
	spin_lock_irqsave(&worker->worker_lock, flags);
	xbus = worker->xbus;
	list_for_each_safe(card, next_card, &worker->card_list) {
		struct card_desc_struct	*card_desc = list_entry(card, struct card_desc_struct, card_list);

		BUG_ON(card_desc->magic != CARD_DESC_MAGIC);
		list_del(card);
		kfree(card_desc);
	}
	spin_unlock_irqrestore(&worker->worker_lock, flags);
	if(xbus) {
#ifdef CONFIG_PROC_FS
		if(xbus->proc_xbus_dir && worker->proc_xbus_waitfor_xpds) {
			XBUS_DBG(PROC, xbus, "Removing proc '%s'\n", PROC_XBUS_WAITFOR_XPDS);
			remove_proc_entry(PROC_XBUS_WAITFOR_XPDS, xbus->proc_xbus_dir);
			worker->proc_xbus_waitfor_xpds = NULL;
		}
#endif
		XBUS_DBG(DEVICES, xbus, "detach worker\n");
		xbus->worker = NULL;
	}
	if (worker->wq) {
		DBG(DEVICES, "XBUS #%d: destroy workqueue\n", worker->xbus->num);
		flush_workqueue(worker->wq);
		destroy_workqueue(worker->wq);
		worker->wq = NULL;
	}
	put_xbus(xbus);	/* Taken in worker_new() */
	KZFREE(worker);
}

/*
 * Allocate a worker for the xbus including the nessessary workqueue.
 * May call blocking operations, but only briefly (as we are called
 * from xbus_new() which is called from khubd.
 */
static struct xbus_workqueue *worker_new(int xbus_num)
{
	struct xbus_workqueue	*worker;
	xbus_t			*xbus;

	xbus = get_xbus(xbus_num);		/* release in worker_destroy */
	BUG_ON(xbus->busname[0] == '\0');	/* No name? */
	BUG_ON(xbus->worker != NULL);			/* Hmmm... nested workers? */
	XBUS_DBG(DEVICES, xbus, "\n");
	worker = KZALLOC(sizeof(*worker), GFP_KERNEL);
	if(!worker)
		goto err;
	worker->xbus = xbus;
	/* poll related variables */
	spin_lock_init(&worker->worker_lock);
	INIT_LIST_HEAD(&worker->card_list);
	init_waitqueue_head(&worker->wait_for_xpd_initialization);
	worker->wq = create_singlethread_workqueue(xbus->busname);
	if(!worker->wq) {
		XBUS_ERR(xbus, "Failed to create worker workqueue.\n");
		goto err;
	}
#ifdef CONFIG_PROC_FS
	if(xbus->proc_xbus_dir) {
		worker->proc_xbus_waitfor_xpds = create_proc_read_entry(
				PROC_XBUS_WAITFOR_XPDS, 0444,
				xbus->proc_xbus_dir,
				xbus_read_waitfor_xpds,
				xbus);
		if (!worker->proc_xbus_waitfor_xpds) {
			XBUS_ERR(xbus, "Failed to create proc file '%s'\n", PROC_XBUS_WAITFOR_XPDS);
			goto err;
		}
		worker->proc_xbus_waitfor_xpds->owner = THIS_MODULE;
	}
#endif
	return worker;
err:
	worker_destroy(worker);
	return NULL;
}

int xbus_activate(xbus_t *xbus)
{
	struct xbus_ops		*ops;
	struct xbus_workqueue	*worker;

	BUG_ON(!xbus);
	ops = transportops_get(xbus);
	BUG_ON(!ops);
	worker = xbus->worker;
	BUG_ON(!worker);
	/* Sanity checks */
	BUG_ON(!ops->xframe_send_pcm);
	BUG_ON(!ops->xframe_send_cmd);
	BUG_ON(!ops->alloc_xframe);
	BUG_ON(!ops->free_xframe);
	xpp_drift_init(xbus);
	/*
	 * We start with timer based ticking
	 */
	xbus_set_command_timer(xbus, 1);
	xbus->transport.transport_running = 1;	/* must be done after transport is valid */
	XBUS_INFO(xbus, "[%s] Activating\n", xbus->label);
	/*
	 * Make sure Astribank knows not to send us ticks.
	 */
	xbus_request_sync(xbus, SYNC_MODE_NONE);
	CALL_PROTO(GLOBAL, AB_REQUEST, xbus, NULL);
	return 0;
}

void xbus_disconnect(xbus_t *xbus)
{
	int	i;

	BUG_ON(!xbus);
	XBUS_INFO(xbus, "[%s] Disconnecting\n", xbus->label);
	xbus_set_command_timer(xbus, 1);
	xbus_request_sync(xbus, SYNC_MODE_NONE);	/* no more ticks */
	xbus_sysfs_remove(xbus);	/* Device-Model */
	for(i = 0; i < MAX_XPDS; i++) {
		xpd_t *xpd = xpd_of(xbus, i);
		if(!xpd)
			continue;
		if(xpd->xbus_idx != i) {
			XBUS_ERR(xbus, "BUG: xpd->xbus_idx=%d != i=%d\n", xpd->xbus_idx, i);
			continue;
		}
		xpd_disconnect(xpd);
	}
	XBUS_DBG(DEVICES, xbus, "Deactivating\n");
	tasklet_kill(&xbus->receive_tasklet);
	xframe_queue_clear(&xbus->receive_queue);
	xbus_command_queue_clean(xbus);
	xbus_command_queue_waitempty(xbus);
	del_timer_sync(&xbus->command_timer);
	xframe_queue_clear(&xbus->send_pool);
	xframe_queue_clear(&xbus->receive_pool);
	xframe_queue_clear(&xbus->pcm_tospan);
	transportops_put(xbus);
	transport_destroy(xbus);
	elect_syncer("disconnect");
	XBUS_DBG(DEVICES, xbus, "Deactivated (refcount_xbus=%d)\n", refcount_xbus(xbus->num));
	if(atomic_dec_and_test(&xbus->xbus_ref_count)) {
		XBUS_DBG(DEVICES, xbus, "Going to remove XBUS\n");
		xbus_remove(xbus);
	}
}

static xbus_t *xbus_alloc(void)
{
	unsigned long	flags;
	xbus_t	*xbus;
	int	i;

	xbus = KZALLOC(sizeof(xbus_t), GFP_KERNEL);
	if(!xbus) {
		ERR("%s: out of memory\n", __FUNCTION__);
		return NULL;
	}
	spin_lock_irqsave(&xbuses_lock, flags);
	for(i = 0; i < MAX_BUSES; i++)
		if(xbuses_array[i].xbus == NULL)
			break;
	if(i >= MAX_BUSES) {
		ERR("%s: No free slot for new bus. i=%d\n", __FUNCTION__, i);
		kfree(xbus);
		xbus = NULL;
		goto out;
	}
	/* Found empty slot */
	xbus->num = i;
	init_xbus(i, xbus);
	xbus = get_xbus(i);
	bus_count++;
out:
	spin_unlock_irqrestore(&xbuses_lock, flags);
	return xbus;
}


static void xbus_free(xbus_t *xbus)
{
	unsigned long	flags;
	uint		num;

	if(!xbus)
		return;
	spin_lock_irqsave(&xbuses_lock, flags);
	num = xbus->num;
	BUG_ON(!xbuses_array[num].xbus);
	BUG_ON(xbus != xbuses_array[num].xbus);
	spin_unlock_irqrestore(&xbuses_lock, flags);
#ifdef	XPP_DEBUGFS
	if(xbus->debugfs_dir) {
		if(xbus->debugfs_file) {
			XBUS_DBG(GENERAL, xbus, "Removing debugfs file\n");
			debugfs_remove(xbus->debugfs_file);
		}
		XBUS_DBG(GENERAL, xbus, "Removing debugfs directory\n");
		debugfs_remove(xbus->debugfs_dir);
	}
#endif
#ifdef CONFIG_PROC_FS
	if(xbus->proc_xbus_dir) {
		if(xbus->proc_xbus_summary) {
			XBUS_DBG(PROC, xbus, "Removing proc '%s'\n", PROC_XBUS_SUMMARY);
			remove_proc_entry(PROC_XBUS_SUMMARY, xbus->proc_xbus_dir);
			xbus->proc_xbus_summary = NULL;
		}
#ifdef	PROTOCOL_DEBUG
		if(xbus->proc_xbus_command) {
			XBUS_DBG(PROC, xbus, "Removing proc '%s'\n", PROC_XBUS_COMMAND);
			remove_proc_entry(PROC_XBUS_COMMAND, xbus->proc_xbus_dir);
			xbus->proc_xbus_command = NULL;
		}
#endif
		XBUS_DBG(PROC, xbus, "Removing proc directory\n");
		remove_proc_entry(xbus->busname, xpp_proc_toplevel);
		xbus->proc_xbus_dir = NULL;
	}
#endif
	spin_lock_irqsave(&xbuses_lock, flags);
	/*
	 * Return to xbus reference counts:
	 *  - One from our caller: transport disconnect or xpp_close()
	 *  - One from xbus_alloc()
 	 */
	put_xbus(xbus);
	put_xbus(xbus);
	if(!wait_for_xbus_release(xbus->num))
		BUG();	/* Let's see what happens next... */
	bus_count--;
	XBUS_DBG(DEVICES, xbus, "Going to free... refcount_xbus=%d\n", refcount_xbus(num));
	BUG_ON(refcount_xbus(num) != 0);
	init_xbus(num, NULL);
	spin_unlock_irqrestore(&xbuses_lock, flags);
	KZFREE(xbus);
}

xbus_t *xbus_new(struct xbus_ops *ops, ushort max_send_size, void *priv)
{
	int			err;
	xbus_t			*xbus = NULL;

	BUG_ON(!ops);
	XBUS_DBG(GENERAL, xbus, "allocate new xbus\n");
	xbus = xbus_alloc();
	if(!xbus)
		return NULL;
	transport_init(xbus, ops, max_send_size, priv);
	spin_lock_init(&xbus->lock);
	atomic_set(&xbus->xbus_ref_count, 1);	/* a single ref */
	snprintf(xbus->busname, XBUS_NAMELEN, "XBUS-%02d", xbus->num);
	init_waitqueue_head(&xbus->command_queue_empty);
	init_timer(&xbus->command_timer);
	atomic_set(&xbus->pcm_rx_counter, 0);
	xbus->min_tx_sync = INT_MAX;
	xbus->min_rx_sync = INT_MAX;
	
	xbus->num_xpds = 0;
	xbus->sync_mode = SYNC_MODE_NONE;
	init_rwsem(&xbus->in_use);
	xbus_reset_counters(xbus);
#ifdef CONFIG_PROC_FS
	XBUS_DBG(PROC, xbus, "Creating xbus proc directory\n");
	xbus->proc_xbus_dir = proc_mkdir(xbus->busname, xpp_proc_toplevel);
	if(!xbus->proc_xbus_dir) {
		XBUS_ERR(xbus, "Failed to create proc directory\n");
		err = -EIO;
		goto nobus;
	}
	xbus->proc_xbus_summary = create_proc_read_entry(PROC_XBUS_SUMMARY,
			0444, xbus->proc_xbus_dir,
			xbus_read_proc,
			(void *)((unsigned long)(xbus->num)));
	if (!xbus->proc_xbus_summary) {
		XBUS_ERR(xbus, "Failed to create proc file '%s'\n", PROC_XBUS_SUMMARY);
		err = -EIO;
		goto nobus;
	}
	xbus->proc_xbus_summary->owner = THIS_MODULE;
#ifdef	PROTOCOL_DEBUG
	xbus->proc_xbus_command = create_proc_entry(PROC_XBUS_COMMAND, 0200, xbus->proc_xbus_dir);
	if (!xbus->proc_xbus_command) {
		XBUS_ERR(xbus, "Failed to create proc file '%s'\n", PROC_XBUS_COMMAND);
		err = -EIO;
		goto nobus;
	}
	xbus->proc_xbus_command->write_proc = proc_xbus_command_write;
	xbus->proc_xbus_command->data = xbus;
	xbus->proc_xbus_command->owner = THIS_MODULE;
#endif
#endif
#ifdef	XPP_DEBUGFS
	xbus->debugfs_dir = debugfs_create_dir(xbus->busname, debugfs_root);
	if(!xbus->debugfs_dir) {
		XBUS_ERR(xbus, "Failed to create debugfs directory\n");
		goto nobus;
	}
	xbus->debugfs_file = debugfs_create_file("dchannel", S_IFREG|S_IRUGO|S_IWUSR, xbus->debugfs_dir, xbus, &debugfs_operations);
	if(!xbus->debugfs_file) {
		XBUS_ERR(xbus, "Failed to create dchannel file\n");
		goto nobus;
	}
#endif
	xframe_queue_init(&xbus->command_queue, 10, 200, "command_queue", xbus);
	xframe_queue_init(&xbus->receive_queue, 10, 50, "receive_queue", xbus);
	xframe_queue_init(&xbus->send_pool, 10, 200, "send_pool", xbus);
	xframe_queue_init(&xbus->receive_pool, 10, 50, "receive_pool", xbus);
	xframe_queue_init(&xbus->pcm_tospan, 5, 10, "pcm_tospan", xbus);
	tasklet_init(&xbus->receive_tasklet, receive_tasklet_func, (unsigned long)xbus);
	/*
	 * Create worker after /proc/XBUS-?? so the directory exists
	 * before /proc/XBUS-??/waitfor_xpds tries to get created.
	 */
	xbus->worker = worker_new(xbus->num);
	if(!xbus->worker) {
		ERR("Failed to allocate worker\n");
		goto nobus;
	}
	return xbus;
nobus:
	xbus_free(xbus);
	return NULL;
}

void xbus_remove(xbus_t *xbus)
{
	int	i;

	BUG_ON(TRANSPORT_RUNNING(xbus));
	down_write(&xbus->in_use);

	XBUS_INFO(xbus, "[%s] Removing\n", xbus->label);
	for(i = 0; i < MAX_XPDS; i++) {
		xpd_t *xpd = xpd_of(xbus, i);

		if(xpd) {
			if(xpd->xbus_idx != i) {
				XBUS_ERR(xbus, "BUG: xpd->xbus_idx=%d != i=%d\n", xpd->xbus_idx, i);
				continue;
			}
			XBUS_DBG(DEVICES, xbus, "  Removing xpd #%d\n", i);
			xpd_remove(xpd);
		}
		xbus->xpds[i] = NULL;
	}
	worker_destroy(xbus->worker);
	xbus_free(xbus);
}

/*------------------------- Proc handling --------------------------*/

void xbus_reset_counters(xbus_t *xbus)
{
	int	i;

	XBUS_DBG(GENERAL, xbus, "Reseting counters\n");
	for(i = 0; i < XBUS_COUNTER_MAX; i++) {
		xbus->counters[i] = 0;
	}
}

#if CONFIG_PROC_FS

static int xbus_fill_proc_queue(char *p, struct xframe_queue *q)
{
	int	len;

	len = sprintf(p,
			"%-15s: counts %3d, %3d, %3d worst %3d, overflows %3d worst_lag %02ld.%ld ms\n",
				q->name,
				q->steady_state_count,
				q->count,
				q->max_count,
				q->worst_count,
				q->overflows,
				q->worst_lag_usec / 1000,
				q->worst_lag_usec % 1000);
	xframe_queue_clearstats(q);
	return len;
}

static int xbus_read_proc(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	xbus_t			*xbus;
	struct xbus_workqueue	*worker;
	unsigned long		flags;
	int			len = 0;
	int			i = (int)((unsigned long)data);

	xbus = get_xbus(i);
	if(!xbus)
		goto out;
	spin_lock_irqsave(&xbus->lock, flags);
	worker = xbus->worker;

	len += sprintf(page + len, "%s: CONNECTOR=%s LABEL=[%s] STATUS=%s\n",
			xbus->busname,
			xbus->location,
			xbus->label,
			(TRANSPORT_RUNNING(xbus)) ? "connected" : "missing"
		      );
	len += sprintf(page + len, "\nxbus_ref_count=%d\n",
			atomic_read(&xbus->xbus_ref_count)
			);
	len += xbus_fill_proc_queue(page + len, &xbus->send_pool);
	len += xbus_fill_proc_queue(page + len, &xbus->receive_pool);
	len += xbus_fill_proc_queue(page + len, &xbus->command_queue);
	len += xbus_fill_proc_queue(page + len, &xbus->receive_queue);
	len += xbus_fill_proc_queue(page + len, &xbus->pcm_tospan);
	if(rx_tasklet) {
		len += sprintf(page + len, "\ncpu_rcv_intr:    ");
		for_each_online_cpu(i)
			len += sprintf(page + len, "%5d ", xbus->cpu_rcv_intr[i]);
		len += sprintf(page + len, "\ncpu_rcv_tasklet: ");
		for_each_online_cpu(i)
			len += sprintf(page + len, "%5d ", xbus->cpu_rcv_tasklet[i]);
		len += sprintf(page + len, "\n");
	}
	len += sprintf(page + len, "self_ticking: %d (last_tick at %ld)\n",
			xbus->self_ticking, xbus->ticker.last_sample.tv.tv_sec);
	len += sprintf(page + len, "xbus: pcm_rx_counter = %d, frag = %d\n",
		atomic_read(&xbus->pcm_rx_counter), xbus->xbus_frag_count);
	len += sprintf(page + len, "max_rx_process = %2ld.%ld ms\n",
		xbus->max_rx_process / 1000,
		xbus->max_rx_process % 1000);
	xbus->max_rx_process = 0;
	len += sprintf(page + len, "\nTRANSPORT: max_send_size=%d refcount=%d\n",
			MAX_SEND_SIZE(xbus),
			atomic_read(&xbus->transport.transport_refcount)
			);
	len += sprintf(page + len, "PCM Metrices:\n");
	len += sprintf(page + len, "\tPCM TX: min=%ld  max=%ld\n",
				xbus->min_tx_sync, xbus->max_tx_sync);
	len += sprintf(page + len, "\tPCM RX: min=%ld  max=%ld\n",
				xbus->min_rx_sync, xbus->max_rx_sync);
	len += sprintf(page + len, "COUNTERS:\n");
	for(i = 0; i < XBUS_COUNTER_MAX; i++) {
		len += sprintf(page + len, "\t%-15s = %d\n",
				xbus_counters[i].name, xbus->counters[i]);
	}
	len += sprintf(page + len, "<-- len=%d\n", len);
	spin_unlock_irqrestore(&xbus->lock, flags);
	put_xbus(xbus);
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

static int xbus_read_waitfor_xpds(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int			len = 0;
	unsigned long		flags;
	xbus_t			*xbus = data;
	struct xbus_workqueue	*worker;
	int			ret;

	if(!xbus)
		goto out;
	/* first handle special cases */
	if(!count || off)
		goto out;
	/*
	 * worker is created before /proc/XBUS-??
	 * So by now it exists and initialized.
	 */
	worker = xbus->worker;
	BUG_ON(!worker);
	XBUS_DBG(DEVICES, xbus,
		"Waiting for card initialization of %d XPD's max %d seconds\n",
		worker->num_units,
		INITIALIZATION_TIMEOUT/HZ);
	/*
	 * when polling is finished xbus_poll():
	 *   - Unset worker->is_polling
	 *   - Sets worker->count_xpds_to_initialize.
	 * So we wait until polling is finished (is_polling == 0) and:
	 *   - No poll answers from Astribank (e.g: defective firmware).
	 *   - Or no units to initialize (e.g: mini-AB with only main card).
	 *   - Or we finished initializing all existing units.
	 *   - Or A timeout passed.
	 */
	ret = wait_event_interruptible_timeout(
		worker->wait_for_xpd_initialization,
		worker->xpds_init_done,
		INITIALIZATION_TIMEOUT);
	if(ret == 0) {
		XBUS_ERR(xbus, "Card Initialization Timeout\n");
		return ret;
	} else if(ret < 0) {
		XBUS_ERR(xbus, "Card Initialization Interrupted %d\n", ret);
		return ret;
	} else
		XBUS_DBG(DEVICES, xbus,
			"Finished initialization of %d XPD's in %d seconds.\n",
			worker->num_units_initialized,
			(INITIALIZATION_TIMEOUT - ret)/HZ);
	spin_lock_irqsave(&xbus->lock, flags);
	len += sprintf(page + len, "XPDS_READY: %s: %d/%d\n",
			xbus->busname,
			worker->num_units_initialized, worker->num_units);
	spin_unlock_irqrestore(&xbus->lock, flags);
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

#ifdef	PROTOCOL_DEBUG
static int proc_xbus_command_write(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
	char			*buf;
	xbus_t			*xbus = data;
	char			*p;
	byte			*pack_start;
	byte			*q;
	xframe_t		*xframe;
	size_t			len;
	const size_t		max_len = xbus->transport.max_send_size;
	const size_t		max_text = max_len * 3 + 10;

	if(count > max_text) {
		XBUS_ERR(xbus, "%s: line too long (%ld > %zd)\n", __FUNCTION__, count, max_len);
		return -EFBIG;
	}
	/* 3 bytes per hex-digit and space */
	buf = kmalloc(max_text, GFP_KERNEL);
	if(!buf)
		return -ENOMEM;
	if(copy_from_user(buf, buffer, count)) {
		count = -EINVAL;
		goto out;
	}
	buf[count] = '\0';
	XBUS_DBG(GENERAL, xbus, "count=%ld\n", count);
	/*
	 * We replace the content of buf[] from
	 * ascii representation to packet content
	 * as the binary representation is shorter
	 */
	q = pack_start = buf;
	for(p = buf; *p;) {
		int	val;
		char	hexdigit[3];

		while(*p && isspace(*p))	// skip whitespace
			p++;
		if(!(*p))
			break;
		if(!isxdigit(*p)) {
			XBUS_ERR(xbus, "%s: bad hex value ASCII='0x%X' at position %ld\n",
					__FUNCTION__, *p, (long)(p - buf));
			count = -EINVAL;
			goto out;
		}
		hexdigit[0] = *p++;
		hexdigit[1] = '\0';
		hexdigit[2] = '\0';
		if(isxdigit(*p))
			hexdigit[1] = *p++;
		if(sscanf(hexdigit, "%2X", &val) != 1) {
			XBUS_ERR(xbus, "%s: bad hex value '%s' at position %ld\n",
					__FUNCTION__, hexdigit, (long)(p - buf));
			count = -EINVAL;
			goto out;
		}
		*q++ = val;
		XBUS_DBG(GENERAL, xbus, "%3zd> '%s' val=%d\n", q - pack_start, hexdigit, val);
	}
	len = q - pack_start;
	xframe = ALLOC_SEND_XFRAME(xbus);
	if(!xframe) {
		count = -ENOMEM;
		goto out;
	}
	if(len > max_len)
		len = max_len;
	atomic_set(&xframe->frame_len, len);
	memcpy(xframe->packets, pack_start, len);		/* FIXME: checksum? */
	dump_xframe("COMMAND", xbus, xframe, debug);
	send_cmd_frame(xbus, xframe);
out:
	kfree(buf);
	return count;
}
#endif


static int read_proc_xbuses(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = 0;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&xbuses_lock, flags);
	for(i = 0; i < MAX_BUSES; i++) {
		xbus_t *xbus = get_xbus(i);

		if(xbus) {
			len += sprintf(page + len, "%s: CONNECTOR=%s LABEL=[%s] STATUS=%s REFCOUNT=%d\n",
					xbus->busname,
					xbus->location,
					xbus->label,
					(TRANSPORT_RUNNING(xbus)) ? "connected" : "missing",
					refcount_xbus(i) - 1
				      );
			put_xbus(xbus);
		}
	}
#if 0
	len += sprintf(page + len, "<-- len=%d\n", len);
#endif
	spin_unlock_irqrestore(&xbuses_lock, flags);
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

static void transport_init(xbus_t *xbus, struct xbus_ops *ops, ushort max_send_size, void *priv)
{
	BUG_ON(!xbus);
	BUG_ON(!ops);
	BUG_ON(!ops->xframe_send_pcm);
	BUG_ON(!ops->xframe_send_cmd);
	BUG_ON(!ops->alloc_xframe);
	BUG_ON(!ops->free_xframe);
	xbus->transport.ops = ops;
	xbus->transport.max_send_size = max_send_size;
	xbus->transport.priv = priv;
	spin_lock_init(&xbus->transport.lock);
	atomic_set(&xbus->transport.transport_refcount, 0);
	init_waitqueue_head(&xbus->transport.transport_unused);
}

static void transport_destroy(xbus_t *xbus)
{
	int	ret;

	BUG_ON(!xbus);
	xbus->transport.transport_running = 0;
	XBUS_DBG(DEVICES, xbus, "Waiting... (transport_refcount=%d)\n",
		atomic_read(&xbus->transport.transport_refcount));
	ret = wait_event_interruptible(xbus->transport.transport_unused,
			atomic_read(&xbus->transport.transport_refcount) == 0);
	if(ret)
		XBUS_ERR(xbus, "Waiting for transport_refcount interrupted!!!\n");
	xbus->transport.ops = NULL;
	xbus->transport.priv = NULL;
}

struct xbus_ops *transportops_get(xbus_t *xbus)
{
	struct xbus_ops	*ops;

	BUG_ON(!xbus);
	atomic_inc(&xbus->transport.transport_refcount);
	ops = xbus->transport.ops;
	if(!ops)
		atomic_dec(&xbus->transport.transport_refcount);
	/* fall through */
	return ops;
}

void transportops_put(xbus_t *xbus)
{
	struct xbus_ops	*ops;

	BUG_ON(!xbus);
	ops = xbus->transport.ops;
	BUG_ON(!ops);
	if(atomic_dec_and_test(&xbus->transport.transport_refcount))
		wake_up(&xbus->transport.transport_unused);
}

/*------------------------- Initialization -------------------------*/
static void xbus_core_cleanup(void)
{
	finalize_xbuses_array();
#ifdef	XPP_DEBUGFS
	if(debugfs_root) {
		DBG(GENERAL, "Removing xpp from debugfs\n");
		debugfs_remove(debugfs_root);
	}
#endif
#ifdef CONFIG_PROC_FS
	if(proc_xbuses) {
		DBG(PROC, "Removing " PROC_XBUSES " from proc\n");
		remove_proc_entry(PROC_XBUSES, xpp_proc_toplevel);
		proc_xbuses = NULL;
	}
#endif
}

int __init xbus_core_init(void)
{
	int	ret = 0;

	initialize_xbuses_array();
#ifdef PROTOCOL_DEBUG
	INFO("FEATURE: with PROTOCOL_DEBUG\n");
#endif
#ifdef	XPP_DEBUGFS
	INFO("FEATURE: with XPP_DEBUGFS support\n");
#endif
#ifdef CONFIG_PROC_FS
	proc_xbuses = create_proc_read_entry(PROC_XBUSES, 0444, xpp_proc_toplevel, read_proc_xbuses, NULL);
	if (!proc_xbuses) {
		ERR("Failed to create proc file %s\n", PROC_XBUSES);
		ret = -EFAULT;
		goto err;
	}
	proc_xbuses->owner = THIS_MODULE;
#endif
#ifdef	XPP_DEBUGFS
	DBG(GENERAL, "Creating debugfs xpp root\n");
	debugfs_root = debugfs_create_dir("xpp", NULL);
	if(!debugfs_root) {
		ERR("Failed to create debugfs root\n");
		ret = -EFAULT;
		goto err;
	}
#endif
	if((ret = register_xpp_bus()) < 0)
		goto err;
	return 0;
err:
	xbus_core_cleanup();
	return ret;
}


void xbus_core_shutdown(void)
{
	int		i;

	for(i = 0; i < MAX_BUSES; i++) {
		xbus_t	*xbus = get_xbus(i);

		if(xbus) {
			xbus_remove(xbus);
		}
	}
	BUG_ON(bus_count);
	unregister_xpp_bus();
	xbus_core_cleanup();
}

EXPORT_SYMBOL(xpd_of);
EXPORT_SYMBOL(xpd_byaddr);
EXPORT_SYMBOL(get_xbus);
EXPORT_SYMBOL(put_xbus);
EXPORT_SYMBOL(xbus_new);
EXPORT_SYMBOL(xbus_remove);
EXPORT_SYMBOL(xbus_activate);
EXPORT_SYMBOL(xbus_disconnect);
EXPORT_SYMBOL(xbus_receive_xframe);
EXPORT_SYMBOL(xbus_reset_counters);
EXPORT_SYMBOL(xframe_next_packet);
EXPORT_SYMBOL(dump_xframe);
EXPORT_SYMBOL(send_pcm_frame);
EXPORT_SYMBOL(send_cmd_frame);
EXPORT_SYMBOL(xframe_init);
EXPORT_SYMBOL(transportops_get);
EXPORT_SYMBOL(transportops_put);
EXPORT_SYMBOL(xbus_command_queue_tick);
#ifdef XPP_DEBUGFS
EXPORT_SYMBOL(xbus_log);
#endif
