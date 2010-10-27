/* #define	OLD_ASTERISK */
/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Copyright (C) 2008, Scott Lawson/KI4LKF
 * ScottLawson/KI4LKF <ham44865@yahoo.com>
 * Jim Dixon, WB6NIL <jim@lambdatel.com>
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
 * \brief TheLinkBox channel driver for Asterisk
 * 
 * \author Scott Lawson/KI4LKF <ham44865@yahoo.com>
 * \author Jim Dixon, WB6NIL <jim@lambdatel.com>
 *
 * \ingroup channel_drivers
 */

/*** MODULEINFO
 ***/

/*

TheLinkBox channel driver for Asterisk/app_rpt.

I wish to thank the following people for the immeasurable amount of
very high-quality assistance they have provided me, without which this
project would have been impossible:

Scott, KI4LKF
Skip, WB6YMH

It is invoked as tlb/identifier (like tlb0 for example)
Example: 
Under a node stanza in rpt.conf, 
rxchannel=tlb/tlb0

The tlb0 or whatever you put there must match the stanza in the
tlb.conf file.

*/

#include "asterisk.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <search.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <zlib.h>
#include <pthread.h>
#include <signal.h>
#include <fnmatch.h>

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/dsp.h"
#include "asterisk/translate.h"
#include "asterisk/astdb.h"
#include "asterisk/cli.h"

#ifdef	OLD_ASTERISK
#define	AST_MODULE_LOAD_DECLINE -1
#endif

#define	MAX_RXKEY_TIME 4

/* 50 * 10 * 20ms iax2 = 10,000ms = 10 seconds heartbeat */
#define	KEEPALIVE_TIME 50 * 10
#define	AUTH_RETRY_MS 5000
#define	AUTH_ABANDONED_MS 15000
#define	BLOCKING_FACTOR 4
#define	GSM_FRAME_SIZE 33
#define QUEUE_OVERLOAD_THRESHOLD_AST 25
#define QUEUE_OVERLOAD_THRESHOLD_EL 20
#define	MAXPENDING 20
#define DTMF_NPACKETS 5

#define TLB_IP_SIZE 16
#define TLB_CALL_SIZE 16
#define TLB_NAME_SIZE 32
#define TLB_PWD_SIZE 16
#define TLB_EMAIL_SIZE 32
#define TLB_QTH_SIZE 32
#define TLB_SERVERNAME_SIZE 63
#define	TLB_MAX_INSTANCES 100
#define	TLB_MAX_CALL_LIST 30

#define	DELIMCHR ','
#define	QUOTECHR 34

/* 
   If you want to compile/link this code
   on "BIG-ENDIAN" platforms, then
   use this: #define RTP_BIG_ENDIAN
   Have only tested this code on "little-endian"
   platforms running Linux.
*/

static const char tdesc[] = "TheLinkBox channel driver";
static int prefformat = AST_FORMAT_GSM;
static char type[] = "tlb";

int run_forever = 1;
static int killing = 0;
static int nullfd = -1;


/* TheLinkBox audio packet heafer */
struct rtpVoice_t {
#ifdef RTP_BIG_ENDIAN
  uint8_t version:2;
  uint8_t pad:1;
  uint8_t ext:1;
  uint8_t csrc:4;
  uint8_t marker:1;
  uint8_t payt:7;
#else
  uint8_t csrc:4;
  uint8_t ext:1;
  uint8_t pad:1;
  uint8_t version:2;
  uint8_t payt:7;
  uint8_t marker:1;
#endif
  uint16_t seqnum;
  uint32_t time;
  uint32_t ssrc;
  unsigned char data[BLOCKING_FACTOR * GSM_FRAME_SIZE];
};

struct TLB_instance;
struct TLB_pvt;

/* TheLinkBox node details */
/* Also each node in binary tree in memory */
struct TLB_node {
   char ip[TLB_IP_SIZE + 1]; 
   uint16_t port;
   char call[TLB_CALL_SIZE + 1];
   char name[TLB_NAME_SIZE + 1];
   unsigned int nodenum; /* not used yet */
   short countdown;
   uint16_t seqnum;
   struct TLB_instance *instp;
   struct TLB_pvt *p;
   struct ast_channel *chan;
};

struct TLB_pending {
	char fromip[TLB_IP_SIZE + 1];
	struct timeval reqtime;
} ;		

struct TLB_instance
{
	ast_mutex_t lock;
	char name[TLB_NAME_SIZE + 1];
	char mycall[TLB_CALL_SIZE + 1];
	char ipaddr[TLB_IP_SIZE + 1];
	char port[TLB_IP_SIZE + 1];
	char astnode[TLB_NAME_SIZE + 1];
	char context[TLB_NAME_SIZE + 1];
	char *denylist[TLB_MAX_CALL_LIST];
	int ndenylist;
	char *permitlist[TLB_MAX_CALL_LIST];
	int npermitlist;
	/* missed 10 heartbeats, you're out */
	short rtcptimeout;
	char fdr_file[FILENAME_MAX];
	int audio_sock;
	int ctrl_sock;
	uint16_t audio_port;
	uint16_t ctrl_port;
	int fdr;
	unsigned long seqno;
	int confmode;
	struct TLB_pvt *confp;
	struct rtpVoice_t audio_all_but_one;
	struct rtpVoice_t audio_all;
	struct TLB_node TLB_node_test;
	struct TLB_pending pending[MAXPENDING];
	pthread_t TLB_reader_thread;
} ;

struct TLB_rxqast {
	struct TLB_rxqast *qe_forw;
	struct TLB_rxqast *qe_back;
	char buf[GSM_FRAME_SIZE];
};

struct TLB_rxqel {
        struct TLB_rxqel *qe_forw;
        struct TLB_rxqel *qe_back;
        char buf[BLOCKING_FACTOR * GSM_FRAME_SIZE];
        char fromip[TLB_IP_SIZE + 1];
	uint16_t fromport;
};

struct TLB_pvt {
	ast_mutex_t lock;
	struct ast_channel *owner;
	struct TLB_instance *instp;
	char app[16];		
	char stream[80];
	char ip[TLB_IP_SIZE + 1]; 
	uint16_t port;
	char txkey;
	int rxkey;
	int keepalive;
	struct ast_frame fr;	
	int txindex;
	struct TLB_rxqast rxqast;
        struct TLB_rxqel rxqel;
	char firstsent;
	char firstheard;
	struct ast_dsp *dsp;
	struct ast_module_user *u;
	struct ast_trans_pvt *xpath;
	unsigned int nodenum;
	char *linkstr;
	uint32_t dtmflastseq;
	uint32_t dtmflasttime;
	uint32_t dtmfseq;
	uint32_t dtmfidx;
};

struct rtcp_sdes_request_item {
    unsigned char r_item;
    char *r_text;
};

struct rtcp_sdes_request {
   int nitems;
   unsigned char ssrc[4];
   struct rtcp_sdes_request_item item[10];
};

struct rtcp_common_t {
#ifdef RTP_BIG_ENDIAN
  uint8_t version:2;
  uint8_t p:1;
  uint8_t count:5;
#else
  uint8_t count:5;
  uint8_t p:1;
  uint8_t version:2;
#endif
  uint8_t pt:8;
  uint16_t length;
};

struct rtcp_sdes_item_t {
  uint8_t type;
  uint8_t length;
  char data[1];
};

struct rtcp_t {
  struct rtcp_common_t common;
  union {
    struct {
      uint32_t src[1];
    } bye;

    struct rtcp_sdes_t {
      uint32_t src;
      struct rtcp_sdes_item_t item[1];
    } sdes;
  } r;
};

#ifdef	OLD_ASTERISK
static int usecnt;
AST_MUTEX_DEFINE_STATIC(usecnt_lock);
#endif

int debug = 0;
struct TLB_instance *instances[TLB_MAX_INSTANCES];
int ninstances = 0;

/* binary search tree in memory, root node */
static void *TLB_node_list = NULL;

static char *config = "tlb.conf";

#ifdef OLD_ASTERISK
#define ast_free free
#define ast_malloc malloc
#endif

static struct ast_channel *TLB_request(const char *type, int format, void *data, int *cause);
static int TLB_call(struct ast_channel *ast, char *dest, int timeout);
static int TLB_hangup(struct ast_channel *ast);
static struct ast_frame *TLB_xread(struct ast_channel *ast);
static int TLB_xwrite(struct ast_channel *ast, struct ast_frame *frame);
#ifdef	OLD_ASTERISK
static int TLB_indicate(struct ast_channel *ast, int cond);
static int TLB_digit_end(struct ast_channel *c, char digit);
#else
static int TLB_indicate(struct ast_channel *ast, int cond, const void *data, size_t datalen);
static int TLB_digit_begin(struct ast_channel *c, char digit);
static int TLB_digit_end(struct ast_channel *c, char digit, unsigned int duratiion);
#endif
static int TLB_text(struct ast_channel *c, const char *text);

static int rtcp_make_sdes(unsigned char *pkt, int pktLen, char *call);
static int rtcp_make_bye(unsigned char *p, char *reason);
static void parse_sdes(unsigned char *packet, struct rtcp_sdes_request *r);
static void copy_sdes_item(char *source, char *dest, int destlen);
static int is_rtcp_bye(unsigned char *p, int len);
static int is_rtcp_sdes(unsigned char *p, int len);
 /* remove binary tree functions if Asterisk has similar functionality */
