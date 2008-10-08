/*
 * Zapata Telephony "Tormenta" ISA card LINUX driver, version 2.2 11/29/01
 * 
 * Modified from original tor.c by Mark Spencer <markster@digium.com>
 *                     original by Jim Dixon <jim@lambdatel.com>
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
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
#include <linux/config.h>
#endif
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <asm/io.h>
#ifdef STANDALONE_ZAPATA
#include "zaptel.h"
#else
#include <zaptel/zaptel.h>
#endif
#ifdef LINUX26
#include <linux/moduleparam.h>
#endif

/* Board address offsets (specified in word (not byte) offsets) */
#define	DDATA 0			/* Data I/O Register */
#define	DADDR 0x100		/* Dallas Card Address Reg., 0x200 in byte offset higher */
#define	CTLREG 0x100		/* Control/Status Reg., 0x200 in byte offset */

/* Control register bits */
#define	OUTBIT 8		/* Status output bit (for external measurements) */
#define	INTENA 4		/* Interrupt enable bit */
#define	MASTERVAL 0x41		/* Enable E1 master clock on Rev. B board */
#define	ENA16 0x80		/* 16 bit bus cycle enable bit */

#define	TYPE_T1	1		/* is a T1 card */
#define	TYPE_E1	2		/* is an E1 card */

#define	E1SYNCSTABLETHRESH 15000 /* amount of samples needed for E1 Sync stability */

static int syncsrc;

static int syncs[2];

static int debug;

#define	MASTERCLOCK (*clockvals)  /* value for master clock */

/* clock values */
static u_char clockvals_t1[] = {MASTERVAL,0x12,0x22,MASTERVAL};
static u_char clockvals_e1[] = {MASTERVAL,0x13,0x23,MASTERVAL};

static u_char *clockvals;

/* translations of data channels for 24 channels in a 32 bit PCM highway */
unsigned datxlt_t1[] = { 0,
    1 ,2 ,3 ,5 ,6 ,7 ,9 ,10,11,13,14,15,17,18,19,21,22,23,25,26,27,29,30,31 };

/* translations of data channels for 30/31 channels in a 32 bit PCM highway */
unsigned datxlt_e1[] = { 0,
    1 ,2 ,3 ,4 ,5 ,6 ,7 ,8 ,9 ,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,
	25,26,27,28,29,30,31 };

unsigned int *datxlt;

/* This is the order that the data (audio) channels get
scanned in. This was done in this rather poopy manner because when outputting
(and inputting) a sine wave, such as in the case of TDD, any repeated samples
(because of PCM bus contention) will result in nasty-sounding distortion. The 
Mitel STPA chips (MT8920) have a contention mechanism, which results in a
situation where, if the processor accesses a timeslot that is currently
being transmitted or received, it will HOLD the bus until it is done with
the timeslot. This means that there can be cases where we are trying
to write to a timeslot, and its already outputting the same value
as the last one (since we didnt get there in time), and in a sine-wave
output, distortion will occur. In any other output, it will be utterly
un-noticeable. So, what we do is use a pattern that gives us the most
flexibility in how long our interrupt latency is (note: Even with this,
our interrupt latency must be between 4 and 28 microseconds!!!)  Essentially
we receive the interrupt just after the 24th channel is read.  It will
take us AT LEAST 30 microseconds to read it, but could take as much as
35 microseconds to read all the channels.  In any case it's the very
first thing we do in the interrupt handler.  Worst case (30 microseconds)
is that the MT8920 has only moved 7 channels.  That's where the 6 comes from.
*/

static int chseq_t1[] = 
	{ 6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,1,2,3,4,5 } ;

static int chseq_e1[] = 
	{ 6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,1,2,3,4,5 } ;

static int *chseq;

struct torisa_pvt {
	int span;
};

static struct zt_span spans[2];
static struct zt_chan chans[64];
static struct torisa_pvt pvts[64];
static u_char txsigs[2][16];
static int loopupcnt[2];
static int loopdowncnt[2];
static int alarmtimer[2];

static int channels_per_span = 24; 

static int card_type = TYPE_T1; 

static int prefmaster = 0;

static int spansstarted = 0;

#ifdef DEFINE_RWLOCK
static DEFINE_RWLOCK(torisa);
#else
static rwlock_t torisa = RW_LOCK_UNLOCKED;
#endif

static u_char readdata[2][64][ZT_MAX_CHUNKSIZE];
static u_char writedata[2][64][ZT_MAX_CHUNKSIZE];
static u_char last_ecwrite[2][32];
static int curread;

static unsigned long base;
volatile static unsigned short *maddr;

static int irq;
static unsigned int irqcount = 0;
static unsigned int taskletsched = 0;
static unsigned int taskletrun = 0;
static unsigned int taskletexec = 0;

/* set the control register */
static void setctlreg(unsigned char val)
{
volatile register char *cp;

	cp = (char *) &maddr[CTLREG];
	*cp = val;
}

/* output a byte to one of the registers in one of the Dallas T-1 chips */
static void t1out(int spanno, int loc, unsigned char val)
{
register int n;
volatile register char *cp;

	  /* get the memory offset */
	n = spanno << 9;
	  /* point a char * at the address location */
	cp = (char *) &maddr[DADDR + n];
	*cp = loc;  /* set address in T1 chip */
	  /* point a char * at the data location */
	cp = (char *) &maddr[DDATA + n];
	*cp = val;  /* out the value */
}

