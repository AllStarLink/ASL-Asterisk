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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "compat.h"
#include "libpri.h"
#include "pri_internal.h"
#include "pri_q921.h" 
#include "pri_q931.h"

/*
 * Define RANDOM_DROPS To randomly drop packets in order to simulate loss for testing
 * retransmission functionality
 */

/*
#define RANDOM_DROPS
*/

#define Q921_INIT(pri, hf) do { \
	memset(&(hf),0,sizeof(hf)); \
	(hf).h.sapi = (pri)->sapi; \
	(hf).h.ea1 = 0; \
	(hf).h.ea2 = 1; \
	(hf).h.tei = (pri)->tei; \
} while(0)

static void reschedule_t203(struct pri *pri);
static void q921_restart(struct pri *pri, int now);
static void q921_tei_release_and_reacquire(struct pri *master);

static void q921_discard_retransmissions(struct pri *pri)
{
	struct q921_frame *f, *p;
	f = pri->txqueue;
	while(f) {
		p = f;
		f = f->next;
		/* Free frame */
		free(p);
	}
	pri->txqueue = NULL;
}

static int q921_transmit(struct pri *pri, q921_h *h, int len) 
{
	int res;

	if (pri->master)
		pri = pri->master;

#ifdef RANDOM_DROPS
   if (!(random() % 3)) {
         pri_message(pri, " === Dropping Packet ===\n");
         return 0;
   }
#endif   
#ifdef LIBPRI_COUNTERS
	pri->q921_txcount++;      
#endif
	/* Just send it raw */
	if (pri->debug & (PRI_DEBUG_Q921_DUMP | PRI_DEBUG_Q921_RAW))
		q921_dump(pri, h, len, pri->debug & PRI_DEBUG_Q921_RAW, 1);
	/* Write an extra two bytes for the FCS */
	res = pri->write_func ? pri->write_func(pri, h, len + 2) : 0;
	if (res != (len + 2)) {
		pri_error(pri, "Short write: %d/%d (%s)\n", res, len + 2, strerror(errno));
		return -1;
	}
	reschedule_t203(pri);
	return 0;
}

static void q921_send_tei(struct pri *pri, int message, int ri, int ai, int iscommand)
{
	q921_u *f;

	if (!(f = calloc(1, sizeof(*f) + 5)))
		return;

	Q921_INIT(pri, *f);
	f->h.c_r = (pri->localtype == PRI_NETWORK) ? iscommand : !iscommand;
	f->ft = Q921_FRAMETYPE_U;
	f->data[0] = 0x0f;	/* Management entity */
	f->data[1] = (ri >> 8) & 0xff;
	f->data[2] = ri & 0xff;
	f->data[3] = message;
	f->data[4] = (ai << 1) | 1;
	if (pri->debug & PRI_DEBUG_Q921_STATE)
		pri_message(pri, "Sending TEI management message %d, TEI=%d\n", message, ai);
	q921_transmit(pri, (q921_h *)f, 8);
	free(f);
}

static void q921_tei_request(void *vpri)
{
	struct pri *pri = (struct pri *)vpri;

	if (pri->subchannel) {
		pri_error(pri, "Cannot request TEI while its already assigned\n");
		return;
	}
	pri->n202_counter++;
#if 0
	if (pri->n202_counter > pri->timers[PRI_TIMER_N202]) {
		pri_error(pri, "Unable to assign TEI from network\n");
		return;
	}
#endif
	pri->ri = random() % 65535;
	q921_send_tei(pri, Q921_TEI_IDENTITY_REQUEST, pri->ri, Q921_TEI_GROUP, 1);
	if (pri->t202_timer)
		pri_schedule_del(pri, pri->t202_timer);
	pri->t202_timer = pri_schedule_event(pri, pri->timers[PRI_TIMER_T202], q921_tei_request, pri);
}

static void q921_send_ua(struct pri *pri, int pfbit)
{
	q921_h h;
	Q921_INIT(pri, h);
	h.u.m3 = 3;		/* M3 = 3 */
	h.u.m2 = 0;		/* M2 = 0 */
	h.u.p_f = pfbit;	/* Final bit on */
	h.u.ft = Q921_FRAMETYPE_U;
	switch(pri->localtype) {
	case PRI_NETWORK:
		h.h.c_r = 0;
		break;
	case PRI_CPE:
		h.h.c_r = 1;
		break;
	default:
		pri_error(pri, "Don't know how to U/A on a type %d node\n", pri->localtype);
		return;
	}
	if (pri->debug & (PRI_DEBUG_Q921_STATE | PRI_DEBUG_Q921_DUMP))
		pri_message(pri, "Sending Unnumbered Acknowledgement\n");
	q921_transmit(pri, &h, 3);
}

static void q921_send_sabme_now(void *vpri);