static int compare_ip(const void *pa, const void *pb);
static void send_audio_all_but_one(const void *nodep, const VISIT which, const int depth);
static void send_audio_all(const void *nodep, const VISIT which, const int depth);
static void send_heartbeat(const void *nodep, const VISIT which, const int depth);
static void free_node(void *nodep);
static int find_delete(struct TLB_node *key);
static int do_new_call(struct TLB_instance *instp, struct TLB_pvt *p, char *call, char *name);

static const struct ast_channel_tech TLB_tech = {
	.type = type,
	.description = tdesc,
	.capabilities = AST_FORMAT_GSM,
	.requester = TLB_request,
	.call = TLB_call,
	.hangup = TLB_hangup,
	.read = TLB_xread,
	.write = TLB_xwrite,
	.indicate = TLB_indicate,
	.send_text = TLB_text,
#ifdef	OLD_ASTERISK
	.send_digit = TLB_digit_end,
#else
	.send_digit_begin = TLB_digit_begin,
	.send_digit_end = TLB_digit_end,
#endif
};

/*
* CLI extensions
*/

/* Debug mode */
static int TLB_do_debug(int fd, int argc, char *argv[]);
static int TLB_do_nodedump(int fd, int argc, char *argv[]);
static int TLB_do_nodeget(int fd, int argc, char *argv[]);

static char debug_usage[] =
"Usage: tlbx debug level {0-7}\n"
"       Enables debug messages in chan_tlb\n";

static char nodedump_usage[] =
"Usage: tlb nodedump\n"
"       Dumps entire tlb node list\n";

static char nodeget_usage[] =
"Usage: tlb nodeget <nodename|callsign|ipaddr> <lookup-data>\n"
"       Looks up tlb node entry\n";

#ifndef	NEW_ASTERISK

static struct ast_cli_entry  cli_debug =
        { { "tlb", "debug", "level" }, TLB_do_debug, 
		"Enable TheLinkBox debugging", debug_usage };

static struct ast_cli_entry  cli_nodedump =
        { { "tlb", "nodedump" }, TLB_do_nodedump,
		"Dump entire tlb node list", nodedump_usage };

static struct ast_cli_entry  cli_nodeget =
        { { "tlb", "nodeget" }, TLB_do_nodeget,
		"Look up tlb node entry", nodeget_usage };

#endif

static void mythread_exit(void *nothing)
{
int	i;

	if (killing) pthread_exit(NULL);
	killing = 1;
	run_forever = 0;
	for(i = 0; i < ninstances; i++)
	{
		if (instances[i]->TLB_reader_thread) 
			pthread_kill(instances[i]->TLB_reader_thread,SIGTERM);
	}
	ast_log(LOG_ERROR,"Exiting chan_tlb, FATAL ERROR!!\n");
	ast_cli_command(nullfd,"rpt restart");
	pthread_exit(NULL);
}

static void strupr(char *str)
{
        while (*str)
           {
                *str = toupper(*str);
                str++;
           }
        return;
}

/*
* Break up a delimited string into a table of substrings
*
* str - delimited string ( will be modified )
* strp- list of pointers to substrings (this is built by this function), NULL will be placed at end of list
* limit- maximum number of substrings to process
*/
	


static int finddelim(char *str, char *strp[], int limit)
{
int     i,l,inquo;

        inquo = 0;
        i = 0;
        strp[i++] = str;
        if (!*str)
           {
                strp[0] = 0;
                return(0);
           }
        for(l = 0; *str && (l < limit) ; str++)
           {
                if (*str == QUOTECHR)
                   {
                        if (inquo)
                           {
                                *str = 0;
                                inquo = 0;
                           }
                        else
                           {
                                strp[i - 1] = str + 1;
                                inquo = 1;
                           }
		}
                if ((*str == DELIMCHR) && (!inquo))
                {
                        *str = 0;
			l++;
                        strp[i++] = str + 1;
                }
           }
        strp[i] = 0;
        return(i);

}

static int rtcp_make_sdes(unsigned char *pkt, int pktLen, char *call)
{
    unsigned char zp[1500];
    unsigned char *p = zp;
    struct rtcp_t *rp;
    unsigned char *ap;
    char line[100];
    int l, hl, pl;

    hl = 0;
    *p++ = 2 << 6;
    *p++ = 201;
    *p++ = 0;
    *p++ = 1;
    *((long *) p) = htonl(0);
    p += 4;
    hl = 8;

    rp = (struct rtcp_t *) p;
    *((short *) p) = htons((2 << 14) | 202 | (1 << 8));
    rp->r.sdes.src = htonl(0);
    ap = (unsigned char *) rp->r.sdes.item;

    strcpy(line,"CALLSIGN");
    *ap++ = 1;
    *ap++ = l = strlen(line);
    memcpy(ap,line,l);
    ap += l;

    snprintf(line,TLB_CALL_SIZE,"%s",call);
    *ap++ = 2;
    *ap++ = l = strlen(line);
    memcpy(ap,line,l); 
    ap += l;

    strcpy(line,"Asterisk/app_rpt/TheLinkBox");
    *ap++ = 6;
    *ap++ = l = strlen(line);
    memcpy(ap,line,l); 
    ap += l;

    *ap++ = 0;
    *ap++ = 0;
    l = ap - p;

    rp->common.length = htons(((l + 3) / 4) - 1);
    l = hl + ((ntohs(rp->common.length) + 1) * 4);

    pl = (l & 4) ? l : l + 4;

    if (pl > l) {
       int pad = pl - l;
       memset(zp + l, '\0', pad);  
       zp[pl - 1] = pad;
       p[0] |= 0x20;
       rp->common.length = htons(ntohs(rp->common.length) + ((pad) / 4));
       l = pl;
    }

    if (l > pktLen)
       return 0;
    memcpy(pkt,zp,l);
    return l;
}

static int rtcp_make_bye(unsigned char *p, char *reason)
{
    struct rtcp_t *rp;
    unsigned char *ap, *zp;
    int l, hl, pl;

    zp = p;
    hl = 0;

    *p++ = 2 << 6;
    *p++ = 201;
    *p++ = 0;
    *p++ = 1;
    *((long *) p) = htonl(0);
    p += 4;
    hl = 8;

    rp = (struct rtcp_t *)p;
    *((short *) p) = htons((2 << 14) | 203 | (1 << 8));
    rp->r.bye.src[0] = htonl(0);
    ap = (unsigned char *) rp->r.sdes.item;
    l = 0; 
    if (reason != NULL) {
        l = strlen(reason);
        if (l > 0) {
            *ap++ = l;
             memcpy(ap,reason,l);
            ap += l;
        }
    }
    while ((ap - p) & 3)
        *ap++ = 0;
    l = ap - p;
    rp->common.length = htons((l / 4) - 1);
    l = hl + ((ntohs(rp->common.length) + 1) * 4);

    pl = (l & 4) ? l : l + 4;
    if (pl > l) {
       int pad = pl - l;
       memset(zp + l, '\0', pad);
       zp[pl - 1] = pad;
       p[0] |= 0x20;
       rp->common.length = htons(ntohs(rp->common.length) + ((pad) / 4));
       l = pl;    
    }
    return l;
}

static void parse_sdes(unsigned char *packet, struct rtcp_sdes_request *r)
{
    int i;
    unsigned char *p = packet;

    for (i = 0; i < r->nitems; i++)
        r->item[i].r_text = NULL;

    while ((p[0] >> 6 & 3) == 2 || (p[0] >> 6 & 3) == 1) {
        if ((p[1] == 202) && ((p[0] & 0x1F) > 0)) {
            unsigned char *cp = p + 8,
                          *lp = cp + (ntohs(*((short *) (p + 2))) + 1) * 4;
            memcpy(r->ssrc, p + 4, 4); 
            while (cp < lp) {
                unsigned char itype = *cp;
                if (itype == 0)
                    break;

                for (i = 0; i < r->nitems; i++) {
                    if (r->item[i].r_item == itype &&
                        r->item[i].r_text == NULL) {
                        r->item[i].r_text = (char *) cp;
                        break;
                    }
                }
                cp += cp[1] + 2;
            }
            break;
        }
        p += (ntohs(*((short *) (p + 2))) + 1) * 4;
    }
    return;
}

static void copy_sdes_item(char *source, char *dest, int destlen)
{
    int len = source[1] & 0xFF;
    if (len > destlen)
       len = destlen;
    memcpy(dest,source + 2, len);
    dest[len] = 0;
    return;
}

static int is_rtcp_bye(unsigned char *p, int len)
{
    unsigned char *end;
    int sawbye = 0;


    if ((((p[0] >> 6) & 3) != 2 && ((p[0] >> 6) & 3) != 1) ||
        ((p[0] & 0x20) != 0) ||
        ((p[1] != 200) && (p[1] != 201)))
      return 0;

    end = p + len;

    do {
        if (p[1] == 203)
            sawbye = 1;

        p += (ntohs(*((short *) (p + 2))) + 1) * 4;
    } while (p < end && (((p[0] >> 6) & 3) == 2));

    return sawbye;
}

static int is_rtcp_sdes(unsigned char *p, int len)
{
    unsigned char *end;
    int sawsdes = 0;

    if ((((p[0] >> 6) & 3) != 2 && ((p[0] >> 6) & 3) != 1) ||
        ((p[0] & 0x20) != 0) ||
        ((p[1] != 200) && (p[1] != 201)))
      return 0;

    end = p + len;
    do {
       if (p[1] == 202)
          sawsdes = 1;

        p += (ntohs(*((short *) (p + 2))) + 1) * 4;
    } while (p < end && (((p[0] >> 6) & 3) == 2));

    return sawsdes;
}

