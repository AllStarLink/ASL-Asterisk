/* #define	OLD_ASTERISK */
/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2011, Digium, Inc.
 *
 * Copyright (C) 2011
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
 * \brief Radio Voter channel driver for Asterisk
 * 
 * \author Jim Dixon, WB6NIL <jim@lambdatel.com>
 *
 * \ingroup channel_drivers
 */

/*** MODULEINFO
 ***/

/*  Basic Information On How This Works
Each node has a number of potential "clients" associated with it. In the voter.conf file, each stanza (category)
is named by the node number that the clients specified within the stanza are to be associated with. Each entry
consists of an arbitrary (realtively meaningless, just included for easy identification putposes within this
channel driver, and has nothing to do with its operation) identifier equated to a unique password. This password
is programmed into the client. All clients must have unique passwords, as that is what is used by this channel
driver to identify them.

Each channel instance (as opened by app_rpt as a main radio channel, e.g. rxchannel=Voter/1999 in rpt.conf) and
is directly associated with the node that opened it.

Each client has a pair of circular buffers, one for mu-law audio data, and one for RSSI value. The allocated buffer
length in all clients is determined by the 'buflen' parameter, which is specified in the "global" stanza in the 
voter.conf file in milliseconds, and represnted in the channel driver as number of samples (actual buffer length,
which is 8 * milliseconds). 

Every channel instance has a index ("drainindex"), indicating the next position within the physical buffer(s) where
the audio will be taken from the buffers and presented to the Asterisk channel stream as VOICE frames.

Therefore, there is an abstraction of a "buffer" that exists starting at drainindex and ending (modulo) at
drainindex - 1, with length of buflen.

Buflen is selected so that there is enough time (delay) for any straggling packets to arrive before it is time
to present the data to the Asterisk channel. 

The idea is that the current audio being presented to Asterisk is from some time shortly in the past. Therefore,
"Now" is the position in the abstratcted buffer of 'bufdelay' (generally buflen - 160) (you gotta at least leave room for
an entire frame) and the data is being presented from the start of the abstracted buffer. As the physical buffer
moves along, what was once "now" will eventually become far enough in the "past" to be presented to Asterisk (gosh,
doesn't this sound like a scene from "Spaceballs"??.. I too always drink coffee while watching "Mr. Radar").

During the processing of an audio frame to be presented to Asterisk, all client's buffers that are associated with
a channel instance (node) are examined by taking an average of the RSSI value for each sample in the associated
time period (the first 160 samples of the abstracted buffer (which is the physical buffer from drainindex to
drainindex + 159) and whichever one, if any that has the largest RSSI average greather then zero is selected
as the audio source for that frame. The corresponding audio buffer's contents (in the corresponding offsets)
are presented to Asterisk, then ALL the clients corresponding RSSI data is set to 0, ALL the clients corresponding
audio is set to quiet (0x7f). The overwriting of the buffers after their use/examination is done so that the 
next time those positions in the physical buffer are examined, they will not contain any data that was not actually
put there, since all client's buffers are significant regardless of whether they were populated or not. This
allows for the true 'connectionless-ness' of this protocol implementation.


Voter Channel test modes:

0 - Normal voting operation
1 - Randomly pick which client of all that
    are receving at the max rssi value to use.
> 1 - Cycle thru all the clients that are receiving
    at the max rssi value with a cycle time of (test mode - 1)
    frames. In other words, if you set it to 2, it will
    change every single time. If you set it to 11, it will
    change every 10 times. This is serious torture test.
< 1 - Explicitly select the client to use. Specify the
    negative value of the client in the list in voter.conf
    (reversed). In other words, if you want the last client
    you specified in the list for the particular node, set
    the value to -1. If you want the 3rd from the last, set
    it to -4.
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
#include <sys/timex.h>
#include <ctype.h>
#include <search.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <fnmatch.h>
#include <math.h>
#include <zaptel/zaptel.h>

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
#include "asterisk/astdb.h"
#include "asterisk/cli.h"


#ifdef	OLD_ASTERISK
#define	AST_MODULE_LOAD_DECLINE -1
#endif

#define	VOTER_CHALLENGE_LEN 10
#define	VOTER_NAME_LEN 50

#define	RX_TIMEOUT_MS 200

#define	DELIMCHR ','
#define	QUOTECHR 34

static const char vdesc[] = "radio Voter channel driver";
static char type[] = "voter";

int run_forever = 1;
static int nullfd = -1;

AST_MUTEX_DEFINE_STATIC(voter_lock);

int16_t listen_port = 667;				/* port to listen to UDP packets on */
int udp_socket = -1;

int voter_timing_fd = -1;

char challenge[VOTER_CHALLENGE_LEN];
char password[100];
char context[100];

double dnsec;

#define	FRAME_SIZE 160

#define	DEFAULT_BUFLEN 500 /* 500ms default buffer len */
#define	DEFAULT_BUFDELAY ((DEFAULT_BUFLEN * 8) - FRAME_SIZE)

#pragma pack(push)
#pragma pack(1)
typedef struct {
	uint32_t vtime_sec;
	uint32_t vtime_nsec;
} VTIME;

typedef struct {
	VTIME curtime;
	uint8_t challenge[10];
	uint32_t digest;
	uint16_t payload_type;
} VOTER_PACKET_HEADER;

typedef struct {
	char lat[9];
	char lon[10];
	char elev[6];
} VOTER_GPS;

typedef struct {
	char name[32];
	uint8_t audio[FRAME_SIZE];
	uint8_t rssi;
} VOTER_REC;	
#pragma pack(pop)

#define VOTER_PAYLOAD_NONE	0
#define VOTER_PAYLOAD_ULAW	1
#define	VOTER_PAYLOAD_GPS	2

