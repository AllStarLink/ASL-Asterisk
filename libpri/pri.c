/*
 * libpri: An implementation of Primary Rate ISDN
 *
 * Written by Mark Spencer <markster@digium.com>
 *
 * Copyright (C) 2001-2005, Digium, Inc.
 * All Rights Reserved.
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
 *
 * In addition, when this program is distributed with Asterisk in
 * any form that would qualify as a 'combined work' or as a
 * 'derivative work' (but not mere aggregation), you can redistribute
 * and/or modify the combination under the terms of the license
 * provided with that copy of Asterisk, instead of the license
 * terms granted here.
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/select.h>
#include <stdarg.h>
#include "compat.h"
#include "libpri.h"
#include "pri_internal.h"
#include "pri_facility.h"
#include "pri_q921.h"
#include "pri_q931.h"
#include "pri_timers.h"

char *pri_node2str(int node)
{
	switch(node) {
	case PRI_UNKNOWN:
		return "Unknown node type";
	case PRI_NETWORK:
		return "Network";
	case PRI_CPE:
		return "CPE";
	default:
		return "Invalid value";
	}
}

char *pri_switch2str(int sw)
{
	switch(sw) {
	case PRI_SWITCH_NI2:
		return "National ISDN";
	case PRI_SWITCH_DMS100:
		return "Nortel DMS100";
	case PRI_SWITCH_LUCENT5E:
		return "Lucent 5E";
	case PRI_SWITCH_ATT4ESS:
		return "AT&T 4ESS";
	case PRI_SWITCH_NI1:
		return "National ISDN 1";
	case PRI_SWITCH_EUROISDN_E1:
		return "EuroISDN";
	case PRI_SWITCH_GR303_EOC:
		return "GR303 EOC";
	case PRI_SWITCH_GR303_TMC:
		return "GR303 TMC";
	case PRI_SWITCH_QSIG:
		return "Q.SIG switch";
	default:
		return "Unknown switchtype";
	}
}

static void pri_default_timers(struct pri *pri, int switchtype)
{
	static const int defaulttimers[20][PRI_MAX_TIMERS] = PRI_TIMERS_ALL;
	int x;

	for (x = 0; x<PRI_MAX_TIMERS; x++) {
		pri->timers[x] = defaulttimers[switchtype][x];
	}
}

int pri_set_timer(struct pri *pri, int timer, int value)
{
	if (timer < 0 || timer > PRI_MAX_TIMERS || value < 0)
		return -1;

	pri->timers[timer] = value;
	return 0;
}

int pri_get_timer(struct pri *pri, int timer)
{
	if (timer < 0 || timer > PRI_MAX_TIMERS)
		return -1;
	return pri->timers[timer];
}

int pri_timer2idx(char *timer)
{
	if (!strcasecmp(timer, "N200"))
		return PRI_TIMER_N200;
	else if (!strcasecmp(timer, "N201"))
		return PRI_TIMER_N201;
	else if (!strcasecmp(timer, "N202"))
		return PRI_TIMER_N202;
	else if (!strcasecmp(timer, "K"))
		return PRI_TIMER_K;
	else if (!strcasecmp(timer, "T200"))
		return PRI_TIMER_T200;
	else if (!strcasecmp(timer, "T202"))
		return PRI_TIMER_T202;
	else if (!strcasecmp(timer, "T203"))
		return PRI_TIMER_T203;
	else if (!strcasecmp(timer, "T300"))
		return PRI_TIMER_T300;
	else if (!strcasecmp(timer, "T301"))
		return PRI_TIMER_T301;
	else if (!strcasecmp(timer, "T302"))
		return PRI_TIMER_T302;
	else if (!strcasecmp(timer, "T303"))
		return PRI_TIMER_T303;
	else if (!strcasecmp(timer, "T304"))
		return PRI_TIMER_T304;
	else if (!strcasecmp(timer, "T305"))
		return PRI_TIMER_T305;
	else if (!strcasecmp(timer, "T306"))
		return PRI_TIMER_T306;
	else if (!strcasecmp(timer, "T307"))
		return PRI_TIMER_T307;
	else if (!strcasecmp(timer, "T308"))
		return PRI_TIMER_T308;
	else if (!strcasecmp(timer, "T309"))
		return PRI_TIMER_T309;
	else if (!strcasecmp(timer, "T310"))
		return PRI_TIMER_T310;
	else if (!strcasecmp(timer, "T313"))
		return PRI_TIMER_T313;
	else if (!strcasecmp(timer, "T314"))
		return PRI_TIMER_T314;
	else if (!strcasecmp(timer, "T316"))
		return PRI_TIMER_T316;
	else if (!strcasecmp(timer, "T317"))
		return PRI_TIMER_T317;
	else if (!strcasecmp(timer, "T318"))
		return PRI_TIMER_T318;
	else if (!strcasecmp(timer, "T319"))
		return PRI_TIMER_T319;
	else if (!strcasecmp(timer, "T320"))
		return PRI_TIMER_T320;
	else if (!strcasecmp(timer, "T321"))
		return PRI_TIMER_T321;
	else if (!strcasecmp(timer, "T322"))
		return PRI_TIMER_T322;
	else
		return -1;
}

static int __pri_read(struct pri *pri, void *buf, int buflen)
{
	int res = read(pri->fd, buf, buflen);
	if (res < 0) {
		if (errno != EAGAIN)
			pri_error(pri, "Read on %d failed: %s\n", pri->fd, strerror(errno));
		return 0;
	}
	return res;
}

static int __pri_write(struct pri *pri, void *buf, int buflen)
{
	int res = write(pri->fd, buf, buflen);
	if (res < 0) {
		if (errno != EAGAIN)
			pri_error(pri, "Write to %d failed: %s\n", pri->fd, strerror(errno));
		return 0;
	}
	return res;
}

/* Pass in the master for this function */
void __pri_free_tei(struct pri * p)
{
	free (p);
}

