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


/*
 * This program tests libpri call reception using a zaptel interface.
 * Its state machines are setup for RECEIVING CALLS ONLY, so if you
 * are trying to both place and receive calls you have to a bit more.
 */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/signal.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <zaptel/zaptel.h>
#ifndef SOLARIS
#include <zap.h>
#endif
#include <pthread.h>
#include <sys/select.h>
#include "libpri.h"
#include "pri_q931.h"

#ifndef AF_LOCAL
#define AF_LOCAL AF_UNIX
#endif

#define DEBUG_LEVEL	PRI_DEBUG_ALL

#define PRI_DEF_NODETYPE	PRI_CPE
#define PRI_DEF_SWITCHTYPE	PRI_SWITCH_NI2

static struct pri *first, *cur;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

#define TEST_CALLS 1

static void event1(struct pri *pri, pri_event *e)
{
	/* Network */
	int x;
	static q931_call *calls[TEST_CALLS];
	char name[256], num[256], dest[256];
	switch(e->gen.e) {
	case PRI_EVENT_DCHAN_UP:
		printf("Network is up.  Sending blast of calls!\n");
		for (x=0;x<TEST_CALLS;x++) {
			sprintf(name, "Caller %d", x + 1);
			sprintf(num, "25642860%02d", x+1);
			sprintf(dest, "60%02d", x + 1);
			if (!(calls[x] = pri_new_call(pri))) {
				perror("pri_new_call");
				continue;
			}
#if 0
			{
				struct pri_sr *sr;
				sr = pri_sr_new();
				pri_sr_set_channel(sr, x+1, 0, 0);
				pri_sr_set_bearer(sr, 0, PRI_LAYER_1_ULAW);
				pri_sr_set_called(sr, dest, PRI_NATIONAL_ISDN, 1);
				pri_sr_set_caller(sr, num, name, PRI_NATIONAL_ISDN, PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN);
				pri_sr_set_redirecting(sr, num, PRI_NATIONAL_ISDN, PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN, PRI_REDIR_UNCONDITIONAL);
				if (pri_setup(pri, calls[x], sr))
					perror("pri_setup");
				pri_sr_free(sr);
			}
#else
			if (pri_call(pri, calls[x], PRI_TRANS_CAP_DIGITAL, x + 1, 1, 1, num, 
				PRI_NATIONAL_ISDN, name, PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN,
				dest, PRI_NATIONAL_ISDN, PRI_LAYER_1_ULAW)) {
					perror("pri_call");
			}
#endif
		}
		printf("Setup %d calls!\n", TEST_CALLS);
		break;
	case PRI_EVENT_RINGING:
		printf("PRI 1: %s (%d)\n", pri_event2str(e->gen.e), e->gen.e);
		q931_facility(pri, e->ringing.call);
		pri_answer(pri, e->ringing.call, e->ringing.channel, 0);
		break;
	case PRI_EVENT_HANGUP_REQ:
		printf("PRI 1: %s (%d)\n", pri_event2str(e->gen.e), e->gen.e);
		pri_hangup(pri, e->hangup.call, e->hangup.cause);
		break;
	default:
		printf("PRI 1: %s (%d)\n", pri_event2str(e->gen.e), e->gen.e);
	}
}

static void event2(struct pri *pri, pri_event *e)
{
	/* CPE */
	switch(e->gen.e) {
	case PRI_EVENT_RING:
		printf("PRI 2: %s (%d)\n", pri_event2str(e->gen.e), e->gen.e);
		pri_proceeding(pri, e->ring.call, e->ring.channel, 0);
		pri_acknowledge(pri, e->ring.call, e->ring.channel, 0);
		break;
	case PRI_EVENT_ANSWER:
		printf("PRI 2: %s (%d)\n", pri_event2str(e->gen.e), e->gen.e);
		pri_hangup(pri, e->answer.call, PRI_CAUSE_NORMAL_UNSPECIFIED);
		break;
	case PRI_EVENT_HANGUP:
		printf("PRI 2: %s (%d)\n", pri_event2str(e->gen.e), e->gen.e);
		pri_hangup(pri, e->hangup.call, e->hangup.cause);
		break;
	case PRI_EVENT_DCHAN_UP:
	default:
		printf("PRI 2: %s (%d)\n", pri_event2str(e->gen.e), e->gen.e);
	}
}

