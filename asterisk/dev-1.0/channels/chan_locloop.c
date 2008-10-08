/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Version 0.0.3 7/12/2006
 *
 * Copyright (C) 2008, Jim Dixon
 * Jim Dixon <jim@lambdatel.com>
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
 * \brief Ham Radio Bridging Channel support
 * 
 * \author Jim Dixon <jim@lambdatel.com>
 *
 * \ingroup channel_drivers
 */

/*** MODULEINFO
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 138259 $")

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <search.h>
#include <sys/ioctl.h>

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

#define	READ_BLOCK_MAX 4096

static const char tdesc[] = "locloop Ham Radio Bridging Driver";

int debug = 0;

/* Only linear is allowed */
static int prefformat = AST_FORMAT_SLINEAR;

static char context[AST_MAX_EXTENSION] = "default";
static char type[] = "locloop";

/* locloop creates private structures on demand */
   
struct locloop_binder;

struct locloop_pvt {
 	int locloop;				/* open UNIX socket */
	struct ast_channel *owner;		/* Channel we belong to, possibly NULL */
	struct locloop_bindq *mybindq	;	/* pointer to my binder queue location */
	char app[16];					/* Our app */
	char stream[80];
	struct sockaddr_in si_other;		/* for UDP sending */
	char txkey;
	int rxkey;
	int keepalive;
	struct ast_frame fr;			/* "null" frame */
	char rxbuf[READ_BLOCK_MAX + AST_FRIENDLY_OFFSET];
	int txindex;
	struct ast_module_user *u;		/*! for holding a reference to this module */
};

struct locloop_binder {
	char name[80];
 	int sv[2];
	struct locloop_pvt *p1;
	struct locloop_pvt *p2;
} ;

struct locloop_bindq {
	struct locloop_bindq *qe_forw;
	struct locloop_bindq *qe_back;
	struct locloop_binder b;
} ;

static struct ast_channel *locloop_request(const char *type, int format, void *data, int *cause);
static int locloop_call(struct ast_channel *ast, char *dest, int timeout);
static int locloop_hangup(struct ast_channel *ast);
static struct ast_frame *locloop_xread(struct ast_channel *ast);
static int locloop_xwrite(struct ast_channel *ast, struct ast_frame *frame);
static int locloop_indicate(struct ast_channel *ast, int cond, const void *data, size_t datalen);
static int locloop_answer(struct ast_channel *ast);

static struct locloop_bindq bindq;
AST_MUTEX_DEFINE_STATIC(bindq_lock);

static const struct ast_channel_tech locloop_tech = {
	.type = type,
	.description = tdesc,
	.capabilities = AST_FORMAT_SLINEAR,
	.requester = locloop_request,
	.call = locloop_call,
	.hangup = locloop_hangup,
	.read = locloop_xread,
	.write = locloop_xwrite,
	.indicate = locloop_indicate,
	.answer = locloop_answer,
};

static int locloop_call(struct ast_channel *ast, char *dest, int timeout)
{
	struct locloop_pvt *p;

	p = ast->tech_pvt;

	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "locloop_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}
	/* When we call, it just works, really, there's no destination...  Just
	   ring the phone and wait for someone to answer */
	if (option_debug)
		ast_log(LOG_DEBUG, "Calling %s on %s\n", dest, ast->name);

	ast_setstate(ast,AST_STATE_UP);
	return 0;
}

static void locloop_destroy(struct locloop_pvt *p)
{
struct locloop_bindq *bp;

	if (debug) printf("destroying %s\n",p->stream);

	ast_mutex_lock(&bindq_lock);
	for(bp = bindq.qe_forw; bindq.qe_forw != &bindq; bp = bp->qe_forw)
	{
		if (((bp->b.p1 == p) && (!bp->b.p2)) ||
		    ((bp->b.p2 == p) && (!bp->b.p1)))
		{
			if (debug) printf("Trashing slot %s\n",bp->b.name);
			remque(bp);
			close(bp->b.sv[0]);
			close(bp->b.sv[1]);
			free(bp);
			break;
		}
		if (bp->b.p1 == p) bp->b.p1 = NULL;
		else if (bp->b.p2 == p) bp->b.p2 = NULL;
		else continue;
		break;
	}	
	ast_mutex_unlock(&bindq_lock);
	ast_module_user_remove(p->u);
	free(p);
}