struct pri *__pri_new_tei(int fd, int node, int switchtype, struct pri *master, pri_io_cb rd, pri_io_cb wr, void *userdata, int tei, int bri)
{
	struct pri *p;

	if (!(p = calloc(1, sizeof(*p))))
		return NULL;

	p->bri = bri;
	p->fd = fd;
	p->read_func = rd;
	p->write_func = wr;
	p->userdata = userdata;
	p->localtype = node;
	p->switchtype = switchtype;
	p->cref = 1;
	p->sapi = (tei == Q921_TEI_GROUP) ? Q921_SAPI_LAYER2_MANAGEMENT : Q921_SAPI_CALL_CTRL;
	p->tei = tei;
	p->nsf = PRI_NSF_NONE;
	p->protodisc = Q931_PROTOCOL_DISCRIMINATOR;
	p->master = master;
	p->callpool = &p->localpool;
	pri_default_timers(p, switchtype);
	if (master) {
		pri_set_debug(p, master->debug);
		if (master->sendfacility)
			pri_facility_enable(p);
	}
#ifdef LIBPRI_COUNTERS
	p->q921_rxcount = 0;
	p->q921_txcount = 0;
	p->q931_rxcount = 0;
	p->q931_txcount = 0;
#endif
	if (switchtype == PRI_SWITCH_GR303_EOC) {
		p->protodisc = GR303_PROTOCOL_DISCRIMINATOR;
		p->sapi = Q921_SAPI_GR303_EOC;
		p->tei = Q921_TEI_GR303_EOC_OPS;
		p->subchannel = __pri_new_tei(-1, node, PRI_SWITCH_GR303_EOC_PATH, p, NULL, NULL, NULL, Q921_TEI_GR303_EOC_PATH, 0);
		if (!p->subchannel) {
			free(p);
			p = NULL;
		}
	} else if (switchtype == PRI_SWITCH_GR303_TMC) {
		p->protodisc = GR303_PROTOCOL_DISCRIMINATOR;
		p->sapi = Q921_SAPI_GR303_TMC_CALLPROC;
		p->tei = Q921_TEI_GR303_TMC_CALLPROC;
		p->subchannel = __pri_new_tei(-1, node, PRI_SWITCH_GR303_TMC_SWITCHING, p, NULL, NULL, NULL, Q921_TEI_GR303_TMC_SWITCHING, 0);
		if (!p->subchannel) {
			free(p);
			p = NULL;
		}
	} else if (switchtype == PRI_SWITCH_GR303_TMC_SWITCHING) {
		p->protodisc = GR303_PROTOCOL_DISCRIMINATOR;
		p->sapi = Q921_SAPI_GR303_TMC_SWITCHING;
		p->tei = Q921_TEI_GR303_TMC_SWITCHING;
	} else if (switchtype == PRI_SWITCH_GR303_EOC_PATH) {
		p->protodisc = GR303_PROTOCOL_DISCRIMINATOR;
		p->sapi = Q921_SAPI_GR303_EOC;
		p->tei = Q921_TEI_GR303_EOC_PATH;
	}
	/* Start Q.921 layer, Wait if we're the network */
	if (p)
		q921_start(p, p->localtype == PRI_CPE);
	
