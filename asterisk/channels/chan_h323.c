/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005
 *
 * OpenH323 Channel Driver for ASTERISK PBX.
 *			By Jeremy McNamara
 *                      For The NuFone Network
 *
 * chan_h323 has been derived from code created by
 *               Michael Manousos and Mark Spencer
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

/*! \file
 *
 * \brief This file is part of the chan_h323 driver for Asterisk
 *
 * \author Jeremy McNamara
 *
 * \par See also
 * \arg Config_h323
 *
 * \ingroup channel_drivers
 */

/*** MODULEINFO
	<depend>openh323</depend>
	<defaultenabled>yes</defaultenabled>
 ***/

#ifdef __cplusplus
extern "C" {
#endif

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 211528 $")

#ifdef __cplusplus
}
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/param.h>
#if defined(BSD) || defined(SOLARIS)
#ifndef IPTOS_MINCOST
#define IPTOS_MINCOST 0x02
#endif
#endif
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <stdlib.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "asterisk/lock.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/musiconhold.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/sched.h"
#include "asterisk/io.h"
#include "asterisk/rtp.h"
#include "asterisk/acl.h"
#include "asterisk/callerid.h"
#include "asterisk/cli.h"
#include "asterisk/dsp.h"
#include "asterisk/causes.h"
#include "asterisk/stringfields.h"
#include "asterisk/abstract_jb.h"
#include "asterisk/astobj.h"

#ifdef __cplusplus
}
#endif

#undef open
#undef close
#include "h323/chan_h323.h"

receive_digit_cb on_receive_digit;
on_rtp_cb on_external_rtp_create;
start_rtp_cb on_start_rtp_channel;
setup_incoming_cb on_incoming_call;
setup_outbound_cb on_outgoing_call;
chan_ringing_cb	on_chan_ringing;
con_established_cb on_connection_established;
clear_con_cb on_connection_cleared;
answer_call_cb on_answer_call;
progress_cb on_progress;
rfc2833_cb on_set_rfc2833_payload;
hangup_cb on_hangup;
setcapabilities_cb on_setcapabilities;
setpeercapabilities_cb on_setpeercapabilities;

/* global debug flag */
int h323debug;

/*! Global jitterbuffer configuration - by default, jb is disabled */
static struct ast_jb_conf default_jbconf =
{
	.flags = 0,
	.max_size = -1,
	.resync_threshold = -1,
	.impl = ""
};
static struct ast_jb_conf global_jbconf;

/** Variables required by Asterisk */
static const char tdesc[] = "The NuFone Network's Open H.323 Channel Driver";
static const char config[] = "h323.conf";
static char default_context[AST_MAX_CONTEXT] = "default";
static struct sockaddr_in bindaddr;

#define GLOBAL_CAPABILITY (AST_FORMAT_G723_1 | AST_FORMAT_GSM | AST_FORMAT_ULAW | AST_FORMAT_ALAW | AST_FORMAT_G729A | AST_FORMAT_H261)

/** H.323 configuration values */
static int h323_signalling_port = 1720;
static char gatekeeper[100];
static int gatekeeper_disable = 1;
static int gatekeeper_discover = 0;
static int gkroute = 0;
/* Find user by alias (h.323 id) is default, alternative is the incomming call's source IP address*/
static int userbyalias = 1;
static int acceptAnonymous = 1;
static int tos = 0;
static char secret[50];
static unsigned int unique = 0;

static call_options_t global_options;

/** Private structure of a OpenH323 channel */
struct oh323_pvt {
	ast_mutex_t lock;					/* Channel private lock */
	call_options_t options;				/* Options to be used during call setup */
	int alreadygone;					/* Whether or not we've already been destroyed by our peer */
	int needdestroy;					/* if we need to be destroyed */
	call_details_t cd;					/* Call details */
	struct ast_channel *owner;			/* Who owns us */
	struct sockaddr_in sa;				/* Our peer */
	struct sockaddr_in redirip;			/* Where our RTP should be going if not to us */
	int nonCodecCapability;				/* non-audio capability */
	int outgoing;						/* Outgoing or incoming call? */
	char exten[AST_MAX_EXTENSION];		/* Requested extension */
	char context[AST_MAX_CONTEXT];		/* Context where to start */
	char accountcode[256];				/* Account code */
	char rdnis[80];						/* Referring DNIS, if available */
	int amaflags;						/* AMA Flags */
	struct ast_rtp *rtp;				/* RTP Session */
	struct ast_dsp *vad;				/* Used for in-band DTMF detection */
	int nativeformats;					/* Codec formats supported by a channel */
	int needhangup;						/* Send hangup when Asterisk is ready */
	int hangupcause;					/* Hangup cause from OpenH323 layer */
	int newstate;						/* Pending state change */
	int newcontrol;						/* Pending control to send */
	int newdigit;						/* Pending DTMF digit to send */
	int newduration;					/* Pending DTMF digit duration to send */
	int pref_codec;						/* Preferred codec */
	int peercapability;					/* Capabilities learned from peer */
	int jointcapability;				/* Common capabilities for local and remote side */
	struct ast_codec_pref peer_prefs;	/* Preferenced list of codecs which remote side supports */
	int dtmf_pt;						/* Payload code used for RFC2833 messages */
	int curDTMF;						/* DTMF tone being generated to Asterisk side */
	int DTMFsched;						/* Scheduler descriptor for DTMF */
	int update_rtp_info;				/* Configuration of fd's array is pending */
	int recvonly;						/* Peer isn't wish to receive our voice stream */
	int txDtmfDigit;					/* DTMF digit being to send to H.323 side */
	int noInbandDtmf;					/* Inband DTMF processing by DSP isn't available */
	int connection_established;			/* Call got CONNECT message */
	int got_progress;					/* Call got PROGRESS message, pass inband audio */
	struct oh323_pvt *next;				/* Next channel in list */
} *iflist = NULL;

static struct ast_user_list {
	ASTOBJ_CONTAINER_COMPONENTS(struct oh323_user);
} userl;

static struct ast_peer_list {
	ASTOBJ_CONTAINER_COMPONENTS(struct oh323_peer);
} peerl;

static struct ast_alias_list {
	ASTOBJ_CONTAINER_COMPONENTS(struct oh323_alias);
} aliasl;

/** Asterisk RTP stuff */
static struct sched_context *sched;
static struct io_context *io;

/** Protect the interface list (oh323_pvt) */
AST_MUTEX_DEFINE_STATIC(iflock);

/* Protect the monitoring thread, so only one process can kill or start it, and not
   when it's doing something critical. */
AST_MUTEX_DEFINE_STATIC(monlock);

/* Protect the H.323 capabilities list, to avoid more than one channel to set the capabilities simultaneaously in the h323 stack. */
AST_MUTEX_DEFINE_STATIC(caplock);

/* Protect the reload process */
AST_MUTEX_DEFINE_STATIC(h323_reload_lock);
static int h323_reloading = 0;

/* This is the thread for the monitor which checks for input on the channels
   which are not currently in use. */
static pthread_t monitor_thread = AST_PTHREADT_NULL;
static int restart_monitor(void);
static int h323_do_reload(void);

static struct ast_channel *oh323_request(const char *type, int format, void *data, int *cause);
static int oh323_digit_begin(struct ast_channel *c, char digit);
static int oh323_digit_end(struct ast_channel *c, char digit, unsigned int duration);
static int oh323_call(struct ast_channel *c, char *dest, int timeout);
static int oh323_hangup(struct ast_channel *c);
static int oh323_answer(struct ast_channel *c);
static struct ast_frame *oh323_read(struct ast_channel *c);
static int oh323_write(struct ast_channel *c, struct ast_frame *frame);
static int oh323_indicate(struct ast_channel *c, int condition, const void *data, size_t datalen);
static int oh323_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);

static const struct ast_channel_tech oh323_tech = {
	.type = "H323",
	.description = tdesc,
	.capabilities = ((AST_FORMAT_MAX_AUDIO << 1) - 1),
	.properties = AST_CHAN_TP_WANTSJITTER | AST_CHAN_TP_CREATESJITTER,
	.requester = oh323_request,
	.send_digit_begin = oh323_digit_begin,
	.send_digit_end = oh323_digit_end,
	.call = oh323_call,
	.hangup = oh323_hangup,
	.answer = oh323_answer,
	.read = oh323_read,
	.write = oh323_write,
	.indicate = oh323_indicate,
	.fixup = oh323_fixup,
	/* disable, for now */
#if 0
	.bridge = ast_rtp_bridge,
#endif
};

static const char* redirectingreason2str(int redirectingreason)
{
	switch (redirectingreason) {
	case 0:
		return "UNKNOWN";
	case 1:
		return "BUSY";
	case 2:
		return "NO_REPLY";
	case 0xF:
		return "UNCONDITIONAL";
	default:
		return "NOREDIRECT";
	}
}

static void oh323_destroy_alias(struct oh323_alias *alias)
{
	if (h323debug)
		ast_log(LOG_DEBUG, "Destroying alias '%s'\n", alias->name);
	free(alias);
}

static void oh323_destroy_user(struct oh323_user *user)
{
	if (h323debug)
		ast_log(LOG_DEBUG, "Destroying user '%s'\n", user->name);
	ast_free_ha(user->ha);
	free(user);
}

static void oh323_destroy_peer(struct oh323_peer *peer)
{
	if (h323debug)
		ast_log(LOG_DEBUG, "Destroying peer '%s'\n", peer->name);
	ast_free_ha(peer->ha);
	free(peer);
}

static int oh323_simulate_dtmf_end(const void *data)
{
	struct oh323_pvt *pvt = (struct oh323_pvt *)data;

	if (pvt) {
		ast_mutex_lock(&pvt->lock);
		/* Don't hold pvt lock while trying to lock the channel */
		while(pvt->owner && ast_channel_trylock(pvt->owner)) {
			ast_mutex_unlock(&pvt->lock);
			usleep(1);
			ast_mutex_lock(&pvt->lock);
		}

		if (pvt->owner) {
			struct ast_frame f = {
				.frametype = AST_FRAME_DTMF_END,
				.subclass = pvt->curDTMF,
				.samples = 0,
				.src = "SIMULATE_DTMF_END",
			};
			ast_queue_frame(pvt->owner, &f);
			ast_channel_unlock(pvt->owner);
		}

		pvt->DTMFsched = -1;
		ast_mutex_unlock(&pvt->lock);
	}

	return 0;
}

/* Channel and private structures should be already locked */
static void __oh323_update_info(struct ast_channel *c, struct oh323_pvt *pvt)
{
	if (c->nativeformats != pvt->nativeformats) {
		if (h323debug)
			ast_log(LOG_DEBUG, "Preparing %s for new native format\n", c->name);
		c->nativeformats = pvt->nativeformats;
		ast_set_read_format(c, c->readformat);
		ast_set_write_format(c, c->writeformat);
	}
	if (pvt->needhangup) {
		if (h323debug)
			ast_log(LOG_DEBUG, "Process pending hangup for %s\n", c->name);
		c->_softhangup |= AST_SOFTHANGUP_DEV;
		c->hangupcause = pvt->hangupcause;
		ast_queue_hangup(c);
		pvt->needhangup = 0;
		pvt->newstate = pvt->newcontrol = pvt->newdigit = pvt->DTMFsched = -1;
	}
	if (pvt->newstate >= 0) {
		ast_setstate(c, pvt->newstate);
		pvt->newstate = -1;
	}
	if (pvt->newcontrol >= 0) {
		ast_queue_control(c, pvt->newcontrol);
		pvt->newcontrol = -1;
	}
	if (pvt->newdigit >= 0) {
		struct ast_frame f = {
			.frametype = AST_FRAME_DTMF_END,
			.subclass = pvt->newdigit,
			.samples = pvt->newduration * 8,
			.len = pvt->newduration,
			.src = "UPDATE_INFO",
		};
		if (pvt->newdigit == ' ') {		/* signalUpdate message */
			f.subclass = pvt->curDTMF;
			if (pvt->DTMFsched >= 0) {
				AST_SCHED_DEL(sched, pvt->DTMFsched);
			}
		} else {						/* Regular input or signal message */
			if (pvt->newduration) {		/* This is a signal, signalUpdate follows */
				f.frametype = AST_FRAME_DTMF_BEGIN;
				AST_SCHED_DEL(sched, pvt->DTMFsched);
				pvt->DTMFsched = ast_sched_add(sched, pvt->newduration, oh323_simulate_dtmf_end, pvt);
				if (h323debug)
					ast_log(LOG_DTMF, "Scheduled DTMF END simulation for %d ms, id=%d\n", pvt->newduration, pvt->DTMFsched);
			}
			pvt->curDTMF = pvt->newdigit;
		}
		ast_queue_frame(c, &f);
		pvt->newdigit = -1;
	}
	if (pvt->update_rtp_info > 0) {
		if (pvt->rtp) {
			ast_jb_configure(c, &global_jbconf);
			c->fds[0] = ast_rtp_fd(pvt->rtp);
			c->fds[1] = ast_rtcp_fd(pvt->rtp);
			ast_queue_frame(pvt->owner, &ast_null_frame);	/* Tell Asterisk to apply changes */
		}
		pvt->update_rtp_info = -1;
	}
}

/* Only channel structure should be locked */
static void oh323_update_info(struct ast_channel *c)
{
	struct oh323_pvt *pvt = c->tech_pvt;

	if (pvt) {
		ast_mutex_lock(&pvt->lock);
		__oh323_update_info(c, pvt);
		ast_mutex_unlock(&pvt->lock);
	}
}

static void cleanup_call_details(call_details_t *cd)
{
	if (cd->call_token) {
		free(cd->call_token);
		cd->call_token = NULL;
	}
	if (cd->call_source_aliases) {
		free(cd->call_source_aliases);
		cd->call_source_aliases = NULL;
	}
	if (cd->call_dest_alias) {
		free(cd->call_dest_alias);
		cd->call_dest_alias = NULL;
	}
	if (cd->call_source_name) {
		free(cd->call_source_name);
		cd->call_source_name = NULL;
	}
	if (cd->call_source_e164) {
		free(cd->call_source_e164);
		cd->call_source_e164 = NULL;
	}
	if (cd->call_dest_e164) {
		free(cd->call_dest_e164);
		cd->call_dest_e164 = NULL;
	}
	if (cd->sourceIp) {
		free(cd->sourceIp);
		cd->sourceIp = NULL;
	}
	if (cd->redirect_number) {
		free(cd->redirect_number);
		cd->redirect_number = NULL;
	}
}