static void q921_send_sabme(void *vpri, int now)
{
	struct pri *pri = vpri;
	q921_h h;
	pri_schedule_del(pri, pri->sabme_timer);
	pri->sabme_timer = 0;
	pri->sabme_timer = pri_schedule_event(pri, pri->timers[PRI_TIMER_T200], q921_send_sabme_now, pri);
	if (!now)
		return;
	Q921_INIT(pri, h);
	h.u.m3 = 3;	/* M3 = 3 */
	h.u.m2 = 3;	/* M2 = 3 */
	h.u.p_f = 1;	/* Poll bit set */
	h.u.ft = Q921_FRAMETYPE_U;
	switch(pri->localtype) {
	case PRI_NETWORK:
		h.h.c_r = 1;
		break;
	case PRI_CPE:
		h.h.c_r = 0;
		break;
	default:
		pri_error(pri, "Don't know how to U/A on a type %d node\n", pri->localtype);
		return;
	}
	if (pri->bri && (pri->state == Q921_AWAITING_ESTABLISH)) {
		if (pri->sabme_count >= pri->timers[PRI_TIMER_N200]) {
			pri_schedule_del(pri, pri->sabme_timer);
			pri->sabme_timer = 0;
			q921_tei_release_and_reacquire(pri->master);
		} else {
			pri->sabme_count++;
		}
	}
	if (pri->debug & (PRI_DEBUG_Q921_STATE | PRI_DEBUG_Q921_DUMP))
		pri_message(pri, "Sending Set Asynchronous Balanced Mode Extended\n");
	q921_transmit(pri, &h, 3);
	if (pri->debug & PRI_DEBUG_Q921_STATE && pri->q921_state != Q921_AWAITING_ESTABLISH)
		pri_message(pri, DBGHEAD "q921_state now is Q921_AWAITING_ESTABLISH\n", DBGINFO);
	pri->q921_state = Q921_AWAITING_ESTABLISH;
}

static void q921_send_sabme_now(void *vpri)
{
	q921_send_sabme(vpri, 1);
}

static int q921_ack_packet(struct pri *pri, int num)
{
	struct q921_frame *f, *prev = NULL;
	f = pri->txqueue;
	while(f) {
		if (f->h.n_s == num) {
			/* Cancel each packet as necessary */
			/* That's our packet */
			if (prev)
				prev->next = f->next;
			else
				pri->txqueue = f->next;
			if (pri->debug & PRI_DEBUG_Q921_DUMP)
				pri_message(pri, "-- ACKing packet %d, new txqueue is %d (-1 means empty)\n", f->h.n_s, pri->txqueue ? pri->txqueue->h.n_s : -1);
			/* Update v_a */
			pri->v_a = num;
			free(f);
			/* Reset retransmission counter if we actually acked something */
			pri->retrans = 0;
			/* Decrement window size */
			pri->windowlen--;
			/* Search for something to send */
			f = pri->txqueue;
			while(f) {
				if (!f->transmitted) {
					/* Send it now... */
					if (pri->debug & PRI_DEBUG_Q921_DUMP)
						pri_message(pri, "-- Finally transmitting %d, since window opened up\n", f->h.n_s);
					f->transmitted++;
					pri->windowlen++;
					f->h.n_r = pri->v_r;
					q921_transmit(pri, (q921_h *)(&f->h), f->len);
					break;
				}
				f = f->next;
			}
			return 1;
		}
		prev = f;
		f = f->next;
	}
	return 0;
}

static void t203_expire(void *);
static void t200_expire(void *);
static pri_event *q921_dchannel_down(struct pri *pri);

static void reschedule_t203(struct pri *pri)
{
	if (pri->t203_timer) {
		pri_schedule_del(pri, pri->t203_timer);
		if (pri->debug & PRI_DEBUG_Q921_DUMP)
			pri_message(pri, "-- Restarting T203 counter\n");
		/* Nothing to transmit, start the T203 counter instead */
		pri->t203_timer = pri_schedule_event(pri, pri->timers[PRI_TIMER_T203], t203_expire, pri);
	}
}

static pri_event *q921_ack_rx(struct pri *pri, int ack)
{
	int x;
	int cnt=0;
	pri_event *ev;
	/* Make sure the ACK was within our window */
	for (x=pri->v_a; (x != pri->v_s) && (x != ack); Q921_INC(x));
	if (x != ack) {
		/* ACK was outside of our window --- ignore */
		pri_error(pri, "ACK received for '%d' outside of window of '%d' to '%d', restarting\n", ack, pri->v_a, pri->v_s);
		ev = q921_dchannel_down(pri);
		q921_start(pri, 1);
		pri->schedev = 1;
		return ev;
	}
	/* Cancel each packet as necessary */
	if (pri->debug & PRI_DEBUG_Q921_DUMP)
		pri_message(pri, "-- ACKing all packets from %d to (but not including) %d\n", pri->v_a, ack);
	for (x=pri->v_a; x != ack; Q921_INC(x)) 
		cnt += q921_ack_packet(pri, x);	
	if (!pri->txqueue) {
		if (pri->debug & PRI_DEBUG_Q921_DUMP)
			pri_message(pri, "-- Since there was nothing left, stopping T200 counter\n");
		/* Something was ACK'd.  Stop T200 counter */
		pri_schedule_del(pri, pri->t200_timer);
		pri->t200_timer = 0;
	}
	if (pri->t203_timer) {
		if (pri->debug & PRI_DEBUG_Q921_DUMP)
			pri_message(pri, "-- Stopping T203 counter since we got an ACK\n");
		pri_schedule_del(pri, pri->t203_timer);
		pri->t203_timer = 0;
	}
	if (pri->txqueue) {
		/* Something left to transmit, Start the T200 counter again if we stopped it */
		if (pri->debug & PRI_DEBUG_Q921_DUMP)
			pri_message(pri, "-- Something left to transmit (%d), restarting T200 counter\n", pri->txqueue->h.n_s);
		if (!pri->t200_timer)
			pri->t200_timer = pri_schedule_event(pri, pri->timers[PRI_TIMER_T200], t200_expire, pri);
	} else {
		if (pri->debug & PRI_DEBUG_Q921_DUMP)
			pri_message(pri, "-- Nothing left, starting T203 counter\n");
		/* Nothing to transmit, start the T203 counter instead */
		pri->t203_timer = pri_schedule_event(pri, pri->timers[PRI_TIMER_T203], t203_expire, pri);
	}
	return NULL;
}

