/*
 * VoiceBus(tm) Interface Library.
 *
 * Written by Shaun Ruffell <sruffell@digium.com>
 * and based on previous work by Mark Spencer <markster@digium.com>, 
 * Matthew Fredrickson <creslin@digium.com>, and
 * Michael Spiceland <mspiceland@digium.com>
 * 
 * Copyright (C) 2007-2008 Digium, Inc.
 *
 * All rights reserved.
 *
 * VoiceBus is a registered trademark of Digium.
 *
 * \todo   Make the client drivers back out gracefully when presented with a
 * signal.
 * \todo   Modify clients to sleep with timeout when waiting for interrupt.
 * \todo   Check on a 64-bit CPU / Kernel
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
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/timer.h>

#include "voicebus.h"

#define assert(__x__) BUG_ON(!(__x__))

#define INTERRUPT 0	/* Run the deferred processing in the ISR. */
#define TASKLET 1	/* Run in a tasklet. */
#define TIMER 2		/* Run in a system timer. */
#define WORKQUEUE 3	/* Run in a workqueue. */
#ifndef VOICEBUS_DEFERRED
#define VOICEBUS_DEFERRED INTERRUPT
#endif
#if VOICEBUS_DEFERRED == WORKQUEUE
#define VOICEBUS_ALLOC_FLAGS GFP_KERNEL
#else
#define VOICEBUS_ALLOC_FLAGS GFP_ATOMIC
#endif

#if VOICEBUS_DEFERRED == TIMER 
#if HZ < 1000
/* \todo Put an error message here. */
#endif
#endif

/*! The number of descriptors in both the tx and rx descriptor ring. */
#define DRING_SIZE (1 << 5)  /* Must be a power of 2 */
#define DRING_MASK	(DRING_SIZE-1) 

/* Interrupt status' reported in SR_CSR5 */
#define TX_COMPLETE_INTERRUPT 		0x00000001
#define TX_STOPPED_INTERRUPT 		0x00000002
#define TX_UNAVAILABLE_INTERRUPT	0x00000004
#define TX_JABBER_TIMEOUT_INTERRUPT	0x00000008
#define TX_UNDERFLOW_INTERRUPT		0x00000020
#define RX_COMPLETE_INTERRUPT		0x00000040
#define RX_UNAVAILABLE_INTERRUPT	0x00000080
#define RX_STOPPED_INTERRUPT		0x00000100
#define RX_WATCHDOG_TIMEOUT_INTERRUPT	0x00000200
#define TIMER_INTERRUPT			0x00000800
#define FATAL_BUS_ERROR_INTERRUPT	0x00002000
#define ABNORMAL_INTERRUPT_SUMMARY	0x00008000
#define NORMAL_INTERRUPT_SUMMARY	0x00010000

#define SR_CSR5				0x0028
#define NAR_CSR6			0x0030

#define IER_CSR7			0x0038
#define		CSR7_TCIE		0x00000001 /* tx complete */
#define		CSR7_TPSIE		0x00000002 /* tx processor stopped */
#define		CSR7_TDUIE		0x00000004 /* tx desc unavailable */
#define 	CSR7_TUIE		0x00000020 /* tx underflow */
#define		CSR7_RCIE		0x00000040 /* rx complete */
#define 	CSR7_RUIE		0x00000080 /* rx desc unavailable */
#define		CSR7_RSIE		0x00000100 /* rx processor stopped */
#define 	CSR7_FBEIE		0x00002000 /* fatal bus error */
#define		CSR7_AIE		0x00008000 /* abnormal enable */
#define 	CSR7_NIE		0x00010000 /* normal enable */

#define DEFAULT_INTERRUPTS	( CSR7_TCIE | CSR7_TPSIE | CSR7_TDUIE |  \
				 CSR7_RUIE | CSR7_RSIE | CSR7_FBEIE | \
				 CSR7_AIE | CSR7_NIE)

#define CSR9				0x0048
#define 	CSR9_MDC		0x00010000
#define 	CSR9_MDO		0x00020000
#define 	CSR9_MMC		0x00040000
#define 	CSR9_MDI		0x00080000

#define OWN_BIT (1 << 31)

/* In memory structure shared by the host and the adapter. */
struct voicebus_descriptor {
	u32 des0;
	u32 des1;
	u32 buffer1;
	u32 container; /* Unused */
} __attribute__((packed));

struct voicebus_descriptor_list {
	/* Pointer to an array of descriptors to give to hardware. */
	struct voicebus_descriptor* desc;
	/* Read completed buffers from the head. */
	unsigned int 	head;
	/* Write ready buffers to the tail. */
	unsigned int 	tail;
	/* Array to save the kernel virtual address of pending buffers. */
	void * 		pending[DRING_SIZE];
	/* PCI Bus address of the descriptor list. */
	dma_addr_t	desc_dma;
	/*! either DMA_FROM_DEVICE or DMA_TO_DEVICE */
	unsigned int 	direction;
	/*! The number of buffers currently submitted to the hardware. */
	atomic_t 	count;
	/*! The number of bytes to pad each descriptor for cache alignment. */
	unsigned int	padding;
};


/*!  * \brief Represents a VoiceBus interface on a Digium telephony card.
 */
