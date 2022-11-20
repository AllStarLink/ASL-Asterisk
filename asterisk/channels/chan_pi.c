/* #define	NEW_ASTERISK */
/*
 * Copyright (C) 2016, Jim Dixon
 *
 * Asterisk -- An open source telephony toolkit.
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Jim Dixon, WB6NIL <jim@lambdatel.com>
 * Based upon work by Mark Spencer <markster@digium.com> and Luigi Rizzo
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
  * at the top of the source tree.
 */

/* 
 * Patching for aarch64 (ARM64) support by Gianni Peschiutta (F4IKZ)
 * Disable Direct I/O port access on ARM64
 */


/*! \file
 *
 * \brief Channel driver for DMK Engineering "PITA" Board on Rpi2/3
 *
 * \author Jim Dixon  <jim@lambdatel.com>
 *
 * \ingroup channel_drivers
 */

/*** MODULEINFO
        <defaultenabled>yes</defaultenabled> 	 	 
 ***/

#include "asterisk.h"
#include "../astver.h"

/*
 * Please change this revision number when you make a edit
 * use the simple format YYMMDD
*/

ASTERISK_FILE_VERSION(__FILE__,"$Revision$")
// ASTERISK_FILE_VERSION(__FILE__,"$Revision$")

#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#ifndef __aarch64__ /* no direct IO port access on ARM64 architecture */
#include <sys/io.h>
#endif
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <sys/mman.h>
#include <alsa/asoundlib.h>
#include <i2c/smbus.h>
#include <linux/i2c-dev.h>		//Needed for I2C port
#include <math.h>

#define DEBUG_CAPTURES	 		1

#define RX_CAP_RAW_FILE			"/tmp/rx_cap_in.pcm"
#define TX_CAP_RAW_FILE			"/tmp/tx_cap_in.pcm"

#define	DELIMCHR ','
#define	QUOTECHR 34

#define	READERR_THRESHOLD 50

#include "asterisk/lock.h"
#include "asterisk/frame.h"
#include "asterisk/logger.h"
#include "asterisk/callerid.h"
#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/options.h"
#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/causes.h"
#include "asterisk/endian.h"
#include "asterisk/stringfields.h"
#include "asterisk/abstract_jb.h"
#include "asterisk/musiconhold.h"
#include "asterisk/dsp.h"

#define	DESIRED_RATE ((usedsp) ? 48000 : 8000)

#define	NTAPS 31
#define	NTAPS_PL 6

#include "xpmr/xpmr.h"

/*! Global jitterbuffer configuration - by default, jb is disabled */
static struct ast_jb_conf default_jbconf =
{
	.flags = 0,
	.max_size = -1,
	.resync_threshold = -1,
	.impl = "",
};
static struct ast_jb_conf global_jbconf;

/*
 * pi.conf parameters are
START_CONFIG

[general]
    ; General config options which propigate to all devices, with
    ; default values shown. You may have as many devices as the
    ; system will allow. You must use one section per device, with
    ; [1] generally (although its up to you) being the first device.
    ;
    ;
    ; debug = 0x0		; misc debug flags, default is 0
    ; i2caddr = 0x55		; i2c io address of IO Expander, default is 0x27

	; rxboost = 0          ; no rx gain boost
	; carrierfrom = hw   ;no,hw,hwinvert
	; ctcssfrom = hw      ;no,hw,hwinvert

	; invertptt = 0

        ; duplex = 1		; duplex mode    

	; rxondelay = 0		  ; number of 20ms intervals to hold off receiver turn-on indication


    ;------------------------------ JITTER BUFFER CONFIGURATION --------------------------
    ; jbenable = yes              ; Enables the use of a jitterbuffer on the receiving side of an
                                  ; pi channel. Defaults to "no". An enabled jitterbuffer will
                                  ; be used only if the sending side can create and the receiving
                                  ; side can not accept jitter. The pi channel can't accept jitter,
                                  ; thus an enabled jitterbuffer on the receive pi side will always
                                  ; be used if the sending side can create jitter.

    ; jbmaxsize = 200             ; Max length of the jitterbuffer in milliseconds.

    ; jbresyncthreshold = 1000    ; Jump in the frame timestamps over which the jitterbuffer is
                                  ; resynchronized. Useful to improve the quality of the voice, with
                                  ; big jumps in/broken timestamps, usualy sent from exotic devices
                                  ; and programs. Defaults to 1000.

    ; jbimpl = fixed              ; Jitterbuffer implementation, used on the receiving side of an pi
                                  ; channel. Two implementations are currenlty available - "fixed"
                                  ; (with size always equals to jbmax-size) and "adaptive" (with
                                  ; variable size, actually the new jb of IAX2). Defaults to fixed.

    ; jblog = no                  ; Enables jitterbuffer frame logging. Defaults to "no".
    ;-----------------------------------------------------------------------------------

[1]

; First channel unique config

[2]

; Second channel config

END_CONFIG

 */

/*
 * Helper macros to parse config arguments. They will go in a common
 * header file if their usage is globally accepted. In the meantime,
 * we define them here. Typical usage is as below.
 * Remember to open a block right before M_START (as it declares
 * some variables) and use the M_* macros WITHOUT A SEMICOLON:
 *
 *	{
 *		M_START(v->name, v->value) 
 *
 *		M_BOOL("dothis", x->flag1)
 *		M_STR("name", x->somestring)
 *		M_F("bar", some_c_code)
 *		M_END(some_final_statement)
 *		... other code in the block
 *	}
 *
 * XXX NOTE these macros should NOT be replicated in other parts of asterisk. 
 * Likely we will come up with a better way of doing config file parsing.
 */
#define M_START(var, val) \
        char *__s = var; char *__val = val;
#define M_END(x)   x;
#define M_F(tag, f)			if (!strcasecmp((__s), tag)) { f; } else
#define M_BOOL(tag, dst)	M_F(tag, (dst) = ast_true(__val) )
#define M_UINT(tag, dst)	M_F(tag, (dst) = strtoul(__val, NULL, 0) )
#define M_STR(tag, dst)		M_F(tag, ast_copy_string(dst, __val, sizeof(dst)))

/*
 * The following parameters are used in the driver:
 *
 *  FRAME_SIZE	the size of an audio frame, in samples.
 *		160 is used almost universally, so you should not change it.
 *
 */

#define FRAME_SIZE	160
#define PERIOD_FRAMES 80
#define	PLAYBACK_MAX 192

#define	IOEXP_IODIRA 0
#define	IOEXP_IODIRB 1
#define	IOEXP_GPIOA 0x12
#define	IOEXP_GPIOB 0x13
#define	IOEXP_OLATA 0x14
#define	IOEXP_OLATB 0x15

#define	MASK_GPIOS_PTT1 1
#define	MASK_GPIOS_PTT2 2
#define	MASK_GPIOS_COR1 4
#define	MASK_GPIOS_CTCSS1 8
#define	MASK_GPIOS_COR2 0x10
#define	MASK_GPIOS_CTCSS2 0x20
#define	MASK_GPIOS_GPIO1 0x40
#define	MASK_GPIOS_GPIO2 0x80
#define	MASK_GPIOS_GPIO3 0x100
#define	MASK_GPIOS_GPIO4 0x200
#define	MASK_GPIOS_GPIO5 0x400
#define	MASK_GPIOS_GPIO6 0x800
#define	MASK_GPIOS_GPIO7 0x1000
#define	MASK_GPIOS_GPIO8 0x2000
#define	MASK_GPIOS_GPIO9 0x4000
#define	MASK_GPIOS_GPIO10 0x8000
#define	MASK_GPIOS_GPIOS (MASK_GPIOS_GPIO1 | MASK_GPIOS_GPIO2 | MASK_GPIOS_GPIO3 | MASK_GPIOS_GPIO4 | MASK_GPIOS_GPIO5 |  \
	MASK_GPIOS_GPIO6 | MASK_GPIOS_GPIO7 | MASK_GPIOS_GPIO8 | MASK_GPIOS_GPIO9 | MASK_GPIOS_GPIO10)

#define	DEFAULT_IODIRA 0xfc  // Everyting input export PTT's
#define	DEFAULT_IODIRB 0xff  // Everyting input

#define IN  1
#define OUT 0

#define LOW  0
#define HIGH 1

#define	MICMAX 31

#if defined(__FreeBSD__)
#define	FRAGS	0x8
#else
#define	FRAGS	( ( (5 * 6) << 16 ) | 0xa )
#endif

/*
 * XXX text message sizes are probably 256 chars, but i am
 * not sure if there is a suitable definition anywhere.
 */
#define TEXT_SIZE	256

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

struct
{
	snd_pcm_t *icard, *ocard;
} alsa;


int readdev = -1;
int writedev = -1;
int w_errors = 0;				/* overfull in the write path */
int ndevices = 0;
int readpos;				/* read position above */
// Buffers for stereo signed 16bit, 48Ks/s
char pi_read_buf[FRAME_SIZE * 4 * 6];
char pi_read_buf1[FRAME_SIZE * 4 * 6];
char pi_write_buf[FRAME_SIZE * 4 * 6];    
ast_mutex_t rxbuflock;

int pi_write_dst;
int total_blocks;			/* total blocks in the output device */
int usedsp = 1;		// Default to using DSP
int readpipe[2];

int has_been_open = 0;

int32_t	gpio_val = 0;		/* current value of gpios */
int32_t	gpio_ctl = 0;		/* mask of output ones */
int32_t	gpios_mask[] = {0,MASK_GPIOS_GPIO1,MASK_GPIOS_GPIO2,MASK_GPIOS_GPIO3,MASK_GPIOS_GPIO4,MASK_GPIOS_GPIO5,
	MASK_GPIOS_GPIO6,MASK_GPIOS_GPIO7,MASK_GPIOS_GPIO8,MASK_GPIOS_GPIO9,MASK_GPIOS_GPIO10} ;
int32_t gpio_lastmask = 0;
int32_t	gpio_pulsemask = 0;
int	gpio_pulsetimer[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0} ;
struct timeval gpio_then;
AST_MUTEX_DEFINE_STATIC(gpio_lock);

int file_i2c = -1;

#if __BYTE_ORDER == __LITTLE_ENDIAN
static snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;
#else
static snd_pcm_format_t format = SND_PCM_FORMAT_S16_BE;
#endif

static char *config = "pi.conf";	/* default config file */
static char *config1 = "pi_tune_%s.conf";    /* tune config file */
static char *i2c_filename = "/dev/i2c-1";
int i2c_addr = 0x27;

static FILE *frxcapraw = NULL;
static FILE *ftxcapraw = NULL;

static int pi_debug;

char *notindsp = "Cannot do this while in DSP mode!!\n";
char *onlyindsp = "Cannot do this while NOT in DSP mode!!\n";

/*	DECLARE STRUCTURES */

/*
 * descriptor for one of our channels.
 * There is one used for 'default' values (from the [general] entry in
 * the configuration file), and then one instance for each device
 * (the default is cloned from [general], others are only created
 * if the relevant section exists).
 */
struct chan_pi_pvt {
	char *name;

	short cdMethod;
	int hookstate;
	int usedtmf;

	int overridecontext;

	struct ast_channel *owner;
	char ext[AST_MAX_EXTENSION];
	char ctx[AST_MAX_CONTEXT];
	char language[MAX_LANGUAGE];
	char cid_name[256];			/*XXX */
	char cid_num[256];			/*XXX */
	char mohinterpret[MAX_MUSICCLASS];

	/* buffers used in pi_read - AST_FRIENDLY_OFFSET space for headers
	 * plus enough room for a full frame
	 */
	char pi_read_frame_buf[FRAME_SIZE * 2 + AST_FRIENDLY_OFFSET];
#ifdef 	OLD_ASTERISK
	AST_LIST_HEAD(, ast_frame) txq;
#else
	AST_LIST_HEAD_NOLOCK(, ast_frame) txq;
#endif
	ast_mutex_t  txqlock;
	struct ast_frame read_f;	/* returned by pi_read */

	char 	debuglevel;
	char 	radioduplex;			// 

	int  	tracetype;
	int     tracelevel;

	char lastrx;
	char rxhidsq;
	char rxhidctcss;

	char rxkeyed;	  			// indicates rx signal present
	char ptt;				// indicates asserting PTT

	char lasttx;
	char txkeyed;				// tx key request from upper layers 
	char txtestkey;

	struct ast_dsp *dsp;

	float	hpx[NTAPS_PL + 1];
	float	hpy[NTAPS_PL + 1];

	int32_t	destate;

	char 	rxcdtype;
	char 	rxsdtype;

	char	invertptt;

	int	rxondelay;
	int	rxoncnt;
	char	rxboost;

	int	rxmixerset;
	float	rxvoiceadj;
	float	rxctcssadj;
	int	txctcssadj;
	int 	txmixerset;

	int measure_enabled;

	t_pmr_chan	*pmrChan;
	char	txctcssdefault[32];
	char	txctcssfreqs[32];
	int	txmix;
	int	txtoctype;
	int	txlimonly;
	int	txprelim;
	char	rxctcssfreqs[32];
	char	rxdemod;
	int	rxsquelchadj;   /* this copy needs to be here for initialization */
	int	rxsqhyst;
	int     rxsqvoxadj;
	int	rxnoisefiltype;
	int	rxsquelchdelay;
	int	rxctcssrelax;


	int32_t discfactor;
	int32_t discounterl;
	int32_t discounteru;
	int16_t amax;
	int16_t amin;
	int16_t apeak;
	int plfilter;
	int deemphasis;
	char *gpios[32];
	int32_t	last_gpios_in;
	char had_gpios_in;
	struct timeval starttime;
};

static struct chan_pi_pvt pi_default = {
	.ext = "s",
	.ctx = "default",
	.usedtmf = 1,
	.rxondelay = 0,
};

/*	DECLARE FUNCTION PROTOTYPES	*/

static int mixer_write(void);
static void tune_write(struct chan_pi_pvt *o);

static char *pi_active = NULL;	 /* the active device */