static void __oh323_destroy(struct oh323_pvt *pvt)
{
	struct oh323_pvt *cur, *prev = NULL;

	AST_SCHED_DEL(sched, pvt->DTMFsched);

	if (pvt->rtp) {
		ast_rtp_destroy(pvt->rtp);
	}

	/* Free dsp used for in-band DTMF detection */
	if (pvt->vad) {
		ast_dsp_free(pvt->vad);
	}
	cleanup_call_details(&pvt->cd);

	/* Unlink us from the owner if we have one */
	if (pvt->owner) {
		ast_channel_lock(pvt->owner);
		if (h323debug)
			ast_log(LOG_DEBUG, "Detaching from %s\n", pvt->owner->name);
		pvt->owner->tech_pvt = NULL;
		ast_channel_unlock(pvt->owner);
	}
	cur = iflist;
	while(cur) {
		if (cur == pvt) {
			if (prev)
				prev->next = cur->next;
			else
				iflist = cur->next;
			break;
		}
		prev = cur;
		cur = cur->next;
	}
	if (!cur) {
		ast_log(LOG_WARNING, "%p is not in list?!?! \n", cur);
	} else {
		ast_mutex_unlock(&pvt->lock);
		ast_mutex_destroy(&pvt->lock);
		free(pvt);
	}
}

static void oh323_destroy(struct oh323_pvt *pvt)
{
	if (h323debug) {
		ast_log(LOG_DEBUG, "Destroying channel %s\n", (pvt->owner ? pvt->owner->name : "<unknown>"));
	}
	ast_mutex_lock(&iflock);
	ast_mutex_lock(&pvt->lock);
	__oh323_destroy(pvt);
	ast_mutex_unlock(&iflock);
}

static int oh323_digit_begin(struct ast_channel *c, char digit)
{
	struct oh323_pvt *pvt = (struct oh323_pvt *) c->tech_pvt;
	char *token;

	if (!pvt) {
		ast_log(LOG_ERROR, "No private structure?! This is bad\n");
		return -1;
	}
	ast_mutex_lock(&pvt->lock);
	if (pvt->rtp && (pvt->options.dtmfmode & H323_DTMF_RFC2833) && (pvt->dtmf_pt > 0)) {
		/* out-of-band DTMF */
		if (h323debug) {
			ast_log(LOG_DTMF, "Begin sending out-of-band digit %c on %s\n", digit, c->name);
		}
		ast_rtp_senddigit_begin(pvt->rtp, digit);
		ast_mutex_unlock(&pvt->lock);
	} else if (pvt->txDtmfDigit != digit) {
		/* in-band DTMF */
		if (h323debug) {
			ast_log(LOG_DTMF, "Begin sending inband digit %c on %s\n", digit, c->name);
		}
		pvt->txDtmfDigit = digit;
		token = pvt->cd.call_token ? strdup(pvt->cd.call_token) : NULL;
		ast_mutex_unlock(&pvt->lock);
		h323_send_tone(token, digit);
		if (token) {
			free(token);
		}
	} else
		ast_mutex_unlock(&pvt->lock);
	oh323_update_info(c);
	return 0;
}

/**
 * Send (play) the specified digit to the channel.
 *
 */
static int oh323_digit_end(struct ast_channel *c, char digit, unsigned int duration)
{
	struct oh323_pvt *pvt = (struct oh323_pvt *) c->tech_pvt;
	char *token;

	if (!pvt) {
		ast_log(LOG_ERROR, "No private structure?! This is bad\n");
		return -1;
	}
	ast_mutex_lock(&pvt->lock);
	if (pvt->rtp && (pvt->options.dtmfmode & H323_DTMF_RFC2833) && (pvt->dtmf_pt > 0)) {
		/* out-of-band DTMF */
		if (h323debug) {
			ast_log(LOG_DTMF, "End sending out-of-band digit %c on %s, duration %d\n", digit, c->name, duration);
		}
		ast_rtp_senddigit_end(pvt->rtp, digit);
		ast_mutex_unlock(&pvt->lock);
	} else {
		/* in-band DTMF */
		if (h323debug) {
			ast_log(LOG_DTMF, "End sending inband digit %c on %s, duration %d\n", digit, c->name, duration);
		}
		pvt->txDtmfDigit = ' ';
		token = pvt->cd.call_token ? strdup(pvt->cd.call_token) : NULL;
		ast_mutex_unlock(&pvt->lock);
		h323_send_tone(token, ' ');
		if (token) {
			free(token);
		}
	}
	oh323_update_info(c);
	return 0;
}

/**
 * Make a call over the specified channel to the specified
 * destination.
 * Returns -1 on error, 0 on success.
 */
static int oh323_call(struct ast_channel *c, char *dest, int timeout)
{
	int res = 0;
	struct oh323_pvt *pvt = (struct oh323_pvt *)c->tech_pvt;
	const char *addr;
	char called_addr[1024];

	if (h323debug) {
		ast_log(LOG_DEBUG, "Calling to %s on %s\n", dest, c->name);
	}
	if ((c->_state != AST_STATE_DOWN) && (c->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "Line is already in use (%s)\n", c->name);
		return -1;
	}
	ast_mutex_lock(&pvt->lock);
	if (!gatekeeper_disable) {
		if (ast_strlen_zero(pvt->exten)) {
			ast_copy_string(called_addr, dest, sizeof(called_addr));
		} else {
			snprintf(called_addr, sizeof(called_addr), "%s@%s", pvt->exten, dest);
		}
	} else {
		res = htons(pvt->sa.sin_port);
		addr = ast_inet_ntoa(pvt->sa.sin_addr);
		if (ast_strlen_zero(pvt->exten)) {
			snprintf(called_addr, sizeof(called_addr), "%s:%d", addr, res);
		} else {
			snprintf(called_addr, sizeof(called_addr), "%s@%s:%d", pvt->exten, addr, res);
		}
	}
	/* make sure null terminated */
	called_addr[sizeof(called_addr) - 1] = '\0';

	if (c->cid.cid_num)
		ast_copy_string(pvt->options.cid_num, c->cid.cid_num, sizeof(pvt->options.cid_num));

	if (c->cid.cid_name)
		ast_copy_string(pvt->options.cid_name, c->cid.cid_name, sizeof(pvt->options.cid_name));

	if (c->cid.cid_rdnis) {
		ast_copy_string(pvt->options.cid_rdnis, c->cid.cid_rdnis, sizeof(pvt->options.cid_rdnis));
	}

	pvt->options.presentation = c->cid.cid_pres;
	pvt->options.type_of_number = c->cid.cid_ton;

	if ((addr = pbx_builtin_getvar_helper(c, "PRIREDIRECTREASON"))) {
		if (!strcasecmp(addr, "UNKNOWN"))
			pvt->options.redirect_reason = 0;
		else if (!strcasecmp(addr, "BUSY"))
			pvt->options.redirect_reason = 1;
		else if (!strcasecmp(addr, "NO_REPLY"))
			pvt->options.redirect_reason = 2;
		else if (!strcasecmp(addr, "UNCONDITIONAL"))
			pvt->options.redirect_reason = 15;
		else
			pvt->options.redirect_reason = -1;
	} else
		pvt->options.redirect_reason = -1;

	pvt->options.transfer_capability = c->transfercapability;

	/* indicate that this is an outgoing call */
	pvt->outgoing = 1;

	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Requested transfer capability: 0x%.2x - %s\n", c->transfercapability, ast_transfercapability2str(c->transfercapability));
	if (h323debug)
		ast_log(LOG_DEBUG, "Placing outgoing call to %s, %d\n", called_addr, pvt->options.dtmfcodec);
	ast_mutex_unlock(&pvt->lock);
	res = h323_make_call(called_addr, &(pvt->cd), &pvt->options);
	if (res) {
		ast_log(LOG_NOTICE, "h323_make_call failed(%s)\n", c->name);
		return -1;
	}
	oh323_update_info(c);
	return 0;
}

static int oh323_answer(struct ast_channel *c)
{
	int res;
	struct oh323_pvt *pvt = (struct oh323_pvt *) c->tech_pvt;
	char *token;

	if (h323debug)
		ast_log(LOG_DEBUG, "Answering on %s\n", c->name);

	ast_mutex_lock(&pvt->lock);
	token = pvt->cd.call_token ? strdup(pvt->cd.call_token) : NULL;
	ast_mutex_unlock(&pvt->lock);
	res = h323_answering_call(token, 0);
	if (token)
		free(token);

	oh323_update_info(c);
	if (c->_state != AST_STATE_UP) {
		ast_setstate(c, AST_STATE_UP);
	}
	return res;
}

static int oh323_hangup(struct ast_channel *c)
{
	struct oh323_pvt *pvt = (struct oh323_pvt *) c->tech_pvt;
	int q931cause = AST_CAUSE_NORMAL_CLEARING;
	char *call_token;


	if (h323debug)
		ast_log(LOG_DEBUG, "Hanging up and scheduling destroy of call %s\n", c->name);

	if (!c->tech_pvt) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
	ast_mutex_lock(&pvt->lock);
	/* Determine how to disconnect */
	if (pvt->owner != c) {
		ast_log(LOG_WARNING, "Huh?  We aren't the owner?\n");
		ast_mutex_unlock(&pvt->lock);
		return 0;
	}

	pvt->owner = NULL;
	c->tech_pvt = NULL;

	if (c->hangupcause) {
		q931cause = c->hangupcause;
	} else {
		const char *cause = pbx_builtin_getvar_helper(c, "DIALSTATUS");
		if (cause) {
			if (!strcmp(cause, "CONGESTION")) {
				q931cause = AST_CAUSE_NORMAL_CIRCUIT_CONGESTION;
			} else if (!strcmp(cause, "BUSY")) {
				q931cause = AST_CAUSE_USER_BUSY;
			} else if (!strcmp(cause, "CHANISUNVAIL")) {
				q931cause = AST_CAUSE_REQUESTED_CHAN_UNAVAIL;
			} else if (!strcmp(cause, "NOANSWER")) {
				q931cause = AST_CAUSE_NO_ANSWER;
			} else if (!strcmp(cause, "CANCEL")) {
				q931cause = AST_CAUSE_CALL_REJECTED;
			}
		}
	}

	/* Start the process if it's not already started */
	if (!pvt->alreadygone && !pvt->hangupcause) {
		call_token = pvt->cd.call_token ? strdup(pvt->cd.call_token) : NULL;
		if (call_token) {
			/* Release lock to eliminate deadlock */
			ast_mutex_unlock(&pvt->lock);
			if (h323_clear_call(call_token, q931cause)) {
				ast_log(LOG_WARNING, "ClearCall failed.\n");
			}
			free(call_token);
			ast_mutex_lock(&pvt->lock);
		}
	}
	pvt->needdestroy = 1;
	ast_mutex_unlock(&pvt->lock);

	/* Update usage counter */
	ast_module_unref(ast_module_info->self);

	return 0;
}

static struct ast_frame *oh323_rtp_read(struct oh323_pvt *pvt)
{
	/* Retrieve audio/etc from channel. Assumes pvt->lock is already held. */
	struct ast_frame *f;

	/* Only apply it for the first packet, we just need the correct ip/port */
	if (pvt->options.nat) {
		ast_rtp_setnat(pvt->rtp, pvt->options.nat);
		pvt->options.nat = 0;
	}

	f = ast_rtp_read(pvt->rtp);
	/* Don't send RFC2833 if we're not supposed to */
	if (f && (f->frametype == AST_FRAME_DTMF) && !(pvt->options.dtmfmode & H323_DTMF_RFC2833)) {
		return &ast_null_frame;
	}
	if (pvt->owner) {
		/* We already hold the channel lock */
		if (f->frametype == AST_FRAME_VOICE) {
			if (f->subclass != pvt->owner->nativeformats) {
				/* Try to avoid deadlock */
				if (ast_channel_trylock(pvt->owner)) {
					ast_log(LOG_NOTICE, "Format changed but channel is locked. Ignoring frame...\n");
					return &ast_null_frame;
				}
				if (h323debug)
					ast_log(LOG_DEBUG, "Oooh, format changed to %d\n", f->subclass);
				pvt->owner->nativeformats = f->subclass;
				pvt->nativeformats = f->subclass;
				ast_set_read_format(pvt->owner, pvt->owner->readformat);
				ast_set_write_format(pvt->owner, pvt->owner->writeformat);
				ast_channel_unlock(pvt->owner);
			}
			/* Do in-band DTMF detection */
			if ((pvt->options.dtmfmode & H323_DTMF_INBAND) && pvt->vad) {
				if ((pvt->nativeformats & (AST_FORMAT_SLINEAR | AST_FORMAT_ALAW | AST_FORMAT_ULAW))) {
					if (!ast_channel_trylock(pvt->owner)) {
						f = ast_dsp_process(pvt->owner, pvt->vad, f);
						ast_channel_unlock(pvt->owner);
					}
					else
						ast_log(LOG_NOTICE, "Unable to process inband DTMF while channel is locked\n");
				} else if (pvt->nativeformats && !pvt->noInbandDtmf) {
					ast_log(LOG_NOTICE, "Inband DTMF is not supported on codec %s. Use RFC2833\n", ast_getformatname(f->subclass));
					pvt->noInbandDtmf = 1;
				}
				if (f &&(f->frametype == AST_FRAME_DTMF)) {
					if (h323debug)
						ast_log(LOG_DTMF, "Received in-band digit %c.\n", f->subclass);
				}
			}
		}
	}
	return f;
}

static struct ast_frame *oh323_read(struct ast_channel *c)
{
	struct ast_frame *fr;
	struct oh323_pvt *pvt = (struct oh323_pvt *)c->tech_pvt;
	ast_mutex_lock(&pvt->lock);
	__oh323_update_info(c, pvt);
	switch(c->fdno) {
	case 0:
		fr = oh323_rtp_read(pvt);
		break;
	case 1:
		if (pvt->rtp)
			fr = ast_rtcp_read(pvt->rtp);
		else
			fr = &ast_null_frame;
		break;
	default:
		ast_log(LOG_ERROR, "Unable to handle fd %d on channel %s\n", c->fdno, c->name);
		fr = &ast_null_frame;
		break;
	}
	ast_mutex_unlock(&pvt->lock);
	return fr;
}

static int oh323_write(struct ast_channel *c, struct ast_frame *frame)
{
	struct oh323_pvt *pvt = (struct oh323_pvt *) c->tech_pvt;
	int res = 0;
	if (frame->frametype != AST_FRAME_VOICE) {
		if (frame->frametype == AST_FRAME_IMAGE) {
			return 0;
		} else {
			ast_log(LOG_WARNING, "Can't send %d type frames with H323 write\n", frame->frametype);
			return 0;
		}
	} else {
		if (!(frame->subclass & c->nativeformats)) {
			ast_log(LOG_WARNING, "Asked to transmit frame type %d, while native formats is %d (read/write = %d/%d)\n",
				frame->subclass, c->nativeformats, c->readformat, c->writeformat);
			return 0;
		}
	}
	if (pvt) {
		ast_mutex_lock(&pvt->lock);
		if (pvt->rtp && !pvt->recvonly)
			res = ast_rtp_write(pvt->rtp, frame);
		__oh323_update_info(c, pvt);
		ast_mutex_unlock(&pvt->lock);
	}
	return res;
}