struct voter_pvt {
	struct ast_channel *owner;
	unsigned int nodenum;				/* node # associated with instance */
	struct voter_pvt *next;
	struct ast_frame fr;
	char buf[FRAME_SIZE + AST_FRIENDLY_OFFSET];
	char txkey;
	char rxkey;
	struct ast_module_user *u;
	struct timeval lastrxtime;
	int drainindex;
	char drained_once;
	int testcycle;
	int testindex;
#ifdef 	OLD_ASTERISK
	AST_LIST_HEAD(, ast_frame) txq;
#else
	AST_LIST_HEAD_NOLOCK(, ast_frame) txq;
#endif
	ast_mutex_t  txqlock;
};

struct voter_client {
	uint32_t nodenum;
	uint32_t digest;
	char name[VOTER_NAME_LEN];
	uint8_t *audio;
	uint8_t *rssi;
	uint32_t respdigest;
	struct sockaddr_in sin;
	char heardfrom;
	char totransmit;
	char ismaster;
	struct voter_client *next;
	long long dtime;
	uint8_t lastrssi;
} ;

#ifdef	OLD_ASTERISK
static int usecnt;
AST_MUTEX_DEFINE_STATIC(usecnt_lock);
#endif

int debug = 0;
int voter_test = 0;
FILE *recfp = NULL;

unsigned int buflen = DEFAULT_BUFLEN * 8;

unsigned int bufdelay = DEFAULT_BUFDELAY;

static char *config = "voter.conf";

struct ast_config *cfg = NULL;

struct voter_pvt *pvts = NULL;

struct voter_client *clients = NULL;

FILE *fp;

VTIME master_time = {0,0};


#ifdef OLD_ASTERISK
#define ast_free free
#define ast_malloc malloc
#endif

static struct ast_channel *voter_request(const char *type, int format, void *data, int *cause);
static int voter_call(struct ast_channel *ast, char *dest, int timeout);
static int voter_hangup(struct ast_channel *ast);
static struct ast_frame *voter_read(struct ast_channel *ast);
static int voter_write(struct ast_channel *ast, struct ast_frame *frame);
#ifdef	OLD_ASTERISK
static int voter_indicate(struct ast_channel *ast, int cond);
static int voter_digit_end(struct ast_channel *c, char digit);
#else
static int voter_indicate(struct ast_channel *ast, int cond, const void *data, size_t datalen);
static int voter_digit_begin(struct ast_channel *c, char digit);
static int voter_digit_end(struct ast_channel *c, char digit, unsigned int duratiion);
#endif
static int voter_text(struct ast_channel *c, const char *text);

static const struct ast_channel_tech voter_tech = {
	.type = type,
	.description = vdesc,
	.capabilities = AST_FORMAT_ULAW,
	.requester = voter_request,
	.call = voter_call,
	.hangup = voter_hangup,
	.read = voter_read,
	.write = voter_write,
	.indicate = voter_indicate,
	.send_text = voter_text,
#ifdef	OLD_ASTERISK
	.send_digit = voter_digit_end,
#else
	.send_digit_begin = voter_digit_begin,
	.send_digit_end = voter_digit_end,
#endif
};

/*
* CLI extensions
*/

/* Debug mode */
static int voter_do_debug(int fd, int argc, char *argv[]);

static char debug_usage[] =
"Usage: voter debug level {0-7}\n"
"       Enables debug messages in chan_voter\n";


/* Test */
static int voter_do_test(int fd, int argc, char *argv[]);

static char test_usage[] =
"Usage: voter test {test value}\n"
"       Specifies test mode for voter chan_voter\n";


/* Record */
static int voter_do_record(int fd, int argc, char *argv[]);

static char record_usage[] =
"Usage: voter record [record filename]\n"
"       Enables/Specifies (or disables) recording file for chan_voter\n";



#ifndef	NEW_ASTERISK

static struct ast_cli_entry  cli_debug =
        { { "voter", "debug", "level" }, voter_do_debug, 
		"Enable voter debugging", debug_usage };
static struct ast_cli_entry  cli_test =
        { { "voter", "test" }, voter_do_test, 
		"Specify voter test value", test_usage };
static struct ast_cli_entry  cli_record =
        { { "voter", "record" }, voter_do_record, 
		"Enables/Specifies (or disables) voter recording file", record_usage };

#endif