static struct locloop_pvt *locloop_alloc(void *data)
{
	struct locloop_pvt *p;
	struct locloop_bindq *bp;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(pipename);
	);

	if (ast_strlen_zero(data)) return NULL;

	AST_NONSTANDARD_APP_ARGS(args,data,':');

	if ((!args.pipename) || ast_strlen_zero(args.pipename)) return NULL;

	p = malloc(sizeof(struct locloop_pvt));
	if (p) {
		memset(p, 0, sizeof(struct locloop_pvt));
		ast_mutex_lock(&bindq_lock);
		if (bindq.qe_forw != &bindq) 
		    for(bp = bindq.qe_forw; bp != &bindq; bp = bp->qe_forw)
		{
			if (!strcmp(bp->b.name,args.pipename))
			{
				/* if full */
				if (bp->b.p1 && bp->b.p2)
				{
					ast_mutex_unlock(&bindq_lock);
					return NULL;
				}
				if (bp->b.p1) 
				{
					bp->b.p2 = p;
					p->locloop = bp->b.sv[1];
				}
				else 
				{
					bp->b.p1 = p;
					p->locloop = bp->b.sv[0];
				}
				p->mybindq = bp;
				break;
			}
		}
		if (!p->mybindq)
		{
			bp = malloc(sizeof(struct locloop_bindq));
			if (!bp)
			{
				ast_log(LOG_ERROR,"Cannot malloc\n");
				ast_mutex_unlock(&bindq_lock);
				return NULL;
			}
			strcpy(bp->b.name,args.pipename);
			bp->b.p1 = p;
			bp->b.p2 = NULL;
			p->mybindq = bp;
			if (socketpair(AF_UNIX,SOCK_DGRAM,0,bp->b.sv) == -1)
			{
				ast_mutex_unlock(&bindq_lock);
				ast_log(LOG_ERROR,"Cannot open unix native socket\n");
				return NULL;
			}
			p->locloop = bp->b.sv[0];
			insque((struct qelem *)bp,
				(struct qelem *)bindq.qe_back);
		}
		ast_mutex_unlock(&bindq_lock);
		if (p == bp->b.p1) sprintf(p->stream,"%s-1",args.pipename);
		else sprintf(p->stream,"%s-2",args.pipename);
	}
	if (debug) printf("made channel %s\n",p->stream);
	return p;
}

static int locloop_hangup(struct ast_channel *ast)
{
	struct locloop_pvt *p;
	p = ast->tech_pvt;
	if (option_debug)
		ast_log(LOG_DEBUG, "locloop_hangup(%s)\n", ast->name);
	if (!ast->tech_pvt) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
	locloop_destroy(p);
	ast->tech_pvt = NULL;
	ast_setstate(ast, AST_STATE_DOWN);
	return 0;
}

static int locloop_indicate(struct ast_channel *ast, int cond, const void *data, size_t datalen)
{
	struct locloop_pvt *p = ast->tech_pvt;

	switch (cond) {
		case AST_CONTROL_RADIO_KEY:
			p->txkey = 1;
			break;
		case AST_CONTROL_RADIO_UNKEY:
			p->txkey = 0;
			break;
		case AST_CONTROL_HANGUP:
			return -1;
		default:
			return -1;
	}

	return 0;
}

static int locloop_answer(struct ast_channel *ast)
{
	ast_setstate(ast,AST_STATE_UP);
	return 0;
}

static struct ast_frame  *locloop_xread(struct ast_channel *ast)
{