	return p;
}

void pri_call_set_useruser(q931_call *c, const char *userchars)
{
	if (userchars)
		libpri_copy_string(c->useruserinfo, userchars, sizeof(c->useruserinfo));
}

void pri_sr_set_useruser(struct pri_sr *sr, const char *userchars)
{
	sr->useruserinfo = userchars;
}

int pri_restart(struct pri *pri)
{
	/* Restart Q.921 layer */
	if (pri) {
		q921_reset(pri);
		q921_start(pri, pri->localtype == PRI_CPE);	
	}
	return 0;
}

struct pri *pri_new(int fd, int nodetype, int switchtype)
{
	return __pri_new_tei(fd, nodetype, switchtype, NULL, __pri_read, __pri_write, NULL, Q921_TEI_PRI, 0);
}

struct pri *pri_new_bri(int fd, int ptpmode, int nodetype, int switchtype)
{
	if (ptpmode)
		return __pri_new_tei(fd, nodetype, switchtype, NULL, __pri_read, __pri_write, NULL, Q921_TEI_PRI, 1);
	else
		return __pri_new_tei(fd, nodetype, switchtype, NULL, __pri_read, __pri_write, NULL, Q921_TEI_GROUP, 1);
}

struct pri *pri_new_cb(int fd, int nodetype, int switchtype, pri_io_cb io_read, pri_io_cb io_write, void *userdata)
{
	if (!io_read)
		io_read = __pri_read;
	if (!io_write)
		io_write = __pri_write;
	return __pri_new_tei(fd, nodetype, switchtype, NULL, io_read, io_write, userdata, Q921_TEI_PRI, 0);
}

void *pri_get_userdata(struct pri *pri)
{
	return pri ? pri->userdata : NULL;
}

void pri_set_userdata(struct pri *pri, void *userdata)
{
	if (pri)
		pri->userdata = userdata;
}

void pri_set_nsf(struct pri *pri, int nsf)
{
	if (pri)
		pri->nsf = nsf;
}

char *pri_event2str(int id)
{
	switch(id) {
	case PRI_EVENT_DCHAN_UP:
		return "D-Channel Up";
	case PRI_EVENT_DCHAN_DOWN:
		return "D-channel Down";
	case PRI_EVENT_RESTART:
		return "Restart channel";
	case PRI_EVENT_RING:
		return "Ring";
	case PRI_EVENT_HANGUP:
		return "Hangup";
	case PRI_EVENT_RINGING:
		return "Ringing";
	case PRI_EVENT_ANSWER:
		return "Answer";
	case PRI_EVENT_HANGUP_ACK:
		return "Hangup ACK";
	case PRI_EVENT_RESTART_ACK:
		return "Restart ACK";
	case PRI_EVENT_FACNAME:
		return "FacName";
	case PRI_EVENT_INFO_RECEIVED:
		return "Info Received";
	case PRI_EVENT_PROCEEDING:
		return "Proceeding";
	case PRI_EVENT_SETUP_ACK:
		return "Setup ACK";
	case PRI_EVENT_HANGUP_REQ:
		return "Hangup Req";
	case PRI_EVENT_NOTIFY:
		return "Notify";
	case PRI_EVENT_PROGRESS:
		return "Progress";
	case PRI_EVENT_CONFIG_ERR:
		return "Configuration Error";
	default:
		return "Unknown Event";
	}
}