static uint32_t crc_32_tab[] = { /* CRC polynomial 0xedb88320 */
0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

static int32_t crc32_bufs(char *buf, char *buf1)
{
        int32_t oldcrc32;

        oldcrc32 = 0xFFFFFFFF;
        while(buf && *buf)
        {
                oldcrc32 = crc_32_tab[(oldcrc32 ^ *buf++) & 0xff] ^ ((unsigned long)oldcrc32 >> 8);
        }
        while(buf1 && *buf1)
        {
                oldcrc32 = crc_32_tab[(oldcrc32 ^ *buf1++) & 0xff] ^ ((unsigned long)oldcrc32 >> 8);
        }
        return ~oldcrc32;
}

#if 0
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
#endif

static int voter_call(struct ast_channel *ast, char *dest, int timeout)
{
	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "voter_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}
	/* When we call, it just works, really, there's no destination...  Just
	   ring the phone and wait for someone to answer */
	if (option_debug)
		ast_log(LOG_DEBUG, "Calling %s on %s\n", dest, ast->name);
	ast_setstate(ast,AST_STATE_UP);
	return 0;
}

static int voter_hangup(struct ast_channel *ast)
{
	struct voter_pvt *p = ast->tech_pvt,*q;

	if (option_debug)
		ast_log(LOG_DEBUG, "voter_hangup(%s)\n", ast->name);
	if (!ast->tech_pvt) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
	ast_mutex_lock(&voter_lock);
	for(q = pvts; q; q = q->next)
	{
		if (q->next == p) break;
	}
	if (q) q->next = p->next;
	else pvts = NULL;
	ast_mutex_unlock(&voter_lock);
	ast_free(p);
	ast->tech_pvt = NULL;
	ast_setstate(ast, AST_STATE_DOWN);
	return 0;
}

#ifdef	OLD_ASTERISK
static int voter_indicate(struct ast_channel *ast, int cond)
#else
static int voter_indicate(struct ast_channel *ast, int cond, const void *data, size_t datalen)
#endif
{
	struct voter_pvt *p = ast->tech_pvt;

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

#ifndef	OLD_ASTERISK

static int voter_digit_begin(struct ast_channel *ast, char digit)
{
	return -1;
}

#endif

#ifdef	OLD_ASTERISK
static int voter_digit_end(struct ast_channel *ast, char digit)
#else
static int voter_digit_end(struct ast_channel *ast, char digit, unsigned int duration)
#endif
{
	return(0);
}

static int voter_text(struct ast_channel *ast, const char *text)
{
	return 0;
}


static struct ast_frame *voter_read(struct ast_channel *ast)
{
	struct voter_pvt *p = ast->tech_pvt;

	memset(&p->fr,0,sizeof(struct ast_frame));
        p->fr.frametype = AST_FRAME_NULL;
        return &p->fr;
}

static int voter_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct voter_pvt *p = ast->tech_pvt;
	struct ast_frame *f1;

	if (frame->frametype != AST_FRAME_VOICE) return 0;

	if (!p->txkey) return 0;

	if (fp != NULL) fwrite(frame->data,1,frame->datalen,fp);
	f1 = ast_frdup(frame);
	memset(&f1->frame_list,0,sizeof(f1->frame_list));
	ast_mutex_lock(&p->txqlock);
	AST_LIST_INSERT_TAIL(&p->txq,f1,frame_list);
	ast_mutex_unlock(&p->txqlock);

	return 0;
}



static struct ast_channel *voter_request(const char *type, int format, void *data, int *cause)
{
	int oldformat;
	struct voter_pvt *p;
	struct ast_channel *tmp = NULL;
	
	oldformat = format;
	format &= AST_FORMAT_ULAW;
	if (!format) {
		ast_log(LOG_ERROR, "Asked to get a channel of unsupported format '%d'\n", oldformat);
		return NULL;
	}
	p = ast_malloc(sizeof(struct voter_pvt));
	if (!p)
	{
		ast_log(LOG_ERROR,"Cant malloc() for voter structure!\n");
		return NULL;
	}
	memset(p, 0, sizeof(struct voter_pvt));
	p->nodenum = strtoul((char *)data,NULL,0);
	ast_mutex_init(&p->txqlock);
#ifdef	OLD_ASTERISK
	tmp = ast_channel_alloc(1);
	if (!tmp)
	{
		ast_log(LOG_ERROR,"Cant alloc new asterisk channel\n");
		ast_free(p);
		return NULL;
	}
	ast_setstate(tmp,AST_STATE_DOWN);
	ast_copy_string(tmp->context, context, sizeof(tmp->context));
	ast_copy_string(tmp->exten, (char *)data, sizeof(tmp->exten));
	snprintf(tmp->name, sizeof(tmp->name), "voter/%s", (char *)data);
#else
	tmp = ast_channel_alloc(1, AST_STATE_DOWN, 0, 0, "", (char *)data, context, 0, "voter/%s", (char *)data);
	if (!tmp)
	{
		ast_log(LOG_ERROR,"Cant alloc new asterisk channel\n");
		ast_free(p);
		return NULL;
	}
#endif
	ast_mutex_lock(&voter_lock);
	if (pvts != NULL) p->next = pvts;
	pvts = p;
	ast_mutex_unlock(&voter_lock);
	tmp->tech = &voter_tech;
	tmp->rawwriteformat = AST_FORMAT_ULAW;
	tmp->writeformat = AST_FORMAT_ULAW;
	tmp->rawreadformat = AST_FORMAT_ULAW;
	tmp->readformat = AST_FORMAT_ULAW;
	tmp->nativeformats = AST_FORMAT_ULAW;
//	if (state == AST_STATE_RING) tmp->rings = 1;
	tmp->tech_pvt = p;
#ifdef	OLD_ASTERISK
	ast_copy_string(tmp->language, "", sizeof(tmp->language));
#else
	ast_string_field_set(tmp, language, "");
#endif
	p->owner = tmp;
#ifdef	OLD_ASTERISK
	ast_mutex_lock(&usecnt_lock);
	usecnt++;
	ast_mutex_unlock(&usecnt_lock);
	ast_update_use_count();
#else
	p->u = ast_module_user_add(tmp);
#endif
	return tmp;
}

/*
* Enable or disable debug output at a given level at the console
*/
                                                                                                                                 
static int voter_do_debug(int fd, int argc, char *argv[])
{
	int newlevel;

        if (argc != 4)
                return RESULT_SHOWUSAGE;
        newlevel = atoi(argv[3]);
        if((newlevel < 0) || (newlevel > 7))
                return RESULT_SHOWUSAGE;
        if(newlevel)
                ast_cli(fd, "voter Debugging enabled, previous level: %d, new level: %d\n", debug, newlevel);
        else
                ast_cli(fd, "voter Debugging disabled\n");

        debug = newlevel;                                                                                                                          
        return RESULT_SUCCESS;
}

static int voter_do_test(int fd, int argc, char *argv[])
{
	int newlevel;

	if (argc == 2)
	{
		if (voter_test)
			ast_cli(fd,"voter Test: currently set to %d\n",voter_test);
		else
			ast_cli(fd,"voter Test: currently disabled\n");
		return RESULT_SUCCESS;
	}		
        if (argc != 3)
                return RESULT_SHOWUSAGE;
        newlevel = atoi(argv[2]);
        if(newlevel)
                ast_cli(fd, "voter Test: previous level: %d, new level: %d\n", voter_test, newlevel);
        else
                ast_cli(fd, "voter Test disabled\n");

        voter_test = newlevel;                                                                                                                          
        return RESULT_SUCCESS;
}

static int voter_do_record(int fd, int argc, char *argv[])
{
	if (argc == 2)
	{
		if (recfp) fclose(recfp);
		recfp = NULL;
		ast_cli(fd,"voter recording disabled\n");
		return RESULT_SUCCESS;
	}		
        if (argc != 3)
                return RESULT_SHOWUSAGE;
	recfp = fopen(argv[2],"w");
	if (!recfp)
	{
		ast_cli(fd,"voter Record: Could not open file %s\n",argv[2]);
		return RESULT_SUCCESS;
	}
        ast_cli(fd, "voter Record: Recording enabled info file %s\n",argv[2]);
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
                e->command = "voter debug level";
                e->usage = debug_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(voter_do_debug(a->fd,a->argc,a->argv));
}

static char *handle_cli_test(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "voter test";
                e->usage = test_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(voter_do_test(a->fd,a->argc,a->argv));
}

static char *handle_cli_record(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "voter record";
                e->usage = record_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(voter_do_record(a->fd,a->argc,a->argv));
}

static struct ast_cli_entry rpt_cli[] = {
	AST_CLI_DEFINE(handle_cli_debug,"Enable voter debugging"),
	AST_CLI_DEFINE(handle_cli_test,"Specify voter test value"),
	AST_CLI_DEFINE(handle_cli_record,"Enable/Specify (or disable) voter recording file"),
} ;

#endif

#ifndef	OLD_ASTERISK
static
#endif
int unload_module(void)
{
        run_forever = 0;

#ifdef	NEW_ASTERISK
	ast_cli_unregister_multiple(voter_cli,sizeof(voter_cli) / 
		sizeof(struct ast_cli_entry));
#else
	/* Unregister cli extensions */
	ast_cli_unregister(&cli_debug);
	ast_cli_unregister(&cli_test);
	ast_cli_unregister(&cli_record);
#endif
	/* First, take us out of the channel loop */
	ast_channel_unregister(&voter_tech);
	if (nullfd != -1) close(nullfd);
	return 0;
}

static void *voter_reader(void *data)
{
 	char buf[4096],timestr[100],hasmastered;
	struct sockaddr_in sin;
	struct voter_pvt *p;
	int i,j,k,n,maxrssi;
	struct ast_frame *f1,fr;
        socklen_t fromlen;
	ssize_t recvlen;
	struct timeval tmout,tv,timetv;
	fd_set fds;
	struct voter_client *client,*maxclient;
	VOTER_PACKET_HEADER *vph;
	VOTER_GPS *vgp;
	VOTER_REC rec;
	time_t timestuff;
#pragma pack(push)
#pragma pack(1)
	struct {
		VOTER_PACKET_HEADER vp;
		char rssi;
		char audio[FRAME_SIZE];
	} audiopacket;
#pragma pack(pop)

	struct {
		VOTER_PACKET_HEADER vp;
		char flags;
	} authpacket;

	if (option_verbose > 2) ast_verbose(VERBOSE_PREFIX_3 "voter: reader thread started.\n");
	ast_mutex_lock(&voter_lock);
	while(run_forever && (!ast_shutting_down()))
	{
		ast_mutex_unlock(&voter_lock);
		FD_ZERO(&fds);
		FD_SET(udp_socket,&fds);
		tmout.tv_sec = 0;
		tmout.tv_usec = 50000;
		i = select(udp_socket + 1,&fds,NULL,NULL,&tmout);
		ast_mutex_lock(&voter_lock);
		if (i < 0)
		{
			ast_mutex_unlock(&voter_lock);
			ast_log(LOG_ERROR,"Error in select()\n");
			pthread_exit(NULL);
		}
		gettimeofday(&tv,NULL);
		for(p = pvts; p; p = p->next)
		{
			if (!p->rxkey) continue;
			if (ast_tvdiff_ms(tv,p->lastrxtime) > RX_TIMEOUT_MS)
			{
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
				ast_queue_frame(p->owner,&fr);
				p->rxkey = 0;
			}
		}
		if (i == 0) continue;
		if (FD_ISSET(udp_socket,&fds)) /* if we get a packet */
		{
			fromlen = sizeof(struct sockaddr_in);
			recvlen = recvfrom(udp_socket,buf,sizeof(buf) - 1,0,
				(struct sockaddr *)&sin,&fromlen);
			if (recvlen >= sizeof(VOTER_PACKET_HEADER)) /* if set got something worthwile */
			{
				vph = (VOTER_PACKET_HEADER *)buf;
				if (debug > 2) ast_verbose("Got rx packet, len %d payload %d challenge %s digest %08x\n",recvlen,ntohs(vph->payload_type),vph->challenge,ntohl(vph->digest));
				client = NULL;
				if (vph->digest)
				{
					
					for(client = clients; client; client = client->next)
					{
						if (client->digest == htonl(vph->digest)) break;
					}

					if ((debug >= 2) && client && (!client->ismaster) && ntohs(vph->payload_type) == VOTER_PAYLOAD_ULAW)
					{
						timestuff = (time_t) ntohl(vph->curtime.vtime_sec);
						strftime(timestr,sizeof(timestr) - 1,"%D %T",localtime((time_t *)&timestuff));
						ast_verbose("Time:      %s.%03d, RSSI: %d\n",timestr,ntohl(vph->curtime.vtime_nsec) / 1000000,(unsigned char)*(buf + sizeof(VOTER_PACKET_HEADER)));
					}
					if (client)
					{
						client->respdigest = crc32_bufs((char*)vph->challenge,password);
						client->sin = sin;
					}
					/* if we know the dude, find the connection his audio belongs to and send it there */
					if (client && client->heardfrom  && (ntohs(vph->payload_type) == VOTER_PAYLOAD_ULAW) && 
						(recvlen == (sizeof(VOTER_PACKET_HEADER) + FRAME_SIZE + 1)))
					{
						for(p = pvts; p; p = p->next)
						{
							if (p->nodenum == client->nodenum) break;
						}
						if (p) /* if we found 'em */
						{
							long long btime,ptime,difftime;
							int index,mdiff,mmax;
							struct timeval mtv;


							if (client->ismaster)
							{
								master_time.vtime_sec = ntohl(vph->curtime.vtime_sec);
								master_time.vtime_nsec = ntohl(vph->curtime.vtime_nsec);

							}
							else
							{
								if (!master_time.vtime_sec) continue;
								gettimeofday(&tv,NULL);
								mtv.tv_sec = master_time.vtime_sec;
								mtv.tv_usec = master_time.vtime_nsec / 1000;
								if ((mtv.tv_sec <= (tv.tv_sec - 2)) || (mtv.tv_sec >= (tv.tv_sec + 2))) continue;
								mdiff = ast_tvdiff_ms(tv,mtv);
								mmax = buflen / 8;
								if ((mdiff > mmax) || (mdiff < -mmax)) continue;
							}
							btime = ((long long)master_time.vtime_sec * 1000000000LL) + master_time.vtime_nsec;
							btime += 40000000;
							ptime = ((long long)ntohl(vph->curtime.vtime_sec) * 1000000000LL) + ntohl(vph->curtime.vtime_nsec);
							difftime = (ptime - btime) + (bufdelay * 125000LL);
							index = (int)((long long)difftime / 125000LL);
							if ((debug >= 2) && (!client->ismaster))
							{
								timestuff = (time_t) master_time.vtime_sec;
								strftime(timestr,sizeof(timestr) - 1,"%D %T",localtime((time_t *)&timestuff));
								ast_verbose("DrainTime: %s.%03d\n",timestr,master_time.vtime_nsec / 1000000);
								gettimeofday(&timetv,NULL);
								timestuff = (time_t) timetv.tv_sec;
								strftime(timestr,sizeof(timestr) - 1,"%D %T",localtime((time_t *)&timestuff));
								ast_verbose("SysTime:   %s.%03d, diff: %lld,index: %d\n",timestr,(int)timetv.tv_usec / 1000,btime - ptime,index);
							}
							/* if in bounds */
							if ((index > 0) && (index < (buflen - FRAME_SIZE)))
							{
								index = (index + p->drainindex) % buflen;
								i = (int)buflen - (index + FRAME_SIZE);
								if (i >= 0)
								{
									memcpy(client->audio + index,
										buf + sizeof(VOTER_PACKET_HEADER) + 1,FRAME_SIZE);
									memset(client->rssi + index,buf[sizeof(VOTER_PACKET_HEADER)],FRAME_SIZE);
								}
								else
								{
									memcpy(client->audio + index,
										buf + sizeof(VOTER_PACKET_HEADER) + 1,FRAME_SIZE + i);
									memset(client->rssi + index,buf[sizeof(VOTER_PACKET_HEADER)],FRAME_SIZE + i);
									memcpy(client->audio,
										buf + sizeof(VOTER_PACKET_HEADER) + 1 + (FRAME_SIZE + i),-i);
									memset(client->rssi,buf[sizeof(VOTER_PACKET_HEADER)],-i);
								}
							}
							if (client->ismaster)
							{
								hasmastered = 0;
								for(p = pvts; p; p = p->next)
								{
									if (!p->drained_once)
									{
										p->drained_once = 1;
										continue;
									}
									ast_mutex_lock(&voter_lock);
									n = 0;
									ast_mutex_lock(&p->txqlock);
									AST_LIST_TRAVERSE(&p->txq, f1,frame_list) n++;
									ast_mutex_unlock(&p->txqlock);
									if (n && ((n > 3) || (!p->txkey)))
									{
										ast_mutex_lock(&p->txqlock);
										f1 = AST_LIST_REMOVE_HEAD(&p->txq,frame_list);
										ast_mutex_unlock(&p->txqlock);
										memset(&audiopacket,0,sizeof(audiopacket));
										strcpy((char *)audiopacket.vp.challenge,challenge);
										audiopacket.vp.payload_type = htons(1);
										audiopacket.rssi = 0;
										memcpy(audiopacket.audio,f1->data,FRAME_SIZE);
										ast_free(f1);
										audiopacket.vp.curtime.vtime_sec = htonl(master_time.vtime_sec);
										audiopacket.vp.curtime.vtime_nsec = htonl(master_time.vtime_nsec);
										for(client = clients; client; client = client->next)
										{
											if (client->nodenum != p->nodenum) continue;
											if (!client->respdigest) continue;
											audiopacket.vp.digest = htonl(client->respdigest);
											if (client->totransmit)
											{
												if (debug > 0) ast_verbose("sending audio packet to client %s digest %08x\n",client->name,client->respdigest);
												/* send em the empty packet to get things started */
												sendto(udp_socket, &audiopacket, sizeof(audiopacket),0,(struct sockaddr *)&client->sin,sizeof(client->sin));
											}
										}
									}
									maxrssi = 0;
									maxclient = NULL;
									for(client = clients; client; client = client->next)
									{
										if (client->nodenum != p->nodenum) continue;
										k = 0;
										i = (int)buflen - ((int)p->drainindex + FRAME_SIZE);
										if (i >= 0)
										{
											for(j = p->drainindex; j < p->drainindex + FRAME_SIZE; j++)
											{
												k += client->rssi[j];
												client->rssi[j] = 0;
											}
										}
										else
										{
											for(j = p->drainindex; j < p->drainindex + (FRAME_SIZE + i); j++)
											{
												k += client->rssi[j];
												client->rssi[j] = 0;
											}
											for(j = 0; j < -i; j++)
											{
												k += client->rssi[j];
												client->rssi[j] = 0;
											}
										}			
										client->lastrssi = k / FRAME_SIZE; 
										if (client->lastrssi > maxrssi)
										{
											maxrssi =  client->lastrssi;
											maxclient = client;
										}
									}
									if (!maxclient) /* if nothing there */
									{
										memset(&fr,0,sizeof(struct ast_frame));
									        fr.frametype = 0;
									        fr.subclass = 0;
									        fr.datalen = 0;
									        fr.samples = 0;
									        fr.data =  NULL;
									        fr.src = type;
									        fr.offset = 0;
									        fr.mallocd=0;
									        fr.delivery.tv_sec = 0;
									        fr.delivery.tv_usec = 0;
										p->drainindex += FRAME_SIZE;
										if (p->drainindex >= buflen) p->drainindex -= buflen;
										ast_mutex_unlock(&voter_lock);
										ast_queue_frame(p->owner,&fr);
										continue;
									}
									/* if we are in test mode, we need to artifically affect the vote outcome */
									if (voter_test < 0) /* force explicit selection */
									{
										p->testcycle = 0;
										p->testindex = 0;
										for(i = 0,client = clients; client; client = client->next)
										{
											if (client->nodenum != p->nodenum) continue;
											maxclient = 0;
											i++;
											if (i == -voter_test)
											{
												if (client->lastrssi)
												{
													maxclient = client;
													maxrssi = client->lastrssi;
												}												
												break;
											}
										}
									}
									else if (voter_test > 0) /* perform cyclic selection */
									{
										/* see how many are eligible */
										for(i = 0,client = clients; client; client = client->next)
										{
//if (client->ismaster) continue;
											if (client->nodenum != p->nodenum) continue;
											if (client->lastrssi == maxrssi) i++;
										}
										if (voter_test == 1)
										{
											p->testindex = random() % i;
										}
										else
										{
											p->testcycle++;
											if (p->testcycle >= (voter_test - 1))
											{
												p->testcycle = 0;
												p->testindex++;
												if (p->testindex >= i) p->testindex = 0;
											}
										}
										for(i = 0,client = clients; client; client = client->next)
										{
											if (client->nodenum != p->nodenum) continue;
											if (client->lastrssi != maxrssi) continue;
//if (client->ismaster) continue;
											if (i++ == p->testindex)
											{
												maxclient = client;
												maxrssi = client->lastrssi;
												break;
											}
										}
									}
									else
									{
										p->testcycle = 0;
										p->testindex = 0;
									}
									if (!maxclient) /* if nothing there */
									{
										memset(&fr,0,sizeof(struct ast_frame));
									        fr.frametype = 0;
									        fr.subclass = 0;
									        fr.datalen = 0;
									        fr.samples = 0;
									        fr.data =  NULL;
									        fr.src = type;
									        fr.offset = 0;
									        fr.mallocd=0;
									        fr.delivery.tv_sec = 0;
									        fr.delivery.tv_usec = 0;
										p->drainindex += FRAME_SIZE;
										if (p->drainindex >= buflen) p->drainindex -= buflen;
										ast_mutex_unlock(&voter_lock);
										ast_queue_frame(p->owner,&fr);
										continue;
									}
									i = (int)buflen - ((int)p->drainindex + FRAME_SIZE);
									if (i >= 0)
									{
										memcpy(p->buf + AST_FRIENDLY_OFFSET,maxclient->audio + p->drainindex,FRAME_SIZE);
									}
									else
									{
										memcpy(p->buf + AST_FRIENDLY_OFFSET,maxclient->audio + p->drainindex,FRAME_SIZE + i);
										memcpy(p->buf + AST_FRIENDLY_OFFSET + (buflen - i),maxclient->audio,-i);
									}
									for(client = clients; client; client = client->next)
									{
										if (client->nodenum != p->nodenum) continue;
										if (recfp)
										{
											if (!hasmastered)
											{
												hasmastered = 1;
												memset(&rec,0,sizeof(rec));
												memcpy(rec.audio,&master_time,sizeof(master_time));
												fwrite(&rec,1,sizeof(rec),recfp);
											}
											ast_copy_string(rec.name,client->name,sizeof(rec.name) - 1);
											rec.rssi = client->lastrssi;
											if (i >= 0)
											{
												memcpy(rec.audio,client->audio + p->drainindex,FRAME_SIZE);
											}
											else

											{
												memcpy(rec.audio,client->audio + p->drainindex,FRAME_SIZE + i);												memset(client->audio + p->drainindex,0xff,FRAME_SIZE + i);
												memcpy(rec.audio + FRAME_SIZE + i,client->audio,-i);												memset(client->audio + p->drainindex,0xff,FRAME_SIZE + i);

											}
											fwrite(&rec,1,sizeof(rec),recfp);
										}
										if (i >= 0)
										{
											memset(client->audio + p->drainindex,0xff,FRAME_SIZE);
										}
										else
										{
											memset(client->audio + p->drainindex,0xff,FRAME_SIZE + i);
											memset(client->audio,0xff,-i);
										}
									}
									if (debug > 0) ast_verbose("Sending from client %s RSSI %d\n",maxclient->name,maxrssi);
									p->drainindex += FRAME_SIZE;
									if (p->drainindex >= buflen) p->drainindex -= buflen;
									gettimeofday(&p->lastrxtime,NULL);
									ast_mutex_unlock(&voter_lock);
									if (!p->rxkey)
									{
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
										ast_queue_frame(p->owner,&fr);
									}
									p->rxkey = 1;
									memset(&fr,0,sizeof(struct ast_frame));
								        fr.frametype = AST_FRAME_VOICE;
								        fr.subclass = AST_FORMAT_ULAW;
								        fr.datalen = FRAME_SIZE;
								        fr.samples = FRAME_SIZE;
								        fr.data =  p->buf + AST_FRIENDLY_OFFSET;
								        fr.src = type;
								        fr.offset = AST_FRIENDLY_OFFSET;
								        fr.mallocd = 0;
								        fr.delivery.tv_sec = 0;
								        fr.delivery.tv_usec = 0;
									ast_queue_frame(p->owner,&fr);
								}
							}
						}
						else
						{
							if (debug > 0) ast_verbose("Request for voter client %s to unknown node %d\n",
								client->name,client->nodenum);
						}
						continue;
					} 
					/* if we know the dude, find the connection his audio belongs to and send it there */
					if (client && client->heardfrom && (ntohs(vph->payload_type) == VOTER_PAYLOAD_GPS) && 
							(recvlen == (sizeof(VOTER_PACKET_HEADER) + sizeof(VOTER_GPS))))
					{
						if ((debug >= 2) /*&& (!client->ismaster)*/)
						{
							gettimeofday(&timetv,NULL);
							timestuff = (time_t) ntohl(vph->curtime.vtime_sec);
							strftime(timestr,sizeof(timestr) - 1,"%D %T",localtime((time_t *)&timestuff));
	
							ast_verbose("GPSTime:   %s.%09d\n",timestr,ntohl(vph->curtime.vtime_nsec));
							timetv.tv_usec = ((timetv.tv_usec + 10000) / 20000) * 20000;
							if (timetv.tv_usec >= 1000000)
							{
								timetv.tv_sec++;
								timetv.tv_usec -= 1000000;
							}
							timestuff = (time_t) timetv.tv_sec;
							strftime(timestr,sizeof(timestr) - 1,"%D %T",localtime((time_t *)&timestuff));
							ast_verbose("SysTime:   %s.%06d\n",timestr,(int)timetv.tv_usec);
							timestuff = (time_t) master_time.vtime_sec;
							strftime(timestr,sizeof(timestr) - 1,"%D %T",localtime((time_t *)&timestuff));
							ast_verbose("DrainTime: %s.%03d\n",timestr,master_time.vtime_nsec / 1000000);
						}
						vgp = (VOTER_GPS *)(buf + sizeof(VOTER_PACKET_HEADER));
						if (debug > 0) ast_verbose("Got GPS (%s): Lat: %s, Lon: %s, Elev: %s\n",
							client->name,vgp->lat,vgp->lon,vgp->elev);
						continue;
					}
					if (client) client->heardfrom = 1;
				}
				/* otherwise, we just need to send an empty packet to the dude */
				memset(&authpacket,0,sizeof(authpacket));
				strcpy((char *)authpacket.vp.challenge,challenge);
				gettimeofday(&tv,NULL);
				authpacket.vp.curtime.vtime_sec = htonl(tv.tv_sec);
				authpacket.vp.curtime.vtime_nsec = htonl(tv.tv_usec * 1000);
				/* make our digest based on their challenge */
				authpacket.vp.digest = htonl(crc32_bufs((char*)vph->challenge,password));
				authpacket.flags = 0;
				if (client && client->ismaster) authpacket.flags |= 2 | 8;
				if (debug > 0) ast_verbose("sending packet challenge %s digest %08x password %s\n",authpacket.vp.challenge,ntohl(authpacket.vp.digest),password);
				/* send em the empty packet to get things started */
				sendto(udp_socket, &authpacket, sizeof(authpacket),0,(struct sockaddr *)&sin,sizeof(sin));
				continue;
			}
		}
	}
	ast_mutex_unlock(&voter_lock);
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "voter: read thread exited.\n");
	return NULL;
}

