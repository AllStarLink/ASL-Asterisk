/*
 * Wildcard TC400B Driver
 *
 * Copyright (C) 2006-2008, Digium, Inc.
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
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <linux/jiffies.h>
#include <linux/moduleparam.h>
#include <linux/firmware.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/etherdevice.h>
#include <linux/timer.h>

#include "zaptel.h"

/* COMPILE TIME OPTIONS =================================================== */

#define INTERRUPT 0
#define WORKQUEUE 1
#define TASKLET   2

#ifndef DEFERRED_PROCESSING 
#	define DEFERRED_PROCESSING WORKQUEUE
#endif

#if DEFERRED_PROCESSING == INTERRUPT
#	define ALLOC_FLAGS GFP_ATOMIC
#elif DEFERRED_PROCESSING == TASKLET
#	define ALLOC_FLAGS GFP_ATOMIC
#else
#	define ALLOC_FLAGS GFP_KERNEL
#endif

#define WARN() WARN_ON(1)

#define DTE_PRINTK(_lvl, _fmt, _args...) \
	   printk(KERN_##_lvl "%s: %s: " _fmt, THIS_MODULE->name, \
	          (wc)->board_name, ## _args)

#define DTE_DEBUG(_dbgmask, _fmt, _args...)                                 \
	if ((debug & _dbgmask) == (_dbgmask)) {                             \
		printk(KERN_DEBUG "%s: %s: " _fmt, THIS_MODULE->name,       \
		       (wc)->board_name, ## _args);                         \
	}                                                                   \

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
#define WARN_ON_ONCE(__condition) do {         \
	static int __once = 1;                 \
	if (unlikely(__condition)) {           \
		if (__once) {                  \
			__once = 0;            \
			WARN_ON(0);            \
		}                              \
	}                                      \
} while(0) 
#endif

#define INVALID 999 /* Used to mark invalid channels, commands, etc.. */
#define MAX_CHANNEL_PACKETS  5 /* Never let more than 5 outstanding packets exist for any channel. */

#define G729_LENGTH	20
#define G723_LENGTH	30

#define G729_SAMPLES	160	/* G.729 */
#define G723_SAMPLES	240 	/* G.723.1 */

#define G729_BYTES	20	/* G.729 */
#define G723_6K_BYTES	24 	/* G.723.1 at 6.3kb/s */
#define G723_5K_BYTES	20	/* G.723.1 at 5.3kb/s */
#define G723_SID_BYTES	4	/* G.723.1 SID frame */

#define MAX_CAPTURED_PACKETS 5000

/* The following bit fields are used to set the various debug levels. */
#define DTE_DEBUG_GENERAL          (1 << 0) /* 1  */
#define DTE_DEBUG_CHANNEL_SETUP    (1 << 1) /* 2  */
#define DTE_DEBUG_RTP_TX           (1 << 2) /* 4  */
#define DTE_DEBUG_RTP_RX           (1 << 3) /* 8  */
#define DTE_DEBUG_RX_TIMEOUT	   (1 << 4) /* 16 */
#define DTE_DEBUG_NETWORK_IF       (1 << 5) /* 32 */
#define DTE_DEBUG_NETWORK_EARLY    (1 << 6) /* 64 */

int debug;
char *mode;

static spinlock_t wctc4xxp_list_lock;
static struct list_head wctc4xxp_list;

#define ETH_P_CSM_ENCAPS 0x889B

struct rtphdr {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8    csrc_count:4;
	__u8    extension:1;
	__u8    padding:1;
	__u8    ver:2;
	__u8    type:7;
	__u8 	marker:1;
#elif defined(__BIG_ENDIAN_BITFIELD)
	__u8    ver:2;
	__u8    padding:1;
	__u8    extension:1;
	__u8    csrc_count:4;
	__u8 	marker:1;
	__u8    type:7;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	__be16	seqno;
	__be32  timestamp;
	__be32  ssrc;
} __attribute__((packed));

struct rtp_packet {
	struct ethhdr ethhdr;
	struct iphdr  iphdr;
	struct udphdr udphdr;
	struct rtphdr rtphdr;
	__u8   payload[0];
}__attribute__((packed));

/* Ethernet packet type for communication control information to the DTE. */
struct csm_encaps_hdr {
	struct ethhdr ethhdr;
	/* CSM_ENCAPS HEADER */
	__be16 op_code;
	__u8   seq_num;
	__u8   control;
	__be16 channel;
	/* COMMON PART OF PAYLOAD HEADER */
	__u8   length;
	__u8   index;
	__u8   type;
	__u8   class;
	__le16 function;
	__le16 reserved;
	__le16 params[0];
} __attribute__((packed));

struct csm_create_channel_cmd {
	struct csm_encaps_hdr hdr;
	__le16 channel_type;
	__le16 timeslot;
} __attribute__((packed));

#define CMD_MSG_TDM_SELECT_BUS_MODE_LEN 30
#define CMD_MSG_TDM_SELECT_BUS_MODE(s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0A, 0x01, 0x00,0x06,0x17,0x04, 0xFF,0xFF, \
	0x04,0x00 }
#define CMD_MSG_TDM_ENABLE_BUS_LEN 30
#define CMD_MSG_TDM_ENABLE_BUS(s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0A, 0x02, 0x00,0x06,0x05,0x04, 0xFF,0xFF, \
	0x04,0x00 }
#define CMD_MSG_SUPVSR_SETUP_TDM_PARMS_LEN 34
#define CMD_MSG_SUPVSR_SETUP_TDM_PARMS(s,p1,p2,p3) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x10, p1, 0x00,0x06,0x07,0x04, 0xFF,0xFF, \
	p2,0x83, 0x00,0x0C, 0x00,0x00, p3,0x00 }
#define CMD_MSG_TDM_OPT_LEN 30
#define CMD_MSG_TDM_OPT(s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0A, 0x00, 0x00,0x06,0x35,0x04, 0xFF,0xFF, \
	0x00,0x00 }
#define CMD_MSG_DEVICE_SET_COUNTRY_CODE_LEN 30
#define CMD_MSG_DEVICE_SET_COUNTRY_CODE(s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0A, 0x00, 0x00,0x06,0x1B,0x04, 0xFF,0xFF, \
	0x00,0x00 }

/* CPU Commands */
#define CMD_MSG_SET_ARM_CLK_LEN 32
#define CMD_MSG_SET_ARM_CLK(s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0C, 0x00, 0x00,0x06,0x11,0x04, 0x00,0x00, \
	0x2C,0x01, 0x00,0x00 }
#define CMD_MSG_SET_SPU_CLK_LEN 32
#define CMD_MSG_SET_SPU_CLK(s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0C, 0x00, 0x00,0x06,0x12,0x04, 0x00,0x00, \
	0x2C,0x01, 0x00,0x00 }
#define CMD_MSG_SPU_FEATURES_CONTROL_LEN 30
#define CMD_MSG_SPU_FEATURES_CONTROL(s,p1) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0A, 0x00, 0x00,0x06,0x13,0x00, 0xFF,0xFF, \
	p1,0x00 }
#define CMD_MSG_DEVICE_STATUS_CONFIG_LEN 30
#define CMD_MSG_DEVICE_STATUS_CONFIG(s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0A, 0x00, 0x00,0x06,0x0F,0x04, 0xFF,0xFF, \
	0x05,0x00 }

/* General IP/RTP Commands */
#define CMD_MSG_SET_ETH_HEADER_LEN 44
#define CMD_MSG_SET_ETH_HEADER(s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x18, 0x00, 0x00,0x06,0x00,0x01, 0xFF,0xFF, \
	0x01,0x00, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x00,0x11,0x22,0x33,0x44,0x55, 0x08,0x00 }
#define CMD_MSG_IP_SERVICE_CONFIG_LEN 30
#define CMD_MSG_IP_SERVICE_CONFIG(s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0A, 0x00, 0x00,0x06,0x02,0x03, 0xFF,0xFF, \
	0x00,0x02 }
#define CMD_MSG_ARP_SERVICE_CONFIG_LEN 30
#define CMD_MSG_ARP_SERVICE_CONFIG(s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0A, 0x00, 0x00,0x06,0x05,0x01, 0xFF,0xFF, \
	0x01,0x00 }
#define CMD_MSG_ICMP_SERVICE_CONFIG_LEN 30
#define CMD_MSG_ICMP_SERVICE_CONFIG(s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0A, 0x00, 0x00,0x06,0x04,0x03, 0xFF,0xFF, \
	0x01,0xFF }
#define CMD_MSG_IP_OPTIONS_LEN 30
#define CMD_MSG_IP_OPTIONS(s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0A, 0x00, 0x00,0x06,0x06,0x03, 0xFF,0xFF, \
	0x02,0x00 }

#define CONTROL_PACKET_OPCODE  0x0001
/* Control bits */
#define LITTLE_ENDIAN   0x01
#define SUPPRESS_ACK    0x40
#define MESSAGE_PACKET  0x80

#define SUPERVISOR_CHANNEL 0xffff

/* Supervisor function codes */
#define SUPVSR_CREATE_CHANNEL  0x0010

#define CONFIG_CHANGE_TYPE        0x00 
#define CONFIG_DEVICE_CLASS       0x06

#define CMD_MSG_QUERY_CHANNEL_LEN 30
#define CMD_MSG_QUERY_CHANNEL(s,t) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0A, 0x00, 0x01,0x06,0x10,0x00, 0x00,0x00, \
	(t&0x00FF), ((t&0xFF00) >> 8) }

#define CMD_MSG_TRANS_CONNECT_LEN 38
#define CMD_MSG_TRANS_CONNECT(s,e,c1,c2,f1,f2) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x12, 0x00, 0x00,0x06,0x22,0x93, 0x00,0x00, \
	e,0x00, (c1&0x00FF),((c1&0xFF00)>>8), f1,0x00, (c2&0x00FF),((c2&0xFF00)>>8), f2,0x00 }
#define CMD_MSG_DESTROY_CHANNEL_LEN 32
#define CMD_MSG_DESTROY_CHANNEL(s,t) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0A, 0x00, 0x00,0x06,0x11,0x00, 0x00,0x00, \
	(t&0x00FF),((t&0xFF00)>>8), 0x00, 0x00 }

/* Individual channel config commands */
#define CMD_MSG_SET_IP_HDR_CHANNEL_LEN 58
#define CMD_MSG_SET_IP_HDR_CHANNEL(s,c,t2,t1) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, ((c&0xFF00) >> 8),(c&0x00FF), 0x26, 0x00, 0x00,0x02,0x00,0x90, 0x00,0x00, \
	0x00,0x00, 0x45,0x00, 0x00,0x00, 0x00,0x00, 0x40,0x00, 0x80,0x11, 0x00,0x00, \
	0xC0,0xA8,0x09,0x03, 0xC0,0xA8,0x09,0x03, \
	((t2&0xFF00)>>8)+0x50,(t2&0x00FF), ((t1&0xFF00)>>8)+0x50,(t1&0x00FF), 0x00,0x00, 0x00,0x00 }
#define CMD_MSG_VOIP_VCEOPT_LEN 40
#define CMD_MSG_VOIP_VCEOPT(s,c,l,w) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, ((c&0xFF00)>>8),(c&0x00FF), 0x12, 0x00, 0x00,0x02,0x01,0x80, 0x00,0x00, \
	0x21,l, 0x00,0x1C, 0x04,0x00, 0x00,0x00, w,0x00, 0x80,0x11 }
#define CMD_MSG_VOIP_VOPENA_LEN 44
#define CMD_MSG_VOIP_VOPENA(s,c,f) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, ((c&0xFF00)>>8),(c&0x00FF), 0x16, 0x00, 0x00,0x02,0x00,0x80, 0x00,0x00, \
	0x01,0x00, 0x80,f, 0x00,0x00, 0x00,0x00, 0x00,0x00, 0x12,0x34, 0x56,0x78, 0x00,0x00 }
#define CMD_MSG_VOIP_VOPENA_CLOSE_LEN 32
#define CMD_MSG_VOIP_VOPENA_CLOSE(s,c) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, ((c&0xFF00)>>8),(c&0x00FF), 0x0A, 0x00, 0x00,0x02,0x00,0x80, 0x00,0x00, \
	0x00,0x00, 0x00,0x00 }
#define CMD_MSG_VOIP_INDCTRL_LEN 32
#define CMD_MSG_VOIP_INDCTRL(s,c) {0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, ((c&0xFF00)>>8),(c&0x00FF), 0x0A, 0x00, 0x00,0x02,0x84,0x80, 0x00,0x00, \
	0x07,0x00, 0x00,0x00 }
#define CMD_MSG_VOIP_DTMFOPT_LEN 32
#define CMD_MSG_VOIP_DTMFOPT(s,c) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, ((c&0xFF00)>>8),(c&0x00FF), 0x0A, 0x00, 0x00,0x02,0x02,0x80, 0x00,0x00, \
	0x08,0x00, 0x00,0x00 }

#define CMD_MSG_VOIP_TONECTL_LEN 32
#define CMD_MSG_VOIP_TONECTL(s,c) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, ((c&0xFF00)>>8),(c&0x00FF), 0x0A, 0x00, 0x00,0x02,0x5B,0x80, 0x00,0x00, \
	0x00,0x00, 0x00,0x00 }

#define SFRAME_SIZE 320 

/* Transcoder buffer (tcb) */
struct tcb {
	/* First field so that is aligned by default. */
	u8 cmd[SFRAME_SIZE];
	struct list_head node;
	unsigned long timeout;
	unsigned long retries;
	/* NOTE:  these flags aren't bit fields because some of the flags are
	 * combinations of the other ones. */
#define DO_NOT_AUTO_FREE        (1 << 0)
#define TX_COMPLETE             (1 << 1)
#define DO_NOT_CAPTURE          (1 << 2)
#define __WAIT_FOR_ACK          (1 << 3)
#define __WAIT_FOR_RESPONSE     (1 << 4) 
#define DTE_CMD_TIMEOUT         (1 << 5)
#define WAIT_FOR_ACK (__WAIT_FOR_ACK | DO_NOT_AUTO_FREE)
#define WAIT_FOR_RESPONSE (__WAIT_FOR_RESPONSE | DO_NOT_AUTO_FREE)
	unsigned long flags;
	struct tcb *response;
	struct completion complete;
	struct timer_list timer;
	void *data;
	/* The number of bytes available in data. */
	int data_len; 
};

static inline void *hdr_from_cmd(struct tcb *cmd) {
	return cmd->data;
}