static int oh323_indicate(struct ast_channel *c, int condition, const void *data, size_t datalen)
{

	struct oh323_pvt *pvt = (struct oh323_pvt *) c->tech_pvt;
	char *token = (char *)NULL;
	int res = -1;
	int got_progress;

	ast_mutex_lock(&pvt->lock);
	token = (pvt->cd.call_token ? strdup(pvt->cd.call_token) : NULL);
	got_progress = pvt->got_progress;
	if (condition == AST_CONTROL_PROGRESS)
		pvt->got_progress = 1;
	else if ((condition == AST_CONTROL_BUSY) || (condition == AST_CONTROL_CONGESTION))
		pvt->alreadygone = 1;
	ast_mutex_unlock(&pvt->lock);

	if (h323debug)
		ast_log(LOG_DEBUG, "OH323: Indicating %d on %s\n", condition, token);

	switch(condition) {
	case AST_CONTROL_RINGING:
		if (c->_state == AST_STATE_RING || c->_state == AST_STATE_RINGING) {
			h323_send_alerting(token);
			res = (got_progress ? 0 : -1);	/* Do not simulate any audio tones if we got PROGRESS message */
		}
		break;
	case AST_CONTROL_PROGRESS:
		if (c->_state != AST_STATE_UP) {
			/* Do not send PROGRESS message more than once */
			if (!got_progress)
				h323_send_progress(token);
			res = 0;
		}
		break;
	case AST_CONTROL_BUSY:
		if (c->_state != AST_STATE_UP) {
			h323_answering_call(token, 1);
			ast_softhangup_nolock(c, AST_SOFTHANGUP_DEV);
			res = 0;
		}
		break;
	case AST_CONTROL_CONGESTION:
		if (c->_state != AST_STATE_UP) {
			h323_answering_call(token, 1);
			ast_softhangup_nolock(c, AST_SOFTHANGUP_DEV);
			res = 0;
		}
		break;
	case AST_CONTROL_HOLD:
		ast_moh_start(c, data, NULL);
		res = 0;
		break;
	case AST_CONTROL_UNHOLD:
		ast_moh_stop(c);
		res = 0;
		break;
	case AST_CONTROL_SRCUPDATE:
		ast_rtp_new_source(pvt->rtp);
		res = 0;
		break;
	case AST_CONTROL_PROCEEDING:
	case -1:
		break;
	default:
		ast_log(LOG_WARNING, "OH323: Don't know how to indicate condition %d on %s\n", condition, token);
		break;
	}

	if (h323debug)
		ast_log(LOG_DEBUG, "OH323: Indicated %d on %s, res=%d\n", condition, token, res);
	if (token)
		free(token);
	oh323_update_info(c);

	return res;
}

static int oh323_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct oh323_pvt *pvt = (struct oh323_pvt *) newchan->tech_pvt;

	ast_mutex_lock(&pvt->lock);
	if (pvt->owner != oldchan) {
		ast_log(LOG_WARNING, "old channel wasn't %p but was %p\n", oldchan, pvt->owner);
		return -1;
	}
	pvt->owner = newchan;
	ast_mutex_unlock(&pvt->lock);
	return 0;
}

static int __oh323_rtp_create(struct oh323_pvt *pvt)
{
	struct in_addr our_addr;

	if (pvt->rtp)
		return 0;

	if (ast_find_ourip(&our_addr, bindaddr)) {
		ast_mutex_unlock(&pvt->lock);
		ast_log(LOG_ERROR, "Unable to locate local IP address for RTP stream\n");
		return -1;
	}
	pvt->rtp = ast_rtp_new_with_bindaddr(sched, io, 1, 0, our_addr);
	if (!pvt->rtp) {
		ast_mutex_unlock(&pvt->lock);
		ast_log(LOG_WARNING, "Unable to create RTP session: %s\n", strerror(errno));
		return -1;
	}
	if (h323debug)
		ast_log(LOG_DEBUG, "Created RTP channel\n");

	ast_rtp_settos(pvt->rtp, tos);

	if (h323debug)
		ast_log(LOG_DEBUG, "Setting NAT on RTP to %d\n", pvt->options.nat);
	ast_rtp_setnat(pvt->rtp, pvt->options.nat);

	if (pvt->dtmf_pt > 0)
		ast_rtp_set_rtpmap_type(pvt->rtp, pvt->dtmf_pt, "audio", "telephone-event", 0);

	if (pvt->peercapability)
		ast_rtp_codec_setpref(pvt->rtp, &pvt->peer_prefs);

	if (pvt->owner && !ast_channel_trylock(pvt->owner)) {
		ast_jb_configure(pvt->owner, &global_jbconf);
		pvt->owner->fds[0] = ast_rtp_fd(pvt->rtp);
		pvt->owner->fds[1] = ast_rtcp_fd(pvt->rtp);
		ast_queue_frame(pvt->owner, &ast_null_frame);	/* Tell Asterisk to apply changes */
		ast_channel_unlock(pvt->owner);
	} else
		pvt->update_rtp_info = 1;

	return 0;
}

/* Private structure should be locked on a call */
static struct ast_channel *__oh323_new(struct oh323_pvt *pvt, int state, const char *host)
{
	struct ast_channel *ch;
	char *cid_num, *cid_name;
	int fmt;

	if (!ast_strlen_zero(pvt->options.cid_num))
		cid_num = pvt->options.cid_num;
	else
		cid_num = pvt->cd.call_source_e164;

	if (!ast_strlen_zero(pvt->options.cid_name))
		cid_name = pvt->options.cid_name;
	else
		cid_name = pvt->cd.call_source_name;
	
	/* Don't hold a oh323_pvt lock while we allocate a chanel */
	ast_mutex_unlock(&pvt->lock);
	ch = ast_channel_alloc(1, state, cid_num, cid_name, pvt->accountcode, pvt->exten, pvt->context, pvt->amaflags, "H323/%s", host);
	/* Update usage counter */
	ast_module_ref(ast_module_info->self);
	ast_mutex_lock(&pvt->lock);
	if (ch) {
		ch->tech = &oh323_tech;
		if (!(fmt = pvt->jointcapability) && !(fmt = pvt->options.capability))
			fmt = global_options.capability;
		ch->nativeformats = ast_codec_choose(&pvt->options.prefs, fmt, 1)/* | (pvt->jointcapability & AST_FORMAT_VIDEO_MASK)*/;
		pvt->nativeformats = ch->nativeformats;
		fmt = ast_best_codec(ch->nativeformats);
		ch->writeformat = fmt;
		ch->rawwriteformat = fmt;
		ch->readformat = fmt;
		ch->rawreadformat = fmt;
#if 0
		ch->fds[0] = ast_rtp_fd(pvt->rtp);
		ch->fds[1] = ast_rtcp_fd(pvt->rtp);
#endif
#ifdef VIDEO_SUPPORT
		if (pvt->vrtp) {
			ch->fds[2] = ast_rtp_fd(pvt->vrtp);
			ch->fds[3] = ast_rtcp_fd(pvt->vrtp);
		}
#endif
#ifdef T38_SUPPORT
		if (pvt->udptl) {
			ch->fds[4] = ast_udptl_fd(pvt->udptl);
		}
#endif
		if (state == AST_STATE_RING) {
			ch->rings = 1;
		}
		/* Allocate dsp for in-band DTMF support */
		if (pvt->options.dtmfmode & H323_DTMF_INBAND) {
			pvt->vad = ast_dsp_new();
			ast_dsp_set_features(pvt->vad, DSP_FEATURE_DTMF_DETECT);
		}
		/* Register channel functions. */
		ch->tech_pvt = pvt;
		/* Set the owner of this channel */
		pvt->owner = ch;

		ast_copy_string(ch->context, pvt->context, sizeof(ch->context));
		ast_copy_string(ch->exten, pvt->exten, sizeof(ch->exten));
		ch->priority = 1;
		if (!ast_strlen_zero(pvt->accountcode)) {
			ast_string_field_set(ch, accountcode, pvt->accountcode);
		}
		if (pvt->amaflags) {
			ch->amaflags = pvt->amaflags;
		}

		/* Don't use ast_set_callerid() here because it will
		 * generate a needless NewCallerID event */
		ch->cid.cid_ani = ast_strdup(cid_num);

		if (pvt->cd.redirect_reason >= 0) {
			ch->cid.cid_rdnis = ast_strdup(pvt->cd.redirect_number);
			pbx_builtin_setvar_helper(ch, "PRIREDIRECTREASON", redirectingreason2str(pvt->cd.redirect_reason));
		}
		ch->cid.cid_pres = pvt->cd.presentation;
		ch->cid.cid_ton = pvt->cd.type_of_number;

		if (!ast_strlen_zero(pvt->exten) && strcmp(pvt->exten, "s")) {
			ch->cid.cid_dnid = strdup(pvt->exten);
		}
		if (pvt->cd.transfer_capability >= 0)
			ch->transfercapability = pvt->cd.transfer_capability;
		if (state != AST_STATE_DOWN) {
			if (ast_pbx_start(ch)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n", ch->name);
				ast_hangup(ch);
				ch = NULL;
			}
		}
	} else {
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
	}
	return ch;
}

static struct oh323_pvt *oh323_alloc(int callid)
{
	struct oh323_pvt *pvt;

	pvt = (struct oh323_pvt *) malloc(sizeof(struct oh323_pvt));
	if (!pvt) {
		ast_log(LOG_ERROR, "Couldn't allocate private structure. This is bad\n");
		return NULL;
	}
	memset(pvt, 0, sizeof(struct oh323_pvt));
	pvt->cd.redirect_reason = -1;
	pvt->cd.transfer_capability = -1;
	/* Ensure the call token is allocated for outgoing call */
	if (!callid) {
		if ((pvt->cd).call_token == NULL) {
			(pvt->cd).call_token = (char *)malloc(128);
		}
		if (!pvt->cd.call_token) {
			ast_log(LOG_ERROR, "Not enough memory to alocate call token\n");
			ast_rtp_destroy(pvt->rtp);
			free(pvt);
			return NULL;
		}
		memset((char *)(pvt->cd).call_token, 0, 128);
		pvt->cd.call_reference = callid;
	}
	memcpy(&pvt->options, &global_options, sizeof(pvt->options));
	pvt->jointcapability = pvt->options.capability;
	if (pvt->options.dtmfmode & H323_DTMF_RFC2833) {
		pvt->nonCodecCapability |= AST_RTP_DTMF;
	} else {
		pvt->nonCodecCapability &= ~AST_RTP_DTMF;
	}
	ast_copy_string(pvt->context, default_context, sizeof(pvt->context));
	pvt->newstate = pvt->newcontrol = pvt->newdigit = pvt->update_rtp_info = pvt->DTMFsched = -1;
	ast_mutex_init(&pvt->lock);
	/* Add to interface list */
	ast_mutex_lock(&iflock);
	pvt->next = iflist;
	iflist = pvt;
	ast_mutex_unlock(&iflock);
	return pvt;
}

static struct oh323_pvt *find_call_locked(int call_reference, const char *token)
{
	struct oh323_pvt *pvt;

	ast_mutex_lock(&iflock);
	pvt = iflist;
	while(pvt) {
		if (!pvt->needdestroy && ((signed int)pvt->cd.call_reference == call_reference)) {
			/* Found the call */
			if ((token != NULL) && (pvt->cd.call_token != NULL) && (!strcmp(pvt->cd.call_token, token))) {
				ast_mutex_lock(&pvt->lock);
				ast_mutex_unlock(&iflock);
				return pvt;
			} else if (token == NULL) {
				ast_log(LOG_WARNING, "Call Token is NULL\n");
				ast_mutex_lock(&pvt->lock);
				ast_mutex_unlock(&iflock);
				return pvt;
			}
		}
		pvt = pvt->next;
	}
	ast_mutex_unlock(&iflock);
	return NULL;
}

static int update_state(struct oh323_pvt *pvt, int state, int signal)
{
	if (!pvt)
		return 0;
	if (pvt->owner && !ast_channel_trylock(pvt->owner)) {
		if (state >= 0)
			ast_setstate(pvt->owner, state);
		if (signal >= 0)
			ast_queue_control(pvt->owner, signal);
		ast_channel_unlock(pvt->owner);
		return 1;
	}
	else {
		if (state >= 0)
			pvt->newstate = state;
		if (signal >= 0)
			pvt->newcontrol = signal;
		return 0;
	}
}

static struct oh323_alias *build_alias(const char *name, struct ast_variable *v, struct ast_variable *alt, int realtime)
{
	struct oh323_alias *alias;
	int found = 0;

	alias = ASTOBJ_CONTAINER_FIND_UNLINK_FULL(&aliasl, name, name, 0, 0, strcasecmp);

	if (alias)
		found++;
	else {
		if (!(alias = (struct oh323_alias *)calloc(1, sizeof(*alias))))
			return NULL;
		ASTOBJ_INIT(alias);
	}
	if (!found && name)
		ast_copy_string(alias->name, name, sizeof(alias->name));
	for (; v || ((v = alt) && !(alt = NULL)); v = v->next) {
		if (!strcasecmp(v->name, "e164")) {
			ast_copy_string(alias->e164, v->value, sizeof(alias->e164));
		} else if (!strcasecmp(v->name, "prefix")) {
			ast_copy_string(alias->prefix, v->value, sizeof(alias->prefix));
		} else if (!strcasecmp(v->name, "context")) {
			ast_copy_string(alias->context, v->value, sizeof(alias->context));
		} else if (!strcasecmp(v->name, "secret")) {
			ast_copy_string(alias->secret, v->value, sizeof(alias->secret));
		} else {
			if (strcasecmp(v->value, "h323")) {
				ast_log(LOG_WARNING, "Keyword %s does not make sense in type=h323\n", v->name);
			}
		}
	}
	ASTOBJ_UNMARK(alias);
	return alias;
}

static struct oh323_alias *realtime_alias(const char *alias)
{
	struct ast_variable *var, *tmp;
	struct oh323_alias *a;

	var = ast_load_realtime("h323", "name", alias, NULL);

	if (!var)
		return NULL;

	for (tmp = var; tmp; tmp = tmp->next) {
		if (!strcasecmp(tmp->name, "type") &&
		!(!strcasecmp(tmp->value, "alias") || !strcasecmp(tmp->value, "h323"))) {
			ast_variables_destroy(var);
			return NULL;
		}
	}

	a = build_alias(alias, var, NULL, 1);

	ast_variables_destroy(var);

	return a;
}

#define DEPRECATED(_v, _new_opt) \
	ast_log(LOG_WARNING, "Option %s found at line %d has beed deprecated. Use %s instead.\n", (_v)->name, (_v)->lineno, (_new_opt))