struct voicebus {
	/*! Name of this card. */
	const char *board_name; 
	/*! The system pci device for this VoiceBus interface. */
	struct pci_dev *pdev;
	/*! Protects access to card registers and this structure. You should
	 * hold this lock before accessing most of the members of this data
	 * structure or the card registers. */
	spinlock_t lock;
	/*! The size of the transmit and receive buffers for this card. */
	u32 framesize;
	/*! The number of u32s in the host system cache line. */	
	u8 cache_line_size;
	/*! Pool to allocate memory for the tx and rx descriptor rings. */
	struct voicebus_descriptor_list rxd;
	struct voicebus_descriptor_list txd;
	/*! Level of debugging information.  0=None, 5=Insane. */
	atomic_t debuglevel;
	/*! Cache of buffer objects. */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
	kmem_cache_t *buffer_cache;
#else
	struct kmem_cache *buffer_cache;
#endif
	/*! Base address of the VoiceBus interface registers in I/O space. */
	u32 iobase;
	/*! The IRQ line for this VoiceBus interface. */
	unsigned int irq;
#if VOICEBUS_DEFERRED == WORKQUEUE
	/*! Process buffers in the context of this workqueue. */
	struct workqueue_struct *workqueue;
	/*! Work item to process tx / rx buffers. */
	struct work_struct workitem;
#elif VOICEBUS_DEFERRED == TASKLET
	/*! Process buffers in the context of a tasklet. */
	struct tasklet_struct 	tasklet;
#elif VOICEBUS_DEFERRED == TIMER
	/*! Process buffers in a timer without generating interrupts. */
	struct timer_list timer;
#endif
	/*! Callback function to board specific module to process frames. */
	void (*handle_receive)(void *vbb, void *context);
	void (*handle_transmit)(void *vbb, void *context);
	/*! Data to pass to the receive and transmit callback. */
	void *context;
	struct completion stopped_completion;
	/*! Flags */
	unsigned long flags;
	/* \todo see about removing this... */
	u32 sdi;
	/*! Number of tx buffers to queue up before enabling interrupts. */
	unsigned int 	min_tx_buffer_count;
};

/*
 * Use the following macros to lock the VoiceBus interface, and it won't
 * matter if the deferred processing is running inside the interrupt handler,
 * in a tasklet, or in a workqueue.
 */
#if VOICEBUS_DEFERRED == WORKQUEUE
/*
 * When the deferred processing is running in a workqueue, voicebus will never
 * be locked from the context of the interrupt handler, and therefore we do
 * not need to lock interrupts.
 */
#define LOCKS_VOICEBUS			
#define LOCKS_FROM_DEFERRED		
#define VBLOCK(_vb_) 			spin_lock(&((_vb_)->lock))
#define VBUNLOCK(_vb_)			spin_unlock(&((_vb_)->lock))
#define VBLOCK_FROM_DEFERRED(_vb_) 	spin_lock(&((_vb_)->lock))
#define VBUNLOCK_FROM_DEFERRED(_vb_)	spin_lock(&((_vb_)->lock))
#else
#define LOCKS_VOICEBUS			unsigned long _irqflags
#define LOCKS_FROM_DEFERRED		
#define VBLOCK(_vb_) 			spin_lock_irqsave(&((_vb_)->lock), _irqflags)
#define VBUNLOCK(_vb_)			spin_unlock_irqrestore(&((_vb_)->lock), _irqflags)
#define VBLOCK_FROM_DEFERRED(_vb_) 	spin_lock(&((_vb_)->lock))
#define VBUNLOCK_FROM_DEFERRED(_vb_)	spin_lock(&((_vb_)->lock))
#endif

#define VB_PRINTK(_vb, _lvl, _fmt, _args...) \
	printk(KERN_##_lvl "%s: " _fmt, (_vb)->board_name, ## _args)

/* Bit definitions for struct voicebus.flags */
#define TX_UNDERRUN			1
#define RX_UNDERRUN			2
#define IN_DEFERRED_PROCESSING		3
#define STOP				4

#if VOICEBUS_DEFERRED == WORKQUEUE
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
/*! \brief Make the current task real-time. */
static void 
vb_setup_deferred(void *data)
#else
static void 
vb_setup_deferred(struct work_struct *work)
#endif
{
	struct sched_param param = { .sched_priority = 99 };
	sched_setscheduler(current, SCHED_FIFO, &param);
}
/*! \brief Schedule a work item to make the voicebus workqueue real-time. */
static void
vb_set_workqueue_priority(struct voicebus *vb)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
	DECLARE_WORK(deferred_setup, vb_setup_deferred, NULL);
#else
	DECLARE_WORK(deferred_setup, vb_setup_deferred);
#endif
	queue_work(vb->workqueue, &deferred_setup);
	flush_workqueue(vb->workqueue);
}
#endif
#endif

#ifdef DBG
static inline int 
assert_in_vb_deferred(struct voicebus *vb)
{
	assert(test_bit(IN_DEFERRED_PROCESSING, &vb->flags));
}

static inline void
start_vb_deferred(struct voicebus *vb)
{
	set_bit(IN_DEFERRED_PROCESSING, &vb->flags);
}

static inline void
stop_vb_deferred(struct voicebus *vb)
{
	clear_bit(IN_DEFERRED_PROCESSING, &vb->flags);
}
#else
#define assert_in_vb_deferred(_x_)  do {;} while(0)
#define start_vb_deferred(_x_) do {;} while(0)
#define stop_vb_deferred(_x_) do {;} while(0)
#endif

static inline struct voicebus_descriptor *
vb_descriptor(struct voicebus_descriptor_list *dl, int index)
{
	struct voicebus_descriptor *d;
	d = (struct voicebus_descriptor *)((u8*)dl->desc + 
		((sizeof(*d) + dl->padding) * index));
	return d;
}

static int
vb_initialize_descriptors(struct voicebus *vb, struct voicebus_descriptor_list *dl, 
	u32 des1, unsigned int direction)
{
	int i; 
	struct voicebus_descriptor *d;
	const u32 END_OF_RING = 0x02000000;

	assert(dl);

	/*
	 * Add some padding to each descriptor to ensure that they are
	 * aligned on host system cache-line boundaries, but only for the 
	 * cache-line sizes that we support.
	 *
	 */
	if ((0x08 == vb->cache_line_size) || (0x10 == vb->cache_line_size) ||
	    (0x20 == vb->cache_line_size)) 
	{
		dl->padding = (vb->cache_line_size*sizeof(u32)) - sizeof(*d);
	} else {
		dl->padding = 0;
	}
	
	dl->desc = pci_alloc_consistent(vb->pdev, 
		(sizeof(*d) + dl->padding) * DRING_SIZE, &dl->desc_dma);
	if (!dl->desc) {
		return -ENOMEM;
	}

	memset(dl->desc, 0, (sizeof(*d) + dl->padding) * DRING_SIZE);
	for ( i = 0; i < DRING_SIZE; ++i) {
		d = vb_descriptor(dl, i);
		d->des1 = des1;
	}
	d->des1 |= cpu_to_le32(END_OF_RING);
	dl->direction = direction;
	atomic_set(&dl->count, 0);
	return 0;
}