static struct ast_channel *pi_request(const char *type, int format, void *data, int *cause);
static int pi_digit_begin(struct ast_channel *c, char digit);
static int pi_digit_end(struct ast_channel *c, char digit, unsigned int duration);
static int pi_text(struct ast_channel *c, const char *text);
static int pi_hangup(struct ast_channel *c);
static int pi_answer(struct ast_channel *c);
static struct ast_frame *pi_read(struct ast_channel *chan);
static int pi_call(struct ast_channel *c, char *dest, int timeout);
static int pi_write(struct ast_channel *chan, struct ast_frame *f);
static int pi_indicate(struct ast_channel *chan, int cond, const void *data, size_t datalen);
static int pi_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static int pi_setoption(struct ast_channel *chan, int option, void *data, int datalen);

static char tdesc[] = "Raspberry Pi Radio Channel Driver";

struct chan_pi_pvt pvts[2];

static const struct ast_channel_tech pi_tech = {
	.type = "Pi",
	.description = tdesc,
	.capabilities = AST_FORMAT_SLINEAR,
	.requester = pi_request,
	.send_digit_begin = pi_digit_begin,
	.send_digit_end = pi_digit_end,
	.send_text = pi_text,
	.hangup = pi_hangup,
	.answer = pi_answer,
	.read = pi_read,
	.call = pi_call,
	.write = pi_write,
	.indicate = pi_indicate,
	.fixup = pi_fixup,
	.setoption = pi_setoption,
};

static int
GPIODirection(int pin, int dir)
{
int	x;

	if (pin > 0x80)
	{
		pin >>= 8;
		x = i2c_smbus_read_byte_data(file_i2c,IOEXP_IODIRB);
		if (x < 0)
		{
			ast_log(LOG_ERROR,"Failed to read IOEXP_IODIRB\n");
			return -1;
		}
		if (dir)
			x |= pin;
		else
			x &= ~pin;
		if (i2c_smbus_write_byte_data(file_i2c,IOEXP_IODIRB,0) < 0)
		{
			ast_log(LOG_ERROR,"Failed to write IOEXP_IODIRB\n");
			return -1;
		}
		return 0;
	}
	x = i2c_smbus_read_byte_data(file_i2c,IOEXP_IODIRA);
	if (x < 0)
	{
		ast_log(LOG_ERROR,"Failed to read IOEXP_IODIRA\n");
		return -1;
	}
	if (dir)
		x |= pin;
	else
		x &= ~pin;
	if (i2c_smbus_write_byte_data(file_i2c,IOEXP_IODIRA,0) < 0)
	{
		ast_log(LOG_ERROR,"Failed to write IOEXP_IODIRA\n");
		return -1;
	}
	return 0;
}
 
static int
GPIOWrite(int pin, int value)
{
int	x;

	if (pin > 0x80)
	{
		pin >>= 8;
		x = i2c_smbus_read_byte_data(file_i2c,IOEXP_OLATB);
		if (x < 0)
		{
			ast_log(LOG_ERROR,"Failed to read IOEXP_OLATB\n");
			return -1;
		}
		if (value)
			x |= pin;
		else
			x &= ~pin;
		if (i2c_smbus_write_byte_data(file_i2c,IOEXP_OLATB,x) < 0)
		{
			ast_log(LOG_ERROR,"Failed to write IOEXP_OLATB\n");
			return -1;
		}
		return 0;
	}
	x = i2c_smbus_read_byte_data(file_i2c,IOEXP_OLATA);
	if (x < 0)
	{
		ast_log(LOG_ERROR,"Failed to read IOEXP_OLATA\n");
		return -1;
	}
	if (value)
		x |= pin;
	else
		x &= ~pin;
	if (i2c_smbus_write_byte_data(file_i2c,IOEXP_OLATA,x) < 0)
	{
		ast_log(LOG_ERROR,"Failed to write IOEXP_OLATA\n");
		return -1;
	}
	return 0;
}

static unsigned long get_gpios(void)
{
int	x,y;

	x = i2c_smbus_read_byte_data(file_i2c,IOEXP_GPIOA);
	if (x < 0)
	{
		ast_log(LOG_ERROR,"Failed to read IOEXP_GPIOA\n");
		return -1;
	}
	y = i2c_smbus_read_byte_data(file_i2c,IOEXP_GPIOB);
	if (y < 0)
	{
		ast_log(LOG_ERROR,"Failed to read IOEXP_GPIOB\n");
		return -1;
	}
	return((y << 8) | x);
}

static void update_ptt(void)
{
	GPIOWrite(MASK_GPIOS_PTT1,(pvts[0].ptt) ? ((pvts[0].invertptt) ? LOW : HIGH) : ((pvts[0].invertptt) ? HIGH : LOW));
	GPIOWrite(MASK_GPIOS_PTT2,(pvts[1].ptt) ? ((pvts[1].invertptt) ? LOW : HIGH) : ((pvts[1].invertptt) ? HIGH : LOW));
	return;
}

static void set_ptt(int n,int val)
{
	pvts[n].ptt = val;
	update_ptt();
	return;
}

static void set_gpios(void)
{
	if (gpio_ctl & MASK_GPIOS_GPIO1)
	{
		GPIOWrite(MASK_GPIOS_GPIO1,(gpio_val & MASK_GPIOS_GPIO1) ? HIGH : LOW);
	}
	if (gpio_ctl & MASK_GPIOS_GPIO2)
	{
		GPIOWrite(MASK_GPIOS_GPIO2,(gpio_val & MASK_GPIOS_GPIO2) ? HIGH : LOW);
	}
	if (gpio_ctl & MASK_GPIOS_GPIO3)
	{
		GPIOWrite(MASK_GPIOS_GPIO3,(gpio_val & MASK_GPIOS_GPIO3) ? HIGH : LOW);
	}
	if (gpio_ctl & MASK_GPIOS_GPIO4)
	{
		GPIOWrite(MASK_GPIOS_GPIO4,(gpio_val & MASK_GPIOS_GPIO4) ? HIGH : LOW);
	}
	if (gpio_ctl & MASK_GPIOS_GPIO5)
	{
		GPIOWrite(MASK_GPIOS_GPIO5,(gpio_val & MASK_GPIOS_GPIO5) ? HIGH : LOW);
	}
	if (gpio_ctl & MASK_GPIOS_GPIO6)
	{
		GPIOWrite(MASK_GPIOS_GPIO6,(gpio_val & MASK_GPIOS_GPIO6) ? HIGH : LOW);
	}
	if (gpio_ctl & MASK_GPIOS_GPIO7)
	{
		GPIOWrite(MASK_GPIOS_GPIO7,(gpio_val & MASK_GPIOS_GPIO7) ? HIGH : LOW);
	}
	if (gpio_ctl & MASK_GPIOS_GPIO8)
	{
		GPIOWrite(MASK_GPIOS_GPIO8,(gpio_val & MASK_GPIOS_GPIO8) ? HIGH : LOW);
	}
	if (gpio_ctl & MASK_GPIOS_GPIO9)
	{
		GPIOWrite(MASK_GPIOS_GPIO9,(gpio_val & MASK_GPIOS_GPIO9) ? HIGH : LOW);
	}
	if (gpio_ctl & MASK_GPIOS_GPIO10)
	{
		GPIOWrite(MASK_GPIOS_GPIO10,(gpio_val & MASK_GPIOS_GPIO10) ? HIGH : LOW);
	}
	return;
}

/* IIR 6 pole High pass filter, 300 Hz corner with 0.5 db ripple */

#define GAIN1   1.745882764e+00

static int16_t hpass6(int16_t input,float *xv,float *yv)
{
        xv[0] = xv[1]; xv[1] = xv[2]; xv[2] = xv[3]; xv[3] = xv[4]; xv[4] = xv[5]; xv[5] = xv[6];
        xv[6] = ((float)input) / GAIN1;
        yv[0] = yv[1]; yv[1] = yv[2]; yv[2] = yv[3]; yv[3] = yv[4]; yv[4] = yv[5]; yv[5] = yv[6];
        yv[6] =   (xv[0] + xv[6]) - 6 * (xv[1] + xv[5]) + 15 * (xv[2] + xv[4])
                     - 20 * xv[3]
                     + ( -0.3491861578 * yv[0]) + (  2.3932556573 * yv[1])
                     + ( -6.9905126572 * yv[2]) + ( 11.0685981760 * yv[3])
                     + ( -9.9896695552 * yv[4]) + (  4.8664511065 * yv[5]);
        return((int)yv[6]);
}


/* Perform standard 6db/octave de-emphasis */
static int16_t deemph(int16_t input,int32_t *state)
{

int16_t coeff00 = 6878;
int16_t coeff01 = 25889;
int32_t accum; /* 32 bit accumulator */

        accum = input;
        /* YES! The parenthesis REALLY do help on this one! */
        *state = accum + ((*state * coeff01) >> 15);
        accum = (*state * coeff00);
        /* adjust gain so that we have unity @ 1KHz */
        return((accum >> 14) + (accum >> 15));
}


/* IIR 3 pole High pass filter, 300 Hz corner with 0.5 db ripple */

static int16_t hpass(int16_t input,float *xv,float *yv)
{
#define GAIN   1.280673652e+00

        xv[0] = xv[1]; xv[1] = xv[2]; xv[2] = xv[3];
        xv[3] = ((float)input) / GAIN;
        yv[0] = yv[1]; yv[1] = yv[2]; yv[2] = yv[3];
        yv[3] =   (xv[3] - xv[0]) + 3 * (xv[1] - xv[2])
                     + (  0.5999763543 * yv[0]) + ( -2.1305919790 * yv[1])
                     + (  2.5161440793 * yv[2]);
        return((int)yv[3]);
}

static struct chan_pi_pvt *find_pvt(char *str)
{
int	i;

	if (str == NULL) return NULL;
	for(i = 0; i < 2; i++)
	{
		if (!pvts[i].name) continue;
		if (!strcasecmp(pvts[i].name,str)) return (&pvts[i]);
	}
	return NULL;
}

static int rad_rxwait(int fd,int ms)
{
fd_set fds;
struct timeval tv;

	FD_ZERO(&fds);
	FD_SET(fd,&fds);
	tv.tv_usec = ms * 1000;
	tv.tv_sec = 0;
	return(select(fd + 1,&fds,NULL,NULL,&tv));
}

static int happy_mswait(int fd,int ms, int flag)
{
int	i;

	if (!flag)
	{
		usleep(ms * 1000);
		return(0);
	}
	i = 0;
	if (ms >= 100) for(i = 0; i < ms; i += 100)
	{
		ast_cli(fd,"\r");
		if (rad_rxwait(fd,100)) return(1);
	}
	if (rad_rxwait(fd,ms - i)) return(1);
	ast_cli(fd,"\r");
	return(0);
}

/* Call with:  devnum: alsa major device number, param: ascii Formal
Parameter Name, val1, first or only value, val2 second value, or 0 
if only 1 value. Values: 0-99 (percent) or 0-1 for baboon.

Note: must add -lasound to end of linkage */

static int setamixer(int devnum,char *param, int v1, int v2)
{
int	type;
char	str[100];
snd_hctl_t *hctl;
snd_ctl_elem_id_t *id;
snd_ctl_elem_value_t *control;
snd_hctl_elem_t *elem;
snd_ctl_elem_info_t *info;

	sprintf(str,"hw:%d",devnum);
	if (snd_hctl_open(&hctl, str, 0)) return(-1);
	snd_hctl_load(hctl);
	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
	snd_ctl_elem_id_set_name(id, param);  
	elem = snd_hctl_find_elem(hctl, id);
	if (!elem)
	{
		snd_hctl_close(hctl);
		fprintf(stderr,"Cannot find mixer element '%s'\n",param);
		return(-1);
	}
	snd_ctl_elem_info_alloca(&info);
	snd_hctl_elem_info(elem,info);
	type = snd_ctl_elem_info_get_type(info);
	snd_ctl_elem_value_alloca(&control);
	snd_ctl_elem_value_set_id(control, id);    
	switch(type)
	{
	    case SND_CTL_ELEM_TYPE_INTEGER:
	    case SND_CTL_ELEM_TYPE_BYTES:
	    case SND_CTL_ELEM_TYPE_ENUMERATED:
		snd_ctl_elem_value_set_integer(control, 0, v1);
		if (v2 >= 0) snd_ctl_elem_value_set_integer(control, 1, v2);
		break;
	    case SND_CTL_ELEM_TYPE_BOOLEAN:
		snd_ctl_elem_value_set_integer(control, 0, (v1 != 0));
		if (v2 >= 0) snd_ctl_elem_value_set_integer(control, 1, (v2 != 0));
		break;
	}
	if (snd_hctl_elem_write(elem, control))
	{
		snd_hctl_close(hctl);
		fprintf(stderr,"Cannot set value for mixer element '%s'\n",param);
		return(-1);
	}
	snd_hctl_close(hctl);
	return(0);
}

/*
*/

/* Write an exactly FRAME_SIZE sized frame */
static int soundcard_writeframe(short *data)
{
        snd_pcm_state_t state;

#if DEBUG_CAPTURES == 1
	if (ftxcapraw) 
		fwrite(((void *) data),1,FRAME_SIZE * 2 * 2,ftxcapraw);
#endif
	state = snd_pcm_state(alsa.ocard);
	if (state == SND_PCM_STATE_XRUN)
		snd_pcm_prepare(alsa.ocard);
	return(snd_pcm_writei(alsa.ocard, (void *)data, (usedsp) ? FRAME_SIZE * 6 : FRAME_SIZE));
}