static int update_common_options(struct ast_variable *v, struct call_options *options)
{
	int tmp;

	if (!strcasecmp(v->name, "allow")) {
		ast_parse_allow_disallow(&options->prefs, &options->capability, v->value, 1);
	} else if (!strcasecmp(v->name, "autoframing")) {
		options->autoframing = ast_true(v->value);
	} else if (!strcasecmp(v->name, "disallow")) {
		ast_parse_allow_disallow(&options->prefs, &options->capability, v->value, 0);
	} else if (!strcasecmp(v->name, "dtmfmode")) {
		if (!strcasecmp(v->value, "inband")) {
			options->dtmfmode = H323_DTMF_INBAND;
		} else if (!strcasecmp(v->value, "rfc2833")) {
			options->dtmfmode = H323_DTMF_RFC2833;
		} else {
			ast_log(LOG_WARNING, "Unknown dtmf mode '%s', using rfc2833\n", v->value);
			options->dtmfmode = H323_DTMF_RFC2833;
		}
	} else if (!strcasecmp(v->name, "dtmfcodec")) {
		tmp = atoi(v->value);
		if (tmp < 96)
			ast_log(LOG_WARNING, "Invalid %s value %s at line %d\n", v->name, v->value, v->lineno);
		else
			options->dtmfcodec = tmp;
	} else if (!strcasecmp(v->name, "bridge")) {
		options->bridge = ast_true(v->value);
	} else if (!strcasecmp(v->name, "nat")) {
		options->nat = ast_true(v->value);
	} else if (!strcasecmp(v->name, "noFastStart")) {
		DEPRECATED(v, "fastStart");
		options->fastStart = !ast_true(v->value);
	} else if (!strcasecmp(v->name, "fastStart")) {
		options->fastStart = ast_true(v->value);
	} else if (!strcasecmp(v->name, "noH245Tunneling")) {
		DEPRECATED(v, "h245Tunneling");
		options->h245Tunneling = !ast_true(v->value);
	} else if (!strcasecmp(v->name, "h245Tunneling")) {
		options->h245Tunneling = ast_true(v->value);
	} else if (!strcasecmp(v->name, "noSilenceSuppression")) {
		DEPRECATED(v, "silenceSuppression");
		options->silenceSuppression = !ast_true(v->value);
	} else if (!strcasecmp(v->name, "silenceSuppression")) {
		options->silenceSuppression = ast_true(v->value);
	} else if (!strcasecmp(v->name, "progress_setup")) {
		tmp = atoi(v->value);
		if ((tmp != 0) && (tmp != 1) && (tmp != 3) && (tmp != 8)) {
			ast_log(LOG_WARNING, "Invalid value %s for %s at line %d, assuming 0\n", v->value, v->name, v->lineno);
			tmp = 0;
		}
		options->progress_setup = tmp;
	} else if (!strcasecmp(v->name, "progress_alert")) {
		tmp = atoi(v->value);
		if ((tmp != 0) && (tmp != 1) && (tmp != 8)) {
			ast_log(LOG_WARNING, "Invalid value %s for %s at line %d, assuming 0\n", v->value, v->name, v->lineno);
			tmp = 0;
		}
		options->progress_alert = tmp;
	} else if (!strcasecmp(v->name, "progress_audio")) {
		options->progress_audio = ast_true(v->value);
	} else if (!strcasecmp(v->name, "callerid")) {
		ast_callerid_split(v->value, options->cid_name, sizeof(options->cid_name), options->cid_num, sizeof(options->cid_num));
	} else if (!strcasecmp(v->name, "fullname")) {
		ast_copy_string(options->cid_name, v->value, sizeof(options->cid_name));
	} else if (!strcasecmp(v->name, "cid_number")) {
		ast_copy_string(options->cid_num, v->value, sizeof(options->cid_num));
	} else if (!strcasecmp(v->name, "tunneling")) {
		if (!strcasecmp(v->value, "none"))
			options->tunnelOptions = 0;
		else if (!strcasecmp(v->value, "cisco"))
			options->tunnelOptions |= H323_TUNNEL_CISCO;
		else if (!strcasecmp(v->value, "qsig"))
			options->tunnelOptions |= H323_TUNNEL_QSIG;
		else
			ast_log(LOG_WARNING, "Invalid value %s for %s at line %d\n", v->value, v->name, v->lineno);
	} else
		return 1;

	return 0;
}
#undef DEPRECATED

static struct oh323_user *build_user(char *name, struct ast_variable *v, struct ast_variable *alt, int realtime)
{
	struct oh323_user *user;
	struct ast_ha *oldha;
	int found = 0;
	int format;

	user = ASTOBJ_CONTAINER_FIND_UNLINK_FULL(&userl, name, name, 0, 0, strcmp);

	if (user)
		found++;
	else {
		if (!(user = (struct oh323_user *)calloc(1, sizeof(*user))))
			return NULL;
		ASTOBJ_INIT(user);
	}
	oldha = user->ha;
	user->ha = (struct ast_ha *)NULL;
	memcpy(&user->options, &global_options, sizeof(user->options));
	/* Set default context */
	ast_copy_string(user->context, default_context, sizeof(user->context));
	if (user && !found)
		ast_copy_string(user->name, name, sizeof(user->name));

#if 0 /* XXX Port channel variables functionality from chan_sip XXX */
	if (user->chanvars) {
		ast_variables_destroy(user->chanvars);
		user->chanvars = NULL;
	}
#endif

	for (; v || ((v = alt) && !(alt = NULL)); v = v->next) {
		if (!update_common_options(v, &user->options))
			continue;
		if (!strcasecmp(v->name, "context")) {
			ast_copy_string(user->context, v->value, sizeof(user->context));
		} else if (!strcasecmp(v->name, "secret")) {
			ast_copy_string(user->secret, v->value, sizeof(user->secret));
		} else if (!strcasecmp(v->name, "accountcode")) {
			ast_copy_string(user->accountcode, v->value, sizeof(user->accountcode));
		} else if (!strcasecmp(v->name, "host")) {
			if (!strcasecmp(v->value, "dynamic")) {
				ast_log(LOG_ERROR, "A dynamic host on a type=user does not make any sense\n");
				ASTOBJ_UNREF(user, oh323_destroy_user);
				return NULL;
			} else if (ast_get_ip(&user->addr, v->value)) {
				ASTOBJ_UNREF(user, oh323_destroy_user);
				return NULL;
			}
			/* Let us know we need to use ip authentication */
			user->host = 1;
		} else if (!strcasecmp(v->name, "amaflags")) {
			format = ast_cdr_amaflags2int(v->value);
			if (format < 0) {
				ast_log(LOG_WARNING, "Invalid AMA Flags: %s at line %d\n", v->value, v->lineno);
			} else {
				user->amaflags = format;
			}
		} else if (!strcasecmp(v->name, "permit") ||
					!strcasecmp(v->name, "deny")) {
			user->ha = ast_append_ha(v->name, v->value, user->ha);
		}
	}
	ASTOBJ_UNMARK(user);
	ast_free_ha(oldha);
	return user;
}

static struct oh323_user *realtime_user(const call_details_t *cd)
{
	struct ast_variable *var, *tmp;
	struct oh323_user *user;
	char *username;

	if (userbyalias)
		var = ast_load_realtime("h323", "name", username = cd->call_source_aliases, NULL);
	else {
		username = (char *)NULL;
		var = ast_load_realtime("h323", "host", cd->sourceIp, NULL);
	}

	if (!var)
		return NULL;

	for (tmp = var; tmp; tmp = tmp->next) {
		if (!strcasecmp(tmp->name, "type") &&
		!(!strcasecmp(tmp->value, "user") || !strcasecmp(tmp->value, "friend"))) {
			ast_variables_destroy(var);
			return NULL;
		} else if (!username && !strcasecmp(tmp->name, "name"))
			username = tmp->value;
	}

	if (!username) {
		ast_log(LOG_WARNING, "Cannot determine user name for IP address %s\n", cd->sourceIp);
		ast_variables_destroy(var);
		return NULL;
	}

	user = build_user(username, var, NULL, 1);

	ast_variables_destroy(var);

	return user;
}

static struct oh323_peer *build_peer(const char *name, struct ast_variable *v, struct ast_variable *alt, int realtime)
{
	struct oh323_peer *peer;
	struct ast_ha *oldha;
	int found = 0;

	peer = ASTOBJ_CONTAINER_FIND_UNLINK_FULL(&peerl, name, name, 0, 0, strcmp);

	if (peer)
		found++;
	else {
		if (!(peer = (struct oh323_peer*)calloc(1, sizeof(*peer))))
			return NULL;
		ASTOBJ_INIT(peer);
	}
	oldha = peer->ha;
	peer->ha = NULL;
	memcpy(&peer->options, &global_options, sizeof(peer->options));
	peer->addr.sin_port = htons(h323_signalling_port);
	peer->addr.sin_family = AF_INET;
	if (!found && name)
		ast_copy_string(peer->name, name, sizeof(peer->name));

#if 0 /* XXX Port channel variables functionality from chan_sip XXX */
	if (peer->chanvars) {
		ast_variables_destroy(peer->chanvars);
		peer->chanvars = NULL;
	}
#endif
	/* Default settings for mailbox */
	peer->mailbox[0] = '\0';

	for (; v || ((v = alt) && !(alt = NULL)); v = v->next) {
		if (!update_common_options(v, &peer->options))
			continue;
		if (!strcasecmp(v->name, "host")) {
			if (!strcasecmp(v->value, "dynamic")) {
				ast_log(LOG_ERROR, "Dynamic host configuration not implemented.\n");
				ASTOBJ_UNREF(peer, oh323_destroy_peer);
				return NULL;
			}
			if (ast_get_ip(&peer->addr, v->value)) {
				ast_log(LOG_ERROR, "Could not determine IP for %s\n", v->value);
				ASTOBJ_UNREF(peer, oh323_destroy_peer);
				return NULL;
			}
		} else if (!strcasecmp(v->name, "port")) {
			peer->addr.sin_port = htons(atoi(v->value));
		} else if (!strcasecmp(v->name, "permit") ||
					!strcasecmp(v->name, "deny")) {
			peer->ha = ast_append_ha(v->name, v->value, peer->ha);
		} else if (!strcasecmp(v->name, "mailbox")) {
			ast_copy_string(peer->mailbox, v->value, sizeof(peer->mailbox));
		} else if (!strcasecmp(v->name, "hasvoicemail")) {
			if (ast_true(v->value) && ast_strlen_zero(peer->mailbox)) {
				ast_copy_string(peer->mailbox, name, sizeof(peer->mailbox));
			}
		}
	}
	ASTOBJ_UNMARK(peer);
	ast_free_ha(oldha);
	return peer;
}

static struct oh323_peer *realtime_peer(const char *peername, struct sockaddr_in *sin)
{
	struct oh323_peer *peer;
	struct ast_variable *var;
	struct ast_variable *tmp;
	const char *addr;

	/* First check on peer name */
	if (peername)
		var = ast_load_realtime("h323", "name", peername, addr = NULL);
	else if (sin) /* Then check on IP address for dynamic peers */
		var = ast_load_realtime("h323", "host", addr = ast_inet_ntoa(sin->sin_addr), NULL);
	else
		return NULL;

	if (!var)
		return NULL;

	for (tmp = var; tmp; tmp = tmp->next) {
		/* If this is type=user, then skip this object. */
		if (!strcasecmp(tmp->name, "type") &&
				!(!strcasecmp(tmp->value, "peer") || !strcasecmp(tmp->value, "friend"))) {
			ast_variables_destroy(var);
			return NULL;
		} else if (!peername && !strcasecmp(tmp->name, "name")) {
			peername = tmp->value;
		}
	}

	if (!peername) {	/* Did not find peer in realtime */
		ast_log(LOG_WARNING, "Cannot determine peer name for IP address %s\n", addr);
		ast_variables_destroy(var);
		return NULL;
	}

	/* Peer found in realtime, now build it in memory */
	peer = build_peer(peername, var, NULL, 1);

	ast_variables_destroy(var);

	return peer;
}

static int oh323_addrcmp_str(struct in_addr inaddr, char *addr)
{
	return strcmp(ast_inet_ntoa(inaddr), addr);
}

static struct oh323_user *find_user(const call_details_t *cd, int realtime)
{
	struct oh323_user *u;

	if (userbyalias)
		u = ASTOBJ_CONTAINER_FIND(&userl, cd->call_source_aliases);
	else
		u = ASTOBJ_CONTAINER_FIND_FULL(&userl, cd->sourceIp, addr.sin_addr, 0, 0, oh323_addrcmp_str);

	if (!u && realtime)
		u = realtime_user(cd);

	if (!u && h323debug)
		ast_log(LOG_DEBUG, "Could not find user by name %s or address %s\n", cd->call_source_aliases, cd->sourceIp);

	return u;
}

static int oh323_addrcmp(struct sockaddr_in addr, struct sockaddr_in *sin)
{
	int res;

	if (!sin)
		res = -1;
	else
		res = inaddrcmp(&addr , sin);

	return res;
}

static struct oh323_peer *find_peer(const char *peer, struct sockaddr_in *sin, int realtime)
{
	struct oh323_peer *p;

	if (peer)
		p = ASTOBJ_CONTAINER_FIND(&peerl, peer);
	else
		p = ASTOBJ_CONTAINER_FIND_FULL(&peerl, sin, addr, 0, 0, oh323_addrcmp);

	if (!p && realtime)
		p = realtime_peer(peer, sin);

	if (!p && h323debug)
		ast_log(LOG_DEBUG, "Could not find peer by name %s or address %s\n", (peer ? peer : "<NONE>"), (sin ? ast_inet_ntoa(sin->sin_addr) : "<NONE>"));

	return p;
}

static int create_addr(struct oh323_pvt *pvt, char *opeer)
{
	struct hostent *hp;
	struct ast_hostent ahp;
	struct oh323_peer *p;
	int portno;
	int found = 0;
	char *port;
	char *hostn;
	char peer[256] = "";

	ast_copy_string(peer, opeer, sizeof(peer));
	port = strchr(peer, ':');
	if (port) {
		*port = '\0';
		port++;
	}
	pvt->sa.sin_family = AF_INET;
	p = find_peer(peer, NULL, 1);
	if (p) {
		found++;
		memcpy(&pvt->options, &p->options, sizeof(pvt->options));
		pvt->jointcapability = pvt->options.capability;
		if (pvt->options.dtmfmode) {
			if (pvt->options.dtmfmode & H323_DTMF_RFC2833) {
				pvt->nonCodecCapability |= AST_RTP_DTMF;
			} else {
				pvt->nonCodecCapability &= ~AST_RTP_DTMF;
			}
		}
		if (p->addr.sin_addr.s_addr) {
			pvt->sa.sin_addr = p->addr.sin_addr;
			pvt->sa.sin_port = p->addr.sin_port;
		}
		ASTOBJ_UNREF(p, oh323_destroy_peer);
	}
	if (!p && !found) {
		hostn = peer;
		if (port) {
			portno = atoi(port);
		} else {
			portno = h323_signalling_port;
		}
		hp = ast_gethostbyname(hostn, &ahp);
		if (hp) {
			memcpy(&pvt->sa.sin_addr, hp->h_addr, sizeof(pvt->sa.sin_addr));
			pvt->sa.sin_port = htons(portno);
			/* Look peer by address */
			p = find_peer(NULL, &pvt->sa, 1);
			memcpy(&pvt->options, (p ? &p->options : &global_options), sizeof(pvt->options));
			pvt->jointcapability = pvt->options.capability;
			if (p) {
				ASTOBJ_UNREF(p, oh323_destroy_peer);
			}
			if (pvt->options.dtmfmode) {
				if (pvt->options.dtmfmode & H323_DTMF_RFC2833) {
					pvt->nonCodecCapability |= AST_RTP_DTMF;
				} else {
					pvt->nonCodecCapability &= ~AST_RTP_DTMF;
				}
			}
			return 0;
		} else {
			ast_log(LOG_WARNING, "No such host: %s\n", peer);
			return -1;
		}
	} else if (!found) {
		return -1;
	} else {
		return 0;
	}
}
static struct ast_channel *oh323_request(const char *type, int format, void *data, int *cause)
{
	int oldformat;
	struct oh323_pvt *pvt;
	struct ast_channel *tmpc = NULL;
	char *dest = (char *)data;
	char *ext, *host;
	char *h323id = NULL;
	char tmp[256], tmp1[256];