pri_event *pri_check_event(struct pri *pri)
{
	char buf[1024];
	int res;
	pri_event *e;
	res = pri->read_func ? pri->read_func(pri, buf, sizeof(buf)) : 0;
	if (!res)
		return NULL;
	/* Receive the q921 packet */
	e = q921_receive(pri, (q921_h *)buf, res);
	return e;
}

static int wait_pri(struct pri *pri)
{	
	struct timeval *tv, real;
	fd_set fds;
	int res;
	FD_ZERO(&fds);
	FD_SET(pri->fd, &fds);
	tv = pri_schedule_next(pri);
	if (tv) {
		gettimeofday(&real, NULL);
		real.tv_sec = tv->tv_sec - real.tv_sec;
		real.tv_usec = tv->tv_usec - real.tv_usec;
		if (real.tv_usec < 0) {
			real.tv_usec += 1000000;
			real.tv_sec -= 1;
		}
		if (real.tv_sec < 0) {
			real.tv_sec = 0;
			real.tv_usec = 0;
		}
	}
	res = select(pri->fd + 1, &fds, NULL, NULL, tv ? &real : tv);
	if (res < 0) 
		return -1;
	return res;
}

pri_event *pri_mkerror(struct pri *pri, char *errstr)
{
	/* Return a configuration error */
	pri->ev.err.e = PRI_EVENT_CONFIG_ERR;
	libpri_copy_string(pri->ev.err.err, errstr, sizeof(pri->ev.err.err));
	return &pri->ev;
}


pri_event *pri_dchannel_run(struct pri *pri, int block)
{
	pri_event *e;
	int res;
	if (!pri)
		return NULL;
	if (block) {
		do {
			e =  NULL;
			res = wait_pri(pri);
			/* Check for error / interruption */
			if (res < 0)
				return NULL;
			if (!res)
				e = pri_schedule_run(pri);
			else
				e = pri_check_event(pri);
		} while(!e);
	} else {
		e = pri_check_event(pri);
		return e;
	}
	return e;
}

void pri_set_debug(struct pri *pri, int debug)
{
	if (!pri)
		return;
	pri->debug = debug;
	if (pri->subchannel)
		pri_set_debug(pri->subchannel, debug);
}

int pri_get_debug(struct pri *pri)
{
	if (!pri)
		return -1;
	if (pri->subchannel)
		return pri_get_debug(pri->subchannel);
	return pri->debug;
}

void pri_facility_enable(struct pri *pri)
{
	if (!pri)
		return;
	pri->sendfacility = 1;
	if (pri->subchannel)
		pri_facility_enable(pri->subchannel);
	return;
}

int pri_acknowledge(struct pri *pri, q931_call *call, int channel, int info)
{
	if (!pri || !call)
		return -1;
	return q931_alerting(pri, call, channel, info);
}

int pri_proceeding(struct pri *pri, q931_call *call, int channel, int info)
{
	if (!pri || !call)
		return -1;
	return q931_call_proceeding(pri, call, channel, info);
}

int pri_progress(struct pri *pri, q931_call *call, int channel, int info)
{
	if (!pri || !call)
		return -1;
	return q931_call_progress(pri, call, channel, info);
}

int pri_information(struct pri *pri, q931_call *call, char digit)
{
	if (!pri || !call)
		return -1;
	return q931_information(pri, call, digit);
}

int pri_keypad_facility(struct pri *pri, q931_call *call, char *digits)
{
	if (!pri || !call || !digits || !digits[0])
		return -1;

	return q931_keypad_facility(pri, call, digits);
}

int pri_notify(struct pri *pri, q931_call *call, int channel, int info)
{
	if (!pri || !call)
		return -1;
	return q931_notify(pri, call, channel, info);
}

void pri_destroycall(struct pri *pri, q931_call *call)
{
	if (pri && call)
		__q931_destroycall(pri, call);
	return;
}