static inline void 
initialize_cmd(struct tcb *cmd, unsigned long cmd_flags) 
{
	memset(cmd, 0, sizeof(*cmd));
	INIT_LIST_HEAD(&cmd->node);
	init_completion(&cmd->complete);
	cmd->flags = cmd_flags;
	cmd->data = &cmd->cmd[0];
	cmd->data_len = SFRAME_SIZE;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
/*! Used to allocate commands to submit to the dte. */
kmem_cache_t *cmd_cache;
#else
/*! Used to allocate commands to submit to the dte. */
struct kmem_cache *cmd_cache;
#endif

static inline struct tcb *
__alloc_cmd(unsigned alloc_flags, unsigned long cmd_flags)
{
	struct tcb *cmd;

	cmd = kmem_cache_alloc(cmd_cache, alloc_flags);
	if (likely(cmd)) {
		initialize_cmd(cmd, cmd_flags);
	}
	return cmd;
}

static struct tcb *
alloc_cmd(void)
{
	return __alloc_cmd(GFP_KERNEL, 0);
}

static void 
__free_cmd(struct tcb *cmd)
{
	if (cmd->data != &cmd->cmd[0]) {
		kfree(cmd->data);
	}
	kmem_cache_free(cmd_cache, cmd);
	return;
}

static void 
free_cmd(struct tcb *cmd)
{
	if (cmd->response) {
		__free_cmd(cmd->response);
	}
	__free_cmd(cmd);
}

typedef enum { DECODER=0, ENCODER, } encode_t;

struct channel_stats {
	atomic_t packets_sent;
	atomic_t packets_received;
};

struct channel_pvt {
	spinlock_t lock;		/* Lock for this structure */
	encode_t encoder;		/* If we're an encoder */
	struct wcdte *wc;

	unsigned int timestamp;
	unsigned int seqno;

	unsigned int cmd_seqno;

	unsigned int timeslot_in_num;	/* DTE channel on which results will be received from */
	unsigned int timeslot_out_num;	/* DTE channel to send data to */

	unsigned int chan_in_num;	/* DTE channel on which results will be received from */
	unsigned int chan_out_num;	/* DTE channel to send data to */
	
	struct channel_stats stats;

	u16 last_dte_seqno;
	unsigned int wctc4xxp_seqno_rcv;

	unsigned char ssrc;
	struct list_head rx_queue;	/* Transcoded packets for this channel. */
};

struct wcdte {
	char board_name[40];
	const char *variety;
	int pos;
	int cards;
	struct list_head node;
	spinlock_t reglock;
	wait_queue_head_t waitq;
	struct semaphore chansem;
#define DTE_READY	1
#define DTE_SHUTDOWN	2 
	unsigned long flags;

	spinlock_t cmd_list_lock;
	/* This is a device-global list of commands that are waiting to be
	 * transmited (and did not fit on the transmit descriptor ring) */
	struct list_head cmd_list;
	struct list_head waiting_for_response_list;

	unsigned int seq_num;
	unsigned char numchannels;
	unsigned char complexname[40];

	/* This section contains the members necessary to communicate with the
	 * physical interface to the transcoding engine.  */
	struct pci_dev *pdev;
	unsigned int   intmask;
	unsigned long  iobase;
	struct wctc4xxp_descriptor_ring *txd;
	struct wctc4xxp_descriptor_ring *rxd;
	
	struct zt_transcoder *uencode;
	struct zt_transcoder *udecode;
	struct channel_pvt *encoders;
	struct channel_pvt *decoders;

#if DEFERRED_PROCESSING == WORKQUEUE
	struct work_struct deferred_work;
#endif

	/*
	 * This section contains the members necessary for exporting the
	 * network interface to the host system.  This is only used for
	 * debugging purposes.
	 *
	 */
	struct sk_buff_head captured_packets;
	struct net_device *netdev;
	struct net_device_stats net_stats;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
	struct napi_struct napi;
#endif
	struct timer_list watchdog;

};

static inline void wctc4xxp_set_ready(struct wcdte *wc) {
	set_bit(DTE_READY, &wc->flags);
}
static inline int wctc4xxp_is_ready(struct wcdte *wc) {
	return test_bit(DTE_READY, &wc->flags);
}

#if 1
 /* \todo This macro is a candidate for removal.  It's still here because of
 * how the commands are passed to this zt_send_cmd */
#define wctc4xxp_send_cmd(wc, command) ({                                         \
	int __res;                                                           \
	u8 _cmd[] = command;                                                 \
	struct tcb *cmd;                                                 \
	if (!(cmd=__alloc_cmd(GFP_KERNEL, WAIT_FOR_RESPONSE)))               \
		return -ENOMEM;                                              \
	BUG_ON(sizeof(_cmd) > SFRAME_SIZE);                                  \
	memcpy(cmd->data, _cmd, sizeof(_cmd));                               \
	cmd->data_len = sizeof(_cmd);                                        \
	__res = __wctc4xxp_send_cmd(wc, cmd);                                     \
	__res;                                                               \
})
#define wctc4xxp_create_cmd(wc, command) ({                                       \
	u8 _command[] = command;                                             \
	struct tcb *_cmd;                                                \
	if (!(_cmd=__alloc_cmd(GFP_KERNEL, WAIT_FOR_RESPONSE)))              \
		return -ENOMEM;                                              \
	BUG_ON(sizeof(_command) > SFRAME_SIZE);                              \
	memcpy(_cmd->data, _command, sizeof(_command));                      \
	_cmd->data_len = sizeof(_command);                                   \
	_cmd;                                                                \
})
#endif

#define DTE_FORMAT_ULAW   0x00
#define DTE_FORMAT_G723_1 0x04
#define DTE_FORMAT_ALAW   0x08
#define DTE_FORMAT_G729A  0x12
#define DTE_FORMAT_UNDEF  0xFF

static inline u8 wctc4xxp_zapfmt_to_dtefmt(unsigned int fmt)
{
	u8 pt;
	
	switch(fmt) {
		case ZT_FORMAT_G723_1:
			pt = DTE_FORMAT_G723_1;
			break;
		case ZT_FORMAT_ULAW:
			pt = DTE_FORMAT_ULAW;
			break;
		case ZT_FORMAT_ALAW:
			pt = DTE_FORMAT_ALAW;
			break;
		case ZT_FORMAT_G729A:
			pt = DTE_FORMAT_G729A;
			break;
		default:
			pt = DTE_FORMAT_UNDEF;
			break;
	}

	return pt;
}


static struct sk_buff * 
tcb_to_skb(struct net_device *netdev, const struct tcb *cmd)
{
	struct sk_buff *skb;
	skb = alloc_skb(cmd->data_len, in_atomic() ? GFP_ATOMIC : GFP_KERNEL);
	if (skb) {
		skb->dev = netdev;
		skb_put(skb, cmd->data_len);
		memcpy(skb->data, cmd->data, cmd->data_len);
		skb->protocol = eth_type_trans(skb,netdev);
	}
	return skb;
}

/** 
 * wctc4xxp_skb_to_cmd - Convert a socket buffer (skb) to a tcb
 * @wc: The transcoder that we're going to send this command to.
 * @skb: socket buffer to convert.
 *
 */
static struct tcb *
wctc4xxp_skb_to_cmd(struct wcdte *wc, const struct sk_buff *skb)
{
	const unsigned long alloc_flags = in_interrupt() ? GFP_ATOMIC : GFP_KERNEL;
	struct tcb *cmd;
	/* const static char dev_mac[] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55}; */
	if ((cmd = __alloc_cmd(alloc_flags, 0))) {
		int res;
		cmd->data_len = skb->len;
		if ((res = skb_copy_bits(skb, 0, cmd->data, cmd->data_len))) {
			DTE_PRINTK(WARNING, 
			   "Failed call to skb_copy_bits.\n");
			free_cmd(cmd);
			cmd = NULL;
		}
		/* When we set up our interface we indicated that we do not
		 * support ARP.  Therefore, the destination MAC on packets
		 * arriving from the kernel networking components are not
		 * going to be correct. Let's fix that here.
		 */
		/* \todo let us just use whatever was in the packet already... */
		/* memcpy(&cmd->cmd[6], dev_mac, sizeof(dev_mac)); */
	}
	return cmd;
}

static void 
wctc4xxp_net_set_multi(struct net_device *netdev)
{
	struct wcdte *wc = netdev->priv;
	DTE_DEBUG(DTE_DEBUG_GENERAL, "%s promiscuity:%d\n", 
	   __FUNCTION__, netdev->promiscuity);
}

static int 
wctc4xxp_net_up(struct net_device *netdev)
{
	struct wcdte *wc = netdev->priv;
	DTE_DEBUG(DTE_DEBUG_GENERAL, "%s\n", __FUNCTION__);
#if 1
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	netif_poll_enable(netdev);
#else
	napi_enable(&wc->napi);
#endif
#endif
	return 0;
}

static int 
wctc4xxp_net_down(struct net_device *netdev)
{
	struct wcdte *wc = netdev->priv;
	DTE_DEBUG(DTE_DEBUG_GENERAL, "%s\n", __FUNCTION__);
#if 1
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	netif_poll_disable(netdev);
#else
	napi_disable(&wc->napi);
#endif
#endif
	return 0;
}

static void wctc4xxp_transmit_cmd(struct wcdte *, struct tcb *);

static int 
wctc4xxp_net_hard_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct wcdte *wc = netdev->priv;
	struct tcb *cmd;

	/* We set DO_NOT_CAPTURE because this packet was already captured by
	 * in code higher up in the networking stack.  We don't want to
	 * capture it twice. 
	 */
	if ((cmd = wctc4xxp_skb_to_cmd(wc, skb))) {
		cmd->flags |= DO_NOT_CAPTURE; 
		wctc4xxp_transmit_cmd(wc, cmd);
	}

	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static int 
wctc4xxp_net_receive(struct wcdte *wc, int max)
{
	int count = 0;
	struct sk_buff *skb;
	WARN_ON(0 == max);
	while ((skb = skb_dequeue(&wc->captured_packets))) {
		netif_receive_skb(skb);
		if (++count >= max) {
			break;
		}
	}
	return count;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
static int 
wctc4xxp_poll(struct net_device *netdev, int *budget)
{
	struct wcdte *wc = netdev->priv;
	int count = 0;
	int quota = min(netdev->quota, *budget);

	count = wctc4xxp_net_receive(wc, quota);

	*budget -=       count;
	netdev->quota -= count;

	if (!skb_queue_len(&wc->captured_packets)) {
		netif_rx_complete(netdev);
		return 0;
	} else {
		return -1;
	}
}
#else
static int 
wctc4xxp_poll(struct napi_struct *napi, int budget)
{
	struct wcdte *wc = container_of(napi, struct wcdte, napi);
	int count;

	count = wctc4xxp_net_receive(wc, budget);

	if (!skb_queue_len(&wc->captured_packets)) {
		netif_rx_complete(wc->netdev, &wc->napi);
	}
	return count;
}
#endif

static struct net_device_stats *
wctc4xxp_net_get_stats(struct net_device *netdev)
{
	struct wcdte *wc = netdev->priv;
	return &wc->net_stats;
}

/* Wait until this device is put into promiscuous mode, or we timeout. */
static void 
wctc4xxp_net_waitfor_promiscuous(struct wcdte *wc)
{
	unsigned int seconds = 15;
	unsigned long start = jiffies;
	struct net_device *netdev = wc->netdev;

	DTE_PRINTK(INFO, 
	   "Waiting %d seconds for adapter to be placed in " \
	   "promiscuous mode for early trace.\n", seconds);

	while (!netdev->promiscuity) {
		if (signal_pending(current)) {
			DTE_PRINTK(INFO, 
			   "Aborting wait due to signal.\n");
			break;
		}
		msleep(100);
		if (time_after(jiffies, start + (seconds * HZ))) {
			DTE_PRINTK(INFO,
			   "Aborting wait due to timeout.\n");
			break;
		}
	}
}

static int  wctc4xxp_turn_off_booted_led(struct wcdte *wc);
static void wctc4xxp_turn_on_booted_led(struct wcdte *wc);

static int 
wctc4xxp_net_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	struct wcdte *wc = netdev->priv;
	switch(cmd) {
	case 0x89f0:
		down(&wc->chansem);
		wctc4xxp_turn_off_booted_led(wc);
		break;
	case 0x89f1:
		wctc4xxp_turn_on_booted_led(wc);
		up(&wc->chansem);
		break;
	default:
		return -EOPNOTSUPP;
	};
	return 0;
}

/** 
 * wctc4xxp_net_register - Register a new network interface.
 * @wc: transcoder card to register the interface for.
 *
 * The network interface is primarily used for debugging in order to watch the
 * traffic between the transcoder and the host.
 * 
 */
static int 
wctc4xxp_net_register(struct wcdte *wc)
{
	int res;
	struct net_device *netdev;
	const char our_mac[] = { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};

	if (!(netdev = alloc_netdev(0, wc->board_name, ether_setup))) {
		return -ENOMEM;
	}

	memcpy(netdev->dev_addr, our_mac, sizeof(our_mac));
	netdev->priv = wc;
	netdev->set_multicast_list = &wctc4xxp_net_set_multi;
	netdev->open = &wctc4xxp_net_up;
	netdev->stop = &wctc4xxp_net_down;
	netdev->hard_start_xmit = &wctc4xxp_net_hard_start_xmit;
	netdev->get_stats = &wctc4xxp_net_get_stats;
	netdev->do_ioctl = &wctc4xxp_net_ioctl;
	netdev->promiscuity = 0;
	netdev->flags |= IFF_NOARP;
#	if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	netdev->poll = &wctc4xxp_poll;
	netdev->weight = 64;
#	else
	netif_napi_add(netdev, &wc->napi, &wctc4xxp_poll, 64);
#	endif

	if ((res = register_netdev(netdev))) {
		DTE_PRINTK(WARNING, 
		   "Failed to register network device %s.\n",
		   wc->board_name);
		goto error_sw;
	}

	wc->netdev = netdev;
	skb_queue_head_init(&wc->captured_packets);

	if (debug & DTE_DEBUG_NETWORK_EARLY) {
		wctc4xxp_net_waitfor_promiscuous(wc);
	}

	DTE_PRINTK(DEBUG, 
	   "Created network device %s for debug.\n", wc->board_name);
	return 0;

error_sw:
	if (netdev) free_netdev(netdev);
	return res;
}

static void 
wctc4xxp_net_unregister(struct wcdte *wc)
{
	struct sk_buff *skb;
	if (!wc->netdev) {
		return;
	}

	unregister_netdev(wc->netdev);

	while ((skb = skb_dequeue(&wc->captured_packets))) {
		kfree_skb(skb);
	}
	
	free_netdev(wc->netdev);
	wc->netdev = NULL;
}


/**
 * wctc4xxp_net_capture_cmd - Send a tcb to the network stack.
 * @wc: transcoder that received the command.
 * @cmd: command to send to network stack.
 *
 */