static int TLB_call(struct ast_channel *ast, char *dest, int timeout)
{
	struct TLB_pvt *p = ast->tech_pvt;
	struct TLB_instance *instp = p->instp;
	unsigned char  pack[256];
	int pack_length;
	struct sockaddr_in sin;
	unsigned short n;


	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "TLB_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}
	/* When we call, it just works, really, there's no destination...  Just
	   ring the phone and wait for someone to answer */
	if (option_debug)
		ast_log(LOG_DEBUG, "Calling %s on %s\n", dest, ast->name);
	if (*dest)  /* if number specified */
	{
		char *str,*cp,*val,*sval,*strs[10];
		struct ast_config *cfg = NULL;

		str = ast_strdup(dest);
		cp = strchr(str,'/');
		if (cp) *cp++ = 0; else cp = str;
#ifdef  NEW_ASTERISK
	        if (!(cfg = ast_config_load(config,zeroflag))) {
#else
	        if (!(cfg = ast_config_load(config))) {
#endif
	                ast_log(LOG_ERROR, "Unable to load config %s\n", config);
			return -1;
		}
		val = (char *)ast_variable_retrieve(cfg,"nodes",str);
		if (!val)
		{
			ast_log(LOG_ERROR,"Node %s not found!\n",str);
			ast_config_destroy(cfg);
			return -1;
		}
		sval = ast_strdupa(val);
		ast_config_destroy(cfg);
		strupr(sval);
		n = finddelim(sval,strs,10);
		if (n < 3) 
		{
			ast_log(LOG_ERROR,"Node %s not found!\n",str);
			return -1;
		}
		ast_mutex_lock(&instp->lock);
		strcpy(instp->TLB_node_test.ip,strs[1]);
		instp->TLB_node_test.port = strtoul(strs[2],NULL,0);
		do_new_call(instp,p,"OUTBOUND","OUTBOUND");
		pack_length = rtcp_make_sdes(pack,sizeof(pack),instp->mycall);
		sin.sin_family = AF_INET;
		sin.sin_port = htons(atoi(strs[2]) + 1);
		sin.sin_addr.s_addr = inet_addr(strs[1]);
	        sendto(instp->ctrl_sock, pack, pack_length,
			0,(struct sockaddr *)&sin,sizeof(sin));
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "tlb: Connect request sent to %s (%s:%s)\n", str,strs[1],strs[2]);
		ast_mutex_unlock(&instp->lock);
		ast_free(str);
	}
	ast_setstate(ast,AST_STATE_RINGING);
	return 0;
}

static void TLB_destroy(struct TLB_pvt *p)
{
	if (p->dsp) ast_dsp_free(p->dsp);
	if (p->xpath) ast_translator_free_path(p->xpath);
	if (p->linkstr) ast_free(p->linkstr);
	p->linkstr = NULL;
#ifdef	OLD_ASTERISK
	ast_mutex_lock(&usecnt_lock);
	usecnt--;
	ast_mutex_unlock(&usecnt_lock);
#else
	ast_module_user_remove(p->u);
#endif
	ast_free(p);
}

static struct TLB_pvt *TLB_alloc(void *data)
{
	struct TLB_pvt *p;
	int n;
	/* int flags = 0; */
	char stream[256];
        
	if (ast_strlen_zero(data)) return NULL;

	for(n = 0; n < ninstances; n++)
	{
		if (!strcmp(instances[n]->name,(char *)data)) break;
	}
	if (n >= ninstances)
	{
		ast_log(LOG_ERROR,"Cannot find TheLinkBox channel %s\n",(char *)data);
		return NULL;
	}

	p = ast_malloc(sizeof(struct TLB_pvt));
	if (p) {
		memset(p, 0, sizeof(struct TLB_pvt));
		
		ast_mutex_init(&p->lock);
		sprintf(stream,"%s-%lu",(char *)data,instances[n]->seqno++);
		strcpy(p->stream,stream);
		p->rxqast.qe_forw = &p->rxqast;
		p->rxqast.qe_back = &p->rxqast;

                p->rxqel.qe_forw = &p->rxqel;
                p->rxqel.qe_back = &p->rxqel;
                
		p->keepalive = KEEPALIVE_TIME;
		p->instp = instances[n];
		p->instp->confp = p;  /* save for conference mode */
		if (!p->instp->confmode)
		{
			p->dsp = ast_dsp_new();
			if (!p->dsp)
			{
				ast_log(LOG_ERROR,"Cannot get DSP!!\n");
				return NULL;
			}
#ifdef  NEW_ASTERISK
	                ast_dsp_set_features(p->dsp,DSP_FEATURE_DIGIT_DETECT);
	                ast_dsp_set_digitmode(p->dsp,DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_RELAXDTMF);
#else
	                ast_dsp_set_features(p->dsp,DSP_FEATURE_DTMF_DETECT);
	                ast_dsp_digitmode(p->dsp,DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_RELAXDTMF);
#endif
			p->xpath = ast_translator_build_path(AST_FORMAT_SLINEAR,AST_FORMAT_GSM);
			if (!p->xpath)
			{
				ast_log(LOG_ERROR,"Cannot get translator!!\n");
				return NULL;
			}
		}
	}
	return p;
}

static int TLB_hangup(struct ast_channel *ast)
{
	struct TLB_pvt *p = ast->tech_pvt;
	struct TLB_instance *instp = p->instp;
	int i,n;
        unsigned char bye[50];
	struct sockaddr_in sin;

	if (!instp->confmode)
	{
		if (debug) ast_log(LOG_DEBUG,"Sent bye to IP address %s\n",p->ip);
		ast_mutex_lock(&instp->lock);
		strcpy(instp->TLB_node_test.ip,p->ip);
		instp->TLB_node_test.port = p->port;
		find_delete(&instp->TLB_node_test);
		ast_mutex_unlock(&instp->lock);
		n = rtcp_make_bye(bye,"disconnected");
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = inet_addr(p->ip);
		sin.sin_port = htons(p->port + 1);
		for (i = 0; i < 20; i++)
		{
			sendto(instp->ctrl_sock, bye, n,
				0,(struct sockaddr *)&sin,sizeof(sin));
		}
	}		
	if (option_debug)
		ast_log(LOG_DEBUG, "TLB_hangup(%s)\n", ast->name);
	if (!ast->tech_pvt) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
	TLB_destroy(p);
	ast->tech_pvt = NULL;
	ast_setstate(ast, AST_STATE_DOWN);
	return 0;
}

#ifdef	OLD_ASTERISK
static int TLB_indicate(struct ast_channel *ast, int cond)
#else
static int TLB_indicate(struct ast_channel *ast, int cond, const void *data, size_t datalen)
#endif
{
	struct TLB_pvt *p = ast->tech_pvt;

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

static int tlb_send_dtmf(struct ast_channel *ast,char digit)
{

	time_t	now;
	struct rtpVoice_t pkt;
	struct TLB_pvt *p = ast->tech_pvt;
	struct sockaddr_in sin;
        struct TLB_node **found_key = NULL;
	int i;

	/* set all packet contents to zero */
	memset(&pkt,0,sizeof(pkt));

	/* get a pointer to the TLB_Node entry and get and
		increment the seqno for the RTP packet */
	ast_mutex_lock(&p->instp->lock);
	strcpy(p->instp->TLB_node_test.ip,p->ip);
	p->instp->TLB_node_test.port = p->port;
	found_key = (struct TLB_node **)tfind(&p->instp->TLB_node_test,
			&TLB_node_list, compare_ip); 
	if (found_key)
	{
	        pkt.seqnum = htons((*(struct TLB_node **)found_key)->seqnum++); 
	} 
	ast_mutex_unlock(&p->instp->lock);
	if (!found_key)
	{
		ast_log(LOG_ERROR,"Unable to find node refernce for IP addr %s, port %u\n",
			p->ip,p->port & 0xffff);
		return -1;
	}

	time(&now);
        sin.sin_family = AF_INET;
        sin.sin_port = htons(p->port);
        sin.sin_addr.s_addr = inet_addr(p->ip);

	/* build the rest of the RTP packet */
        pkt.version = 2;
        pkt.pad = 0;
        pkt.ext = 0;
        pkt.csrc = 0;
        pkt.marker = 0;
        pkt.payt = 69;
        pkt.time = htonl(0);
        pkt.ssrc = htonl(6969);
	ast_mutex_lock(&p->lock); /* needs to be locked, since we are incrementing dtmfseq */
	sprintf((char *)pkt.data,"DTMF%c %u %u",digit,++p->dtmfseq,(uint32_t)now);
	ast_mutex_unlock(&p->lock);
	for(i = 0; i < DTMF_NPACKETS; i++)
	{
	        sendto(p->instp->audio_sock, (char *)&pkt, strlen((char *)pkt.data) + 12,
	                0,(struct sockaddr *)&sin,sizeof(sin));
	}
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "tlb: Sent DTMF digit %c to IP %s, port %u\n",
			digit,p->ip,p->port & 0xffff);
	return(0);
}


#ifndef	OLD_ASTERISK

static int TLB_digit_begin(struct ast_channel *ast, char digit)
{
	return -1;
}

#endif

#ifdef	OLD_ASTERISK
static int TLB_digit_end(struct ast_channel *ast, char digit)
#else
static int TLB_digit_end(struct ast_channel *ast, char digit, unsigned int duration)
#endif
{
	return(tlb_send_dtmf(ast,digit));
}