static snd_pcm_t *alsa_card_init(char *dev, snd_pcm_stream_t stream)
{
	int err;
	snd_pcm_t *handle = NULL;
	snd_pcm_hw_params_t *hwparams = NULL;
	snd_pcm_sw_params_t *swparams = NULL;
	snd_pcm_uframes_t chunk_size = 0;
	unsigned period_time = 0;
	unsigned buffer_time = 80000;
	snd_pcm_uframes_t period_frames = 0;
	snd_pcm_uframes_t buffer_frames = 0;
	struct pollfd pfd;
	snd_pcm_uframes_t buffer_size = 0;

	unsigned int rate = DESIRED_RATE;
	snd_pcm_uframes_t start_threshold, stop_threshold;

	err = snd_pcm_open(&handle, dev, stream, SND_PCM_NONBLOCK );
	if (err < 0) {
		ast_log(LOG_ERROR, "snd_pcm_open failed: %s\n", snd_strerror(err));
		return NULL;
	} else
		ast_log(LOG_DEBUG, "Opening device %s in %s mode\n", dev, (stream == SND_PCM_STREAM_CAPTURE) ? "read" : "write");

	hwparams = alloca(snd_pcm_hw_params_sizeof());
	memset(hwparams, 0, snd_pcm_hw_params_sizeof());
	snd_pcm_hw_params_any(handle, hwparams);

	err = snd_pcm_hw_params_set_access(handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0)
	{
		ast_log(LOG_ERROR, "set_access failed: %s\n", snd_strerror(err));
		return NULL;
	}
	err = snd_pcm_hw_params_set_format(handle, hwparams, format);
	if (err < 0) 
	{
		ast_log(LOG_ERROR, "set_format failed: %s\n", snd_strerror(err));
		return NULL;
	}

	err = snd_pcm_hw_params_set_channels(handle, hwparams, 2);
	if (err < 0)
	{
		ast_log(LOG_ERROR, "set_channels failed: %s\n", snd_strerror(err));
		return NULL;
	}
	err = snd_pcm_hw_params_set_rate_near(handle, hwparams, &rate, 0);
	if (err < 0)
	{
		ast_log(LOG_ERROR, "set_rate failed: %s\n", snd_strerror(err));
		return NULL;
	}

	if (buffer_time == 0 && buffer_frames == 0) {
		err = snd_pcm_hw_params_get_buffer_time_max(hwparams,
							    &buffer_time, 0);
		assert(err >= 0);
		if (buffer_time > 500000)
			buffer_time = 500000;
	}
	if (period_time == 0 && period_frames == 0) {
		if (buffer_time > 0)
			period_time = buffer_time / 4;
		else
			period_frames = buffer_frames / 4;
	}
	if (period_time > 0)
		err = snd_pcm_hw_params_set_period_time_near(handle, hwparams,
							     &period_time, 0);
	else
		err = snd_pcm_hw_params_set_period_size_near(handle, hwparams,
							     &period_frames, 0);
	if (err < 0)
	{
		ast_log(LOG_ERROR, "set_period time/size failed: %s\n", snd_strerror(err));
		return NULL;
	}

	if (buffer_time > 0) {
		err = snd_pcm_hw_params_set_buffer_time_near(handle, hwparams,
							     &buffer_time, 0);
	} else {
		err = snd_pcm_hw_params_set_buffer_size_near(handle, hwparams,
							     &buffer_frames);
	}

	if (err < 0)
	{
		ast_log(LOG_ERROR, "set_buffer time/size failed: %s\n", snd_strerror(err));
		return NULL;
	}

	err = snd_pcm_hw_params(handle, hwparams);
	if (err < 0)
	{
		ast_log(LOG_ERROR, "set_hw_params failed: %s\n", snd_strerror(err));
		return NULL;
	}

	snd_pcm_hw_params_get_period_size(hwparams, &chunk_size, 0);
	snd_pcm_hw_params_get_buffer_size(hwparams, &buffer_size);

	if (chunk_size == buffer_size) {
		ast_log(LOG_ERROR,"Can't use period equal to buffer size (%lu == %lu)\n",
		      chunk_size, buffer_size);
		return NULL;
	}

	swparams = alloca(snd_pcm_sw_params_sizeof());
	memset(swparams, 0, snd_pcm_sw_params_sizeof());
	snd_pcm_sw_params_current(handle, swparams);

	err = snd_pcm_sw_params_set_avail_min(handle, swparams, chunk_size);

	if (err < 0)
	{
		ast_log(LOG_ERROR, "set_avail_min failed: %s\n", snd_strerror(err));
		return NULL;
	}

	if (stream == SND_PCM_STREAM_PLAYBACK)
		start_threshold = buffer_size;
	else
		start_threshold = 1;

	err = snd_pcm_sw_params_set_start_threshold(handle, swparams, start_threshold);
	if (err < 0)
	{
		ast_log(LOG_ERROR, "set_start_threshold failed: %s\n", snd_strerror(err));
		return NULL;
	}

	stop_threshold = buffer_size;
	err = snd_pcm_sw_params_set_stop_threshold(handle, swparams, stop_threshold);
	if (err < 0)
	{
		ast_log(LOG_ERROR, "set_stopt_threshold failed: %s\n", snd_strerror(err));
		return NULL;
	}

	err = snd_pcm_sw_params(handle, swparams);
	if (err < 0)
	{
		ast_log(LOG_ERROR, "set_sw_params failed: %s\n", snd_strerror(err));
		return NULL;
	}

	err = snd_pcm_poll_descriptors_count(handle);
	if (err <= 0)
	{
		ast_log(LOG_ERROR, "Unable to get a poll descriptors count, error is %s\n", snd_strerror(err));
		return NULL;
	}
	if (err != 1)
	{
		ast_log(LOG_ERROR, "Can't handle more than one device\n");
		return NULL;
	}
	snd_pcm_poll_descriptors(handle, &pfd, err);
	ast_log(LOG_DEBUG, "Acquired fd %d from the poll descriptor\n", pfd.fd);

	if (stream == SND_PCM_STREAM_CAPTURE)
		readdev = pfd.fd;
	else
		writedev = pfd.fd;

	return handle;
}

/*
 * some of the standard methods supported by channels.
 */
static int pi_digit_begin(struct ast_channel *c, char digit)
{
	return 0;
}

static int pi_digit_end(struct ast_channel *c, char digit, unsigned int duration)
{
	/* no better use for received digits than print them */
	ast_verbose(" << Chan_pi Received digit %c of duration %u ms >> \n", 
		digit, duration);
	return 0;
}

static int pi_text(struct ast_channel *c, const char *text)
{
	struct chan_pi_pvt *o = c->tech_pvt;
	char *cmd;

	cmd = alloca(strlen(text) + 10);

	/* print received messages */
	if(o->debuglevel)ast_verbose(" << Chan_pi Received pi text %s >> \n", text);

	if (!strncasecmp(text,"GPIO",4))
	{
		int i,j,cnt;

		cnt = sscanf(text,"%s %d %d",cmd,&i,&j);
		if (cnt < 3) return 0;
		if ((i < 1) || (i > 5)) return 0;
		ast_mutex_lock(&gpio_lock);
		if (j > 1) /* if to request pulse-age */
		{
			gpio_pulsetimer[i] = j - 1;
		}
		else
		{
			/* clear pulsetimer, if in the middle of running */
			gpio_pulsetimer[i] = 0;
			gpio_val &= ~gpios_mask[i];
			if (j) gpio_val |= gpios_mask[i];
			set_gpios();
		}
		ast_mutex_unlock(&gpio_lock);
		return 0;
	}
	return 0;
}

/*
 * handler for incoming calls. Either autoanswer, or start ringing
 */
static int pi_call(struct ast_channel *c, char *dest, int timeout)
{

	ast_setstate(c, AST_STATE_UP);
	return 0;
}

/*
 * remote side answered the phone
 */
static int pi_answer(struct ast_channel *c)
{

	ast_setstate(c, AST_STATE_UP);
	return 0;
}

static int pi_hangup(struct ast_channel *c)
{
	struct chan_pi_pvt *o = c->tech_pvt;
	int chindex = 0;

	if (o == &pvts[1]) chindex = 1;
	//ast_log(LOG_NOTICE, "pi_hangup()\n");
	c->tech_pvt = NULL;
	o->owner = NULL;
	ast_module_unref(ast_module_info->self);
	if (o->hookstate) o->hookstate = 0;
	set_ptt(chindex,0);
	return 0;
}


/* used for data coming from the network */
static int pi_write(struct ast_channel *c, struct ast_frame *f)
{
	struct chan_pi_pvt *o = c->tech_pvt;
	struct ast_frame *f1;

	if (usedsp)
	{
		o->pmrChan->txPttIn =  o->txkeyed || o->txtestkey;
		PmrTx(o->pmrChan,(i16*)AST_FRAME_DATAP(f));
	}
	else
	{
		if ((!o->txkeyed) && (!o->txtestkey)) return 0;
		f1 = ast_frdup(f);
		memset(&f1->frame_list,0,sizeof(f1->frame_list));
		ast_mutex_lock(&o->txqlock);
		AST_LIST_INSERT_TAIL(&o->txq,f1,frame_list);
		ast_mutex_unlock(&o->txqlock);
	}
	return 0;
}

static void dorxproc(struct ast_channel *c)
{
	int i;
	short *sp,*sp1;
	char xbuf[(FRAME_SIZE * 12) + AST_FRIENDLY_OFFSET];
	i16 xmtbuf1[FRAME_SIZE * 12];
	struct ast_frame *f1,f2;
	struct chan_pi_pvt *o = c->tech_pvt;

	memset(&f2,0,sizeof(f2));
        f2.frametype = AST_FRAME_VOICE;
        f2.subclass = AST_FORMAT_SLINEAR;
        f2.samples = FRAME_SIZE * 6;
        f2.datalen = FRAME_SIZE * 12;
        AST_FRAME_DATA(f2) = xbuf + AST_FRIENDLY_OFFSET;
	f2.offset = AST_FRIENDLY_OFFSET;
	sp = (o == &pvts[0]) ? (i16*)pi_read_buf : (i16*)(pi_read_buf1 + 2);
	PmrRx(o->pmrChan,sp,(i16*)(o->pi_read_frame_buf + AST_FRIENDLY_OFFSET),xmtbuf1);
	ast_mutex_lock(&rxbuflock);
	memcpy(pi_read_buf1,pi_read_buf,sizeof(pi_read_buf1));
	ast_mutex_unlock(&rxbuflock);
	sp = (short *) AST_FRAME_DATA(f2);
	sp1 = (short *) xmtbuf1;
	for(i = 0; i < FRAME_SIZE * 6; i++)
	{
		*sp++ = *sp1;
		sp1 += 2;
	}
	f1 = ast_frdup(&f2);
	memset(&f1->frame_list,0,sizeof(f1->frame_list));
	ast_mutex_lock(&o->txqlock);
	AST_LIST_INSERT_TAIL(&o->txq,f1,frame_list);
	ast_mutex_unlock(&o->txqlock);
	return;
}

static struct ast_frame *pi_read(struct ast_channel *c)
{
	int res,cd,sd,i,j,k,m,n,n0,n1,ismaster,chindex;
	unsigned int gv;
	struct chan_pi_pvt *o = c->tech_pvt;
	struct ast_frame *f = &o->read_f,*f1,*f0;
	struct ast_frame wf = { AST_FRAME_CONTROL };
	short *sp,*sp0,*sp1, *sp2;
        snd_pcm_state_t state;
	struct timeval tv,now;

	ismaster = 0;
	for(chindex = 0; chindex < 2; chindex++)
	{
		if (o == &pvts[chindex]) break;
	}
	if ((chindex == 0) ||
		((chindex == 1) && (pvts[0].owner == NULL))) ismaster = 1;

	/* XXX can be simplified returning &ast_null_frame */
	/* prepare a NULL frame in case we don't have enough data to return */
	bzero(f, sizeof(struct ast_frame));
	f->frametype = AST_FRAME_NULL;
	f->src = pi_tech.type;

	if (!ismaster)
	{
		if (usedsp) 
		{
			while (read(readpipe[0],&i,1) == 1);
			dorxproc(c);
		}
		return f;
	}

	state = snd_pcm_state(alsa.icard);
	if ((state != SND_PCM_STATE_PREPARED) && (state != SND_PCM_STATE_RUNNING)) {
		snd_pcm_prepare(alsa.icard);
	}

	gettimeofday(&tv,NULL);

	ast_mutex_lock(&rxbuflock);
	res = snd_pcm_readi(alsa.icard, pi_read_buf,(usedsp) ? FRAME_SIZE * 6 :FRAME_SIZE);
	if (res < 0)				/* audio data not ready, return a NULL frame */
	{
                return f;
        }
	ast_mutex_unlock(&rxbuflock);
	if (usedsp && pvts[0].owner && pvts[1].owner)
		write(readpipe[1],&n0,1);

#if DEBUG_CAPTURES == 1
	if (frxcapraw) fwrite(pi_read_buf,1,res,frxcapraw);
#endif

