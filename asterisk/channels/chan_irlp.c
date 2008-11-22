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
 * \brief irlp channel driver for Asterisk
 * 
 * \author Scott Lawson/KI4LKF <ham44865@yahoo.com>
 * \author Jim Dixon, WB6NIL <jim@lambdatel.com>
 *
 * \ingroup channel_drivers
 */

/*** MODULEINFO
 ***/

#define	rpt_free(p) __ast_free(p,__FILE__,__LINE__,__PRETTY_FUNCTION__)


/* Version 0.16, 11/17/2008
irlp channel driver for Asterisk/app_rpt.

I wish to thank the following people for the immeasurable amount of
very high-quality assistance they have provided me, without which this
project would have been impossible:

Scott, KI4LKF
Skip, WB6YMH
Randy, KC6HUR
Steve, N4IRS
Dixon Jim, K6JWN (not to be confused with Jim Dixon)
Eric, KA6UAI for putting up with a few miserable days of 
   testing using his on-the-air repeater
*/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

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
#include <sys/types.h>
#include <sys/stat.h>

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/translate.h"
#include "asterisk/cli.h"

#define	MAX_RXKEY_TIME 4

#define	KEEPALIVE_TIME 10
#define QUEUE_OVERLOAD_THRESHOLD_AST 25

#define ADPCM_BLOCKING_FACTOR 6
#define ADPCM_FRAME_SIZE 80

#define GSM_BLOCKING_FACTOR 10
#define GSM_FRAME_SIZE 33

#define ULAW_BLOCKING_FACTOR 1
#define ULAW_FRAME_SIZE 256

#define IRLP_IP_SIZE 15
#define IRLP_CALL_SIZE 15
#define IRLP_NODE_SIZE 7
#define IRLP_HEADER_INFO_SIZE 24
#define IRLP_ADPCM_STATE_INFO_SIZE 3

#define	LARGEST_PACKET_SIZE 1024
#define	IRLP_ROOT "/home/irlp/local"
#define	IRLP_RESET "su - repeater /tmp/irlpwrap /home/irlp/custom/irlp_fullreset"
#define	IRLP_END "su - repeater /tmp/irlpwrap /home/irlp/scripts/end &"
#define	IRLP_CALL_REFL "su - repeater /tmp/irlpwrap /home/irlp/scripts/connect_to_reflector ref%04d &"
#define	IRLP_CALL "su - repeater /tmp/irlpwrap /home/irlp/scripts/call stn%04d &"
#define	IRLP_WRAPPER "echo '#! /bin/sh\n\n. /home/irlp/custom/environment\n" \
	"exec $1 $2 $3 $4 $5 $6\n' > /tmp/irlpwrap ; chown repeater /tmp/irlpwrap ; chmod 755 /tmp/irlpwrap"
#define	IRLP_AST_PLAYFILE "rpt playback %s /home/irlp/astrun/astplay"
#define	IRLP_PLAY_FILE "/home/irlp/astrun/astplay.pcm"
#define	IRLP_DTMF_FIFO "/home/irlp/astrun/dtmf_fifo"
#define	IRLP_MAKE_FIFO "(mkdir -p /home/irlp/astrun; " \
	"chown repeater /home/irlp/astrun; "\
	"/bin/mknod /home/irlp/astrun/dtmf_fifo p;" \
	"chown repeater /home/irlp/astrun/dtmf_fifo) > /dev/null 2>&1"
#define	IRLP_SEND_DTMF "su - repeater /tmp/irlpwrap \"/home/irlp/scripts/fifoecho stn%04d dtmfregen %s\""

enum {IRLP_NOPROTO,IRLP_ISADPCM,IRLP_ISGSM,IRLP_ISULAW} ;

struct irlp_audio
{
   int compression;
   char sendinghost[IRLP_IP_SIZE + 1];
   struct
   {
      int buffer_len;
      char buffer_val[LARGEST_PACKET_SIZE];
   } buffer;
};

struct adpcm_state {
   short       valprev;
   char        index; 
};

struct irlp_rxqast {
	struct irlp_rxqast *qe_forw;
	struct irlp_rxqast *qe_back;
        short int len;
	char buf[1];
};