static int TLB_text(struct ast_channel *ast, const char *text)
{
	char buf[200],*arg1 = NULL,*arg2 = NULL;
	char *arg3 = NULL, *arg4 = NULL,*ptr,*saveptr;
	char delim = ' ',*cmd;

	strncpy(buf,text,sizeof(buf) - 1);
	ptr = strchr(buf, (int)'\r'); 
	if (ptr) *ptr = '\0';
	ptr = strchr(buf, (int)'\n');    
	if (ptr) *ptr = '\0';

	cmd = strtok_r(buf, &delim, &saveptr);
	if (!cmd)
	{
		return 0;
	}

	arg1 = strtok_r(NULL, &delim, &saveptr);
	arg2 = strtok_r(NULL, &delim, &saveptr);
	arg3 = strtok_r(NULL, &delim, &saveptr);
	arg4 = strtok_r(NULL, &delim, &saveptr);

	if (!strcasecmp(cmd,"D"))
	{
		tlb_send_dtmf(ast,*arg4);
	}
	return 0;
}

static int compare_ip(const void *pa, const void *pb)
{
   return strncmp(((struct TLB_node *)pa)->ip,((struct TLB_node *)pb)->ip,TLB_IP_SIZE); 
}

/* TheLinkBox ---> TheLinkBox */
void send_audio_all_but_one(const void *nodep, const VISIT which, const int depth) 
{
   struct sockaddr_in sin;
   struct TLB_instance *instp = (*(struct TLB_node **)nodep)->instp;

   if ((which == leaf) || (which == postorder)) {
      if ((strncmp((*(struct TLB_node **)nodep)->ip, instp->TLB_node_test.ip,TLB_IP_SIZE) != 0) &&
		((*(struct TLB_node **)nodep)->port == instp->TLB_node_test.port)) {
         sin.sin_family = AF_INET;
         sin.sin_port = htons((*(struct TLB_node **)nodep)->port);
         sin.sin_addr.s_addr = inet_addr((*(struct TLB_node **)nodep)->ip);

         instp->audio_all_but_one.version = 2;
         instp->audio_all_but_one.pad = 0;
         instp->audio_all_but_one.ext = 0;
         instp->audio_all_but_one.csrc = 0;
         instp->audio_all_but_one.marker = 0;
         instp->audio_all_but_one.payt = 3;
         instp->audio_all_but_one.seqnum = htons((*(struct TLB_node **)nodep)->seqnum++); 
         instp->audio_all_but_one.time = htonl(0);
         instp->audio_all_but_one.ssrc = htonl(6969);

         sendto(instp->audio_sock, (char *)&instp->audio_all_but_one, sizeof(instp->audio_all_but_one),
                0,(struct sockaddr *)&sin,sizeof(sin));
      }
   }
}

static void send_audio_only_one(const void *nodep, const VISIT which, const int depth) 
{
   struct sockaddr_in sin;
   struct TLB_instance *instp = (*(struct TLB_node **)nodep)->instp;

   if ((which == leaf) || (which == postorder)) {
      if ((strncmp((*(struct TLB_node **)nodep)->ip, instp->TLB_node_test.ip,TLB_IP_SIZE) == 0) &&
		((*(struct TLB_node **)nodep)->port == instp->TLB_node_test.port)) {
         sin.sin_family = AF_INET;
         sin.sin_port = htons((*(struct TLB_node **)nodep)->port);
         sin.sin_addr.s_addr = inet_addr((*(struct TLB_node **)nodep)->ip);

      instp->audio_all.version = 2;
      instp->audio_all.pad = 0;
      instp->audio_all.ext = 0;
      instp->audio_all.csrc = 0;
      instp->audio_all.marker = 0;
      instp->audio_all.payt = 3;
      instp->audio_all.seqnum = htons((*(struct TLB_node **)nodep)->seqnum++);
      instp->audio_all.time = htonl(0);
      instp->audio_all.ssrc = htonl(6969);

      sendto(instp->audio_sock, (char *)&instp->audio_all, sizeof(instp->audio_all), 
             0,(struct sockaddr *)&sin,sizeof(sin));
      }
   }
}

/* Asterisk ---> TheLinkBox */
void send_audio_all(const void *nodep, const VISIT which, const int depth)
{
   struct sockaddr_in sin;
   struct TLB_instance *instp = (*(struct TLB_node **)nodep)->instp;

   if ((which == leaf) || (which == postorder)) {
      sin.sin_family = AF_INET;
      sin.sin_port = htons(instp->audio_port);
      sin.sin_addr.s_addr = inet_addr((*(struct TLB_node **)nodep)->ip);

      instp->audio_all.version = 2;
      instp->audio_all.pad = 0;
      instp->audio_all.ext = 0;
      instp->audio_all.csrc = 0;
      instp->audio_all.marker = 0;
      instp->audio_all.payt = 3;
      instp->audio_all.seqnum = htons((*(struct TLB_node **)nodep)->seqnum++);
      instp->audio_all.time = htonl(0);
      instp->audio_all.ssrc = htonl(6969);

      sendto(instp->audio_sock, (char *)&instp->audio_all, sizeof(instp->audio_all), 
             0,(struct sockaddr *)&sin,sizeof(sin));
   }
}

static void send_heartbeat(const void *nodep, const VISIT which, const int depth)
{
   struct sockaddr_in sin;
   unsigned char  sdes_packet[256];
   int sdes_length;
   struct TLB_instance *instp = (*(struct TLB_node **)nodep)->instp;

   if ((which == leaf) || (which == postorder)) {

      if ((*(struct TLB_node **)nodep)->countdown >= 0)
         (*(struct TLB_node **)nodep)->countdown --;
  
      if ((*(struct TLB_node **)nodep)->countdown < 0) {
         ast_copy_string(instp->TLB_node_test.ip,(*(struct TLB_node **)nodep)->ip,TLB_IP_SIZE);
	 instp->TLB_node_test.port = (*(struct TLB_node **)nodep)->port;
         ast_copy_string(instp->TLB_node_test.call,(*(struct TLB_node **)nodep)->call,TLB_CALL_SIZE);
         ast_log(LOG_WARNING,"countdown for %s(%s) negative\n",instp->TLB_node_test.call,instp->TLB_node_test.ip);
      }
      memset(sdes_packet,0,sizeof(sdes_packet));
      sdes_length = rtcp_make_sdes(sdes_packet,sizeof(sdes_packet),
	instp->mycall);

      sin.sin_family = AF_INET;
      sin.sin_port = htons((*(struct TLB_node **)nodep)->port + 1);
      sin.sin_addr.s_addr = inet_addr((*(struct TLB_node **)nodep)->ip);
      sendto(instp->ctrl_sock, sdes_packet, sdes_length, 
             0,(struct sockaddr *)&sin,sizeof(sin));
   }
}

static void free_node(void *nodep)
{
}

static int find_delete(struct TLB_node *key)
{
   int found = 0;
   struct TLB_node **found_key = NULL;

   found_key = (struct TLB_node **)tfind(key, &TLB_node_list, compare_ip);
   if (found_key) {
       if (debug) ast_log(LOG_DEBUG,"...removing %s(%s)\n", (*found_key)->call, (*found_key)->ip); 
       found = 1;
       if (!(*found_key)->instp->confmode) 
		ast_softhangup((*found_key)->chan,AST_SOFTHANGUP_DEV);
       tdelete(key, &TLB_node_list, compare_ip);
   }
   return found;
}

static struct ast_frame  *TLB_xread(struct ast_channel *ast)
{
	struct TLB_pvt *p = ast->tech_pvt;
  
