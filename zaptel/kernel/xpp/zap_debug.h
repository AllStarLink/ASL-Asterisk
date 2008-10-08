#ifndef	ZAP_DEBUG_H
#define	ZAP_DEBUG_H
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

#include <zaptel.h>	/* for zt_* defs */

/* Debugging Macros */

#define	PRINTK(level, category, fmt, ...)	\
	printk(KERN_ ## level "%s%s-%s: " fmt, #level, category, THIS_MODULE->name, ## __VA_ARGS__)

#define	XBUS_PRINTK(level, category, xbus, fmt, ...)	\
	printk(KERN_ ## level "%s%s-%s: %s: " fmt, #level,	\
		category, THIS_MODULE->name, (xbus)->busname, ## __VA_ARGS__)

#define	XPD_PRINTK(level, category, xpd, fmt, ...)	\
	printk(KERN_ ## level "%s%s-%s: %s/%s: " fmt, #level,	\
		category, THIS_MODULE->name, (xpd)->xbus->busname, (xpd)->xpdname, ## __VA_ARGS__)

#define	LINE_PRINTK(level, category, xpd, pos, fmt, ...)	\
	printk(KERN_ ## level "%s%s-%s: %s/%s/%d: " fmt, #level,	\
		category, THIS_MODULE->name, (xpd)->xbus->busname, (xpd)->xpdname, (pos), ## __VA_ARGS__)

#define	PORT_PRINTK(level, category, xbus, unit, port, fmt, ...)	\
	printk(KERN_ ## level "%s%s-%s: %s UNIT=%d PORT=%d: " fmt, #level,	\
		category, THIS_MODULE->name, (xbus)->busname, (unit), (port), ## __VA_ARGS__)

#define	DBG(bits, fmt, ...)	\
	((void)((debug & (DBG_ ## bits)) && PRINTK(DEBUG, "-" #bits, "%s: " fmt, __FUNCTION__, ## __VA_ARGS__)))
#define	INFO(fmt, ...)		PRINTK(INFO, "", fmt, ## __VA_ARGS__)
#define	NOTICE(fmt, ...)	PRINTK(NOTICE, "", fmt, ## __VA_ARGS__)
#define	WARNING(fmt, ...)	PRINTK(WARNING, "", fmt, ## __VA_ARGS__)
#define	ERR(fmt, ...)		PRINTK(ERR, "", fmt, ## __VA_ARGS__)

#define	XBUS_DBG(bits, xbus, fmt, ...)	\
			((void)((debug & (DBG_ ## bits)) && XBUS_PRINTK(DEBUG, "-" #bits, xbus, "%s: " fmt, __FUNCTION__, ## __VA_ARGS__)))
#define	XBUS_INFO(xbus, fmt, ...)		XBUS_PRINTK(INFO, "", xbus, fmt, ## __VA_ARGS__)
#define	XBUS_NOTICE(xbus, fmt, ...)		XBUS_PRINTK(NOTICE, "", xbus, fmt, ## __VA_ARGS__)
#define	XBUS_ERR(xbus, fmt, ...)		XBUS_PRINTK(ERR, "", xbus, fmt, ## __VA_ARGS__)

#define	XPD_DBG(bits, xpd, fmt, ...)	\
		((void)((debug & (DBG_ ## bits)) && XPD_PRINTK(DEBUG, "-" #bits, xpd, "%s: " fmt, __FUNCTION__, ## __VA_ARGS__)))
#define	XPD_INFO(xpd, fmt, ...)		XPD_PRINTK(INFO, "", xpd, fmt, ## __VA_ARGS__)
#define	XPD_NOTICE(xpd, fmt, ...)	XPD_PRINTK(NOTICE, "", xpd, fmt, ## __VA_ARGS__)
#define	XPD_WARNING(xpd, fmt, ...)	XPD_PRINTK(WARNING, "", xpd, fmt, ## __VA_ARGS__)
#define	XPD_ERR(xpd, fmt, ...)		XPD_PRINTK(ERR, "", xpd, fmt, ## __VA_ARGS__)

#define	LINE_DBG(bits, xpd, pos, fmt, ...)	\
			((void)((debug & (DBG_ ## bits)) && LINE_PRINTK(DEBUG, "-" #bits, xpd, pos, "%s: " fmt, __FUNCTION__, ## __VA_ARGS__)))
#define	LINE_NOTICE(xpd, pos, fmt, ...)		LINE_PRINTK(NOTICE, "", xpd, pos, fmt, ## __VA_ARGS__)
#define	LINE_ERR(xpd, pos, fmt, ...)		LINE_PRINTK(ERR, "", xpd, pos, fmt, ## __VA_ARGS__)

#define	PORT_DBG(bits, xbus, unit, port, fmt, ...)	\
			((void)((debug & (DBG_ ## bits)) && PORT_PRINTK(DEBUG, "-" #bits,	\
					xbus, unit, port, "%s: " fmt, __FUNCTION__, ## __VA_ARGS__)))
#define	PORT_NOTICE(xbus, unit, port, fmt, ...)	PORT_PRINTK(NOTICE, "", xbus, unit, port, fmt, ## __VA_ARGS__)
#define	PORT_ERR(xbus, unit, port, fmt, ...)		PORT_PRINTK(ERR, "", xbus, unit, port, fmt, ## __VA_ARGS__)

/*
 * Bits for debug
 */
#define	DBG_GENERAL	BIT(0)
#define	DBG_PCM		BIT(1)
#define	DBG_LEDS	BIT(2)
#define	DBG_SYNC	BIT(3)
#define	DBG_SIGNAL	BIT(4)
#define	DBG_PROC	BIT(5)
#define	DBG_REGS	BIT(6)
#define	DBG_DEVICES	BIT(7)	/* instantiation/destruction etc. */
#define	DBG_COMMANDS	BIT(8)	/* All commands */
#define	DBG_ANY		(~0)

void dump_poll(int debug, const char *msg, int poll);

static inline char *rxsig2str(zt_rxsig_t sig)
{
	switch(sig) {
		case ZT_RXSIG_ONHOOK:	return "ONHOOK";
		case ZT_RXSIG_OFFHOOK:	return "OFFHOOK";
		case ZT_RXSIG_START:	return "START";
		case ZT_RXSIG_RING:	return "RING";
		case ZT_RXSIG_INITIAL:	return "INITIAL";
	}
	return "Unknown rxsig";
}

static inline char *txsig2str(zt_txsig_t sig)
{
	switch(sig) {
		case ZT_TXSIG_ONHOOK:	return "TXSIG_ONHOOK";
		case ZT_TXSIG_OFFHOOK:	return "TXSIG_OFFHOOK";
		case ZT_TXSIG_START:	return "TXSIG_START";
		case ZT_TXSIG_KEWL:	return "TXSIG_KEWL";				/* Drop battery if possible */
	}
	return "Unknown txsig";
}

static inline char *event2str(int event)
{
	switch(event) {
		case ZT_EVENT_NONE:		return "NONE";
		case ZT_EVENT_ONHOOK:		return "ONHOOK";
		case ZT_EVENT_RINGOFFHOOK:	return "RINGOFFHOOK";
		case ZT_EVENT_WINKFLASH:	return "WINKFLASH";
		case ZT_EVENT_ALARM:		return "ALARM";
		case ZT_EVENT_NOALARM:		return "NOALARM";
		case ZT_EVENT_ABORT:		return "ABORT";
		case ZT_EVENT_OVERRUN:		return "OVERRUN";
		case ZT_EVENT_BADFCS:		return "BADFCS";
		case ZT_EVENT_DIALCOMPLETE:	return "DIALCOMPLETE";
		case ZT_EVENT_RINGERON:		return "RINGERON";
		case ZT_EVENT_RINGEROFF:	return "RINGEROFF";
		case ZT_EVENT_HOOKCOMPLETE:	return "HOOKCOMPLETE";
		case ZT_EVENT_BITSCHANGED:	return "BITSCHANGED";
		case ZT_EVENT_PULSE_START:	return "PULSE_START";
		case ZT_EVENT_TIMER_EXPIRED:	return "TIMER_EXPIRED";
		case ZT_EVENT_TIMER_PING:	return "TIMER_PING";
		case ZT_EVENT_POLARITY:		return "POLARITY";
	}
	return "Unknown event";
}

static inline char *hookstate2str(int hookstate)
{
	switch(hookstate) {
		case ZT_ONHOOK:		return "ZT_ONHOOK";
		case ZT_START:		return "ZT_START";
		case ZT_OFFHOOK:	return "ZT_OFFHOOK";
		case ZT_WINK:		return "ZT_WINK";
		case ZT_FLASH:		return "ZT_FLASH";
		case ZT_RING:		return "ZT_RING";
		case ZT_RINGOFF:	return "ZT_RINGOFF";
	}
	return "Unknown hookstate";
}

/* From zaptel.c */
static inline char *sig2str(int sig)
{
	switch (sig) {
		case ZT_SIG_FXSLS:	return "FXSLS";
		case ZT_SIG_FXSKS:	return "FXSKS";
		case ZT_SIG_FXSGS:	return "FXSGS";
		case ZT_SIG_FXOLS:	return "FXOLS";
		case ZT_SIG_FXOKS:	return "FXOKS";
		case ZT_SIG_FXOGS:	return "FXOGS";
		case ZT_SIG_EM:		return "E&M";
		case ZT_SIG_EM_E1:	return "E&M-E1";
		case ZT_SIG_CLEAR:	return "Clear";
		case ZT_SIG_HDLCRAW:	return "HDLCRAW";
		case ZT_SIG_HDLCFCS:	return "HDLCFCS";
		case ZT_SIG_HDLCNET:	return "HDLCNET";
		case ZT_SIG_SLAVE:	return "Slave";
		case ZT_SIG_CAS:	return "CAS";
		case ZT_SIG_DACS:	return "DACS";
		case ZT_SIG_DACS_RBS:	return "DACS+RBS";
		case ZT_SIG_SF:		return "SF (ToneOnly)";
		case ZT_SIG_NONE:
					break;
	}
	return "Unconfigured";
}

static inline char *alarmbit2str(int alarmbit)
{
	/* from zaptel.h */
	switch(1 << alarmbit) {
		case ZT_ALARM_NONE:	return "NONE";
		case ZT_ALARM_RECOVER:	return "RECOVER";
		case ZT_ALARM_LOOPBACK:	return "LOOPBACK";
		case ZT_ALARM_YELLOW:	return "YELLOW";
		case ZT_ALARM_RED:	return "RED";
		case ZT_ALARM_BLUE:	return "BLUE";
		case ZT_ALARM_NOTOPEN:	return "NOTOPEN";
	}
	return "UNKNOWN";
}

void alarm2str(int alarm, char *buf, int buflen);

#endif	/* ZAP_DEBUG_H */