struct irlp_pvt {
	struct ast_channel *owner;
	char app[16];		
	char txkey;
	int rxkey;
	struct ast_frame fr;	
	struct ast_module_user *u;
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

/*
   The remote IRLP node
*/ 
static char remote_irlp_node_ip[IRLP_IP_SIZE + 1];

/* 
   This is your local IRLP node and
   some config values.
   Maybe save that into a config struct
*/
static char mycall[IRLP_CALL_SIZE + 1];
static char mynode[IRLP_NODE_SIZE + 1];
static char astnode[20];
static char astnode1[20];
static short rtcptimeout = 10;
static short localispeakerport = 2174;
static int radmode = 0;
static int nodenum = 0;
static int audio_sock = -1;
static int ctrl_sock = -1;
static int tx_buf_n = 0;
static int tx_audio_port = 0;
static int alt_audio_sock = -1;
static int alt_ctrl_sock = -1;
static uint16_t audio_port_cfg = 2084;
static uint16_t audio_port;
static uint16_t ctrl_port;
static char *config = "irlp.conf";
static const char tdesc[] = "irlp channel driver by KI4LKF";
static int prefformat = AST_FORMAT_ADPCM;
static char context[AST_MAX_EXTENSION] = "default";
static char type[] = "irlp";
static pthread_t irlp_reader_thread;
static int run_forever = 1;
static int proto = IRLP_NOPROTO;
static int in_node = 0;
static struct irlp_rxqast rxqast;
static char *outbuf_old;
static int rxlen;
static int rxidx;
static int ready = 0;
static struct ast_channel *curcall = NULL;
AST_MUTEX_DEFINE_STATIC(irlplock);
static char stream[256];
static int nullfd = -1;
static int dfd = -1;
static int playing = 0;
static unsigned int xcount = 0;
static char havedtmf = 0;
static char irlp_dtmf_string[64];
static char irlp_dtmf_special = 0;
time_t keepalive = 0;

#ifdef OLD_ASTERISK
#define ast_free free
#define ast_malloc malloc
#endif

static struct ast_channel *irlp_request(const char *type, int format, void *data, int *cause);
static int irlp_call(struct ast_channel *ast, char *dest, int timeout);
static int irlp_hangup(struct ast_channel *ast);
static struct ast_frame *irlp_xread(struct ast_channel *ast);
static int irlp_xwrite(struct ast_channel *ast, struct ast_frame *frame);
static int irlp_indicate(struct ast_channel *ast, int cond, const void *data, size_t datalen);
static int irlp_digit_begin(struct ast_channel *c, char digit);
static int irlp_digit_end(struct ast_channel *c, char digit, unsigned int duratiion);
static int irlp_text(struct ast_channel *c, const char *text);
static struct irlp_pvt *irlp_alloc(void *data);
static int is_valid_rtcp(unsigned char *p, int len);
static int is_rtcp_bye(unsigned char *p, int len);
static struct ast_channel *irlp_new(struct irlp_pvt *i, int state);

static const struct ast_channel_tech irlp_tech = {
	.type = type,
	.description = tdesc,
	.capabilities = AST_FORMAT_ADPCM | AST_FORMAT_GSM,
	.requester = irlp_request,
	.call = irlp_call,
	.hangup = irlp_hangup,
	.read = irlp_xread,
	.write = irlp_xwrite,
	.indicate = irlp_indicate,
	.send_text = irlp_text,
	.send_digit_begin = irlp_digit_begin,
	.send_digit_end = irlp_digit_end,
};

void static reset_stuff(void)
{

	if (alt_audio_sock != -1) close(alt_audio_sock);
	alt_audio_sock = -1;
	if (alt_ctrl_sock != -1) close(alt_ctrl_sock);
	alt_ctrl_sock = -1;
	proto = IRLP_NOPROTO;
	keepalive = 0;
	in_node = 0;
	nodenum = 0;
	tx_buf_n = 0;
	ready = 0;
	rxqast.qe_forw = &rxqast;
	rxqast.qe_back = &rxqast;
	remote_irlp_node_ip[0] = 0;
	tx_audio_port = audio_port_cfg;
	audio_port = audio_port_cfg;
	ctrl_port = audio_port + 1;
	havedtmf = 0;
	memset(irlp_dtmf_string,0,sizeof(irlp_dtmf_string));
	irlp_dtmf_special = 0;
	if (curcall)
	{
	       curcall->nativeformats = AST_FORMAT_ADPCM;
	       ast_set_read_format(curcall,curcall->readformat);
	       ast_set_write_format(curcall,curcall->writeformat);
	}
	return;
}


static char *irlp_read_file(char *basename,char *fname)
{
char s[200],*str;
FILE *fp;
int  len;
struct stat mystat;

	snprintf(s,sizeof(s) - 1,"%s/%s",basename,fname);
	fp = fopen(s,"r");
	if (!fp) return(NULL);
	if (fstat(fileno(fp),&mystat) == -1) 
	{
		fclose(fp);
		return NULL;
	}
	len = mystat.st_size;
	str = malloc(len + 1);
	if (!str)
	{
		ast_log(LOG_ERROR,"Cant malloc");
		fclose(fp);
		return NULL;
	}
	if (fread(str,1,len,fp) != len)
	{
		fclose(fp);
		return NULL;
	}
	fclose(fp);
	str[len] = 0;
	return(str);
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

static int is_valid_rtcp(unsigned char *p, int len)
{
   unsigned char *end;

    if (len < 4)
       return 0;

    if (((((p[0] >> 6) & 3) != 2) &&
        ((((p[0] >> 6) & 3) != 1))) ||
        ((p[0] & 0x20) != 0) ||
        ((p[1] != 200) && (p[1] != 201)))
      return 0;

    end = p + len;

    do {
        p += (((p[2] << 8) | p[3]) + 1) * 4;
    } while (p < end && (((p[0] >> 6) & 3) == 2));

    return (p == end)?1:0;
}

static int rtcp_make_sdes(unsigned char *pkt, int pktLen)
{
    unsigned char zp[1500];
    unsigned char *p = zp;
    struct rtcp_t *rp;
    unsigned char *ap;
    char line[150];
    int l, hl, pl;

    hl = 0;
    *p++ = 0x40;
    *p++ = 201;
    *p++ = 0;
    *p++ = 1;
    *((long *) p) = htonl(0x0310f987);
    p += 4;
    hl = 8;

    rp = (struct rtcp_t *) p;
    *((short *) p) = htons((2 << 14) | 202 | (1 << 8));
    rp->r.sdes.src = htonl(0x0310f987);
    ap = (unsigned char *) rp->r.sdes.item;

    strcpy(line,"CALLSIGN");
    *ap++ = 1;
    *ap++ = l = strlen(line);
    memcpy(ap,line,l);
    ap += l;

    snprintf(line,sizeof(line) - 1,"%s IRLP",mynode);
    *ap++ = 2;
    *ap++ = l = strlen(line);
    memcpy(ap,line,l);
    ap += l;

    strcpy(line,"Asterisk/app_rpt/IRLP");
    *ap++ = 6;
    *ap++ = l = strlen(line);
    memcpy(ap,line,l); 
    ap += l;

    *ap++ = 0;
    *ap++ = 0;
    *ap++ = 0xbf;
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

    *p++ = 0x40;
    *p++ = 201;
    *p++ = 0;
    *p++ = 1;
    *((long *) p) = htonl(0x0310f987);
    p += 4;
    hl = 8;

    rp = (struct rtcp_t *)p;
    *((short *) p) = htons((2 << 14) | 203 | (1 << 8));
    rp->r.bye.src[0] = htonl(0x0310f987);
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

static void send_bye(char *reason)
{
char	buf[200],*cp,ipbuf[100];
int	len,x;
struct sockaddr_in sin;

	if (!*remote_irlp_node_ip)
	{
		cp = irlp_read_file(IRLP_ROOT,"calledip");
		if (!cp) return;
		if (cp[strlen(cp) - 1] == '\n')
			cp[strlen(cp) - 1] = 0;
		strncpy(ipbuf,cp,sizeof(ipbuf) - 1);
		ast_free(cp);
	}
	else
	{
		strncpy(ipbuf,remote_irlp_node_ip,sizeof(buf) - 1);
	}
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = inet_addr(ipbuf);
        sin.sin_port = htons(tx_audio_port + 1);
	len = rtcp_make_bye((unsigned char *)buf,reason);
	for(x = 0; x < 1; x++)
	{	
	        sendto((alt_ctrl_sock != -1) ? alt_ctrl_sock : ctrl_sock,buf,len,
        	        0,(struct sockaddr *)&sin,sizeof(struct sockaddr));
	}
	return;
}

static void send_keepalive(void)
{
char	buf[200],*cp;
int	len;
struct sockaddr_in sin;

	if (!*remote_irlp_node_ip)
	{
		cp = irlp_read_file(IRLP_ROOT,"calledip");
		if (!cp) return;
		if (cp[strlen(cp) - 1] == '\n')
			cp[strlen(cp) - 1] = 0;
		strncpy(buf,cp,sizeof(buf) - 1);
		ast_free(cp);
	}
	else
	{
		strncpy(buf,remote_irlp_node_ip,sizeof(buf) - 1);
	}
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = inet_addr(buf);
        sin.sin_port = htons(tx_audio_port + 1);
	len = rtcp_make_sdes((unsigned char *)buf,sizeof(buf) - 1);
        sendto((alt_ctrl_sock != -1) ? alt_ctrl_sock : ctrl_sock,buf,len,
                0,(struct sockaddr *)&sin,sizeof(struct sockaddr));
	return;
}

static int do_new_call(void)
{

	struct irlp_pvt *p;

	p = irlp_alloc((void *)"");
	if (!p)
	{
		ast_log(LOG_ERROR,"Cannot alloc irlp channel\n");
		return -1;
	}
	curcall = irlp_new(p,AST_STATE_RINGING);
	if (!curcall)
	{
		ast_log(LOG_ERROR,"Cannot alloc irlp channel\n");
		return -1;
	}
	return 0;
}

static int irlp_call(struct ast_channel *ast, char *dest, int timeout)
{
	struct irlp_pvt *p;
	char *cp,str[100];

	p = ast->tech_pvt;

	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "irlp_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}
	cp = irlp_read_file(IRLP_ROOT,"active");
	if (cp && *cp) 
	{
		ast_safe_system(IRLP_END);
		usleep(10000);
	}
	if (cp) ast_free(cp);
	if ((!radmode) && nodenum)
	{
		if (nodenum >= 9000)
			snprintf(str,sizeof(str) - 1,IRLP_CALL_REFL,nodenum);
		else
			snprintf(str,sizeof(str) - 1,IRLP_CALL,nodenum);
		ast_safe_system(str);
		usleep(10000);
	}		

	ast_setstate(ast,(radmode) ? AST_STATE_UP : AST_STATE_RINGING);
	return 0;
}

static void irlp_destroy(struct irlp_pvt *p)
{
	reset_stuff();
	curcall = NULL;
	ast_module_user_remove(p->u);
	ast_free(p);
}

static void process_codec_file(struct ast_channel *ast)
{
	char *cp;

	  if (!ready) return;
	  cp = irlp_read_file(IRLP_ROOT,"codec");
	  if (cp) 
	  {
		  if (cp[strlen(cp) - 1] == '\n') cp[strlen(cp) - 1] = 0;
		  if (!strncasecmp(cp,"GSM",3))
		  {
			  if (proto == IRLP_NOPROTO)
				  ast_log(LOG_NOTICE,"irlp channel format set to %s\n",cp);
			  else if (proto != IRLP_ISGSM)
				  ast_log(LOG_NOTICE,"irlp channel format changed to %s\n",cp);
			  proto = IRLP_ISGSM;
		          ast->nativeformats = AST_FORMAT_GSM;
		  }
		  else if (!strncasecmp(cp,"UNCOMP",6))
		  {
			  if (proto == IRLP_NOPROTO)
				  ast_log(LOG_NOTICE,"irlp channel format set to ULAW (%s)\n",cp);
			  else if (proto != IRLP_ISULAW)
				  ast_log(LOG_NOTICE,"irlp channel format changed to ULAW (%s)\n",cp);
			  proto = IRLP_ISULAW;
		          ast->nativeformats = AST_FORMAT_ULAW;
		  }
		  else 
		  {
			  if (proto == IRLP_NOPROTO)
				  ast_log(LOG_NOTICE,"irlp channel format set to %s\n",cp);
			  else if (proto != IRLP_ISADPCM)
				  ast_log(LOG_NOTICE,"irlp channel format changed to %s\n",cp);
			  proto = IRLP_ISADPCM;
		          ast->nativeformats = AST_FORMAT_ADPCM;
		  }
		  ast_set_read_format(ast,ast->readformat);
		  ast_set_write_format(ast,ast->writeformat);
		  ast_free(cp);
	  }
	  return;
}


static void *irlp_reader(void *nothing)
{

	fd_set fds[3];
	struct timeval tmout;
	int i,myaud,myctl,x,was_outbound,seen_curcall;

	char buf[LARGEST_PACKET_SIZE + 1];
	struct sockaddr_in sin;
        struct irlp_rxqast *qpast;
        char ip[IRLP_IP_SIZE + 1],*cp;
        socklen_t fromlen;
	ssize_t recvlen;
        size_t len;
	struct stat statbuf;
	static int play_outbound = 0;
	time_t now;

	ast_log(LOG_NOTICE, "IRLP reader thread started.\n");
	seen_curcall = 0;
	while(run_forever)
	{
		if ((proto == IRLP_NOPROTO) && curcall) process_codec_file(curcall);
		i = ((stat(IRLP_PLAY_FILE,&statbuf) != -1));
		if (i != playing)
		{
			char mystr[60];

			if (!playing)
			{
				was_outbound = 0;
				if (((curcall != NULL)) != play_outbound)
				{
					was_outbound = play_outbound;
					play_outbound = ((curcall != NULL));
				}
				snprintf(mystr,sizeof(mystr) - 1,
				    IRLP_AST_PLAYFILE,(play_outbound || 
					was_outbound) ? astnode1 : astnode);
				ast_cli_command(nullfd,mystr);
			}
			playing = i;
		}
		myaud = (alt_audio_sock != -1) ? alt_audio_sock : audio_sock;
		myctl = (alt_ctrl_sock != -1) ? alt_ctrl_sock : ctrl_sock;

		time(&now);
		if (keepalive && ((keepalive + KEEPALIVE_TIME) < now))
		{
			cp = irlp_read_file(IRLP_ROOT,"codec");
			i = ((cp && *cp));
			if (cp) ast_free(cp);
			if (i) send_keepalive();
			keepalive = now;
		}
		FD_ZERO(fds);
		FD_SET(myaud,fds);
		FD_SET(myctl,fds);
		x = myaud;
		if (myctl > x) x = myctl;
		tmout.tv_sec = 0;
		tmout.tv_usec = 50000;
		i = select(x + 1,fds,NULL,NULL,&tmout);
		if (i == 0) 
		{
			continue;
		}
		if (i < 0)
		{
			ast_log(LOG_ERROR,"Error in select()\n");
			break;
		}
		if (FD_ISSET(myctl,fds)) /* if a ctrl packet */
		{ 
	           fromlen = sizeof(struct sockaddr_in);
	           recvlen = recvfrom(myctl,
                                  buf,
                                  LARGEST_PACKET_SIZE,
                                  0,
                                  (struct sockaddr *)&sin,&fromlen);
			
	           if (recvlen > 0) 
		   {
	              buf[recvlen] = '\0';
#ifdef  OLD_ASTERISK
	              ast_inet_ntoa(ip,IRLP_IP_SIZE,sin.sin_addr);
#else
	              strncpy(ip,ast_inet_ntoa(sin.sin_addr),IRLP_IP_SIZE);
#endif
	              if (is_valid_rtcp((unsigned char *)buf,recvlen))
		      {
	                 if (!is_rtcp_bye((unsigned char *)buf,recvlen))
			 {
	                    if (strncmp(ip, "127.0.0.1",IRLP_IP_SIZE) != 0)
			    {
	                       if (strncmp(remote_irlp_node_ip, ip, IRLP_IP_SIZE) != 0) 
			       {
	                          strncpy(remote_irlp_node_ip, ip, IRLP_IP_SIZE);
				  cp = irlp_read_file(IRLP_ROOT,"active");
				  if (cp && (strlen(cp) > 3))
				  {
					if (cp[strlen(cp) - 1] == '\n')
						cp[strlen(cp) - 1] = 0;
					in_node = atoi(cp + 3);
				  	ast_log(LOG_NOTICE,"irlp node connected from %s node %s\n",ip,cp + 3);
					if (in_node >= 9990) keepalive = 0;
				  	if (!curcall) do_new_call();
					if ((!ready) && curcall)
					{
						struct ast_frame fr;

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
						ast_queue_frame(curcall,&fr);
		
					}
					ready = 1;
					if (curcall && (proto == IRLP_NOPROTO)) process_codec_file(curcall);
				  } else ast_log(LOG_NOTICE,"irlp node attempted connect from %s with no node info\n", ip);
				  if (cp) ast_free(cp);
	                       }
	                    }
	                 } 
	                 else 
			 {
	                    if (strncmp(ip, remote_irlp_node_ip, IRLP_IP_SIZE) == 0)
			    {
			       reset_stuff();
			       if ((!radmode) && curcall) ast_softhangup(curcall,AST_SOFTHANGUP_DEV);
	                       ast_log(LOG_NOTICE, "received IRLP bye from %s\n",ip);
			    }
	                 }
	              }
		   }
		}
		if (FD_ISSET(myaud,fds)) /* if a audio packet */
		{
	           fromlen = sizeof(struct sockaddr_in);
	           recvlen = recvfrom(myaud,
                              buf,
                              LARGEST_PACKET_SIZE,
                              0,
                              (struct sockaddr *)&sin,&fromlen);
	           if (recvlen > 0)
		   {
	              buf[recvlen] = '\0';
#ifdef  OLD_ASTERISK
	              ast_inet_ntoa(ip,IRLP_IP_SIZE,sin.sin_addr);
#else
	              strncpy(ip,ast_inet_ntoa(sin.sin_addr),IRLP_IP_SIZE);
#endif
	              len = ntohl(((struct irlp_audio *)buf)->buffer.buffer_len); 
	              if (((strncmp(ip, remote_irlp_node_ip, IRLP_IP_SIZE) == 0) ||
	                   (strncmp(ip, "127.0.0.1", IRLP_IP_SIZE) == 0)) &&
	                   (len > IRLP_ADPCM_STATE_INFO_SIZE) &&
	                   ((recvlen - IRLP_HEADER_INFO_SIZE) == len)) 
		      {

	                  if (((struct irlp_audio *)buf)->compression == htonl(0x200 | 0x40000000))
	    	          {
			      if (proto == IRLP_NOPROTO)
				  ast_log(LOG_NOTICE,"irlp channel format set to ADPCM\n");
			      else if (proto != IRLP_ISADPCM)
				  ast_log(LOG_NOTICE,"irlp channel format changed to ADPCM\n");
			      proto = IRLP_ISADPCM;
			      if (curcall)
			      {

		                      curcall->nativeformats = AST_FORMAT_ADPCM;
				      ast_set_read_format(curcall,curcall->readformat);
				      ast_set_write_format(curcall,curcall->writeformat);
			      }
		          }
	                  else if (((struct irlp_audio *)buf)->compression == htonl(0x20 | 0x40000000))
		          {
			      if (proto == IRLP_NOPROTO)
				  ast_log(LOG_NOTICE,"irlp channel format set to GSM\n");
			      else if (proto != IRLP_ISGSM)
				  ast_log(LOG_NOTICE,"irlp channel format changed to GSM\n");
			      proto = IRLP_ISGSM;
			      if (curcall)
			      {
		                      curcall->nativeformats = AST_FORMAT_GSM;
				      ast_set_read_format(curcall,curcall->readformat);
				      ast_set_write_format(curcall,curcall->writeformat);
			      }
		          }
	                  else if (((struct irlp_audio *)buf)->compression == htonl(0x40000000))
		          {
			      if (proto == IRLP_NOPROTO)
				  ast_log(LOG_NOTICE,"irlp channel format set to ULAW (UNCOMP)\n");
			      else if (proto != IRLP_ISULAW)
				  ast_log(LOG_NOTICE,"irlp channel format changed to ULAW (UNCOMP)\n");
			      proto = IRLP_ISULAW;
			      if (curcall)
			      {
		                      curcall->nativeformats = AST_FORMAT_ULAW;
				      ast_set_read_format(curcall,curcall->readformat);
				      ast_set_write_format(curcall,curcall->writeformat);
			      }
		          }
	                 qpast = ast_malloc(sizeof(struct irlp_rxqast) + len);
	                 if (!qpast) 
			 {
	                    ast_log(LOG_NOTICE,"Cannot malloc for qpast\n");
			    break;
	                 }
			 if (proto != IRLP_ISGSM)
			 {
		                 qpast->len = len;
		                 memcpy(qpast->buf,((struct irlp_audio *)buf)->buffer.buffer_val,len);
			 }
			 else
			 {
		                 qpast->len = len - 2;
		                 memcpy(qpast->buf,((struct irlp_audio *)buf)->buffer.buffer_val + 2,len);
			 }
	                 insque((struct qelem *)qpast,(struct qelem *)rxqast.qe_back);
	                 if (strncmp(ip, "127.0.0.1", IRLP_IP_SIZE) == 0)
			 {
	                    if (remote_irlp_node_ip[0] != '\0') 
			    {
	                       sin.sin_family = AF_INET;
	                       sin.sin_addr.s_addr = inet_addr(remote_irlp_node_ip);
	                       sin.sin_port = htons(tx_audio_port);
	                       sendto((alt_audio_sock != -1) ? alt_audio_sock : audio_sock,buf,recvlen,
	                               0,(struct sockaddr *)&sin,sizeof(struct sockaddr));
	                    }
	                 }
			 else 
			 {
	                    sin.sin_family = AF_INET;
	                    sin.sin_addr.s_addr = inet_addr("127.0.0.1");
	                    sin.sin_port = htons(localispeakerport); 
	                    sendto(audio_sock,buf,recvlen,
	                           0,(struct sockaddr *)&sin,sizeof(struct sockaddr));
	                 }
	              }
		   }
		}
 	} 
	ast_mutex_unlock(&irlplock);
	if (run_forever)
	{
		ast_cli_command(nullfd,"rpt restart");
	}
	run_forever = 0;
	ast_log(LOG_NOTICE, "IRLP read thread exited.\n");
	pthread_exit(NULL);
}


static struct irlp_pvt *irlp_alloc(void *data)
{
	struct irlp_pvt *p;
	struct sockaddr_in si_me;
        
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(astnode);
		AST_APP_ARG(nodenum);
	);

	args.nodenum = NULL;
	args.astnode = NULL;
	if (!ast_strlen_zero(data)) 
		AST_NONSTANDARD_APP_ARGS(args,data,'/');

	p = ast_malloc(sizeof(struct irlp_pvt));
	if (p) {
		memset(p, 0, sizeof(struct irlp_pvt));
		nodenum = 0;
		rxqast.qe_forw = &rxqast;
		rxqast.qe_back = &rxqast;
		sprintf(stream,"%d",audio_port);
		tx_audio_port = audio_port_cfg;
		if ((!radmode) && args.astnode && *args.astnode)
		{
			strncpy(astnode1,args.astnode,sizeof(astnode1) - 1);
		}
		if ((!radmode) && args.nodenum && *args.nodenum)
		{
			nodenum = atoi(args.nodenum);
			if ((nodenum < 1000) || (nodenum > 9999))
			{
				ast_log(LOG_ERROR,"Requested node number %s invalid\n",args.nodenum);
				ast_free(p);
				return NULL;
			}
			if (nodenum >= 9000) tx_audio_port += (2 * (nodenum % 10));
		}

		if (tx_audio_port != audio_port)
		{
			if ((alt_audio_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
			{
				ast_log(LOG_WARNING, 
	                                "Unable to create new socket for irlp audio connection\n");
				ast_free(p);
				return(NULL);
			}
			if ((alt_ctrl_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
				ast_log(LOG_WARNING, 
	                                "Unable to create new socket for irlp control connection\n");
	                        close(alt_audio_sock);
	                        alt_audio_sock = -1;
				ast_free(p);
				return(NULL);
			}
			memset((char *) &si_me, 0, sizeof(si_me));
			si_me.sin_family = AF_INET;
	                si_me.sin_addr.s_addr = htonl(INADDR_ANY);
	 		si_me.sin_port = htons(tx_audio_port);               
			if (bind(alt_audio_sock, &si_me, sizeof(si_me))==-1) 
			{
				ast_log(LOG_WARNING, "Unable to bind port for irlp audio connection\n");
	                        close(alt_ctrl_sock); alt_ctrl_sock = -1;
	                        close(alt_audio_sock); alt_audio_sock = -1;
				ast_free(p);
				return(NULL);
			}
			si_me.sin_port = htons(tx_audio_port + 1);
			if (bind(alt_ctrl_sock, &si_me, sizeof(si_me))==-1) {
				ast_log(LOG_WARNING, "Unable to bind port for irlp control connection\n");
	                        close(alt_ctrl_sock); alt_ctrl_sock = -1;
	                        close(alt_audio_sock); alt_audio_sock = -1;
				ast_free(p);
				return(NULL);
			}
	                fcntl(alt_audio_sock,F_SETFL,O_NONBLOCK);
	                fcntl(alt_ctrl_sock,F_SETFL,O_NONBLOCK);
		}
	}
	time(&keepalive);
	return p;
}

static int irlp_hangup(struct ast_channel *ast)
{
	struct irlp_pvt *p;
	p = ast->tech_pvt;

	if (option_debug)
		ast_log(LOG_DEBUG, "irlp_hangup(%s)\n", ast->name);
	if (!ast->tech_pvt) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
	reset_stuff();
	irlp_destroy(p);
	curcall = 0;
	if (ast)
	{
		ast->tech_pvt = NULL;
		ast_setstate(ast, AST_STATE_DOWN);
	}
	ast_safe_system(IRLP_END);
	send_bye("disconnected");
	return 0;
}

static int irlp_indicate(struct ast_channel *ast, int cond, const void *data, size_t datalen)
{
	struct irlp_pvt *p = ast->tech_pvt;

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

static int irlp_text(struct ast_channel *ast, const char *text)
{
int	destnode,hisnode,seqno,i,j;
char	c;

/*	struct irlp_pvt *p = ast->tech_pvt; */

/*	if (!p->txkey) return(0); */
	if (text[0] != 'D') return 0;
	if (sscanf(text + 2,"%d %d %d %c",&destnode,&hisnode,&seqno,&c) != 4) return(0);
	if (destnode != (in_node + 40000)) return(0);
	if (c == '*') c = 'S';
	if (c == '#') c = 'P';
	if ((c == 'D') && (!irlp_dtmf_special))
	{
		irlp_dtmf_special = 1;
		return 0;
	}
	i = strlen(irlp_dtmf_string);
	j = 1;
	if (irlp_dtmf_special && (c != 'D')) j = 2;
	if (i < (sizeof(irlp_dtmf_string) - j))
	{
		irlp_dtmf_string[i + 1] = 0;
		if ((irlp_dtmf_special) && (c != 'D'))
		{
			irlp_dtmf_string[i + 2] = 0;
			irlp_dtmf_string[i] = 'P';
			irlp_dtmf_string[i + 1] = c;
		}
		else 
		{
			irlp_dtmf_string[i] = c;
		}
	}
	irlp_dtmf_special = 0;
	return 0;
}

static int irlp_digit_begin(struct ast_channel *ast, char digit)
{
	return 0;
}

static int irlp_digit_end(struct ast_channel *ast, char digit, unsigned int duration)
{
	return 0;
}

static struct ast_frame  *irlp_xread(struct ast_channel *ast)
{
	struct irlp_pvt *p = ast->tech_pvt;

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

static int irlp_xwrite(struct ast_channel *ast, struct ast_frame *frame)
{
        struct sockaddr_in sin;
	struct irlp_pvt *p = ast->tech_pvt;
	struct ast_frame fr;
	struct irlp_rxqast *qpast;
	int n,i,len,gotone,dosync,blocking_factor,frame_size,frame_samples,recvlen,flen;
        char outbuf[ULAW_FRAME_SIZE + AST_FRIENDLY_OFFSET + 3]; /* turns out that ADPCM is larger */
        struct irlp_audio irlp_audio_packet;
        static char tx_buf[(ADPCM_BLOCKING_FACTOR * ADPCM_FRAME_SIZE) + 3]; /* turns out the ADPCM is larger */
	char *outbuf_new,*cp,c,str[200];
        unsigned char *dp;
	static int lasttx = 0;

	if (ast->_state != AST_STATE_UP) 
           return 0;

	if (frame->frametype != AST_FRAME_VOICE) 
           return 0;

	if (proto == IRLP_NOPROTO) return 0;

	if ((xcount++ & 3) == 0)  /* every 80 ms */
	{
		struct ast_frame fr;

		fr.datalen = 0;
		fr.samples = 0;
		fr.frametype = AST_FRAME_DTMF_END;
		fr.subclass = havedtmf;
		fr.len = 80;
		fr.data =  0;
		fr.src = type;
		fr.offset = 0;
		fr.mallocd=0;
		fr.delivery.tv_sec = 0;
		fr.delivery.tv_usec = 0;
		if (havedtmf)
		{
			if (curcall) ast_queue_frame(curcall,&fr);
			ast_log(LOG_NOTICE,"Got DTMF %c on IRLP\n",havedtmf);
			havedtmf = 0;
		}
		else
		{
			recvlen = read(dfd,&c,1);
			if ((recvlen > 0) && strchr("0123456789SPABCD",c))
			{

				if (c == 'S') c = '*';
				if (c == 'P') c = '#';

				fr.len = 0;
				fr.subclass = c;
				fr.frametype = AST_FRAME_DTMF_BEGIN;
				if (curcall) ast_queue_frame(curcall,&fr);
				havedtmf = c;
			}
		}
	}
        /* IRLP to Asterisk */
	frame_samples = 160;
	if (proto == IRLP_ISGSM)
	{
		frame_size = GSM_FRAME_SIZE;
	}
	else if (proto == IRLP_ISULAW)
	{
		frame_size = frame_samples;
	}
	else
	{
		frame_size = ADPCM_FRAME_SIZE;
	}
	if (rxqast.qe_forw != &rxqast) {
		for(n = 0,qpast = rxqast.qe_forw; qpast != &rxqast; qpast = qpast->qe_forw) {
			n++;
		}
		if (n > QUEUE_OVERLOAD_THRESHOLD_AST) {
			while(rxqast.qe_forw != &rxqast) {
				qpast = rxqast.qe_forw;
				remque((struct qelem *)qpast);
				ast_free(qpast);
			}
			if (outbuf_old) ast_free(outbuf_old);
			rxlen = 0;
			rxidx = 0;
			if (p->rxkey) p->rxkey = 1;
		} else {		
			if (!p->rxkey) {
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
		}
	}
	outbuf_new = NULL;
	len = 0;
	gotone = 0;
	dosync = 0;
	if ((rxqast.qe_forw != &rxqast) &&
	    ((rxlen - rxidx) < frame_size))
	{
		qpast = rxqast.qe_forw;
		remque((struct qelem *)qpast);
		len = qpast->len;
		outbuf_new = malloc(len);
		if (!outbuf_new)
		{
			ast_log(LOG_ERROR,"Cannot Malloc");
			return -1;
		}
		memcpy(outbuf_new,qpast->buf,len);
		ast_free(qpast);
		if (proto == IRLP_ISADPCM) len -= 3;
		i = (rxlen - rxidx);
		if ((proto == IRLP_ISADPCM) && (!rxidx) &&
			(len >= frame_size)) dosync = len;
		/* if something to output */
		if ((len + i) >= frame_size)
		{
			if (i && outbuf_old)
			{
				memcpy(outbuf + AST_FRIENDLY_OFFSET,
					outbuf_old + rxidx,i);
				ast_free(outbuf_old);
			}
			memcpy(outbuf + i + AST_FRIENDLY_OFFSET,
				outbuf_new,frame_size - i);
			rxlen = len;
			rxidx = frame_size - i;
			outbuf_old = outbuf_new;
			gotone = 1;
		} 
		else
		{
			cp = malloc((rxlen - rxidx) + len);
			if (!cp)
			{
				ast_log(LOG_ERROR,"Cannot Malloc");
				return -1;
			}
			if (rxlen) memcpy(cp,outbuf_old + rxidx,i);
			memcpy(cp + i,outbuf_new,len);
			rxlen = len + i;
			rxidx = 0;
			ast_free(outbuf_new);
			outbuf_new = NULL;
			free(outbuf_old);
			outbuf_old = cp;
		}
	}
	else if ((rxlen - rxidx) >= frame_size)
	{
		memcpy(outbuf + AST_FRIENDLY_OFFSET,
			outbuf_old + rxidx,frame_size);
		rxidx += frame_size;
		gotone = 1;
	}
	if (gotone)
	{
 		p->rxkey = MAX_RXKEY_TIME;
		fr.datalen = frame_size;
		fr.samples = frame_samples;
		if ((proto == IRLP_ISADPCM) && dosync)
		{
			fr.datalen += 3;
			memcpy(outbuf + AST_FRIENDLY_OFFSET + frame_size,
				outbuf_new + dosync,3);
		}
		fr.frametype = AST_FRAME_VOICE;
		if (proto == IRLP_ISADPCM)
		{
			fr.subclass = AST_FORMAT_ADPCM;
		}
		else if (proto == IRLP_ISULAW)
		{
			fr.subclass = AST_FORMAT_ULAW;
		}
		else
		{
			fr.subclass = AST_FORMAT_GSM;
		}
		fr.data =  outbuf + AST_FRIENDLY_OFFSET;
		fr.src = type;
		fr.offset = AST_FRIENDLY_OFFSET;
		fr.mallocd=0;
		fr.delivery.tv_sec = 0;
		fr.delivery.tv_usec = 0;
		ast_queue_frame(ast,&fr);
	}
	if (p->rxkey == 1) {
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

		if (outbuf_old) ast_free(outbuf_old);
		rxlen = 0;
		rxidx = 0;
	} 
	if (p->rxkey) p->rxkey--;

	if (lasttx != p->txkey)
	{
		lasttx = p->txkey;
		if ((!p->txkey) && irlp_dtmf_string[0])
		{
			if (irlp_dtmf_special)
			{
				i = strlen(irlp_dtmf_string);
				if (i < (sizeof(irlp_dtmf_string) - 1))
				{
					irlp_dtmf_string[i + 1] = 0;
					irlp_dtmf_string[i] = 'P';
				}
			}
			sprintf(str,IRLP_SEND_DTMF,in_node,irlp_dtmf_string);
			ast_safe_system(str);
			ast_log(LOG_NOTICE,"Sent DTMF %s to IRLP\n",irlp_dtmf_string);
		}
		irlp_dtmf_string[0] = 0;
		irlp_dtmf_special = 0;
	}

	if (!p->txkey) 
	{
		tx_buf_n = 0;
		return(0);
	}

	flen = frame->datalen;
	/* Asterisk to IRLP */
	if (proto == IRLP_ISGSM)
	{
		blocking_factor = GSM_BLOCKING_FACTOR;
		frame_size = GSM_FRAME_SIZE;
		i = AST_FORMAT_GSM;	
	}
	else if (proto == IRLP_ISULAW)
	{
		blocking_factor = ULAW_BLOCKING_FACTOR;
		frame_size = ULAW_FRAME_SIZE;
		i = AST_FORMAT_ULAW;
	}
	else
	{
		blocking_factor = ADPCM_BLOCKING_FACTOR;
		frame_size = ADPCM_FRAME_SIZE;
		i = AST_FORMAT_ADPCM;
		flen = frame->datalen - 3;
	}
	
        if (!(frame->subclass & i)) {
             ast_log(LOG_WARNING, "Cannot handle frames in %d format\n", frame->subclass);
             return 0;
        }
	cp = frame->data;
	if ((proto == IRLP_ISADPCM) && (!tx_buf_n)) {
	    memcpy(tx_buf + (frame_size * 
		    blocking_factor),cp + frame_size,3);
	}
        memcpy(tx_buf + tx_buf_n,frame->data,flen);
	tx_buf_n += flen;
        if (tx_buf_n >= (blocking_factor * frame_size)) { 

           dp = (unsigned char *)irlp_audio_packet.buffer.buffer_val;
	   if (proto == IRLP_ISADPCM)
	   {
	           irlp_audio_packet.compression = htonl(0x200 | 0x40000000);
	   }
	   else if (proto == IRLP_ISULAW)
	   {
	           irlp_audio_packet.compression = htonl(0x40000000);
	   }
	   else
	   {
	           irlp_audio_packet.compression = htonl(0x20 | 0x40000000);
	   }

           snprintf(irlp_audio_packet.sendinghost,IRLP_IP_SIZE,
                     "stn%s-%s",mynode,mycall);

	   if (proto == IRLP_ISADPCM)
	   {
		memcpy((char *)dp,tx_buf,(blocking_factor * frame_size) + 3);
	        irlp_audio_packet.buffer.buffer_len = htonl((blocking_factor * 
			frame_size) + 3);
	   }
	   else if (proto == IRLP_ISULAW)
	   {
		memcpy((char *)dp,tx_buf,blocking_factor * frame_size);
	        irlp_audio_packet.buffer.buffer_len = htonl(blocking_factor * 
			frame_size);
	   }
	   else
	   {
		memcpy((char *)dp + 2,tx_buf,blocking_factor * frame_size);
	        irlp_audio_packet.buffer.buffer_len = htonl((blocking_factor * 
			frame_size) + 2);
                *((uint16_t *)dp) = htons(1600);
	   }

           if (remote_irlp_node_ip[0] != '\0') {
              sin.sin_family = AF_INET;
              sin.sin_addr.s_addr = inet_addr(remote_irlp_node_ip);
              sin.sin_port = htons(tx_audio_port);

	      if (proto == IRLP_ISADPCM)
	      {
                  sendto((alt_audio_sock != -1) ? alt_audio_sock : audio_sock,(char *)&irlp_audio_packet,507,
                      0,(struct sockaddr *)&sin,sizeof(struct sockaddr));
	      }
	      else if (proto == IRLP_ISULAW)
	      {
                  sendto((alt_audio_sock != -1) ? alt_audio_sock : audio_sock,(char *)&irlp_audio_packet,280,
                      0,(struct sockaddr *)&sin,sizeof(struct sockaddr));
	      }
	      else
	      {
                  sendto((alt_audio_sock != -1) ? alt_audio_sock : audio_sock,(char *)&irlp_audio_packet,356,
                      0,(struct sockaddr *)&sin,sizeof(struct sockaddr));
	      }
           }
           sin.sin_family = AF_INET;
           sin.sin_addr.s_addr = inet_addr("127.0.0.1");
           sin.sin_port = htons(localispeakerport);

	   if (proto == IRLP_ISADPCM)
	   {
               sendto(audio_sock,(char *)&irlp_audio_packet,507,
                  0,(struct sockaddr *)&sin,sizeof(struct sockaddr));
	   }
	   else if (proto == IRLP_ISULAW)
	   {
               sendto(audio_sock,(char *)&irlp_audio_packet,280,
                  0,(struct sockaddr *)&sin,sizeof(struct sockaddr));
	   }
	   else
	   {
               sendto(audio_sock,(char *)&irlp_audio_packet,356,
                  0,(struct sockaddr *)&sin,sizeof(struct sockaddr));
	   }
           tx_buf_n -= (blocking_factor * frame_size);
	   if (tx_buf_n)
	   {
		i = (blocking_factor * frame_size);
		memmove(tx_buf,tx_buf + i,tx_buf_n);
	   }
        }
	return 0;
}

static struct ast_channel *irlp_new(struct irlp_pvt *i, int state)
{
	struct ast_channel *tmp;
	char tmpstr[30];

	tmp = ast_channel_alloc(1, state, 0, 0, "", astnode, context, 0, "irlp/%s", stream);
	if (tmp) {
		tmp->tech = &irlp_tech;
		tmp->nativeformats = prefformat;
		tmp->rawreadformat = prefformat;
		tmp->rawwriteformat = prefformat;
		tmp->writeformat = prefformat;
		tmp->readformat = prefformat;
		if (state == AST_STATE_RING)
			tmp->rings = 1;
		tmp->tech_pvt = i;
		ast_copy_string(tmp->context, context, sizeof(tmp->context));
		ast_copy_string(tmp->exten, astnode,  sizeof(tmp->exten));
		ast_string_field_set(tmp, language, "");
		sprintf(tmpstr,"4%04u",(in_node) ? in_node : atoi(mynode));
		ast_set_callerid(tmp,tmpstr,NULL,NULL);
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


static struct ast_channel *irlp_request(const char *type, int format, void *data, int *cause)
{
	int oldformat;
	struct irlp_pvt *p;
	struct ast_channel *tmp = NULL;
	
	if (curcall) 
	{
		ast_log(LOG_NOTICE,"Channel is busy!\n");
		return NULL;
	}
	oldformat = format;
	format &= (AST_FORMAT_ADPCM | AST_FORMAT_GSM);
	if (!format) {
		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%d'\n", oldformat);
		return NULL;
	}
	unlink(IRLP_PLAY_FILE);
	ast_safe_system(IRLP_RESET);
	usleep(10000);
	reset_stuff();
	p = irlp_alloc(data);
	if (p) {
		tmp = irlp_new(p, AST_STATE_DOWN);
		if (!tmp)
			irlp_destroy(p);
	}
	curcall = tmp;
	return tmp;
}

static int unload_module(void)
{
	run_forever = 0;
	usleep(100000);
	if (audio_sock != -1) close(audio_sock);
	audio_sock = -1;
	if (ctrl_sock != -1) close(ctrl_sock);
	ctrl_sock = -1;
	if (alt_audio_sock != -1) close(alt_audio_sock);
	alt_audio_sock = -1;
	if (alt_ctrl_sock != -1) close(alt_ctrl_sock);
	alt_ctrl_sock = -1;
	if (nullfd != -1) close(nullfd);
	if (dfd != -1) close(dfd);
	/* First, take us out of the channel loop */
	ast_channel_unregister(&irlp_tech);
	return 0;
}

static int load_module(void)
{
	struct ast_config *cfg = NULL;
        char *val = NULL;
	pthread_attr_t attr;
	struct sockaddr_in si_me;


#ifdef  NEW_ASTERISK
        struct ast_flags zeroflag = {0};
#endif

#ifdef  NEW_ASTERISK
        if (!(cfg = ast_config_load(config,zeroflag))) {
#else
        if (!(cfg = ast_config_load(config))) {
#endif
                ast_log(LOG_NOTICE, "Unable to load config %s\n", config);
                return AST_MODULE_LOAD_DECLINE;
        }

	ast_safe_system(IRLP_WRAPPER);

        /* some of these values can be copied from /home/irlp/custom/environment */
        val = (char *)ast_variable_retrieve(cfg,"general","rtcptimeout");
        if (!val)
           rtcptimeout = 15;
        else
           rtcptimeout = atoi(val);

        val = (char *)ast_variable_retrieve(cfg,"general","call");
        if (!val)
           strncpy(mycall,"stnXXXX",IRLP_CALL_SIZE);
        else
           strncpy(mycall,val,IRLP_CALL_SIZE);

        val = (char *)ast_variable_retrieve(cfg,"general","node");
        if (!val)
           strncpy(mynode,"XXXX",IRLP_NODE_SIZE);
        else
           strncpy(mynode,val,IRLP_NODE_SIZE);

        val = (char *)ast_variable_retrieve(cfg,"general","localispeakerport");
        if (!val)
           localispeakerport = 2174;
        else
           localispeakerport = atoi(val);

        val = (char *)ast_variable_retrieve(cfg,"general","radmode");
        if (val) radmode = ast_true(val);

        val = (char *)ast_variable_retrieve(cfg,"general","audioport");
        if (!val)
	    audio_port_cfg = 2074;
        else
            audio_port_cfg = atoi(val);

        val = (char *)ast_variable_retrieve(cfg,"general","astnode");
        if (!val)
	   astnode[0] = 0;
        else
           strncpy(astnode,val,sizeof(astnode) - 1);

        val = (char *)ast_variable_retrieve(cfg,"general","context");
        if (!val)
	   context[0] = 0;
        else
           strncpy(context,val,sizeof(context) - 1);

        /* initialize local and remote IRLP node */
	reset_stuff();
	curcall = 0;

        ast_config_destroy(cfg);

	ast_safe_system(IRLP_RESET);
	usleep(10000);

	audio_port = audio_port_cfg;
	if ((audio_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1) 
	{
		ast_log(LOG_WARNING, 
                      "Unable to create new socket for irlp audio connection\n");
		return -1;
	}
		
	if ((ctrl_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1)
	{
		ast_log(LOG_WARNING, 
                        "Unable to create new socket for irlp control connection\n");
                close(audio_sock);
		return -1;
	}
	memset((char *) &si_me, 0, sizeof(si_me));
	si_me.sin_family = AF_INET;
        si_me.sin_addr.s_addr = htonl(INADDR_ANY);
	si_me.sin_port = htons(audio_port);               
	if (bind(audio_sock, &si_me, sizeof(si_me))==-1)
	{
		ast_log(LOG_WARNING, "Unable to bind port for irlp audio connection\n");
                close(ctrl_sock); ctrl_sock = -1;
                close(audio_sock); audio_sock = -1;
		return -1;
	}
        ctrl_port = audio_port + 1;
	si_me.sin_port = htons(ctrl_port);
	if (bind(ctrl_sock, &si_me, sizeof(si_me))==-1)
	{
		ast_log(LOG_WARNING, "Unable to bind port for irlp control connection\n");
                close(ctrl_sock); ctrl_sock = -1;
                close(audio_sock); audio_sock = -1;
	}
        fcntl(audio_sock,F_SETFL,O_NONBLOCK);
        fcntl(ctrl_sock,F_SETFL,O_NONBLOCK);
	nullfd = open("/dev/null",O_RDWR);

	ast_safe_system(IRLP_MAKE_FIFO);
	dfd = open(IRLP_DTMF_FIFO,O_RDONLY | O_NONBLOCK);
	if (dfd == -1)
	{
		ast_log(LOG_ERROR,"Cannot open FIFO for DTMF!!\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_safe_system(IRLP_RESET);
	unlink(IRLP_PLAY_FILE);

	usleep(100000);
        cfg = NULL; 

        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        ast_pthread_create(&irlp_reader_thread,&attr,irlp_reader,NULL);
	/* Make sure we can register our channel type */
	if (ast_channel_register(&irlp_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
                return AST_MODULE_LOAD_DECLINE;
	}

	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "irlp channel driver");