/* get a byte from one of the registers in one of the Dallas T-1 chips */
static unsigned char t1in(int spanno, int loc)
{
register int n;
volatile register char *cp;

	  /* get the memory offset */
	n = spanno << 9;
	  /* point a char * at the address location */
	cp = (char *) &maddr[DADDR + n];
	*cp = loc;  /* set address in T1 chip */
	cp = (char *) &maddr[DDATA + n];
	  /* point a char * at the data location */
	return(*cp);
}

/* get input from the status register */
static unsigned char getctlreg(void)
{
register char *cp;

	cp = (char *) &maddr[CTLREG];
	return(*cp);
}

static void set_clear(void)
{
	int i,j,s;
	unsigned short val=0;
	for (s=0;s<2;s++) {
		for (i=0;i<channels_per_span;i++) {
			j = (i/8);
			if (spans[s].chans[i].flags & ZT_FLAG_CLEAR) 
				val |= 1 << (i % 8);

			if ((i % 8)==7) {
#if 0
				printk("Putting %d in register %02x on span %d\n",
				       val, 0x39 + j, 1 + s);
#endif
				t1out(1 + s, 0x39 + j, val);
				val = 0;
			}
		}
	}
		
}

/* device probe routine .. determines if the Tormenta device is present in
   the system */
static int
tor_probe(void)
{
	int			i,status;
	u_char			c1,c2;
	maddr = phys_to_virt(base);

	status = -1; /* default status return is 'not present' */

	clockvals = clockvals_t1;
	datxlt = datxlt_t1;
	chseq = chseq_t1;

	  /* initialize control register */
	setctlreg(MASTERCLOCK);

	   /* init all the registers in first T-1 chip to 0 */
	for(i = 0; i <= 0xff; i++) t1out(1,i,0); /* set register to 0 */
	/* simple test that will fail if tried in an array of standard memory */
	  /* put an 0x55 here */
	t1out(1,0x2b,0x55);
	  /* put an 0xaa here */
	t1out(1,0x2c,0xaa);
	  /* get input from first location */
	c1 = t1in(1,0x2b);
	  /* get input from second location */
	c2 = t1in(1,0x2c);
	  /* see if we read back what we put in */
	if ((c1 == 0x55) && (c2 == 0xaa)) {
		/* We now need to determine card type */
		/* This test is documented in Dallas app note 341 */
		t1out(1, 0x7D, 0);
		t1out(1, 0x36, 0);
		t1out(1, 0x15, 0);
		t1out(1, 0x19, 0);
		t1out(1, 0x23, 0x55);
		c1 = t1in(1, 0x23);  
		if (c1 == 0x55) { /* if this is an E-1 card */
	
			clockvals = clockvals_e1;
			chseq = chseq_e1;
			channels_per_span = 31;
			datxlt = datxlt_e1;
			card_type = TYPE_E1;

			  /* initialize control register */
			setctlreg(MASTERCLOCK);
		}
		/* Try to get the irq if the user didn't specify one */
		if (irq < 1) {
#ifdef LINUX26
			unsigned long irqs;
			unsigned long delay = jiffies + 5;
			irqs = probe_irq_on();
			setctlreg(MASTERCLOCK|INTENA);
			while((long)(jiffies - delay) < 0);
			irq = probe_irq_off(irqs);
#else			
			autoirq_setup(0);
			setctlreg(MASTERCLOCK|INTENA);
			/* Wait a jiffie -- that's plenty of time */
			irq = autoirq_report(5);
#endif			
		}
		/* disable interrupts having gotten one */
		setctlreg(MASTERCLOCK);
		if (irq == 2)
			irq = 9;
		if (irq) {
	  		/* init both STPA's to all silence */
			for(i = 0; i < 32; i++) maddr[i] = 0x7f7f;

			status = 0;	/* found */
			if (debug)
				printk("ISA Tormenta %s Card found at base addr 0x%lx, irq %d\n",
					((card_type == TYPE_E1) ? "E1" : "T1"),
						base,irq);
		} else
			printk("ISA Tormenta %s Card found at base addr 0x%lx, but unable to determine IRQ.  Try using irq= option\n", 
				((card_type == TYPE_E1) ? "E1" : "T1"), base );
	   }
	return status;
}

static void make_chans(void)
{
	int x,y;
	int c;
	for (x=0;x<2;x++)
		for (y=0;y<channels_per_span;y++) {
			c = x * channels_per_span + y;
			sprintf(chans[c].name, "TorISA/%d/%d", x + 1, y + 1);
			chans[c].sigcap = ZT_SIG_EM | ZT_SIG_CLEAR | ZT_SIG_FXSLS | ZT_SIG_FXSGS | ZT_SIG_FXSKS |
									 ZT_SIG_FXOLS | ZT_SIG_FXOGS | ZT_SIG_FXOKS | ZT_SIG_CAS | ZT_SIG_SF;
			chans[c].pvt = &pvts[c];
			pvts[c].span = x;
			chans[c].chanpos = y + 1;
		}
			
}