	for(;;)
	{
		n0 = 0;
		ast_mutex_lock(&pvts[0].txqlock);
		AST_LIST_TRAVERSE(&pvts[0].txq, f1,frame_list) n0++;
		ast_mutex_unlock(&pvts[0].txqlock);
		n1 = 0;
		ast_mutex_lock(&pvts[1].txqlock);
		AST_LIST_TRAVERSE(&pvts[1].txq, f1,frame_list) n1++;
		ast_mutex_unlock(&pvts[1].txqlock);
		n = MAX(n0,n1);
		if (n && ((n > 3) || (!o->txkeyed)))
		{
			f0 = f1 = NULL;
			if (n0)
			{
				ast_mutex_lock(&pvts[0].txqlock);
				f0 = AST_LIST_REMOVE_HEAD(&pvts[0].txq,frame_list);
				ast_mutex_unlock(&pvts[0].txqlock);
				n = f0->datalen;
			}
			if (n1)
			{
				ast_mutex_lock(&pvts[1].txqlock);
				f1 = AST_LIST_REMOVE_HEAD(&pvts[1].txq,frame_list);
				ast_mutex_unlock(&pvts[1].txqlock);
				n = f1->datalen;
			}
			if (n0 && n1) n = MIN(f0->datalen,f1->datalen);
			if (usedsp)
			{
				if (n > (FRAME_SIZE * 12)) n = FRAME_SIZE * 12;
			}
			else
			{
				if (n > (FRAME_SIZE * 2)) n = FRAME_SIZE * 2;
			}
			sp = (short *) (pi_write_buf);
			sp0 = sp1 = NULL;
			if (f0) sp0 = (short *) AST_FRAME_DATAP(f0);
			if (f1) sp1 = (short *) AST_FRAME_DATAP(f1);
			for(i = 0; i < (n / 2); i++)
			{
				if (f0) *sp++ = *sp0++;
				else *sp++ = 0;					
				if (f1) *sp++ = *sp1++;
				else *sp++ = 0;						
			}	
			soundcard_writeframe((short *)pi_write_buf);
			if (f0) ast_frfree(f0);
			if (f1) ast_frfree(f1);
			continue;
		}
		break;
	}
	if (usedsp)
		dorxproc(c);
	gv = get_gpios();
	pvts[0].rxhidsq = 0;
	if (gv & MASK_GPIOS_COR1) pvts[0].rxhidsq = 1;
	pvts[0].rxhidctcss = 0;
	if (gv & MASK_GPIOS_CTCSS1) pvts[0].rxhidctcss = 1;
	pvts[1].rxhidsq = 0;
	if (gv & MASK_GPIOS_COR2) pvts[1].rxhidsq = 1;
	pvts[1].rxhidctcss = 0;
	if (gv & MASK_GPIOS_CTCSS2) pvts[1].rxhidctcss = 1;
	ast_mutex_lock(&gpio_lock);
	now = ast_tvnow();
	j = ast_tvdiff_ms(now,gpio_then);
	gpio_then = now;
	/* make output inversion mask (for pulseage) */
	gpio_lastmask = gpio_pulsemask;
	gpio_pulsemask = 0;
	for(i = 1; i <= 10; i++)
	{
		k = gpio_pulsetimer[i];
		if (k)
		{
			k -= j;
			if (k < 0) k = 0;
			gpio_pulsetimer[i] = k;
		}
		if (k) gpio_pulsemask |= gpios_mask[i];
	}
	if (gpio_pulsemask != gpio_lastmask) /* if anything inverted (temporarily) */
	{
		gpio_val ^= gpio_lastmask ^ gpio_pulsemask;
		set_gpios();
	}
	ast_mutex_unlock(&gpio_lock);
	if (!usedsp)
	{
		sp = (short *)pi_read_buf;
		sp1 = (short *)(pvts[0].pi_read_frame_buf + AST_FRIENDLY_OFFSET);
		sp2 = (short *)(pvts[1].pi_read_frame_buf + AST_FRIENDLY_OFFSET);
		for(n = 0; n < FRAME_SIZE; n++)
		{
			if (pvts[1].plfilter && pvts[1].deemphasis)
				*sp1++ = hpass6(deemph(*sp++,&pvts[1].destate),pvts[1].hpx,pvts[1].hpy);
			else if (pvts[1].deemphasis)
				*sp1++ = deemph(*sp++,&pvts[1].destate);
			else if (pvts[1].plfilter)
				*sp1++ = hpass(*sp++,pvts[1].hpx,pvts[1].hpy);
			else
				*sp1++ = *sp++;
			if (pvts[0].plfilter && pvts[0].deemphasis)
				*sp2++ = hpass6(deemph(*sp++,&pvts[0].destate),pvts[0].hpx,pvts[0].hpy);
			else if (pvts[0].deemphasis)
				*sp2++ = deemph(*sp++,&pvts[0].destate);
			else if (pvts[0].plfilter)
				*sp2++ = hpass(*sp++,pvts[0].hpx,pvts[0].hpy);
			else
				*sp2++ = *sp++;
		}			
	}
        readpos = 0;		       /* reset read pointer for next frame */

	for(m = 0; m < 2; m++)
	{
		o = &pvts[m];

		if (!o->owner) continue;
		if (o->owner)
		{

			cd = 1; /* assume CD */
			if ((o->rxcdtype == CD_HID) && (!o->rxhidsq)) cd = 0;
			else if ((o->rxcdtype == CD_HID_INVERT) && o->rxhidsq) cd = 0;
			else if (usedsp && (o->rxcdtype==CD_XPMR_NOISE) && (!o->pmrChan->rxCarrierDetect)) cd = 0;
			/* apply cd turn-on delay, if one specified */
			if (o->rxondelay && cd && (o->rxoncnt++ < o->rxondelay)) cd = 0;
			else if (!cd) o->rxoncnt = 0;

			sd = 1; /* assume SD */
			if ((o->rxsdtype == SD_HID) && (!o->rxhidctcss)) sd = 0;
			else if ((o->rxsdtype == SD_HID_INVERT) && o->rxhidctcss) sd = 0;
			else if (usedsp && (o->rxsdtype == SD_XPMR) &&
				((!o->pmrChan->b.ctcssRxEnable) || (o->pmrChan->rxCtcss->decode <= CTCSS_NULL))) sd = 0;

			o->rxkeyed = sd && cd && ((!o->lasttx) || o->radioduplex);
	
			if (o->lastrx && (!o->rxkeyed))
			{
				o->lastrx = 0;
				wf.subclass = AST_CONTROL_RADIO_UNKEY;
				ast_queue_frame(o->owner, &wf);
				if (o->debuglevel) ast_verbose("Channel %s RX UNKEY\n",o->name);
			}
			else if ((!o->lastrx) && (o->rxkeyed))
			{
				o->lastrx = 1;
				wf.subclass = AST_CONTROL_RADIO_KEY;
				ast_queue_frame(o->owner, &wf);
				if (o->debuglevel) ast_verbose("Channel %s RX KEY\n",o->name);
			}
	
			if (usedsp)
				n = o->pmrChan->txPttOut;
			else
				n = o->txkeyed || o->txtestkey;
			if (o->lasttx && (!n))
			{
				o->lasttx = 0;
				ast_mutex_lock(&gpio_lock);
				set_ptt(m,0);
				ast_mutex_unlock(&gpio_lock);
				if (o->debuglevel) ast_verbose("Channel %s TX UNKEY\n",o->name);
			}
			else if ((!o->lasttx) && n)
			{
				o->lasttx = 1;
				ast_mutex_lock(&gpio_lock);
				set_ptt(m,1);
				ast_mutex_unlock(&gpio_lock);
				if (o->debuglevel) ast_verbose("Channel %s TX KEY\n",o->name);
			}

			gv = get_gpios() & MASK_GPIOS_GPIOS;
			for(i = 1; i <= 10; i++)
			{
				/* if a valid input bit, dont clear it */
				if ((o->gpios[i]) && (!strcasecmp(o->gpios[i],"in"))) continue;
				gv &= ~(gpios_mask[i]); /* clear the bit, since its not an input */
			}
			if (((!o->had_gpios_in) || (o->last_gpios_in != gv)) &&
				(!ast_tvzero(o->starttime)) && (ast_tvdiff_ms(ast_tvnow(),o->starttime) >= 550))
			{
				char buf1[100];
				struct ast_frame fr;

				for(i = 1; i <= 10; i++)
				{
					/* skip if not specified */
					if (!o->gpios[i]) continue;
					/* skip if not input */
					if (strcasecmp(o->gpios[i],"in")) continue;
					/* if bit has changed, or never reported */
					if ((!o->had_gpios_in) || ((o->last_gpios_in & gpios_mask[i]) != (gv & (gpios_mask[i]))))
					{
						sprintf(buf1,"GPIO%d %d",i,(gv & (gpios_mask[i])) ? 1 : 0);
						memset(&fr,0,sizeof(fr));
						AST_FRAME_DATA(fr) =  buf1;
						fr.datalen = strlen(buf1) + 1;
						fr.samples = 0;
						fr.frametype = AST_FRAME_TEXT;
						fr.subclass = 0;
						fr.src = "chan_pi";
						fr.offset = 0;
						fr.mallocd=0;
						fr.delivery.tv_sec = 0;
						fr.delivery.tv_usec = 0;
						ast_queue_frame(o->owner,&fr);
					}
				}
				o->had_gpios_in = 1;
				o->last_gpios_in = gv;
			}
		        if (c->_state != AST_STATE_UP)  /* drop data if frame is not up */
				continue;
		}
		f->offset = AST_FRIENDLY_OFFSET;
	        /* ok we can build and deliver the frame to the caller */
	        f->frametype = AST_FRAME_VOICE;
	        f->subclass = AST_FORMAT_SLINEAR;
	        f->samples = FRAME_SIZE;
	        f->datalen = FRAME_SIZE * 2;
	        AST_FRAME_DATAP(f) = o->pi_read_frame_buf + AST_FRIENDLY_OFFSET;
	        if (o->rxvoiceadj > 1.0) {  /* scale and clip values */
	                int i, x;
			float f1;
	                int16_t *p = (int16_t *) AST_FRAME_DATAP(f);

	                for (i = 0; i < f->samples; i++) {
				f1 = (float)p[i] * o->rxvoiceadj;
				x = (int)f1;
	                        if (x > 32767)
	                                x = 32767;
	                        else if (x < -32768)
	                                x = -32768;
	                        p[i] = x;
	                }
	        }
		if (o->measure_enabled)
		{
			int i;
			int32_t accum;
	                int16_t *p = (int16_t *) AST_FRAME_DATAP(f);

			for(i = 0; i < f->samples; i++)
			{
				accum = p[i];
				if (accum > o->amax)
				{
					o->amax = accum;
					o->discounteru = o->discfactor;
				}
				else if (--o->discounteru <= 0)
				{
					o->discounteru = o->discfactor;
					o->amax = (int32_t)((o->amax * 32700) / 32768);
				}
				if (accum < o->amin)
				{
					o->amin = accum;
					o->discounterl = o->discfactor;
				}
				else if (--o->discounterl <= 0)
				{
					o->discounterl = o->discfactor;
					o->amin= (int32_t)((o->amin * 32700) / 32768);
				}
			}
			o->apeak = (int32_t)(o->amax - o->amin) / 2;
		}
		if (o->usedtmf && o->dsp)
		{
		    f1 = ast_dsp_process(c,o->dsp,f);
		    if ((f1->frametype == AST_FRAME_DTMF_END) ||
		      (f1->frametype == AST_FRAME_DTMF_BEGIN))
		    {
			if ((f1->subclass == 'm') || (f1->subclass == 'u')) continue;
			if (f1->frametype == AST_FRAME_DTMF_END)
				ast_log(LOG_NOTICE,"pi/%s Got DTMF char %c\n",o->name,f1->subclass);
			ast_queue_frame(o->owner,f1);
			continue;
		    }
		}
	        f->offset = AST_FRIENDLY_OFFSET;
		ast_queue_frame(o->owner,f);
	}
	f->frametype = AST_FRAME_NULL;
        return f;
}

static int pi_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct chan_pi_pvt *o = newchan->tech_pvt;
	ast_log(LOG_WARNING,"pi_fixup()\n");
	o->owner = newchan;
	return 0;
}

static int pi_indicate(struct ast_channel *c, int cond, const void *data, size_t datalen)
{
	struct chan_pi_pvt *o = c->tech_pvt;
	int res = 0;

	switch (cond) {
		case AST_CONTROL_BUSY:
		case AST_CONTROL_CONGESTION:
		case AST_CONTROL_RINGING:
			res = cond;
			break;

		case -1:
			return 0;

		case AST_CONTROL_VIDUPDATE:
			res = -1;
			break;
		case AST_CONTROL_HOLD:
			ast_verbose(" << Chan_pi Has Been Placed on Hold >> \n");
			ast_moh_start(c, data, o->mohinterpret);
			break;
		case AST_CONTROL_UNHOLD:
			ast_verbose(" << Chan_pi Has Been Retrieved from Hold >> \n");
			ast_moh_stop(c);
			break;
		case AST_CONTROL_PROCEEDING:
			ast_verbose(" << Call Proceeding... >> \n");
			ast_moh_stop(c);
			break;
		case AST_CONTROL_PROGRESS:
			ast_verbose(" << Call Progress... >> \n");
			ast_moh_stop(c);
			break;
		case AST_CONTROL_RADIO_KEY:
			o->txkeyed = 1;
			break;
		case AST_CONTROL_RADIO_UNKEY:
			o->txkeyed = 0;
			break;
		default:
			ast_log(LOG_WARNING, "Don't know how to display condition %d on %s\n", cond, c->name);
			return -1;
	}

	return res;
}

static int pi_setoption(struct ast_channel *chan, int option, void *data, int datalen)
{
	char *cp;
	struct chan_pi_pvt *o = chan->tech_pvt;

	/* all supported options require data */
	if (!data || (datalen < 1)) {
		errno = EINVAL;
		return -1;
	}

	switch (option) {
	case AST_OPTION_TONE_VERIFY:
		cp = (char *) data;
		switch (*cp) {
		case 1:
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: OFF(0) on %s\n",chan->name);
			o->usedtmf = 1;
			break;
		case 2:
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: MUTECONF/MAX(2) on %s\n",chan->name);
			o->usedtmf = 1;
			break;
		case 3:
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: DISABLE DETECT(3) on %s\n",chan->name);
			o->usedtmf = 0;
			break;
		default:
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: OFF(0) on %s\n",chan->name);
			o->usedtmf = 1;
			break;
		}
		break;
	}
	errno = 0;
	return 0;
}

/*
 * allocate a new channel.
 */
static struct ast_channel *pi_new(struct chan_pi_pvt *o, char *ext, char *ctx, int state)
{
	struct ast_channel *c;

	c = ast_channel_alloc(1, state, o->cid_num, o->cid_name, "", ext, ctx, 0, "Pi/%s", o->name);
	if (c == NULL)
		return NULL;
	c->tech = &pi_tech;

	if (o == &pvts[0])
	{
		if (pvts[1].owner) pvts[1].owner->fds[0] = (usedsp) ? readpipe[0] : -1;
		c->fds[0] = readdev;
	}
	else
	{
		if (!pvts[0].owner) 
		{
			c->fds[0] = readdev;
		}
		else
		{
			if (usedsp)
				c->fds[0] = readpipe[0];
		}
	}

	c->nativeformats = AST_FORMAT_SLINEAR;
	c->readformat = AST_FORMAT_SLINEAR;
	c->writeformat = AST_FORMAT_SLINEAR;
	c->tech_pvt = o;

	if (!ast_strlen_zero(o->language))
		ast_string_field_set(c, language, o->language);
	/* Don't use ast_set_callerid() here because it will
	 * generate a needless NewCallerID event */
	c->cid.cid_num = ast_strdup(o->cid_num);
	c->cid.cid_ani = ast_strdup(o->cid_num);
	c->cid.cid_name = ast_strdup(o->cid_name);
	if (!ast_strlen_zero(ext))
		c->cid.cid_dnid = ast_strdup(ext);

	o->owner = c;
	ast_module_ref(ast_module_info->self);
	ast_jb_configure(c, &global_jbconf);
	if (state != AST_STATE_DOWN) {
		if (ast_pbx_start(c)) {
			ast_log(LOG_WARNING, "Unable to start PBX on %s\n", c->name);
			ast_hangup(c);
			o->owner = c = NULL;
			/* XXX what about the channel itself ? */
			/* XXX what about usecnt ? */
		}
	}
	return c;
}
/*
*/
static struct ast_channel *pi_request(const char *type, int format, void *data, int *cause)
{
	struct ast_channel *c;
	struct chan_pi_pvt *o = find_pvt(data);