static int
vb_initialize_tx_descriptors(struct voicebus *vb)
{
	return vb_initialize_descriptors(
		vb, &vb->txd, 0xe4800000 | vb->framesize, DMA_TO_DEVICE);
}

static int
vb_initialize_rx_descriptors(struct voicebus *vb)
{
	return vb_initialize_descriptors(
		vb, &vb->rxd, vb->framesize, DMA_FROM_DEVICE);
}

/*! \brief  Use to set the minimum number of buffers queued to the hardware
 * before enabling interrupts. 
 */
int 
voicebus_set_minlatency(struct voicebus *vb, unsigned int ms)
{
	LOCKS_VOICEBUS;
	/*
	 * One millisecond of latency means that we have 3 buffers pending,
	 * since two are always going to be waiting in the TX fifo on the
	 * interface chip.
	 *
	 */
#define MESSAGE "%d ms is an invalid value for minumum latency.  Setting to %d ms.\n"
	if ( DRING_SIZE < ms ) {
		VB_PRINTK(vb, WARNING, MESSAGE, ms, DRING_SIZE);
		return -EINVAL;
	} else if (VOICEBUS_DEFAULT_LATENCY > ms ) {
		VB_PRINTK(vb, WARNING, MESSAGE, ms, VOICEBUS_DEFAULT_LATENCY);
		return -EINVAL;
	}
	VBLOCK(vb);
	vb->min_tx_buffer_count = ms;
	VBUNLOCK(vb);
	return 0;
}

/*! \brief Returns the number of buffers currently on the transmit queue. */
int 
voicebus_current_latency(struct voicebus *vb)
{
	LOCKS_VOICEBUS;
	int latency;
	VBLOCK(vb);
	latency = vb->min_tx_buffer_count;
	VBUNLOCK(vb);
	return latency;
}

/*! 
 * \brief Read one of the hardware control registers without acquiring locks.
 */
static inline u32 
__vb_getctl(struct voicebus *vb, u32 addr)
{
	return le32_to_cpu(inl(vb->iobase + addr));
}

/*! 
 * \brief Read one of the hardware control registers with locks held.
 */
static inline u32 
vb_getctl(struct voicebus *vb, u32 addr)
{
	LOCKS_VOICEBUS;
	u32 val;
	VBLOCK(vb);
	val = __vb_getctl(vb, addr);
	VBUNLOCK(vb);
	return val;
}

/*!
 * \brief Returns whether or not the interface is running. 
 *
 * NOTE:  Running in this case means whether or not the hardware reports the 
 *        transmit processor in any state but stopped.
 *
 * \return 1 of the process is stopped, 0 if running.
 */
static int
vb_is_stopped(struct voicebus *vb)
{
	u32 reg;
	reg = vb_getctl(vb, SR_CSR5);
	reg = (reg >> 17)&0x38;
	return (0 == reg) ? 1 : 0;
}

static void 
vb_cleanup_descriptors(struct voicebus *vb, struct voicebus_descriptor_list *dl)
{
	unsigned int i;
	struct voicebus_descriptor *d;

	assert(vb_is_stopped(vb));

	for (i=0; i < DRING_SIZE; ++i) {
		d = vb_descriptor(dl, i);
		if (d->buffer1) {
			d->buffer1 = 0;
			assert(dl->pending[i]);
			voicebus_free(vb, dl->pending[i]);
			dl->pending[i] = NULL;
		}
		d->des0 &= ~OWN_BIT;
	}
	dl->head = 0;
	dl->tail = 0;
	atomic_set(&dl->count, 0);
}

static void
vb_free_descriptors(struct voicebus *vb, struct voicebus_descriptor_list *dl)
{
	if (NULL == dl->desc) {
		WARN_ON(1);
		return;
	}
	vb_cleanup_descriptors(vb, dl);
	pci_free_consistent(
		vb->pdev, 
		(sizeof(struct voicebus_descriptor)+dl->padding)*DRING_SIZE,
		dl->desc, dl->desc_dma);
}

/*! 
 * \brief Write one of the hardware control registers without acquiring locks.
 */
static inline void 
__vb_setctl(struct voicebus *vb, u32 addr, u32 val)
{
	wmb();
	outl(cpu_to_le32(val), vb->iobase + addr);
}

/*!
 * \brief Write one of the hardware control registers with locks held.
 */
static inline void 
vb_setctl(struct voicebus *vb, u32 addr, u32 val)
{
	LOCKS_VOICEBUS;
	VBLOCK(vb);
	__vb_setctl(vb, addr, val);
	VBUNLOCK(vb);
}

static int 
__vb_sdi_clk(struct voicebus* vb)
{
	unsigned int ret;
	vb->sdi &= ~CSR9_MDC;
	__vb_setctl(vb, 0x0048, vb->sdi);
	ret = __vb_getctl(vb, 0x0048);
	vb->sdi |= CSR9_MDC;
	__vb_setctl(vb, 0x0048, vb->sdi);
	return (ret & CSR9_MDI) ? 1: 0;
}

static void 
__vb_sdi_sendbits(struct voicebus *vb, u32 bits, int count)
{
	vb->sdi &= ~CSR9_MMC;
	__vb_setctl(vb, 0x0048, vb->sdi);
	while(count--) {
		if (bits & (1 << count)) {
			vb->sdi |= CSR9_MDO;
		} else {
			vb->sdi &= ~CSR9_MDO;
		}
		__vb_sdi_clk(vb);
	}
}

#if 0  /* this function might be useful in the future for debugging. */
static unsigned int 
__vb_sdi_recvbits(struct voicebus *vb, int count)
{
	unsigned int bits=0;
	vb->sdi |= CSR9_MMC;
	__vb_setctl(vb, 0x0048, vb->sdi);
	while(count--) {
		bits <<= 1;
		if (__vb_sdi_clk(vb))
			bits |= 1;
		else
			bits &= ~1;
	}
	return bits;
}
#endif