	struct locloop_pvt *p = ast->tech_pvt;
	int n;

	if ((n = recv(p->locloop,p->rxbuf + AST_FRIENDLY_OFFSET,READ_BLOCK_MAX,0)) == -1)
	{
		ast_log(LOG_ERROR,"Cannot recv()");
		return NULL;
	}

	p->fr.datalen = n;
	p->fr.samples = n / 2;
	p->fr.frametype = AST_FRAME_VOICE;
	p->fr.subclass = AST_FORMAT_SLINEAR;
	p->fr.data = p->rxbuf + AST_FRIENDLY_OFFSET;
	p->fr.src = type;
	p->fr.offset = AST_FRIENDLY_OFFSET;
	p->fr.mallocd=0;
	p->fr.delivery.tv_sec = 0;
	p->fr.delivery.tv_usec = 0;
	return &p->fr;
}

static int locloop_xwrite(struct ast_channel *ast, struct ast_frame *frame)
{
	struct locloop_pvt *p = ast->tech_pvt;

	if (ast->_state != AST_STATE_UP) {
		/* Don't try tos end audio on-hook */
		return 0;
	}
	/* Write a frame of (presumably voice) data */
	if (frame->frametype != AST_FRAME_VOICE) return 0;

	if (!(frame->subclass & (AST_FORMAT_SLINEAR))) {
		ast_log(LOG_WARNING, "Cannot handle frames in %d format\n", frame->subclass);
		return 0;
	}
	if (send(p->locloop,frame->data,frame->datalen,0) == -1)
	    return -1;
	return 0;
}

static struct ast_channel *locloop_new(struct locloop_pvt *i, int state)
{
	struct ast_channel *tmp;
	tmp = ast_channel_alloc(1, state, 0, 0, "", "s", context, 0, "locloop/%s", i->stream);
	if (tmp) {
		tmp->tech = &locloop_tech;
		tmp->fds[0] = i->locloop;
		tmp->nativeformats = prefformat;
		tmp->rawreadformat = prefformat;
		tmp->rawwriteformat = prefformat;
		tmp->writeformat = prefformat;
		tmp->readformat = prefformat;
		if (state == AST_STATE_RING)
			tmp->rings = 1;
		tmp->tech_pvt = i;
		ast_copy_string(tmp->context, context, sizeof(tmp->context));
		ast_copy_string(tmp->exten, "s",  sizeof(tmp->exten));
		ast_string_field_set(tmp, language, "");
		i->owner = tmp;
		i->u = ast_module_user_add(tmp);
		if (state != AST_STATE_DOWN) {
			if (ast_pbx_start(tmp)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
				ast_hangup(tmp);
			}
		}
	} else
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
	return tmp;
}


static struct ast_channel *locloop_request(const char *type, int format, void *data, int *cause)
{
	int oldformat;
	struct locloop_pvt *p;
	struct ast_channel *tmp = NULL;
	
	oldformat = format;
	format &= (AST_FORMAT_SLINEAR);
	if (!format) {
		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%d'\n", oldformat);
		return NULL;
	}
	p = locloop_alloc(data);
	if (p) {
		tmp = locloop_new(p, AST_STATE_DOWN);
		if (!tmp)
			locloop_destroy(p);
	}
	return tmp;
}

static int unload_module(void)
{
struct locloop_bindq *bp;

	ast_mutex_lock(&bindq_lock);
	while(bindq.qe_forw != &bindq)
	{
		bp = bindq.qe_forw;
		remque(bp);
		free(bp);
	}	
	ast_mutex_unlock(&bindq_lock);
	/* First, take us out of the channel loop */
	ast_channel_unregister(&locloop_tech);
	return 0;
}

static int load_module(void)
{
	bindq.qe_forw = &bindq;
	bindq.qe_back = &bindq;
	/* Make sure we can register our channel type */
	if (ast_channel_register(&locloop_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		return -1;
	}
	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "LocLoop pseudo-driver Thingy");