	if (o == NULL) {
		ast_log(LOG_NOTICE, "Device %s not found\n", (char *) data);
		/* XXX we could default to 'dsp' perhaps ? */
		return NULL;
	}
	if ((format & AST_FORMAT_SLINEAR) == 0) {
		ast_log(LOG_NOTICE, "Format 0x%x unsupported\n", format);
		return NULL;
	}
	if (o->owner) {
		ast_log(LOG_NOTICE, "Already have a call (chan %p) on the pi channel\n", o->owner);
		*cause = AST_CAUSE_BUSY;
		return NULL;
	}
	c = pi_new(o, NULL, NULL, AST_STATE_DOWN);
	if (c == NULL) {
		ast_log(LOG_WARNING, "Unable to create new pi channel\n");
		return NULL;
	}
	if (!has_been_open)
	{
		snd_pcm_prepare(alsa.icard);
		snd_pcm_start(alsa.icard);
		has_been_open = 1;
	}
	o->starttime = ast_tvnow();
	return c;
}
/*
*/
static int pi_key(int fd, int argc, char *argv[])
{
	struct chan_pi_pvt *o = find_pvt(pi_active);
	if (argc != 2)
		return RESULT_SHOWUSAGE; 
	o->txtestkey = 1;
	return RESULT_SUCCESS;
}
/*
*/
static int pi_unkey(int fd, int argc, char *argv[])
{
	struct chan_pi_pvt *o = find_pvt(pi_active);
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	o->txtestkey = 0;
	return RESULT_SUCCESS;
}

/*
	Adjust Input Attenuator with maximum signal input
*/
static void tune_rxinput(int fd, struct chan_pi_pvt *o, int setsql, int intflag)
{
	const int settingmin=1;
	const int settingstart=2;
	const int maxtries=12;

	int target;
	int tolerance=2750;
	int setting=0, tries=0, tmpdiscfactor, meas, measnoise;
	float settingmax,f;

	if (!usedsp)
	{
		ast_cli(fd,onlyindsp);
		return;
	}
	if(o->rxdemod==RX_AUDIO_SPEAKER && o->rxcdtype==CD_XPMR_NOISE)
	{
		ast_cli(fd,"ERROR: pi.conf rxdemod=speaker vs. carrierfrom=dsp \n");	
	}

	if( o->rxdemod==RX_AUDIO_FLAT )
		target=27000;
	else
		target=23000;

	settingmax = MICMAX;

	o->pmrChan->b.tuning=1;

	setting = settingstart;

	ast_cli(fd,"tune rxnoise maxtries=%i, target=%i, tolerance=%i\n",maxtries,target,tolerance);

	while(tries<maxtries)
	{
		if (o == &pvts[0])
		{
			setamixer(0,"IN3L Volume",setting,-1);
		        setamixer(0,"IN3L Digital Volume",pvts[0].rxboost ? 142 : 102,-1);
		}
		else
		{
			setamixer(0,"IN3R Volume",setting,-1);
		        setamixer(0,"IN3R Digital Volume",pvts[1].rxboost ? 142 : 102,-1);
		}
		if (happy_mswait(fd,100,intflag)) 
		{
			o->pmrChan->b.tuning=0;
			return;
		}
		o->pmrChan->spsMeasure->source = o->pmrChan->spsRx->source;
		o->pmrChan->spsMeasure->discfactor=2000;
		o->pmrChan->spsMeasure->enabled=1;
		o->pmrChan->spsMeasure->amax = o->pmrChan->spsMeasure->amin = 0;
		if (happy_mswait(fd,400,intflag))
		{
			o->pmrChan->b.tuning=0;
			return;
		}
		meas=o->pmrChan->spsMeasure->apeak;
		o->pmrChan->spsMeasure->enabled=0;

		if(!meas)meas++;
		ast_cli(fd,"tries=%i, setting=%i, meas=%i\n",tries,setting,meas);

		if( (meas<(target-tolerance) || meas>(target+tolerance)) && tries<=2){
			f=(float)(setting*target)/meas;
			setting=(int)(f+0.5);
		}
		else if( meas<(target-tolerance) && tries>2){
			setting++;
		}
		else if( meas>(target+tolerance) && tries>2){
			setting--;
		}
		else if(tries>5 && meas>(target-tolerance) && meas<(target+tolerance) )
		{
			break;
		}

		if(setting<settingmin)setting=settingmin;
		else if(setting>settingmax)setting=settingmax;

		tries++;
	}


	/* Measure HF Noise */
	tmpdiscfactor=o->pmrChan->spsRx->discfactor;
	o->pmrChan->spsRx->discfactor=(i16)2000;
	o->pmrChan->spsRx->discounteru=o->pmrChan->spsRx->discounterl=0;
	o->pmrChan->spsRx->amax=o->pmrChan->spsRx->amin=0;
	if (happy_mswait(fd,200,intflag))
	{
		o->pmrChan->b.tuning=0;
		return;
	}
	measnoise=o->pmrChan->rxRssi;

	/* Measure RSSI */
	o->pmrChan->spsRx->discfactor=tmpdiscfactor;
	o->pmrChan->spsRx->discounteru=o->pmrChan->spsRx->discounterl=0;
	o->pmrChan->spsRx->amax=o->pmrChan->spsRx->amin=0;
	if (happy_mswait(fd,200,intflag))
	{
		o->pmrChan->b.tuning=0;
		return;
	}

	ast_cli(fd,"DONE tries=%i, setting=%i, meas=%i, sqnoise=%i\n",tries,
		((setting * 1000) + (MICMAX / 2)) / MICMAX,meas,measnoise);

	if( meas<(target-tolerance) || meas>(target+tolerance) ){
		ast_cli(fd,"ERROR: RX INPUT ADJUST FAILED.\n");
	}else{
		ast_cli(fd,"INFO: RX INPUT ADJUST SUCCESS.\n");
		o->rxmixerset=((setting * 1000) + (MICMAX / 2)) / MICMAX;

		if(o->rxcdtype==CD_XPMR_NOISE)
		{
			int normRssi=((32767-o->pmrChan->rxRssi)*1000/32767);

			if((meas/(measnoise/10))>26){
				ast_cli(fd,"WARNING: Insufficient high frequency noise from receiver.\n");
				ast_cli(fd,"WARNING: Rx input point may be de-emphasized and not flat.\n");
				ast_cli(fd,"         pi.conf setting of 'carrierfrom=dsp' not recommended.\n");
			}
			else
			{
				ast_cli(fd,"Rx noise input seems sufficient for squelch.\n");	
			}
			if (setsql)
			{
				o->rxsquelchadj = normRssi + 150;
				if (o->rxsquelchadj > 999) o->rxsquelchadj = 999;
				*(o->pmrChan->prxSquelchAdjust)= ((999 - o->rxsquelchadj) * 32767) / 1000;
				ast_cli(fd,"Rx Squelch set to %d (RSSI=%d).\n",o->rxsquelchadj,normRssi);
			}
			else 
			{
				if(o->rxsquelchadj<normRssi)
				{
					ast_cli(fd,"WARNING: RSSI=%i SQUELCH=%i and is set too loose.\n",normRssi,o->rxsquelchadj);
					ast_cli(fd,"         Use 'radio tune rxsquelch' to adjust.\n");
				}
			}
		}
	}
	o->pmrChan->b.tuning=0;
}

static void tune_rxvoice(int fd, struct chan_pi_pvt *o,int intflag)
{
	const int target=7200;	 			// peak
	const int tolerance=360;	   		// peak to peak
	const float settingmin=0.1;
	const float settingmax=5;
	const float settingstart=1;
	const int maxtries=12;

	float setting;

	int tries=0, meas;

	if (!usedsp)
	{
		ast_cli(fd,onlyindsp);
		return;
	}
	ast_cli(fd,"INFO: RX VOICE ADJUST START.\n");
	ast_cli(fd,"target=%i tolerance=%i \n",target,tolerance);

	o->pmrChan->b.tuning=1;
	if(!o->pmrChan->spsMeasure)
		ast_cli(fd,"ERROR: NO MEASURE BLOCK.\n");

	if(!o->pmrChan->spsMeasure->source || !o->pmrChan->prxVoiceAdjust )
		ast_cli(fd,"ERROR: NO SOURCE OR MEASURE SETTING.\n");

	o->pmrChan->spsMeasure->source=o->pmrChan->spsRxOut->sink;
	o->pmrChan->spsMeasure->enabled=1;
	o->pmrChan->spsMeasure->discfactor=1000;

	setting=settingstart;

	// ast_cli(fd,"ERROR: NO MEASURE BLOCK.\n");

	while(tries<maxtries)
	{
		*(o->pmrChan->prxVoiceAdjust)=setting*M_Q8;
		if (happy_mswait(fd,10,intflag))
		{
			o->pmrChan->b.tuning=0;
			return;
		}
    	o->pmrChan->spsMeasure->amax = o->pmrChan->spsMeasure->amin = 0;
		if (happy_mswait(fd,1000,intflag))
		{
			o->pmrChan->b.tuning=0;
			return;
		}
		meas = o->pmrChan->spsMeasure->apeak;
		ast_cli(fd,"tries=%i, setting=%f, meas=%i\n",tries,setting,meas);

		if( meas<(target-tolerance) || meas>(target+tolerance) || tries<3){
			setting=setting*target/meas;
		}
		else if(tries>4 && meas>(target-tolerance) && meas<(target+tolerance) )
		{
			break;
		}
		if(setting<settingmin)setting=settingmin;
		else if(setting>settingmax)setting=settingmax;

		tries++;
	}

	o->pmrChan->spsMeasure->enabled=0;

	ast_cli(fd,"DONE tries=%i, setting=%f, meas=%f\n",tries,setting,(float)meas);
	if( meas<(target-tolerance) || meas>(target+tolerance) ){
		ast_cli(fd,"ERROR: RX VOICE GAIN ADJUST FAILED.\n");
	}else{
		ast_cli(fd,"INFO: RX VOICE GAIN ADJUST SUCCESS.\n");
		o->rxvoiceadj=setting;
	}
	o->pmrChan->b.tuning=0;
}
/*
*/
static void tune_rxctcss(int fd, struct chan_pi_pvt *o,int intflag)
{
	const int target=2400;		 // was 4096 pre 20080205
	const int tolerance=100;
	const float settingmin=0.1;
	const float settingmax=8;
	const float settingstart=1;
	const int maxtries=12;

	float setting;
	int tries=0,meas;

	if (!usedsp)
	{
		ast_cli(fd,onlyindsp);
		return;
	}
	ast_cli(fd,"INFO: RX CTCSS ADJUST START.\n");
	ast_cli(fd,"target=%i tolerance=%i \n",target,tolerance);

	o->pmrChan->b.tuning=1;
	o->pmrChan->spsMeasure->source=o->pmrChan->prxCtcssMeasure;
	o->pmrChan->spsMeasure->discfactor=400;
	o->pmrChan->spsMeasure->enabled=1;

	setting=settingstart;

	while(tries<maxtries)
	{
		*(o->pmrChan->prxCtcssAdjust)=setting*M_Q8;
		if (happy_mswait(fd,10,intflag))
		{
			o->pmrChan->b.tuning=0;
			return;
		}
    	o->pmrChan->spsMeasure->amax = o->pmrChan->spsMeasure->amin = 0;
		if (happy_mswait(fd,500,intflag))
		{
			o->pmrChan->b.tuning=0;
			return;
		}
		meas = o->pmrChan->spsMeasure->apeak;
		ast_cli(fd,"tries=%i, setting=%f, meas=%i\n",tries,setting,meas);

		if( meas<(target-tolerance) || meas>(target+tolerance) || tries<3){
			setting=setting*target/meas;
		}
		else if(tries>4 && meas>(target-tolerance) && meas<(target+tolerance) )
		{
			break;
		}
		if(setting<settingmin)setting=settingmin;
		else if(setting>settingmax)setting=settingmax;

		tries++;
	}
	o->pmrChan->spsMeasure->enabled=0;
	ast_cli(fd,"DONE tries=%i, setting=%f, meas=%.2f\n",tries,setting,(float)meas);
	if( meas<(target-tolerance) || meas>(target+tolerance) ){
		ast_cli(fd,"ERROR: RX CTCSS GAIN ADJUST FAILED.\n");
	}else{
		ast_cli(fd,"INFO: RX CTCSS GAIN ADJUST SUCCESS.\n");
		o->rxctcssadj=setting;
	}

	if(o->rxcdtype==CD_XPMR_NOISE){
		int normRssi;

		if (happy_mswait(fd,200,intflag))
		{
			o->pmrChan->b.tuning=0;
			return;
		}
		normRssi=((32767-o->pmrChan->rxRssi)*1000/32767);

		if(o->rxsquelchadj>normRssi)
			ast_cli(fd,"WARNING: RSSI=%i SQUELCH=%i and is too tight. Use 'radio tune rxsquelch'.\n",normRssi,o->rxsquelchadj);
		else
			ast_cli(fd,"INFO: RX RSSI=%i\n",normRssi);

	}
	o->pmrChan->b.tuning=0;
}

static void tune_rxdisplay(int fd, struct chan_pi_pvt *o)
{
	int j,waskeyed,meas,ncols = 75,wasverbose;
	char str[256];

	for(j = 0; j < ncols; j++) str[j] = ' ';
	str[j] = 0;
	ast_cli(fd," %s \r",str);
	ast_cli(fd,"RX VOICE DISPLAY:\n");
	ast_cli(fd,"                                 v -- 3KHz        v -- 5KHz\n");

	o->measure_enabled = 1;
	o->discfactor = 1000;
	o->discounterl = o->discounteru = 0;
	wasverbose = option_verbose;
	option_verbose = 0;

	waskeyed = !o->rxkeyed;
	for(;;)
	{
		o->amax = o->amin = 0;
		if (rad_rxwait(fd,100)) break;
		if (o->rxkeyed != waskeyed)
		{
			for(j = 0; j < ncols; j++) str[j] = ' ';
			str[j] = 0;
			ast_cli(fd," %s \r",str);
		}
		waskeyed = o->rxkeyed;
		if (!o->rxkeyed) {
			ast_cli(fd,"\r");
			continue;
		}
		meas = o->apeak;
		for(j = 0; j < ncols; j++)
		{
			int thresh = (meas * ncols) / 16384;
			if (j < thresh) str[j] = '=';
			else if (j == thresh) str[j] = '>';
			else str[j] = ' ';
		}
		str[j] = 0;
		ast_cli(fd,"|%s|\r",str);
	}
	o->measure_enabled = 0;
	option_verbose = wasverbose;
}