static void q921_reject(struct pri *pri, int pf)
{
	q921_h h;
	Q921_INIT(pri, h);
	h.s.x0 = 0;	/* Always 0 */
	h.s.ss = 2;	/* Reject */
	h.s.ft = 1;	/* Frametype (01) */
	h.s.n_r = pri->v_r;	/* Where to start retransmission */
	h.s.p_f = pf;	
	switch(pri->localtype) {
	case PRI_NETWORK:
		h.h.c_r = 0;
		break;
	case PRI_CPE:
		h.h.c_r = 1;
		break;
	default:
		pri_error(pri, "Don't know how to U/A on a type %d node\n", pri->localtype);
		return;
	}
	if (pri->debug & PRI_DEBUG_Q921_DUMP)
		pri_message(pri, "Sending Reject (%d)\n", pri->v_r);
	pri->sentrej = 1;
	q921_transmit(pri, &h, 4);
}

static void q921_rr(struct pri *pri, int pbit, int cmd) {
	q921_h h;
	Q921_INIT(pri, h);
	h.s.x0 = 0;	/* Always 0 */
	h.s.ss = 0; /* Receive Ready */
	h.s.ft = 1;	/* Frametype (01) */
	h.s.n_r = pri->v_r;	/* N/R */
	h.s.p_f = pbit;		/* Poll/Final set appropriately */
	switch(pri->localtype) {
	case PRI_NETWORK:
		if (cmd)
			h.h.c_r = 1;
		else
			h.h.c_r = 0;
		break;
	case PRI_CPE:
		if (cmd)
			h.h.c_r = 0;
		else
			h.h.c_r = 1;
		break;
	default:
		pri_error(pri, "Don't know how to U/A on a type %d node\n", pri->localtype);
		return;
	}
	pri->v_na = pri->v_r;	/* Make a note that we've already acked this */
	if (pri->debug & PRI_DEBUG_Q921_DUMP)
		pri_message(pri, "Sending Receiver Ready (%d)\n", pri->v_r);
	q921_transmit(pri, &h, 4);
}

static void t200_expire(void *vpri)
{
	struct pri *pri = vpri;

	if (pri->txqueue) {
		/* Retransmit first packet in the queue, setting the poll bit */
		if (pri->debug & PRI_DEBUG_Q921_DUMP)
			pri_message(pri, "-- T200 counter expired, What to do...\n");
		/* Force Poll bit */
		pri->txqueue->h.p_f = 1;	
		/* Update nr */
		pri->txqueue->h.n_r = pri->v_r;
		pri->v_na = pri->v_r;
		pri->solicitfbit = 1;
		pri->retrans++;
		/* Up to three retransmissions */
		if (pri->retrans < pri->timers[PRI_TIMER_N200]) {
			/* Reschedule t200_timer */
			if (pri->debug & PRI_DEBUG_Q921_DUMP)
				pri_message(pri, "-- Retransmitting %d bytes\n", pri->txqueue->len);
			if (pri->busy) 
				q921_rr(pri, 1, 1);
			else {
				if (!pri->txqueue->transmitted) 
					pri_error(pri, "!! Not good - head of queue has not been transmitted yet\n");
				q921_transmit(pri, (q921_h *)&pri->txqueue->h, pri->txqueue->len);
			}
			if (pri->debug & PRI_DEBUG_Q921_DUMP)
			      pri_message(pri, "-- Rescheduling retransmission (%d)\n", pri->retrans);
			pri->t200_timer = pri_schedule_event(pri, pri->timers[PRI_TIMER_T200], t200_expire, pri);
		} else {
			if (pri->debug & PRI_DEBUG_Q921_STATE)
			      pri_message(pri, "-- Timeout occured, restarting PRI\n");
			if (pri->debug & PRI_DEBUG_Q921_STATE && pri->q921_state != Q921_LINK_CONNECTION_RELEASED)
			     pri_message(pri, DBGHEAD "q921_state now is Q921_LINK_CONNECTION_RELEASED\n",DBGINFO);
			pri->q921_state = Q921_LINK_CONNECTION_RELEASED;
			     pri->t200_timer = 0;
			q921_dchannel_down(pri);
			q921_start(pri, 1);
			pri->schedev = 1;
		}
	} else if (pri->solicitfbit) {
		if (pri->debug & PRI_DEBUG_Q921_DUMP)
			pri_message(pri, "-- Retrying poll with f-bit\n");
		pri->retrans++;
		if (pri->retrans < pri->timers[PRI_TIMER_N200]) {
			pri->solicitfbit = 1;
			q921_rr(pri, 1, 1);
			pri->t200_timer = pri_schedule_event(pri, pri->timers[PRI_TIMER_T200], t200_expire, pri);
		} else {
			if (pri->debug & PRI_DEBUG_Q921_STATE)
				pri_message(pri, "-- Timeout occured, restarting PRI\n");
			if (pri->debug & PRI_DEBUG_Q921_STATE && pri->q921_state != Q921_LINK_CONNECTION_RELEASED)
				pri_message(pri, DBGHEAD "q921_state now is Q921_LINK_CONNECTION_RELEASED\n", DBGINFO);
			pri->q921_state = Q921_LINK_CONNECTION_RELEASED;
			pri->t200_timer = 0;
			if (pri->bri && pri->master) {
				q921_tei_release_and_reacquire(pri->master);
				return;
			} else {
				q921_dchannel_down(pri);
				q921_start(pri, 1);
				pri->schedev = 1;
			}
		}
	} else {
		pri_error(pri, "T200 counter expired, nothing to send...\n");
	   	pri->t200_timer = 0;
	}
}