static void 
vb_setsdi(struct voicebus *vb, int addr, u16 val)
{
	LOCKS_VOICEBUS;
	u32 bits;
	/* Send preamble */
	bits = 0xffffffff;
	VBLOCK(vb);
	__vb_sdi_sendbits(vb, bits, 32);
	bits = (0x5 << 12) | (1 << 7) | (addr << 2) | 0x2;
	__vb_sdi_sendbits(vb, bits, 16);
	__vb_sdi_sendbits(vb, val, 16);
	VBUNLOCK(vb);
}

static void 
vb_enable_io_access(struct voicebus *vb)
{
	LOCKS_VOICEBUS;
	u32 reg;
	assert(vb->pdev);
	VBLOCK(vb);
	pci_read_config_dword(vb->pdev, 0x0004, &reg);
	reg |= 0x00000007;
	pci_write_config_dword(vb->pdev, 0x0004, reg);
	VBUNLOCK(vb);
}

/*! \todo Insert comments...
 * context: !in_interrupt()
 */
void*
voicebus_alloc(struct voicebus *vb)
{
	void *vbb;
	vbb = kmem_cache_alloc(vb->buffer_cache, VOICEBUS_ALLOC_FLAGS);
	return vbb;
}

void
voicebus_setdebuglevel(struct voicebus *vb, u32 level)
{
	atomic_set(&vb->debuglevel, level);
}

int 
voicebus_getdebuglevel(struct voicebus *vb)
{
	return atomic_read(&vb->debuglevel);
}

/*! \brief Resets the voicebus hardware interface. */
static int 
vb_reset_interface(struct voicebus *vb)
{
	unsigned long timeout;
	u32 reg;
	u32 pci_access;
	const u32 DEFAULT_PCI_ACCESS = 0xfff80002;
	BUG_ON(in_interrupt());
	
	switch (vb->cache_line_size) {
	case 0x08:
		pci_access = DEFAULT_PCI_ACCESS | (0x1 << 14);
		break;
	case 0x10:
		pci_access = DEFAULT_PCI_ACCESS | (0x2 << 14);
		break;
	case 0x20: 
		pci_access = DEFAULT_PCI_ACCESS | (0x3 << 14);
		break;
	default:
		VB_PRINTK(vb, WARNING, "Host system set a cache size "\
		 "of %d which is not supported. " \
		 "Disabling memory write line and memory read line.",
		 vb->cache_line_size);
		pci_access = 0xfe584202;
		break;
	}

	/* The transmit and receive descriptors will have the same padding. */
	pci_access |= ((vb->txd.padding / sizeof(u32)) << 2) & 0x7c;

	vb_setctl(vb, 0x0000, pci_access | 1);

	timeout = jiffies + HZ/10; /* 100ms interval */
	do {
		reg = vb_getctl(vb, 0x0000);
	} while ((reg & 0x00000001) && time_before(jiffies, timeout));

	if (reg & 0x00000001) {
		VB_PRINTK(vb, ERR, "Hardware did not come out of reset "\
		 "within 100ms!");
		return -EIO;
	}

	vb_setctl(vb, 0x0000, pci_access);

	vb_cleanup_descriptors(vb, &vb->txd);
	vb_cleanup_descriptors(vb, &vb->rxd);

	/* Pass bad packets, runt packets, disable SQE function,
	 * store-and-forward */
	vb_setctl(vb, 0x0030, 0x00280048);
	/* ...disable jabber and the receive watchdog. */ 
	vb_setctl(vb, 0x0078, 0x00000013);

	/* Tell the card where the descriptors are in host memory. */
	vb_setctl(vb, 0x0020, (u32)vb->txd.desc_dma);
	vb_setctl(vb, 0x0018, (u32)vb->rxd.desc_dma);

	reg = vb_getctl(vb, 0x00fc);
	vb_setctl(vb, 0x00fc, (reg & ~0x7) | 0x7);
	vb_setsdi(vb, 0x00, 0x0100);
	vb_setsdi(vb, 0x16, 0x2100);
	
	reg = vb_getctl(vb, 0x00fc);

	vb_setctl(vb, 0x00fc, (reg & ~0x7) | 0x4);
	vb_setsdi(vb, 0x00, 0x0100); 
	vb_setsdi(vb, 0x16, 0x2100);
	reg = vb_getctl(vb, 0x00fc);
	
	
	/*
	 * The calls to setsdi above toggle the reset line of the CPLD.  Wait
	 * here to give the CPLD time to stabilize after reset.
	 */
	mdelay(1);

	return ((reg&0x7) == 0x4) ? 0 : -EIO;
}

#define OWNED(_d_) (((_d_)->des0)&OWN_BIT)
#define SET_OWNED(_d_) do { wmb(); (_d_)->des0 |= OWN_BIT; wmb();} while (0)

#ifdef DBG
static void
dump_descriptor(struct voicebus *vb, volatile struct voicebus_descriptor *d)
{
	VB_PRINTK(vb, DEBUG, "Displaying descriptor at address %08x\n", (unsigned int)d);
	VB_PRINTK(vb, DEBUG, "   des0:      %08x\n", d->des0);
	VB_PRINTK(vb, DEBUG, "   des1:      %08x\n", d->des1);
	VB_PRINTK(vb, DEBUG, "   buffer1:   %08x\n", d->buffer1);
	VB_PRINTK(vb, DEBUG, "   container: %08x\n", d->container);
}

static void
show_buffer(struct voicebus *vb, void *vbb)
{
	int x;
	unsigned char *c;
	c = vbb;
	printk("Packet %d\n", count);
	for (x = 1; x <= vb->framesize; ++x) {
		printk("%02x ", c[x]);
		if (x % 16 == 0) {
			printk("\n");
		}
	}
	printk("\n\n");
}
#endif

static inline int
vb_submit(struct voicebus *vb, struct voicebus_descriptor_list *dl, void *vbb)
{
	volatile struct voicebus_descriptor *d;
	unsigned int tail = dl->tail;
	assert_in_vb_deferred(vb);

	d = vb_descriptor(dl, tail); 

	if (unlikely(d->buffer1)) {
		/* Do not overwrite a buffer that is still in progress. */
		WARN_ON(1);
		voicebus_free(vb, vbb);
		return -EBUSY;
	}

	dl->pending[tail] = vbb;
	dl->tail = (++tail) & DRING_MASK; 
	d->buffer1 = dma_map_single(
			&vb->pdev->dev, vbb, vb->framesize, dl->direction);
	SET_OWNED(d); /* That's it until the hardware is done with it. */ 
	atomic_inc(&dl->count);
	return 0;
}