static int torisa_rbsbits(struct zt_chan *chan, int bits)
{
	u_char m,c;
	int k,n,b;
	struct torisa_pvt *p = chan->pvt;
	unsigned long flags;
#if	0
	printk("Setting bits to %x hex on channel %s\n", bits, chan->name);
#endif
	if (card_type == TYPE_E1) { /* do it E1 way */
		if (chan->chanpos > 30) return 0;  /* cant do this for chan 31 */
		n = chan->chanpos - 1;
		k = p->span;
		b = (n % 15) + 1;
		c = txsigs[k][b];
		m = (n / 15) * 4; /* nibble selector */
		c &= (15 << m); /* keep the other nibble */
		c |= (bits & 15) << (4 - m); /* put our new nibble here */
		txsigs[k][b] = c;
		  /* output them into the chip */
		t1out(k + 1,0x40 + b,c); 
		return 0;
	}						
	n = chan->chanpos - 1;
	k = p->span;
	b = (n / 8); /* get byte number */
	m = 1 << (n & 7); /* get mask */
	c = txsigs[k][b];
	c &= ~m;  /* clear mask bit */
	  /* set mask bit, if bit is to be set */
	if (bits & ZT_ABIT) c |= m;
	txsigs[k][b] = c;
	write_lock_irqsave(&torisa, flags);	
	t1out(k + 1,0x70 + b,c);
	b += 3; /* now points to b bit stuff */
	  /* get current signalling values */
	c = txsigs[k][b];
	c &= ~m;  /* clear mask bit */
	  /* set mask bit, if bit is to be set */
	if (bits & ZT_BBIT) c |= m;
	  /* save new signalling values */
	txsigs[k][b] = c;
	  /* output them into the chip */
	t1out(k + 1,0x70 + b,c);
	b += 3; /* now points to c bit stuff */
	  /* get current signalling values */
	c = txsigs[k][b];
	c &= ~m;  /* clear mask bit */
	  /* set mask bit, if bit is to be set */
	if (bits & ZT_CBIT) c |= m;
	  /* save new signalling values */
	txsigs[k][b] = c;
	  /* output them into the chip */
	t1out(k + 1,0x70 + b,c);
	b += 3; /* now points to d bit stuff */
	  /* get current signalling values */
	c = txsigs[k][b];
	c &= ~m;  /* clear mask bit */
	  /* set mask bit, if bit is to be set */
	if (bits & ZT_DBIT) c |= m;
	  /* save new signalling values */
	txsigs[k][b] = c;
	  /* output them into the chip */
	t1out(k + 1,0x70 + b,c);
	write_unlock_irqrestore(&torisa, flags);
	return 0;
}

static inline int getspan(struct zt_span *span)
{
	if (span == spans)
		return 1;
	if (span == spans + 1)
		return 2;
	return -1;
}

static int torisa_shutdown(struct zt_span *span)
{
	int i;
	int tspan;
	int wasrunning;
	unsigned long flags;

	tspan = getspan(span);
	if (tspan < 0) {
		printk("TorISA: Span '%d' isn't us?\n", span->spanno);
		return -1;
	}

	write_lock_irqsave(&torisa, flags);
	wasrunning = span->flags & ZT_FLAG_RUNNING;

	span->flags &= ~ZT_FLAG_RUNNING;
	/* Zero out all registers */
	for (i = 0; i< 0xff; i++) t1out(tspan, i, 0);
	if (wasrunning)
		spansstarted--;
	write_unlock_irqrestore(&torisa, flags);	
	if (!spans[0].flags & ZT_FLAG_RUNNING &&
	    !spans[1].flags & ZT_FLAG_RUNNING)
		/* No longer in use, disable interrupts */
		setctlreg(clockvals[syncsrc]);

	if (debug)
		printk("Span %d (%s) shutdown\n", span->spanno, span->name);
	return 0;
}