	if (h323debug)
		ast_log(LOG_DEBUG, "type=%s, format=%d, data=%s.\n", type, format, (char *)data);

	pvt = oh323_alloc(0);
	if (!pvt) {
		ast_log(LOG_WARNING, "Unable to build pvt data for '%s'\n", (char *)data);
		return NULL;
	}
	oldformat = format;
	format &= ((AST_FORMAT_MAX_AUDIO << 1) - 1);
	if (!format) {
		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%d'\n", format);
		oh323_destroy(pvt);
		if (cause)
			*cause = AST_CAUSE_INCOMPATIBLE_DESTINATION;
		return NULL;
	}
	ast_copy_string(tmp, dest, sizeof(tmp));
	host = strchr(tmp, '@');
	if (host) {
		*host = '\0';
		host++;
		ext = tmp;
	} else {
		ext = strrchr(tmp, '/');
		if (ext)
			*ext++ = '\0';
		host = tmp;
	}
	strtok_r(host, "/", &(h323id));
	if (!ast_strlen_zero(h323id)) {
		h323_set_id(h323id);
	}
	if (ext) {
		ast_copy_string(pvt->exten, ext, sizeof(pvt->exten));
	}
	if (h323debug)
		ast_log(LOG_DEBUG, "Extension: %s Host: %s\n", pvt->exten, host);

	if (gatekeeper_disable) {
		if (create_addr(pvt, host)) {
			oh323_destroy(pvt);
			if (cause)
				*cause = AST_CAUSE_DESTINATION_OUT_OF_ORDER;
			return NULL;
		}
	}
	else {
		memcpy(&pvt->options, &global_options, sizeof(pvt->options));
		pvt->jointcapability = pvt->options.capability;
		if (pvt->options.dtmfmode) {
			if (pvt->options.dtmfmode & H323_DTMF_RFC2833) {
				pvt->nonCodecCapability |= AST_RTP_DTMF;
			} else {
				pvt->nonCodecCapability &= ~AST_RTP_DTMF;
			}
		}
	}

	ast_mutex_lock(&caplock);
	/* Generate unique channel identifier */
	snprintf(tmp1, sizeof(tmp1)-1, "%s-%u", host, ++unique);
	tmp1[sizeof(tmp1)-1] = '\0';
	ast_mutex_unlock(&caplock);

	ast_mutex_lock(&pvt->lock);
	tmpc = __oh323_new(pvt, AST_STATE_DOWN, tmp1);
	ast_mutex_unlock(&pvt->lock);
	if (!tmpc) {
		oh323_destroy(pvt);
		if (cause)
			*cause = AST_CAUSE_NORMAL_TEMPORARY_FAILURE;
	}
	ast_update_use_count();
	restart_monitor();
	return tmpc;
}

/** Find a call by alias */
static struct oh323_alias *find_alias(const char *source_aliases, int realtime)
{
	struct oh323_alias *a;

	a = ASTOBJ_CONTAINER_FIND(&aliasl, source_aliases);

	if (!a && realtime)
		a = realtime_alias(source_aliases);

	return a;
}

/**
  * Callback for sending digits from H.323 up to asterisk
  *
  */
static int receive_digit(unsigned call_reference, char digit, const char *token, int duration)
{
	struct oh323_pvt *pvt;
	int res;

	pvt = find_call_locked(call_reference, token);
	if (!pvt) {
		ast_log(LOG_ERROR, "Received digit '%c' (%u ms) for call %s without private structure\n", digit, duration, token);
		return -1;
	}
	if (h323debug)
		ast_log(LOG_DTMF, "Received %s digit '%c' (%u ms) for call %s\n", (digit == ' ' ? "update for" : "new"), (digit == ' ' ? pvt->curDTMF : digit), duration, token);

	if (pvt->owner && !ast_channel_trylock(pvt->owner)) {
		if (digit == '!')
			res = ast_queue_control(pvt->owner, AST_CONTROL_FLASH);
		else {
			struct ast_frame f = {
				.frametype = AST_FRAME_DTMF_END,
				.subclass = digit,
				.samples = duration * 8,
				.len = duration,
				.src = "SEND_DIGIT",
			};
			if (digit == ' ') {		/* signalUpdate message */
				f.subclass = pvt->curDTMF;
				AST_SCHED_DEL(sched, pvt->DTMFsched);
			} else {				/* Regular input or signal message */
				if (pvt->DTMFsched >= 0) {
					/* We still don't send DTMF END from previous event, send it now */
					AST_SCHED_DEL(sched, pvt->DTMFsched);
					f.subclass = pvt->curDTMF;
					f.samples = f.len = 0;
					ast_queue_frame(pvt->owner, &f);
					/* Restore values */
					f.subclass = digit;
					f.samples = duration * 8;
					f.len = duration;
				}
				if (duration) {		/* This is a signal, signalUpdate follows */
					f.frametype = AST_FRAME_DTMF_BEGIN;
					pvt->DTMFsched = ast_sched_add(sched, duration, oh323_simulate_dtmf_end, pvt);
					if (h323debug)
						ast_log(LOG_DTMF, "Scheduled DTMF END simulation for %d ms, id=%d\n", duration, pvt->DTMFsched);
				}
				pvt->curDTMF = digit;
			}
			res = ast_queue_frame(pvt->owner, &f);
		}
		ast_channel_unlock(pvt->owner);
	} else {
		if (digit == '!')
			pvt->newcontrol = AST_CONTROL_FLASH;
		else {
			pvt->newduration = duration;
			pvt->newdigit = digit;
		}
		res = 0;
	}
	ast_mutex_unlock(&pvt->lock);
	return res;
}

/**
  * Callback function used to inform the H.323 stack of the local rtp ip/port details
  *
  * Returns the local RTP information
  */
static struct rtp_info *external_rtp_create(unsigned call_reference, const char * token)
{
	struct oh323_pvt *pvt;
	struct sockaddr_in us;
	struct rtp_info *info;

	info = (struct rtp_info *)malloc(sizeof(struct rtp_info));
	if (!info) {
		ast_log(LOG_ERROR, "Unable to allocated info structure, this is very bad\n");
		return NULL;
	}
	pvt = find_call_locked(call_reference, token);
	if (!pvt) {
		free(info);
		ast_log(LOG_ERROR, "Unable to find call %s(%d)\n", token, call_reference);
		return NULL;
	}
	if (!pvt->rtp)
		__oh323_rtp_create(pvt);
	if (!pvt->rtp) {
		ast_mutex_unlock(&pvt->lock);
		free(info);
		ast_log(LOG_ERROR, "No RTP stream is available for call %s (%d)", token, call_reference);
		return NULL;
	}
	/* figure out our local RTP port and tell the H.323 stack about it */
	ast_rtp_get_us(pvt->rtp, &us);
	ast_mutex_unlock(&pvt->lock);

	ast_copy_string(info->addr, ast_inet_ntoa(us.sin_addr), sizeof(info->addr));
	info->port = ntohs(us.sin_port);
	if (h323debug)
		ast_log(LOG_DEBUG, "Sending RTP 'US' %s:%d\n", info->addr, info->port);
	return info;
}

/**
 * Definition taken from rtp.c for rtpPayloadType because we need it here.
 */
struct rtpPayloadType {
	int isAstFormat;	/* whether the following code is an AST_FORMAT */
	int code;
};

/**
  * Call-back function passing remote ip/port information from H.323 to asterisk
  *
  * Returns nothing
  */
static void setup_rtp_connection(unsigned call_reference, const char *remoteIp, int remotePort, const char *token, int pt)
{
	struct oh323_pvt *pvt;
	struct sockaddr_in them;
	struct rtpPayloadType rtptype;
	int nativeformats_changed;
	enum { NEED_NONE, NEED_HOLD, NEED_UNHOLD } rtp_change = NEED_NONE;

	if (h323debug)
		ast_log(LOG_DEBUG, "Setting up RTP connection for %s\n", token);

	/* Find the call or allocate a private structure if call not found */
	pvt = find_call_locked(call_reference, token);
	if (!pvt) {
		ast_log(LOG_ERROR, "Something is wrong: rtp\n");
		return;
	}
	if (pvt->alreadygone) {
		ast_mutex_unlock(&pvt->lock);
		return;
	}

	if (!pvt->rtp)
		__oh323_rtp_create(pvt);

	them.sin_family = AF_INET;
	/* only works for IPv4 */
	them.sin_addr.s_addr = inet_addr(remoteIp);
	them.sin_port = htons(remotePort);

	if (them.sin_addr.s_addr) {
		ast_rtp_set_peer(pvt->rtp, &them);
		if (pvt->recvonly) {
			pvt->recvonly = 0;
			rtp_change = NEED_UNHOLD;
		}
	} else {
		ast_rtp_stop(pvt->rtp);
		if (!pvt->recvonly) {
			pvt->recvonly = 1;
			rtp_change = NEED_HOLD;
		}
	}

	/* Change native format to reflect information taken from OLC/OLCAck */
	nativeformats_changed = 0;
	if (pt != 128 && pvt->rtp) {	/* Payload type is invalid, so try to use previously decided */
		rtptype = ast_rtp_lookup_pt(pvt->rtp, pt);
		if (h323debug)
			ast_log(LOG_DEBUG, "Native format is set to %d from %d by RTP payload type %d\n", rtptype.code, pvt->nativeformats, pt);
		if (pvt->nativeformats != rtptype.code) {
			pvt->nativeformats = rtptype.code;
			nativeformats_changed = 1;
		}
	} else if (h323debug)
		ast_log(LOG_NOTICE, "Payload type is unknown, formats isn't changed\n");

	/* Don't try to lock the channel if nothing changed */
	if (nativeformats_changed || pvt->options.progress_audio || (rtp_change != NEED_NONE)) {
		if (pvt->owner && !ast_channel_trylock(pvt->owner)) {
			/* Re-build translation path only if native format(s) has been changed */
			if (pvt->owner->nativeformats != pvt->nativeformats) {
				if (h323debug)
					ast_log(LOG_DEBUG, "Native format changed to %d from %d, read format is %d, write format is %d\n", pvt->nativeformats, pvt->owner->nativeformats, pvt->owner->readformat, pvt->owner->writeformat);
				pvt->owner->nativeformats = pvt->nativeformats;
				ast_set_read_format(pvt->owner, pvt->owner->readformat);
				ast_set_write_format(pvt->owner, pvt->owner->writeformat);
			}
			if (pvt->options.progress_audio)
				ast_queue_control(pvt->owner, AST_CONTROL_PROGRESS);
			switch (rtp_change) {
			case NEED_HOLD:
				ast_queue_control(pvt->owner, AST_CONTROL_HOLD);
				break;
			case NEED_UNHOLD:
				ast_queue_control(pvt->owner, AST_CONTROL_UNHOLD);
				break;
			default:
				break;
			}
			ast_channel_unlock(pvt->owner);
		}
		else {
			if (pvt->options.progress_audio)
				pvt->newcontrol = AST_CONTROL_PROGRESS;
			else if (rtp_change == NEED_HOLD)
				pvt->newcontrol = AST_CONTROL_HOLD;
			else if (rtp_change == NEED_UNHOLD)
				pvt->newcontrol = AST_CONTROL_UNHOLD;
			if (h323debug)
				ast_log(LOG_DEBUG, "RTP connection preparation for %s is pending...\n", token);
		}
	}
	ast_mutex_unlock(&pvt->lock);

	if (h323debug)
		ast_log(LOG_DEBUG, "RTP connection prepared for %s\n", token);

	return;
}

/**
  *	Call-back function to signal asterisk that the channel has been answered
  * Returns nothing
  */
static void connection_made(unsigned call_reference, const char *token)
{
	struct oh323_pvt *pvt;

	if (h323debug)
		ast_log(LOG_DEBUG, "Call %s answered\n", token);

	pvt = find_call_locked(call_reference, token);
	if (!pvt) {
		ast_log(LOG_ERROR, "Something is wrong: connection\n");
		return;
	}

	/* Inform asterisk about remote party connected only on outgoing calls */
	if (!pvt->outgoing) {
		ast_mutex_unlock(&pvt->lock);
		return;
	}
	/* Do not send ANSWER message more than once */
	if (!pvt->connection_established) {
		pvt->connection_established = 1;
		update_state(pvt, -1, AST_CONTROL_ANSWER);
	}
	ast_mutex_unlock(&pvt->lock);
	return;
}

static int progress(unsigned call_reference, const char *token, int inband)
{
	struct oh323_pvt *pvt;

	if (h323debug)
		ast_log(LOG_DEBUG, "Received ALERT/PROGRESS message for %s tones\n", (inband ? "inband" : "self-generated"));

	pvt = find_call_locked(call_reference, token);
	if (!pvt) {
		ast_log(LOG_ERROR, "Private structure not found in progress.\n");
		return -1;
	}
	if (!pvt->owner) {
		ast_mutex_unlock(&pvt->lock);
		ast_log(LOG_ERROR, "No Asterisk channel associated with private structure.\n");
		return -1;
	}
	update_state(pvt, -1, (inband ? AST_CONTROL_PROGRESS : AST_CONTROL_RINGING));
	ast_mutex_unlock(&pvt->lock);

	return 0;
}

/**
 *  Call-back function for incoming calls
 *
 *  Returns 1 on success
 */