static inline void* 
vb_retrieve(struct voicebus *vb, struct voicebus_descriptor_list *dl)
{
	volatile struct voicebus_descriptor *d;
	void *vbb;
	unsigned int head = dl->head;
	assert_in_vb_deferred(vb);
	d = vb_descriptor(dl, head);
	if (!OWNED(d)) {
		dma_unmap_single(&vb->pdev->dev, d->buffer1, 
			vb->framesize, dl->direction);
		vbb = dl->pending[head];
		dl->head = (++head) & DRING_MASK; 
		d->buffer1 = 0;
		atomic_dec(&dl->count);
		return vbb;
	} else {
		return NULL;
	}
}

/*!
 * \brief Give a frame to the hardware to transmit. 
 *
 */
int
voicebus_transmit(struct voicebus *vb, void *vbb)
{
	return vb_submit(vb, &vb->txd, vbb);
}

/*!
 * \brief Give a frame to the hardware to use for receiving.
 *
 */
static inline int 
vb_submit_rxb(struct voicebus *vb, void *vbb)
{
	return vb_submit(vb, &vb->rxd, vbb);
}

/*!
 * \brief Remove the next completed transmit buffer (txb) from the tx 
 * descriptor ring.
 *
 * NOTE:  This function doesn't need any locking because only one instance is
 * 	  ever running on the deferred processing routine and it only looks at
 * 	  the head pointer. The deferred routine should only ever be running
 * 	  on one processor at a time (no multithreaded workqueues allowed!)
 *
 * Context: Must be called from the voicebus deferred workqueue.
 *
 * \return Pointer to buffer, or NULL if not available.
 */
static inline void * 
vb_get_completed_txb(struct voicebus *vb)
{
	return vb_retrieve(vb, &vb->txd); 
}

static inline void * 
vb_get_completed_rxb(struct voicebus *vb)
{
	return vb_retrieve(vb, &vb->rxd);
}

/*! 
 * \brief Free a buffer for reuse.
 *
 */
void
voicebus_free(struct voicebus *vb, void *vbb)
{
	kmem_cache_free(vb->buffer_cache, vbb);
}

/*!
 * \brief Instruct the hardware to check for a new tx descriptor.
 */
inline static void
__vb_tx_demand_poll(struct voicebus *vb) 
{
	__vb_setctl(vb, 0x0008, 0x00000000);
}

/*!
 * \brief Command the hardware to check if it owns the next transmit
 * descriptor. 
 */
static void
vb_tx_demand_poll(struct voicebus *vb)
{
	LOCKS_VOICEBUS;
	VBLOCK(vb);
	__vb_tx_demand_poll(vb);
	VBUNLOCK(vb);
}

/*!
 * \brief Command the hardware to check if it owns the next receive
 * descriptor.
 */
inline static void
__vb_rx_demand_poll(struct voicebus *vb) 
{
	__vb_setctl(vb, 0x0010, 0x00000000);
}

static void
vb_rx_demand_poll(struct voicebus *vb)
{
	LOCKS_VOICEBUS;
	VBLOCK(vb);
	__vb_rx_demand_poll(vb);
	VBUNLOCK(vb);
}

static void
__vb_enable_interrupts(struct voicebus *vb)
{
	__vb_setctl(vb, IER_CSR7, DEFAULT_INTERRUPTS); 
}

static void 
__vb_disable_interrupts(struct voicebus *vb)
{
	__vb_setctl(vb, IER_CSR7, 0); 
}

static void
vb_disable_interrupts(struct voicebus *vb)
{
	LOCKS_VOICEBUS;
	VBLOCK(vb);
	__vb_disable_interrupts(vb);
	VBUNLOCK(vb);
}

/*!
 * \brief Starts the VoiceBus interface.
 *
 * When the VoiceBus interface is started, it is actively transferring
 * frames to and from the backend of the card.  This means the card will
 * generate interrupts. 
 *
 * This function should only be called from process context, with interrupts
 * enabled, since it can sleep while running the self checks.
 *
 * \return zero on success. -EBUSY if device is already running.
 */
int 
voicebus_start(struct voicebus *vb)
{
	LOCKS_VOICEBUS;
	u32 reg;
	int i;
	void *vbb;
	int ret;

	assert(!in_interrupt());

	if (!vb_is_stopped(vb)) {
		return -EBUSY;
	}

	if ((ret=vb_reset_interface(vb))) {
		return ret;
	}
	
	/* We must set up a minimum of three buffers to start with, since two
	 * are immediately read into the TX FIFO, and the descriptor of the
	 * third is read as soon as the first buffer is done. 
	 */

	/*
	 * NOTE: The very first buffer after coming out of reset is used to
	 *  prime the pump and is lost.  So we do not want the client driver to
	 *  prepare it, since it will never see the corresponding receive
	 *  buffer.
	 * NOTE: handle_transmit is normally only called in the context of the
	 *  deferred processing thread.  Since the deferred processing thread
	 *  is known to not be running at this point, it is safe to call the
	 *  handle transmit as if it were.
	 */
	start_vb_deferred(vb); 
	/* Ensure that all the rx slots are ready for a buffer. */
	for ( i = 0; i < DRING_SIZE; ++i) {
		vbb = voicebus_alloc(vb);
		if (unlikely(NULL == vbb)) {
			BUG_ON(1);
			/* \todo I need to make sure the driver can recover
			 * from this condition. .... */
		} else {
			vb_submit_rxb(vb, vbb);
		}
	}

	for ( i=0; i < vb->min_tx_buffer_count; ++i) {
		vbb = voicebus_alloc(vb);
		if (unlikely(NULL == vbb)) {
			BUG_ON(1);
		} else {
			vb->handle_transmit(vbb, vb->context);
		}
	}
	stop_vb_deferred(vb);

	VBLOCK(vb);
	clear_bit(STOP, &vb->flags);
#if VOICEBUS_DEFERRED == TIMER
	vb->timer.expires = jiffies + HZ/1000;
	add_timer(&vb->timer);
#else
	/* Clear the interrupt status register. */
	__vb_setctl(vb, SR_CSR5, 0xffffffff);
	__vb_enable_interrupts(vb);
#endif
	/* Start the transmit and receive processors. */
	reg = __vb_getctl(vb, 0x0030);
	__vb_setctl(vb, 0x0030, reg|0x00002002);
	/* Tell the interface to poll the tx and rx descriptors. */
	__vb_rx_demand_poll(vb);
	__vb_tx_demand_poll(vb);
	VBUNLOCK(vb);

	assert(!vb_is_stopped(vb));

	return 0;
}