static int torisa_startup(struct zt_span *span)
{
	unsigned long endjif;
	int i;
	int tspan;
	unsigned long flags;
	char *coding;
	char *framing;
	char *crcing;
	int alreadyrunning;

	tspan = getspan(span);
	if (tspan < 0) {
		printk("TorISA: Span '%d' isn't us?\n", span->spanno);
		return -1;
	}


	write_lock_irqsave(&torisa, flags);

	alreadyrunning = span->flags & ZT_FLAG_RUNNING;

	/* initialize the start value for the last ec buffer */
	for(i = 0; i < span->channels; i++)
	{
		last_ecwrite[tspan - 1][i] = ZT_LIN2X(0,&span->chans[i]);
	}
	crcing = "";
	if (card_type == TYPE_T1) { /* if its a T1 card */
		if (!alreadyrunning) {

			setctlreg(MASTERCLOCK);
			/* Zero out all registers */
			for (i = 0x20; i< 0x40; i++)
				t1out(tspan, i, 0);
			for (i = 0x60; i< 0x80; i++)
				t1out(tspan, i, 0);
	
			/* Full-on Sync required (RCR1) */
			t1out(tspan, 0x2b, 8);	
			/* RSYNC is an input (RCR2) */
			t1out(tspan, 0x2c, 8);	
			/* RBS enable (TCR1) */
			t1out(tspan, 0x35, 0x10);
			/* TSYNC to be output (TCR2) */
			t1out(tspan, 0x36, 4);
			/* Tx & Rx Elastic store, sysclk = 2.048 mhz, loopback controls (CCR1) */
			t1out(tspan, 0x37, 0x8c);
		}
		/* Enable F bits pattern */
		i = 0x20;
		if (span->lineconfig & ZT_CONFIG_ESF)
			i = 0x88;
		if (span->lineconfig & ZT_CONFIG_B8ZS)
			i |= 0x44;
		t1out(tspan, 0x38, i);
		if (i & 0x80)
			coding = "ESF";
		else
			coding = "SF";
		if (i & 0x40)
			framing = "B8ZS";
		else {
			framing = "AMI";
			t1out(tspan,0x7e,0x1c); /* F bits pattern (0x1c) into FDL register */
		}
		t1out(tspan, 0x7c, span->txlevel << 5);

		if (!alreadyrunning) {	
			/* LIRST to 1 in CCR3 */
			t1out(tspan, 0x30, 1);
	
			/* Wait 100 ms */
			endjif = jiffies + 10;
			write_unlock_irqrestore(&torisa, flags);
	
			while(jiffies < endjif); /* wait 100 ms */

			write_lock_irqsave(&torisa, flags);
			t1out(tspan,0x30,0x40); /* set CCR3 to 0x40, resetting Elastic Store */

			span->flags |= ZT_FLAG_RUNNING;
			spansstarted++;

#if 0
			printk("Enabling interrupts: %d\n", clockvals[syncsrc] | INTENA);
#endif

			/* output the clock info and enable interrupts */
			setctlreg(clockvals[syncsrc] | INTENA);
		}
	set_clear();  /* this only applies to a T1 */
	} else { /* if its an E1 card */
		u_char ccr1 = 0, tcr1 = 0;

		if (!alreadyrunning) {
			t1out(tspan,0x1a,4); /* CCR2: set LOTCMC */
			for(i = 0; i <= 8; i++) t1out(tspan,i,0);
			for(i = 0x10; i <= 0x4f; i++) if (i != 0x1a) t1out(tspan,i,0);
			t1out(tspan,0x10,0x20); /* RCR1: Rsync as input */
			t1out(tspan,0x11,6); /* RCR2: Sysclk=2.048 Mhz */
			t1out(tspan,0x12,8); /* TCR1: TSiS mode */
		}
		tcr1 = 8; /* base TCR1 value: TSis mode */
		if (span->lineconfig & ZT_CONFIG_CCS) {
			ccr1 |= 8; /* CCR1: Rx Sig mode: CCS */
			coding = "CCS";
		} else {
			tcr1 |= 0x20; 
			coding = "CAS";
		}
		if (span->lineconfig & ZT_CONFIG_HDB3) {
			ccr1 |= 0x44; /* CCR1: TX and RX HDB3 */
			framing = "HDB3";
		} else framing = "AMI";
		if (span->lineconfig & ZT_CONFIG_CRC4) {
			ccr1 |= 0x11; /* CCR1: TX and TX CRC4 */
			crcing = "/CRC4";
		} 
		t1out(tspan,0x12,tcr1);
		t1out(tspan,0x14,ccr1);
		t1out(tspan, 0x18, 0x80);

		if (!alreadyrunning) {
			t1out(tspan,0x1b,0x8a); /* CCR3: LIRST & TSCLKM */
			t1out(tspan,0x20,0x1b); /* TAFR */
			t1out(tspan,0x21,0x5f); /* TNAFR */
			t1out(tspan,0x40,0xb); /* TSR1 */
			for(i = 0x41; i <= 0x4f; i++) t1out(tspan,i,0x55);
			for(i = 0x22; i <= 0x25; i++) t1out(tspan,i,0xff);
			/* Wait 100 ms */
			endjif = jiffies + 10;
			write_unlock_irqrestore(&torisa, flags);
			while(jiffies < endjif); /* wait 100 ms */
			write_lock_irqsave(&torisa, flags);
			t1out(tspan,0x1b,0x9a); /* CCR3: set also ESR */
			t1out(tspan,0x1b,0x82); /* CCR3: TSCLKM only now */
			
			/* output the clock info and enable interrupts */
			setctlreg(clockvals[syncsrc] | INTENA);
		}

	}

	write_unlock_irqrestore(&torisa, flags);	

	if (debug) {
		if (card_type == TYPE_T1) {
			if (alreadyrunning) 
				printk("TorISA: Reconfigured span %d (%s/%s) LBO: %s\n", span->spanno, coding, framing, zt_lboname(span->txlevel));
			else
				printk("TorISA: Startup span %d (%s/%s) LBO: %s\n", span->spanno, coding, framing, zt_lboname(span->txlevel));
		} else {
			if (alreadyrunning) 
				printk("TorISA: Reconfigured span %d (%s/%s%s) 120 ohms\n", span->spanno, coding, framing, crcing);
			else
				printk("TorISA: Startup span %d (%s/%s%s) 120 ohms\n", span->spanno, coding, framing, crcing);
		}
	}
	if (syncs[0] == span->spanno) printk("SPAN %d: Primary Sync Source\n",span->spanno);
	if (syncs[1] == span->spanno) printk("SPAN %d: Secondary Sync Source\n",span->spanno);
	return 0;
}