int q921_transmit_iframe(struct pri *pri, void *buf, int len, int cr)
{
	q921_frame *f, *prev=NULL;

	for (f=pri->txqueue; f; f = f->next) prev = f;
	f = calloc(1, sizeof(q921_frame) + len + 2);
	if (f) {
		Q921_INIT(pri, f->h);
		switch(pri->localtype) {
		case PRI_NETWORK:
			if (cr)
				f->h.h.c_r = 1;
			else
				f->h.h.c_r = 0;
		break;
		case PRI_CPE:
			if (cr)
				f->h.h.c_r = 0;
			else
				f->h.h.c_r = 1;
		break;
		}
		f->next = NULL;
		f->transmitted = 0;
		f->len = len + 4;
		memcpy(f->h.data, buf, len);
		f->h.n_s = pri->v_s;
		f->h.n_r = pri->v_r;
		pri->v_s++;
		pri->v_na = pri->v_r;
		f->h.p_f = 0;
		f->h.ft = 0;
		if (prev)
			prev->next = f;
		else
			pri->txqueue = f;
		/* Immediately transmit unless we're in a recovery state, or the window
		   size is too big */
		if (!pri->retrans && !pri->busy) {
			if (pri->windowlen < pri->window) {
				pri->windowlen++;
				q921_transmit(pri, (q921_h *)(&f->h), f->len);
				f->transmitted++;
			} else {
				if (pri->debug & PRI_DEBUG_Q921_DUMP)
					pri_message(pri, "Delaying transmission of %d, window is %d/%d long\n", 
						f->h.n_s, pri->windowlen, pri->window);
			}
		}
		if (pri->t203_timer) {
			if (pri->debug & PRI_DEBUG_Q921_DUMP)
				pri_message(pri, "Stopping T_203 timer\n");
			pri_schedule_del(pri, pri->t203_timer);
			pri->t203_timer = 0;
		}
		if (!pri->t200_timer) {
			if (pri->debug & PRI_DEBUG_Q921_DUMP)
				pri_message(pri, "Starting T_200 timer\n");
			pri->t200_timer = pri_schedule_event(pri, pri->timers[PRI_TIMER_T200], t200_expire, pri);
		} else
			if (pri->debug & PRI_DEBUG_Q921_DUMP)
				pri_message(pri, "T_200 timer already going (%d)\n", pri->t200_timer);
		
	} else {
		pri_error(pri, "!! Out of memory for Q.921 transmit\n");
		return -1;
	}
	return 0;
}

static void t203_expire(void *vpri)
{
	struct pri *pri = vpri;
	if (pri->q921_state == Q921_LINK_CONNECTION_ESTABLISHED) {
		if (pri->debug & PRI_DEBUG_Q921_DUMP)
			pri_message(pri, "T203 counter expired, sending RR and scheduling T203 again\n");
		/* Solicit an F-bit in the other's RR */
		pri->solicitfbit = 1;
		pri->retrans = 0;
		q921_rr(pri, 1, 1);
		/* Start timer T200 to resend our RR if we don't get it */
		pri->t203_timer = pri_schedule_event(pri, pri->timers[PRI_TIMER_T200], t200_expire, pri);
	} else {
		if (pri->debug & PRI_DEBUG_Q921_DUMP)
			pri_message(pri, "T203 counter expired in weird state %d\n", pri->q921_state);
		pri->t203_timer = 0;
	}
}

static pri_event *q921_handle_iframe(struct pri *pri, q921_i *i, int len)
{
	int res;
	pri_event *ev;
	/* Make sure this is a valid packet */
	if (i->n_s == pri->v_r) {
		/* Increment next expected I-frame */
		Q921_INC(pri->v_r);
		/* Handle their ACK */
		pri->sentrej = 0;
		ev = q921_ack_rx(pri, i->n_r);
		if (ev)
			return ev;
		if (i->p_f) {
			/* If the Poll/Final bit is set, immediate send the RR */
			q921_rr(pri, 1, 0);
		} else if (pri->busy) {
			q921_rr(pri, 0, 0);
		}
		/* Receive Q.931 data */
		res = q931_receive(pri, (q931_h *)i->data, len - 4);
		/* Send an RR if one wasn't sent already */
		if (pri->v_na != pri->v_r) 
			q921_rr(pri, 0, 0);
		if (res == -1) {
			return NULL;
		}
		if (res & Q931_RES_HAVEEVENT)
			return &pri->ev;
	} else {
		/* If we haven't already sent a reject, send it now, otherwise
		   we are obliged to RR */
		if (!pri->sentrej)
			q921_reject(pri, i->p_f);
		else if (i->p_f)
			q921_rr(pri, 1, 0);
	}
	return NULL;
}