int pri_need_more_info(struct pri *pri, q931_call *call, int channel, int nonisdn)
{
	if (!pri || !call)
		return -1;
	return q931_setup_ack(pri, call, channel, nonisdn);
}

int pri_answer(struct pri *pri, q931_call *call, int channel, int nonisdn)
{
	if (!pri || !call)
		return -1;
	return q931_connect(pri, call, channel, nonisdn);
}

#if 0
/* deprecated routines, use pri_hangup */
int pri_release(struct pri *pri, q931_call *call, int cause)
{
	if (!pri || !call)
		return -1;
	return q931_release(pri, call, cause);
}

int pri_disconnect(struct pri *pri, q931_call *call, int cause)
{
	if (!pri || !call)
		return -1;
	return q931_disconnect(pri, call, cause);
}
#endif

int pri_channel_bridge(q931_call *call1, q931_call *call2)
{
	if (!call1 || !call2)
		return -1;

	/* Make sure we have compatible switchtypes */
	if (call1->pri->switchtype != call2->pri->switchtype)
		return -1;

	/* Check for bearer capability */
	if (call1->transcapability != call2->transcapability)
		return -1;

	/* Check to see if we're on the same PRI */
	if (call1->pri != call2->pri)
		return -1;
	
	switch (call1->pri->switchtype) {
		case PRI_SWITCH_NI2:
		case PRI_SWITCH_LUCENT5E:
		case PRI_SWITCH_ATT4ESS:
			if (eect_initiate_transfer(call1->pri, call1, call2))
				return -1;
			else
				return 0;
			break;
		case PRI_SWITCH_DMS100:
			if (rlt_initiate_transfer(call1->pri, call1, call2))
				return -1;
			else
				return 0;
			break;
		case PRI_SWITCH_QSIG:
			call1->bridged_call = call2;
			call2->bridged_call = call1;
			if (anfpr_initiate_transfer(call1->pri, call1, call2))
				return -1;
			else
				return 0;
			break;
		default:
			return -1;
	}
}

int pri_hangup(struct pri *pri, q931_call *call, int cause)
{
	if (!pri || !call)
		return -1;
	if (cause == -1)
		/* normal clear cause */
		cause = 16;
	return q931_hangup(pri, call, cause);
}

int pri_reset(struct pri *pri, int channel)
{
	if (!pri)
		return -1;
	return q931_restart(pri, channel);
}

q931_call *pri_new_call(struct pri *pri)
{
	if (!pri)
		return NULL;
	return q931_new_call(pri);
}

void pri_dump_event(struct pri *pri, pri_event *e)
{
	if (!pri || !e)
		return;
	pri_message(pri, "Event type: %s (%d)\n", pri_event2str(e->gen.e), e->gen.e);
	switch(e->gen.e) {
	case PRI_EVENT_DCHAN_UP:
	case PRI_EVENT_DCHAN_DOWN:
		break;
	case PRI_EVENT_CONFIG_ERR:
		pri_message(pri, "Error: %s", e->err.err);
		break;
	case PRI_EVENT_RESTART:
		pri_message(pri, "Restart on channel %d\n", e->restart.channel);
	case PRI_EVENT_RING:
		pri_message(pri, "Calling number: %s (%s, %s)\n", e->ring.callingnum, pri_plan2str(e->ring.callingplan), pri_pres2str(e->ring.callingpres));
		pri_message(pri, "Called number: %s (%s)\n", e->ring.callednum, pri_plan2str(e->ring.calledplan));
		pri_message(pri, "Channel: %d (%s) Reference number: %d\n", e->ring.channel, e->ring.flexible ? "Flexible" : "Not Flexible", e->ring.cref);
		break;
	case PRI_EVENT_HANGUP:
		pri_message(pri, "Hangup, reference number: %d, reason: %s\n", e->hangup.cref, pri_cause2str(e->hangup.cause));
		break;
	default:
		pri_message(pri, "Don't know how to dump events of type %d\n", e->gen.e);
	}
}

static void pri_sr_init(struct pri_sr *req)
{
	memset(req, 0, sizeof(struct pri_sr));
	
}