static void 
vb_clear_start_transmit_bit(struct voicebus *vb)
{
	LOCKS_VOICEBUS;
	u32 reg;
	VBLOCK(vb);
	reg = __vb_getctl(vb, NAR_CSR6);
	reg &= ~0x00002000;
	__vb_setctl(vb, NAR_CSR6, reg);
	VBUNLOCK(vb);
}

static void 
vb_clear_start_receive_bit(struct voicebus *vb)
{
	LOCKS_VOICEBUS;
	u32 reg;
	VBLOCK(vb);
	reg = __vb_getctl(vb, NAR_CSR6);
	reg &= ~0x00000002;
	__vb_setctl(vb, NAR_CSR6, reg);
	VBUNLOCK(vb);
}

unsigned long
vb_wait_for_completion_timeout(struct completion *x, unsigned long timeout)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11)
	/* There is a race condition here.  If x->done is reset to 0
	 * before the call to wait_for_completion after this thread wakes.
	 */
	timeout = wait_event_timeout(x->wait, x->done, timeout);
	if (timeout) {
		wait_for_completion(x);
	}
	return timeout;
#else
	return wait_for_completion_timeout(x, timeout);
#endif
}

/*!
 * \brief Stops the VoiceBus interface.
 *
 * Stops the VoiceBus interface and waits for any outstanding DMA transactions
 * to complete.  When this functions returns the VoiceBus interface tx and rx
 * states will both be suspended.
 * 
 * Only call this function from process context, with interrupt enabled, 
 * without any locks held since it sleeps.
 *
 * \return zero on success, -1 on error.
 */
int
voicebus_stop(struct voicebus *vb)
{
	assert(!in_interrupt());
	if (vb_is_stopped(vb)) {
		return 0;
	}
	INIT_COMPLETION(vb->stopped_completion);
	set_bit(STOP, &vb->flags);
	vb_clear_start_transmit_bit(vb);
	if (vb_wait_for_completion_timeout(&vb->stopped_completion, HZ)) {
#if VOICEBUS_DEFERRED == TIMER
		del_timer_sync(&vb->timer);
#else
		vb_disable_interrupts(vb);
#endif
		assert(vb_is_stopped(vb));
		clear_bit(STOP, &vb->flags);
	}
	else {
		VB_PRINTK(vb, WARNING, "Timeout while waiting for board to "\
			"stop.\n");
	}
	return 0;
}
	
/*!
 * \brief Prepare the interface for module unload.
 *
 * Stop the interface and free all the resources allocated by the driver.  The 
 * caller should have returned all VoiceBus buffers to the VoiceBus layer
 * before calling this function.
 * 
 * context: !in_interrupt()
 */
void 
voicebus_release(struct voicebus *vb)
{
	assert(!in_interrupt());

	/* quiesce the hardware */
	voicebus_stop(vb);
#if VOICEBUS_DEFERRED == WORKQUEUE
	destroy_workqueue(vb->workqueue);
#elif VOICEBUS_DEFERRED == TASKLET
	tasklet_kill(&vb->tasklet);
#endif
	vb_reset_interface(vb);
#if VOICEBUS_DEFERRED != TIMER
	free_irq(vb->pdev->irq, vb);
#endif

	/* Cleanup memory and software resources. */
	vb_free_descriptors(vb, &vb->txd);
	vb_free_descriptors(vb, &vb->rxd);
	kmem_cache_destroy(vb->buffer_cache);
	release_region(vb->iobase, 0xff);
	pci_disable_device(vb->pdev);
	kfree(vb);
}

void
__vb_increase_latency(struct voicebus *vb) 
{
	static int __warn_once = 1;
	void *vbb;
	int latency;

	assert_in_vb_deferred(vb);

	latency = atomic_read(&vb->txd.count);
	if (DRING_SIZE == latency) {
		if (__warn_once) {
			/* We must subtract two from this number since there
			 * are always two buffers in the TX FIFO.
			 */
			VB_PRINTK(vb,ERR,
				"ERROR: Unable to service card within %d ms "\
				"and unable to further increase latency.\n",
				DRING_SIZE-2);
			__warn_once = 0;
		}
	} else {
		/* Because there are 2 buffers in the transmit FIFO on the
		 * hardware, setting 3 ms of latency means that the host needs
		 * to be able to service the cards within 1ms.  This is because
		 * the interface will load up 2 buffers into the TX FIFO then
		 * attempt to read the 3rd descriptor.  If the OWN bit isn't
		 * set, then the hardware will set the TX descriptor not
		 * available interrupt.
		 */
		VB_PRINTK(vb, INFO, "Missed interrupt. " \
			"Increasing latency to %d ms in order to compensate.\n",
			latency+1);
		/* Set the minimum latency in case we're restarted...we don't
		 * want to wait for the buffer to grow to this depth again in
		 * that case. 
		 */
		voicebus_set_minlatency(vb, latency+1);
		vbb = voicebus_alloc(vb);
		if (unlikely(NULL == vbb)) {
			BUG_ON(1);
		} else {
			vb->handle_transmit(vbb, vb->context);
		}
	}
}

/*! 
 * \brief Actually process the completed transmit and receive buffers.
 *
 * NOTE: This function may be called either from a tasklet, workqueue, or
 * 	 directly in the interrupt service routine depending on 
 * 	 VOICEBUS_DEFERRED.
 */ 