static void 
wctc4xxp_net_capture_cmd(struct wcdte *wc, const struct tcb *cmd)
{
	struct sk_buff *skb;
	struct net_device *netdev = wc->netdev;

	if (!netdev) {
		return;
	}

	/* No need to capture if there isn't anyone listening. */
	if (!(netdev->flags & IFF_UP)) {
		return;
	}
	
	if (skb_queue_len(&wc->captured_packets) > MAX_CAPTURED_PACKETS) {
		WARN_ON_ONCE(1);
		return;
	}

	if (!(skb = tcb_to_skb(netdev, cmd))) {
		return;
	}

	skb_queue_tail(&wc->captured_packets, skb);
#	if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	netif_rx_schedule(netdev);
#	else
	netif_rx_schedule(netdev, &wc->napi);
#	endif
	return;
}


/*! In-memory structure shared by the host and the adapter. */
struct wctc4xxp_descriptor {
	__le32 des0;
	__le32 des1;
	__le32 buffer1;
	__le32 container; /* Unused */
} __attribute__((packed));

#define DRING_SIZE (1 << 3) /* Must be a power of two */
#define DRING_MASK (DRING_SIZE-1)
#define MIN_PACKET_LEN  64

struct wctc4xxp_descriptor_ring {
	/* Pointer to an array of descriptors to give to hardware. */
	struct wctc4xxp_descriptor* desc;
	/* Read completed buffers from the head. */
	unsigned int 	head;
	/* Write ready buffers to the tail. */
	unsigned int 	tail;
	/* Array to save the kernel virtual address of pending commands. */
	struct tcb *pending[DRING_SIZE];
	/* PCI Bus address of the descriptor list. */
	dma_addr_t	desc_dma;
	/*! either DMA_FROM_DEVICE or DMA_TO_DEVICE */
	unsigned int 	direction;
	/*! The number of buffers currently submitted to the hardware. */
	unsigned int    count;
	/*! The number of bytes to pad each descriptor for cache alignment. */
	unsigned int	padding;
	/*! Protects this structure from concurrent access. */
	spinlock_t      lock;
	/*! PCI device for the card associated with this ring. */
	struct pci_dev  *pdev;
};

/**
 * wctc4xxp_descriptor - Returns the desriptor at index.
 * @dr: The descriptor ring we're using.
 * @index: index of the descriptor we want.
 * 
 * We need this function because we do not know what the padding on the
 * descriptors will be.  Otherwise, we would just use an array.
 */
static inline struct wctc4xxp_descriptor *
wctc4xxp_descriptor(struct wctc4xxp_descriptor_ring *dr, int index)
{
	return (struct wctc4xxp_descriptor *)((u8*)dr->desc + 
		((sizeof(*dr->desc) + dr->padding) * index));
}

static int
wctc4xxp_initialize_descriptor_ring(struct pci_dev *pdev, struct wctc4xxp_descriptor_ring *dr, 
	u32 des1, unsigned int direction)
{
	int i; 
	const u32 END_OF_RING = 0x02000000;
	u8 cache_line_size = 0;
	struct wctc4xxp_descriptor *d;

	BUG_ON(!pdev);
	BUG_ON(!dr);

	if (pci_read_config_byte(pdev, 0x0c, &cache_line_size)) {
		/* \todo Print an error message... */
		return -EIO;
	}

	memset(dr, 0, sizeof(*dr));

	/*
	 * Add some padding to each descriptor to ensure that they are
	 * aligned on host system cache-line boundaries, but only for the 
	 * cache-line sizes that we support.
	 *
	 */
	if ((0x08 == cache_line_size) || (0x10 == cache_line_size) ||
	    (0x20 == cache_line_size)) 
	{
		dr->padding = (cache_line_size*sizeof(u32)) - sizeof(*d);
	} 

	dr->desc = pci_alloc_consistent(pdev, 
			(sizeof(*d)+dr->padding)*DRING_SIZE, &dr->desc_dma);

	if (!dr->desc) {
		return -ENOMEM;
	}

	memset(dr->desc, 0, (sizeof(*d) + dr->padding) * DRING_SIZE);
	for (i = 0; i < DRING_SIZE; ++i) {
		d = wctc4xxp_descriptor(dr, i);
		d->des1 = cpu_to_le32(des1);
	}

	d->des1 |= cpu_to_le32(END_OF_RING);
	dr->direction = direction;
	spin_lock_init(&dr->lock);
	return 0;
}

#define OWN_BIT cpu_to_le32(0x80000000)
#define OWNED(_d_) (((_d_)->des0)&OWN_BIT)
#define SET_OWNED(_d_) do { wmb(); (_d_)->des0 |= OWN_BIT; wmb();} while (0)

const unsigned int BUFFER1_SIZE_MASK = 0x7ff;

static int 
wctc4xxp_submit(struct wctc4xxp_descriptor_ring* dr, struct tcb *c)
{
	volatile struct wctc4xxp_descriptor *d;
	unsigned int len;

	WARN_ON(!c);
	len = (c->data_len < MIN_PACKET_LEN) ? MIN_PACKET_LEN : c->data_len;
	if (c->data_len > 1518) {
		WARN_ON_ONCE(!"Invalid command length passed\n");
		c->data_len = 1518;
	}

	spin_lock_bh(&dr->lock);
	d = wctc4xxp_descriptor(dr, dr->tail); 
	WARN_ON(!d);
	if (d->buffer1) {
		spin_unlock_bh(&dr->lock);
		/* Do not overwrite a buffer that is still in progress. */
		return -EBUSY;
	}
	d->des1 &= cpu_to_le32(~(BUFFER1_SIZE_MASK));
	d->des1 |= cpu_to_le32(len & BUFFER1_SIZE_MASK);
	d->buffer1 = pci_map_single(dr->pdev, c->data, 
	                            SFRAME_SIZE, dr->direction);

	SET_OWNED(d); /* That's it until the hardware is done with it. */ 
	dr->pending[dr->tail] = c;
	dr->tail = ++dr->tail & DRING_MASK;
	++dr->count;
	spin_unlock_bh(&dr->lock);
	return 0;
}

static inline struct tcb* 
wctc4xxp_retrieve(struct wctc4xxp_descriptor_ring *dr)
{
	volatile struct wctc4xxp_descriptor *d;
	struct tcb *c;
	unsigned int head = dr->head;
	spin_lock_bh(&dr->lock);
	d = wctc4xxp_descriptor(dr, head);
	if (d->buffer1 && !OWNED(d)) {
		pci_unmap_single(dr->pdev, d->buffer1, 
		                 SFRAME_SIZE, dr->direction);
		c = dr->pending[head];
		WARN_ON(!c);
		dr->head = (++head) & DRING_MASK; 
		d->buffer1 = 0;
		--dr->count;
		WARN_ON(!c);
		c->data_len = (d->des0 >> 16) & BUFFER1_SIZE_MASK;
		c->flags |= TX_COMPLETE;
	} else {
		c = NULL;
	}
	spin_unlock_bh(&dr->lock);
	return c;
}

static inline int wctc4xxp_getcount(struct wctc4xxp_descriptor_ring *dr) 
{
	int count;
	spin_lock_bh(&dr->lock);
	count = dr->count;
	spin_unlock_bh(&dr->lock);
	return count;
}

static inline void 
__wctc4xxp_setctl(struct wcdte *wc, unsigned int addr, unsigned int val)
{
	outl(val, wc->iobase + addr);
}

static inline unsigned int 
__wctc4xxp_getctl(struct wcdte *wc, unsigned int addr)
{
	return inl(wc->iobase + addr);
}

static inline void 
wctc4xxp_setctl(struct wcdte *wc, unsigned int addr, unsigned int val)
{
	spin_lock_bh(&wc->reglock);
	__wctc4xxp_setctl(wc, addr, val);
	spin_unlock_bh(&wc->reglock);
}

static inline void 
wctc4xxp_receive_demand_poll(struct wcdte *wc) 
{
	__wctc4xxp_setctl(wc, 0x0010, 0x00000000);
}

static inline void 
wctc4xxp_transmit_demand_poll(struct wcdte *wc)
{
	__wctc4xxp_setctl(wc, 0x0008, 0x00000000);

	/* \todo Investigate why this register needs to be written twice in
	 * order to get it to poll reliably.  So far, most of the problems
	 * I've seen with timeouts had more to do with an untransmitted
	 * packet sitting in the outbound descriptor list as opposed to any
	 * problem with the dte firmware.
	 */
	__wctc4xxp_setctl(wc, 0x0008, 0x00000000);
}

/* Returns the size, in bytes, of a CSM_ENCAPS packet, given the number of
 * parameters used. */
#define SIZE_WITH_N_PARAMETERS(__n) (sizeof(struct csm_encaps_hdr) + ((__n) * (sizeof(u16))))

/* There are 20 bytes in the ethernet header and the common CSM_ENCAPS header
 * that we don't want in the length of the actual CSM_ENCAPS command */
#define LENGTH_WITH_N_PARAMETERS(__n) (SIZE_WITH_N_PARAMETERS(__n) - 20)

static const u8 dst_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
static const u8 src_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

static void 
setup_common_header(struct wcdte *wc, struct csm_encaps_hdr *hdr)
{
	memcpy(hdr->ethhdr.h_dest, dst_mac, sizeof(dst_mac));
	memcpy(hdr->ethhdr.h_source, src_mac, sizeof(src_mac));
	hdr->ethhdr.h_proto = cpu_to_be16(ETH_P_CSM_ENCAPS);
}

static void 
setup_supervisor_header(struct wcdte *wc, struct csm_encaps_hdr *hdr)
{
	setup_common_header(wc, hdr);

	hdr->op_code = cpu_to_be16(CONTROL_PACKET_OPCODE);
	hdr->control = LITTLE_ENDIAN;
	hdr->seq_num = (wc->seq_num++)&0xf;
	hdr->channel = cpu_to_be16(SUPERVISOR_CHANNEL);
}

static void  
__wctc4xxp_create_channel_cmd(struct wcdte *wc, struct tcb *cmd, u16 timeslot)
{
	struct csm_create_channel_cmd *c; 
	c = hdr_from_cmd(cmd);

	BUG_ON(timeslot > 0x01ff);

	setup_supervisor_header(wc, &c->hdr);

	c->hdr.length =    LENGTH_WITH_N_PARAMETERS(2);
	c->hdr.index =     0x0;
	c->hdr.type  =     CONFIG_CHANGE_TYPE;
	c->hdr.class =     CONFIG_DEVICE_CLASS;
	c->hdr.function =  cpu_to_le16(SUPVSR_CREATE_CHANNEL);
	c->hdr.reserved =  0x0000;

	c->channel_type = cpu_to_le16(0x0002); /* Channel type is VoIP */
	c->timeslot =     cpu_to_le16(timeslot);

	cmd->flags |= WAIT_FOR_RESPONSE;
	cmd->data_len = sizeof(*c);
}

struct tcb * 
wctc4xxp_create_channel_cmd(struct wcdte *wc, u16 timeslot)
{
	struct tcb *cmd;
	if (!(cmd = alloc_cmd())) {
		return NULL;
	}
	__wctc4xxp_create_channel_cmd(wc, cmd, timeslot);
	return cmd;
}

void 
__wctc4xxp_create_set_arm_clk_cmd(struct wcdte *wc, struct tcb *cmd)
{
	struct csm_encaps_hdr *hdr = cmd->data;
	BUG_ON(SIZE_WITH_N_PARAMETERS(2) > cmd->data_len);

	setup_supervisor_header(wc, hdr);

	hdr->length =		LENGTH_WITH_N_PARAMETERS(2);
	hdr->index =		0x0;
	hdr->type =		CONFIG_CHANGE_TYPE;
	hdr->class =		CONFIG_DEVICE_CLASS;
	hdr->function =		cpu_to_le16(0x0411);
	hdr->reserved =		0x0000;
	hdr->params[0] =	cpu_to_le16(0x012c);
	hdr->params[1] =	cpu_to_le16(0x0000);

	cmd->flags |= WAIT_FOR_RESPONSE;
	cmd->data_len = SIZE_WITH_N_PARAMETERS(2);
	return;
}

struct tcb *
wctc4xxp_create_rtp_cmd(struct wcdte *wc, struct zt_transcoder_channel *dtc, size_t inbytes)
{
	const struct channel_pvt *cpvt = dtc->pvt;
	struct rtp_packet *packet;
	struct tcb *cmd;

	if (!(cmd = alloc_cmd())) {
		return NULL;
	}

	packet = cmd->data;

	BUG_ON(cmd->data_len < sizeof(*packet));
	
	/* setup the ethernet header */
	memcpy(packet->ethhdr.h_dest, dst_mac, sizeof(dst_mac));
	memcpy(packet->ethhdr.h_source, src_mac, sizeof(src_mac));
	packet->ethhdr.h_proto = cpu_to_be16(ETH_P_IP);
	
	/* setup the IP header */
	packet->iphdr.ihl =	5;
	packet->iphdr.version =	4;
	packet->iphdr.tos =	0;
	packet->iphdr.tot_len =	cpu_to_be16(inbytes+40);
	packet->iphdr.id =	cpu_to_be16(cpvt->seqno);
	packet->iphdr.frag_off=	cpu_to_be16(0x4000);
	packet->iphdr.ttl =	64;
	packet->iphdr.protocol=	0x11; /* UDP */
	packet->iphdr.check =	0;
	packet->iphdr.saddr =	cpu_to_be32(0xc0a80903);
	packet->iphdr.daddr =	cpu_to_be32(0xc0a80903);
	packet->iphdr.check =	ip_fast_csum((void*)&packet->iphdr, sizeof(struct iphdr));

	/* setup the UDP header */
	packet->udphdr.source =	cpu_to_be16(cpvt->timeslot_out_num + 0x5000);
	packet->udphdr.dest =	cpu_to_be16(cpvt->timeslot_in_num + 0x5000);
	packet->udphdr.len  =	cpu_to_be16(inbytes + sizeof(struct rtphdr) + sizeof(struct udphdr));
	packet->udphdr.check =	0;

	/* Setup the RTP header */
	packet->rtphdr.ver =		2;
	packet->rtphdr.padding =	0;
	packet->rtphdr.extension =	0;
	packet->rtphdr.csrc_count =	0;
	packet->rtphdr.marker =		0;
	packet->rtphdr.type =		wctc4xxp_zapfmt_to_dtefmt(dtc->srcfmt);
	packet->rtphdr.seqno =		cpu_to_be16(cpvt->seqno);
	packet->rtphdr.timestamp =	cpu_to_be32(cpvt->timestamp);
	packet->rtphdr.ssrc =		cpu_to_be32(cpvt->ssrc);

	cmd->data_len = sizeof(*packet) + inbytes;

	return cmd;
}
static void
wctc4xxp_cleanup_descriptor_ring(struct wctc4xxp_descriptor_ring *dr) 
{
	int i; 
	struct wctc4xxp_descriptor *d;
	
	/* NOTE: The DTE must be in the stopped state. */
	spin_lock_bh(&dr->lock);
	for (i = 0; i < DRING_SIZE; ++i) {
		d = wctc4xxp_descriptor(dr, i);
		if (d->buffer1) {
			dma_unmap_single(&dr->pdev->dev, d->buffer1, 
			                 SFRAME_SIZE, dr->direction);
			d->buffer1 = 0;
			/* Commands will also be sitting on the waiting for
			 * response list, so we want to make sure to delete
			 * them from that list as well. */
			list_del_init(&(dr->pending[i])->node);
			free_cmd(dr->pending[i]);
			dr->pending[i] = NULL;
		}
	}
	dr->head = 0;
	dr->tail = 0;
	dr->count = 0;
	spin_unlock_bh(&dr->lock);
	pci_free_consistent(dr->pdev, (sizeof(*d)+dr->padding) * DRING_SIZE, 
	                    dr->desc, dr->desc_dma); 
}