static void set_txctcss_level(struct chan_pi_pvt *o)
{							  
	if(o->pmrChan->ptxCtcssAdjust) /* Ignore if ptr not defined */
		*o->pmrChan->ptxCtcssAdjust=(o->txctcssadj * M_Q8) / 1000;
}

static int _send_tx_test_tone(int fd, struct chan_pi_pvt *o,int ms, int intflag)
{
int	i,ret;

	ast_tonepair_stop(o->owner);
        if (ast_tonepair_start(o->owner, 1004.0, 0, 99999999, 7200.0))
	{
		if (fd >= 0) ast_cli(fd,"Error starting test tone on %s!!\n",pi_active);
		return -1;
	}
	ast_clear_flag(o->owner, AST_FLAG_WRITE_INT);
	o->txtestkey = 1;	
	i = 0;
	ret = 0;
	ast_cli(fd,"Tone output starting on channel %s...\n",o->name);
        while(o->owner->generatordata && (i < ms)) 
	{
		if (happy_mswait(fd,50,intflag)) 
		{
			ret = 1;
			break;
		}
		i += 50;
	}
	if (fd > 0) ast_cli(fd,"Tone output ending on channel %s...\n",o->name);
	ast_tonepair_stop(o->owner);
 	ast_set_flag(o->owner, AST_FLAG_WRITE_INT);
	o->txtestkey = 0;	
	return ret;
}

static void tune_txoutput(struct chan_pi_pvt *o, int value, int fd, int intflag)
{
	if (!o->pmrChan)
	{
		_send_tx_test_tone(fd,o,5000,1);
		return;
	}
	o->txtestkey=1;
	o->pmrChan->txPttIn=1;
	TxTestTone(o->pmrChan, 1);	  // generate 1KHz tone at 7200 peak
	if (fd > 0)
	{
		ast_cli(fd,"Tone output starting on channel %s...\n",o->name);
		if (happy_mswait(fd,5000,intflag))
		{
			o->pmrChan->txPttIn=0;
			o->txtestkey=0;
		}
	} else usleep(5000000);
	TxTestTone(o->pmrChan, 0);
	if (fd > 0) ast_cli(fd,"Tone output ending on channel %s...\n",o->name);
	o->pmrChan->txPttIn=0;
	o->txtestkey=0;
}

/*
	Adjust Input Attenuator with maximum signal input
*/

static void _menu_rxvoice(int fd, struct chan_pi_pvt *o, char *str)
{
	int i,x;

	if (!str[0])
	{
		if (o->rxdemod == RX_AUDIO_FLAT)
			ast_cli(fd,"Current Rx voice setting: %d\n",(int)((o->rxvoiceadj * 200.0) + .5));
		else
			ast_cli(fd,"Current Rx voice setting: %d\n",o->rxmixerset);
		return;
	}
	for(x = 0; str[x]; x++)
	{
		if (!isdigit(str[x])) break;
	}
	if (str[x] || (sscanf(str,"%d",&i) < 1) || (i < 0) || (i > 999))
	{
		ast_cli(fd,"Entry Error, Rx voice setting not changed\n");
		return;
	}
	if (usedsp && (o->rxdemod == RX_AUDIO_FLAT))
	{
	 	o->rxvoiceadj=(float)i / 200.0;
	}
	else
	{
		o->rxmixerset = i;
		mixer_write();
	}
	if (usedsp && o->pmrChan)
		*(o->pmrChan->prxVoiceAdjust)=o->rxvoiceadj * M_Q8;
	ast_cli(fd,"Changed rx voice setting to %d\n",i);
	return;
}

static void _menu_print(int fd, struct chan_pi_pvt *o)
{
	ast_cli(fd,"Active Pi radio interface is [%s]\n",pi_active);
	ast_cli(fd,"Output is currently set to ");
	if(o->txmix==TX_OUT_COMPOSITE)ast_cli(fd,"composite.\n");
	else if (o->txmix==TX_OUT_VOICE)ast_cli(fd,"voice.\n");
	else if (o->txmix==TX_OUT_LSD)ast_cli(fd,"tone.\n");
	else ast_cli(fd,"off.\n");

	ast_cli(fd,"Tx Voice Level currently set to %d\n",o->txmixerset);
	ast_cli(fd,"Tx Tone Level currently set to %d\n",o->txctcssadj);
	ast_cli(fd,"Rx Squelch currently set to %d\n",o->rxsquelchadj);
	return;
}

static void _menu_rxsquelch(int fd, struct chan_pi_pvt *o, char *str)
{
int	i,x;

	if (!usedsp)
	{
		ast_cli(fd,"Must have DSP enabled to use this function!!\n");
		return;
	}
	if (!str[0])
	{
		ast_cli(fd,"Current Signal Strength is %d\n",((32767-o->pmrChan->rxRssi)*1000/32767));
		ast_cli(fd,"Current Squelch setting is %d\n",o->rxsquelchadj);
		return;
	}
	for(x = 0; str[x]; x++)
	{
		if (!isdigit(str[x])) break;
	}
	if (str[x] || (sscanf(str,"%d",&i) < 1) || (i < 0) || (i > 999))
	{
		ast_cli(fd,"Entry Error, Rx Squelch Level setting not changed\n");
		return;
	}
	ast_cli(fd,"Changed Rx Squelch Level setting to %d\n",i);
	o->rxsquelchadj = i;
	*(o->pmrChan->prxSquelchAdjust)= ((999 - i) * 32767) / 1000;
	return;
}

static void _menu_txvoice(int fd, struct chan_pi_pvt *o,char *cstr)
{
char	*str = cstr;
int	i,j,x,dokey,withctcss;

	if (!str[0])
	{
		ast_cli(fd,"Current Tx Voice Level setting on Channel A is %d\n",o->txmixerset);
		return;
	}
	j = o->txmixerset;
	dokey = 0;
	if (str[0] == 'K')
	{
		dokey = 1;
		str++;
	}
	withctcss = 0;
	if (str[0] == 'C')
	{
		withctcss = 1;
		str++;
	}
	if (!str[0])
	{
		ast_cli(fd,"Keying Transmitter and sending 1000 Hz tone for 5 seconds...\n");
		if (o->pmrChan && withctcss) o->pmrChan->b.txCtcssInhibit=1;
		tune_txoutput(o,j,fd,1);
		if (o->pmrChan) o->pmrChan->b.txCtcssInhibit=0;
		ast_cli(fd,"DONE.\n");
		return;
	}
	for(x = 0; str[x]; x++)
	{
		if (!isdigit(str[x])) break;
	}
	if (str[x] || (sscanf(str,"%d",&i) < 1) || (i < 0) || (i > 999))
	{
		ast_cli(fd,"Entry Error, Tx Voice Level setting not changed\n");
		return;
	}
 	o->txmixerset=i;
	ast_cli(fd,"Changed Tx Voice Level setting on Channel A to %d\n",o->txmixerset);
	mixer_write();
	if (dokey)
	{
		ast_cli(fd,"Keying Transmitter and sending 1000 Hz tone for 5 seconds...\n");
		if (o->pmrChan && (!withctcss)) o->pmrChan->b.txCtcssInhibit=1;
		tune_txoutput(o,i,fd,1);
		if (o->pmrChan) o->pmrChan->b.txCtcssInhibit=0;
		ast_cli(fd,"DONE.\n");
	}
	return;
}


static void _menu_txtone(int fd, struct chan_pi_pvt *o, char *cstr)
{
char	*str = cstr;
int	i,x,dokey;

	if (!usedsp)
	{
		ast_cli(fd,"Must have DSP enabled to use this function!!\n");
		return;
	}
	if (!str[0])
	{
		ast_cli(fd,"Current Tx CTCSS Modulation Level setting = %d\n",o->txctcssadj);
		return;
	}
	dokey = 0;
	if (str[0] == 'K')
	{
		dokey = 1;
		str++;
	}
	if (str[0])
	{
		for(x = 0; str[x]; x++)
		{
			if (!isdigit(str[x])) break;
		}
		if (str[x] || (sscanf(str,"%d",&i) < 1) || (i < 0) || (i > 999))
		{
			ast_cli(fd,"Entry Error, Tx CTCSS Modulation Level setting not changed\n");
			return;
		}
		o->txctcssadj = i;
		set_txctcss_level(o);
		ast_cli(fd,"Changed Tx CTCSS Modulation Level setting to %i\n",i);
	}
	if (dokey)
	{
		ast_cli(fd,"Keying Radio and sending CTCSS tone for 5 seconds...\n");
		o->txtestkey=1;
		happy_mswait(fd,5000,1);
		o->txtestkey=0;
		ast_cli(fd,"DONE.\n");
	}
	return;
}

static void tune_menusupport(int fd, struct chan_pi_pvt *o, char *cmd)
{
int	x,oldverbose,flatrx,txhasctcss;

	oldverbose = option_verbose;
	option_verbose = 0;
	flatrx = 0;
	if (o->rxdemod == RX_AUDIO_FLAT) flatrx = 1;
	txhasctcss = 0;
	if ((o->txmix == TX_OUT_LSD) || (o->txmix == TX_OUT_COMPOSITE)) 
		txhasctcss = 1;
	switch(cmd[0])
	{
	    case '0': /* return audio processing configuration */
		ast_cli(fd,"%d,%d,%d\n",flatrx,txhasctcss,0);
		break;
	    case '1': /* return pi device name list */
		for(x = 0; x < 2; x++)
		{
			if (x) ast_cli(fd,",");
			if (pvts[x].name)
				ast_cli(fd,"%s",pvts[x].name);
		}
		ast_cli(fd,"\n");
		break;
	    case '2': /* print parameters */
		_menu_print(fd,o);
		break;
	    case 'a':
		tune_rxinput(fd,o,1,1);
		break;
	    case 'b':
		tune_rxdisplay(fd,o);
		break;
	    case 'c':
		_menu_rxvoice(fd, o, cmd + 1);
		break;
	    case 'd':
		tune_rxctcss(fd,o,1);
		break;
	    case 'e':
		_menu_rxsquelch(fd,o,cmd + 1);
		break;
	    case 'f':
		_menu_txvoice(fd,o,cmd + 1);
		break;
	    case 'h':
		_menu_txtone(fd,o,cmd + 1);
		break;
	    case 'i':
		tune_rxvoice(fd,o,1);
		break;
	    case 'j':
		tune_write(o);
		ast_cli(fd,"Saved radio tuning settings to pi_tune_%s.conf\n",o->name);
		break;
	    default:
		ast_cli(fd,"Invalid Command\n");
		break;
	}
	if (o->pmrChan)
		o->pmrChan->b.tuning=0;
	option_verbose = oldverbose;
	return;
}


static int pi_tune(int fd, int argc, char *argv[])
{
	struct chan_pi_pvt *o = find_pvt(pi_active);
	int i=0;

	if ((argc < 2) || (argc > 4))
		return RESULT_SHOWUSAGE; 

	if (argc == 2) /* just show stuff */
	{
		ast_cli(fd,"Active radio interface is [%s]\n",pi_active);
		ast_cli(fd,"Rx Level currently set to %d\n",o->rxmixerset);
		ast_cli(fd,"Tx Output Level currently set to %d\n",o->txmixerset);
		return RESULT_SHOWUSAGE;
	}

	if (!strcasecmp(argv[2],"menu-support"))
	{
		if (argc > 3) tune_menusupport(fd,o,argv[3]);
		return RESULT_SUCCESS;
	}

	else if (!strcasecmp(argv[2],"rxnoise")) tune_rxinput(fd,o,1,0);
	else if (!strcasecmp(argv[2],"rxvoice")) tune_rxvoice(fd,o,0);
	else if (!strcasecmp(argv[2],"rxtone")) tune_rxctcss(fd,o,0);
	else if (!strcasecmp(argv[2],"rxsquelch"))
	{
		if (argc == 3)
		{
		    ast_cli(fd,"Current Signal Strength is %d\n",((32767-o->pmrChan->rxRssi)*1000/32767));
		    ast_cli(fd,"Current Squelch setting is %d\n",o->rxsquelchadj);
			//ast_cli(fd,"Current Raw RSSI        is %d\n",o->pmrChan->rxRssi);
		    //ast_cli(fd,"Current (real) Squelch setting is %d\n",*(o->pmrChan->prxSquelchAdjust));
		} else {
			i = atoi(argv[3]);
			if ((i < 0) || (i > 999)) return RESULT_SHOWUSAGE;
			ast_cli(fd,"Changed Squelch setting to %d\n",i);
			o->rxsquelchadj = i;
			*(o->pmrChan->prxSquelchAdjust)= ((999 - i) * 32767) / 1000;
		}
	}
	if (!strcasecmp(argv[2],"rx")) {
		i = 0;

		if (usedsp)
		{
			ast_cli(fd,notindsp);
			return RESULT_FAILURE;
		}
		if (argc == 3)
		{
			ast_cli(fd,"Current setting on Rx Channel is %d\n",o->rxmixerset);
		}
		else
		{
			i = atoi(argv[3]);
			if ((i < 0) || (i > 999)) return RESULT_SHOWUSAGE;
		 	o->rxmixerset = i;
			ast_cli(fd,"Changed setting on RX Channel to %d\n",o->rxmixerset);
			mixer_write();
		}
	}
	else if (!strncasecmp(argv[2],"rxd",3)) {
		tune_rxdisplay(fd,o);
	}
	else if (!strcasecmp(argv[2],"txvoice")) {
		i = 0;

		if( (o->txmix!=TX_OUT_VOICE) && (o->txmix!=TX_OUT_COMPOSITE))
		{
			ast_log(LOG_ERROR,"No txvoice output configured.\n");
		}
		else if (argc == 3)
		{
			if((o->txmix==TX_OUT_VOICE)||(o->txmix==TX_OUT_COMPOSITE))
				ast_cli(fd,"Current txvoice setting on Channel A is %d\n",o->txmixerset);
		}
		else
		{
			i = atoi(argv[3]);
			if ((i < 0) || (i > 999)) return RESULT_SHOWUSAGE;

			if((o->txmix==TX_OUT_VOICE)||(o->txmix==TX_OUT_COMPOSITE))
			{
			 	o->txmixerset=i;
				ast_cli(fd,"Changed txvoice setting on Channel A to %d\n",o->txmixerset);
			}
			mixer_write();
			ast_cli(fd,"Changed Tx Voice Output setting to %d\n",i);
		}
		o->pmrChan->b.txCtcssInhibit=1;
		tune_txoutput(o,i,fd,0);
		o->pmrChan->b.txCtcssInhibit=0;
	}
	else if (!strcasecmp(argv[2],"tx")) {
		i = 0;

		if (argc == 3)
		{
			ast_cli(fd,"Current setting on Tx Channel is %d\n",o->txmixerset);
		}
		else
		{
			i = atoi(argv[3]);
			if ((i < 0) || (i > 999)) return RESULT_SHOWUSAGE;
		 	o->txmixerset = i;
			ast_cli(fd,"Changed setting on TX Channel to %d\n",o->txmixerset);
			mixer_write();
		}
	}
	else if (!strcasecmp(argv[2],"txtone"))
	{
		if (!usedsp)
		{
			ast_cli(fd,onlyindsp);
			return RESULT_FAILURE;
		}
		if (argc == 3)
			ast_cli(fd,"Current Tx CTCSS modulation setting = %d\n",o->txctcssadj);
		else
		{
			i = atoi(argv[3]);
			if ((i < 0) || (i > 999)) return RESULT_SHOWUSAGE;
			o->txctcssadj = i;
			set_txctcss_level(o);
			ast_cli(fd,"Changed Tx CTCSS modulation setting to %i\n",i);
		}
		o->txtestkey=1;
		usleep(5000000);
		o->txtestkey=0;
	}
	else if (!strcasecmp(argv[2],"nocap")) 	
	{
		ast_cli(fd,"cap off.\n");
		if (frxcapraw) { fclose(frxcapraw); frxcapraw = NULL; }
		if (ftxcapraw) { fclose(ftxcapraw); ftxcapraw = NULL; }
	}
	else if (!strcasecmp(argv[2],"rxcap")) 
	{
		if (!frxcapraw) frxcapraw = fopen(RX_CAP_RAW_FILE,"w");
		ast_cli(fd,"cap rx raw on.\n");
	}
	else if (!strcasecmp(argv[2],"txcap")) 
	{
		if (!ftxcapraw) ftxcapraw = fopen(TX_CAP_RAW_FILE,"w");
		ast_cli(fd,"cap tx raw on.\n");
	}
	else if (!strcasecmp(argv[2],"save"))
	{
		tune_write(o);
		ast_cli(fd,"Saved radio tuning settings to pi_tune_%s.conf\n",o->name);
	}
	else
	{
		return RESULT_SHOWUSAGE;
	}
	return RESULT_SUCCESS;
}