static call_options_t *setup_incoming_call(call_details_t *cd)
{
	struct oh323_pvt *pvt;
	struct oh323_user *user = NULL;
	struct oh323_alias *alias = NULL;

	if (h323debug)
		ast_log(LOG_DEBUG, "Setting up incoming call for %s\n", cd->call_token);

	/* allocate the call*/
	pvt = oh323_alloc(cd->call_reference);

	if (!pvt) {
		ast_log(LOG_ERROR, "Unable to allocate private structure, this is bad.\n");
		cleanup_call_details(cd);
		return NULL;
	}

	/* Populate the call details in the private structure */
	memcpy(&pvt->cd, cd, sizeof(pvt->cd));
	memcpy(&pvt->options, &global_options, sizeof(pvt->options));
	pvt->jointcapability = pvt->options.capability;

	if (h323debug) {
		ast_verbose(VERBOSE_PREFIX_3 "Setting up Call\n");
		ast_verbose(VERBOSE_PREFIX_3 " \tCall token:  [%s]\n", pvt->cd.call_token);
		ast_verbose(VERBOSE_PREFIX_3 " \tCalling party name:  [%s]\n", pvt->cd.call_source_name);
		ast_verbose(VERBOSE_PREFIX_3 " \tCalling party number:  [%s]\n", pvt->cd.call_source_e164);
		ast_verbose(VERBOSE_PREFIX_3 " \tCalled party name:  [%s]\n", pvt->cd.call_dest_alias);
		ast_verbose(VERBOSE_PREFIX_3 " \tCalled party number:  [%s]\n", pvt->cd.call_dest_e164);
		if (pvt->cd.redirect_reason >= 0)
			ast_verbose(VERBOSE_PREFIX_3 " \tRedirecting party number:  [%s] (reason %d)\n", pvt->cd.redirect_number, pvt->cd.redirect_reason);
		ast_verbose(VERBOSE_PREFIX_3 " \tCalling party IP:  [%s]\n", pvt->cd.sourceIp);
	}

	/* Decide if we are allowing Gatekeeper routed calls*/
	if ((!strcasecmp(cd->sourceIp, gatekeeper)) && (gkroute == -1) && !gatekeeper_disable) {
		if (!ast_strlen_zero(cd->call_dest_e164)) {
			ast_copy_string(pvt->exten, cd->call_dest_e164, sizeof(pvt->exten));
			ast_copy_string(pvt->context, default_context, sizeof(pvt->context));
		} else {
			alias = find_alias(cd->call_dest_alias, 1);
			if (!alias) {
				ast_log(LOG_ERROR, "Call for %s rejected, alias not found\n", cd->call_dest_alias);
				oh323_destroy(pvt);
				return NULL;
			}
			ast_copy_string(pvt->exten, alias->name, sizeof(pvt->exten));
			ast_copy_string(pvt->context, alias->context, sizeof(pvt->context));
		}
	} else {
		/* Either this call is not from the Gatekeeper
		   or we are not allowing gk routed calls */
		user = find_user(cd, 1);
		if (!user) {
			if (!acceptAnonymous) {
				ast_log(LOG_NOTICE, "Anonymous call from '%s@%s' rejected\n", pvt->cd.call_source_aliases, pvt->cd.sourceIp);
				oh323_destroy(pvt);
				return NULL;
			}
			if (ast_strlen_zero(default_context)) {
				ast_log(LOG_ERROR, "Call from '%s@%s' rejected due to no default context\n", pvt->cd.call_source_aliases, pvt->cd.sourceIp);
				oh323_destroy(pvt);
				return NULL;
			}
			ast_copy_string(pvt->context, default_context, sizeof(pvt->context));
			if (!ast_strlen_zero(pvt->cd.call_dest_e164)) {
				ast_copy_string(pvt->exten, cd->call_dest_e164, sizeof(pvt->exten));
			} else {
				ast_copy_string(pvt->exten, cd->call_dest_alias, sizeof(pvt->exten));
			}
			if (h323debug)
				ast_log(LOG_DEBUG, "Sending %s@%s to context [%s] extension %s\n", cd->call_source_aliases, cd->sourceIp, pvt->context, pvt->exten);
		} else {
			if (user->host) {
				if (strcasecmp(cd->sourceIp, ast_inet_ntoa(user->addr.sin_addr))) {
					if (ast_strlen_zero(user->context)) {
						if (ast_strlen_zero(default_context)) {
							ast_log(LOG_ERROR, "Call from '%s' rejected due to non-matching IP address (%s) and no default context\n", user->name, cd->sourceIp);
							oh323_destroy(pvt);
							ASTOBJ_UNREF(user, oh323_destroy_user);
							return NULL;
						}
						ast_copy_string(pvt->context, default_context, sizeof(pvt->context));
					} else {
						ast_copy_string(pvt->context, user->context, sizeof(pvt->context));
					}
					pvt->exten[0] = 'i';
					pvt->exten[1] = '\0';
					ast_log(LOG_ERROR, "Call from '%s' rejected due to non-matching IP address (%s)s\n", user->name, cd->sourceIp);
					oh323_destroy(pvt);
					ASTOBJ_UNREF(user, oh323_destroy_user);
					return NULL;	/* XXX: Hmmm... Why to setup context if we drop connection immediately??? */
				}
			}
			ast_copy_string(pvt->context, user->context, sizeof(pvt->context));
			memcpy(&pvt->options, &user->options, sizeof(pvt->options));
			pvt->jointcapability = pvt->options.capability;
			if (!ast_strlen_zero(pvt->cd.call_dest_e164)) {
				ast_copy_string(pvt->exten, cd->call_dest_e164, sizeof(pvt->exten));
			} else {
				ast_copy_string(pvt->exten, cd->call_dest_alias, sizeof(pvt->exten));
			}
			if (!ast_strlen_zero(user->accountcode)) {
				ast_copy_string(pvt->accountcode, user->accountcode, sizeof(pvt->accountcode));
			}
			if (user->amaflags) {
				pvt->amaflags = user->amaflags;
			}
			ASTOBJ_UNREF(user, oh323_destroy_user);
		}
	}
	return &pvt->options;
}

/**
 * Call-back function to start PBX when OpenH323 ready to serve incoming call
 *
 * Returns 1 on success
 */
static int answer_call(unsigned call_reference, const char *token)
{
	struct oh323_pvt *pvt;
	struct ast_channel *c = NULL;
	enum {ext_original, ext_s, ext_i, ext_notexists} try_exten;
	char tmp_exten[sizeof(pvt->exten)];

	if (h323debug)
		ast_log(LOG_DEBUG, "Preparing Asterisk to answer for %s\n", token);

	/* Find the call or allocate a private structure if call not found */
	pvt = find_call_locked(call_reference, token);
	if (!pvt) {
		ast_log(LOG_ERROR, "Something is wrong: answer_call\n");
		return 0;
	}
	/* Check if requested extension@context pair exists in the dialplan */
	ast_copy_string(tmp_exten, pvt->exten, sizeof(tmp_exten));

	/* Try to find best extension in specified context */
	if ((tmp_exten[0] != '\0') && (tmp_exten[1] == '\0')) {
		if (tmp_exten[0] == 's')
			try_exten = ext_s;
		else if (tmp_exten[0] == 'i')
			try_exten = ext_i;
		else
			try_exten = ext_original;
	} else
		try_exten = ext_original;
	do {
		if (ast_exists_extension(NULL, pvt->context, tmp_exten, 1, NULL))
			break;
		switch (try_exten) {
		case ext_original:
			tmp_exten[0] = 's';
			tmp_exten[1] = '\0';
			try_exten = ext_s;
			break;
		case ext_s:
			tmp_exten[0] = 'i';
			try_exten = ext_i;
			break;
		case ext_i:
			try_exten = ext_notexists;
			break;
		default:
			break;
		}
	} while (try_exten != ext_notexists);

	/* Drop the call if we don't have <exten>, s and i extensions */
	if (try_exten == ext_notexists) {
		ast_log(LOG_NOTICE, "Dropping call because extensions '%s', 's' and 'i' doesn't exists in context [%s]\n", pvt->exten, pvt->context);
		ast_mutex_unlock(&pvt->lock);
		h323_clear_call(token, AST_CAUSE_UNALLOCATED);
		return 0;
	} else if ((try_exten != ext_original) && (strcmp(pvt->exten, tmp_exten) != 0)) {
		if (h323debug)
			ast_log(LOG_DEBUG, "Going to extension %s@%s because %s@%s isn't exists\n", tmp_exten, pvt->context, pvt->exten, pvt->context);
		ast_copy_string(pvt->exten, tmp_exten, sizeof(pvt->exten));
	}

	/* allocate a channel and tell asterisk about it */
	c = __oh323_new(pvt, AST_STATE_RINGING, pvt->cd.call_token);

	/* And release when done */
	ast_mutex_unlock(&pvt->lock);
	if (!c) {
		ast_log(LOG_ERROR, "Couldn't create channel. This is bad\n");
		return 0;
	}
	return 1;
}

/**
 * Call-back function to establish an outgoing H.323 call
 *
 * Returns 1 on success
 */
static int setup_outgoing_call(call_details_t *cd)
{
	/* Use argument here or free it immediately */
	cleanup_call_details(cd);

	return 1;
}

/**
  *  Call-back function to signal asterisk that the channel is ringing
  *  Returns nothing
  */
static void chan_ringing(unsigned call_reference, const char *token)
{
	struct oh323_pvt *pvt;

	if (h323debug)
		ast_log(LOG_DEBUG, "Ringing on %s\n", token);

	pvt = find_call_locked(call_reference, token);
	if (!pvt) {
		ast_log(LOG_ERROR, "Something is wrong: ringing\n");
		return;
	}
	if (!pvt->owner) {
		ast_mutex_unlock(&pvt->lock);
		ast_log(LOG_ERROR, "Channel has no owner\n");
		return;
	}
	update_state(pvt, AST_STATE_RINGING, AST_CONTROL_RINGING);
	ast_mutex_unlock(&pvt->lock);
	return;
}

/**
  * Call-back function to cleanup communication
  * Returns nothing,
  */
static void cleanup_connection(unsigned call_reference, const char *call_token)
{
	struct oh323_pvt *pvt;

	if (h323debug)
		ast_log(LOG_DEBUG, "Cleaning connection to %s\n", call_token);

	while (1) {
		pvt = find_call_locked(call_reference, call_token);
		if (!pvt) {
			if (h323debug)
				ast_log(LOG_DEBUG, "No connection for %s\n", call_token);
			return;
		}
		if (!pvt->owner || !ast_channel_trylock(pvt->owner))
			break;
#if 1
#ifdef DEBUG_THREADS
		ast_log(LOG_NOTICE, "Avoiding H.323 destory deadlock on %s, locked at %ld/%d by %s (%s:%d)\n", call_token, pvt->owner->lock.thread[0], pvt->owner->lock.reentrancy, pvt->owner->lock.func[0], pvt->owner->lock.file[0], pvt->owner->lock.lineno[0]);
#else
		ast_log(LOG_NOTICE, "Avoiding H.323 destory deadlock on %s\n", call_token);
#endif
#endif
		ast_mutex_unlock(&pvt->lock);
		usleep(1);
	}
	if (pvt->rtp) {
		/* Immediately stop RTP */
		ast_rtp_destroy(pvt->rtp);
		pvt->rtp = NULL;
	}
	/* Free dsp used for in-band DTMF detection */
	if (pvt->vad) {
		ast_dsp_free(pvt->vad);
		pvt->vad = NULL;
	}
	cleanup_call_details(&pvt->cd);
	pvt->alreadygone = 1;
	/* Send hangup */
	if (pvt->owner) {
		pvt->owner->_softhangup |= AST_SOFTHANGUP_DEV;
		ast_queue_hangup(pvt->owner);
		ast_channel_unlock(pvt->owner);
	}
	ast_mutex_unlock(&pvt->lock);
	if (h323debug)
		ast_log(LOG_DEBUG, "Connection to %s cleaned\n", call_token);
	return;
}

static void hangup_connection(unsigned int call_reference, const char *token, int cause)
{
	struct oh323_pvt *pvt;

	if (h323debug) {
		ast_log(LOG_DEBUG, "Hanging up connection to %s with cause %d\n", token, cause);
	}

	pvt = find_call_locked(call_reference, token);
	if (!pvt) {
		if (h323debug) {
			ast_log(LOG_DEBUG, "Connection to %s already cleared\n", token);
		}
		return;
	}
	if (pvt->owner && !ast_channel_trylock(pvt->owner)) {
		pvt->owner->_softhangup |= AST_SOFTHANGUP_DEV;
		pvt->owner->hangupcause = pvt->hangupcause = cause;
		ast_queue_hangup(pvt->owner);
		ast_channel_unlock(pvt->owner);
	}
	else {
		pvt->needhangup = 1;
		pvt->hangupcause = cause;
		if (h323debug)
			ast_log(LOG_DEBUG, "Hangup for %s is pending\n", token);
	}
	ast_mutex_unlock(&pvt->lock);
}

static void set_dtmf_payload(unsigned call_reference, const char *token, int payload)
{
	struct oh323_pvt *pvt;

	if (h323debug)
		ast_log(LOG_DEBUG, "Setting DTMF payload to %d on %s\n", payload, token);

	pvt = find_call_locked(call_reference, token);
	if (!pvt) {
		return;
	}
	if (pvt->rtp) {
		ast_rtp_set_rtpmap_type(pvt->rtp, payload, "audio", "telephone-event", 0);
	}
	pvt->dtmf_pt = payload;
	ast_mutex_unlock(&pvt->lock);
	if (h323debug)
		ast_log(LOG_DEBUG, "DTMF payload on %s set to %d\n", token, payload);
}

static void set_peer_capabilities(unsigned call_reference, const char *token, int capabilities, struct ast_codec_pref *prefs)
{
	struct oh323_pvt *pvt;

	if (h323debug)
		ast_log(LOG_DEBUG, "Got remote capabilities from connection %s\n", token);

	pvt = find_call_locked(call_reference, token);
	if (!pvt)
		return;
	pvt->peercapability = capabilities;
	pvt->jointcapability = pvt->options.capability & capabilities;
	if (prefs) {
		memcpy(&pvt->peer_prefs, prefs, sizeof(pvt->peer_prefs));
		if (h323debug) {
			int i;
			for (i = 0; i < 32; ++i) {
				if (!prefs->order[i])
					break;
				ast_log(LOG_DEBUG, "prefs[%d]=%s:%d\n", i, (prefs->order[i] ? ast_getformatname(1 << (prefs->order[i]-1)) : "<none>"), prefs->framing[i]);
			}
		}
		if (pvt->rtp) {
			if (pvt->options.autoframing) {
				ast_log(LOG_DEBUG, "Autoframing option set, using peer's packetization settings\n");
				ast_rtp_codec_setpref(pvt->rtp, &pvt->peer_prefs);
			} else {
				ast_log(LOG_DEBUG, "Autoframing option not set, using ignoring peer's packetization settings\n");
				ast_rtp_codec_setpref(pvt->rtp, &pvt->options.prefs);
			}
		}
	}
	ast_mutex_unlock(&pvt->lock);
}

static void set_local_capabilities(unsigned call_reference, const char *token)
{
	struct oh323_pvt *pvt;
	int capability, dtmfmode, pref_codec;
	struct ast_codec_pref prefs;

	if (h323debug)
		ast_log(LOG_DEBUG, "Setting capabilities for connection %s\n", token);

	pvt = find_call_locked(call_reference, token);
	if (!pvt)
		return;
	capability = (pvt->jointcapability) ? pvt->jointcapability : pvt->options.capability;
	dtmfmode = pvt->options.dtmfmode;
	prefs = pvt->options.prefs;
	pref_codec = pvt->pref_codec;
	ast_mutex_unlock(&pvt->lock);
	h323_set_capabilities(token, capability, dtmfmode, &prefs, pref_codec);

	if (h323debug) {
		int i;
		for (i = 0; i < 32; i++) {
			if (!prefs.order[i])
				break;
			ast_log(LOG_DEBUG, "local prefs[%d]=%s:%d\n", i, (prefs.order[i] ? ast_getformatname(1 << (prefs.order[i]-1)) : "<none>"), prefs.framing[i]);
		}
		ast_log(LOG_DEBUG, "Capabilities for connection %s is set\n", token);
	}
}