int pri_sr_set_connection_call_independent(struct pri_sr *req)
{
	if (!req)
		return -1;

	req->justsignalling = 1; /* have to set justsignalling for all those pesky IEs we need to setup */
	return 0;
}

/* Don't call any other pri functions on this */
int pri_mwi_activate(struct pri *pri, q931_call *c, char *caller, int callerplan, char *callername, int callerpres, char *called,
					int calledplan)
{
	struct pri_sr req;
	if (!pri || !c)
		return -1;

	pri_sr_init(&req);
	pri_sr_set_connection_call_independent(&req);

	req.caller = caller;
	req.callerplan = callerplan;
	req.callername = callername;
	req.callerpres = callerpres;
	req.called = called;
	req.calledplan = calledplan;

	if (mwi_message_send(pri, c, &req, 1) < 0) {
		pri_message(pri, "Unable to send MWI activate message\n");
		return -1;
	}
	/* Do more stuff when we figure out that the CISC stuff works */
	return q931_setup(pri, c, &req);
}

int pri_mwi_deactivate(struct pri *pri, q931_call *c, char *caller, int callerplan, char *callername, int callerpres, char *called,
					int calledplan)
{
	struct pri_sr req;
	if (!pri || !c)
		return -1;

	pri_sr_init(&req);
	pri_sr_set_connection_call_independent(&req);

	req.caller = caller;
	req.callerplan = callerplan;
	req.callername = callername;
	req.callerpres = callerpres;
	req.called = called;
	req.calledplan = calledplan;

	if(mwi_message_send(pri, c, &req, 0) < 0) {
		pri_message(pri, "Unable to send MWI deactivate message\n");
		return -1;
	}

	return q931_setup(pri, c, &req);
}
	
int pri_setup(struct pri *pri, q931_call *c, struct pri_sr *req)
{
	if (!pri || !c)
		return -1;

	return q931_setup(pri, c, req);
}

int pri_call(struct pri *pri, q931_call *c, int transmode, int channel, int exclusive, 
					int nonisdn, char *caller, int callerplan, char *callername, int callerpres, char *called,
					int calledplan,int ulayer1)
{
	struct pri_sr req;
	if (!pri || !c)
		return -1;
	pri_sr_init(&req);
	req.transmode = transmode;
	req.channel = channel;
	req.exclusive = exclusive;
	req.nonisdn =  nonisdn;
	req.caller = caller;
	req.callerplan = callerplan;
	req.callername = callername;
	req.callerpres = callerpres;
	req.called = called;
	req.calledplan = calledplan;
	req.userl1 = ulayer1;
	return q931_setup(pri, c, &req);
}	

static void (*__pri_error)(struct pri *pri, char *stuff);
static void (*__pri_message)(struct pri *pri, char *stuff);

void pri_set_message(void (*func)(struct pri *pri, char *stuff))
{
	__pri_message = func;
}

void pri_set_error(void (*func)(struct pri *pri, char *stuff))
{
	__pri_error = func;
}