	memset(&p->fr,0,sizeof(struct ast_frame));
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

static int TLB_xwrite(struct ast_channel *ast, struct ast_frame *frame)
{
        int bye_length;
        unsigned char bye[50];
        unsigned short i;
        struct sockaddr_in sin;
	struct TLB_pvt *p = ast->tech_pvt;
	struct TLB_instance *instp = p->instp;
	struct ast_frame fr,*f1, *f2;
	struct TLB_rxqast *qpast;
	int n,m,x;
        struct TLB_rxqel *qpel;
	char buf[GSM_FRAME_SIZE + AST_FRIENDLY_OFFSET];

	if (frame->frametype != AST_FRAME_VOICE) return 0;

	if (!p->firstsent)
	{
		struct sockaddr_in sin;
		unsigned char  sdes_packet[256];
		int sdes_length;

		p->firstsent = 1;
		memset(sdes_packet,0,sizeof(sdes_packet));
		sdes_length = rtcp_make_sdes(sdes_packet,sizeof(sdes_packet),
			instp->mycall);

		sin.sin_family = AF_INET;
		sin.sin_port = htons(p->port + 1);
		sin.sin_addr.s_addr = inet_addr(p->ip);
		sendto(instp->ctrl_sock, sdes_packet, sdes_length, 
			0,(struct sockaddr *)&sin,sizeof(sin));
	}

        /* TheLinkBox to Asterisk */
	if (p->rxqast.qe_forw != &p->rxqast) {
		for(n = 0,qpast = p->rxqast.qe_forw; qpast != &p->rxqast; qpast = qpast->qe_forw) {
			n++;
		}
		if (n > QUEUE_OVERLOAD_THRESHOLD_AST) {
			while(p->rxqast.qe_forw != &p->rxqast) {
				qpast = p->rxqast.qe_forw;
				remque((struct qelem *)qpast);
				ast_free(qpast);
			}
			if (p->rxkey) p->rxkey = 1;
		} else {		
			if (!p->rxkey) {
				memset(&fr,0,sizeof(fr));
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
			qpast = p->rxqast.qe_forw;
			remque((struct qelem *)qpast);
			memcpy(buf + AST_FRIENDLY_OFFSET,qpast->buf,GSM_FRAME_SIZE);
			ast_free(qpast);

			memset(&fr,0,sizeof(fr));
			fr.datalen = GSM_FRAME_SIZE;
			fr.samples = 160;
			fr.frametype = AST_FRAME_VOICE;
			fr.subclass = AST_FORMAT_GSM;
			fr.data =  buf + AST_FRIENDLY_OFFSET;
			fr.src = type;
			fr.offset = AST_FRIENDLY_OFFSET;
			fr.mallocd=0;
			fr.delivery.tv_sec = 0;
			fr.delivery.tv_usec = 0;

			x = 0;
			if (p->dsp && (!instp->confmode))
			{
				f2 = ast_translate(p->xpath,&fr,0);
				f1 = ast_dsp_process(NULL,p->dsp,f2);
				ast_frfree(f2);
#ifdef	OLD_ASTERISK
				if (f1->frametype == AST_FRAME_DTMF)
#else
				if ((f1->frametype == AST_FRAME_DTMF_END) ||
					(f1->frametype == AST_FRAME_DTMF_BEGIN))
#endif
				{
					if ((f1->subclass != 'm') && (f1->subclass != 'u'))
					{
#ifndef	OLD_ASTERISK
						if (f1->frametype == AST_FRAME_DTMF_END)
#endif
							if (option_verbose > 2)
								ast_verbose(VERBOSE_PREFIX_3 "tlb: channel %s Got DTMF char %c from IP %s\n",p->stream,f1->subclass,p->ip);
						ast_queue_frame(ast,f1);
						x = 1;
					}
				}
			} 
			if (!x) ast_queue_frame(ast,&fr);
		}
	}
	if (p->rxkey == 1) {
		memset(&fr,0,sizeof(fr));
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

        if (instp->confmode && (p->rxqel.qe_forw != &p->rxqel))
        {
           for(m = 0,qpel = p->rxqel.qe_forw; qpel != &p->rxqel; qpel = qpel->qe_forw) 
              m++;

           if (m > QUEUE_OVERLOAD_THRESHOLD_EL)
           {
              while(p->rxqel.qe_forw != &p->rxqel) 
              {
                 qpel = p->rxqel.qe_forw;
                 remque((struct qelem *)qpel);
                 ast_free(qpel);
              }
           } 
           else 
           {
              qpel = p->rxqel.qe_forw;
              remque((struct qelem *)qpel);

              memcpy(instp->audio_all_but_one.data,qpel->buf,BLOCKING_FACTOR * GSM_FRAME_SIZE);
              ast_copy_string(instp->TLB_node_test.ip, qpel->fromip, TLB_IP_SIZE);
              instp->TLB_node_test.port = qpel->fromport;

              ast_free(qpel);
	      ast_mutex_lock(&instp->lock);
              twalk(TLB_node_list, send_audio_all_but_one);
	      ast_mutex_unlock(&instp->lock);

              if (instp->fdr >= 0)
                 write(instp->fdr, instp->audio_all_but_one.data, BLOCKING_FACTOR * GSM_FRAME_SIZE);
           }
        }
        else
        {
           /* Asterisk to TheLinkBox */
           if (!(frame->subclass & (AST_FORMAT_GSM))) {
                ast_log(LOG_WARNING, "Cannot handle frames in %d format\n", frame->subclass);
		ast_mutex_unlock(&instp->lock);
                return 0;
           }
           if (p->txkey || p->txindex)  {
                memcpy(instp->audio_all.data + (GSM_FRAME_SIZE * p->txindex++), frame->data,GSM_FRAME_SIZE);
           }      
           if (p->txindex >= BLOCKING_FACTOR) {
		ast_mutex_lock(&instp->lock);
                if (instp->confmode)
		{
			twalk(TLB_node_list, send_audio_all);
		}
		else
		{
			strcpy(instp->TLB_node_test.ip,p->ip);
			instp->TLB_node_test.port = p->port;
			twalk(TLB_node_list, send_audio_only_one);
		}
		ast_mutex_unlock(&instp->lock);
                p->txindex = 0;
           }
        }

	if (p->keepalive--) return 0;
	p->keepalive = KEEPALIVE_TIME;

        /* TheLinkBox: send heartbeats and drop dead stations */
	ast_mutex_lock(&instp->lock);
        instp->TLB_node_test.ip[0] = '\0';
        instp->TLB_node_test.port = 0;
        twalk(TLB_node_list, send_heartbeat); 
        if (instp->TLB_node_test.ip[0] != '\0') {
           if (find_delete(&instp->TLB_node_test)) {
              bye_length = rtcp_make_bye(bye,"rtcp timeout");
              sin.sin_family = AF_INET;
              sin.sin_addr.s_addr = inet_addr(instp->TLB_node_test.ip);
              sin.sin_port = htons(instp->TLB_node_test.port + 1);
	      ast_mutex_lock(&instp->lock);
              for (i = 0; i < 20; i++)
                 sendto(instp->ctrl_sock, bye, bye_length,
                        0,(struct sockaddr *)&sin,sizeof(sin));
	      ast_mutex_unlock(&instp->lock);
              if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "tlb: call=%s RTCP timeout, removing\n",instp->TLB_node_test.call);
           }
           instp->TLB_node_test.ip[0] = '\0';
           instp->TLB_node_test.port = 0;
        } 
	ast_mutex_unlock(&instp->lock);
	return 0;
}

static struct ast_channel *TLB_new(struct TLB_pvt *i, int state, unsigned int nodenum)
{
	struct ast_channel *tmp;
	struct TLB_instance *instp = i->instp;

#ifdef	OLD_ASTERISK
	tmp = ast_channel_alloc(1);
	if (tmp)
	{
		ast_setstate(tmp,state);
		ast_copy_string(tmp->context, instp->context, sizeof(tmp->context));
		ast_copy_string(tmp->exten, instp->astnode, sizeof(tmp->exten));
		snprintf(tmp->name, sizeof(tmp->name), "tlb/%s", i->stream);
#else
	tmp = ast_channel_alloc(1, state, 0, 0, "", instp->astnode, instp->context, 0, "tlb/%s", i->stream);
	if (tmp) {
#endif
		tmp->tech = &TLB_tech;
		tmp->nativeformats = prefformat;
		tmp->rawreadformat = prefformat;
		tmp->rawwriteformat = prefformat;
		tmp->writeformat = prefformat;
		tmp->readformat = prefformat;
		if (state == AST_STATE_RING)
			tmp->rings = 1;
		tmp->tech_pvt = i;
		ast_copy_string(tmp->context, instp->context, sizeof(tmp->context));
		ast_copy_string(tmp->exten, instp->astnode,  sizeof(tmp->exten));
#ifdef	OLD_ASTERISK
		ast_copy_string(tmp->language, "", sizeof(tmp->language));
#else
		ast_string_field_set(tmp, language, "");
#endif
		if (nodenum > 0)
		{
			char tmpstr[30];

			sprintf(tmpstr,"%u",nodenum);
			ast_set_callerid(tmp,tmpstr,NULL,NULL);
		}
		i->owner = tmp;
#ifdef	OLD_ASTERISK
		ast_mutex_lock(&usecnt_lock);
		usecnt++;
		ast_mutex_unlock(&usecnt_lock);
		ast_update_use_count();
#else
		i->u = ast_module_user_add(tmp);
#endif
		i->nodenum = nodenum;
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


static struct ast_channel *TLB_request(const char *type, int format, void *data, int *cause)
{
	int oldformat,nodenum,n;
	struct TLB_pvt *p;
	struct ast_channel *tmp = NULL;
	char *str,*cp,*cp1;
	
	oldformat = format;
	format &= (AST_FORMAT_GSM);
	if (!format) {
		ast_log(LOG_ERROR, "Asked to get a channel of unsupported format '%d'\n", oldformat);
		return NULL;
	}
	cp1 = 0;
	str = ast_strdup((char *)data);
	cp = strchr(str,'/');
	if (cp) *cp++ = 0;
	nodenum = 0;
	if (*cp && *++cp)
	{
		cp1 = strchr(cp,'/');
		if (cp1) *cp1++ = 0;
		nodenum = atoi(cp);
	}
	/* find instance name from AST node number */
	if (cp1) 
	{
	        for(n = 0; n < ninstances; n++)
	        {
	                if (!strcmp(instances[n]->astnode,cp1)) break;
	        }
	        if (n >= ninstances) n = 0;
	} else n = 0;
	p = TLB_alloc(instances[n]->name);
	ast_free(str);
	if (p) {
		tmp = TLB_new(p, AST_STATE_DOWN,nodenum);
		if (!tmp)
			TLB_destroy(p);
	}
	return tmp;
}

/*
* Enable or disable debug output at a given level at the console
*/
                                                                                                                                 
static int TLB_do_debug(int fd, int argc, char *argv[])
{
	int newlevel;

        if (argc != 4)
                return RESULT_SHOWUSAGE;
        newlevel = atoi(argv[3]);
        if((newlevel < 0) || (newlevel > 7))
                return RESULT_SHOWUSAGE;
        if(newlevel)
                ast_cli(fd, "TheLinkBox Debugging enabled, previous level: %d, new level: %d\n", debug, newlevel);
        else
                ast_cli(fd, "TheLinkBox Debugging disabled\n");

        debug = newlevel;                                                                                                                          
        return RESULT_SUCCESS;
}

/*
* Dump entire node list
*/
                                                                                                                                 
static int TLB_do_nodedump(int fd, int argc, char *argv[])
{

	struct ast_config *cfg = NULL;
	struct ast_variable *v;
	char *s,*strs[10];
	int n;

        if (argc != 2)
                return RESULT_SHOWUSAGE;

#ifdef  NEW_ASTERISK
        if (!(cfg = ast_config_load(config,zeroflag))) {
#else
        if (!(cfg = ast_config_load(config))) {
#endif
                ast_log(LOG_ERROR, "Unable to load config %s\n", config);
		return RESULT_FAILURE;
	}
	for (v = ast_variable_browse(cfg, "nodes"); v; v = v->next)
	{
		if (!v->value) continue;
		s = ast_strdupa(v->value);		
		strupr(s);
		n = finddelim(s,strs,10);
		if (n < 3) continue;
		ast_cli(fd,"%s|%s|%s|%s\n",v->name,strs[0],strs[1],strs[2]);
	}
	ast_config_destroy(cfg);
	return RESULT_SUCCESS;
}


/*
* Get tlb node entry
*/
                                                                                                                                 
static int TLB_do_nodeget(int fd, int argc, char *argv[])
{

	char c,*s,*sval,*val,*strs[10];
	int n;
	struct ast_config *cfg = NULL;
	struct ast_variable *v;

        if (argc != 4)
                return RESULT_SHOWUSAGE;

	c = tolower(*argv[2]);
#ifdef  NEW_ASTERISK
        if (!(cfg = ast_config_load(config,zeroflag))) {
#else
        if (!(cfg = ast_config_load(config))) {
#endif
                ast_log(LOG_ERROR, "Unable to load config %s\n", config);
		return RESULT_FAILURE;
	}
	s = ast_strdupa(argv[3]);
	strupr(s);
	if (c == 'n')
	{
		val = (char *)ast_variable_retrieve(cfg,"nodes",s);
		if (!val)
		{
			ast_cli(fd,"Error: Entry for %s not found !\n",s);
			ast_config_destroy(cfg);
			return RESULT_FAILURE;
		}
		sval = ast_strdupa(val);
		strupr(sval);
		n = finddelim(sval,strs,10);
		if (n < 3) 
		{
			ast_cli(fd,"Error: Entry for %s not found!\n",s);
			ast_config_destroy(cfg);
			return RESULT_FAILURE;
		}
	}
	else if ((c == 'i') || (c == 'c'))
	{
		for (v = ast_variable_browse(cfg, "nodes"); v; v = v->next)
		{
			if (!v->value) continue;
			sval = ast_strdupa(v->value);
			strupr(sval);
			n = finddelim(sval,strs,10);
			if (n < 3) continue;
			if (!strcmp(s,strs[(c == 'i') ? 0 : 1])) break;
		}
		if (!v)
		{
			ast_cli(fd,"Error: Entry for %s not found!\n",s);
			ast_config_destroy(cfg);
			return RESULT_FAILURE;
		}
		s = ast_strdupa(v->name);
		strupr(s);
	}
	else 
	{
		ast_config_destroy(cfg);
		return RESULT_FAILURE;
	}
	ast_config_destroy(cfg);
	ast_cli(fd,"%s|%s|%s|%s\n",s,strs[0],strs[1],strs[2]);
	return RESULT_SUCCESS;
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

static char *handle_cli_debug(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "tlb debug level";
                e->usage = debug_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(TLB_do_debug(a->fd,a->argc,a->argv));
}

static char *handle_cli_nodedump(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "tlb nodedump";
                e->usage = nodedump_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(TLB_do_nodedump(a->fd,a->argc,a->argv));
}


static char *handle_cli_nodeget(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "tlb nodeget";
                e->usage = nodeget_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(TLB_do_nodeget(a->fd,a->argc,a->argv));
}

static struct ast_cli_entry rpt_cli[] = {
	AST_CLI_DEFINE(handle_cli_debug,"Enable app_rpt debugging"),
	AST_CLI_DEFINE(handle_cli_nodedump,"Dump entire tlb node list"),
	AST_CLI_DEFINE(handle_cli_nodeget,"Look up tlb node entry""),
} ;

#endif

#ifndef	OLD_ASTERISK
static
#endif
int unload_module(void)
{
int	n;

        run_forever = 0;
        tdestroy(TLB_node_list, free_node);
	for(n = 0; n < ninstances; n++)
	{
		if (instances[n]->audio_sock != -1)
		{
			close(instances[n]->audio_sock);
			instances[n]->audio_sock = -1;
		}
		if (instances[n]->ctrl_sock != -1)
		{
			close(instances[n]->ctrl_sock);
			instances[n]->ctrl_sock = -1;
		}
	}	
#ifdef	NEW_ASTERISK
	ast_cli_unregister_multiple(TLB_cli,sizeof(TLB_cli) / 
		sizeof(struct ast_cli_entry));
#else
	/* Unregister cli extensions */
	ast_cli_unregister(&cli_debug);
	ast_cli_unregister(&cli_nodedump);
	ast_cli_unregister(&cli_nodeget);
#endif
	/* First, take us out of the channel loop */
	ast_channel_unregister(&TLB_tech);
	for(n = 0; n < ninstances; n++) ast_free(instances[n]);
	if (nullfd != -1) close(nullfd);
	return 0;
}

static int do_new_call(struct TLB_instance *instp, struct TLB_pvt *p, char *call, char *name)
{
        struct TLB_node *TLB_node_key = NULL;
	struct ast_config *cfg = NULL;
	struct ast_variable *v;
	char *sval,*strs[10];
	int n;

	TLB_node_key = (struct TLB_node *)ast_malloc(sizeof(struct TLB_node));
	if (TLB_node_key)
	{
		ast_copy_string(TLB_node_key->call,call,TLB_CALL_SIZE);
		ast_copy_string(TLB_node_key->ip, instp->TLB_node_test.ip, TLB_IP_SIZE);
		TLB_node_key->port = instp->TLB_node_test.port;
		ast_copy_string(TLB_node_key->name,name,TLB_NAME_SIZE); 
		/* find the node that matches the ipaddr and call */
#ifdef  NEW_ASTERISK
	        if (!(cfg = ast_config_load(config,zeroflag))) {
#else
	        if (!(cfg = ast_config_load(config))) {
#endif
	                ast_log(LOG_ERROR, "Unable to load config %s\n", config);
			ast_free(TLB_node_key); 
			return -1;
		}
		if (strcmp(call,"OUTBOUND"))
		{
			for (v = ast_variable_browse(cfg, "nodes"); v; v = v->next)
			{
				if (!v->value) continue;
				sval = ast_strdupa(v->value);
				strupr(sval);
				n = finddelim(sval,strs,10);
				if (n < 3) continue;
				if ((!strcmp(TLB_node_key->ip,strs[1])) &&
				    (TLB_node_key->port == (unsigned short)strtoul(strs[2],NULL,0)) &&
					(!strcmp(call,strs[0]))) break;
			}
			if (!v)
			{
				ast_log(LOG_ERROR, "Cannot find node entry for %s IP addr %s port %u\n",
					call,TLB_node_key->ip,TLB_node_key->port & 0xffff);
				ast_free(TLB_node_key); 
				ast_config_destroy(cfg);
				return 1;
			}
			TLB_node_key->nodenum = atoi(v->name);
		}
		else
		{
			TLB_node_key->nodenum = 0;
		}
		ast_config_destroy(cfg);
		TLB_node_key->countdown = instp->rtcptimeout;
		TLB_node_key->seqnum = 1;
		TLB_node_key->instp = instp;
		if (tsearch(TLB_node_key, &TLB_node_list, compare_ip))
		{
			if (option_verbose > 2) ast_verbose(VERBOSE_PREFIX_3 "tlb: new CALL = %s, ip = %s, port = %u\n",
				TLB_node_key->call,TLB_node_key->ip,
					TLB_node_key->port & 0xffff);
			if (instp->confmode)
			{
				TLB_node_key->p = instp->confp;
			}
			else
			{
				if (p == NULL) /* if a new inbound call */
				{
					p = TLB_alloc((void *)instp->name);
					if (!p)
					{
						ast_log(LOG_ERROR,"Cannot alloc el channel\n");
						return -1;
					}	
					TLB_node_key->p = p;
					ast_copy_string(TLB_node_key->p->ip, instp->TLB_node_test.ip,TLB_IP_SIZE);
					TLB_node_key->p->port = instp->TLB_node_test.port;
					TLB_node_key->chan = TLB_new(TLB_node_key->p,
						AST_STATE_RINGING,TLB_node_key->nodenum);
					if (!TLB_node_key->chan)
					{
						TLB_destroy(TLB_node_key->p);
						return -1;
					}
				}
				else
				{
					TLB_node_key->p = p;
					ast_copy_string(TLB_node_key->p->ip, instp->TLB_node_test.ip,TLB_IP_SIZE);
					TLB_node_key->p->port = instp->TLB_node_test.port;
					TLB_node_key->chan = p->owner;
				}
			}
		}
		else
		{
			ast_log(LOG_ERROR, "tsearch() failed to add CALL = %s,ip = %s,port = %u\n",
				TLB_node_key->call,TLB_node_key->ip,TLB_node_key->port & 0xffff);
			ast_free(TLB_node_key); 
			return -1;
		}
	}
	else
	{
		ast_log(LOG_ERROR,"malloc() failed for new CALL=%s, ip=%s, port=%u\n",
			call,instp->TLB_node_test.ip,instp->TLB_node_test.port);
		return -1;
	}
	return 0;
}

static void *TLB_reader(void *data)
{
	struct TLB_instance *instp = (struct TLB_instance *)data;
	char buf[1024];
	unsigned char bye[40];
	struct sockaddr_in sin,sin1;
 	int i,x;
        struct TLB_rxqast *qpast;
        struct TLB_rxqel *qpel;
	struct ast_frame fr;
        socklen_t fromlen;
	ssize_t recvlen;
        struct TLB_node **found_key = NULL;
        struct rtcp_sdes_request items;
        char call[128];
	fd_set fds[2];
	struct timeval tmout;

	if (option_verbose > 2) ast_verbose(VERBOSE_PREFIX_3 "tlb: reader thread started on %s.\n",instp->name);
	ast_mutex_lock(&instp->lock);
	while(run_forever)
	{

		ast_mutex_unlock(&instp->lock);
		FD_ZERO(fds);
		FD_SET(instp->audio_sock,fds);
		FD_SET(instp->ctrl_sock,fds);
		x = instp->audio_sock;
		if (instp->ctrl_sock > x) x = instp->ctrl_sock;
		tmout.tv_sec = 0;
		tmout.tv_usec = 50000;
		i = select(x + 1,fds,NULL,NULL,&tmout);
		if (i == 0) 
		{
			ast_mutex_lock(&instp->lock);
			continue;
		}
		if (i < 0)
		{
			ast_log(LOG_ERROR,"Error in select()\n");
			mythread_exit(NULL);
		}
		ast_mutex_lock(&instp->lock);
		if (FD_ISSET(instp->ctrl_sock,fds)) /* if a ctrl packet */
		{
			fromlen = sizeof(struct sockaddr_in);
			recvlen = recvfrom(instp->ctrl_sock,buf,sizeof(buf) - 1,
				0,(struct sockaddr *)&sin,&fromlen);
			if (recvlen > 0) 
			{
			    buf[recvlen] = '\0';
#ifdef  OLD_ASTERISK
			    ast_inet_ntoa(instp->TLB_node_test.ip,TLB_IP_SIZE,sin.sin_addr);
#else
			    ast_copy_string(instp->TLB_node_test.ip,ast_inet_ntoa(sin.sin_addr),
				TLB_IP_SIZE);
#endif
			    instp->TLB_node_test.port = ntohs(sin.sin_port) - 1;
			    if (is_rtcp_sdes((unsigned char *)buf,recvlen))
			    {
				items.nitems = 1;
				items.item[0].r_item = 2;
				items.item[0].r_text = NULL;
				parse_sdes((unsigned char *)buf,&items);
				call[0] = 0;
				if (items.item[0].r_text != NULL)
					copy_sdes_item(items.item[0].r_text,call, 127);
				if (call[0] != '\0')
				{
					found_key = (struct TLB_node **)tfind(&instp->TLB_node_test,
						&TLB_node_list, compare_ip); 
					if (found_key)
					{
						if (!(*found_key)->p->firstheard)
						{
							(*found_key)->p->firstheard = 1;
							memset(&fr,0,sizeof(fr));
							fr.datalen = 0;
							fr.samples = 0;
							fr.frametype = AST_FRAME_CONTROL;
							fr.subclass = AST_CONTROL_ANSWER;
							fr.data =  0;
							fr.src = type;
							fr.offset = 0;
							fr.mallocd=0;
							fr.delivery.tv_sec = 0;
							fr.delivery.tv_usec = 0;
							ast_queue_frame((*found_key)->chan,&fr);
							if (debug) ast_log(LOG_DEBUG,"Channel %s answering\n",
								(*found_key)->chan->name);
						}

						(*found_key)->countdown = instp->rtcptimeout;
					}
					else /* otherwise its a new request */
					{
						i = 0;  /* default authorized */
						if (instp->ndenylist)
						{
							for (x = 0; x < instp->ndenylist; x++)
							{
								if (!fnmatch(instp->denylist[x],call,FNM_CASEFOLD)) 
								{
									i = 1;
									break;
								}
							}
						}
						else
						{
							/* if permit list specified, default is not to authorize */
							if (instp->npermitlist) i = 1;
						}
						if (instp->npermitlist)
						{
							for (x = 0; x < instp->npermitlist; x++)
							{
								if (!fnmatch(instp->permitlist[x],call,FNM_CASEFOLD)) 
								{
									i = 0;
									break;
								}
							}
						}
						if (!i) /* if authorized */
						{
							i = do_new_call(instp,NULL,call,"UNKNOWN");
							if (i < 0)
							{
								ast_mutex_unlock(&instp->lock);
								mythread_exit(NULL);
							}
						}
						if (i) /* if not authorized */
						{
							if (debug) ast_log(LOG_DEBUG,"Sent bye to IP address %s\n",
								instp->TLB_node_test.ip);
							x = rtcp_make_bye(bye,"UN-AUTHORIZED");
							sin1.sin_family = AF_INET;
							sin1.sin_addr.s_addr = inet_addr(instp->TLB_node_test.ip);
							sin1.sin_port = htons(instp->TLB_node_test.port + 1);
							for (i = 0; i < 20; i++)
							{
								sendto(instp->ctrl_sock, bye, x,
									0,(struct sockaddr *)&sin1,sizeof(sin1));
							}
							instp->pending[x].fromip[0] = 0;
						}
					}
				}
			    }
			    else
			    {
				if (is_rtcp_bye((unsigned char *)buf,recvlen))
				{
					if (find_delete(&instp->TLB_node_test))
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "tlb: Disconnect from ip %s\n",instp->TLB_node_test.ip);
				} 
			    }
			}
		}
		if (FD_ISSET(instp->audio_sock,fds)) /* if an audio packet */
		{
			fromlen = sizeof(struct sockaddr_in);
			recvlen = recvfrom(instp->audio_sock,buf,sizeof(buf) - 1,0,
				(struct sockaddr *)&sin,&fromlen);
			if (recvlen > 0)
			{
				buf[recvlen] = '\0';
#ifdef  OLD_ASTERISK
				ast_inet_ntoa(instp->TLB_node_test.ip,TLB_IP_SIZE,sin.sin_addr);
#else
				ast_copy_string(instp->TLB_node_test.ip,ast_inet_ntoa(sin.sin_addr),TLB_IP_SIZE);
#endif
				instp->TLB_node_test.port = ntohs(sin.sin_port);

				found_key = (struct TLB_node **)tfind(&instp->TLB_node_test, &TLB_node_list, compare_ip);
				if (found_key)
				{
					struct TLB_pvt *p = (*found_key)->p;

					if (!(*found_key)->p->firstheard)
					{
						(*found_key)->p->firstheard = 1;
						memset(&fr,0,sizeof(fr));
						fr.datalen = 0;
						fr.samples = 0;
						fr.frametype = AST_FRAME_CONTROL;
						fr.subclass = AST_CONTROL_ANSWER;
						fr.data =  0;
						fr.src = type;
						fr.offset = 0;
						fr.mallocd=0;
						fr.delivery.tv_sec = 0;
						fr.delivery.tv_usec = 0;
						ast_queue_frame((*found_key)->chan,&fr);
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "tlb: Channel %s answering\n",
								(*found_key)->chan->name);
					}
					(*found_key)->countdown = instp->rtcptimeout;
					if (recvlen > 12) /* if at least a header size and some payload */
					{
						/* if its a DTMF frame */
						if ((((struct rtpVoice_t *)buf)->version == 2) &&
							(((struct rtpVoice_t *)buf)->payt == 69))
						{
							uint32_t dseq,dtime;
							char dchar,dstr[50];

							/* The DTMF sequence numbers are a 32 bit number. I guess to be
							   *really* pedantic, there should be code to handle a roll-over
							   in the sequence counter, but really, I don't think there is even
							   a remote possiblity of sending over 4 BILLION DTMF messages
							   in a lifetime, much less during a single connection, so
							   we're not going to worry about it here. */

							/* parse the packet. If not parseable, throw away */
							if (sscanf((char *)((struct rtpVoice_t *)buf)->data,
								"DTMF%c %u %u",&dchar,&dseq,&dtime) < 3) continue;
							ast_mutex_lock(&p->lock);
							/* if we had a packet before, and this one is before last one,
							    throw away */
							if (p->dtmflasttime && (dtime < p->dtmflasttime)) 
							{
								ast_mutex_unlock(&p->lock);
								continue;

							}
							/* if we get one out of sequence, or the same one again throw away */
							if (dseq <= p->dtmflastseq)			
							{
								ast_mutex_unlock(&p->lock);
								continue;

							}
							/* okay, this one is for real!!! */
							/* save lastdtmftime and lastdtmfseq */
							p->dtmflastseq = dseq;
							p->dtmflasttime = dtime;
							snprintf(dstr, sizeof(dstr) - 1, "D 0 %s %u %c", p->instp->astnode,
								++(p->dtmfidx), dchar);
							ast_mutex_unlock(&p->lock);
							/* Send DTMF (in dchar) to Asterisk */
							memset(&fr,0,sizeof(fr));
							fr.datalen = strlen(dstr) + 1;
							fr.data = dstr;
							fr.samples = 0;
							fr.frametype = AST_FRAME_TEXT;
							fr.subclass = 0;
							fr.src = type;
							fr.offset = 0;
							fr.mallocd=0;
							fr.delivery.tv_sec = 0;
							fr.delivery.tv_usec = 0;
							ast_queue_frame((*found_key)->chan,&fr);
							if (option_verbose > 2)
								ast_verbose(VERBOSE_PREFIX_3 "tlb: Channel %s got DTMF %c\n",
									(*found_key)->chan->name,dchar);
						}
						/* it its a voice frame */
						if ((((struct rtpVoice_t *)buf)->version == 2) &&
							(((struct rtpVoice_t *)buf)->payt == 3))
						{
							if (recvlen == sizeof(struct rtpVoice_t))
							{
								/* break them up for Asterisk */
								for (i = 0; i < BLOCKING_FACTOR; i++)
								{
									qpast = ast_malloc(sizeof(struct TLB_rxqast));
									if (!qpast)
									{
										ast_log(LOG_ERROR,"Cannot malloc for qpast\n");
										ast_mutex_unlock(&instp->lock);
										mythread_exit(NULL);
									}
									memcpy(qpast->buf,((struct rtpVoice_t *)buf)->data +
										(GSM_FRAME_SIZE * i),GSM_FRAME_SIZE);
									insque((struct qelem *)qpast,(struct qelem *)
										p->rxqast.qe_back);
								}
							}
							if (!instp->confmode) continue;
							/* need complete packet and IP address for TheLinkBox */
							qpel = ast_malloc(sizeof(struct TLB_rxqel));
							if (!qpel)
							{
								ast_log(LOG_ERROR,"Cannot malloc for qpel\n");
							}
							else
							{
								memcpy(qpel->buf,((struct rtpVoice_t *)buf)->data,
									BLOCKING_FACTOR * GSM_FRAME_SIZE);
								ast_copy_string(qpel->fromip,instp->TLB_node_test.ip,TLB_IP_SIZE);
								qpel->fromport = instp->TLB_node_test.port;
								insque((struct qelem *)qpel,(struct qelem *)
									p->rxqel.qe_back);
							}
						}
					}   
				}
			}
		}
	}
	ast_mutex_unlock(&instp->lock);
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "tlb: read thread exited.\n");
	mythread_exit(NULL);
	return NULL;
}

static int store_config(struct ast_config *cfg,char *ctg)
{
char	*val;
struct	TLB_instance *instp;
struct sockaddr_in si_me;
pthread_attr_t attr;

	if (ninstances >= TLB_MAX_INSTANCES)
	{
		ast_log(LOG_ERROR,"Too many instances specified\n");
		return -1;
	}

	instp = ast_malloc(sizeof(struct TLB_instance));
	if (!instp)
	{
		ast_log(LOG_ERROR,"Cannot malloc\n");
		return -1;
	}
	memset(instp,0,sizeof(struct TLB_instance));

	ast_mutex_init(&instp->lock);
	instp->audio_sock = -1;
	instp->ctrl_sock = -1;
	instp->fdr = -1;

        val = (char *) ast_variable_retrieve(cfg,ctg,"ipaddr"); 
        if (val) 
	{
		ast_copy_string(instp->ipaddr,val,TLB_IP_SIZE);
	}
        else
	{
		strcpy(instp->ipaddr,"0.0.0.0");
	}
        
        val = (char *) ast_variable_retrieve(cfg,ctg,"port"); 
        if (val)
	{
	   ast_copy_string(instp->port,val,TLB_IP_SIZE);
	}
        else
	{
	   strcpy(instp->port,"44966");
	}

        val = (char *) ast_variable_retrieve(cfg,ctg,"rtcptimeout");
        if (!val)
           instp->rtcptimeout = 15;
        else
           instp->rtcptimeout = atoi(val);

        val = (char *) ast_variable_retrieve(cfg,ctg,"astnode"); 
        if (val)
	{
	   ast_copy_string(instp->astnode,val,TLB_NAME_SIZE);
	}
        else
	{
	   strcpy(instp->astnode,"1999");
	}
        val = (char *) ast_variable_retrieve(cfg,ctg,"context"); 
        if (val)
	{
	   ast_copy_string(instp->context,val,TLB_NAME_SIZE);
	}
        else
	{
	   strcpy(instp->context,"tlb-in");
	}
        val = (char *) ast_variable_retrieve(cfg,ctg,"call");
        if (!val)
           ast_copy_string(instp->mycall,"INVALID",TLB_CALL_SIZE);
        else
           ast_copy_string(instp->mycall,val,TLB_CALL_SIZE);

        if (strcmp(instp->mycall,"INVALID") == 0)
	{
		ast_log(LOG_ERROR,"INVALID TheLinkBox call");
		return -1;
	}

	instp->confmode = 0;

        val = (char *) ast_variable_retrieve(cfg,ctg,"deny"); 
	if (val) instp->ndenylist = finddelim(strdup(val),instp->denylist,TLB_MAX_CALL_LIST);

        val = (char *) ast_variable_retrieve(cfg,ctg,"permit"); 
	if (val) instp->npermitlist = finddelim(strdup(val),instp->permitlist,TLB_MAX_CALL_LIST);

	instp->audio_sock = -1;
	instp->ctrl_sock = -1;

	if ((instp->audio_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1)
	{
		ast_log(LOG_WARNING, 
                        "Unable to create new socket for TheLinkBox audio connection\n");
		return -1;
	}
	if ((instp->ctrl_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1)
	{
		ast_log(LOG_WARNING, 
                        "Unable to create new socket for TheLinkBox control connection\n");
                close(instp->audio_sock);
                instp->audio_sock = -1;
		return -1;
	}
	memset((char *) &si_me, 0, sizeof(si_me));
	si_me.sin_family = AF_INET;
        if (strcmp(instp->ipaddr,"0.0.0.0") == 0)
		si_me.sin_addr.s_addr = htonl(INADDR_ANY);
        else
		si_me.sin_addr.s_addr = inet_addr(instp->ipaddr);
        instp->audio_port = strtoul(instp->port,NULL,0);;
	si_me.sin_port = htons(instp->audio_port);               
	if (bind(instp->audio_sock, &si_me, sizeof(si_me)) == -1) 
	{
		ast_log(LOG_WARNING, "Unable to bind port for TheLinkBox audio connection\n");
                close(instp->ctrl_sock); instp->ctrl_sock = -1;
		close(instp->audio_sock); instp->audio_sock = -1;
		return -1;
	}
        instp->ctrl_port = instp->audio_port + 1;
	si_me.sin_port = htons(instp->ctrl_port);
	if (bind(instp->ctrl_sock, &si_me, sizeof(si_me))==-1)
	{
		ast_log(LOG_WARNING, "Unable to bind port for TheLinkBox control connection\n");
                close(instp->ctrl_sock); instp->ctrl_sock = -1;
                close(instp->audio_sock); instp->audio_sock = -1;
		return -1;
	}
        fcntl(instp->audio_sock,F_SETFL,O_NONBLOCK);
        fcntl(instp->ctrl_sock,F_SETFL,O_NONBLOCK);
	ast_copy_string(instp->name,ctg,TLB_NAME_SIZE);
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        ast_pthread_create(&instp->TLB_reader_thread,&attr,TLB_reader,(void *)instp);
	instances[ninstances++] = instp;

	if (option_verbose > 2)
	{
	        ast_verbose(VERBOSE_PREFIX_3 "tlb: tlb/%s listening on %s port %s\n", instp->name, instp->ipaddr,instp->port);
	        ast_verbose(VERBOSE_PREFIX_3 "tlb: tlb/%s call set to %s\n",instp->name,instp->mycall);
	}
	return 0;
}
#ifndef	OLD_ASTERISK
static
#endif
int load_module(void)
{
	struct ast_config *cfg = NULL;
        char *ctg = NULL;

#ifdef  NEW_ASTERISK
        struct ast_flags zeroflag = {0};
#endif

#ifdef  NEW_ASTERISK
        if (!(cfg = ast_config_load(config,zeroflag))) {
#else
        if (!(cfg = ast_config_load(config))) {
#endif
                ast_log(LOG_ERROR, "Unable to load config %s\n", config);
                return AST_MODULE_LOAD_DECLINE;
        }

        while ( (ctg = ast_category_browse(cfg, ctg)) != NULL)
	{
		if (ctg == NULL) continue;
		if (!strcmp(ctg,"nodes")) continue;
                if (store_config(cfg, ctg) < 0) return AST_MODULE_LOAD_DECLINE;
	}
        ast_config_destroy(cfg);
        cfg = NULL; 
	ast_log(LOG_NOTICE,"Total of %d TheLinkBox instances found\n",ninstances);
	if (ninstances < 1)
	{
		ast_log(LOG_ERROR,"Cannot run TheLinkBox with no instances\n");
		return AST_MODULE_LOAD_DECLINE;
	}

#ifdef	NEW_ASTERISK
	ast_cli_register_multiple(TLB_cli,sizeof(TLB_cli) / 
		sizeof(struct ast_cli_entry));
#else
	/* Register cli extensions */
	ast_cli_register(&cli_debug);
	ast_cli_register(&cli_nodedump);
	ast_cli_register(&cli_nodeget);
#endif
	/* Make sure we can register our channel type */
	if (ast_channel_register(&TLB_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
                return AST_MODULE_LOAD_DECLINE;
	}
	nullfd = open("/dev/null",O_RDWR);
	return 0;
}

#ifdef	OLD_ASTERISK
char *description()
{
	return (char *)TLB_tech.description;
}

int usecount()
{
	return usecnt;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
#else
AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "TheLinkBox channel driver");
#endif
