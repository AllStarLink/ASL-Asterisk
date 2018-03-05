/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Copyright (C) 2009, Scott Lawson
 * Scott Lawson <ham44865@yahoo.com>
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
 * \author KI4LKF <ham44865@yahoo.com>
 *
 * \ingroup channel_drivers
 */

/*** MODULEINFO
 ***/

/* Version 0.6-NO-SEQUENCE-NUMBERS Sun Sep 28 12:03:31 EDT 2008.
   PUT BACK THE WAY IT WAS BEFORE, SEQUENCE NUMBERS NOT NEEDED.
   TEXT HANDLING IS OUT. IRLP DOES NOT PROCESS TEXT.
   THIS DRIVER USES UNCOMPRESSED HIGH_QUALITY AUDIO.

Channel connection for Asterisk to KI4LKF's rtpDir/rtpDir_tm bridge Echolink-IRLP-Asterisk
bridging program for Amateur Radio over VOIP.

Its invoked as rtpdir/HISIP:HISPORT[:MYPORT] 	 
	  	 
HISIP is the IP address (or FQDN) of the rtpDir/rtpDir_tm bridge.
HISPORT is the UDP socket of the RtpDir program 	 
MYPORT (optional) is the UDP socket that Asterisk listens on for this channel 	 

*/

#include "asterisk.h"

/*
 * Please change this revision number when you make a edit
 * use the simple format YYMMDD
*/

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 180112 $")
// ASTERISK_FILE_VERSION(__FILE__, "$"ASTERISK_VERSION" $")

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <search.h>
#include <sys/ioctl.h>
#include <ctype.h>

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

#define	MAX_RXKEY_TIME 4
#define	KEEPALIVE_TIME 50 * 7

#define LINEAR_FRAME_SIZE 160
#define	BLOCKING_FACTOR 4
#define QUEUE_OVERLOAD_THRESHOLD 25

static const char tdesc[] = "RTPDIR Ham Radio Bridging Driver";
static int prefformat = AST_FORMAT_SLINEAR;

static char context[AST_MAX_EXTENSION] = "default";
static char type[] = "rtpdir";
static char keepstr[] = 
	"KEEPALIVE I'm a packet that contains no useful data whatsoever.";

/* rtpdir creates private structures on demand */
struct rtpdir_rxq {
	struct rtpdir_rxq *qe_forw;
	struct rtpdir_rxq *qe_back;
	char buf[LINEAR_FRAME_SIZE * 2];
} ;

struct rtpdir_pvt {
 	int rtpdir;				/* open UDP socket */
	struct ast_channel *owner;		/* Channel we belong to, possibly NULL */
	char app[16];					/* Our app */
	char stream[80];				/* Our stream */
	struct sockaddr_in si_other;		/* for UDP sending */
	char txkey;
	int rxkey;
	int keepalive;
        char start_stop;
	struct ast_frame fr;			/* "null" frame */
	char txbuf[LINEAR_FRAME_SIZE * 2 * BLOCKING_FACTOR];
	int txindex;
	struct rtpdir_rxq rxq;
	struct ast_module_user *u;		/*! for holding a reference to this module */
};

static struct ast_channel *rtpdir_request(const char *type, int format, void *data, int *cause);
static int rtpdir_call(struct ast_channel *ast, char *dest, int timeout);
static int rtpdir_hangup(struct ast_channel *ast);
static struct ast_frame *rtpdir_xread(struct ast_channel *ast);
static int rtpdir_xwrite(struct ast_channel *ast, struct ast_frame *frame);
static int rtpdir_indicate(struct ast_channel *ast, int cond, const void *data, size_t datalen);
static int rtpdir_digit_begin(struct ast_channel *c, char digit);
static int rtpdir_digit_end(struct ast_channel *c, char digit, unsigned int duratiion);
static int rtpdir_text(struct ast_channel *c, const char *text);


static const struct ast_channel_tech rtpdir_tech = {
	.type = type,
	.description = tdesc,
	.capabilities = AST_FORMAT_SLINEAR,
	.requester = rtpdir_request,
	.call = rtpdir_call,
	.hangup = rtpdir_hangup,
	.read = rtpdir_xread,
	.write = rtpdir_xwrite,
	.indicate = rtpdir_indicate,
	.send_text = rtpdir_text,
	.send_digit_begin = rtpdir_digit_begin,
	.send_digit_end = rtpdir_digit_end,
};