/*
	CLI debugging on and off
*/
static int pi_set_debug(int fd, int argc, char *argv[])
{
	struct chan_pi_pvt *o = find_pvt(pi_active);
	o->debuglevel=1;
	ast_cli(fd,"pi debug on.\n");
	return RESULT_SUCCESS;
}

static int pi_set_debug_off(int fd, int argc, char *argv[])
{
	struct chan_pi_pvt *o = find_pvt(pi_active);
	o->debuglevel=0;
	ast_cli(fd,"pi debug off.\n");
	return RESULT_SUCCESS;
}

static int pi_active_cmd(int fd, int argc, char *argv[])
{
        if (argc == 2)
                ast_cli(fd, "active (command) pi device is [%s]\n", pi_active);
        else if (argc != 3)
                return RESULT_SHOWUSAGE;
        else {
                struct chan_pi_pvt *o;
                o = find_pvt(argv[2]);
                if (o == NULL)
                        ast_cli(fd, "No device [%s] exists\n", argv[2]);
                else
		{
                    pi_active = o->name;
		}
        }
        return RESULT_SUCCESS;
}

static char key_usage[] =
	"Usage: pi key\n"
	"       Simulates COR active.\n";

static char unkey_usage[] =
	"Usage: pi unkey\n"
	"       Simulates COR un-active.\n";

static char active_usage[] =
        "Usage: pi active [device-name]\n"
        "       If used without a parameter, displays which device is the current\n"
        "one being commanded.  If a device is specified, the commanded radio device is changed\n"
        "to the device specified.\n";

static char pi_tune_usage[] =
	"Usage: pi tune <function>\n"
	"  With DSP NOT enabled:\n"
	"       rxdisplay\n"
	"       rx [newsetting]\n"
	"       tx [newsetting]\n"
	"       save (settings to tuning file)\n"
	"  With DSP Enabled:\n"
	"       rxdisplay\n"
	"       rxnoise\n"
	"       rxvoice\n"
	"       rxtone\n"
	"       rxsquelch [newsetting]\n"
	"       txvoice [newsetting]\n"
	"       txtone [newsetting]\n"
	"       tx [newsetting]\n"
	"       save (settings to tuning file)\n"
	"\n       All [newsetting]'s are values 0-999\n\n";
					  
#ifndef	NEW_ASTERISK

static struct ast_cli_entry cli_pi[] = {
	{ { "pi", "key", NULL },
	pi_key, "Simulate Rx Signal Present",
	key_usage, NULL, NULL},

	{ { "pi", "unkey", NULL },
	pi_unkey, "Simulate Rx Signal Loss",
	unkey_usage, NULL, NULL },

	{ { "pi", "tune", NULL },
	pi_tune, "Radio Tune",
	pi_tune_usage, NULL, NULL },

	{ { "pi", "set", "debug", NULL },
	pi_set_debug, "Radio Debug",
	pi_tune_usage, NULL, NULL },

	{ { "pi", "set", "debug", "off", NULL },
	pi_set_debug_off, "Radio Debug",
	pi_tune_usage, NULL, NULL },

	{ { "pi", "active", NULL },
	pi_active_cmd, "Change commanded device",
	active_usage, NULL, NULL },

};
#endif

static void store_rxcdtype(struct chan_pi_pvt *o, char *s)
{
	if (!strcasecmp(s,"no")){
		o->rxcdtype = CD_IGNORE;
	}
	else if (!strcasecmp(s,"hw")){
		o->rxcdtype = CD_HID;
	}
	else if (!strcasecmp(s,"hwinvert")){
		o->rxcdtype = CD_HID_INVERT;
	}	
	else if (!strcasecmp(s,"dsp")){
		o->rxcdtype = CD_XPMR_NOISE;
	}	
	else {
		ast_log(LOG_WARNING,"Unrecognized rxcdtype parameter: %s\n",s);
	}

	//ast_log(LOG_WARNING, "set rxcdtype = %s\n", s);
}
/*
*/
static void store_rxsdtype(struct chan_pi_pvt *o, char *s)
{
	if (!strcasecmp(s,"no") || !strcasecmp(s,"SD_IGNORE")){
		o->rxsdtype = SD_IGNORE;
	}
	else if (!strcasecmp(s,"hw") || !strcasecmp(s,"SD_HID")){
		o->rxsdtype = SD_HID;
	}
	else if (!strcasecmp(s,"hwinvert") || !strcasecmp(s,"SD_HID_INVERT")){
		o->rxsdtype = SD_HID_INVERT;
	}	
	else if (!strcasecmp(s,"dsp")){
		o->rxsdtype = SD_XPMR;
	}	
	else {
		ast_log(LOG_WARNING,"Unrecognized rxsdtype parameter: %s\n",s);
	}

	//ast_log(LOG_WARNING, "set rxsdtype = %s\n", s);
}

static void store_rxdemod(struct chan_pi_pvt *o, char *s)
{
	if (!strcasecmp(s,"no")){
		o->rxdemod = RX_AUDIO_NONE;
	}
	else if (!strcasecmp(s,"speaker")){
		o->rxdemod = RX_AUDIO_SPEAKER;
	}
	else if (!strcasecmp(s,"flat")){
			o->rxdemod = RX_AUDIO_FLAT;
	}	
	else {
		ast_log(LOG_WARNING,"Unrecognized rxdemod parameter: %s\n",s);
	}

	//ast_log(LOG_WARNING, "set rxdemod = %s\n", s);
}

static void store_txtoctype(struct chan_pi_pvt *o, char *s)
{
	if (!strcasecmp(s,"no") || !strcasecmp(s,"TOC_NONE")){
		o->txtoctype = TOC_NONE;
	}
	else if (!strcasecmp(s,"phase") || !strcasecmp(s,"TOC_PHASE")){
		o->txtoctype = TOC_PHASE;
	}
	else if (!strcasecmp(s,"notone") || !strcasecmp(s,"TOC_NOTONE")){
		o->txtoctype = TOC_NOTONE;
	}	
	else {
		ast_log(LOG_WARNING,"Unrecognized txtoctype parameter: %s\n",s);
	}
}

static void store_txmix(struct chan_pi_pvt *o, char *s)
{
	if (!strcasecmp(s,"voice")){
		o->txmix = TX_OUT_VOICE;
	}
	else if (!strcasecmp(s,"composite")){
		o->txmix = TX_OUT_COMPOSITE;
	}	
	else {
		ast_log(LOG_WARNING,"Unrecognized txmix parameter: %s\n",s);
	}
}

static void store_rxvoiceadj(struct chan_pi_pvt *o, char *s)
{
	float f;
	sscanf(s,"%f",&f);
	o->rxvoiceadj = f;
}

static void store_rxctcssadj(struct chan_pi_pvt *o, char *s)
{
	float f;
	sscanf(s,"%f",&f);
	o->rxctcssadj = f;
}

static void tune_write(struct chan_pi_pvt *o)
{
	FILE *fp;
	char fname[200];

 	snprintf(fname,sizeof(fname) - 1,"/etc/asterisk/pi_tune_%s.conf",o->name);
	fp = fopen(fname,"w");

	fprintf(fp,"[%s]\n",o->name);

	fprintf(fp,"; name=%s\n",o->name);
	fprintf(fp,"rxmixerset=%i\n",o->rxmixerset);
	fprintf(fp,"txmixerset=%i\n",o->txmixerset);
	fprintf(fp,"rxvoiceadj=%f\n",o->rxvoiceadj);
	fprintf(fp,"rxctcssadj=%f\n",o->rxctcssadj);
	fprintf(fp,"txctcssadj=%i\n",o->txctcssadj);
	fprintf(fp,"rxsquelchadj=%i\n",o->rxsquelchadj);
	fclose(fp);
}

static int mixer_write(void)
{
int	v1,v2;
float f,f1;


        if (setamixer(0,"HPOUT2L Input 1",0,-1) == -1) return -1;
        if (setamixer(0,"HPOUT2R Input 1",0,-1) == -1) return -1;
        if (setamixer(0,"HPOUT2L Input 2",0,-1) == -1) return -1;
        if (setamixer(0,"HPOUT2R Input 2",0,-1) == -1) return -1;
        if (setamixer(0,"HPOUT2L Input 1",15,-1) == -1) return -1;
        if (setamixer(0,"HPOUT2L Input 1 Volume",32,-1) == -1) return -1;
        if (setamixer(0,"HPOUT2R Input 1",16,-1) == -1) return -1;
        if (setamixer(0,"HPOUT2R Input 1 Volume",32,-1) == -1) return -1;
        if (setamixer(0,"HPOUT2 Digital Switch",1,1) == -1) return -1;
        if (setamixer(0,"IN3 High Performance Switch",1,-1) == -1) return -1;
        if (setamixer(0,"IN3L Digital Volume",pvts[0].rxboost ? 142 : 102,-1) == -1) return -1;
        if (setamixer(0,"IN3R Digital Volume",pvts[1].rxboost ? 142 : 102,-1) == -1) return -1;
        if (setamixer(0,"LHPF1 Input 1",11,-1) == -1) return -1;
        if (setamixer(0,"LHPF2 Input 1",12,-1) == -1) return -1;
        if (setamixer(0,"LHPF1 Mode",1,-1) == -1) return -1;
        if (setamixer(0,"LHPF2 Mode",1,-1) == -1) return -1;
        if (setamixer(0,"LHPF1 Coefficients",0x3f0,0) == -1) return -1;
        if (setamixer(0,"LHPF2 Coefficients",0x3f0,0) == -1) return -1;
        if (setamixer(0,"AIF1TX1 Input 1",47,-1) == -1) return -1;
        if (setamixer(0,"AIF1TX1 Input 1 Volume",32,-1) == -1) return -1;
        if (setamixer(0,"AIF1TX2 Input 1",48,-1) == -1) return -1;
        if (setamixer(0,"AIF1TX2 Input 1 Volume",32,-1) == -1) return -1;

	v1 =  pvts[0].txmixerset * PLAYBACK_MAX / 1000;
	v2 =  pvts[1].txmixerset * PLAYBACK_MAX / 1000;
        if (setamixer(0,"HPOUT2 Digital Volume",v1,v2) == -1) return -1;
	v1 =  pvts[0].rxmixerset * 32 / 1000;
	v2 =  pvts[1].rxmixerset * 32 / 1000;
	/* get interval step size */
	f = 1000.0 / 32.0;
	pvts[0].rxvoiceadj = 1.0 + (modff(((float) pvts[0].rxmixerset) / f,&f1) * .093981);
	pvts[1].rxvoiceadj = 1.0 + (modff(((float) pvts[1].rxmixerset) / f,&f1) * .093981);
	if (setamixer(0,"IN3L Volume",v1,-1) == -1) return -1;
	if (setamixer(0,"IN3R Volume",v2,-1) == -1) return -1;
	return 0;
}

/*
 * grab fields from the config file, init the descriptor and open the device.
 */