void q921_dump(struct pri *pri, q921_h *h, int len, int showraw, int txrx)
{
	int x;
        char *type;
	char direction_tag;
	
	direction_tag = txrx ? '>' : '<';
	if (showraw) {
		char *buf = malloc(len * 3 + 1);
		int buflen = 0;
		if (buf) {
			for (x=0;x<len;x++) 
				buflen += sprintf(buf + buflen, "%02x ", h->raw[x]);
			pri_message(pri, "\n%c [ %s]\n", direction_tag, buf);
			free(buf);
		}
	}

	switch (h->h.data[0] & Q921_FRAMETYPE_MASK) {
	case 0:
	case 2:
		pri_message(pri, "\n%c Informational frame:\n", direction_tag);
		break;
	case 1:
		pri_message(pri, "\n%c Supervisory frame:\n", direction_tag);
		break;
	case 3:
		pri_message(pri, "\n%c Unnumbered frame:\n", direction_tag);
		break;
	}
	
	pri_message(pri, 
"%c SAPI: %02d  C/R: %d EA: %d\n"
"%c  TEI: %03d        EA: %d\n", 
    	direction_tag,
	h->h.sapi, 
	h->h.c_r,
	h->h.ea1,
    	direction_tag,
	h->h.tei,
	h->h.ea2);
	switch (h->h.data[0] & Q921_FRAMETYPE_MASK) {
	case 0:
	case 2:
		/* Informational frame */
		pri_message(pri, 
"%c N(S): %03d   0: %d\n"
"%c N(R): %03d   P: %d\n"
"%c %d bytes of data\n",
    	direction_tag,
	h->i.n_s,
	h->i.ft,
    	direction_tag,
	h->i.n_r,
	h->i.p_f, 
    	direction_tag,
	len - 4);
		break;
	case 1:
		/* Supervisory frame */
		type = "???";
		switch (h->s.ss) {
		case 0:
			type = "RR (receive ready)";
			break;
		case 1:
			type = "RNR (receive not ready)";
			break;
		case 2:
			type = "REJ (reject)";
			break;
		}
		pri_message(pri, 
"%c Zero: %d     S: %d 01: %d  [ %s ]\n"
"%c N(R): %03d P/F: %d\n"
"%c %d bytes of data\n",
    	direction_tag,
	h->s.x0,
	h->s.ss,
	h->s.ft,
	type,
	direction_tag,
	h->s.n_r,
	h->s.p_f, 
	direction_tag,
	len - 4);
		break;
	case 3:		
		/* Unnumbered frame */
		type = "???";
		if (h->u.ft == 3) {
			switch (h->u.m3) {
			case 0:
				if (h->u.m2 == 3)
					type = "DM (disconnect mode)";
				else if (h->u.m2 == 0)
					type = "UI (unnumbered information)";
				break;
			case 2:
				if (h->u.m2 == 0)
					type = "DISC (disconnect)";
				break;
			case 3:
			       	if (h->u.m2 == 3)
					type = "SABME (set asynchronous balanced mode extended)";
				else if (h->u.m2 == 0)
					type = "UA (unnumbered acknowledgement)";
				break;
			case 4:
				if (h->u.m2 == 1)
					type = "FRMR (frame reject)";
				break;
			case 5:
				if (h->u.m2 == 3)
					type = "XID (exchange identification note)";
				break;
			}
		}
		pri_message(pri, 
"%c   M3: %d   P/F: %d M2: %d 11: %d  [ %s ]\n"
"%c %d bytes of data\n",
	direction_tag,
	h->u.m3,
	h->u.p_f,
	h->u.m2,
	h->u.ft,
	type,
	direction_tag,
	len - 3);
		break;
	};
}

static pri_event *q921_dchannel_up(struct pri *pri)
{
	/* Reset counters, etc */
	q921_reset(pri);
	
	/* Stop any SABME retransmissions */
	if (pri->sabme_timer) {
		pri_schedule_del(pri, pri->sabme_timer);
		pri->sabme_timer = 0;
	}
	
	/* Reset any rejects */
	pri->sentrej = 0;
	
	/* Go into connection established state */
	if (pri->debug & PRI_DEBUG_Q921_STATE && pri->q921_state != Q921_LINK_CONNECTION_ESTABLISHED)
		pri_message(pri, DBGHEAD "q921_state now is Q921_LINK_CONNECTION_ESTABLISHED\n", DBGINFO);
	pri->q921_state = Q921_LINK_CONNECTION_ESTABLISHED;

	/* Start the T203 timer */
	pri->t203_timer = pri_schedule_event(pri, pri->timers[PRI_TIMER_T203], t203_expire, pri);
	
	/* Notify Layer 3 */
	q931_dl_indication(pri, PRI_EVENT_DCHAN_UP);

	/* Report event that D-Channel is now up */
	pri->ev.gen.e = PRI_EVENT_DCHAN_UP;
	return &pri->ev;
}

static pri_event *q921_dchannel_down(struct pri *pri)
{
	/* Reset counters, reset sabme timer etc */
	q921_reset(pri);
	
	/* Notify Layer 3 */
	q931_dl_indication(pri, PRI_EVENT_DCHAN_DOWN);

	/* Report event that D-Channel is now up */
	pri->ev.gen.e = PRI_EVENT_DCHAN_DOWN;
	return &pri->ev;
}