#ifndef	OLD_ASTERISK
static
#endif
int load_module(void)
{

#ifdef  NEW_ASTERISK
        struct ast_flags zeroflag = {0};
#endif
	struct sockaddr_in sin;
	char *val,*ctg;
	pthread_attr_t attr;
	pthread_t voter_reader_thread;
	unsigned int mynode;
	int bs;
	struct voter_client *client,*client1;
	struct ast_variable *v;

#ifdef  NEW_ASTERISK
        if (!(cfg = ast_config_load(config,zeroflag))) {
#else
        if (!(cfg = ast_config_load(config))) {
#endif
                ast_log(LOG_ERROR, "Unable to load config %s\n", config);
                return AST_MODULE_LOAD_DECLINE;
        }

        val = (char *) ast_variable_retrieve(cfg,"general","password"); 
	if (val) ast_copy_string(password,val,sizeof(password) - 1);

        val = (char *) ast_variable_retrieve(cfg,"general","context"); 
	if (val) ast_copy_string(context,val,sizeof(context) - 1);

        val = (char *) ast_variable_retrieve(cfg,"general","port"); 
	if (val) listen_port = (uint16_t) strtoul(val,NULL,0);

        val = (char *) ast_variable_retrieve(cfg,"general","buflen"); 
	if (val) buflen = strtoul(val,NULL,0) * 8;

        val = (char *) ast_variable_retrieve(cfg,"general","bufdelay"); 
	if (val) bufdelay = strtoul(val,NULL,0) * 8;

	if (buflen < (FRAME_SIZE * 2)) buflen = FRAME_SIZE * 2;

	if (bufdelay > (buflen - FRAME_SIZE)) bufdelay = buflen - FRAME_SIZE;

	snprintf(challenge, sizeof(challenge), "%ld", ast_random());

	if ((udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	{
		ast_log(LOG_ERROR,"Unable to create new socket for voter audio connection\n");
		ast_config_destroy(cfg);
                return AST_MODULE_LOAD_DECLINE;
	}
	memset((char *) &sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
        val = (char *) ast_variable_retrieve(cfg,"general","bindaddr"); 
	if (!val)
		sin.sin_addr.s_addr = htonl(INADDR_ANY);
        else
		sin.sin_addr.s_addr = inet_addr(val);
	sin.sin_port = htons(listen_port);               
	if (bind(udp_socket, &sin, sizeof(sin)) == -1) 
	{
		ast_log(LOG_ERROR, "Unable to bind port for voter audio connection\n");
                close(udp_socket);
		ast_config_destroy(cfg);
                return AST_MODULE_LOAD_DECLINE;
	}

	voter_timing_fd = open("/dev/zap/pseudo",O_RDWR);
	if (voter_timing_fd == -1)
	{
		ast_log(LOG_ERROR,"Cant open zap timing channel\n");
                close(udp_socket);
		ast_config_destroy(cfg);
                return AST_MODULE_LOAD_DECLINE;
	}
	bs = FRAME_SIZE;
	if (ioctl(voter_timing_fd, ZT_SET_BLOCKSIZE, &bs) == -1) 
	{
		ast_log(LOG_WARNING, "Unable to set blocksize '%d': %s\n", bs,  strerror(errno));
		close(voter_timing_fd);
                close(udp_socket);
		ast_config_destroy(cfg);
                return AST_MODULE_LOAD_DECLINE;
	}

	ctg = NULL;
        while ( (ctg = ast_category_browse(cfg, ctg)) != NULL)
	{
		if (ctg == NULL) continue;
		if (!strcmp(ctg,"general")) continue;
		mynode = strtoul(ctg,NULL,0);
		for (v = ast_variable_browse(cfg, ctg); v; v = v->next)
		{
			client = (struct voter_client *)ast_malloc(sizeof(struct voter_client));
			if (!client)
			{
				ast_log(LOG_ERROR,"Cant malloc()\n");
		                close(udp_socket);
				ast_config_destroy(cfg);
		                return AST_MODULE_LOAD_DECLINE;
			}
			memset(client,0,sizeof(struct voter_client));
			client->nodenum = strtoul(ctg,NULL,0);
			ast_copy_string(client->name,v->name,VOTER_NAME_LEN - 1);
			if (strchr(v->name,'!')) client->totransmit = 1;
			if (strchr(v->name,'@')) client->ismaster = 1;
			client->digest = crc32_bufs(challenge,v->value);
			client->audio = (uint8_t *)ast_malloc(buflen);
			if (!client->audio)
			{
				ast_log(LOG_ERROR,"Cant malloc()\n");
		                close(udp_socket);
				ast_config_destroy(cfg);
		                return AST_MODULE_LOAD_DECLINE;
			}
			memset(client->audio,0xff,buflen);
			client->rssi = (uint8_t *)ast_malloc(buflen);
			if (!client->rssi)
			{
				ast_log(LOG_ERROR,"Cant malloc()\n");
		                close(udp_socket);
				ast_config_destroy(cfg);
		                return AST_MODULE_LOAD_DECLINE;
			}
			memset(client->rssi,0,buflen);
			ast_mutex_lock(&voter_lock);
			if (clients != NULL) client->next = clients;
			clients = client;
			ast_mutex_unlock(&voter_lock);
		}
	}
	ast_config_destroy(cfg);
	for(client = clients; client; client = client->next)
	{
		for(client1 = clients; client1; client1 = client1->next)
		{
			if (client->digest == 0)
			{
				ast_log(LOG_ERROR,"Can Not Load chan_voter -- VOTER client %s has invalid authetication digest (can not be 0)!!!\n",client->name);
		                return AST_MODULE_LOAD_DECLINE;
			}
			if (client == client1) continue;
			if (client->digest == client1->digest)
			{
				ast_log(LOG_ERROR,"Can Not Load chan_voter -- VOTER clients %s and %s have same authetication digest!!!\n",client->name,client1->name);
		                return AST_MODULE_LOAD_DECLINE;
			}
		}
	}
#ifdef	NEW_ASTERISK
	ast_cli_register_multiple(voter_cli,sizeof(voter_cli) / 
		sizeof(struct ast_cli_entry));
#else
	/* Register cli extensions */
	ast_cli_register(&cli_debug);
	ast_cli_register(&cli_test);
	ast_cli_register(&cli_record);
#endif

        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        ast_pthread_create(&voter_reader_thread,&attr,voter_reader,NULL);

	/* Make sure we can register our channel type */
	if (ast_channel_register(&voter_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
                close(udp_socket);
                return AST_MODULE_LOAD_DECLINE;
	}
	nullfd = open("/dev/null",O_RDWR);
	return 0;
}

#ifdef	OLD_ASTERISK
char *description()
{
	return (char *)voter_tech.description;
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
AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "radio Voter channel driver");
#endif