static void wctc4xxp_cleanup_command_list(struct wcdte *wc)
{
	struct tcb *cmd;
	LIST_HEAD(local_list);

	spin_lock_bh(&wc->cmd_list_lock);
	list_splice_init(&wc->cmd_list, &local_list);
	list_splice_init(&wc->waiting_for_response_list, &local_list);
	spin_unlock_bh(&wc->cmd_list_lock);

	while(!list_empty(&local_list)) {
		cmd = list_entry(local_list.next, struct tcb, node); 
		list_del_init(&cmd->node);
		free_cmd(cmd);
	}
}

/**
 * The command list is used to store commands that couldn't fit in the tx
 * descriptor list when they were requested. 
 */
static void 
wctc4xxp_add_to_command_list(struct wcdte *wc, struct tcb *cmd) 
{
	spin_lock_bh(&wc->cmd_list_lock);
	list_add_tail(&cmd->node, &wc->cmd_list);
	spin_unlock_bh(&wc->cmd_list_lock);
}

static void
wctc4xxp_add_to_response_list(struct wcdte *wc, struct tcb *cmd)
{
	spin_lock_bh(&wc->cmd_list_lock);
	list_add_tail(&cmd->node, &wc->waiting_for_response_list);
	spin_unlock_bh(&wc->cmd_list_lock);
}

static void
wctc4xxp_remove_from_response_list(struct wcdte *wc, struct tcb *cmd)
{
	spin_lock_bh(&wc->cmd_list_lock);
	list_del_init(&cmd->node);
	spin_unlock_bh(&wc->cmd_list_lock);
}

static void 
wctc4xxp_transmit_cmd(struct wcdte *wc, struct tcb *cmd)
{
	int res;

	if (cmd->data_len < MIN_PACKET_LEN) {
		memset((u8*)(cmd->data) + cmd->data_len, 0, 
		       MIN_PACKET_LEN-cmd->data_len);
		cmd->data_len = MIN_PACKET_LEN;
	} 
	WARN_ON(cmd->response);
	WARN_ON(cmd->flags & TX_COMPLETE);
	cmd->timeout = jiffies + HZ/4;
	if (cmd->flags & (__WAIT_FOR_ACK | __WAIT_FOR_RESPONSE)) {
		if (cmd->flags & __WAIT_FOR_RESPONSE) {
			/* We don't need both an ACK and a response.  Let's
			 * tell the DTE not to generate an ACK, and we'll just
			 * retry if we do not get the response within the
			 * timeout period. */
			struct csm_encaps_hdr *hdr = cmd->data;
			hdr->control |= SUPPRESS_ACK; 
		}
		WARN_ON(!list_empty(&cmd->node));
		wctc4xxp_add_to_response_list(wc, cmd);
		mod_timer(&wc->watchdog, jiffies + HZ/2);
	}
	if (!(cmd->flags & DO_NOT_CAPTURE)) wctc4xxp_net_capture_cmd(wc, cmd);
	if ((res=wctc4xxp_submit(wc->txd, cmd))) {
		if (-EBUSY == res) {
			/* Looks like we're out of room in the descriptor
			 * ring.  We'll add this command to the pending list
			 * and the interrupt service routine will pull from
			 * this list as it clears up room in the descriptor
			 * ring. */
			wctc4xxp_remove_from_response_list(wc, cmd);
			wctc4xxp_add_to_command_list(wc, cmd);
		} else {
			/* Unknown return value... */
			WARN_ON(1);
		}
	} else {
		wctc4xxp_transmit_demand_poll(wc);
	}
}

static int 
wctc4xxp_transmit_cmd_and_wait(struct wcdte *wc, struct tcb *cmd)
{
	cmd->flags |= DO_NOT_AUTO_FREE;
	wctc4xxp_transmit_cmd(wc, cmd);
	wait_for_completion(&cmd->complete);
	if (cmd->flags & DTE_CMD_TIMEOUT) {
		DTE_DEBUG(DTE_DEBUG_GENERAL, "Timeout waiting for command.\n");
		return -EIO;
	} 
	return 0;
}

static int wctc4xxp_create_channel_pair(struct wcdte *wc, 
               struct channel_pvt *cpvt, u8 simple, u8 complicated);
static int wctc4xxp_destroy_channel_pair(struct wcdte *wc, struct channel_pvt *cpvt);
static int __wctc4xxp_setup_channels(struct wcdte *wc);

static int
__wctc4xxp_send_cmd(struct wcdte *wc, struct tcb *cmd)
{
	int ret = 0;
	wctc4xxp_transmit_cmd(wc, cmd);
	wait_for_completion(&cmd->complete);
	if (cmd->flags & DTE_CMD_TIMEOUT) {
		ret = -EIO;
	}
	free_cmd(cmd);
	return ret;
}

static void 
wctc4xxp_init_state(struct channel_pvt *cpvt, encode_t encoder, 
                    unsigned int channel, struct wcdte *wc)
{
	memset(cpvt, 0, sizeof(*cpvt));
	cpvt->encoder = encoder;
	cpvt->wc = wc;

	cpvt->chan_in_num = INVALID;
	cpvt->chan_out_num = INVALID;

	cpvt->ssrc = 0x78;
	
	cpvt->timeslot_in_num = channel* 2;
	cpvt->timeslot_out_num = channel * 2;

	if (ENCODER == encoder) {
		cpvt->timeslot_out_num++;
	} else {
		cpvt->timeslot_in_num++;
	}
	spin_lock_init(&cpvt->lock);
	INIT_LIST_HEAD(&cpvt->rx_queue);
}

static unsigned int 
wctc4xxp_getctl(struct wcdte *wc, unsigned int addr)
{
	unsigned int val;
	spin_lock_bh(&wc->reglock);
	val = __wctc4xxp_getctl(wc, addr);
	spin_unlock_bh(&wc->reglock);
	return val;
}

static void 
wctc4xxp_cleanup_channel_private(struct wcdte *wc, struct channel_pvt *cpvt)
{
	struct tcb *cmd, *temp;
	LIST_HEAD(local_list);

	spin_lock_bh(&cpvt->lock);
	list_splice_init(&cpvt->rx_queue, &local_list);
	spin_unlock_bh(&cpvt->lock);

	list_for_each_entry_safe(cmd, temp, &local_list, node) {
		list_del(&cmd->node);
		free_cmd(cmd);
	}
}

static int
wctc4xxp_mark_channel_complement_built(struct wcdte *wc, 
	struct zt_transcoder_channel *dtc)
{
	int index;
	struct channel_pvt *cpvt = dtc->pvt;
	struct zt_transcoder_channel *compl_dtc;
	struct channel_pvt *compl_cpvt;

	BUG_ON(!cpvt);
	index = cpvt->timeslot_in_num/2;
	BUG_ON(index >= wc->numchannels);
	if (cpvt->encoder == 1) {
		compl_dtc = &(wc->udecode->channels[index]);
	} else {
		compl_dtc = &(wc->uencode->channels[index]);
	}

	/* It shouldn't already have been built... */
	WARN_ON(zt_tc_is_built(compl_dtc));
	compl_dtc->built_fmts = dtc->dstfmt | dtc->srcfmt;
	compl_cpvt = compl_dtc->pvt;
	DTE_DEBUG(DTE_DEBUG_CHANNEL_SETUP, "dtc: %p is the complement to %p\n", compl_dtc, dtc);
	compl_cpvt->chan_in_num = cpvt->chan_out_num;
	compl_cpvt->chan_out_num = cpvt->chan_in_num;
	zt_tc_set_built(compl_dtc);
	wctc4xxp_cleanup_channel_private(wc, compl_cpvt);

	return 0;
}

static int 
do_channel_allocate(struct zt_transcoder_channel *dtc)
{
	struct channel_pvt *cpvt = dtc->pvt;
	struct wcdte *wc = cpvt->wc;
	u8 wctc4xxp_srcfmt; /* Digium Transcoder Engine Source Format */
	u8 wctc4xxp_dstfmt; /* Digium Transcoder Engine Dest Format */
	int res;

	down(&wc->chansem);
	DTE_DEBUG(DTE_DEBUG_CHANNEL_SETUP, 
	          "Entering %s for channel %p.\n", __FUNCTION__, dtc);
	/* Anything on the rx queue now is old news... */
	zt_tc_clear_data_waiting(dtc);
	wctc4xxp_cleanup_channel_private(wc, cpvt);
	DTE_DEBUG(DTE_DEBUG_CHANNEL_SETUP, "Allocating a new channel: %p.\n", dtc);
	wctc4xxp_srcfmt = wctc4xxp_zapfmt_to_dtefmt(dtc->srcfmt);
	wctc4xxp_dstfmt = wctc4xxp_zapfmt_to_dtefmt(dtc->dstfmt);
	res = wctc4xxp_create_channel_pair(wc, cpvt, wctc4xxp_srcfmt, 
	                                   wctc4xxp_dstfmt);
	if (res) {
		/* There was a problem creating the channel.... */
		up(&wc->chansem);
		return res;
	}
	/* Mark this channel as built */
	zt_tc_set_built(dtc);
	dtc->built_fmts = dtc->dstfmt | dtc->srcfmt;
	/* Mark the channel complement (other half of encoder/decoder pair) as built */
	res = wctc4xxp_mark_channel_complement_built(wc, dtc);
	up(&wc->chansem);
	zt_transcoder_alert(dtc);
	return res;
}

static int
wctc4xxp_operation_allocate(struct zt_transcoder_channel *dtc)
{
	struct wcdte *wc = ((struct channel_pvt*)(dtc->pvt))->wc;

	if (unlikely(test_bit(DTE_SHUTDOWN, &wc->flags))) {
		/* The shudown flags can also be set if there is a
		 * catastrophic failure. */
		return -EIO;
	}

	if (zt_tc_is_built(dtc)) {
		DTE_DEBUG(DTE_DEBUG_CHANNEL_SETUP, 
		          "Allocating channel %p which is already built.\n", dtc);
		return 0;
	}
	return do_channel_allocate(dtc);
}

static int
wctc4xxp_operation_release(struct zt_transcoder_channel *dtc)
{
	int res;
	int index;
	/* This is the 'complimentary channel' to dtc.  I.e., if dtc is an
	 * encoder, compl_dtc is the decoder and vice-versa */
	struct zt_transcoder_channel *compl_dtc;
	struct channel_pvt *compl_cpvt;
	struct channel_pvt *cpvt = dtc->pvt;
	struct wcdte *wc = cpvt->wc;

	BUG_ON(!cpvt);
	BUG_ON(!wc);

	if (unlikely(test_bit(DTE_SHUTDOWN, &wc->flags))) {
		/* The shudown flags can also be set if there is a
		 * catastrophic failure. */
		return -EIO;
	}

	/* !!!SRR!!! change this back to down after troubleshooting */
	if (down_interruptible(&wc->chansem)) {
		return -EINTR;
	}
	/* Remove any packets that are waiting on the outbound queue. */
	wctc4xxp_cleanup_channel_private(wc, cpvt);
	index = cpvt->timeslot_in_num/2;
	BUG_ON(index >= wc->numchannels);
	if (ENCODER == cpvt->encoder) {
		compl_dtc = &(wc->udecode->channels[index]);
	} else {
		compl_dtc = &(wc->uencode->channels[index]);
	}
	BUG_ON(!compl_dtc);
	if (!zt_tc_is_built(compl_dtc)) {
		DTE_DEBUG(DTE_DEBUG_CHANNEL_SETUP, 
			"Releasing a channel that was never built.\n");
		res = 0;
		goto error_exit;
	}
	/* If the channel complement (other half of the encoder/decoder pair) is
	 * being used... */
	if (zt_tc_is_busy(compl_dtc)) {
		res = -EBUSY;
		goto error_exit;
	}
	if ((res = wctc4xxp_destroy_channel_pair(wc, cpvt))) {
		goto error_exit;
	}
	DTE_DEBUG(DTE_DEBUG_CHANNEL_SETUP, "Releasing channel: %p\n", dtc);
	/* Mark this channel as not built */
	zt_tc_clear_built(dtc);
	dtc->built_fmts = 0;
	cpvt->chan_in_num = INVALID;
	cpvt->chan_out_num = INVALID;
	/* Mark the channel complement as not built */
	zt_tc_clear_built(compl_dtc);
	compl_dtc->built_fmts = 0;
	compl_cpvt = compl_dtc->pvt;
	compl_cpvt->chan_in_num = INVALID;
	compl_cpvt->chan_out_num = INVALID;
	cpvt->wctc4xxp_seqno_rcv = 0;
error_exit:
	up(&wc->chansem);
	return res;
}

static inline struct tcb* 
get_ready_cmd(struct zt_transcoder_channel *dtc)
{
	struct channel_pvt *cpvt = dtc->pvt;
	struct tcb *cmd;
	spin_lock_bh(&cpvt->lock);
	if (!list_empty(&cpvt->rx_queue)) {
		WARN_ON(!zt_tc_is_data_waiting(dtc));
		cmd = list_entry(cpvt->rx_queue.next, struct tcb, node); 
		list_del_init(&cmd->node);
	} else {
		cmd = NULL;
	}
	if (list_empty(&cpvt->rx_queue)) {
		zt_tc_clear_data_waiting(dtc);
	}
	spin_unlock_bh(&cpvt->lock);
	return cmd;
}