static int torisa_spanconfig(struct zt_span *span, struct zt_lineconfig *lc)
{
	if (debug)
		printk("TorISA: Configuring span %d\n", span->spanno);

	span->syncsrc = syncsrc;
	
	/* remove this span number from the current sync sources, if there */
	if (syncs[0] == span->spanno) syncs[0] = 0;
	if (syncs[1] == span->spanno) syncs[1] = 0;
	/* if a sync src, put it in proper place */
	if (lc->sync) syncs[lc->sync - 1] = span->spanno;
	
	/* If we're already running, then go ahead and apply the changes */
	if (span->flags & ZT_FLAG_RUNNING)
		return torisa_startup(span);

	return 0;
}

static int torisa_chanconfig(struct zt_chan *chan, int sigtype)
{
	int alreadyrunning;
	unsigned long flags;
	alreadyrunning = chan->span->flags & ZT_FLAG_RUNNING;
	if (debug) {
		if (alreadyrunning)
			printk("TorISA: Reconfigured channel %d (%s) sigtype %d\n", chan->channo, chan->name, sigtype);
		else
			printk("TorISA: Configured channel %d (%s) sigtype %d\n", chan->channo, chan->name, sigtype);
	}		
	write_lock_irqsave(&torisa, flags);	
	if (alreadyrunning && (card_type == TYPE_T1))
		set_clear();
	write_unlock_irqrestore(&torisa, flags);	
	return 0;
}

static int torisa_open(struct zt_chan *chan)
{
#ifndef LINUX26
	MOD_INC_USE_COUNT;
#endif	
	return 0;
}

static int torisa_close(struct zt_chan *chan)
{
#ifndef LINUX26
	MOD_DEC_USE_COUNT;
#endif
	return 0;
}

static int torisa_maint(struct zt_span *span, int cmd)
{
	int tspan = getspan(span);
	
	switch(cmd) {
	case ZT_MAINT_NONE:
		t1out(tspan,0x1a,4); /* clear system */
		break;
	case ZT_MAINT_LOCALLOOP:
		t1out(tspan,0x1a,5); /* local loopback */
		break;
	case ZT_MAINT_REMOTELOOP:
		t1out(tspan,0x37,6); /* remote loopback */
		break;
	case ZT_MAINT_LOOPUP:
		if (card_type == TYPE_E1) return -ENOSYS;
		t1out(tspan,0x30,2); /* send loopup code */
		break;
	case ZT_MAINT_LOOPDOWN:
		if (card_type == TYPE_E1) return -ENOSYS;
		t1out(tspan,0x30,4); /* send loopdown code */
		break;
	case ZT_MAINT_LOOPSTOP:
		if (card_type == TYPE_T1)
			 t1out(tspan,0x30,0);	/* stop sending loopup code */
		break;
	default:
		printk("torisa: Unknown maint command: %d\n", cmd);
		break;
   }
	return 0;
}

static int taskletpending;

static struct tasklet_struct torisa_tlet;

static void torisa_tasklet(unsigned long data)
{
	int x,y;
	u_char mychunk[2][ZT_CHUNKSIZE];
	taskletrun++;
	if (taskletpending) {
		taskletexec++;
		/* Perform receive data calculations.  Reverse to run most
		   likely master last */
		if (spans[1].flags & ZT_FLAG_RUNNING) {
			/* Perform echo cancellation */
			for (x=0;x<channels_per_span;x++) 
			{
				for(y = 0; y < ZT_CHUNKSIZE; y++)
				{
					mychunk[1][y] = last_ecwrite[1][x];
					last_ecwrite[1][x] = 
						writedata[1-curread][x + channels_per_span][y];
				}
				zt_ec_chunk(&spans[1].chans[x], 
					spans[1].chans[x].readchunk,mychunk[1]);
			}
			zt_receive(&spans[1]);
		}
		if (spans[0].flags & ZT_FLAG_RUNNING) {
			/* Perform echo cancellation */
			for (x=0;x<channels_per_span;x++) 
			{
				for(y = 0; y < ZT_CHUNKSIZE; y++)
				{
					mychunk[0][y] = last_ecwrite[0][x];
					last_ecwrite[0][x] = writedata[1-curread][x][y];
				}
				zt_ec_chunk(&spans[0].chans[x], 
					spans[0].chans[x].readchunk,mychunk[0]);
			}
			zt_receive(&spans[0]);
		}
	
		/* Prepare next set for transmission */
		if (spans[1].flags & ZT_FLAG_RUNNING)
			zt_transmit(&spans[1]);
		if (spans[0].flags & ZT_FLAG_RUNNING)
			zt_transmit(&spans[0]);
	}
	taskletpending = 0;
}

static int txerrors;