static inline void 
vb_deferred(struct voicebus *vb)
{
	void *vbb;
#ifdef DBG
	static int count = 0;
#endif
	int stopping = test_bit(STOP, &vb->flags);
	int underrun = test_bit(TX_UNDERRUN, &vb->flags);


	start_vb_deferred(vb);
	if (unlikely(stopping)) {
		while((vbb = vb_get_completed_txb(vb))) {
			voicebus_free(vb, vbb);
		}
		while((vbb = vb_get_completed_rxb(vb))) {
			voicebus_free(vb, vbb);
		}
		stop_vb_deferred(vb);
		return;
	}

	if (unlikely(underrun)) {
		/* When we've underrun our FIFO, for some reason we're not
		 * able to keep enough transmit descriptors pending.  This can
		 * happen if either interrupts or this deferred processing
		 * function is not run soon enough (within 1ms when using the
		 * default 3 transmit buffers to start).  In this case, we'll
		 * insert an additional transmit buffer onto the descriptor
		 * list which decreases the sensitivity to latency, but also
		 * adds more delay to the TDM and SPI data.
		 */
		__vb_increase_latency(vb);
	}

	/* Always handle the transmit buffers first. */
	while ((vbb = vb_get_completed_txb(vb))) {
		vb->handle_transmit(vbb, vb->context);
	}

	if (unlikely(underrun)) {
		vb_rx_demand_poll(vb);
		vb_tx_demand_poll(vb);
		clear_bit(TX_UNDERRUN, &vb->flags);
	}

	while ((vbb = vb_get_completed_rxb(vb))) {
		vb->handle_receive(vbb, vb->context);
		vb_submit_rxb(vb, vbb);
	}

	stop_vb_deferred(vb);
}


/*! 
 * \brief Interrupt handler for VoiceBus interface.
 *
 * NOTE: This handler is optimized for the case where only a single interrupt
 * condition will be generated at a time.
 *
 * ALSO NOTE:  Only access the interrupt status register from this function
 * since it doesn't employ any locking on the voicebus interface.
 */
static irqreturn_t
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
vb_isr(int irq, void *dev_id, struct pt_regs *regs)
#else
vb_isr(int irq, void *dev_id)
#endif
{
	struct voicebus *vb = dev_id;
	u32 int_status;

	int_status = __vb_getctl(vb, SR_CSR5);
	/* Mask out the reserved bits. */
	int_status &= ~(0xfc004010);
	int_status &= 0x7fff;

	if (!int_status) {
		return IRQ_NONE;
	}

	if (likely(int_status & TX_COMPLETE_INTERRUPT)) {
		/* ******************************************************** */
		/* NORMAL INTERRUPT CASE				    */
		/* ******************************************************** */
#		if VOICEBUS_DEFERRED == WORKQUEUE
		queue_work(vb->workqueue, &vb->workitem);
#		elif VOICEBUS_DEFERRED == TASKLET
		tasklet_schedule(&vb->tasklet);
#		else
		vb_deferred(vb);
#		endif
		__vb_setctl(vb, SR_CSR5, TX_COMPLETE_INTERRUPT); 
	} else {
		/* ******************************************************** */
		/* ABNORMAL / ERROR CONDITIONS 				    */
		/* ******************************************************** */
		if ((int_status & TX_UNAVAILABLE_INTERRUPT) ) {
			/* This can happen if the host fails to service the
			 * interrupt within the required time interval (1ms
			 * for each buffer on the queue).  Increasing the
			 * depth of the tx queue (up to a maximum of
			 * DRING_SIZE) can make the driver / system more
			 * tolerant of interrupt latency under periods of
			 * heavy system load, but also increases the general
			 * latency that the driver adds to the voice
			 * conversations.
			 */
			set_bit(TX_UNDERRUN, &vb->flags);
#			if VOICEBUS_DEFERRED == WORKQUEUE
			queue_work(vb->workqueue, &vb->workitem);
#			elif VOICEBUS_DEFERRED == TASKLET
			tasklet_schedule(&vb->tasklet);
#			else
			vb_deferred(vb);
#			endif
		}

		if (int_status & FATAL_BUS_ERROR_INTERRUPT) {
			VB_PRINTK(vb, ERR, "Fatal Bus Error detected!\n");
		}

		if (int_status & TX_STOPPED_INTERRUPT) {
			assert(test_bit(STOP, &vb->flags));
			vb_clear_start_receive_bit(vb);
			__vb_setctl(vb, SR_CSR5, DEFAULT_INTERRUPTS); 
			__vb_disable_interrupts(vb);
			complete(&vb->stopped_completion);
		}
		if (int_status & RX_STOPPED_INTERRUPT) {
			assert(test_bit(STOP, &vb->flags));
			if (vb_is_stopped(vb)) {
				complete(&vb->stopped_completion);
			}
		}

		/* Clear the interrupt(s) */
		__vb_setctl(vb, SR_CSR5, int_status); 
	}

	return IRQ_HANDLED;
}

#if VOICEBUS_DEFERRED == TIMER
/*! \brief Called if the deferred processing is to happen in the context of
 * the timer.
 */
static void
vb_timer(unsigned long data)
{
	unsigned long start = jiffies;
	struct voicebus *vb = (struct voicebus *)data;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
	vb_isr(0, vb, 0);
#else
	vb_isr(0, vb);
#endif
	if (!vb_is_stopped(vb)) {
		vb->timer.expires = start + HZ/1000;
		add_timer(&vb->timer);
	}
}
#endif