/* Called with a buffer in which to copy a transcoded frame. */
static ssize_t 
wctc4xxp_read(struct file *file, char __user *frame, size_t count, loff_t *ppos)
{
	ssize_t ret;
	struct zt_transcoder_channel *dtc = file->private_data;
	struct channel_pvt *cpvt = dtc->pvt;
	struct wcdte *wc = cpvt->wc;
	struct tcb *cmd;
	struct rtp_packet *packet;
	ssize_t payload_bytes;
	u16 rtp_eseq;

	BUG_ON(!dtc);
	BUG_ON(!cpvt);

	if (unlikely(test_bit(DTE_SHUTDOWN, &wc->flags))) {
		/* The shudown flags can also be set if there is a
		 * catastrophic failure. */
		return -EIO;
	}

	if (!(cmd = get_ready_cmd(dtc))) {
		if (file->f_flags & O_NONBLOCK) {
			return -EAGAIN;
		} else {
			ret = wait_event_interruptible(dtc->ready, 
			          zt_tc_is_data_waiting(dtc));
			if (-ERESTARTSYS == ret) {
				/* Signal interrupted the wait */
				return -EINTR;
			} else {
				/* List went not empty. */
				cmd = get_ready_cmd(dtc);
			} 
		}
	}

	BUG_ON(!cmd);
	packet = cmd->data;
	
	payload_bytes = be16_to_cpu(packet->udphdr.len) - sizeof(struct rtphdr) - 
	          sizeof(struct udphdr);

	if (count < payload_bytes) {
		DTE_PRINTK(ERR, "Insufficient room to copy read data. Dropping packet.\n");
		free_cmd(cmd);
		return -EFBIG;
	}

	atomic_inc(&cpvt->stats.packets_received);

	if (0 == cpvt->wctc4xxp_seqno_rcv) {
		cpvt->wctc4xxp_seqno_rcv = 1;
		cpvt->last_dte_seqno = be16_to_cpu(packet->rtphdr.seqno);
	} else {
		rtp_eseq = ++cpvt->last_dte_seqno;
		if ( be16_to_cpu(packet->rtphdr.seqno) != rtp_eseq )
			DTE_DEBUG(DTE_DEBUG_GENERAL,
			 "Bad seqno from DTE! [%04X][%d][%d][%d]\n", 
			 be16_to_cpu(packet->rtphdr.seqno), 
			 (be16_to_cpu(packet->udphdr.dest) - 0x5000),
			 be16_to_cpu(packet->rtphdr.seqno), 
			 rtp_eseq);

		cpvt->last_dte_seqno = be16_to_cpu(packet->rtphdr.seqno);
	}

	if (unlikely(copy_to_user(frame, &packet->payload[0], payload_bytes))) {
		DTE_PRINTK(ERR, "Failed to copy data in %s\n", __FUNCTION__);
		free_cmd(cmd);
		return -EFAULT;
	}

	free_cmd(cmd);

	return payload_bytes;
}

/* Called with a frame in the srcfmt that is to be transcoded into the dstfmt. */
static ssize_t 
wctc4xxp_write(struct file *file, const char __user *frame, size_t count, loff_t *ppos)
{
	struct zt_transcoder_channel *dtc = file->private_data;
	struct channel_pvt *cpvt = dtc->pvt;
	struct wcdte *wc = cpvt->wc;
	struct tcb *cmd;

	BUG_ON(!cpvt);
	BUG_ON(!wc);

	if (unlikely(test_bit(DTE_SHUTDOWN, &wc->flags))) {
		/* The shudown flags can also be set if there is a
		 * catastrophic failure. */
		return -EIO;
	}

	if (!test_bit(ZT_TC_FLAG_CHAN_BUILT, &dtc->flags)) {
		return -EAGAIN;
	}

	if (count < 2) {
		DTE_DEBUG(DTE_DEBUG_GENERAL, 
		   "Cannot request to transcode a packet that is less than 2 bytes.\n");
		return -EINVAL;
	}

	if (unlikely(count > SFRAME_SIZE - sizeof(struct rtp_packet))) {
		DTE_DEBUG(DTE_DEBUG_GENERAL, 
		   "Cannot transcode packet of %Zu bytes. This exceeds the " \
		   "maximum size of %Zu bytes.\n", count, 
		   SFRAME_SIZE - sizeof(struct rtp_packet));
		return -EINVAL;
	}

	if (ZT_FORMAT_G723_1 == dtc->srcfmt) {
		if ((G723_5K_BYTES != count) && (G723_6K_BYTES != count)) {
			DTE_DEBUG(DTE_DEBUG_GENERAL, 
			   "Trying to transcode packet into G723 format " \
			   "that is %Zu bytes instead of the expected " \
			   "%d/%d bytes.\n", count, G723_5K_BYTES, G723_6K_BYTES);
			return -EINVAL;
		}
		cpvt->timestamp += G723_SAMPLES;
	} else {
		/* Same for ulaw and alaw */
		cpvt->timestamp += G729_SAMPLES;
	}

	if (!(cmd = wctc4xxp_create_rtp_cmd(wc, dtc, count))) {
		return -ENOMEM;
	}
	/* Copy the data directly from user space into the command buffer. */
	if (copy_from_user(&((struct rtp_packet*)(cmd->data))->payload[0], frame, count)) {
		DTE_PRINTK(ERR, "Failed to copy packet from userspace.\n");
		free_cmd(cmd);
		return -EFAULT;
	}
	cpvt->seqno += 1;

	DTE_DEBUG(DTE_DEBUG_RTP_TX, 
	    "Sending packet of %Zu byte on channel (%p).\n", count, dtc);

	wctc4xxp_transmit_cmd(wc, cmd);
	atomic_inc(&cpvt->stats.packets_sent);

	return count;
}

static void 
wctc4xxp_send_ack(struct wcdte *wc, u8 seqno, __be16 channel)
{
	struct tcb *cmd;
	struct csm_encaps_hdr *hdr;
	if (!(cmd = __alloc_cmd(ALLOC_FLAGS, 0))) {
		WARN_ON(1);
		return;
	}
	hdr = cmd->data;
	BUG_ON(sizeof(*hdr) > cmd->data_len);
	setup_common_header(wc, hdr);
	hdr->op_code = cpu_to_be16(0x0001);
	hdr->seq_num = seqno;
	hdr->control = 0xe0;
	hdr->channel = channel;

	cmd->data_len = sizeof(*hdr);
	wctc4xxp_transmit_cmd(wc, cmd);
}

static void 
do_rx_response_packet(struct wcdte *wc, struct tcb *cmd)
{
	const struct csm_encaps_hdr *listhdr;
	const struct csm_encaps_hdr *rxhdr;
	struct tcb *pos;
	struct tcb *temp;

	rxhdr = cmd->data;
	spin_lock_bh(&wc->cmd_list_lock);
	list_for_each_entry_safe(pos, temp, &wc->waiting_for_response_list, node) {
		listhdr = pos->data;
		if ((listhdr->function == rxhdr->function) &&
		    (listhdr->channel == rxhdr->channel)) {
			list_del_init(&pos->node);
			pos->flags &= ~(__WAIT_FOR_RESPONSE);
			WARN_ON(pos->response);
			pos->response = cmd;
			complete(&pos->complete);
			break;
		}
	}
	spin_unlock_bh(&wc->cmd_list_lock);
}

static void 
do_rx_ack_packet(struct wcdte *wc, struct tcb *cmd)
{
	const struct csm_encaps_hdr *listhdr;
	const struct csm_encaps_hdr *rxhdr;
	struct tcb *pos;
	struct tcb *temp;

	rxhdr = cmd->data;

	spin_lock_bh(&wc->cmd_list_lock);
	list_for_each_entry_safe(pos, temp, &wc->waiting_for_response_list, node) {
		listhdr = pos->data;
		if (cpu_to_be16(0xefed) == listhdr->ethhdr.h_proto) {
			wc->seq_num = (rxhdr->seq_num + 1) & 0xff;
			WARN_ON(!(pos->flags & DO_NOT_AUTO_FREE));
			list_del_init(&pos->node);
			complete(&pos->complete);
		} else if ((listhdr->seq_num == rxhdr->seq_num) && 
			   (listhdr->channel == rxhdr->channel)) {
			if (pos->flags & __WAIT_FOR_RESPONSE) {
				pos->flags &= ~(__WAIT_FOR_ACK);
			} else {
				list_del_init(&pos->node);

				if (pos->flags & DO_NOT_AUTO_FREE) {
					complete(&pos->complete);
				} else {
					free_cmd(pos);
				}
			}
			break;
		} 
	}
	spin_unlock_bh(&wc->cmd_list_lock);

	/* There is never a reason to store up the ack packets. */
	free_cmd(cmd);
}

static inline int 
is_response(const struct csm_encaps_hdr *hdr)
{
	return ((0x02 == hdr->type) || (0x04 == hdr->type)) ? 1 : 0;
}

static void 
receive_csm_encaps_packet(struct wcdte *wc, struct tcb *cmd)
{
	const struct csm_encaps_hdr *hdr = cmd->data;

	if (!(hdr->control & MESSAGE_PACKET)) {

		if (!(hdr->control & SUPPRESS_ACK)) {
			wctc4xxp_send_ack(wc, hdr->seq_num, hdr->channel);
		}

		if (is_response(hdr)) {
			do_rx_response_packet(wc, cmd);
		} else if (0xc1 == hdr->type) {
			if (0x75 == hdr->class) {
				DTE_PRINTK(WARNING,
				   "Received alert (0x%04x) from dsp\n", 
				   le16_to_cpu(hdr->params[0]));
			}
			free_cmd(cmd);
		} else if (0xd4 == hdr->type) {
			if (hdr->params[0] != le16_to_cpu(0xffff)) {
				DTE_PRINTK(WARNING, 
				   "DTE Failed self test (%04x).\n", 
				   le16_to_cpu(hdr->params[0]));
			} else if (hdr->params[1] != le16_to_cpu(0x000c)) {
				DTE_PRINTK(WARNING, 
				   "Unexpected ERAM status (%04x).\n", 
				   le16_to_cpu(hdr->params[1]));
			} else {
				wctc4xxp_set_ready(wc);
				wake_up(&wc->waitq);
			}
			free_cmd(cmd);
		} else {
			DTE_PRINTK(WARNING, "Unknown command type received. %02x\n", hdr->type);
			free_cmd(cmd);
		}
	} else {
		do_rx_ack_packet(wc, cmd);
	}
}

static void 
queue_rtp_packet(struct wcdte *wc, struct tcb *cmd)
{
	int index;
	struct zt_transcoder_channel *dtc;
	struct channel_pvt *cpvt;
	struct rtp_packet *packet = cmd->data;

	index = (be16_to_cpu(packet->udphdr.dest) - 0x5000) / 2;
	if (unlikely(index >= wc->numchannels)) {
		DTE_PRINTK(ERR, "Invalid channel number in response from DTE.\n");
		free_cmd(cmd);
		return;
	}

	switch (packet->rtphdr.type) {
	case 0x00: 
	case 0x08:
		dtc = &(wc->udecode->channels[index]);
		break;
	case 0x04:
	case 0x12:
		dtc = &(wc->uencode->channels[index]);
		break;
	default:
		DTE_PRINTK(ERR, "Unknown codec in packet (0x%02x).\n",\
		           packet->rtphdr.type);
		free_cmd(cmd);
		return;
	}

	cpvt = dtc->pvt;
	spin_lock_bh(&cpvt->lock);
	list_add_tail(&cmd->node, &cpvt->rx_queue);
	spin_unlock_bh(&cpvt->lock);
	zt_tc_set_data_waiting(dtc);
	zt_transcoder_alert(dtc);
	return;
}

static inline void 
wctc4xxp_receiveprep(struct wcdte *wc, struct tcb *cmd)
{
	const struct ethhdr *ethhdr = (const struct ethhdr*)(cmd->data);

	if (cpu_to_be16(ETH_P_IP) == ethhdr->h_proto) {
		queue_rtp_packet(wc, cmd);
	} else if (cpu_to_be16(ETH_P_CSM_ENCAPS) == ethhdr->h_proto) {
		receive_csm_encaps_packet(wc, cmd);
	} else {
		DTE_PRINTK(WARNING, 
		   "Unknown packet protocol recieved: %04x.\n", 
		   be16_to_cpu(ethhdr->h_proto));
		free_cmd(cmd);
	}
}