static int rtpdir_call(struct ast_channel *ast, char *dest, int timeout)
{
	struct rtpdir_pvt *p;

	p = ast->tech_pvt;

	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "rtpdir_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}
	/* When we call, it just works, really, there's no destination...  Just
	   ring the phone and wait for someone to answer */
	if (option_debug)
		ast_log(LOG_DEBUG, "Calling %s on %s\n", dest, ast->name);

	ast_setstate(ast,AST_STATE_UP);
	return 0;
}

static void rtpdir_destroy(struct rtpdir_pvt *p)
{
	if (p->rtpdir)
		close(p->rtpdir);
	ast_module_user_remove(p->u);
	ast_free(p);
}

static struct rtpdir_pvt *rtpdir_alloc(void *data)
{
	struct rtpdir_pvt *p;
	int flags = 0;
	char stream[256];
	struct sockaddr_in si_me;
	struct hostent *host;
	struct ast_hostent ah;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(hisip);
		AST_APP_ARG(hisport);
		AST_APP_ARG(myport);
	);

	if (ast_strlen_zero(data)) return NULL;

	AST_NONSTANDARD_APP_ARGS(args,data,':');

	if ((!args.hisip) || (!args.hisip[0])) args.hisip = "127.0.0.1";
	if ((!args.hisport) || (!args.hisport[0])) args.hisport = "1234";
	if ((!args.myport) || (!args.myport[0]))  args.myport = args.hisport;

	p = ast_malloc(sizeof(struct rtpdir_pvt));
	if (p) {
		memset(p, 0, sizeof(struct rtpdir_pvt));

                p->start_stop = 'B';
		sprintf(stream,"%s:%d",args.hisip,atoi(args.hisport));
		strcpy(p->stream,stream);
		p->rxq.qe_forw = &p->rxq;
		p->rxq.qe_back = &p->rxq;

		memset(&ah,0,sizeof(ah));
		host = ast_gethostbyname(args.hisip,&ah);
		if (!host)
		{
			ast_log(LOG_WARNING, "Unable to find host %s\n", args.hisip);
			ast_free(p);
			return NULL;
		}
		memset((char *) &p->si_other, 0, sizeof(p->si_other));
		p->si_other.sin_addr = *(struct in_addr *)host->h_addr;
		p->si_other.sin_family = AF_INET;
		p->si_other.sin_port = htons(atoi(args.hisport));

		if ((p->rtpdir=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1)
		{
			ast_log(LOG_WARNING, "Unable to create new socket for rtpdir connection\n");
			ast_free(p);
			return(NULL);

		}
		
		memset((char *) &si_me, 0, sizeof(si_me));
		si_me.sin_family = AF_INET;
		si_me.sin_port = htons(atoi(args.myport));
		si_me.sin_addr.s_addr = htonl(INADDR_ANY);
		if (!strncmp(ast_inet_ntoa(p->si_other.sin_addr),"127.",4))
			si_me.sin_addr.s_addr = inet_addr("127.0.0.1");
		if (bind(p->rtpdir, &si_me, sizeof(si_me))==-1)
		{
			ast_log(LOG_WARNING, "Unable to bind port for rtpdir connection\n");
			ast_free(p);
			return(NULL);

		}
		if (!p->rtpdir) {
			ast_log(LOG_WARNING, 
                         "Unable to allocate new rtpdir stream '%s' with flags %d\n", stream, flags);
			ast_free(p);
			return NULL;
		}
	}
	return p;
}

static int rtpdir_hangup(struct ast_channel *ast)
{
	struct rtpdir_pvt *p;
	p = ast->tech_pvt;
	if (option_debug)
		ast_log(LOG_DEBUG, "rtpdir_hangup(%s)\n", ast->name);
	if (!ast->tech_pvt) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
	rtpdir_destroy(p);
	ast->tech_pvt = NULL;
	ast_setstate(ast, AST_STATE_DOWN);
	return 0;
}

static int rtpdir_indicate(struct ast_channel *ast, int cond, const void *data, size_t datalen)
{
	struct rtpdir_pvt *p = ast->tech_pvt;

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
			return 0;
	}

	return 0;
}

static int rtpdir_text(struct ast_channel *ast, const char *text)
{
	return 0;
}

static int rtpdir_digit_begin(struct ast_channel *ast, char digit)
{
	return 0;
}