ZAP_IRQ_HANDLER(torisa_intr)
{
	static unsigned int passno = 0, mysynccnt = 0, lastsyncsrc = -1;
	int n, n1, i, j, k, x, mysyncsrc, oldn;
	static unsigned short rxword[33],txword[33];
	unsigned char txc, rxc, c;
	unsigned char abits, bbits, cbits, dbits;
	

	irqcount++;
	
	/* 1.  Do all I/O Immediately -- Normally we would ask for
	   the transmission first, but because of the incredibly
	   tight timing we're lucky to be able to do the I/O
	   at this point */

	  /* make sure its a real interrupt for us */
	if (!(getctlreg() & 1)) /* if not, just return */
	   {
#ifdef LINUX26
		return IRQ_NONE;
#else		
		return; 
#endif		
	   }

	  /* set outbit and put int 16 bit bus mode, reset interrupt enable */
	setctlreg(clockvals[syncsrc] | OUTBIT | ENA16);

#if 0
	if (!passno)
		printk("Interrupt handler\n");
#endif		

	/* Do the actual transmit and receive in poopy order */
	for(n1 = 0; n1 < channels_per_span; n1++)
	   {
	    n = chseq[n1];
	    maddr[DDATA + datxlt[n]] = txword[n];
	    rxword[n] = maddr[DDATA + datxlt[n]]; /* get rx word */
	   }


	setctlreg(clockvals[syncsrc] | OUTBIT); /* clear 16 bit mode */

	/* Calculate the transmit, and receive go thru all the chans */
	oldn = -1;
	for(n1 = 0; n1 < channels_per_span; n1++) {
	    n = chseq[n1];
	    txword[n] = 0;
	    if (n < oldn) {
		/* We've circled around.  
		   Now we increment the passno and stuff */
		if ((passno % ZT_CHUNKSIZE) == (ZT_CHUNKSIZE - 1)) {
			/* Swap buffers */
			for (x = 0;x < (channels_per_span * 2);x++) {
				chans[x].readchunk = readdata[curread][x];
				chans[x].writechunk = writedata[curread][x];
			}
			/* Lets work with the others now which presumably have been filled */
			curread = 1 - curread;
			if (!taskletpending) {
				taskletpending = 1;
				taskletsched++;
				tasklet_hi_schedule(&torisa_tlet); 
			} else {
				txerrors++;
			}
		}
		passno++;
	    }
	    oldn = n;
	      /* go thru both spans */
	    for(j = 0; j < 2; j++)
	      {
			/* enter the transmit stuff with i being channel number,
			   leaving with txc being character to transmit */
			txc = writedata[curread][j * channels_per_span + n-1][passno % ZT_CHUNKSIZE];
			txword[n] |= txc << (j * 8); 

			/* receive side */
			i = n + (j * channels_per_span);  /* calc chan number */
			rxc = (rxword[n] >> (j * 8)) & 0xff;
			readdata[curread][j * channels_per_span + n - 1][passno % ZT_CHUNKSIZE] = rxc;
	      }
	}

	i = passno & 127;
	/* if an E1 card, do rx signalling for it */
	if (i < 3 && (card_type == TYPE_E1)) { /* if an E1 card */
		for(j = (i * 3); j < (i * 3) + 5; j++)
		   {
			for(k = 1,x = j; k <= 2; k++,x += channels_per_span) {
				c = t1in(k,0x31 + j);
				rxc = c & 15;
				if (rxc != chans[x + 15].rxsig) {
					/* Check for changes in received bits */
					if (!(chans[x + 15].sig & ZT_SIG_CLEAR))
						zt_rbsbits(&chans[x + 15], rxc);
				}
				rxc = c >> 4;
				if (rxc != chans[x].rxsig) {
					/* Check for changes in received bits */
					if (!(chans[x].sig & ZT_SIG_CLEAR))
						zt_rbsbits(&chans[x], rxc);
				}
			}
		}
	}
	/* if a t1 card, do rx signalling for it */
	if ((i < 6) && (card_type == TYPE_T1)) {
		k = (i / 3);	/* get span */
		n = (i % 3);	/* get base */
		abits = t1in(k + 1, 0x60 + n);
		bbits = t1in(k + 1, 0x63 + n);
		cbits = t1in(k + 1, 0x66 + n);
		dbits = t1in(k + 1, 0x69 + n);
		for (j=0; j< 8; j++) {
			/* Get channel number */
			i = (k * 24) + (n * 8) + j;
			rxc = 0;
			if (abits & (1 << j)) rxc |= ZT_ABIT;
			if (bbits & (1 << j)) rxc |= ZT_BBIT;
			if (cbits & (1 << j)) rxc |= ZT_CBIT;
			if (dbits & (1 << j)) rxc |= ZT_DBIT;
			if (chans[i].rxsig != rxc) {
				/* Check for changes in received bits */
				if (!(chans[i].sig & ZT_SIG_CLEAR))
					zt_rbsbits(&chans[i], rxc);
			}
		}
	}

	if (!(passno & 0x7)) {
		for(i = 0; i < 2; i++)
		   {
			  /* if alarm timer, and it's timed out */
			if (alarmtimer[i]) {
			  if (!--alarmtimer[i])
			   {
				  /* clear recover status */
				spans[i].alarms &= ~ZT_ALARM_RECOVER;
				if (card_type == TYPE_T1)
					t1out(i + 1,0x35,0x10); /* turn off yel */
				else
					t1out(i + 1,0x21,0x5f); /* turn off remote alarm */
				zt_alarm_notify(&spans[i]);  /* let them know */
			   }
			  }
		   }
	}

	i = passno & 511;
	if ((i == 100) || (i == 101))
	   {
		j = 0;  /* clear this alarm status */
		i -= 100;
		if (card_type == TYPE_T1) {
			c = t1in(i + 1,0x31); /* get RIR2 */
			spans[i].rxlevel = c >> 6;  /* get rx level */
			t1out(i + 1,0x20,0xff); 
			c = t1in(i + 1,0x20);  /* get the status */
			  /* detect the code, only if we are not sending one */
			if ((!spans[i].mainttimer) && (c & 0x80))  /* if loop-up code detected */
			   {
				  /* set into remote loop, if not there already */
				if ((loopupcnt[i]++ > 80) && 
					(spans[i].maintstat != ZT_MAINT_REMOTELOOP))
				   {
					t1out(i + 1,0x37,0x9c); /* remote loopback */
					spans[i].maintstat = ZT_MAINT_REMOTELOOP;
				   }
			   } else loopupcnt[i] = 0;
			  /* detect the code, only if we are not sending one */
			if ((!spans[i].mainttimer) && (c & 0x40))  /* if loop-down code detected */
			   {
				  /* if in remote loop, get out of it */
				if ((loopdowncnt[i]++ > 80) &&
					(spans[i].maintstat == ZT_MAINT_REMOTELOOP))
				   {
					t1out(i + 1,0x37,0x8c); /* normal */
					spans[i].maintstat = ZT_MAINT_NONE;
				   }
			   } else loopdowncnt[i] = 0;
			if (c & 3) /* if red alarm */
			   {
				j |= ZT_ALARM_RED;
			   }
			if (c & 8) /* if blue alarm */
			   {
				j |= ZT_ALARM_BLUE;
			   }
		} else { /* its an E1 card */
			t1out(i + 1,6,0xff); 
			c = t1in(i + 1,6);  /* get the status */
			if (c & 9) /* if red alarm */
			   {
				j |= ZT_ALARM_RED;
			   }
			if (c & 2) /* if blue alarm */
			   {
				j |= ZT_ALARM_BLUE;
			   }
		}
		  /* only consider previous carrier alarm state */
		spans[i].alarms &= (ZT_ALARM_RED | ZT_ALARM_BLUE | ZT_ALARM_NOTOPEN);
		n = 1; /* set to 1 so will not be in yellow alarm if we dont
			care about open channels */
		  /* if to have yellow alarm if nothing open */
		if (spans[i].lineconfig & ZT_CONFIG_NOTOPEN)
		   {
			  /* go thru all chans, and count # open */
			for(n = 0,k = (i * channels_per_span); k < (i * channels_per_span) + channels_per_span; k++) 
			   {
				if ((chans[k].flags & ZT_FLAG_OPEN) ||
				    (chans[k].flags & ZT_FLAG_NETDEV)) n++;
			   }
			  /* if none open, set alarm condition */
			if (!n) j |= ZT_ALARM_NOTOPEN; 
		   }
		  /* if no more alarms, and we had some */
		if ((!j) && spans[i].alarms)
		   {
			alarmtimer[i] = ZT_ALARMSETTLE_TIME; 
		   }
		if (alarmtimer[i]) j |= ZT_ALARM_RECOVER;
		  /* if going into alarm state, set yellow (remote) alarm */
		if ((j) && (!spans[i].alarms)) {
			if (card_type == TYPE_T1) t1out(i + 1,0x35,0x11);
				else t1out(i + 1,0x21,0x7f);
		}
		if (c & 4) /* if yellow alarm */
			j |= ZT_ALARM_YELLOW;
		if (spans[i].maintstat || spans[i].mainttimer) j |= ZT_ALARM_LOOPBACK;
		spans[i].alarms = j;
		zt_alarm_notify(&spans[i]);
	   }
	if (!(passno % 8000)) /* even second boundary */
	   {
		  /* do both spans */
		for(i = 1; i <= 2; i++)
		   {
			if (card_type == TYPE_T1) {
				   /* add this second's BPV count to total one */
				spans[i - 1].bpvcount += t1in(i,0x24) + (t1in(i,0x23) << 8);
			} else {
				   /* add this second's BPV count to total one */
				spans[i - 1].bpvcount += t1in(i,1) + (t1in(i,0) << 8);
			}
		   }
	   }
	/* re-evaluate active sync src */
	mysyncsrc = 0;
	  /* if primary sync specified, see if we can use it */
	if (syncs[0])
	   {
		  /* if no alarms, use it */
		if (!(spans[syncs[0] - 1].alarms & (ZT_ALARM_RED | ZT_ALARM_BLUE | 
			ZT_ALARM_LOOPBACK))) mysyncsrc = syncs[0];
	   }
	   /* if we dont have one yet, and there is a secondary, see if we can use it */
	if ((!mysyncsrc) && (syncs[1]))
	   {
		  /* if no alarms, use it */
		if (!(spans[syncs[1] - 1].alarms & (ZT_ALARM_RED | ZT_ALARM_BLUE | 
			ZT_ALARM_LOOPBACK))) mysyncsrc = syncs[1];
	   }
	/* on the E1 card, the PLL takes a bit of time to lock going
	   between internal and external clocking. There needs to be some
	   settle time before actually changing the source, otherwise it will
	   oscillate between in and out of sync */
	if (card_type == TYPE_E1)
	   {
		  /* if stable, add to count */
		if (lastsyncsrc == mysyncsrc) mysynccnt++; else mysynccnt = 0;
		lastsyncsrc = mysyncsrc;
		 /* if stable sufficiently long, change it */
		if (mysynccnt >= E1SYNCSTABLETHRESH)
		   {
			mysynccnt = 0;
			syncsrc = mysyncsrc;
		   } 
	   }
	else syncsrc = mysyncsrc; /* otherwise on a T1 card, just use current value */
	/* update sync src info */
	spans[0].syncsrc = spans[1].syncsrc = syncsrc;
	/* If this is the last pass, then prepare the next set */
	  /* clear outbit, restore interrupt enable */
	setctlreg(clockvals[syncsrc] | INTENA);
#ifdef LINUX26
	return IRQ_RETVAL(1);
#endif	
}