static void *do_monitor(void *data)
{
	int res;
	int reloading;
	struct oh323_pvt *oh323 = NULL;

	for(;;) {
		/* Check for a reload request */
		ast_mutex_lock(&h323_reload_lock);
		reloading = h323_reloading;
		h323_reloading = 0;
		ast_mutex_unlock(&h323_reload_lock);
		if (reloading) {
			if (option_verbose > 0) {
				ast_verbose(VERBOSE_PREFIX_1 "Reloading H.323\n");
			}
			h323_do_reload();
		}
		/* Check for interfaces needing to be killed */
		if (!ast_mutex_trylock(&iflock)) {
#if 1
			do {
				for (oh323 = iflist; oh323; oh323 = oh323->next) {
					if (!ast_mutex_trylock(&oh323->lock)) {
						if (oh323->needdestroy) {
							__oh323_destroy(oh323);
							break;
						}
						ast_mutex_unlock(&oh323->lock);
					}
				}
			} while (/*oh323*/ 0);
#else
restartsearch:
			oh323 = iflist;
			while(oh323) {
				if (!ast_mutex_trylock(&oh323->lock)) {
					if (oh323->needdestroy) {
						__oh323_destroy(oh323);
						goto restartsearch;
					}
					ast_mutex_unlock(&oh323->lock);
					oh323 = oh323->next;
				}
			}
#endif
			ast_mutex_unlock(&iflock);
		} else
			oh323 = (struct oh323_pvt *)1;	/* Force fast loop */
		pthread_testcancel();
		/* Wait for sched or io */
		res = ast_sched_wait(sched);
		if ((res < 0) || (res > 1000)) {
			res = 1000;
		}
		/* Do not wait if some channel(s) is destroyed, probably, more available too */
		if (oh323)
			res = 1;
		res = ast_io_wait(io, res);
		pthread_testcancel();
		ast_mutex_lock(&monlock);
		if (res >= 0) {
			ast_sched_runq(sched);
		}
		ast_mutex_unlock(&monlock);
	}
	/* Never reached */
	return NULL;
}

static int restart_monitor(void)
{
	/* If we're supposed to be stopped -- stay stopped */
	if (ast_mutex_lock(&monlock)) {
		ast_log(LOG_WARNING, "Unable to lock monitor\n");
		return -1;
	}
	if (monitor_thread == AST_PTHREADT_STOP) {
		ast_mutex_unlock(&monlock);
		return 0;
	}
	if (monitor_thread == pthread_self()) {
		ast_mutex_unlock(&monlock);
		ast_log(LOG_WARNING, "Cannot kill myself\n");
		return -1;
	}
	if (monitor_thread && (monitor_thread != AST_PTHREADT_NULL)) {
		/* Wake up the thread */
		pthread_kill(monitor_thread, SIGURG);
	} else {
		/* Start a new monitor */
		if (ast_pthread_create_background(&monitor_thread, NULL, do_monitor, NULL) < 0) {
			monitor_thread = AST_PTHREADT_NULL;
			ast_mutex_unlock(&monlock);
			ast_log(LOG_ERROR, "Unable to start monitor thread.\n");
			return -1;
		}
	}
	ast_mutex_unlock(&monlock);
	return 0;
}

static int h323_do_trace(int fd, int argc, char *argv[])
{
	if (argc != 4) {
		return RESULT_SHOWUSAGE;
	}
	h323_debug(1, atoi(argv[3]));
	ast_cli(fd, "H.323 trace set to level %s\n", argv[2]);
	return RESULT_SUCCESS;
}

static int h323_no_trace(int fd, int argc, char *argv[])
{
	if (argc < 3 || argc > 4) {
		return RESULT_SHOWUSAGE;
	}
	h323_debug(0,0);
	ast_cli(fd, "H.323 trace disabled\n");
	return RESULT_SUCCESS;
}

static int h323_do_debug(int fd, int argc, char *argv[])
{
	if (argc < 2 || argc > 3) {
		return RESULT_SHOWUSAGE;
	}
	h323debug = 1;
	ast_cli(fd, "H.323 debug enabled\n");
	return RESULT_SUCCESS;
}

static int h323_no_debug(int fd, int argc, char *argv[])
{
	if (argc < 3 || argc > 4) {
		return RESULT_SHOWUSAGE;
	}
	h323debug = 0;
	ast_cli(fd, "H.323 debug disabled\n");
	return RESULT_SUCCESS;
}

static int h323_gk_cycle(int fd, int argc, char *argv[])
{
	if (argc != 3) {
		return RESULT_SHOWUSAGE;
	}
	h323_gk_urq();

	/* Possibly register with a GK */
	if (!gatekeeper_disable) {
		if (h323_set_gk(gatekeeper_discover, gatekeeper, secret)) {
			ast_log(LOG_ERROR, "Gatekeeper registration failed.\n");
		}
	}
	return RESULT_SUCCESS;
}

static int h323_ep_hangup(int fd, int argc, char *argv[])
{
	if (argc != 3) {
		return RESULT_SHOWUSAGE;
	}
	if (h323_soft_hangup(argv[2])) {
		ast_verbose(VERBOSE_PREFIX_3 "Hangup succeeded on %s\n", argv[2]);
	} else {
		ast_verbose(VERBOSE_PREFIX_3 "Hangup failed for %s\n", argv[2]);
	}
	return RESULT_SUCCESS;
}

static int h323_tokens_show(int fd, int argc, char *argv[])
{
	if (argc != 3) {
		return RESULT_SHOWUSAGE;
	}
	h323_show_tokens();
	return RESULT_SUCCESS;
}

static int h323_version_show(int fd, int argc, char *argv[])
{
	if (argc != 3) {
		return RESULT_SHOWUSAGE;
	}
	h323_show_version();
	return RESULT_SUCCESS;
}

static char trace_usage[] =
"Usage: h.323 trace <level num>\n"
"       Enables H.323 stack tracing for debugging purposes\n";

static char no_trace_usage[] =
"Usage: h.323 trace off\n"
"       Disables H.323 stack tracing for debugging purposes\n";

static char debug_usage[] =
"Usage: h.323 debug\n"
"       Enables H.323 debug output\n";

static char no_debug_usage[] =
"Usage: h.323 debug off\n"
"       Disables H.323 debug output\n";

static char show_cycle_usage[] =
"Usage: h.323 gk cycle\n"
"       Manually re-register with the Gatekeper (Currently Disabled)\n";

static char show_hangup_usage[] =
"Usage: h.323 hangup <token>\n"
"       Manually try to hang up call identified by <token>\n";

static char show_tokens_usage[] =
"Usage: h.323 show tokens\n"
"       Print out all active call tokens\n";

static char show_version_usage[] =
"Usage: h.323 show version\n"
"		Print the version of the H.323 library in use\n";

static char h323_reload_usage[] =
"Usage: h323 reload\n"
"       Reloads H.323 configuration from h323.conf\n";

static struct ast_cli_entry cli_h323_no_trace_deprecated = {
	{ "h.323", "no", "trace", NULL },
	h323_no_trace, "Disable H.323 Stack Tracing",
	no_trace_usage };

static struct ast_cli_entry cli_h323_no_debug_deprecated = {
	{ "h.323", "no", "debug", NULL },
	h323_no_debug, "Disable H.323 debug",
	no_debug_usage };

static struct ast_cli_entry cli_h323_debug_deprecated = {
	{ "h.323", "debug", NULL },
	h323_do_debug, "Enable H.323 debug",
	debug_usage };

static struct ast_cli_entry cli_h323_trace_deprecated = {
	{ "h.323", "trace", NULL },
	h323_do_trace, "Enable H.323 Stack Tracing",
	trace_usage };

static struct ast_cli_entry cli_h323_gk_cycle_deprecated = {
	{ "h.323", "gk", "cycle", NULL },
	h323_gk_cycle, "Manually re-register with the Gatekeper",
	show_cycle_usage };

static struct ast_cli_entry cli_h323[] = {
	{ { "h323", "set", "trace", NULL },
	h323_do_trace, "Enable H.323 Stack Tracing",
	trace_usage, NULL, &cli_h323_trace_deprecated },

	{ { "h323", "set", "trace", "off", NULL },
	h323_no_trace, "Disable H.323 Stack Tracing",
	no_trace_usage, NULL, &cli_h323_no_trace_deprecated },

	{ { "h323", "set", "debug", NULL },
	h323_do_debug, "Enable H.323 debug",
	debug_usage, NULL, &cli_h323_debug_deprecated },

	{ { "h323", "set", "debug", "off", NULL },
	h323_no_debug, "Disable H.323 debug",
	no_debug_usage, NULL, &cli_h323_no_debug_deprecated },

	{ { "h323", "cycle", "gk", NULL },
	h323_gk_cycle, "Manually re-register with the Gatekeper",
	show_cycle_usage, NULL, &cli_h323_gk_cycle_deprecated },

	{ { "h323", "hangup", NULL },
	h323_ep_hangup, "Manually try to hang up a call",
	show_hangup_usage },

	{ { "h323", "show", "tokens", NULL },
	h323_tokens_show, "Show all active call tokens",
	show_tokens_usage },

	{ { "h323", "show", "version", NULL },
	h323_version_show, "Show the version of the H.323 library in use",
	show_version_usage },
};

static void delete_users(void)
{
	int pruned = 0;

	/* Delete all users */
	ASTOBJ_CONTAINER_WRLOCK(&userl);
	ASTOBJ_CONTAINER_TRAVERSE(&userl, 1, do {
		ASTOBJ_RDLOCK(iterator);
		ASTOBJ_MARK(iterator);
		++pruned;
		ASTOBJ_UNLOCK(iterator);
	} while (0) );
	if (pruned) {
		ASTOBJ_CONTAINER_PRUNE_MARKED(&userl, oh323_destroy_user);
	}
	ASTOBJ_CONTAINER_UNLOCK(&userl);

	ASTOBJ_CONTAINER_WRLOCK(&peerl);
	ASTOBJ_CONTAINER_TRAVERSE(&peerl, 1, do {
		ASTOBJ_RDLOCK(iterator);
		ASTOBJ_MARK(iterator);
		ASTOBJ_UNLOCK(iterator);
	} while (0) );
	ASTOBJ_CONTAINER_UNLOCK(&peerl);
}

static void delete_aliases(void)
{
	int pruned = 0;

	/* Delete all aliases */
	ASTOBJ_CONTAINER_WRLOCK(&aliasl);
	ASTOBJ_CONTAINER_TRAVERSE(&aliasl, 1, do {
		ASTOBJ_RDLOCK(iterator);
		ASTOBJ_MARK(iterator);
		++pruned;
		ASTOBJ_UNLOCK(iterator);
	} while (0) );
	if (pruned) {
		ASTOBJ_CONTAINER_PRUNE_MARKED(&aliasl, oh323_destroy_alias);
	}
	ASTOBJ_CONTAINER_UNLOCK(&aliasl);
}

static void prune_peers(void)
{
	/* Prune peers who still are supposed to be deleted */
	ASTOBJ_CONTAINER_PRUNE_MARKED(&peerl, oh323_destroy_peer);
}