static void testmsg(struct pri *pri, char *s)
{
	char *c;
	static int keeplast = 0;
	do {
		c = strchr(s, '\n');
		if (c) {
			*c = '\0';
			c++;
		}
		if (keeplast)
			printf("%s", s);
		else if (cur == first)
			printf("-1 %s", s);
		else
			printf("-2 %s", s);
		if (c)
			printf("\n");
		s = c;
	} while(c && *c);
	if (!c)
		keeplast = 1;
	else
		keeplast = 0;
}

static void testerr(struct pri *pri, char *s)
{
	char *c;
	static int keeplast = 0;
	do {
		c = strchr(s, '\n');
		if (c) {
			*c = '\0';
			c++;
		}
		if (keeplast)
			printf("%s", s);
		else if (cur == first)
			printf("=1 %s", s);
		else
			printf("=2 %s", s);
		if (c)
			printf("\n");
		s = c;
	} while(c && *c);
	if (!c)
		keeplast = 1;
	else
		keeplast = 0;
}


static void *dchan(void *data)
{
	/* Joint D-channel */
	struct pri *pri = data;
	struct timeval *next, tv;
	pri_event *e;
	fd_set fds;
	int res;
	for(;;) {
		if ((next = pri_schedule_next(pri))) {
			gettimeofday(&tv, NULL);
			tv.tv_sec = next->tv_sec - tv.tv_sec;
			tv.tv_usec = next->tv_usec - tv.tv_usec;
			if (tv.tv_usec < 0) {
				tv.tv_usec += 1000000;
				tv.tv_sec -= 1;
			}
			if (tv.tv_sec < 0) {
				tv.tv_sec = 0;
				tv.tv_usec = 0;
			}
		}
		FD_ZERO(&fds);
		FD_SET(pri_fd(pri), &fds);
		res = select(pri_fd(pri) + 1, &fds, NULL, NULL, next ? &tv : NULL);
		pthread_mutex_lock(&lock);
		cur = pri;
		if (res < 0) {
			perror("select");
		} else if (!res) {
			e = pri_schedule_run(pri);
		} else {
			e = pri_check_event(pri);
		}
		if (e) {
			if (first == pri) {
				event1(e->gen.pri, e);
			} else {
				event2(e->gen.pri, e);
			}
		}
		pthread_mutex_unlock(&lock);
	}
	return NULL;
}


int main(int argc, char *argv[])
{
	int pair[2];
	pthread_t tmp;
	struct pri *pri;
	pri_set_message(testmsg);
	pri_set_error(testerr);
	if (socketpair(AF_LOCAL, SOCK_DGRAM, 0, pair)) {
		perror("socketpair");
		exit(1);
	}
	if (!(pri = pri_new_bri(pair[0], 0, PRI_NETWORK, PRI_DEF_SWITCHTYPE))) {
		perror("pri(0)");
		exit(1);
	}
	first = pri;
	pri_set_debug(pri, DEBUG_LEVEL);
	pri_facility_enable(pri);
	if (pthread_create(&tmp, NULL, dchan, pri)) {
		perror("thread(0)");
		exit(1);
	}
	if (!(pri = pri_new_bri(pair[1], 0, PRI_CPE, PRI_DEF_SWITCHTYPE))) {
		perror("pri(1)");
		exit(1);
	}
	pri_set_debug(pri, DEBUG_LEVEL);
	pri_facility_enable(pri);
	if (pthread_create(&tmp, NULL, dchan, pri)) {
		perror("thread(1)");
		exit(1);
	}
	/* Wait for things to run */
	sleep(5);
	exit(0);
}