static inline void service_dte(struct wcdte *wc) 
{
	struct tcb *cmd;

	/* 
	 * Process the received packets
	 */
	while((cmd = wctc4xxp_retrieve(wc->rxd))) {
		struct tcb *newcmd;

		wctc4xxp_net_capture_cmd(wc, cmd);

		if(!(newcmd = __alloc_cmd(ALLOC_FLAGS, 0))) {
			DTE_PRINTK(ERR, "Out of memory in %s.\n", __FUNCTION__);
		} else {
			newcmd->data = kmalloc(1518, ALLOC_FLAGS);
			if (!newcmd->data) {
				DTE_PRINTK(ERR, "out of memory in %s " \
				    "again.\n", __FUNCTION__);
			}
			newcmd->data_len = 1518;
			if (wctc4xxp_submit(wc->rxd, newcmd)) {
				DTE_PRINTK(ERR, "Failed submit in %s\n", __FUNCTION__);
				free_cmd(newcmd);
			}
			wctc4xxp_receive_demand_poll(wc);
		}
		wctc4xxp_receiveprep(wc, cmd);
	}
	wctc4xxp_receive_demand_poll(wc);
	
	/* 
	 * Process the transmit packets
	 */
	while((cmd = wctc4xxp_retrieve(wc->txd))) {
		if (!(cmd->flags & (__WAIT_FOR_ACK | __WAIT_FOR_RESPONSE))) {
			spin_lock_bh(&wc->cmd_list_lock);
			list_del_init(&cmd->node);
			spin_unlock_bh(&wc->cmd_list_lock);
			if (DO_NOT_AUTO_FREE & cmd->flags) {
				complete(&cmd->complete);
			} else {
				free_cmd(cmd);
			}
		}
		/* We've freed up a spot in the hardware ring buffer.  If
		 * another packet is queued up, let's submit it to the
		 * hardware. */
		spin_lock_bh(&wc->cmd_list_lock);
		if (!list_empty(&wc->cmd_list)) {
			cmd = list_entry(wc->cmd_list.next, struct tcb, node);
			list_del_init(&cmd->node);
		} else {
			cmd = NULL;
		}
		spin_unlock_bh(&wc->cmd_list_lock);

		if (cmd) {
			wctc4xxp_transmit_cmd(wc, cmd);
		}
	}
}


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
static void deferred_work_func(void *param)
{
	struct wcdte *wc = param;
#else
static void deferred_work_func(struct work_struct *work)
{
	struct wcdte *wc = container_of(work, struct wcdte, deferred_work);
#endif
	service_dte(wc);
}

ZAP_IRQ_HANDLER(wctc4xxp_interrupt)
{
	struct wcdte *wc = dev_id;
	u32 ints;
	u32 reg;
#define TX_COMPLETE_INTERRUPT 0x00000001
#define RX_COMPLETE_INTERRUPT 0x00000040
#define NORMAL_INTERRUPTS (TX_COMPLETE_INTERRUPT | RX_COMPLETE_INTERRUPT)

	/* Read and clear interrupts */
	ints = __wctc4xxp_getctl(wc, 0x0028);

	ints &= wc->intmask;
	
	if (!ints)
		return IRQ_NONE;

	if (likely(ints & NORMAL_INTERRUPTS)) {
		reg = 0;
		if (ints & TX_COMPLETE_INTERRUPT) {
			reg |= TX_COMPLETE_INTERRUPT;
		}
		if (ints & RX_COMPLETE_INTERRUPT) {
			reg |= RX_COMPLETE_INTERRUPT;
		}
#if DEFERRED_PROCESSING == WORKQUEUE
		schedule_work(&wc->deferred_work);
#elif DEFERRED_PROCESSING == INTERRUPT
#error "You will need to change the locks if you want to run the processing in the interrupt handler."
#else
#error "Define a deferred processing function in kernel/wctc4xxp/wctc4xxp.h"
#endif
		__wctc4xxp_setctl(wc, 0x0028, reg);
	} else {
		if ((ints & 0x00008000) && debug)
			DTE_PRINTK(INFO, "Abnormal Interrupt.\n");

		if ((ints & 0x00002000) && debug)
			DTE_PRINTK(INFO, "Fatal Bus Error INT\n");

		if ((ints & 0x00000100) && debug)
			DTE_PRINTK(INFO, "Receive Stopped INT\n");

		if ((ints & 0x00000080) && debug) {
			DTE_PRINTK(INFO, "Receive Desciptor Unavailable INT " \
			    "(%d)\n", wctc4xxp_getcount(wc->rxd));
		}

		if ((ints & 0x00000020) && debug)
			DTE_PRINTK(INFO, "Transmit Under-flow INT\n");

		if ((ints & 0x00000008) && debug)
			DTE_PRINTK(INFO, "Jabber Timer Time-out INT\n");

		if ((ints & 0x00000004) && debug)
			; // printk("wcdte: Transmit Descriptor Unavailable INT\n");

		if ((ints & 0x00000002) && debug)
			DTE_PRINTK(INFO, "Transmit Processor Stopped INT\n");

		/* Clear all the pending interrupts. */
		__wctc4xxp_setctl(wc, 0x0028, ints);
	}
	return IRQ_HANDLED;
}

static int 
wctc4xxp_hardware_init(struct wcdte *wc)
{
	/* Hardware stuff */
	u32 reg;
	unsigned long newjiffies;
	u8 cache_line_size;
	const u32 DEFAULT_PCI_ACCESS = 0xfff80000;
	
	/* Enable I/O Access */
	pci_read_config_dword(wc->pdev, 0x0004, &reg);
	reg |= 0x00000007;
	pci_write_config_dword(wc->pdev, 0x0004, reg);

	if (pci_read_config_byte(wc->pdev, 0x0c, &cache_line_size)) {
		/* \todo Print an error message... */
		return -EIO;
	}
	switch (cache_line_size) {
	case 0x08:
		reg = DEFAULT_PCI_ACCESS | (0x1 << 14);
		break;
	case 0x10:
		reg = DEFAULT_PCI_ACCESS | (0x2 << 14);
		break;
	case 0x20: 
		reg = DEFAULT_PCI_ACCESS | (0x3 << 14);
		break;
	default:
		reg = 0xfe584202;
		break;
	}
	
	reg |= ((wc->txd->padding / sizeof(u32)) << 2) & 0x7c;

	/* Reset the DTE... */
	wctc4xxp_setctl(wc, 0x0000, reg | 1);
	newjiffies = jiffies + HZ; /* One second timeout */
	/* ...and wait for it to come out of reset. */
	while(((wctc4xxp_getctl(wc,0x0000)) & 0x00000001) && (newjiffies > jiffies)) {
		msleep(1);
	}

	wctc4xxp_setctl(wc, 0x0000, reg);
	
	/* Configure watchdogs, access, etc */
	wctc4xxp_setctl(wc, 0x0030, 0x00280048);
	wctc4xxp_setctl(wc, 0x0078, 0x00000013 /* | (1 << 28) */);

	reg = wctc4xxp_getctl(wc, 0x00fc);
	wctc4xxp_setctl(wc, 0x00fc, (reg & ~0x7) | 0x7);

	reg = wctc4xxp_getctl(wc, 0x00fc);

	return 0;
}

static void 
wctc4xxp_setintmask(struct wcdte *wc, unsigned int intmask)
{
	wc->intmask = intmask;
	wctc4xxp_setctl(wc, 0x0038, intmask);
}

static void 
wctc4xxp_enable_interrupts(struct wcdte *wc)
{
	wctc4xxp_setintmask(wc, 0x000180c1);
	// wctc4xxp_setintmask(wc, 0xffffffff);
}

static void 
wctc4xxp_start_dma(struct wcdte *wc)
{
	int res;
	int i;
	u32 reg;
	struct tcb *cmd;
	
	for (i = 0; i < DRING_SIZE; ++i) { 
		if (!(cmd = alloc_cmd())) {
			WARN();
			return;
		}
		cmd->data_len = SFRAME_SIZE;
		if ((res=wctc4xxp_submit(wc->rxd, cmd))) {
			/* When we're starting the DMA, we should always be
			 * able to fill the ring....so something is wrong
			 * here. */
			WARN();
			free_cmd(cmd);
			break;
		}
	}
	wmb();
	wctc4xxp_setctl(wc, 0x0020, wc->txd->desc_dma);
	wctc4xxp_setctl(wc, 0x0018, wc->rxd->desc_dma);

	/* Start receiver/transmitter */
	reg = wctc4xxp_getctl(wc, 0x0030);
	wctc4xxp_setctl(wc, 0x0030, reg | 0x00002002);		/* Start XMT and RCD */
	wctc4xxp_receive_demand_poll(wc);
	reg = wctc4xxp_getctl(wc, 0x0028);
	wctc4xxp_setctl(wc, 0x0028, reg);

}

static void 
wctc4xxp_stop_dma(struct wcdte *wc)
{
	/* Disable interrupts and reset */
	unsigned int reg;
	unsigned long newjiffies;
	/* Disable interrupts */
	wctc4xxp_setintmask(wc, 0x00000000);
	wctc4xxp_setctl(wc, 0x0084, 0x00000000);
	wctc4xxp_setctl(wc, 0x0048, 0x00000000);
	/* Reset the part to be on the safe side */
	reg = wctc4xxp_getctl(wc, 0x0000);
	reg |= 0x00000001;
	wctc4xxp_setctl(wc, 0x0000, reg);

	newjiffies = jiffies + HZ; /* One second timeout */
	/* We'll wait here for the part to come out of reset */
	while(((wctc4xxp_getctl(wc,0x0000)) & 0x00000001) && (newjiffies > jiffies)) {
		msleep(1);
	}
}

static void 
wctc4xxp_disable_interrupts(struct wcdte *wc)	
{
	/* Disable interrupts */
	wctc4xxp_setintmask(wc, 0x00000000);
	wctc4xxp_setctl(wc, 0x0084, 0x00000000);
}

#define MDIO_SHIFT_CLK		0x10000
#define MDIO_DATA_WRITE1 	0x20000
#define MDIO_ENB		0x00000
#define MDIO_ENB_IN		0x40000
#define MDIO_DATA_READ		0x80000

static int 
wctc4xxp_read_phy(struct wcdte *wc, int location)
{
	int i;
	long mdio_addr = 0x0048;
	int read_cmd = (0xf6 << 10) | (1 << 5) | location;
	int retval = 0;

	/* Establish sync by sending at least 32 logic ones. */
	for (i = 32; i >= 0; i--) {
		wctc4xxp_setctl(wc, mdio_addr, MDIO_ENB | MDIO_DATA_WRITE1);
		wctc4xxp_getctl(wc, mdio_addr);
		wctc4xxp_setctl(wc, mdio_addr, MDIO_ENB | MDIO_DATA_WRITE1 | MDIO_SHIFT_CLK);
		wctc4xxp_getctl(wc, mdio_addr);
	}

	/* Shift the read command bits out. */
	for (i = 17; i >= 0; i--) {
		int dataval = (read_cmd & (1 << i)) ? MDIO_DATA_WRITE1 : 0;

		wctc4xxp_setctl(wc, mdio_addr, MDIO_ENB | dataval);
		wctc4xxp_getctl(wc, mdio_addr);
		wctc4xxp_setctl(wc, mdio_addr, MDIO_ENB | dataval | MDIO_SHIFT_CLK);
		wctc4xxp_getctl(wc, mdio_addr);
	}

	/* Read the two transition, 16 data, and wire-idle bits. */
	for (i = 19; i > 0; i--) {
		wctc4xxp_setctl(wc, mdio_addr, MDIO_ENB_IN);
		wctc4xxp_getctl(wc, mdio_addr);
		retval = (retval << 1) | ((wctc4xxp_getctl(wc, mdio_addr) & MDIO_DATA_READ) ? 1 : 0);
		wctc4xxp_setctl(wc, mdio_addr, MDIO_ENB_IN | MDIO_SHIFT_CLK);
		wctc4xxp_getctl(wc, mdio_addr);
	}
	retval = (retval>>1) & 0xffff;
	return retval;
}

static void 
wctc4xxp_write_phy(struct wcdte *wc, int location, int value)
{
	int i;
	int cmd = (0x5002 << 16) | (1 << 23) | (location<<18) | value;
	long mdio_addr = 0x0048;

	/* Establish sync by sending 32 logic ones. */
	for (i = 32; i >= 0; i--) {
		wctc4xxp_setctl(wc, mdio_addr, MDIO_ENB | MDIO_DATA_WRITE1);
		wctc4xxp_getctl(wc, mdio_addr);
		wctc4xxp_setctl(wc, mdio_addr, MDIO_ENB | MDIO_DATA_WRITE1 | MDIO_SHIFT_CLK);
		wctc4xxp_getctl(wc, mdio_addr);
	}
	/* Shift the command bits out. */
	for (i = 31; i >= 0; i--) {
		int dataval = (cmd & (1 << i)) ? MDIO_DATA_WRITE1 : 0;
		wctc4xxp_setctl(wc, mdio_addr, MDIO_ENB | dataval);
		wctc4xxp_getctl(wc, mdio_addr);
		wctc4xxp_setctl(wc, mdio_addr, MDIO_ENB | dataval | MDIO_SHIFT_CLK);
		wctc4xxp_getctl(wc, mdio_addr);
	}
	/* Clear out extra bits. */
	for (i = 2; i > 0; i--) {
		wctc4xxp_setctl(wc, mdio_addr, MDIO_ENB_IN);
		wctc4xxp_getctl(wc, mdio_addr);
		wctc4xxp_setctl(wc, mdio_addr, MDIO_ENB_IN | MDIO_SHIFT_CLK);
		wctc4xxp_getctl(wc, mdio_addr);
	}
	return;
}

static int
wctc4xxp_wait_for_link(struct wcdte *wc)
{
	int reg;
	unsigned int delay_count = 0;
	do {
		reg = wctc4xxp_getctl(wc, 0x00fc);
		mdelay(2);
		delay_count++;

		if (delay_count >= 5000) {
			DTE_PRINTK(ERR, "Failed to link to DTE processor!\n");
			return -EIO;
		}
	} while ((reg & 0xE0000000) != 0xE0000000);
	return 0;
}

static int 
wctc4xxp_load_firmware(struct wcdte *wc, const struct firmware *firmware)
{
	unsigned int byteloc;
	unsigned int last_byteloc;
	unsigned int length;
	struct tcb *cmd;

	byteloc = 17;

	if (!(cmd = alloc_cmd())) {
		return -ENOMEM;
	}	
	if (1518 > cmd->data_len) {
		cmd->data = kmalloc(1518, GFP_KERNEL);
		if (!(cmd->data)) {
			free_cmd(cmd);
			return -ENOMEM;
		}
		cmd->data_len = 1518;
	}
	while (byteloc < (firmware->size-20)) {
		last_byteloc = byteloc;
		length = (firmware->data[byteloc] << 8) | firmware->data[byteloc+1];
		byteloc += 2;
		cmd->data_len = length;
		BUG_ON(length > cmd->data_len);
		memcpy(cmd->data, &firmware->data[byteloc], length);
		byteloc += length;
		cmd->flags = WAIT_FOR_ACK;
		wctc4xxp_transmit_cmd(wc, cmd);
		wait_for_completion(&cmd->complete);
		if (cmd->flags & DTE_CMD_TIMEOUT) {
			free_cmd(cmd);
			DTE_PRINTK(ERR, "Failed to load firmware.\n");
			return -EIO;
		}
	} 
	free_cmd(cmd);
	if (!wait_event_timeout(wc->waitq, wctc4xxp_is_ready(wc), 15*HZ)) {
		DTE_PRINTK(ERR, "Failed to boot firmware.\n");
		return -EIO;
	}
	return 0;
}

static int
wctc4xxp_turn_off_booted_led(struct wcdte *wc)
{
	int ret = 0;
	int reg;
	/* Turn off auto negotiation */
	wctc4xxp_write_phy(wc, 0, 0x2100);
	DTE_DEBUG(DTE_DEBUG_GENERAL, "PHY register 0 = %X\n", 
	   wctc4xxp_read_phy(wc, 0));

	/* Set reset */
	wctc4xxp_setctl(wc, 0x00A0, 0x04000000);

	/* Wait 4 ms to ensure processor reset */
	mdelay(4);

	/* Clear reset */
	wctc4xxp_setctl(wc, 0x00A0, 0x04080000);

	/* Wait for the ethernet link */
	if ((ret = wctc4xxp_wait_for_link(wc))) {
		return ret;
	}

	/* Turn off booted LED */
	wctc4xxp_setctl(wc, 0x00A0, 0x04084000);
	reg = wctc4xxp_getctl(wc, 0x00fc);
	DTE_DEBUG(DTE_DEBUG_GENERAL, "LINK STATUS: reg(0xfc) = %X\n", reg);

	reg = wctc4xxp_getctl(wc, 0x00A0);

	return ret;
}

static void 
wctc4xxp_turn_on_booted_led(struct wcdte *wc)
{
	wctc4xxp_setctl(wc, 0x00A0, 0x04080000);
}

static int 
wctc4xxp_boot_processor(struct wcdte *wc, const struct firmware *firmware)
{
	int ret;

	wctc4xxp_turn_off_booted_led(wc);

	if ((ret = wctc4xxp_load_firmware(wc, firmware))) {
		return ret;
	}
	
	wctc4xxp_turn_on_booted_led(wc);

	DTE_DEBUG(DTE_DEBUG_GENERAL, "Successfully booted DTE processor.\n");
	return 0;
}

static int 
wctc4xxp_create_channel_pair(struct wcdte *wc, struct channel_pvt *cpvt, 
	u8 simple, u8 complicated)
{
	int res;
	int length;
	u8 chan1, chan2;
	struct zt_transcoder_channel *dtc1, *dtc2;
	struct channel_pvt *cpvt1, *cpvt2;
	/* \todo what do these part variable names mean? */
	unsigned int timeslot;   
	unsigned int part2_id;
	struct tcb *cmd;
	const struct csm_encaps_hdr *hdr;

	
	BUG_ON(!wc || !cpvt);
	if (cpvt->encoder) {
		timeslot = cpvt->timeslot_in_num;
		part2_id = cpvt->timeslot_out_num;
	} else {
		u8 temp;
		timeslot = cpvt->timeslot_out_num;
		part2_id = cpvt->timeslot_in_num;
		temp = simple;
		simple = complicated;
		complicated = temp;
	}

	length = (DTE_FORMAT_G729A == complicated) ? G729_LENGTH : 
	         (DTE_FORMAT_G723_1 == complicated) ? G723_LENGTH : 0;

	if (!(cmd = wctc4xxp_create_channel_cmd(wc, timeslot))) {
		return -ENOMEM;
	}
	if ((res=wctc4xxp_transmit_cmd_and_wait(wc, cmd))) {
		free_cmd(cmd);
		return res;
	}
	free_cmd(cmd);

	cmd = wctc4xxp_create_cmd(wc, CMD_MSG_QUERY_CHANNEL(wc->seq_num++, timeslot));
	if ((res=wctc4xxp_transmit_cmd_and_wait(wc,cmd))) {
		free_cmd(cmd);
		return res;
	}
	WARN_ON(!cmd->response);
	hdr = (const struct csm_encaps_hdr*)cmd->response->data;
	chan1 = le16_to_cpu(hdr->params[0]);;
	free_cmd(cmd);

	if (!(cmd = wctc4xxp_create_channel_cmd(wc, part2_id))) {
		return -ENOMEM;
	}
	if ((res = __wctc4xxp_send_cmd(wc, cmd))) {
		return res;
	}
	cmd = wctc4xxp_create_cmd(wc, CMD_MSG_QUERY_CHANNEL(wc->seq_num++, part2_id));
	cmd->flags |= WAIT_FOR_RESPONSE;
	if ((res=wctc4xxp_transmit_cmd_and_wait(wc,cmd))) {
		return res;
	}
	WARN_ON(!cmd->response);
	hdr = (const struct csm_encaps_hdr*)cmd->response->data;
	chan2 = le16_to_cpu(hdr->params[0]);;
	free_cmd(cmd);

	DTE_DEBUG(DTE_DEBUG_CHANNEL_SETUP, 
	   "DTE is using the following channels chan1: %d chan2: %d\n", chan1, chan2);

	BUG_ON(timeslot/2 >= wc->numchannels);
	BUG_ON(part2_id/2 >= wc->numchannels);
	dtc1 = &(wc->uencode->channels[timeslot/2]);
	dtc2 = &(wc->udecode->channels[part2_id/2]);
	cpvt1 = dtc1->pvt;
	cpvt2 = dtc2->pvt;
	BUG_ON(!cpvt1);
	BUG_ON(!cpvt2);

	/* Configure complex channel */
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_SET_IP_HDR_CHANNEL(cpvt1->cmd_seqno++, chan1, part2_id, timeslot)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_VOIP_VCEOPT(cpvt1->cmd_seqno++, chan1, length, 0)))) {
		return res;
	}
	/* Configure simple channel */
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_SET_IP_HDR_CHANNEL(cpvt2->cmd_seqno++, chan2, timeslot, part2_id)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_VOIP_VCEOPT(cpvt2->cmd_seqno++, chan2, length, 0)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_VOIP_TONECTL(cpvt1->cmd_seqno++, chan1)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_VOIP_DTMFOPT(cpvt1->cmd_seqno++, chan1)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_VOIP_TONECTL(cpvt2->cmd_seqno++, chan2)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_VOIP_DTMFOPT(cpvt2->cmd_seqno++, chan2)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_VOIP_INDCTRL(cpvt1->cmd_seqno++, chan1)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_VOIP_INDCTRL(cpvt2->cmd_seqno++, chan2)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_TRANS_CONNECT(wc->seq_num++, 1, chan1, chan2, complicated, simple)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_VOIP_VOPENA(cpvt1->cmd_seqno++, chan1, complicated)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_VOIP_VOPENA(cpvt2->cmd_seqno++, chan2, simple)))) {
		return res;
	}

	DTE_DEBUG(DTE_DEBUG_CHANNEL_SETUP, 
	   "DTE has completed setup and connected the two channels together.\n");


	/* Save off the channels returned from the DTE so we can use then when
	 * sending the RTP packets. */
	if (ENCODER == cpvt->encoder) {
		cpvt->chan_in_num = chan1;
		cpvt->chan_out_num = chan2;
	} else {
		cpvt->chan_out_num = chan1;
		cpvt->chan_in_num = chan2;
	}

	return 0;
}