void q921_reset(struct pri *pri)
{
	/* Having gotten a SABME we MUST reset our entire state */
	pri->v_s = 0;
	pri->v_a = 0;
	pri->v_r = 0;
	pri->v_na = 0;
	pri->window = pri->timers[PRI_TIMER_K];
	pri->windowlen = 0;
	if (pri->sabme_timer)
		pri_schedule_del(pri, pri->sabme_timer);
	if (pri->t203_timer)
		pri_schedule_del(pri, pri->t203_timer);
	if (pri->t200_timer)
		pri_schedule_del(pri, pri->t200_timer);
	pri->sabme_timer = 0;
	pri->sabme_count = 0;
	pri->t203_timer = 0;
	pri->t200_timer = 0;
	pri->busy = 0;
	pri->solicitfbit = 0;
	if (pri->debug & PRI_DEBUG_Q921_STATE && pri->q921_state != Q921_LINK_CONNECTION_RELEASED)
		pri_message(pri, DBGHEAD "q921_state now is Q921_LINK_CONNECTION_RELEASED\n", DBGINFO);
	pri->q921_state = Q921_LINK_CONNECTION_RELEASED;
	pri->retrans = 0;
	pri->sentrej = 0;
	
	/* Discard anything waiting to go out */
	q921_discard_retransmissions(pri);
}

static void q921_tei_release_and_reacquire(struct pri *master)
{
	/* Make sure the master is passed into this function */
	q921_dchannel_down(master->subchannel);
	__pri_free_tei(master->subchannel);
	master->subchannel = NULL;
	master->ev.gen.e = PRI_EVENT_DCHAN_DOWN;
	master->schedev = 1;
	q921_start(master, master->localtype == PRI_CPE);
}

static pri_event *q921_receive_MDL(struct pri *pri, q921_u *h, int len)
{
	int ri;
	struct pri *sub;
	int tei;
	if (pri->debug & PRI_DEBUG_Q921_STATE)
		pri_message(pri, "Received MDL message\n");
	if (h->data[0] != 0x0f) {
		pri_error(pri, "Received MDL with unsupported management entity %02x\n", h->data[0]);
		return NULL;
	}
	if (!(h->data[4] & 0x01)) {
		pri_error(pri, "Received MDL with multibyte TEI identifier\n");
		return NULL;
	}
	ri = (h->data[1] << 8) | h->data[2];
	tei = (h->data[4] >> 1);
	switch(h->data[3]) {
	case Q921_TEI_IDENTITY_REQUEST:
		if (tei != 127) {
			pri_error(pri, "Received TEI identity request with invalid TEI %d\n", tei);
			q921_send_tei(pri, Q921_TEI_IDENTITY_DENIED, ri, tei, 1);
		}
		/* Go to master */
		for (sub = pri; sub->master; sub = sub->master);
		tei = 64;
		while(sub->subchannel) {
			if(sub->subchannel->tei == tei)
				++tei;
		}
		sub->subchannel = __pri_new_tei(-1, pri->localtype, pri->switchtype, pri, NULL, NULL, NULL, tei, 1);
		if (!sub->subchannel) {
			pri_error(pri, "Unable to allocate D-channel for new TEI %d\n", tei);
			return NULL;
		}
		q921_send_tei(pri, Q921_TEI_IDENTITY_ASSIGNED, ri, tei, 1);
		break;
	case Q921_TEI_IDENTITY_ASSIGNED:
		if (ri != pri->ri) {
			pri_message(pri, "TEI assignment received for invalid Ri %02x (our is %02x)\n", ri, pri->ri);
			return NULL;
		}
		if (pri->t202_timer) {
			pri_schedule_del(pri, pri->t202_timer);
			pri->t202_timer = 0;
		}
		if (pri->subchannel && (pri->subchannel->tei == tei)) {
			pri_error(pri, "TEI already assigned (new is %d, current is %d)\n", tei, pri->subchannel->tei);
			q921_tei_release_and_reacquire(pri);
			return NULL;
		}

		pri_message(pri, "TEI assiged to %d\n", tei);
		pri->subchannel = __pri_new_tei(-1, pri->localtype, pri->switchtype, pri, NULL, NULL, NULL, tei, 1);
		if (!pri->subchannel) {
			pri_error(pri, "Unable to allocate D-channel for new TEI %d\n", tei);
			return NULL;
		}
		pri->q921_state = Q921_TEI_ASSIGNED;
		break;
	case Q921_TEI_IDENTITY_CHECK_REQUEST:
		/* We're assuming one TEI per PRI in TE PTMP mode */

		/* If no subchannel (TEI) ignore */
		if (!pri->subchannel)
			return NULL;

		/* If it's addressed to the group TEI or to our TEI specifically, we respond */
		if ((tei == Q921_TEI_GROUP) || (tei == pri->subchannel->tei))
			q921_send_tei(pri, Q921_TEI_IDENTITY_CHECK_RESPONSE, random() % 65535, pri->subchannel->tei, 1);

		break;
	case Q921_TEI_IDENTITY_REMOVE:
		/* XXX: Assuming multiframe mode has been disconnected already */
		if (!pri->subchannel)
			return NULL;

		if ((tei == Q921_TEI_GROUP) || (tei == pri->subchannel->tei)) {
			q921_tei_release_and_reacquire(pri);
		}
	}
	return NULL;	/* Do we need to return something??? */
}