static int rtpdir_digit_end(struct ast_channel *ast, char digit, unsigned int duration)
{

        unsigned char   buf[64];

        struct rtpdir_pvt *p = ast->tech_pvt;

        char digup = toupper(digit);

        memset(buf,0,sizeof(buf));
        sprintf((char *)buf,"DTMF %c",digup);

        /* re-enable this node */
        p->start_stop = 'B';

        ast_log(LOG_NOTICE,"sending %s to rtpDir/rtpDir_tm bridge\n", buf);
        if (sendto(p->rtpdir,buf,6,0,&p->si_other,sizeof(p->si_other)) == -1)
                return -1;
        return 0;
}

static struct ast_frame  *rtpdir_xread(struct ast_channel *ast)
{

	struct rtpdir_pvt *p = ast->tech_pvt;
	char buf[LINEAR_FRAME_SIZE * 2 * BLOCKING_FACTOR];
	struct sockaddr_in si_them;
	unsigned int themlen;
 	int n,i;
	struct ast_frame fr;
        struct rtpdir_rxq *qp;

	themlen = sizeof(struct sockaddr_in);
	if ((n = recvfrom(p->rtpdir,buf,sizeof(buf),0,&si_them,&themlen)) == -1)
	{
		ast_log(LOG_WARNING,"Cannot recvfrom()\n");
		return NULL;
	}
	if (n < LINEAR_FRAME_SIZE * 2 * BLOCKING_FACTOR)
	{
                if (n == 1)
                {
                   if (buf[0] == 'B')
                      p->start_stop = 'B';
                   else
                   if (buf[0] == 'E')
                      p->start_stop = 'E'; 
                  
                   ast_log(LOG_NOTICE, "received %c\n",buf[0]);
                }
		p->fr.frametype = 0;
		p->fr.subclass = 0;
		p->fr.datalen = 0;
		p->fr.samples = 0;
		p->fr.data =  NULL;
		p->fr.src = type;
		p->fr.offset = 0;
		p->fr.mallocd=0;
		p->fr.delivery.tv_sec = 0;
		p->fr.delivery.tv_usec = 0;
		return &p->fr;
	}

        if (p->start_stop == 'B')
        {	
           for (i = 0; i < BLOCKING_FACTOR; i++)
	   {
	      qp = ast_malloc(sizeof(struct rtpdir_rxq));
	      if (!qp)
	      {
	         ast_log(LOG_NOTICE,"Cannot malloc for qp\n");
	         break;
	      }
	      memcpy(qp->buf,buf + (LINEAR_FRAME_SIZE * 2 * i),LINEAR_FRAME_SIZE * 2);
	      insque((struct qelem *) qp,(struct qelem *) p->rxq.qe_back);
	   }
        }
	fr.datalen = 0;
	fr.samples = 0;
	fr.frametype = 0;
	fr.subclass = 0;
	fr.data =  0;
	fr.src = type;
	fr.offset = 0;
	fr.mallocd=0;
	fr.delivery.tv_sec = 0;
	fr.delivery.tv_usec = 0;

	return &p->fr;
}