static int 
wctc4xxp_destroy_channel_pair(struct wcdte *wc, struct channel_pvt *cpvt)
{
	struct zt_transcoder_channel *dtc1, *dtc2;
	struct channel_pvt *cpvt1, *cpvt2;
	int chan1, chan2;
	int res;

	if (cpvt->encoder) {
		chan1 = cpvt->chan_in_num;
		chan2 = cpvt->chan_out_num;
	} else {
		chan1 = cpvt->chan_out_num;
		chan2 = cpvt->chan_in_num;
	}

	if (chan1/2 >= wc->numchannels || chan2/2 >= wc->numchannels) {
		DTE_PRINTK(WARNING, 
		 "Invalid channel numbers in %s. chan1:%d chan2: %d\n", 
		 __FUNCTION__, chan1/2, chan2/2);
		return 0;
	}

	dtc1 = &(wc->uencode->channels[chan1/2]);
	dtc2 = &(wc->udecode->channels[chan2/2]);
	cpvt1 = dtc1->pvt;
	cpvt2 = dtc2->pvt;

	/* Turn off both channels */
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_VOIP_VOPENA_CLOSE(cpvt1->cmd_seqno++, chan1)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_VOIP_VOPENA_CLOSE(cpvt2->cmd_seqno++, chan2)))) {
		return res;
	}
	
	/* Disconnect the channels */
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_TRANS_CONNECT(wc->seq_num++, 0, chan1, chan2, 0, 0)))) {
		return res;
	}

	/* Remove the channels */
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_DESTROY_CHANNEL(wc->seq_num++, chan1)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_DESTROY_CHANNEL(wc->seq_num++, chan2)))) {
		return res;
	}

	return 0;
}


static int 
__wctc4xxp_setup_channels(struct wcdte *wc)
{
	int res;
	struct tcb *cmd;

	if (!(cmd = alloc_cmd())) {
		return -ENOMEM;
	}

	__wctc4xxp_create_set_arm_clk_cmd(wc, cmd);
	res = wctc4xxp_transmit_cmd_and_wait(wc, cmd);
	free_cmd(cmd);
	if (res) {
		return res;
	}

	cmd = wctc4xxp_create_cmd(wc, CMD_MSG_SET_SPU_CLK(wc->seq_num++));
	res = wctc4xxp_transmit_cmd_and_wait(wc, cmd);
	free_cmd(cmd);
	if (res) {
		return res;
	}

	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_TDM_SELECT_BUS_MODE(wc->seq_num++)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_TDM_ENABLE_BUS(wc->seq_num++)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_SUPVSR_SETUP_TDM_PARMS(wc->seq_num++, 0x03, 0x20, 0x00)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_SUPVSR_SETUP_TDM_PARMS(wc->seq_num++, 0x04, 0x80, 0x04)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_SUPVSR_SETUP_TDM_PARMS(wc->seq_num++, 0x05, 0x20, 0x08)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_SUPVSR_SETUP_TDM_PARMS(wc->seq_num++, 0x06, 0x80, 0x0C)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_SET_ETH_HEADER(wc->seq_num++)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_IP_SERVICE_CONFIG(wc->seq_num++)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_ARP_SERVICE_CONFIG(wc->seq_num++)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_ICMP_SERVICE_CONFIG(wc->seq_num++)))) {
		return res;
	}

	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_DEVICE_SET_COUNTRY_CODE(wc->seq_num++)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_SPU_FEATURES_CONTROL(wc->seq_num++, 0x02)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_IP_OPTIONS(wc->seq_num++)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_SPU_FEATURES_CONTROL(wc->seq_num++, 0x04)))) {
		return res;
	}
	if ((res=wctc4xxp_send_cmd(wc, CMD_MSG_TDM_OPT(wc->seq_num++)))) {
		return res;
	}
	return 0;
}

static int 
wctc4xxp_setup_channels(struct wcdte *wc)
{
	int ret;
	if ((ret=down_interruptible(&wc->chansem))) {
		WARN();
		return ret;
	}
	ret = __wctc4xxp_setup_channels(wc);
	up(&wc->chansem);

	return ret;
}

static void wctc4xxp_setup_file_operations(struct file_operations *fops)
{
	fops->owner = THIS_MODULE;
	fops->read =  wctc4xxp_read;
	fops->write = wctc4xxp_write;
}

static int 
initialize_channel_pvt(struct wcdte *wc, encode_t type, 
                       struct channel_pvt **cpvt)
{
	int chan;
	*cpvt = kmalloc(sizeof(struct channel_pvt) * wc->numchannels, GFP_KERNEL);
	if (!(*cpvt)) {
		return -ENOMEM;
	}
	for (chan = 0; chan < wc->numchannels; ++chan) {
		wctc4xxp_init_state((*cpvt) + chan, type, chan, wc);
	}
	return 0;
}

static int 
initialize_transcoder(struct wcdte *wc, unsigned int srcfmts, 
                      unsigned int dstfmts, struct channel_pvt *pvts, 
		      struct zt_transcoder **zt)
{
	int chan;
	*zt = zt_transcoder_alloc(wc->numchannels);
	if (!(*zt)) {
		return -ENOMEM;
	}
	(*zt)->srcfmts = srcfmts;
	(*zt)->dstfmts = dstfmts;
	(*zt)->allocate = wctc4xxp_operation_allocate;
	(*zt)->release = wctc4xxp_operation_release;
	wctc4xxp_setup_file_operations(&((*zt)->fops));
	for (chan = 0; chan < wc->numchannels; ++chan) {
		(*zt)->channels[chan].pvt = &pvts[chan];
	}
	return 0;
}

static int initialize_encoders(struct wcdte *wc, unsigned int complexfmts)
{
	int res;
	if ((res = initialize_channel_pvt(wc, ENCODER, &wc->encoders))) {
		return res;
	}
	if ((res = initialize_transcoder(wc, 
		 ZT_FORMAT_ULAW | ZT_FORMAT_ALAW,
		 complexfmts, 
		 wc->encoders, 
		 &wc->uencode))) 
	{
		return res;
	}
	sprintf(wc->uencode->name, "DTE Encoder");
	return res;
}

static int 
initialize_decoders(struct wcdte *wc, unsigned int complexfmts)
{
	int res;
	if ((res = initialize_channel_pvt(wc, DECODER, &wc->decoders))) {
		return res;
	}   
	if ((res = initialize_transcoder(wc, 
		complexfmts, 
		ZT_FORMAT_ULAW | ZT_FORMAT_ALAW,
		wc->decoders, 
		&wc->udecode))) 
	{
		return res;
	}

	sprintf(wc->udecode->name, "DTE Decoder");
	return res;
}

static void 
wctc4xxp_send_commands(struct wcdte *wc, struct list_head *to_send)
{
	struct tcb *cmd;
	while (!list_empty(to_send)) {
		cmd = container_of(to_send->next, struct tcb, node);
		list_del_init(&cmd->node);
		wctc4xxp_transmit_cmd(wc, cmd);
	}
}

static void 
wctc4xxp_watchdog(unsigned long data)
{
	struct wcdte *wc = (struct wcdte *)data;
	struct tcb *cmd, *temp;
	LIST_HEAD(cmds_to_retry);
	const int MAX_RETRIES = 5;

	spin_lock(&wc->cmd_list_lock);
	/* Go through the list of messages that are waiting for responses from
	 * the DTE, and complete or retry any that have timed out. */
	list_for_each_entry_safe(cmd, temp, &wc->waiting_for_response_list, node) {
		if (time_after(jiffies, cmd->timeout)) {
			if (++cmd->retries > MAX_RETRIES) {
				if (!(cmd->flags & TX_COMPLETE)) {
					set_bit(DTE_SHUTDOWN, &wc->flags);
					spin_unlock(&wc->cmd_list_lock);
					wctc4xxp_stop_dma(wc);
					DTE_PRINTK(ERR, "Board malfunctioning.  Halting operation.\n");
					return;
				}
				/* ERROR:  We've retried the command and haven't
				 * received the ACK or the response. */
				cmd->flags |= DTE_CMD_TIMEOUT;
				list_del_init(&cmd->node);
				complete(&cmd->complete);
			} else if (cmd->flags & TX_COMPLETE) {
				/* Move this to the local list because we're
				 * going to resend it once we free the locks */
				list_move_tail(&cmd->node, &cmds_to_retry);
			} else {
				/* The command is still sitting on the tx
				 * descriptor ring.  We don't want to move it
				 * off any lists, lets just reset the timeout
				 * and tell the hardware to look for another
				 * command . */
				DTE_PRINTK(WARNING, "Retrying command that was " \
				    "still on descriptor list.\n");
				cmd->timeout = jiffies + HZ/4;
				wctc4xxp_transmit_demand_poll(wc);
			}
		} 
	}
	spin_unlock(&wc->cmd_list_lock);

	wctc4xxp_send_commands(wc, &cmds_to_retry);
}

/**
 * 	Insert an struct wcdte on the global list in sorted order
 *
 */
static int __devinit
wctc4xxp_add_to_device_list(struct wcdte *wc)
{
	struct wcdte *cur;
	int pos = 0;
	INIT_LIST_HEAD(&wc->node);
	spin_lock(&wctc4xxp_list_lock);
	list_for_each_entry(cur, &wctc4xxp_list, node) {
		if (cur->pos != pos) {
			/* Add the new entry before the one here */
			list_add_tail(&wc->node, &cur->node);
			break;
		}
		else {
			++pos;
		}
	}
	if (list_empty(&wc->node)) {
		list_add_tail(&wc->node, &wctc4xxp_list);
	}
	spin_unlock(&wctc4xxp_list_lock);
	return pos;
}