static int torisa_ioctl(struct zt_chan *chan, unsigned int cmd, unsigned long data)
{
	struct torisa_debug td;
	switch(cmd) {
	case TORISA_GETDEBUG:
		td.txerrors = txerrors;
		td.irqcount = irqcount;
		td.taskletsched = taskletsched;
		td.taskletrun = taskletrun;
		td.taskletexec = taskletexec;
		td.span1flags = spans[0].flags;
		td.span2flags = spans[1].flags;
		if (copy_to_user((struct torisa_debug *)data, &td, sizeof(td)))
			return -EFAULT;
		return 0;
	default:
		return -ENOTTY;
	}
	return 0;
}

static int __init tor_init(void)
{
	if (!base) {
		printk("Specify address with base=0xNNNNN\n");
		return -EIO;
	}
	if (tor_probe()) {
		printk(KERN_ERR "No ISA tormenta card found at %05lx\n", base);
		return -EIO;
	}
	if (request_irq(irq, torisa_intr, ZAP_IRQ_DISABLED, "torisa", NULL)) {
		printk(KERN_ERR "Unable to request tormenta IRQ %d\n", irq);
		return -EIO;
	}
	if (!request_mem_region(base, 4096, "Tormenta ISA")) {
		printk(KERN_ERR "Unable to request 4k memory window at %lx\n", base);
		free_irq(irq, NULL);
		return -EIO;
	}

	strcpy(spans[0].name, "TorISA/1");
	zap_copy_string(spans[0].desc, "ISA Tormenta Span 1", sizeof(spans[0].desc));
	spans[0].manufacturer = "Digium";
	zap_copy_string(spans[0].devicetype, "Tormenta ISA", sizeof(spans[0].devicetype));
	spans[0].spanconfig = torisa_spanconfig;
	spans[0].chanconfig = torisa_chanconfig;
	spans[0].startup = torisa_startup;
	spans[0].shutdown = torisa_shutdown;
	spans[0].rbsbits = torisa_rbsbits;
	spans[0].maint = torisa_maint;
	spans[0].open = torisa_open;
	spans[0].close  = torisa_close;
	spans[0].channels = channels_per_span;
	spans[0].chans = &chans[0];
	spans[0].flags = ZT_FLAG_RBS;
	spans[0].ioctl = torisa_ioctl;
	spans[0].irq = irq;

	if (card_type == TYPE_E1) {
		spans[0].spantype = "E1";
		spans[0].linecompat = ZT_CONFIG_AMI | ZT_CONFIG_B8ZS | ZT_CONFIG_D4 | ZT_CONFIG_ESF;
		spans[0].deflaw = ZT_LAW_ALAW;
	} else {
		spans[0].spantype = "T1";
		spans[0].linecompat = ZT_CONFIG_HDB3 | ZT_CONFIG_CCS | ZT_CONFIG_CRC4;
		spans[0].deflaw = ZT_LAW_MULAW;
	}

	spans[1] = spans[0];
	strcpy(spans[1].name, "TorISA/2");
	strcpy(spans[1].desc, "ISA Tormenta Span 2");
	spans[1].chans = &chans[channels_per_span];

	init_waitqueue_head(&spans[0].maintq);
	init_waitqueue_head(&spans[1].maintq);

	make_chans();
	if (zt_register(&spans[0], prefmaster)) {
		printk(KERN_ERR "Unable to register span %s\n", spans[0].name);
		return -EIO;
	}
	if (zt_register(&spans[1], 0)) {
		printk(KERN_ERR "Unable to register span %s\n", spans[1].name);
		zt_unregister(&spans[0]);
		return -EIO;
	}
	tasklet_init(&torisa_tlet, torisa_tasklet, (long)0);
	printk("TORISA Loaded\n");
	return 0;
}


#if !defined(LINUX26)
static int __init set_tor_base(char *str)
{
	base = simple_strtol(str, NULL, 0);
	return 1;
}

__setup("tor=", set_tor_base);
#endif

static void __exit tor_exit(void)
{
	free_irq(irq, NULL);
	release_mem_region(base, 4096);
	if (spans[0].flags & ZT_FLAG_REGISTERED)
		zt_unregister(&spans[0]);
	if (spans[1].flags & ZT_FLAG_REGISTERED)
		zt_unregister(&spans[1]);
}

MODULE_AUTHOR("Mark Spencer <markster@digium.com>");
MODULE_DESCRIPTION("Tormenta ISA Zapata Telephony Driver");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

#ifdef LINUX26
module_param(prefmaster, int, 0600);
module_param(base, long, 0600);
module_param(irq, int, 0600);
module_param(syncsrc, int, 0600);
module_param(debug, int, 0600);
#else
MODULE_PARM(prefmaster, "i");
MODULE_PARM(base, "i");
MODULE_PARM(irq, "i");
MODULE_PARM(syncsrc, "i");
MODULE_PARM(debug, "i");
#endif

module_init(tor_init);
module_exit(tor_exit);