static struct chan_pi_pvt *store_config(struct ast_config *cfg, char *ctg)
{
	struct ast_variable *v;
	struct chan_pi_pvt *o;
	struct ast_config *cfg1;
	char fname[200],buf[100];
	int i;
#ifdef	NEW_ASTERISK
	struct ast_flags zeroflag = {0};
#endif
	if (ctg == NULL) {
		o = &pi_default;
		ctg = "general";
	} else {
		/* "general" is also the default thing */
		if (strcmp(ctg, "general") == 0) {
			o = &pi_default;
		} else {
			if (ndevices == 2)
			{
				ast_log(LOG_ERROR,"You can only define 2 chan_pi devices!!\n");
				return NULL;
			}
			o = &pvts[ndevices++];
//			memset(o,0,sizeof(struct chan_pi_pvt));
			memcpy(o,&pi_default,sizeof(struct chan_pi_pvt));
			o->name = ast_strdup(ctg);
			if (!pi_active) 
				pi_active = o->name;
		}
	}
	ast_mutex_init(&o->txqlock);
	strcpy(o->mohinterpret, "default");
	strcpy(o->txctcssdefault,"100.0");
	strcpy(o->txctcssfreqs,"100.0");
	strcpy(o->rxctcssfreqs,"100.0");
	/* fill other fields from configuration */
	for (v = ast_variable_browse(cfg, ctg); v; v = v->next) {
		M_START((char *)v->name, (char *)v->value);

		/* handle jb conf */
		if (!ast_jb_read_conf(&global_jbconf, v->name, v->value))
			continue;

			M_UINT("debug", pi_debug)
			M_UINT("i2caddr", i2c_addr)
			M_BOOL("dsp", usedsp)
			M_BOOL("invertptt",o->invertptt)
			M_BOOL("rxboost",o->rxboost)
			M_F("carrierfrom",store_rxcdtype(o,(char *)v->value))
			M_F("ctcssfrom",store_rxsdtype(o,(char *)v->value))
			M_UINT("duplex",o->radioduplex)
 			M_BOOL("plfilter",o->plfilter)
 			M_BOOL("deemphasis",o->deemphasis)
			M_STR("txctcssdefault",o->txctcssdefault)
			M_STR("txctcssfreqs",o->txctcssfreqs)
			M_STR("rxctcssfreqs",o->rxctcssfreqs)
			M_F("txmix",store_txmix(o,(char *)v->value))
			M_F("txtoctype",store_txtoctype(o,(char *)v->value))
			M_BOOL("txlimonly",o->txlimonly)
			M_BOOL("txprelim",o->txprelim)
			M_F("rxdemod",store_rxdemod(o,(char *)v->value))
		        M_UINT("rxsqhyst",o->rxsqhyst)
		        M_UINT("rxnoisefiltype",o->rxnoisefiltype)
		        M_UINT("rxsquelchdelay",o->rxsquelchdelay)
			M_UINT("rxctcssrelax",o->rxctcssrelax)
			M_END(;
			);
			for(i = 1; i <= 10; i++)
			{
				sprintf(buf,"gpio%d",i);
				if (!strcmp(v->name,buf)) o->gpios[i] = strdup(v->value);
			}
	}

	o->debuglevel = 0;

	if (o == &pi_default)		/* we are done with the default */
		return NULL;

	snprintf(fname,sizeof(fname) - 1,config1,o->name);
#ifdef	NEW_ASTERISK
	cfg1 = ast_config_load(fname,zeroflag);
#else
	cfg1 = ast_config_load(fname);
#endif
	o->rxmixerset = 500;
	o->txmixerset = 500;
	o->rxvoiceadj = 0.5;
	o->rxctcssadj = 0.5;
	o->txctcssadj = 200;
	o->rxsquelchadj = 500;
	if (cfg1) {
		for (v = ast_variable_browse(cfg1, o->name); v; v = v->next) {
			M_START((char *)v->name, (char *)v->value);
			M_UINT("rxmixerset", o->rxmixerset)
			M_UINT("txmixerset", o->txmixerset)
			M_F("rxvoiceadj",store_rxvoiceadj(o,(char *)v->value))
			M_F("rxctcssadj",store_rxctcssadj(o,(char *)v->value))
			M_UINT("txctcssadj",o->txctcssadj);
			M_UINT("rxsquelchadj", o->rxsquelchadj)
			M_END(;
			);
		}
		ast_config_destroy(cfg1);
	} else ast_log(LOG_WARNING,"File %s not found, using default parameters.\n",fname);
		o->dsp = ast_dsp_new();
	if (o->dsp)
	{
#ifdef  NEW_ASTERISK
          ast_dsp_set_features(o->dsp,DSP_FEATURE_DIGIT_DETECT);
          ast_dsp_set_digitmode(o->dsp,DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_RELAXDTMF);
#else
          ast_dsp_set_features(o->dsp,DSP_FEATURE_DTMF_DETECT);
          ast_dsp_digitmode(o->dsp,DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_RELAXDTMF);
#endif
	}
	for(i = 1; i <= 10; i++)
	{
		/* skip if this one not specified */
		if (!o->gpios[i]) continue;
		/* skip if not out */
		if (strncasecmp(o->gpios[i],"out",3)) continue;
		gpio_ctl |= 1 << gpios_mask[i]; /* set this one to output, also */
		GPIODirection(gpios_mask[i],OUT);
		/* if default value is 1, set it */
		if (!strcasecmp(o->gpios[i],"out1")) gpio_val |= gpios_mask[i];
	}
	return o;
}

#ifdef	NEW_ASTERISK

static char *res2cli(int r)

{
	switch (r)
	{
	    case RESULT_SUCCESS:
		return(CLI_SUCCESS);
	    case RESULT_SHOWUSAGE:
		return(CLI_SHOWUSAGE);
	    default:
		return(CLI_FAILURE);
	}
}

static char *handle_Chan_pi_key(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "pi key";
                e->usage = key_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(Chan_pi_key(a->fd,a->argc,a->argv));
}

static char *handle_Chan_pi_unkey(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "pi unkey";
                e->usage = unkey_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(Chan_pi_unkey(a->fd,a->argc,a->argv));
}

static char *handle_pi_tune(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "pi tune";
                e->usage = pi_tune_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(pi_tune(a->fd,a->argc,a->argv));
}

static char *handle_pi_debug(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "pi debug";
                e->usage = pi_tune_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(pi_set_debug(a->fd,a->argc,a->argv));
}

static char *handle_pi_debug_off(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "pi debug off";
                e->usage = pi_tune_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(pi_set_debug_off(a->fd,a->argc,a->argv));
}

static char *handle_pi_active(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "pi active";
                e->usage = active_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(pi_active_cmd(a->fd,a->argc,a->argv));
}

static struct ast_cli_entry cli_pi[] = {
	AST_CLI_DEFINE(handle_pi_key,"Simulate Rx Signal Present"),
	AST_CLI_DEFINE(handle_pi_unkey,"Simulate Rx Signal Loss"),
	AST_CLI_DEFINE(handle_pi_tune,"pi Tune"),
	AST_CLI_DEFINE(handle_pi_debug,"pi Debug On"),
	AST_CLI_DEFINE(handle_pi_debug_off,"pi Debug Off"),
	AST_CLI_DEFINE(handle_pi_active,"Change commanded device")
};

#endif


/*
*/
static int load_module(void)
{
	int i;
	struct ast_config *cfg = NULL;
	char *ctg = NULL;
#ifdef	NEW_ASTERISK
	struct ast_flags zeroflag = {0};
#endif

#if !defined(__ARMEL__)
	ast_log(LOG_WARNING,"chan_pi declining module load since WE ARE NOT A Pi!!\n");
	return AST_MODULE_LOAD_DECLINE;
#endif
	pi_active = NULL;

	/* Copy the default jb config over global_jbconf */
	memcpy(&global_jbconf, &default_jbconf, sizeof(struct ast_jb_conf));

	/* load config file */
#ifdef	NEW_ASTERISK
	if (!(cfg = ast_config_load(config,zeroflag))) {
#else
	if (!(cfg = ast_config_load(config))) {
#endif
		ast_log(LOG_NOTICE, "Unable to load config %s\n", config);
		return AST_MODULE_LOAD_DECLINE;
	}

	do {
		store_config(cfg, ctg);
	} while ( (ctg = ast_category_browse(cfg, ctg)) != NULL);

	ast_config_destroy(cfg);


	if (find_pvt(pi_active) == NULL) {
		ast_log(LOG_NOTICE, "pi active device not found\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	if ((file_i2c = open(i2c_filename, O_RDWR)) < 0)
	{
		ast_log(LOG_ERROR,"Failed to open the i2c bus (addr %02X hex).\n",i2c_addr);
		return AST_MODULE_LOAD_DECLINE;
	}
	
	if (ioctl(file_i2c, I2C_SLAVE, i2c_addr) < 0)
	{
		ast_log(LOG_ERROR,"Failed to acquire bus access and/or talk to slave (addr %02X hex).\n",i2c_addr);
		return AST_MODULE_LOAD_DECLINE;
	}
	
	if (i2c_smbus_write_byte_data(file_i2c,IOEXP_IODIRA,DEFAULT_IODIRA) < 0)
	{
		ast_log(LOG_ERROR,"Failed to write to IO Expander (addr %02X hex).\n",i2c_addr);
		return AST_MODULE_LOAD_DECLINE;
	}

	if (i2c_smbus_write_byte_data(file_i2c,IOEXP_IODIRB,DEFAULT_IODIRB) < 0)
	{
		ast_log(LOG_ERROR,"Failed to write to IO Expander (addr %02X hex).\n",i2c_addr);
		return AST_MODULE_LOAD_DECLINE;
	}
	
	set_gpios();
	gpio_then = ast_tvnow();

	set_ptt(0,0);
	set_ptt(1,0);

	if (usedsp)
	{
		t_pmr_chan tChan;

		for(i = 0; i < 2; i++)
		{		
			memset(&tChan,0,sizeof(t_pmr_chan));

			tChan.pTxCodeDefault = pvts[i].txctcssdefault;
			tChan.pTxCodeSrc     = pvts[i].txctcssfreqs;
			tChan.pRxCodeSrc     = pvts[i].rxctcssfreqs;
			if (pvts[i].txlimonly) 
				tChan.txMod = 1;
			if (pvts[i].txprelim) 
				tChan.txMod = 2;
			tChan.txMixA = pvts[i].txmix;
			tChan.b.txboost = 1;
			tChan.fever = 1;

			tChan.rxDemod=pvts[i].rxdemod;
			tChan.rxCdType=pvts[i].rxcdtype;
			tChan.rxCarrierHyst=pvts[i].rxsqhyst;
			tChan.rxSqVoxAdj=pvts[i].rxsqvoxadj;
			tChan.rxSquelchDelay=pvts[i].rxsquelchdelay;
			tChan.rxNoiseFilType=pvts[i].rxnoisefiltype;
			if (pvts[i].pmrChan) destroyPmrChannel(pvts[i].pmrChan);
			pvts[i].pmrChan = createPmrChannel(&tChan,FRAME_SIZE);
			pvts[i].pmrChan->radioDuplex = pvts[i].radioduplex;
			pvts[i].pmrChan->b.loopback=0; 
			pvts[i].pmrChan->b.radioactive= 1;
			pvts[i].pmrChan->txrxblankingtime = 0;
			pvts[i].pmrChan->rxCpuSaver = 0;
			pvts[i].pmrChan->txCpuSaver = 0;
			*(pvts[i].pmrChan->prxSquelchAdjust) =
				((999 - pvts[i].rxsquelchadj) * 32767) / 1000;
			*(pvts[i].pmrChan->prxVoiceAdjust)=pvts[i].rxvoiceadj*M_Q8;
			*(pvts[i].pmrChan->prxCtcssAdjust)=pvts[i].rxctcssadj*M_Q8;
			pvts[i].pmrChan->rxCtcss->relax = pvts[i].rxctcssrelax;
			pvts[i].pmrChan->txTocType = pvts[i].txtoctype;
			pvts[i].pmrChan->spsTxOutA->outputGain = 250;
			if (pvts[i].txmix == TX_OUT_COMPOSITE)
				set_txctcss_level(&pvts[i]);
			pvts[i].pmrChan->pTxCodeDefault = pvts[i].txctcssdefault;
			pvts[i].pmrChan->pTxCodeSrc = pvts[i].txctcssfreqs;
		}
	}
 	alsa.icard = alsa_card_init("hw:sndrpiwsp", SND_PCM_STREAM_CAPTURE);
	alsa.ocard = alsa_card_init("hw:sndrpiwsp", SND_PCM_STREAM_PLAYBACK);
	if (!alsa.icard || !alsa.ocard) {
		ast_log(LOG_ERROR, "Problem opening alsa I/O devices\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	if (mixer_write() == -1) return AST_MODULE_LOAD_FAILURE;

	if (ast_channel_register(&pi_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel type 'pi'\n");
		return AST_MODULE_LOAD_FAILURE;
	}

	if (usedsp)
	{
		if (pipe(readpipe) == -1)
		{
			ast_log(LOG_ERROR,"Cannot open readpipe!!\n");
			return AST_MODULE_LOAD_FAILURE;
		}
		if (fcntl(readpipe[0], F_SETFL,  O_NONBLOCK) == -1)
		{
			ast_log(LOG_ERROR,"Cannot set readpipe NONBLOCK!!\n");
			return AST_MODULE_LOAD_FAILURE;
		}
	}

	ast_cli_register_multiple(cli_pi, sizeof(cli_pi) / sizeof(struct ast_cli_entry));

	return AST_MODULE_LOAD_SUCCESS;
}

#include "xpmr/xpmr.c"

/*
*/
static int unload_module(void)
{
	int i;

	ast_log(LOG_WARNING, "unload_module() called\n");

	ast_channel_unregister(&pi_tech);
	ast_cli_unregister_multiple(cli_pi, sizeof(cli_pi) / sizeof(struct ast_cli_entry));

	if (alsa.icard)
		snd_pcm_close(alsa.icard);
	if (alsa.ocard)
		snd_pcm_close(alsa.ocard);

	set_ptt(0,0);
	set_ptt(1,0);

#if DEBUG_CAPTURES == 1
	if (frxcapraw) { fclose(frxcapraw); frxcapraw = NULL; }
	if (ftxcapraw) { fclose(ftxcapraw); ftxcapraw = NULL; }
#endif
	for(i = 0; i < 2; i++)
	{
		if (pvts[i].dsp) ast_dsp_free(pvts[i].dsp);
		if (pvts[i].owner)
			ast_softhangup(pvts[i].owner, AST_SOFTHANGUP_APPUNLOAD);
		/* XXX what about the thread ? */
		/* XXX what about the memory allocated ? */
	}
	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY,"Pi Radio Interface Channel Driver");


/*	end of file */