static pri_event *__q921_receive_qualified(struct pri *pri, q921_h *h, int len)
{
	q921_frame *f;
	pri_event *ev;
	int sendnow;

	switch(h->h.data[0] & Q921_FRAMETYPE_MASK) {
	case 0:
	case 2:
		if (pri->q921_state != Q921_LINK_CONNECTION_ESTABLISHED) {
			pri_error(pri, "!! Got I-frame while link state %d\n", pri->q921_state);
			return NULL;
		}
		/* Informational frame */
		if (len < 4) {
			pri_error(pri, "!! Received short I-frame (expected 4, got %d)\n", len);
			break;
		}
		return q921_handle_iframe(pri, &h->i, len);	
		break;
	case 1:
		if (pri->q921_state != Q921_LINK_CONNECTION_ESTABLISHED) {
			pri_error(pri, "!! Got S-frame while link down\n");
			return NULL;
		}
		if (len < 4) {
			pri_error(pri, "!! Received short S-frame (expected 4, got %d)\n", len);
			break;
		}
		switch(h->s.ss) {
		case 0:
			/* Receiver Ready */
			pri->busy = 0;
			/* Acknowledge frames as necessary */
			ev = q921_ack_rx(pri, h->s.n_r);
			if (ev)
				return ev;
			if (h->s.p_f) {
				/* If it's a p/f one then send back a RR in return with the p/f bit set */
				if (pri->solicitfbit) {
					if (pri->debug & PRI_DEBUG_Q921_DUMP)
						pri_message(pri, "-- Got RR response to our frame\n");
				} else {
					if (pri->debug & PRI_DEBUG_Q921_DUMP)
						pri_message(pri, "-- Unsolicited RR with P/F bit, responding\n");
						q921_rr(pri, 1, 0);
				}
				pri->solicitfbit = 0;
			}
			break;
 		case 1:
 			/* Receiver not ready */
 			if (pri->debug & PRI_DEBUG_Q921_STATE)
 				pri_message(pri, "-- Got receiver not ready\n");
 			if(h->s.p_f) {
 				/* Send RR if poll bit set */
 				q921_rr(pri, h->s.p_f, 0);
 			}
 			pri->busy = 1;
 			break;   
 		case 2:
 			/* Just retransmit */
 			if (pri->debug & PRI_DEBUG_Q921_STATE)
 				pri_message(pri, "-- Got reject requesting packet %d...  Retransmitting.\n", h->s.n_r);
 			if (h->s.p_f) {
 				/* If it has the poll bit set, send an appropriate supervisory response */
 				q921_rr(pri, 1, 0);
 			}
 			sendnow = 0;
 			/* Resend the proper I-frame */
 			for(f=pri->txqueue;f;f=f->next) {
 				if ((sendnow || (f->h.n_s == h->s.n_r)) && f->transmitted) {
 					/* Matches the request, or follows in our window, and has
 					   already been transmitted. */
 					sendnow = 1;
 					pri_error(pri, "!! Got reject for frame %d, retransmitting frame %d now, updating n_r!\n", h->s.n_r, f->h.n_s);
 					f->h.n_r = pri->v_r;
 					q921_transmit(pri, (q921_h *)(&f->h), f->len);
 				}
 			}
 			if (!sendnow) {
 				if (pri->txqueue) {
 					/* This should never happen */
 					if (!h->s.p_f || h->s.n_r) {
 						pri_error(pri, "!! Got reject for frame %d, but we only have others!\n", h->s.n_r);
 					}
 				} else {
 					/* Hrm, we have nothing to send, but have been REJ'd.  Reset v_a, v_s, etc */
 					pri_error(pri, "!! Got reject for frame %d, but we have nothing -- resetting!\n", h->s.n_r);
 					pri->v_a = h->s.n_r;
 					pri->v_s = h->s.n_r;
 					/* Reset t200 timer if it was somehow going */
 					if (pri->t200_timer) {
 						pri_schedule_del(pri, pri->t200_timer);
 						pri->t200_timer = 0;
 					}
 					/* Reset and restart t203 timer */
 					if (pri->t203_timer)
 						pri_schedule_del(pri, pri->t203_timer);
 					pri->t203_timer = pri_schedule_event(pri, pri->timers[PRI_TIMER_T203], t203_expire, pri);
 				}
 			}
 			break;
		default:
			pri_error(pri, "!! XXX Unknown Supervisory frame ss=0x%02x,pf=%02xnr=%02x vs=%02x, va=%02x XXX\n", h->s.ss, h->s.p_f, h->s.n_r,
					pri->v_s, pri->v_a);
		}
		break;
	case 3:
		if (len < 3) {
			pri_error(pri, "!! Received short unnumbered frame\n");
			break;
		}
		switch(h->u.m3) {
		case 0:
			if (h->u.m2 == 3) {
				if (h->u.p_f) {
					/* Section 5.7.1 says we should restart on receiving a DM response with the f-bit set to
					   one, but we wait T200 first */
					if (pri->debug & (PRI_DEBUG_Q921_STATE | PRI_DEBUG_Q921_DUMP))
						pri_message(pri, "-- Got DM Mode from peer.\n");
					/* Disconnected mode, try again after T200 */
					ev = q921_dchannel_down(pri);
					q921_restart(pri, 0);
					return ev;
						
				} else {
					if (pri->debug & PRI_DEBUG_Q921_DUMP)
						pri_message(pri, "-- Ignoring unsolicited DM with p/f set to 0\n");
#if 0
					/* Requesting that we start */
					q921_restart(pri, 0);
#endif					
				}
				break;
			} else if (!h->u.m2) {
				if ((pri->sapi == Q921_SAPI_LAYER2_MANAGEMENT) && (pri->tei == Q921_TEI_GROUP))
					q921_receive_MDL(pri, (q921_u *)h, len);
				else {
					int res;

					res = q931_receive(pri, (q931_h *) h->u.data, len - 3);
					if (res == -1) {
						return NULL;
					}
					if (res & Q931_RES_HAVEEVENT)
						return &pri->ev;
				}
			}
			break;
		case 2:
			if (pri->debug & (PRI_DEBUG_Q921_STATE | PRI_DEBUG_Q921_DUMP))
				pri_message(pri, "-- Got Disconnect from peer.\n");
			/* Acknowledge */
			q921_send_ua(pri, h->u.p_f);
			ev = q921_dchannel_down(pri);
			q921_restart(pri, 0);
			return ev;
		case 3:
			if (h->u.m2 == 3) {
				/* SABME */
				if (pri->debug & (PRI_DEBUG_Q921_STATE | PRI_DEBUG_Q921_DUMP)) {
					pri_message(pri, "-- Got SABME from %s peer.\n", h->h.c_r ? "network" : "cpe");
				}
				if (h->h.c_r) {
					pri->remotetype = PRI_NETWORK;
					if (pri->localtype == PRI_NETWORK) {
						/* We can't both be networks */
						return pri_mkerror(pri, "We think we're the network, but they think they're the network, too.");
					}
				} else {
					pri->remotetype = PRI_CPE;
					if (pri->localtype == PRI_CPE) {
						/* We can't both be CPE */
						return pri_mkerror(pri, "We think we're the CPE, but they think they're the CPE too.\n");
					}
				}
				/* Send Unnumbered Acknowledgement */
				q921_send_ua(pri, h->u.p_f);
				return q921_dchannel_up(pri);
			} else if (h->u.m2 == 0) {
					/* It's a UA */
				if (pri->q921_state == Q921_AWAITING_ESTABLISH) {
					if (pri->debug & (PRI_DEBUG_Q921_STATE | PRI_DEBUG_Q921_DUMP)) {
						pri_message(pri, "-- Got UA from %s peer  Link up.\n", h->h.c_r ? "cpe" : "network");
					}
					return q921_dchannel_up(pri);
				} else if ((pri->q921_state >= Q921_TEI_ASSIGNED) && pri->bri) {
					/* Possible duplicate TEI assignment */
					if (pri->master)
						q921_tei_release_and_reacquire(pri->master);
					else
						pri_error(pri, "Huh!? no master found\n");
				} else {
					/* Since we're not in the AWAITING_ESTABLISH STATE, it's unsolicited */
					pri_error(pri, "!! Got a UA, but i'm in state %d\n", pri->q921_state);

				}
			} else 
				pri_error(pri, "!! Weird frame received (m3=3, m2 = %d)\n", h->u.m2);
			break;
		case 4:
			pri_error(pri, "!! Frame got rejected!\n");
			break;
		case 5:
			pri_error(pri, "!! XID frames not supported\n");
			break;
		default:
			pri_error(pri, "!! Don't know what to do with M3=%d u-frames\n", h->u.m3);
		}
		break;
				
	}
	return NULL;
}