#if VOICEBUS_DEFERRED == WORKQUEUE
static void
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
vb_workfunc(void *data)
{
	struct voicebus *vb = data;
#else
vb_workfunc(struct work_struct *work)
{
	struct voicebus *vb = container_of(work, struct voicebus, workitem);
#endif
	vb_deferred(vb);
}
#elif VOICEBUS_DEFERRED == TASKLET
static void 
vb_tasklet(unsigned long data)
{
	struct voicebus *vb = (struct voicebus*)data;
	vb_deferred(vb);
}
#endif /* #if VOICEBUS_DEFERRED == WORKQUEUE */

/*!
 * \brief Initalize the voicebus interface. 
 *
 * This function must be called in process context since it may sleep.
 * \todo Complete this description.
 */
int 
voicebus_init(struct pci_dev *pdev, u32 framesize, 
		  const char *board_name, 
		  void (*handle_receive)(void *vbb, void *context),
		  void (*handle_transmit)(void *vbb, void *context),
		  void *context, 
		  struct voicebus **vbp
		  )
{
	int retval = 0;
	struct voicebus *vb;

	assert(NULL != pdev);
	assert(NULL != board_name);
	assert(framesize);
	assert(NULL != handle_receive);
	assert(NULL != handle_transmit);

	/* ---------------------------------------------------------------- 
	   Initialize the pure software constructs.
	   ---------------------------------------------------------------- */
	*vbp = NULL;
	vb = kmalloc(sizeof(*vb), GFP_KERNEL);
	if (NULL == vb) {
		VB_PRINTK(vb, DEBUG, "Failed to allocate memory for voicebus "\
			"interface.\n");
		retval = -ENOMEM;
		goto cleanup;
	}
	memset(vb,0,sizeof(*vb));
	/* \todo make sure there is a note that the caller needs to make sure
	 * board_name stays in memory until voicebus_release is called.
	 */
	vb->board_name = board_name;
	spin_lock_init(&vb->lock);
	init_completion(&vb->stopped_completion);
	vb->pdev = pdev;
	set_bit(STOP, &vb->flags);
	clear_bit(IN_DEFERRED_PROCESSING, &vb->flags);
	vb->framesize = framesize;
	vb->min_tx_buffer_count = VOICEBUS_DEFAULT_LATENCY;

#if VOICEBUS_DEFERRED == WORKQUEUE
	/* NOTE: This workqueue must be single threaded because locking is not
	 * used when buffers are removed or added to the descriptor list, and
	 * there should only be one producer / consumer (the hardware or the
	 * deferred processing function). */
	vb->workqueue = create_singlethread_workqueue(board_name);
#	if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
	INIT_WORK(&vb->workitem, vb_workfunc, vb);
#	else
	INIT_WORK(&vb->workitem, vb_workfunc);
#	endif
#	if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
	vb_set_workqueue_priority(vb);
#	endif
#elif VOICEBUS_DEFERRED == TASKLET
	tasklet_init(&vb->tasklet, vb_tasklet, (unsigned long)vb);
#elif VOICEBUS_DEFERRED == TIMER
	init_timer(&vb->timer);
	vb->timer.function = vb_timer;
	vb->timer.data = (unsigned long)vb; 
#endif

	vb->handle_receive = handle_receive;
	vb->handle_transmit = handle_transmit;
	vb->context = context;
	
	/* \todo This cache should be shared by all instances supported by
	 * this driver. */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23)
	vb->buffer_cache = kmem_cache_create(board_name, vb->framesize, 0, 
				SLAB_HWCACHE_ALIGN, NULL, NULL);
#else
	vb->buffer_cache = kmem_cache_create(board_name, vb->framesize, 0, 
				SLAB_HWCACHE_ALIGN, NULL);
#endif
	if (NULL == vb->buffer_cache) {
		VB_PRINTK(vb, ERR, "Failed to allocate buffer cache.\n");
		goto cleanup;
	}
	

	/* ---------------------------------------------------------------- 
	   Configure the hardware / kernel module interfaces. 
	   ---------------------------------------------------------------- */
	if (pci_read_config_byte(vb->pdev, 0x0c, &vb->cache_line_size)) {
		VB_PRINTK(vb, ERR, "Failed read of cache line " \
		 "size from PCI configuration space.\n");
		goto cleanup;
	}

	if (pci_enable_device(pdev)) {
		VB_PRINTK(vb, ERR, "Failed call to pci_enable_device.\n");
		retval = -EIO;
		goto cleanup;
	} 

	/* \todo This driver should be modified to use the memory mapped I/O
	   as opposed to IO space for portability and performance. */
	if (0 == (pci_resource_flags(pdev, 0)&IORESOURCE_IO)) {
		VB_PRINTK(vb, ERR, "BAR0 is not IO Memory.\n");
		retval = -EIO;
		goto cleanup;
	}
	vb->iobase = pci_resource_start(pdev, 0);
	if(NULL == request_region(vb->iobase, 0xff, board_name)) {
		VB_PRINTK(vb, ERR, "IO Registers are in use by another " \
			"module.\n");
		retval = -EIO;
		goto cleanup;
	}

	if ((retval = vb_initialize_tx_descriptors(vb))) {
		goto cleanup;
	}
	if ((retval = vb_initialize_rx_descriptors(vb))) {
		goto cleanup;
	}

	/* ---------------------------------------------------------------- 
	   Configure the hardware interface. 
	   ---------------------------------------------------------------- */
	pci_set_master(pdev);
	vb_enable_io_access(vb);

#if VOICEBUS_DEFERRED != TIMER
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
#	define VB_IRQ_SHARED	SA_SHIRQ
#else
#	define VB_IRQ_SHARED	IRQF_SHARED	
#endif
	if (request_irq(pdev->irq, vb_isr, VB_IRQ_SHARED, vb->board_name,
		vb)) {
		assert(0);
		goto cleanup;
	}
#endif

	*vbp = vb;
	return retval;
cleanup:
	if (NULL == vb) {
		return retval;
	}
#if VOICEBUS_DEFERRED == WORKQUEUE
	if (vb->workqueue) {
		destroy_workqueue(vb->workqueue); 
	}
#elif VOICEBUS_DEFERRED == TASKLET
	tasklet_kill(&vb->tasklet);
#endif
	/* Cleanup memory and software resources. */
	if (vb->txd.desc) {
		vb_free_descriptors(vb, &vb->txd);
	}
	if (vb->rxd.desc) {
		vb_free_descriptors(vb, &vb->rxd);
	}
	if (vb->buffer_cache) {
		kmem_cache_destroy(vb->buffer_cache);
	}
	if (vb->iobase) {
		release_region(vb->iobase, 0xff);
	}
	if (vb->pdev) {
		pci_disable_device(vb->pdev);
	}
	kfree(vb);
	assert(0 != retval);
	return retval;
}


/*! \brief Return the pci_dev in use by this voicebus interface. */
struct pci_dev *
voicebus_get_pci_dev(struct voicebus *vb)
{
	return vb->pdev;
}