void pri_message(struct pri *pri, char *fmt, ...)
{
	char tmp[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	if (__pri_message)
		__pri_message(pri, tmp);
	else
		fputs(tmp, stdout);
}

void pri_error(struct pri *pri, char *fmt, ...)
{
	char tmp[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	if (__pri_error)
		__pri_error(pri, tmp);
	else
		fputs(tmp, stderr);
}

/* Set overlap mode */
void pri_set_overlapdial(struct pri *pri,int state)
{
	pri->overlapdial = state;
}

void pri_set_inbanddisconnect(struct pri *pri, unsigned int enable)
{
	pri->acceptinbanddisconnect = (enable != 0);
}

int pri_fd(struct pri *pri)
{
	return pri->fd;
}

char *pri_dump_info_str(struct pri *pri)
{
	char buf[4096];
	int len = 0;
#ifdef LIBPRI_COUNTERS
	struct q921_frame *f;
	int q921outstanding = 0;
#endif
	if (!pri)
		return NULL;

	/* Might be nice to format these a little better */
	len += sprintf(buf + len, "Switchtype: %s\n", pri_switch2str(pri->switchtype));
	len += sprintf(buf + len, "Type: %s\n", pri_node2str(pri->localtype));
#ifdef LIBPRI_COUNTERS
	/* Remember that Q921 Counters include Q931 packets (and any retransmissions) */
	len += sprintf(buf + len, "Q931 RX: %d\n", pri->q931_rxcount);
	len += sprintf(buf + len, "Q931 TX: %d\n", pri->q931_txcount);
	len += sprintf(buf + len, "Q921 RX: %d\n", pri->q921_rxcount);
	len += sprintf(buf + len, "Q921 TX: %d\n", pri->q921_txcount);
	f = pri->txqueue;
	while (f) {
		q921outstanding++;
		f = f->next;
	}
	len += sprintf(buf + len, "Q921 Outstanding: %d\n", q921outstanding);
#endif
	len += sprintf(buf + len, "Window Length: %d/%d\n", pri->windowlen, pri->window);
	len += sprintf(buf + len, "Sentrej: %d\n", pri->sentrej);
	len += sprintf(buf + len, "SolicitFbit: %d\n", pri->solicitfbit);
	len += sprintf(buf + len, "Retrans: %d\n", pri->retrans);
	len += sprintf(buf + len, "Busy: %d\n", pri->busy);
	len += sprintf(buf + len, "Overlap Dial: %d\n", pri->overlapdial);
	len += sprintf(buf + len, "T200 Timer: %d\n", pri->timers[PRI_TIMER_T200]);
	len += sprintf(buf + len, "T203 Timer: %d\n", pri->timers[PRI_TIMER_T203]);
	len += sprintf(buf + len, "T305 Timer: %d\n", pri->timers[PRI_TIMER_T305]);
	len += sprintf(buf + len, "T308 Timer: %d\n", pri->timers[PRI_TIMER_T308]);
	len += sprintf(buf + len, "T309 Timer: %d\n", pri->timers[PRI_TIMER_T309]);
	len += sprintf(buf + len, "T313 Timer: %d\n", pri->timers[PRI_TIMER_T313]);
	len += sprintf(buf + len, "N200 Counter: %d\n", pri->timers[PRI_TIMER_N200]);

	return strdup(buf);
}

int pri_get_crv(struct pri *pri, q931_call *call, int *callmode)
{
	return q931_call_getcrv(pri, call, callmode);
}

int pri_set_crv(struct pri *pri, q931_call *call, int crv, int callmode)
{
	return q931_call_setcrv(pri, call, crv, callmode);
}

void pri_enslave(struct pri *master, struct pri *slave)
{
	if (master && slave)
		slave->callpool = &master->localpool;
}

struct pri_sr *pri_sr_new(void)
{
	struct pri_sr *req;
	req = malloc(sizeof(*req));
	if (req) 
		pri_sr_init(req);
	return req;
}

void pri_sr_free(struct pri_sr *sr)
{
	free(sr);
}

int pri_sr_set_channel(struct pri_sr *sr, int channel, int exclusive, int nonisdn)
{
	sr->channel = channel;
	sr->exclusive = exclusive;
	sr->nonisdn = nonisdn;
	return 0;
}

int pri_sr_set_bearer(struct pri_sr *sr, int transmode, int userl1)
{
	sr->transmode = transmode;
	sr->userl1 = userl1;
	return 0;
}

int pri_sr_set_called(struct pri_sr *sr, char *called, int calledplan, int numcomplete)
{
	sr->called = called;
	sr->calledplan = calledplan;
	sr->numcomplete = numcomplete;
	return 0;
}

int pri_sr_set_caller(struct pri_sr *sr, char *caller, char *callername, int callerplan, int callerpres)
{
	sr->caller = caller;
	sr->callername = callername;
	sr->callerplan = callerplan;
	sr->callerpres = callerpres;
	return 0;
}

int pri_sr_set_redirecting(struct pri_sr *sr, char *num, int plan, int pres, int reason)
{
	sr->redirectingnum = num;
	sr->redirectingplan = plan;
	sr->redirectingpres = pres;
	sr->redirectingreason = reason;
	return 0;
}
