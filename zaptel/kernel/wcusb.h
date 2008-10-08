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

#ifndef _WCUSB_H
#define _WCUSB_H

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/usb.h>

#include <linux/version.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,4,19)
#define USB2420
#endif

#include "zaptel.h"

#define WC_MAX_IFACES           128

#define POWERSAVE_TIME 4000 /* Powersaving timeout for devices with a proslic */

/* Various registers and data ports on the tigerjet part */
#define WCUSB_SPORT0				0x26
#define WCUSB_SPORT1				0x27
#define WCUSB_SPORT2				0x28
#define WCUSB_SPORT_CTRL			0x29

#define WC_AUX0 0x1
#define WC_AUX1 0x2
#define WC_AUX2 0x4
#define WC_AUX3 0x8

#define CONTROL_TIMEOUT_MS              (500)           /* msec */
#define CONTROL_TIMEOUT_JIFFIES ((CONTROL_TIMEOUT_MS * HZ) / 1000)

#define REQUEST_NORMAL 4

#define FLAG_RUNNING    (1 << 0)
 
/* Important data structures and data types */


/* States for the Proslic read state machine */
typedef enum {
        STATE_WCREAD_WRITEREG,
        STATE_WCREAD_READRES,
        STATE_WCWRITE_WRITEREG,
        STATE_WCWRITE_WRITERES,
} proslic_state_t;

/* Used for current stream state */
typedef enum {
        STREAM_NORMAL, /* Sends normal (unmodified) audio data */
        STREAM_DTMF, /* (For keypad device) Sends dtmf data */
} stream_t;

/* States for the Keypad state machine */
typedef enum {
        STATE_FOR_LOOP_1_OUT,
        STATE_FOR_LOOP_2_IN,
        STATE_FOR_LOOP_PROC_DATA,
        STATE_FOR_LOOP_CLEAR_DIGIT,
} keypad_state_t;

/* Device types.  For radical changes in a new device, use a switch based on the device type */
typedef enum {
        WC_KEYPAD,  /* The tigerjet phone with the keypad.  That was a bugger to implement */
        WC_PROSLIC, /* For various devices with a proslic */
} dev_type_t;

struct wc_keypad_data {
        keypad_state_t state; /* Current state in the keypad detect routine */
#ifdef USB2420
        struct urb urb;  /* urb used for the keypad data transport ... can't remember whether it is used or not */
#else
        urb_t urb;  /* urb used for the keypad data transport ... can't remember whether it is used or not */
#endif
	int running;
        char data;
        char data12;
        char tmp;
        int scanned_event;
        int i;
        int count;
        /* DTMF tone generation stuff for zaptel */
        struct zt_tone_state ts;
        struct zt_tone *tone;
};

struct stinky_urb {
#ifdef USB2420
        struct urb urb;
#ifndef LINUX26
        struct iso_packet_descriptor isoframe[1];
#endif		
#else
        urb_t urb;
        iso_packet_descriptor_t isoframe[1];
#endif
};

struct wc_usb_pvt {
	const char *variety;
        struct usb_device *dev;
        dev_type_t devclass;
        int usecount;
        int dead;
        struct zt_span span;
        struct zt_chan chan;
        struct stinky_urb dataread[2];
        struct stinky_urb datawrite[2];
#ifdef USB2420
        struct urb          control;
        struct usb_ctrlrequest      dr;
#else
        urb_t           control;
        devrequest      dr;
#endif
        proslic_state_t controlstate;
        int urbcount;
        int flags;
        int timer;
        int lowpowertimer;
        int idletxhookstate;
        int hookstate;
        __u8 newtxhook;
        __u8 txhook;
        int pos;
        unsigned char auxstatus;
        unsigned char wcregindex;
        unsigned char wcregbuf[4];
        unsigned char wcregval;
        short readchunk[ZT_MAX_CHUNKSIZE * 2];
        short writechunk[ZT_MAX_CHUNKSIZE * 2];
        stream_t sample;
        void *pvt_data;
};

struct wc_usb_desc {
        char *name;
        int flags;
};
#endif