struct wctc4xxp_desc {
	const char *short_name;
	const char *long_name;
	int flags;
};

static struct wctc4xxp_desc wctc400p = { 
	.short_name = "tc400b",
	.long_name = "Wildcard TC400P+TC400M", 
	.flags = 0, 
};

static struct wctc4xxp_desc wctce400 = { 
	.short_name = "tce400",
	.long_name = "Wildcard TCE400+TC400M", 
	.flags = 0,
};

static int __devinit 
wctc4xxp_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int res, reg, position_on_list;
	struct wcdte *wc = NULL;
	struct wctc4xxp_desc *d = (struct wctc4xxp_desc *)ent->driver_data;
	unsigned char g729_numchannels, g723_numchannels, min_numchannels;
	unsigned char wctc4xxp_firmware_ver, wctc4xxp_firmware_ver_minor;
	unsigned int complexfmts;
	struct firmware embedded_firmware;
	const struct firmware *firmware = &embedded_firmware;
#if !defined(HOTPLUG_FIRMWARE)
	extern void _binary_zt_fw_tc400m_bin_size;
	extern u8 _binary_zt_fw_tc400m_bin_start[];
#else
	static const char tc400m_firmware[] = "zaptel-fw-tc400m.bin";
#endif

	/* ------------------------------------------------------------------
	 * Setup the pure software constructs internal to this driver.
	 * --------------------------------------------------------------- */

	if (!(wc = kmalloc(sizeof(*wc), GFP_KERNEL))) {
		return -ENOMEM;
	}
	memset(wc, 0, sizeof(*wc));

	position_on_list = wctc4xxp_add_to_device_list(wc);
	snprintf(wc->board_name, sizeof(wc->board_name)-1, "%s%d", 
		 d->short_name, position_on_list);
	wc->iobase           = pci_resource_start(pdev, 0);
	wc->pdev             = pdev;
	wc->pos              = position_on_list;
	wc->variety          = d->long_name;

	init_MUTEX(&wc->chansem);
	spin_lock_init(&wc->reglock);
	spin_lock_init(&wc->cmd_list_lock);
	INIT_LIST_HEAD(&wc->cmd_list);
	INIT_LIST_HEAD(&wc->waiting_for_response_list);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
	INIT_WORK(&wc->deferred_work, deferred_work_func, wc);
#else
	INIT_WORK(&wc->deferred_work, deferred_work_func);
#endif
	DTE_PRINTK(INFO, "Attached to device at %s.\n", pci_name(wc->pdev));

	/* Keep track of whether we need to free the region */
	if (!request_region(wc->iobase, 0xff, wc->board_name)) {
		/* \todo put in error message. */
		DTE_PRINTK(WARNING, 
		    "Failed to reserve the I/O ports for this device.\n");
		return -EIO;
	}

	init_waitqueue_head(&wc->waitq);

	if (pci_set_dma_mask(wc->pdev, DMA_32BIT_MASK)) {
		release_region(wc->iobase, 0xff);
		DTE_PRINTK(WARNING, "No suitable DMA available.\n");
		return -EIO;
	}

	if (!(wc->txd = kmalloc(sizeof(*wc->txd), GFP_KERNEL))) {
		res = -ENOMEM;
		goto error_exit_swinit;
	}

	if ((res = wctc4xxp_initialize_descriptor_ring(wc->pdev, wc->txd,
		0xe0800000,
		DMA_TO_DEVICE))) {
		goto error_exit_swinit;
	}

	if (!(wc->rxd = kmalloc(sizeof(*wc->rxd), GFP_KERNEL))) {
		res = -ENOMEM;
		goto error_exit_swinit;
	}

	if ((res = wctc4xxp_initialize_descriptor_ring(wc->pdev, wc->rxd, 0,
		DMA_FROM_DEVICE))) {
		goto error_exit_swinit;
	}

#if defined(HOTPLUG_FIRMWARE)
	if ((res = request_firmware(&firmware, tc400m_firmware, &wc->pdev->dev)) ||
	    !firmware) {
		DTE_PRINTK(ERR, "Firmware %s not available from userspace. (%d)\n", tc400m_firmware, res);
		goto error_exit_swinit;
	}
#else
	embedded_firmware.data = _binary_zt_fw_tc400m_bin_start;
	embedded_firmware.size = (size_t) &_binary_zt_fw_tc400m_bin_size;
#endif

	wctc4xxp_firmware_ver = firmware->data[0];
	wctc4xxp_firmware_ver_minor = firmware->data[16];
	g729_numchannels = firmware->data[1];
	g723_numchannels = firmware->data[2];

	min_numchannels = min(g723_numchannels, g729_numchannels);

	if (!mode || strlen(mode) < 4) {
		sprintf(wc->complexname, "G.729a / G.723.1");
		complexfmts = ZT_FORMAT_G729A | ZT_FORMAT_G723_1;
		wc->numchannels = min_numchannels;
	} else if (mode[3] == '9') {	/* "G.729" */
		sprintf(wc->complexname, "G.729a");
		complexfmts = ZT_FORMAT_G729A;
		wc->numchannels = g729_numchannels;
	} else if (mode[3] == '3') {	/* "G.723.1" */
		sprintf(wc->complexname, "G.723.1");
		complexfmts = ZT_FORMAT_G723_1;
		wc->numchannels = g723_numchannels;
	} else {
		sprintf(wc->complexname, "G.729a / G.723.1");
		complexfmts = ZT_FORMAT_G729A | ZT_FORMAT_G723_1;
		wc->numchannels = min_numchannels;
	}

	if ((res = initialize_encoders(wc, complexfmts))) {
		goto error_exit_swinit;
	}
	if ((res = initialize_decoders(wc, complexfmts))) {
		goto error_exit_swinit;
	}
	
	if (DTE_DEBUG_NETWORK_IF & debug) {
		if ((res = wctc4xxp_net_register(wc))) {
			goto error_exit_swinit;
		}
	}

	setup_timer(&wc->watchdog, wctc4xxp_watchdog, (unsigned long)wc); 

	/* ------------------------------------------------------------------
	 * Load the firmware and start the DTE.
	 * --------------------------------------------------------------- */

	if ((res = pci_enable_device(pdev))) {
		DTE_PRINTK(ERR, "Failed to enable device.\n");
		goto error_exit_swinit;;
	}
	pci_set_master(pdev);
	pci_set_drvdata(pdev, wc);
	if ((res = request_irq(pdev->irq, wctc4xxp_interrupt, ZAP_IRQ_SHARED, wc->board_name, wc))) {
		DTE_PRINTK(ERR, "Unable to request IRQ %d\n", pdev->irq);
		if (firmware != &embedded_firmware) {
			release_firmware(firmware);
		}
		goto error_exit_hwinit;
	}
	if ((res = wctc4xxp_hardware_init(wc))) {
		if (firmware != &embedded_firmware) {
			release_firmware(firmware);
		}
		goto error_exit_hwinit;
	}
	wctc4xxp_enable_interrupts(wc);
	wctc4xxp_start_dma(wc);
	res = wctc4xxp_boot_processor(wc, firmware);
	if (firmware != &embedded_firmware) {
		release_firmware(firmware);
	}
	if (res) {
		goto error_exit_hwinit;
	}

	if ((res = wctc4xxp_setup_channels(wc))) {
		goto error_exit_hwinit;
	}

	/* \todo Read firmware version directly from tc400b.*/
	DTE_PRINTK(INFO, "(%s) Transcoder support LOADED " \
	   "(firm ver = %d.%d)\n", wc->complexname, wctc4xxp_firmware_ver, 
	   wctc4xxp_firmware_ver_minor);

	reg = wctc4xxp_getctl(wc, 0x00fc);

	DTE_DEBUG(DTE_DEBUG_GENERAL, 
	   "debug: (post-boot) Reg fc is %08x\n", reg);
	
	DTE_PRINTK(INFO, "Installed a Wildcard TC: %s \n", wc->variety);
	DTE_DEBUG(DTE_DEBUG_GENERAL, "Operating in DEBUG mode.\n");
	zt_transcoder_register(wc->uencode);
	zt_transcoder_register(wc->udecode);

	return 0;

error_exit_hwinit:
	wctc4xxp_stop_dma(wc);
	wctc4xxp_cleanup_command_list(wc);
	free_irq(pdev->irq, wc);
	pci_set_drvdata(pdev, NULL);
error_exit_swinit:
	wctc4xxp_net_unregister(wc);
	if (wc->encoders) kfree(wc->encoders);
	if (wc->decoders) kfree(wc->decoders);
	if (wc->uencode)  zt_transcoder_free(wc->uencode);
	if (wc->udecode)  zt_transcoder_free(wc->udecode);
	if (wc->txd) {
		if (wc->txd->desc) {
			wctc4xxp_cleanup_descriptor_ring(wc->txd);
		}
		kfree(wc->txd);
	}
	if (wc->rxd) {
		if (wc->rxd && wc->rxd->desc) {
			wctc4xxp_cleanup_descriptor_ring(wc->rxd);
		}
		kfree(wc->rxd);
	}
	release_region(wc->iobase, 0xff);
	spin_lock(&wctc4xxp_list_lock);
	list_del(&wc->node);
	spin_unlock(&wctc4xxp_list_lock);
	kfree(wc);
 	return res;	
}

static void wctc4xxp_cleanup_channels(struct wcdte *wc)
{
	int i;
	struct zt_transcoder_channel *dtc_en, *dtc_de;
	struct channel_pvt *cpvt;

	for(i = 0; i < wc->numchannels; i++) {
		dtc_en = &(wc->uencode->channels[i]);
		cpvt = dtc_en->pvt;
		wctc4xxp_cleanup_channel_private(wc, cpvt);
		
		dtc_de = &(wc->udecode->channels[i]);
		cpvt = dtc_de->pvt;
		wctc4xxp_cleanup_channel_private(wc, cpvt);
	}
}

static void __devexit wctc4xxp_remove_one(struct pci_dev *pdev)
{
	int i;
	struct wcdte *wc = pci_get_drvdata(pdev);
	struct zt_transcoder_channel *dtc_en, *dtc_de;
	struct channel_pvt *cpvt;

	if (!wc) {
		/* \todo print warning message here. */
		return;
	}

	spin_lock(&wctc4xxp_list_lock);
	list_del(&wc->node);
	spin_unlock(&wctc4xxp_list_lock);

	set_bit(DTE_SHUTDOWN, &wc->flags);
	if (del_timer_sync(&wc->watchdog)) {
		/* Just delete the timer twice in case the timer had already
		 * checked DTE_SHUTDOWN and rescheduled itself the first time.
		 */
		del_timer_sync(&wc->watchdog);
	}

	wctc4xxp_net_unregister(wc);

	if (debug) {
		for(i = 0; i < wc->numchannels; i++) {
			dtc_en = &(wc->uencode->channels[i]);
			cpvt = dtc_en->pvt;
			DTE_DEBUG(DTE_DEBUG_GENERAL, 
			   "encoder[%d] snt = %d, rcv = %d [%d]\n", i, 
			   atomic_read(&cpvt->stats.packets_sent), 
			   atomic_read(&cpvt->stats.packets_received), 
			   atomic_read(&cpvt->stats.packets_sent) - atomic_read(&cpvt->stats.packets_received));

			dtc_de = &(wc->udecode->channels[i]);
			cpvt = dtc_de->pvt;
			DTE_DEBUG(DTE_DEBUG_GENERAL, 
			   "decoder[%d] snt = %d, rcv = %d [%d]\n", i, 
			   atomic_read(&cpvt->stats.packets_sent), 
			   atomic_read(&cpvt->stats.packets_received), 
			   atomic_read(&cpvt->stats.packets_sent) - atomic_read(&cpvt->stats.packets_received));
		}
	}
	
	/* Stop any DMA */
	wctc4xxp_stop_dma(wc);

	/* In case hardware is still there */
	wctc4xxp_disable_interrupts(wc);
	
	free_irq(pdev->irq, wc);

	/* There isn't anything that would run in the workqueue that will wait
	 * on an interrupt. */

	zt_transcoder_unregister(wc->udecode);
	zt_transcoder_unregister(wc->uencode);

	/* Free Resources */
	release_region(wc->iobase, 0xff);
	spin_lock_bh(&wc->cmd_list_lock);
	if (wc->txd) {
		if (wc->txd->desc) wctc4xxp_cleanup_descriptor_ring(wc->txd);
		kfree(wc->txd);
	}
	if (wc->rxd) {
		if (wc->rxd && wc->rxd->desc) wctc4xxp_cleanup_descriptor_ring(wc->rxd);
		kfree(wc->rxd);
	}
	spin_unlock_bh(&wc->cmd_list_lock);
	wctc4xxp_cleanup_command_list(wc);
	wctc4xxp_cleanup_channels(wc);

	pci_set_drvdata(pdev, NULL);

	zt_transcoder_free(wc->uencode);
	zt_transcoder_free(wc->udecode);
	kfree(wc->encoders);
	kfree(wc->decoders);
	kfree(wc);
}

static struct pci_device_id wctc4xxp_pci_tbl[] = {
  	{ 0xd161, 0x3400, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (unsigned long) &wctc400p }, /* Digium board */
  	{ 0xd161, 0x8004, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (unsigned long) &wctce400 }, /* Digium board */
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, wctc4xxp_pci_tbl);

static struct pci_driver wctc4xxp_driver = {
	name: 	"wctc4xxp",
	probe: 	wctc4xxp_init_one,
	remove:	__devexit_p(wctc4xxp_remove_one),
	suspend: NULL,
	resume:	NULL,
	id_table: wctc4xxp_pci_tbl,
};

int __init wctc4xxp_init(void)
{
	int res;
#	if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23)
	cmd_cache = kmem_cache_create(THIS_MODULE->name, sizeof(struct tcb), 0, 
	                              SLAB_HWCACHE_ALIGN, NULL, NULL);
#	else
	cmd_cache = kmem_cache_create(THIS_MODULE->name, sizeof(struct tcb), 0, 
	                              SLAB_HWCACHE_ALIGN, NULL);
#	endif
	
	if (!cmd_cache) {
		return -ENOMEM;
	}
	spin_lock_init(&wctc4xxp_list_lock);
	INIT_LIST_HEAD(&wctc4xxp_list);
	res = zap_pci_module(&wctc4xxp_driver);
	if (res)
		return -ENODEV;
	return 0;
}

void __exit wctc4xxp_cleanup(void)
{
	pci_unregister_driver(&wctc4xxp_driver);
	kmem_cache_destroy(cmd_cache);
}

module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_DESCRIPTION("Wildcard TC400P+TC400M Driver");
MODULE_AUTHOR("Digium Incorporated <support@digium.com>");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

module_init(wctc4xxp_init);
module_exit(wctc4xxp_cleanup);