static int rtpdir_xwrite(struct ast_channel *ast, struct ast_frame *frame)
{
	struct rtpdir_pvt *p = ast->tech_pvt;
	struct ast_frame fr;
	struct rtpdir_rxq *qp;
	int n;
	char buf[(LINEAR_FRAME_SIZE * 2) + AST_FRIENDLY_OFFSET];

	if (ast->_state != AST_STATE_UP) {
		/* Don't try tos end audio on-hook */
		return 0;
	}
	/* Write a frame of (presumably voice) data */
	if (frame->frametype != AST_FRAME_VOICE) return 0;

	/* if something in rx queue */
	if (p->rxq.qe_forw != &p->rxq)
	{
		for(n = 0,qp = p->rxq.qe_forw; qp != &p->rxq; qp = qp->qe_forw)
		{
			n++;
		}
		if (n > QUEUE_OVERLOAD_THRESHOLD)
		{
			while(p->rxq.qe_forw != &p->rxq)
			{
				qp = p->rxq.qe_forw;
				remque((struct qelem *)qp);
				free(qp);
			}
			if (p->rxkey) p->rxkey = 1;
		}			
		else
		{
			if (!p->rxkey)
			{
				fr.datalen = 0;
				fr.samples = 0;
				fr.frametype = AST_FRAME_CONTROL;
				fr.subclass = AST_CONTROL_RADIO_KEY;
				fr.data =  0;
				fr.src = type;
				fr.offset = 0;
				fr.mallocd=0;
				fr.delivery.tv_sec = 0;
				fr.delivery.tv_usec = 0;
				ast_queue_frame(ast,&fr);
			} 
			p->rxkey = MAX_RXKEY_TIME;
			qp = p->rxq.qe_forw;
			remque((struct qelem *) qp);
			memcpy(buf + AST_FRIENDLY_OFFSET,qp->buf,LINEAR_FRAME_SIZE * 2);
			ast_free(qp);

			fr.datalen = LINEAR_FRAME_SIZE * 2;
			fr.samples =  LINEAR_FRAME_SIZE;
			fr.frametype = AST_FRAME_VOICE;
			fr.subclass = AST_FORMAT_SLINEAR;
			fr.data =  buf + AST_FRIENDLY_OFFSET;
			fr.src = type;
			fr.offset = AST_FRIENDLY_OFFSET;
			fr.mallocd=0;
			fr.delivery.tv_sec = 0;
			fr.delivery.tv_usec = 0;
			ast_queue_frame(ast,&fr);
		}
	}
	if (p->rxkey == 1)
	{
		fr.datalen = 0;
		fr.samples = 0;
		fr.frametype = AST_FRAME_CONTROL;
		fr.subclass = AST_CONTROL_RADIO_UNKEY;
		fr.data =  0;
		fr.src = type;
		fr.offset = 0;
		fr.mallocd=0;
		fr.delivery.tv_sec = 0;
		fr.delivery.tv_usec = 0;
		ast_queue_frame(ast,&fr);
	} 
	if (p->rxkey) p->rxkey--;

	if (!(frame->subclass & (AST_FORMAT_SLINEAR))) {
		ast_log(LOG_WARNING, "Cannot handle frames in %d format\n", frame->subclass);
		return 0;
	}
	if (p->txkey || p->txindex) 
	{
		p->keepalive = KEEPALIVE_TIME;
		memcpy(&p->txbuf[LINEAR_FRAME_SIZE * 2 * p->txindex++],
			frame->data,LINEAR_FRAME_SIZE * 2);
	}	
	if (p->txindex >= BLOCKING_FACTOR)
	{
                if (p->start_stop == 'B')
                {
		   if (sendto(p->rtpdir,p->txbuf,LINEAR_FRAME_SIZE * 2 * BLOCKING_FACTOR,
			      0,&p->si_other,sizeof(p->si_other)) == -1)
		      return -1;
                }
		p->txindex = 0;
	}
	if (p->txkey) return 0;
	if (p->keepalive--) return 0;
	p->keepalive = KEEPALIVE_TIME;
        if (p->start_stop == 'B')
        {
	   if (sendto(p->rtpdir,keepstr,sizeof(keepstr),0,&p->si_other,sizeof(p->si_other)) == -1)
	      return -1;
        }
	return 0;
}

static struct ast_channel *rtpdir_new(struct rtpdir_pvt *i, int state)
{
	struct ast_channel *tmp;
	tmp = ast_channel_alloc(1, state, 0, 0, "", "s", context, 0, "rtpdir/%s", i->stream);
	if (tmp) {
		tmp->tech = &rtpdir_tech;
		tmp->fds[0] = i->rtpdir;
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


static struct ast_channel *rtpdir_request(const char *type, int format, void *data, int *cause)
{
	int oldformat;
	struct rtpdir_pvt *p;
	struct ast_channel *tmp = NULL;
	
	oldformat = format;
	format &= (AST_FORMAT_SLINEAR);
	if (!format) {
		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%d'\n", oldformat);
		return NULL;
	}
	p = rtpdir_alloc(data);
	if (p) {
		tmp = rtpdir_new(p, AST_STATE_DOWN);
		if (!tmp)
			rtpdir_destroy(p);
	}
	return tmp;
}

static int unload_module(void)
{
	/* First, take us out of the channel loop */
	ast_channel_unregister(&rtpdir_tech);
	return 0;
}

static int load_module(void)
{
	/* Make sure we can register our channel type */
	if (ast_channel_register(&rtpdir_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		return -1;
	}
	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "RTPDIR Ham Radio Bridging Thingy");