static pri_event *__q921_receive(struct pri *pri, q921_h *h, int len)
{
	pri_event *ev;
	/* Discard FCS */
	len -= 2;
	
	if (!pri->master && pri->debug & (PRI_DEBUG_Q921_DUMP | PRI_DEBUG_Q921_RAW))
		q921_dump(pri, h, len, pri->debug & PRI_DEBUG_Q921_RAW, 0);

	/* Check some reject conditions -- Start by rejecting improper ea's */
	if (h->h.ea1 || !(h->h.ea2))
		return NULL;

#if 0 /* Will be rejected by subchannel analyzis */
	/* Check for broadcasts - not yet handled */
	if (h->h.tei == Q921_TEI_GROUP)
		return NULL;
#endif

	if (!((h->h.sapi == pri->sapi) && ((h->h.tei == pri->tei) || (h->h.tei == Q921_TEI_GROUP)))) {
		/* Check for SAPIs we don't yet handle */
		/* If it's not us, try any subchannels we have */
		if (pri->subchannel)
			return q921_receive(pri->subchannel, h, len + 2);
		else {
			return NULL;
		}

	}
	if (pri->debug & PRI_DEBUG_Q921_DUMP)
		pri_message(pri, "Handling message for SAPI/TEI=%d/%d\n", h->h.sapi, h->h.tei);
	ev = __q921_receive_qualified(pri, h, len);
	reschedule_t203(pri);
	return ev;
}

pri_event *q921_receive(struct pri *pri, q921_h *h, int len)
{
	pri_event *e;
	e = __q921_receive(pri, h, len);
#ifdef LIBPRI_COUNTERS
	pri->q921_rxcount++;
#endif
	return e;
}

static void q921_restart(struct pri *pri, int now)
{
	if (pri->q921_state != Q921_LINK_CONNECTION_RELEASED) {
		pri_error(pri, "!! q921_start: Not in 'Link Connection Released' state\n");
		return;
	}
	/* Reset our interface */
	q921_reset(pri);
	/* Do the SABME XXX Maybe we should implement T_WAIT? XXX */
	q921_send_sabme(pri, now);
}

void q921_start(struct pri *pri, int isCPE)
{
	q921_reset(pri);
	if ((pri->sapi == Q921_SAPI_LAYER2_MANAGEMENT) && (pri->tei == Q921_TEI_GROUP)) {
		pri->q921_state = Q921_DOWN;
		if (isCPE)
			q921_tei_request(pri);
	} else {
		q921_send_sabme(pri, isCPE);
	}
}