static int reload_config(int is_reload)
{
	int format;
	struct ast_config *cfg, *ucfg;
	struct ast_variable *v;
	struct oh323_peer *peer = NULL;
	struct oh323_user *user = NULL;
	struct oh323_alias *alias = NULL;
	struct ast_hostent ahp; struct hostent *hp;
	char *cat;
	const char *utype;
	int is_user, is_peer, is_alias;
	char _gatekeeper[100];
	int gk_discover, gk_disable, gk_changed;

	cfg = ast_config_load(config);

	/* We *must* have a config file otherwise stop immediately */
	if (!cfg) {
		ast_log(LOG_NOTICE, "Unable to load config %s, H.323 disabled\n", config);
		return 1;
	}

	if (is_reload) {
		delete_users();
		delete_aliases();
		prune_peers();
	}

	/* fire up the H.323 Endpoint */
	if (!h323_end_point_exist()) {
		h323_end_point_create();
	}
	ast_copy_string(_gatekeeper, gatekeeper, sizeof(_gatekeeper));
	gk_discover = gatekeeper_discover;
	gk_disable = gatekeeper_disable;
	memset(&bindaddr, 0, sizeof(bindaddr));
	memset(&global_options, 0, sizeof(global_options));
	global_options.fastStart = 1;
	global_options.h245Tunneling = 1;
	global_options.dtmfcodec = 101;
	global_options.dtmfmode = H323_DTMF_RFC2833;
	global_options.capability = GLOBAL_CAPABILITY;
	global_options.bridge = 1;		/* Do native bridging by default */
	global_options.autoframing = 0;
	strcpy(default_context, "default");
	h323_signalling_port = 1720;
	gatekeeper_disable = 1;
	gatekeeper_discover = 0;
	gkroute = 0;
	userbyalias = 1;
	acceptAnonymous = 1;
	tos = 0;

	/* Copy the default jb config over global_jbconf */
	memcpy(&global_jbconf, &default_jbconf, sizeof(struct ast_jb_conf));

	/* Load configuration from users.conf */
	ucfg = ast_config_load("users.conf");
	if (ucfg) {
		struct ast_variable *gen;
		int genhas_h323;
		const char *has_h323;

		genhas_h323 = ast_true(ast_variable_retrieve(ucfg, "general", "hash323"));
		gen = ast_variable_browse(ucfg, "general");
		for (cat = ast_category_browse(ucfg, NULL); cat; cat = ast_category_browse(ucfg, cat)) {
			if (strcasecmp(cat, "general")) {
				has_h323 = ast_variable_retrieve(ucfg, cat, "hash323");
				if (ast_true(has_h323) || (!has_h323 && genhas_h323)) {
					user = build_user(cat, gen, ast_variable_browse(ucfg, cat), 0);
					if (user) {
						ASTOBJ_CONTAINER_LINK(&userl, user);
						ASTOBJ_UNREF(user, oh323_destroy_user);
					}
					peer = build_peer(cat, gen, ast_variable_browse(ucfg, cat), 0);
					if (peer) {
						ASTOBJ_CONTAINER_LINK(&peerl, peer);
						ASTOBJ_UNREF(peer, oh323_destroy_peer);
					}
				}
			}
		}
		ast_config_destroy(ucfg);
	}

	for (v = ast_variable_browse(cfg, "general"); v; v = v->next) {
		/* handle jb conf */
		if (!ast_jb_read_conf(&global_jbconf, v->name, v->value))
			continue;
		/* Create the interface list */
		if (!strcasecmp(v->name, "port")) {
			h323_signalling_port = (int)strtol(v->value, NULL, 10);
		} else if (!strcasecmp(v->name, "bindaddr")) {
			if (!(hp = ast_gethostbyname(v->value, &ahp))) {
				ast_log(LOG_WARNING, "Invalid address: %s\n", v->value);
			} else {
				memcpy(&bindaddr.sin_addr, hp->h_addr, sizeof(bindaddr.sin_addr));
			}
		} else if (!strcasecmp(v->name, "tos")) {
			if (sscanf(v->value, "%30d", &format)) {
				tos = format & 0xff;
			} else if (!strcasecmp(v->value, "lowdelay")) {
				tos = IPTOS_LOWDELAY;
			} else if (!strcasecmp(v->value, "throughput")) {
				tos = IPTOS_THROUGHPUT;
			} else if (!strcasecmp(v->value, "reliability")) {
				tos = IPTOS_RELIABILITY;
			} else if (!strcasecmp(v->value, "mincost")) {
				tos = IPTOS_MINCOST;
			} else if (!strcasecmp(v->value, "none")) {
				tos = 0;
			} else {
				ast_log(LOG_WARNING, "Invalid tos value at line %d, should be 'lowdelay', 'throughput', 'reliability', 'mincost', or 'none'\n", v->lineno);
			}
		} else if (!strcasecmp(v->name, "gatekeeper")) {
			if (!strcasecmp(v->value, "DISABLE")) {
				gatekeeper_disable = 1;
			} else if (!strcasecmp(v->value, "DISCOVER")) {
				gatekeeper_disable = 0;
				gatekeeper_discover = 1;
			} else {
				gatekeeper_disable = 0;
				ast_copy_string(gatekeeper, v->value, sizeof(gatekeeper));
			}
		} else if (!strcasecmp(v->name, "secret")) {
			ast_copy_string(secret, v->value, sizeof(secret));
		} else if (!strcasecmp(v->name, "AllowGKRouted")) {
			gkroute = ast_true(v->value);
		} else if (!strcasecmp(v->name, "context")) {
			ast_copy_string(default_context, v->value, sizeof(default_context));
			ast_verbose(VERBOSE_PREFIX_2 "Setting default context to %s\n", default_context);
		} else if (!strcasecmp(v->name, "UserByAlias")) {
			userbyalias = ast_true(v->value);
		} else if (!strcasecmp(v->name, "AcceptAnonymous")) {
			acceptAnonymous = ast_true(v->value);
		} else if (!update_common_options(v, &global_options)) {
			/* dummy */
		}
	}

	for (cat = ast_category_browse(cfg, NULL); cat; cat = ast_category_browse(cfg, cat)) {
		if (strcasecmp(cat, "general")) {
			utype = ast_variable_retrieve(cfg, cat, "type");
			if (utype) {
				is_user = is_peer = is_alias = 0;
				if (!strcasecmp(utype, "user"))
					is_user = 1;
				else if (!strcasecmp(utype, "peer"))
					is_peer = 1;
				else if (!strcasecmp(utype, "friend"))
					is_user = is_peer = 1;
				else if (!strcasecmp(utype, "h323") || !strcasecmp(utype, "alias"))
					is_alias = 1;
				else {
					ast_log(LOG_WARNING, "Unknown type '%s' for '%s' in %s\n", utype, cat, config);
					continue;
				}
				if (is_user) {
					user = build_user(cat, ast_variable_browse(cfg, cat), NULL, 0);
					if (user) {
						ASTOBJ_CONTAINER_LINK(&userl, user);
						ASTOBJ_UNREF(user, oh323_destroy_user);
					}
				}
				if (is_peer) {
					peer = build_peer(cat, ast_variable_browse(cfg, cat), NULL, 0);
					if (peer) {
						ASTOBJ_CONTAINER_LINK(&peerl, peer);
						ASTOBJ_UNREF(peer, oh323_destroy_peer);
					}
				}
				if (is_alias) {
					alias = build_alias(cat, ast_variable_browse(cfg, cat), NULL, 0);
					if (alias) {
						ASTOBJ_CONTAINER_LINK(&aliasl, alias);
						ASTOBJ_UNREF(alias, oh323_destroy_alias);
					}
				}
			} else {
				ast_log(LOG_WARNING, "Section '%s' lacks type\n", cat);
			}
		}
	}
	ast_config_destroy(cfg);

	/* Register our H.323 aliases if any*/
	ASTOBJ_CONTAINER_WRLOCK(&aliasl);
	ASTOBJ_CONTAINER_TRAVERSE(&aliasl, 1, do {
		ASTOBJ_RDLOCK(iterator);
		if (h323_set_alias(iterator)) {
			ast_log(LOG_ERROR, "Alias %s rejected by endpoint\n", alias->name);
			ASTOBJ_UNLOCK(iterator);
			continue;
		}
		ASTOBJ_UNLOCK(iterator);
	} while (0) );
	ASTOBJ_CONTAINER_UNLOCK(&aliasl);

	/* Don't touch GK if nothing changed because URQ will drop all existing calls */
	gk_changed = 0;
	if (gatekeeper_disable != gk_disable)
		gk_changed = is_reload;
	else if(!gatekeeper_disable && (gatekeeper_discover != gk_discover))
		gk_changed = is_reload;
	else if(!gatekeeper_disable && (strncmp(_gatekeeper, gatekeeper, sizeof(_gatekeeper)) != 0))
		gk_changed = is_reload;
	if (gk_changed) {
		if(!gk_disable)
			h323_gk_urq();
		if (!gatekeeper_disable) {
			if (h323_set_gk(gatekeeper_discover, gatekeeper, secret)) {
				ast_log(LOG_ERROR, "Gatekeeper registration failed.\n");
				gatekeeper_disable = 1;
			}
		}
	}
	return 0;
}

static int h323_reload(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&h323_reload_lock);
	if (h323_reloading) {
		ast_verbose("Previous H.323 reload not yet done\n");
	} else {
		h323_reloading = 1;
	}
	ast_mutex_unlock(&h323_reload_lock);
	restart_monitor();
	return 0;
}

static int h323_do_reload(void)
{
	reload_config(1);
	return 0;
}

static int reload(void)
{
	if (!sched || !io) {
		ast_log(LOG_NOTICE, "Unload and load chan_h323.so again in order to receive configuration changes.\n");
		return 0;
	}
	return h323_reload(0, 0, NULL);
}

static struct ast_cli_entry cli_h323_reload =
	{ { "h.323", "reload", NULL },
	h323_reload, "Reload H.323 configuration",
	h323_reload_usage
};

static enum ast_rtp_get_result oh323_get_rtp_peer(struct ast_channel *chan, struct ast_rtp **rtp)
{
	struct oh323_pvt *pvt;
	enum ast_rtp_get_result res = AST_RTP_GET_FAILED;

	if (!(pvt = (struct oh323_pvt *)chan->tech_pvt))
		return res;

	ast_mutex_lock(&pvt->lock);
	if (pvt->rtp && pvt->options.bridge) {
		*rtp = pvt->rtp;
		res = AST_RTP_TRY_NATIVE;
	}
	ast_mutex_unlock(&pvt->lock);

	return res;
}

static enum ast_rtp_get_result oh323_get_vrtp_peer(struct ast_channel *chan, struct ast_rtp **rtp)
{
	return AST_RTP_GET_FAILED;
}

static char *convertcap(int cap)
{
	switch (cap) {
	case AST_FORMAT_G723_1:
		return "G.723";
	case AST_FORMAT_GSM:
		return "GSM";
	case AST_FORMAT_ULAW:
		return "ULAW";
	case AST_FORMAT_ALAW:
		return "ALAW";
	case AST_FORMAT_G722:
		return "G.722";
	case AST_FORMAT_ADPCM:
		return "G.728";
	case AST_FORMAT_G729A:
		return "G.729";
	case AST_FORMAT_SPEEX:
		return "SPEEX";
	case AST_FORMAT_ILBC:
		return "ILBC";
	default:
		ast_log(LOG_NOTICE, "Don't know how to deal with mode %d\n", cap);
		return NULL;
	}
}

static int oh323_set_rtp_peer(struct ast_channel *chan, struct ast_rtp *rtp, struct ast_rtp *vrtp, int codecs, int nat_active)
{
	/* XXX Deal with Video */
	struct oh323_pvt *pvt;
	struct sockaddr_in them;
	struct sockaddr_in us;
	char *mode;

	if (!rtp) {
		return 0;
	}

	mode = convertcap(chan->writeformat);
	pvt = (struct oh323_pvt *) chan->tech_pvt;
	if (!pvt) {
		ast_log(LOG_ERROR, "No Private Structure, this is bad\n");
		return -1;
	}
	ast_rtp_get_peer(rtp, &them);
	ast_rtp_get_us(rtp, &us);
#if 0	/* Native bridge still isn't ready */
	h323_native_bridge(pvt->cd.call_token, ast_inet_ntoa(them.sin_addr), mode);
#endif
	return 0;
}

static struct ast_rtp_protocol oh323_rtp = {
	.type = "H323",
	.get_rtp_info = oh323_get_rtp_peer,
	.get_vrtp_info = oh323_get_vrtp_peer,
	.set_rtp_peer = oh323_set_rtp_peer,
};

static enum ast_module_load_result load_module(void)
{
	int res;

	h323debug = 0;
	sched = sched_context_create();
	if (!sched) {
		ast_log(LOG_WARNING, "Unable to create schedule context\n");
		return AST_MODULE_LOAD_FAILURE;
	}
	io = io_context_create();
	if (!io) {
		ast_log(LOG_WARNING, "Unable to create I/O context\n");
		return AST_MODULE_LOAD_FAILURE;
	}
	ast_cli_register(&cli_h323_reload);
	ASTOBJ_CONTAINER_INIT(&userl);
	ASTOBJ_CONTAINER_INIT(&peerl);
	ASTOBJ_CONTAINER_INIT(&aliasl);
	res = reload_config(0);
	if (res) {
		/* No config entry */
		ast_log(LOG_NOTICE, "Unload and load chan_h323.so again in order to receive configuration changes.\n");
		ast_cli_unregister(&cli_h323_reload);
		io_context_destroy(io);
		io = NULL;
		sched_context_destroy(sched);
		sched = NULL;
		ASTOBJ_CONTAINER_DESTROY(&userl);
		ASTOBJ_CONTAINER_DESTROY(&peerl);
		ASTOBJ_CONTAINER_DESTROY(&aliasl);
		return AST_MODULE_LOAD_DECLINE;
	} else {
		/* Make sure we can register our channel type */
		if (ast_channel_register(&oh323_tech)) {
			ast_log(LOG_ERROR, "Unable to register channel class 'H323'\n");
			ast_cli_unregister(&cli_h323_reload);
			h323_end_process();
			io_context_destroy(io);
			sched_context_destroy(sched);

			ASTOBJ_CONTAINER_DESTROYALL(&userl, oh323_destroy_user);
			ASTOBJ_CONTAINER_DESTROY(&userl);
			ASTOBJ_CONTAINER_DESTROYALL(&peerl, oh323_destroy_peer);
			ASTOBJ_CONTAINER_DESTROY(&peerl);
			ASTOBJ_CONTAINER_DESTROYALL(&aliasl, oh323_destroy_alias);
			ASTOBJ_CONTAINER_DESTROY(&aliasl);

			return AST_MODULE_LOAD_FAILURE;
		}
		ast_cli_register_multiple(cli_h323, sizeof(cli_h323) / sizeof(struct ast_cli_entry));

		ast_rtp_proto_register(&oh323_rtp);

		/* Register our callback functions */
		h323_callback_register(setup_incoming_call,
						setup_outgoing_call,
						external_rtp_create,
						setup_rtp_connection,
						cleanup_connection,
						chan_ringing,
						connection_made,
						receive_digit,
						answer_call,
						progress,
						set_dtmf_payload,
						hangup_connection,
						set_local_capabilities,
						set_peer_capabilities);
		/* start the h.323 listener */
		if (h323_start_listener(h323_signalling_port, bindaddr)) {
			ast_log(LOG_ERROR, "Unable to create H323 listener.\n");
			ast_rtp_proto_unregister(&oh323_rtp);
			ast_cli_unregister_multiple(cli_h323, sizeof(cli_h323) / sizeof(struct ast_cli_entry));
			ast_cli_unregister(&cli_h323_reload);
			h323_end_process();
			io_context_destroy(io);
			sched_context_destroy(sched);

			ASTOBJ_CONTAINER_DESTROYALL(&userl, oh323_destroy_user);
			ASTOBJ_CONTAINER_DESTROY(&userl);
			ASTOBJ_CONTAINER_DESTROYALL(&peerl, oh323_destroy_peer);
			ASTOBJ_CONTAINER_DESTROY(&peerl);
			ASTOBJ_CONTAINER_DESTROYALL(&aliasl, oh323_destroy_alias);
			ASTOBJ_CONTAINER_DESTROY(&aliasl);

			return AST_MODULE_LOAD_FAILURE;
		}
		/* Possibly register with a GK */
		if (!gatekeeper_disable) {
			if (h323_set_gk(gatekeeper_discover, gatekeeper, secret)) {
				ast_log(LOG_ERROR, "Gatekeeper registration failed.\n");
				gatekeeper_disable = 1;
				res = AST_MODULE_LOAD_SUCCESS;
			}
		}
		/* And start the monitor for the first time */
		restart_monitor();
	}
	return res;
}

static int unload_module(void)
{
	struct oh323_pvt *p, *pl;

	/* unregister commands */
	ast_cli_unregister_multiple(cli_h323, sizeof(cli_h323) / sizeof(struct ast_cli_entry));
	ast_cli_unregister(&cli_h323_reload);

	ast_channel_unregister(&oh323_tech);
	ast_rtp_proto_unregister(&oh323_rtp);

	if (!ast_mutex_lock(&iflock)) {
		/* hangup all interfaces if they have an owner */
		p = iflist;
		while(p) {
			if (p->owner) {
				ast_softhangup(p->owner, AST_SOFTHANGUP_APPUNLOAD);
			}
			p = p->next;
		}
		iflist = NULL;
		ast_mutex_unlock(&iflock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the interface list\n");
		return -1;
	}
	if (!ast_mutex_lock(&monlock)) {
		if ((monitor_thread != AST_PTHREADT_STOP) && (monitor_thread != AST_PTHREADT_NULL)) {
			if (monitor_thread != pthread_self()) {
				pthread_cancel(monitor_thread);
			}
			pthread_kill(monitor_thread, SIGURG);
			pthread_join(monitor_thread, NULL);
		}
		monitor_thread = AST_PTHREADT_STOP;
		ast_mutex_unlock(&monlock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}
	if (!ast_mutex_lock(&iflock)) {
		/* destroy all the interfaces and free their memory */
		p = iflist;
		while(p) {
			pl = p;
			p = p->next;
			/* free associated memory */
			ast_mutex_destroy(&pl->lock);
			free(pl);
		}
		iflist = NULL;
		ast_mutex_unlock(&iflock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the interface list\n");
		return -1;
	}
	if (!gatekeeper_disable)
		h323_gk_urq();
	h323_end_process();
	if (io)
		io_context_destroy(io);
	if (sched)
		sched_context_destroy(sched);

	ASTOBJ_CONTAINER_DESTROYALL(&userl, oh323_destroy_user);
	ASTOBJ_CONTAINER_DESTROY(&userl);
	ASTOBJ_CONTAINER_DESTROYALL(&peerl, oh323_destroy_peer);
	ASTOBJ_CONTAINER_DESTROY(&peerl);
	ASTOBJ_CONTAINER_DESTROYALL(&aliasl, oh323_destroy_alias);
	ASTOBJ_CONTAINER_DESTROY(&aliasl);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "The NuFone Network's OpenH323 Channel Driver",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
);
