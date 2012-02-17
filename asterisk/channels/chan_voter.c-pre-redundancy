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

Note on ADPCM functionality:
The original intent was to change this driver to use signed linear internally,
but after some thought, it was determined that it was prudent to continue using
mulaw as the "standard" internal audio format (with the understanding of the slight
degradation in dynamic range when using ADPCM resulting in doing so).  This was 
done becuase existing external entities (such as the recording files and the streaming
stuff) use mulaw as their transport, and changing all of that to signed linear would
be cumbersome, inefficient and undesirable.

Note on "Dynamic" client functionality:
DONT USE IT!!. It is intentionally *NOT* documented to encourage non-use of this
feature. It is for demo purposes *ONLY*. The chan_voter driver will *NOT* properly
perform reliably in a production environment if this option is used.
*/


#include "asterisk.h"
#include "../astver.h"

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
#include "asterisk/ulaw.h"
#include "asterisk/dsp.h"


#ifdef	NEW_ASTERISK
struct ast_flags zeroflag = { 0 };
#endif

#define	XPMR_VOTER
#include "xpmr/xpmr.h"

#ifdef	OLD_ASTERISK
#define	AST_MODULE_LOAD_DECLINE -1
#endif

#define	VOTER_CHALLENGE_LEN 10
#define	VOTER_NAME_LEN 50

#define	RX_TIMEOUT_MS 200
#define	CLIENT_TIMEOUT_MS 3000
#define	TX_KEEPALIVE_MS 1000

#define	DEFAULT_LINGER 6

#define	DEFAULT_DYNTIME 30000

#define MAX_MASTER_COUNT 3

#define CLIENT_WARN_SECS 60

#define	DELIMCHR ','
#define	QUOTECHR 34

#define	MAXSTREAMS 50
#define	MAXTHRESHOLDS 20

#define	GPS_WORK_FILE "/tmp/gps%s.tmp"
#define	GPS_DATA_FILE "/tmp/gps%s.dat"

#define	NTAPS_PL 6
#define	NTAPS_4K 6

static const char vdesc[] = "radio Voter channel driver";
static char type[] = "voter";

int run_forever = 1;
static int nullfd = -1;

AST_MUTEX_DEFINE_STATIC(voter_lock);

int16_t listen_port = 667;				/* port to listen to UDP packets on */
int udp_socket = -1;

int voter_timing_fd = -1;
int voter_timing_count = 0;
int last_master_count = 0;
int dyntime = DEFAULT_DYNTIME;

int check_client_sanity = 1;

char challenge[VOTER_CHALLENGE_LEN];
char password[100];
char context[100];

double dnsec;

static pthread_t voter_reader_thread = 0;
static pthread_t voter_timer_thread = 0;

#define	FRAME_SIZE 160
#define	ADPCM_FRAME_SIZE 163

#define	DEFAULT_BUFLEN 480 /* 480ms default buffer len */

#define BUFDELAY(p) (p->buflen - (FRAME_SIZE * 2))

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
	char elev[7];
} VOTER_GPS;

typedef struct {
	char name[32];
	uint8_t audio[FRAME_SIZE];
	uint8_t rssi;
} VOTER_REC;	

typedef struct {
	VTIME curtime;
	uint8_t audio[FRAME_SIZE];
	char str[152];
} VOTER_STREAM;
#pragma pack(pop)

#define VOTER_PAYLOAD_NONE	0
#define VOTER_PAYLOAD_ULAW	1
#define	VOTER_PAYLOAD_GPS	2
#define VOTER_PAYLOAD_ADPCM	3
#define VOTER_PAYLOAD_NULAW	4

struct voter_client {
	uint32_t nodenum;
	uint32_t digest;
	char name[VOTER_NAME_LEN];
	uint8_t *audio;
	uint8_t *rssi;
	uint32_t respdigest;
	struct sockaddr_in sin;
	int drainindex;
	int drainindex_40ms;
	int buflen;
	char heardfrom;
	char totransmit;
	char ismaster;
	char doadpcm;
	char donulaw;
	char mix;
	char nodeemp;
	char noplfilter;
	char dynamic;
	struct voter_client *next;
	long long dtime;
	uint8_t lastrssi;
	int txseqno;
	int txseqno_rxkeyed;
	int rxseqno;
	int rxseqno_40ms;
	char rxseq40ms;
	char drain40ms;
	time_t warntime;
	char *gpsid;
	int reload;
	int old_buflen;
	struct timeval lastheardtime;
	struct timeval lastdyntime;
	struct timeval lastsenttime;
	int prio;
	int prio_override;
	VTIME lastgpstime;
	VTIME lastmastergpstime;
} ;

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
	char drained_once;
	int testcycle;
	int testindex;
	struct voter_client *lastwon;
	char *streams[MAXSTREAMS];
	int nstreams;
	float	hpx[NTAPS_PL + 1];
	float	hpy[NTAPS_PL + 1];
	float	rlpx[NTAPS_4K + 1];
	float	rlpy[NTAPS_4K + 1];
	float	tlpx[NTAPS_4K + 1];
	float	tlpy[NTAPS_4K + 1];
	char plfilter;
	int linger;
	uint8_t rssi_thresh[MAXTHRESHOLDS];
	uint16_t count_thresh[MAXTHRESHOLDS];
	uint16_t linger_thresh[MAXTHRESHOLDS];
	int nthresholds;
	int threshold;
	struct voter_client *winner;
	uint16_t threshcount;
	uint16_t lingercount;
	struct ast_dsp *dsp;
	struct ast_trans_pvt *adpcmin;
	struct ast_trans_pvt *adpcmout;
	struct ast_trans_pvt *nuin;
	struct ast_trans_pvt *nuout;
	struct ast_trans_pvt *toast;
	struct ast_trans_pvt *toast1;
	struct ast_trans_pvt *fromast;
	t_pmr_chan	*pmrChan;
	char	txctcssfreq[32];
	int	txctcsslevel;
	int	txtoctype;
	char 	duplex;
	struct	ast_frame *adpcmf1;
	struct	ast_frame *nulawf1;
	ast_mutex_t xmit_lock;
	ast_cond_t xmit_cond;
	pthread_t xmit_thread;
	int voter_test;
	char usedtmf;

#ifdef 	OLD_ASTERISK
	AST_LIST_HEAD(, ast_frame) txq;
#else
	AST_LIST_HEAD_NOLOCK(, ast_frame) txq;
#endif
	ast_mutex_t  txqlock;
};

#ifdef	OLD_ASTERISK
int reload();
#else
static int reload(void);
#endif

#ifdef	OLD_ASTERISK
static int usecnt;
AST_MUTEX_DEFINE_STATIC(usecnt_lock);
#endif

int debug = 0;
FILE *recfp = NULL;
int hasmaster = 0;

/* This is just a horrendous KLUDGE!! Some Garmin LVC-18 GPS "pucks"
 *sometimes* get EXACTLY 1 second off!! Some dont do it at all.
 Some do it constantly. Some just do it once in a while. In an attempt
 to be at least somewhat tolerant of such swine poo-poo, the "puckit"
 configuration flag may be set, which makes an attempt to deal with
 this problem by keeping a "time differential" for each client (compared
 with the "master") and applying it to time information within the protocol.
 Obviously, this SHOULD NEVER HAVE TO BE DONE. */

int puckit = 0;

static char *config = "voter.conf";

struct voter_pvt *pvts = NULL;

struct voter_client *clients = NULL;

struct voter_client *dyn_clients = NULL;

FILE *fp;

VTIME master_time = {0,0};
VTIME mastergps_time = {0,0};

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
static int voter_setoption(struct ast_channel *chan, int option, void *data, int datalen);

static const struct ast_channel_tech voter_tech = {
	.type = type,
	.description = vdesc,
	.capabilities = AST_FORMAT_SLINEAR,
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
	.setoption = voter_setoption,

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
"Usage: voter test instance_id [test value]\n"
"       Specifies/Queries test mode for voter instance\n";


/* Prio */
static int voter_do_prio(int fd, int argc, char *argv[]);

static char prio_usage[] =
"Usage: voter prio instance_id [client_id] [priority value]\n"
"       Specifies/Queries priority value for voter client\n";


/* Record */
static int voter_do_record(int fd, int argc, char *argv[]);

static char record_usage[] =
"Usage: voter record [record filename]\n"
"       Enables/Specifies (or disables) recording file for chan_voter\n";

/* Tone */
static int voter_do_tone(int fd, int argc, char *argv[]);

static char tone_usage[] =
"Usage: voter tone instance_id [new_tone_level(0-250)]\n"
"       Sets/Queries Tx CTCSS level for specified chan_voter instance\n";

/* Reload */
static int voter_do_reload(int fd, int argc, char *argv[]);

static char reload_usage[] =
"Usage: voter reload\n"
"       Reload chan_voter parameters\n";

/* Display */
static int voter_do_display(int fd, int argc, char *argv[]);

static char display_usage[] =
"Usage: voter display [instance]\n"
"       Display voter instance clients\n";



#ifndef	NEW_ASTERISK

static struct ast_cli_entry  cli_debug =
        { { "voter", "debug", "level" }, voter_do_debug, 
		"Enable voter debugging", debug_usage };
static struct ast_cli_entry  cli_test =
        { { "voter", "test" }, voter_do_test, 
		"Specify/Query voter instance test mode", test_usage };
static struct ast_cli_entry  cli_prio =
        { { "voter", "prio" }, voter_do_prio, 
		"Specify/Query voter client priority value", prio_usage };
static struct ast_cli_entry  cli_record =
        { { "voter", "record" }, voter_do_record, 
		"Enables/Specifies (or disables) voter recording file", record_usage };
static struct ast_cli_entry  cli_tone =
        { { "voter", "tone" }, voter_do_tone, 
		"Sets/Queries Tx CTCSS level for specified chan_voter instance", tone_usage };
static struct ast_cli_entry  cli_reload =
        { { "voter", "reload" }, voter_do_reload, 
		"Reloads chan_voter parameters", reload_usage };
static struct ast_cli_entry  cli_display =
        { { "voter", "display" }, voter_do_display, 
		"Display voter (instance) clients", display_usage };

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
                oldcrc32 = crc_32_tab[(oldcrc32 ^ *buf++) & 0xff] ^ ((uint32_t)oldcrc32 >> 8);
        }
        while(buf1 && *buf1)
        {
                oldcrc32 = crc_32_tab[(oldcrc32 ^ *buf1++) & 0xff] ^ ((uint32_t)oldcrc32 >> 8);
        }
        return ~oldcrc32;
}

/* IIR 6 pole High pass filter, 300 Hz corner with 0.5 db ripple */

#define GAIN1   1.745882764e+00

static int16_t hpass6(int16_t input,float *xv,float *yv)
{
        xv[0] = xv[1]; xv[1] = xv[2]; xv[2] = xv[3]; xv[3] = xv[4]; xv[4] = xv[5]; xv[5] = xv[6];
        xv[6] = ((float)input) / GAIN1;
        yv[0] = yv[1]; yv[1] = yv[2]; yv[2] = yv[3]; yv[3] = yv[4]; yv[4] = yv[5]; yv[5] = yv[6];
        yv[6] =   (xv[0] + xv[6]) - 6 * (xv[1] + xv[5]) + 15 * (xv[2] + xv[4])
                     - 20 * xv[3]
                     + ( -0.3491861578 * yv[0]) + (  2.3932556573 * yv[1])
                     + ( -6.9905126572 * yv[2]) + ( 11.0685981760 * yv[3])
                     + ( -9.9896695552 * yv[4]) + (  4.8664511065 * yv[5]);
        return((int)yv[6]);
}

/* IIR 6 pole Low pass filter, 1900 Hz corner with 0.5 db ripple */

#define GAIN2   1.080715413e+02

static int16_t lpass4(int16_t input,float *xv,float *yv)
{
        xv[0] = xv[1]; xv[1] = xv[2]; xv[2] = xv[3]; xv[3] = xv[4]; xv[4] = xv[5]; xv[5] = xv[6]; 
        xv[6] = ((float)input) / GAIN2;
        yv[0] = yv[1]; yv[1] = yv[2]; yv[2] = yv[3]; yv[3] = yv[4]; yv[4] = yv[5]; yv[5] = yv[6]; 
        yv[6] =   (xv[0] + xv[6]) + 6 * (xv[1] + xv[5]) + 15 * (xv[2] + xv[4])
                     + 20 * xv[3]
                     + ( -0.1802140297 * yv[0]) + (  0.7084527003 * yv[1])
                     + ( -1.5847014566 * yv[2]) + (  2.3188475168 * yv[3])
                     + ( -2.5392334760 * yv[4]) + (  1.6846484378 * yv[5]);
        return((int)yv[6]);
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

/* return offsetted time */
static long long puckoffset(struct voter_client *client)
{
long long btime,ptime,difftime;

	if (!puckit) return 0;
	btime = ((long long)client->lastmastergpstime.vtime_sec * 1000000000LL) + client->lastmastergpstime.vtime_nsec;
	ptime = ((long long)client->lastgpstime.vtime_sec * 1000000000LL) + client->lastgpstime.vtime_nsec;
	difftime = ptime - btime;
	return difftime;
}

static void mkpucked(struct voter_client *client,VTIME *dst)
{
long long btime;

	btime = ((long long)master_time.vtime_sec * 1000000000LL) + master_time.vtime_nsec;
	btime += puckoffset(client);
	dst->vtime_nsec = htonl((long)(btime % 1000000000LL));
	dst->vtime_sec = htonl((long)(btime / 1000000000LL));
	return;
}
		
/* must be called with voter_lock locked */
 static void incr_drainindex(struct voter_pvt *p)
{
struct voter_client *client;

	if (p == NULL) return;
	for(client = clients; client; client = client->next)
	{
		if (client->nodenum != p->nodenum) continue;
		if (!client->drain40ms) 
		{
			client->drainindex_40ms = client->drainindex;
			client->rxseqno_40ms = client->rxseqno;
		}
		client->drainindex += FRAME_SIZE;
		if (client->drainindex >= client->buflen) client->drainindex -= client->buflen;
		client->drain40ms = !client->drain40ms;
	}
}

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
	if (p->dsp) ast_dsp_free(p->dsp);
	if (p->adpcmin) ast_translator_free_path(p->adpcmin);
	if (p->adpcmout) ast_translator_free_path(p->adpcmout);
	if (p->toast) ast_translator_free_path(p->toast);
	if (p->toast) ast_translator_free_path(p->toast1);
	if (p->fromast) ast_translator_free_path(p->fromast);
	if (p->nuin) ast_translator_free_path(p->nuin);
	if (p->nuout) ast_translator_free_path(p->nuout);
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

static int voter_setoption(struct ast_channel *chan, int option, void *data, int datalen)
{
	char *cp;
	struct voter_pvt *o = chan->tech_pvt;

	/* all supported options require data */
	if (!data || (datalen < 1)) {
		errno = EINVAL;
		return -1;
	}

	switch (option) {
	case AST_OPTION_TONE_VERIFY:
		cp = (char *) data;
		switch (*cp) {
		case 1:
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: OFF(0) on %s\n",chan->name);
			o->usedtmf = 1;
			break;
		case 2:
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: MUTECONF/MAX(2) on %s\n",chan->name);
			o->usedtmf = 1;
			break;
		case 3:
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: DISABLE DETECT(3) on %s\n",chan->name);
			o->usedtmf = 0;
			break;
		default:
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: OFF(0) on %s\n",chan->name);
			o->usedtmf = 1;
			break;
		}
		break;
	}
	errno = 0;
	return 0;
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

	if (hasmaster && (!master_time.vtime_sec))
	{
		ast_mutex_lock(&p->txqlock);
		while((f1 = AST_LIST_REMOVE_HEAD(&p->txq,frame_list)) != NULL) ast_frfree(f1);
		ast_mutex_unlock(&p->txqlock);
		return 0;
	}

	if (fp != NULL) fwrite(AST_FRAME_DATAP(frame),1,frame->datalen,fp);
	f1 = ast_frdup(frame);
	memset(&f1->frame_list,0,sizeof(f1->frame_list));
	ast_mutex_lock(&p->txqlock);
	AST_LIST_INSERT_TAIL(&p->txq,f1,frame_list);
	ast_mutex_unlock(&p->txqlock);

	return 0;
}

static struct ast_frame *ast_frcat(struct ast_frame *f1, struct ast_frame *f2)
{

struct ast_frame *f;
char *cp;
int len;

	if ((f1->subclass != f2->subclass) || (f1->frametype != f2->frametype))
	{
		ast_log(LOG_ERROR,"ast_frcat() called with non-matching frame types!!\n");
		return NULL;
	}
	f = (struct ast_frame *)ast_malloc(sizeof(struct ast_frame));
	if (!f)
	{
		ast_log(LOG_ERROR,"Cant malloc()\n");
		return NULL;
	}
	memset(f,0,sizeof(struct ast_frame));
	len = f1->datalen + f2->datalen + AST_FRIENDLY_OFFSET;
	cp = malloc(len);
	if (!cp)
	{
		ast_log(LOG_ERROR,"Cant malloc()\n");
		return NULL;
	}
	memcpy(cp + AST_FRIENDLY_OFFSET,AST_FRAME_DATAP(f1),f1->datalen);
	memcpy(cp + AST_FRIENDLY_OFFSET + f1->datalen,AST_FRAME_DATAP(f2),f2->datalen);
	f->frametype = f1->frametype;
	f->subclass = f1->subclass;
	f->datalen = f1->datalen + f2->datalen;
	f->samples = f1->samples + f2->samples;
	AST_FRAME_DATAP(f) = cp + AST_FRIENDLY_OFFSET;;
	f->mallocd = AST_MALLOCD_HDR | AST_MALLOCD_DATA;
	f->src = "ast_frcat";
	f->offset = AST_FRIENDLY_OFFSET;
	return(f);
}


/* must be called with voter_lock locked */
static int voter_mix_and_send(struct voter_pvt *p, struct voter_client *maxclient, int maxrssi)
{

	int i,j,k,x,maxprio;
	struct ast_frame fr,*f1,*f2;
	struct voter_client *client;
	short  silbuf[FRAME_SIZE];


	memset(&fr,0,sizeof(struct ast_frame));
        fr.frametype = AST_FRAME_VOICE;
        fr.subclass = AST_FORMAT_ULAW;
        fr.datalen = FRAME_SIZE;
        fr.samples = FRAME_SIZE;
        AST_FRAME_DATA(fr) =  p->buf + AST_FRIENDLY_OFFSET;
        fr.src = type;
        fr.offset = AST_FRIENDLY_OFFSET;
        fr.mallocd = 0;
        fr.delivery.tv_sec = 0;
        fr.delivery.tv_usec = 0;
	f1 = ast_translate(p->toast,&fr,0);
	if (!f1)
	{
		ast_log(LOG_ERROR,"Can not translate frame to send to Asterisk\n");
		return(0);
	}
	maxprio = 0;
	for(client = clients; client; client = client->next)
	{
		if (client->nodenum != p->nodenum) continue;
		if (!client->mix) continue;
		if (client->prio_override == -1) continue;
		if (client->prio_override > -2)
			i = client->prio_override;
		else
			i = client->prio;
		if (i > maxprio) maxprio = i;
	}
	/* f1 now contains the voted-upon audio in slinear */
	for(client = clients; client; client = client->next)
	{
		short *sp1,*sp2;
		if (client->nodenum != p->nodenum) continue;
		if (!client->mix) continue;
		if (client->prio_override == -1) continue;
		if (maxprio)
		{
			if (client->prio_override > -2)
				i = client->prio_override;
			else
				i = client->prio;
			if (i < maxprio) continue;
		}
		i = (int)client->buflen - ((int)client->drainindex + FRAME_SIZE);
		if (i >= 0)
		{
			memcpy(p->buf + AST_FRIENDLY_OFFSET,client->audio + client->drainindex,FRAME_SIZE);
		}
		else
		{
			memcpy(p->buf + AST_FRIENDLY_OFFSET,client->audio + client->drainindex,FRAME_SIZE + i);
			memcpy(p->buf + AST_FRIENDLY_OFFSET + (client->buflen - i),client->audio,-i);
		}
		if (i >= 0)
		{
			memset(client->audio + client->drainindex,0xff,FRAME_SIZE);
		}
		else
		{
			memset(client->audio + client->drainindex,0xff,FRAME_SIZE + i);
			memset(client->audio,0xff,-i);
		}
		k = 0;
		if (i >= 0)
		{
			for(j = client->drainindex; j < client->drainindex + FRAME_SIZE; j++)
			{
				k += client->rssi[j];
				client->rssi[j] = 0;
			}
		}
		else
		{
			for(j = client->drainindex; j < client->drainindex + (FRAME_SIZE + i); j++)
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
			maxrssi = client->lastrssi;
			maxclient = client;
		}
		memset(&fr,0,sizeof(struct ast_frame));
	        fr.frametype = AST_FRAME_VOICE;
	        fr.subclass = AST_FORMAT_ULAW;
	        fr.datalen = FRAME_SIZE;
	        fr.samples = FRAME_SIZE;
	        AST_FRAME_DATA(fr) =  p->buf + AST_FRIENDLY_OFFSET;
	        fr.src = type;
	        fr.offset = AST_FRIENDLY_OFFSET;
	        fr.mallocd = 0;
	        fr.delivery.tv_sec = 0;
	        fr.delivery.tv_usec = 0;
		f2 = ast_translate(p->toast1,&fr,0);
		if (!f2)
		{
			ast_log(LOG_ERROR,"Can not translate frame to send to Asterisk\n");
			return(0);
		}
		sp1 = AST_FRAME_DATAP(f1);
		sp2 = AST_FRAME_DATAP(f2);
		for(i = 0; i < FRAME_SIZE; i++)
		{
			if (maxprio && client->lastrssi)
				j = sp2[i];
			else
				j = sp1[i] + sp2[i];
			if (j > 32767) j = 32767;
			if (j < -32767) j = -32767;
			sp1[i] = j;
		}
		ast_frfree(f2);
	}
	if (!maxclient) /* if nothing there */
	{
		if (p->rxkey && p->dsp && p->usedtmf)
		{
			memset(silbuf,0,sizeof(silbuf));
			memset(&fr,0,sizeof(struct ast_frame));
		        fr.frametype = AST_FRAME_VOICE;
		        fr.subclass = AST_FORMAT_SLINEAR;
		        fr.datalen = FRAME_SIZE * 2;
		        fr.samples = FRAME_SIZE;
		        AST_FRAME_DATA(fr) =  silbuf;
		        fr.src = type;
		        fr.offset = 0;
		        fr.mallocd = 0;
		        fr.delivery.tv_sec = 0;
		        fr.delivery.tv_usec = 0;
			f2 = ast_dsp_process(NULL,p->dsp,&fr);
#ifdef	OLD_ASTERISK
			if (f2->frametype == AST_FRAME_DTMF)
#else
			if ((f2->frametype == AST_FRAME_DTMF_END) ||
				(f2->frametype == AST_FRAME_DTMF_BEGIN))
#endif
			{
				if ((f2->subclass != 'm') && (f2->subclass != 'u'))
				{
#ifndef	OLD_ASTERISK
					if (f2->frametype == AST_FRAME_DTMF_END)
#endif
						ast_log(LOG_NOTICE,"Voter %d Got DTMF char %c\n",p->nodenum,f2->subclass);
				}
				else
				{
					f2->frametype = AST_FRAME_NULL;
					f2->subclass = 0;
				}
				ast_queue_frame(p->owner,f2);
				gettimeofday(&p->lastrxtime,NULL);
			}
		}
		memset(silbuf,0,sizeof(silbuf));
		memset(&fr,0,sizeof(struct ast_frame));
	        fr.frametype = AST_FRAME_VOICE;
	        fr.subclass = AST_FORMAT_SLINEAR;
	        fr.datalen = FRAME_SIZE * 2;
	        fr.samples = FRAME_SIZE;
	        AST_FRAME_DATA(fr) =  silbuf;
	        fr.src = type;
	        fr.offset = 0;
	        fr.mallocd = 0;
	        fr.delivery.tv_sec = 0;
	        fr.delivery.tv_usec = 0;
		p->threshold = 0;
		p->threshcount = 0;
		p->lingercount = 0;
		p->winner = 0;
		incr_drainindex(p);
		ast_queue_frame(p->owner,&fr);
		return(0);
	}
	p->winner = maxclient;
	incr_drainindex(p);
	gettimeofday(&p->lastrxtime,NULL);
	if (!p->rxkey)
	{
		memset(&fr,0,sizeof(fr));
		fr.datalen = 0;
		fr.samples = 0;
		fr.frametype = AST_FRAME_CONTROL;
		fr.subclass = AST_CONTROL_RADIO_KEY;
		AST_FRAME_DATA(fr) =  0;
		fr.src = type;
		fr.offset = 0;
		fr.mallocd=0;
		fr.delivery.tv_sec = 0;
		fr.delivery.tv_usec = 0;
		ast_queue_frame(p->owner,&fr);
	}
	p->rxkey = 1;
	x = 0;
	if (p->dsp && p->usedtmf)
	{
		f2 = ast_dsp_process(NULL,p->dsp,f1);
#ifdef	OLD_ASTERISK
		if (f2->frametype == AST_FRAME_DTMF)
#else
		if ((f2->frametype == AST_FRAME_DTMF_END) ||
			(f2->frametype == AST_FRAME_DTMF_BEGIN))
#endif
		{
			if ((f2->subclass != 'm') && (f2->subclass != 'u'))
			{
#ifndef	OLD_ASTERISK
				if (f2->frametype == AST_FRAME_DTMF_END)
#endif
					ast_log(LOG_NOTICE,"Voter %d Got DTMF char %c\n",p->nodenum,f2->subclass);
			}
			else
			{
				f2->frametype = AST_FRAME_NULL;
				f2->subclass = 0;
			}
			ast_queue_frame(p->owner,f2);
			x = 1;
		}
	}
	if (!x) ast_queue_frame(p->owner,f1);
	else
	{
		memset(silbuf,0,sizeof(silbuf));
		memset(&fr,0,sizeof(struct ast_frame));
	        fr.frametype = AST_FRAME_VOICE;
	        fr.subclass = AST_FORMAT_SLINEAR;
	        fr.datalen = FRAME_SIZE * 2;
	        fr.samples = FRAME_SIZE;
	        AST_FRAME_DATA(fr) =  silbuf;
	        fr.src = type;
	        fr.offset = 0;
	        fr.mallocd = 0;
	        fr.delivery.tv_sec = 0;
	        fr.delivery.tv_usec = 0;
		ast_queue_frame(p->owner,&fr);
	}
	return(1);
}


/* voter xmit thread */
static void *voter_xmit(void *data)
{

struct voter_pvt *p = (struct voter_pvt *)data;
int	i,n,x;
i16 dummybuf1[FRAME_SIZE * 12],xmtbuf1[FRAME_SIZE * 12];
i16 xmtbuf[FRAME_SIZE],dummybuf2[FRAME_SIZE];
struct ast_frame fr,*f1,*f2,*f3;
struct voter_client *client;
struct timeval tv;

#pragma pack(push)
#pragma pack(1)
	struct {
		VOTER_PACKET_HEADER vp;
		char rssi;
		char audio[FRAME_SIZE + 3];
	} audiopacket;
#pragma pack(pop)

	while(run_forever && (!ast_shutting_down()))
	{
		ast_mutex_lock(&p->xmit_lock);
		ast_cond_wait(&p->xmit_cond, &p->xmit_lock);
		ast_mutex_unlock(&p->xmit_lock);
		if (!p->drained_once)
		{
			p->drained_once = 1;
			continue;
		}
		n = x = 0;
		ast_mutex_lock(&p->txqlock);
		AST_LIST_TRAVERSE(&p->txq, f1,frame_list) n++;
		ast_mutex_unlock(&p->txqlock);
		if (n && ((n > 3) || (!p->txkey)))
		{
			x = 1;
			ast_mutex_lock(&p->txqlock);
			f2 = AST_LIST_REMOVE_HEAD(&p->txq,frame_list);
			ast_mutex_unlock(&p->txqlock);
			if (p->pmrChan)
			{
				p->pmrChan->txPttIn = 1;
				PmrTx(p->pmrChan,(i16*) AST_FRAME_DATAP(f2));
				ast_frfree(f2);
			}
		}
		f1 = NULL;
		// x will be set here is there was actual transmit activity
		if ((!x) && (p->pmrChan)) p->pmrChan->txPttIn = 0;
		if (x && (!p->pmrChan))
		{
			f1 = ast_translate(p->fromast,f2,1);
			if (!f1)
			{
				ast_log(LOG_ERROR,"Can not translate frame to recv from Asterisk\n");
				continue;
			}
		}
		if (p->pmrChan)
		{
			if (p->pmrChan->txPttOut && (!x)) 
			{
				memset(xmtbuf,0,sizeof(xmtbuf));
				if (p->pmrChan) PmrTx(p->pmrChan,xmtbuf);
			}
			PmrRx(p->pmrChan,dummybuf1,dummybuf2,xmtbuf1);
			x = p->pmrChan->txPttOut;
			for(i = 0; i < FRAME_SIZE; i++) xmtbuf[i] = xmtbuf1[i * 2];
			memset(&fr,0,sizeof(struct ast_frame));
		        fr.frametype = AST_FRAME_VOICE;
		        fr.subclass = AST_FORMAT_SLINEAR;
		        fr.datalen = ADPCM_FRAME_SIZE;
		        fr.samples = FRAME_SIZE;
		        AST_FRAME_DATA(fr) = xmtbuf;
		        fr.src = type;
		        fr.offset = 0;
		        fr.mallocd = 0;
		        fr.delivery.tv_sec = 0;
		        fr.delivery.tv_usec = 0;
			f1 = ast_translate(p->fromast,&fr,0);
			if (!f1)
			{
				ast_log(LOG_ERROR,"Can not translate frame to recv from Asterisk\n");
				continue;
			}
		}
		// x will now be set if we are to generate TX output
		if (x)
		{
			memset(&audiopacket,0,sizeof(audiopacket));
			strcpy((char *)audiopacket.vp.challenge,challenge);
			audiopacket.vp.payload_type = htons(1);
			audiopacket.rssi = 0;
			memcpy(audiopacket.audio,AST_FRAME_DATAP(f1),FRAME_SIZE);
			audiopacket.vp.curtime.vtime_sec = htonl(master_time.vtime_sec);
			audiopacket.vp.curtime.vtime_nsec = htonl(master_time.vtime_nsec);
			for(client = clients; client; client = client->next)
			{
				if (client->nodenum != p->nodenum) continue;
				if (!client->respdigest) continue;
				if (!client->heardfrom) continue;
				if (client->doadpcm) continue;
				if (client->donulaw) continue;
				mkpucked(client,&audiopacket.vp.curtime);
				audiopacket.vp.digest = htonl(client->respdigest);
				audiopacket.vp.curtime.vtime_nsec = (client->mix) ? htonl(client->txseqno) : htonl(master_time.vtime_nsec);
				if (client->totransmit)
				{
					if (debug > 1) ast_verbose("sending audio packet to client %s digest %08x\n",client->name,client->respdigest);
					sendto(udp_socket, &audiopacket, sizeof(audiopacket) - 3,0,(struct sockaddr *)&client->sin,sizeof(client->sin));
					gettimeofday(&client->lastsenttime,NULL);
				}
			}
		}
		if (x || p->adpcmf1)
		{
			if (p->adpcmf1 == NULL) p->adpcmf1 = ast_frdup(f1);
			else
			{
				memset(xmtbuf,0xff,sizeof(xmtbuf));
				memset(&fr,0,sizeof(struct ast_frame));
			        fr.frametype = AST_FRAME_VOICE;
			        fr.subclass = AST_FORMAT_ULAW;
			        fr.datalen = FRAME_SIZE;
			        fr.samples = FRAME_SIZE;
			        AST_FRAME_DATA(fr) = xmtbuf;
			        fr.src = type;
			        fr.offset = 0;
			        fr.mallocd = 0;
			        fr.delivery.tv_sec = 0;
			        fr.delivery.tv_usec = 0;
				if (x) f3 = ast_frcat(p->adpcmf1,f1); else f3 = ast_frcat(p->adpcmf1,&fr);
				ast_frfree(p->adpcmf1);
				p->adpcmf1 = NULL;
				f2 = ast_translate(p->adpcmout,f3,1);
				memcpy(audiopacket.audio,AST_FRAME_DATAP(f2),f2->datalen);
				audiopacket.vp.curtime.vtime_sec = htonl(master_time.vtime_sec);
				audiopacket.vp.payload_type = htons(3);
				for(client = clients; client; client = client->next)
				{
					if (client->nodenum != p->nodenum) continue;
					if (!client->respdigest) continue;
					if (!client->heardfrom) continue;
					if (!client->doadpcm) continue;
					mkpucked(client,&audiopacket.vp.curtime);
					audiopacket.vp.digest = htonl(client->respdigest);
					audiopacket.vp.curtime.vtime_nsec = (client->mix) ? htonl(client->txseqno) : htonl(master_time.vtime_nsec);
#ifndef	ADPCM_LOOPBACK
					if (client->totransmit)
					{
						if (debug > 1) ast_verbose("sending audio packet to client %s digest %08x\n",client->name,client->respdigest);
						sendto(udp_socket, &audiopacket, sizeof(audiopacket),0,(struct sockaddr *)&client->sin,sizeof(client->sin));
						gettimeofday(&client->lastsenttime,NULL);
					}
#endif
				}
				ast_frfree(f2);
			}
		}
		if (x || p->nulawf1)
		{
			short *sap,s;
			unsigned char nubuf[FRAME_SIZE];

			if (p->nulawf1 == NULL) p->nulawf1 = ast_frdup(f1);
			else
			{
				memset(xmtbuf,0xff,sizeof(xmtbuf));
				memset(&fr,0,sizeof(struct ast_frame));
			        fr.frametype = AST_FRAME_VOICE;
			        fr.subclass = AST_FORMAT_ULAW;
			        fr.datalen = FRAME_SIZE;
			        fr.samples = FRAME_SIZE;
			        AST_FRAME_DATA(fr) = xmtbuf;
			        fr.src = type;
			        fr.offset = 0;
			        fr.mallocd = 0;
			        fr.delivery.tv_sec = 0;
			        fr.delivery.tv_usec = 0;
				if (x) f3 = ast_frcat(p->nulawf1,f1); else f3 = ast_frcat(p->nulawf1,&fr);
				ast_frfree(p->nulawf1);
				p->nulawf1 = NULL;
				f2 = ast_translate(p->nuout,f3,1);
				sap = (short *)AST_FRAME_DATAP(f2);
				for(i = 0; i < f2->samples / 2; i++)
				{
					s = *sap++;
					if (s > 14000) s = 14000;
					if (s < -14000) s = -14000;
					lpass4(s,p->tlpx,p->tlpy);
					s = *sap++;
					if (s > 14000) s = 14000;
					if (s < -14000) s = -14000;
					nubuf[i] = AST_LIN2MU(lpass4(s,p->tlpx,p->tlpy));
				}
				memcpy(audiopacket.audio,nubuf,sizeof(nubuf));
				audiopacket.vp.curtime.vtime_sec = htonl(master_time.vtime_sec);
				audiopacket.vp.payload_type = htons(4);
				for(client = clients; client; client = client->next)
				{
					if (client->nodenum != p->nodenum) continue;
					if (!client->respdigest) continue;
					if (!client->heardfrom) continue;
					if (!client->donulaw) continue;
					mkpucked(client,&audiopacket.vp.curtime);
					audiopacket.vp.digest = htonl(client->respdigest);
					audiopacket.vp.curtime.vtime_nsec = (client->mix) ? htonl(client->txseqno) : htonl(master_time.vtime_nsec);
#ifndef	NULAW_LOOPBACK
					if (client->totransmit)
					{
						if (debug > 1) ast_verbose("sending audio packet to client %s digest %08x\n",client->name,client->respdigest);
						sendto(udp_socket, &audiopacket, sizeof(audiopacket) - 3,0,(struct sockaddr *)&client->sin,sizeof(client->sin));
						gettimeofday(&client->lastsenttime,NULL);
					}
#endif
				}
				ast_frfree(f2);
			}
		}
		if (f1) ast_frfree(f1);
		gettimeofday(&tv,NULL);
		for(client = clients; client; client = client->next)
		{
			if (client->nodenum != p->nodenum) continue;
			if (!client->respdigest) continue;
			if (!client->heardfrom) continue;
			if (ast_tvzero(client->lastsenttime) || (ast_tvdiff_ms(tv,client->lastsenttime) >= TX_KEEPALIVE_MS))
			{
				
				memset(&audiopacket,0,sizeof(audiopacket));
				strcpy((char *)audiopacket.vp.challenge,challenge);
				audiopacket.vp.curtime.vtime_sec = htonl(master_time.vtime_sec);
				audiopacket.vp.payload_type = htons(2);
				audiopacket.vp.digest = htonl(client->respdigest);
				audiopacket.vp.curtime.vtime_nsec = (client->mix) ? htonl(client->txseqno) : htonl(master_time.vtime_nsec);
				if (debug > 1) ast_verbose("sending KEEPALIVE (GPS) packet to client %s digest %08x\n",client->name,client->respdigest);
				sendto(udp_socket, &audiopacket, sizeof(VOTER_PACKET_HEADER),0,(struct sockaddr *)&client->sin,sizeof(client->sin));
				gettimeofday(&client->lastsenttime,NULL);
			}
		}
	}
	pthread_exit(NULL);
}

static struct ast_channel *voter_request(const char *type, int format, void *data, int *cause)
{
	int oldformat,i;
	struct voter_pvt *p;
	struct ast_channel *tmp = NULL;
	char *val,*cp,*cp1,*cp2,*strs[MAXTHRESHOLDS];
	struct ast_config *cfg = NULL;
	pthread_attr_t attr;
	
	oldformat = format;
	format &= AST_FORMAT_SLINEAR;
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
	ast_mutex_init(&p->xmit_lock);
	ast_cond_init(&p->xmit_cond,NULL);
	p->dsp = ast_dsp_new();
	if (!p->dsp)
	{
		ast_log(LOG_ERROR,"Cannot get DSP!!\n");
		ast_free(p);
		return NULL;
	}
#ifdef  NEW_ASTERISK
        ast_dsp_set_features(p->dsp,DSP_FEATURE_DIGIT_DETECT);
        ast_dsp_set_digitmode(p->dsp,DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_RELAXDTMF);
#else
        ast_dsp_set_features(p->dsp,DSP_FEATURE_DTMF_DETECT);
        ast_dsp_digitmode(p->dsp,DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_RELAXDTMF);
#endif
	p->usedtmf = 1;
	p->adpcmin = ast_translator_build_path(AST_FORMAT_ULAW,AST_FORMAT_ADPCM);
	if (!p->adpcmin)
	{
		ast_log(LOG_ERROR,"Cannot get translator from adpcm to ulaw!!\n");
		ast_dsp_free(p->dsp);
		ast_free(p);
		return NULL;
	}
	p->adpcmout = ast_translator_build_path(AST_FORMAT_ADPCM,AST_FORMAT_ULAW);
	if (!p->adpcmout)
	{
		ast_log(LOG_ERROR,"Cannot get translator from ulaw to adpcm!!\n");
		ast_dsp_free(p->dsp);
		ast_free(p);
		return NULL;
	}
	p->toast = ast_translator_build_path(AST_FORMAT_SLINEAR,AST_FORMAT_ULAW);
	if (!p->toast)
	{
		ast_log(LOG_ERROR,"Cannot get translator from ulaw to slinear!!\n");
		ast_dsp_free(p->dsp);
		ast_free(p);
		return NULL;
	}
	p->toast1 = ast_translator_build_path(AST_FORMAT_SLINEAR,AST_FORMAT_ULAW);
	if (!p->toast1)
	{
		ast_log(LOG_ERROR,"Cannot get translator from ulaw to slinear!!\n");
		ast_dsp_free(p->dsp);
		ast_free(p);
		return NULL;
	}
	p->fromast = ast_translator_build_path(AST_FORMAT_ULAW,AST_FORMAT_SLINEAR);
	if (!p->fromast)
	{
		ast_log(LOG_ERROR,"Cannot get translator from slinear to ulaw!!\n");
		ast_dsp_free(p->dsp);
		ast_free(p);
		return NULL;
	}
	p->nuin = ast_translator_build_path(AST_FORMAT_ULAW,AST_FORMAT_SLINEAR);
	if (!p->nuin)
	{
		ast_log(LOG_ERROR,"Cannot get translator from slinear to ulaw!!\n");
		ast_dsp_free(p->dsp);
		ast_free(p);
		return NULL;
	}
	p->nuout = ast_translator_build_path(AST_FORMAT_SLINEAR,AST_FORMAT_ULAW);
	if (!p->nuout)
	{
		ast_log(LOG_ERROR,"Cannot get translator from ulaw to slinear!!\n");
		ast_dsp_free(p->dsp);
		ast_free(p);
		return NULL;
	}
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
	tmp->rawwriteformat = AST_FORMAT_SLINEAR;
	tmp->writeformat = AST_FORMAT_SLINEAR;
	tmp->rawreadformat = AST_FORMAT_SLINEAR;
	tmp->readformat = AST_FORMAT_SLINEAR;
	tmp->nativeformats = AST_FORMAT_SLINEAR;
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
#ifdef  NEW_ASTERISK
        if (!(cfg = ast_config_load(config,zeroflag))) {
#else
        if (!(cfg = ast_config_load(config))) {
#endif
                ast_log(LOG_ERROR, "Unable to load config %s\n", config);
        } else {
	        val = (char *) ast_variable_retrieve(cfg,(char *)data,"linger"); 
		if (val) p->linger = atoi(val); else p->linger = DEFAULT_LINGER;
	        val = (char *) ast_variable_retrieve(cfg,(char *)data,"plfilter"); 
		if (val) p->plfilter = ast_true(val);
	        val = (char *) ast_variable_retrieve(cfg,(char *)data,"duplex"); 
		if (val) p->duplex = ast_true(val); else p->duplex = 1;
	        val = (char *) ast_variable_retrieve(cfg,(char *)data,"streams"); 
		if (val)
		{
			cp = ast_strdup(val);
			p->nstreams = finddelim(cp,p->streams,MAXSTREAMS);
		}		
	        val = (char *) ast_variable_retrieve(cfg,(char *)data,"txctcss"); 
		if (val) ast_copy_string(p->txctcssfreq,val,sizeof(p->txctcssfreq));
	        val = (char *) ast_variable_retrieve(cfg,(char *)data,"txctcsslevel"); 
		if (val) p->txctcsslevel = atoi(val); else p->txctcsslevel = 62;
		p->txtoctype = TOC_NONE;
	        val = (char *) ast_variable_retrieve(cfg,(char *)data,"txtoctype"); 
		if (val)
		{
			if (!strcasecmp(val,"phase")) p->txtoctype = TOC_PHASE;
			else if (!strcasecmp(val,"notone")) p->txtoctype = TOC_NOTONE;
		}
	        val = (char *) ast_variable_retrieve(cfg,(char *)data,"thresholds"); 
		if (val)
		{
			cp = ast_strdup(val);
			p->nthresholds = finddelim(cp,strs,MAXTHRESHOLDS);
			for(i = 0; i < p->nthresholds; i++)
			{
				cp1 = strchr(strs[i],'=');
				p->linger_thresh[i] = p->linger;
				if (cp1)
				{
					*cp1 = 0;
					cp2 = strchr(cp1 + 1,':');
					if (cp2)
					{
						*cp2 = 0;
						if (cp2[1]) p->linger_thresh[i] = (uint16_t)atoi(cp2 + 1);
					}
					if (cp1[1]) p->count_thresh[i] = (uint16_t)atoi(cp1 + 1);
				}
				p->rssi_thresh[i] = (uint8_t)atoi(strs[i]);
			}
			ast_free(cp);
		}		
	}

	if (p->txctcssfreq[0])
	{
		t_pmr_chan tChan;

		memset(&tChan,0,sizeof(t_pmr_chan));

		tChan.pTxCodeDefault = p->txctcssfreq;
		tChan.pTxCodeSrc     = p->txctcssfreq;
		tChan.pRxCodeSrc     = p->txctcssfreq;
		tChan.txMod = 2;
		tChan.txMixA = TX_OUT_COMPOSITE;
		tChan.b.txboost = 1;
		p->pmrChan = createPmrChannel(&tChan,FRAME_SIZE);
		p->pmrChan->radioDuplex = 1;//o->radioduplex;
		p->pmrChan->b.loopback=0; 
		p->pmrChan->b.radioactive= 1;
		p->pmrChan->txrxblankingtime = 0;
		p->pmrChan->rxCpuSaver = 0;
		p->pmrChan->txCpuSaver = 0;
		*(p->pmrChan->prxSquelchAdjust) = 0;
		*(p->pmrChan->prxVoiceAdjust) = 0;
		*(p->pmrChan->prxCtcssAdjust) = 0;
		p->pmrChan->rxCtcss->relax = 0;
		p->pmrChan->txTocType = p->txtoctype;
		p->pmrChan->spsTxOutA->outputGain = 250;
		*p->pmrChan->ptxCtcssAdjust = p->txctcsslevel;
		p->pmrChan->pTxCodeDefault = p->txctcssfreq;
		p->pmrChan->pTxCodeSrc = p->txctcssfreq;
//		p->pmrChan->txfreq = p->txctcssfreq;
			
	}
	ast_config_destroy(cfg);
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        ast_pthread_create(&p->xmit_thread,&attr,voter_xmit,p);
	pthread_attr_destroy(&attr);
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
	struct voter_pvt *p;

        if (argc < 3)
                return RESULT_SHOWUSAGE;
	ast_mutex_lock(&voter_lock);
	for(p = pvts; p; p = p->next)
	{
		if (p->nodenum == atoi(argv[2])) break;
	}
	if (!p)
	{
		ast_cli(fd,"voter instance %s not found\n",argv[2]);
		ast_mutex_unlock(&voter_lock);
		return RESULT_SUCCESS;
	}
	if (argc == 3)
	{
		if (p->voter_test)
			ast_cli(fd,"voter instance %d Test: currently set to %d\n",p->nodenum,p->voter_test);
		else
			ast_cli(fd,"voter instance %d Test: currently disabled\n",p->nodenum);
		ast_mutex_unlock(&voter_lock);
		return RESULT_SUCCESS;
	}		
        if (argc != 4)
	{
		ast_mutex_unlock(&voter_lock);
                return RESULT_SHOWUSAGE;
	}
        newlevel = atoi(argv[3]);
	if (newlevel < 0)
	{
		ast_cli(fd,"Error: Invalid test mode value specification!!\n");
		ast_mutex_unlock(&voter_lock);
		return RESULT_SUCCESS;
	}
        if(newlevel)
                ast_cli(fd, "voter instance %d Test: previous level: %d, new level: %d\n", p->nodenum, p->voter_test, newlevel);
        else
                ast_cli(fd, "voter instance %d Test disabled\n",p->nodenum);

        p->voter_test = newlevel;                                                                                                                          
	ast_mutex_unlock(&voter_lock);
        return RESULT_SUCCESS;
}

static int voter_do_prio(int fd, int argc, char *argv[])
{
	int newlevel;
	struct voter_pvt *p;
	struct voter_client *client;

        if (argc < 3)
                return RESULT_SHOWUSAGE;
	ast_mutex_lock(&voter_lock);
	for(p = pvts; p; p = p->next)
	{
		if (p->nodenum == atoi(argv[2])) break;
	}
	if (!p)
	{
		ast_cli(fd,"voter instance %s not found\n",argv[2]);
		ast_mutex_unlock(&voter_lock);
		return RESULT_SUCCESS;
	}
	if (argc == 3)
	{
		ast_cli(fd,"Voter instance %d priority values:\n\n",p->nodenum);
		for(client = clients; client; client = client->next)
		{
			if (client->nodenum != p->nodenum) continue;
			if (client->prio_override > -2)
				ast_cli(fd,"client %s: eff_prio: %d, prio: %d, override_prio: %d\n",
					client->name,client->prio_override, client->prio,client->prio_override);
			else
				ast_cli(fd,"client %s: prio: %d (not overridden)\n",client->name,client->prio);
		}
		ast_mutex_unlock(&voter_lock);
		return RESULT_SUCCESS;
	}
	if (argc == 4)
	{
		for(client = clients; client; client = client->next)
		{
			if (client->nodenum != p->nodenum) continue;
			if (strcasecmp(argv[3],client->name)) continue;
			if (client->prio_override > -2)
				ast_cli(fd,"Voter instance %d, client %s: eff_prio: %d, prio: %d, override_prio: %d\n",
					p->nodenum,client->name,client->prio_override, client->prio,client->prio_override);
			else
				ast_cli(fd,"Voter instance %d, client %s: prio: %d (not overridden)\n",
					p->nodenum,client->name,client->prio);
			break;
		}
		if (!client) ast_cli(fd,"voter client %s not found\n",argv[3]);
		ast_mutex_unlock(&voter_lock);
		return RESULT_SUCCESS;
	}
        if (argc != 5)
	{
		ast_mutex_unlock(&voter_lock);
                return RESULT_SHOWUSAGE;
	}
	for(client = clients; client; client = client->next)
	{
		if (client->nodenum != p->nodenum) continue;
		if (!strcasecmp(argv[3],client->name)) break;
	}
	if (!client)
	{
		ast_cli(fd,"voter client %s not found\n",argv[3]);
		ast_mutex_unlock(&voter_lock);
		return RESULT_SUCCESS;
	}
	if ((!strcasecmp(argv[4],"off")) || (!strncasecmp(argv[4],"dis",3))) newlevel = -2;
	else 
	{
		if (sscanf(argv[4],"%d",&newlevel) < 1)
		{
			ast_cli(fd,"Error: Invalid priority value specification!!\n");
			ast_mutex_unlock(&voter_lock);
			return RESULT_SUCCESS;
		}
	}
	if (newlevel < -2)
	{
		ast_cli(fd,"Error: Invalid priority value specification!!\n");
		ast_mutex_unlock(&voter_lock);
		return RESULT_SUCCESS;
	}
        if (newlevel > -2)
	{
		if (client->prio_override > -2)
	                ast_cli(fd, "voter instance %d prio (override): previous level: %d, new level: %d\n", 
				p->nodenum, client->prio_override, newlevel);
		else
	                ast_cli(fd, "voter instance %d prio (override): previous level: <disabled>, new level: %d\n", 
				p->nodenum, newlevel);
	}
        else
                ast_cli(fd, "voter instance %d prio (override) disabled\n",p->nodenum);

        client->prio_override = newlevel;                                                                                                                          
	ast_mutex_unlock(&voter_lock);
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

static int voter_do_tone(int fd, int argc, char *argv[])
{
	int newlevel;
	struct voter_pvt *p;

        if (argc < 3)
                return RESULT_SHOWUSAGE;
	ast_mutex_lock(&voter_lock);
	for(p = pvts; p; p = p->next)
	{
		if (p->nodenum == atoi(argv[2])) break;
	}
	if (!p)
	{
		ast_cli(fd,"voter instance %s not found\n",argv[2]);
		ast_mutex_unlock(&voter_lock);
		return RESULT_SUCCESS;
	}
	if (!p->pmrChan)
	{
		ast_cli(fd,"voter instance %s does not have CTCSS enabled\n",argv[2]);
		ast_mutex_unlock(&voter_lock);
		return RESULT_SUCCESS;
	}
	if (argc == 3)
	{
		ast_cli(fd,"voter instance %d CTCSS tone level is %d\n",p->nodenum,p->txctcsslevel);
		ast_mutex_unlock(&voter_lock);
		return RESULT_SUCCESS;
	}
        newlevel = atoi(argv[3]);
        if((newlevel < 0) || (newlevel > 250))
	{
		ast_mutex_unlock(&voter_lock);
                return RESULT_SHOWUSAGE;
	}
        ast_cli(fd, "voter instance %d CTCSS tone level set to %d\n",p->nodenum,newlevel);
	p->txctcsslevel = newlevel;
	*p->pmrChan->ptxCtcssAdjust = newlevel;
	ast_mutex_unlock(&voter_lock);
        return RESULT_SUCCESS;
}

static int voter_do_reload(int fd, int argc, char *argv[])
{
        if (argc != 2)
                return RESULT_SHOWUSAGE;
	reload();
        return RESULT_SUCCESS;
}

static int rad_rxwait(int fd,int ms)
{
int	myms = ms,x;

	x = ast_waitfor_n_fd(&fd, 1, &myms,NULL);
	if (x == -1) return -1;
	if (x == fd) return 1;
	return 0;
}

static void voter_display(int fd, struct voter_pvt *p)
{
	int j,rssi,thresh,ncols = 56,wasverbose,vt100compat;
	char str[256],*term,c,hasdyn;
	struct voter_client *client;


	term = getenv("TERM");
	vt100compat = 0;

	if (term)
	{
                if (!strcmp(term, "linux")) {
                        vt100compat = 1;
                } else if (!strcmp(term, "xterm")) {
                        vt100compat = 1;
                } else if (!strcmp(term, "xterm-color")) {
                        vt100compat = 1;
                } else if (!strncmp(term, "Eterm", 5)) {
                        /* Both entries which start with Eterm support color */
                        vt100compat = 1;
                } else if (!strcmp(term, "vt100")) {
                        vt100compat = 1;
                } else if (!strncmp(term, "crt", 3)) {
                        /* Both crt terminals support color */
                        vt100compat = 1;
                }
	}


	for(j = 0; j < ncols; j++) str[j] = ' ';
	str[j] = 0;
	ast_cli(fd," %s \r",str);

	wasverbose = option_verbose;
	option_verbose = 0;

	for(;;)
	{
		if (rad_rxwait(fd,100)) break;
		if (vt100compat) ast_cli(fd,"\033[2J\033[H");
		ast_cli(fd,"VOTER INSTANCE %d DISPLAY:\n\n",p->nodenum);
		if (hasmaster && (!master_time.vtime_sec))
			ast_cli(fd,"*** WARNING -- LOSS OF MASTER TIMING SOURCE ***\n\n");
		hasdyn = 0;
		for(client = clients; client; client = client->next)
		{
			if (client->nodenum != p->nodenum) continue;
			if (client->dynamic) hasdyn = 1;
			if (!client->respdigest) continue;
			if (!client->heardfrom) continue;
			rssi = client->lastrssi;
			thresh = (rssi * ncols) / 256;
			for(j = 0; j < ncols; j++)
			{
				if (client->prio_override == -1) str[j] = 'X';
				else if (j < thresh) str[j] = '=';
				else if (j == thresh) str[j] = '>';
				else str[j] = ' ';
			}
			str[j] = 0;
			c = ' ';
			if (p->winner == client) c = '*';
			ast_cli(fd,"%c%10.10s |%s| [%3d]\n",c,client->name,str,rssi);
		}
		ast_cli(fd,"\n\n");
		if (hasdyn)
		{
			ast_cli(fd,"ACTIVE DYNAMIC CLIENTS:\n\n");
			for(client = clients; client; client = client->next)
			{
				if (!client->dynamic) continue;
				if (ast_tvzero(client->lastdyntime)) continue;
				ast_cli(fd,"%10.10s -- %s:%d\n",client->name,ast_inet_ntoa(client->sin.sin_addr),ntohs(client->sin.sin_port));
			}
			ast_cli(fd,"\n\n");
		}
	}
	option_verbose = wasverbose;
}

static int voter_do_display(int fd, int argc, char *argv[])
{
struct voter_pvt *p;

        if (argc != 3)
                return RESULT_SHOWUSAGE;
	for(p = pvts; p; p = p->next)
	{
		if (p->nodenum == atoi(argv[2])) break;
	}
	if (!p)
	{
		ast_cli(fd,"voter instance %s not found\n",argv[2]);
		return RESULT_SUCCESS;
	}
	voter_display(fd,p);
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

static char *handle_cli_prio(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "voter prio";
                e->usage = prio_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(voter_do_prio(a->fd,a->argc,a->argv));
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

static char *handle_cli_tone(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "voter tone";
                e->usage = tone_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(voter_do_tone(a->fd,a->argc,a->argv));
}

static char *handle_cli_reload(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "voter reload";
                e->usage = reload_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(voter_do_reload(a->fd,a->argc,a->argv));
}

static char *handle_cli_display(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "voter display";
                e->usage = display_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(voter_do_display(a->fd,a->argc,a->argv));
}

static struct ast_cli_entry voter_cli[] = {
	AST_CLI_DEFINE(handle_cli_debug,"Enable voter debugging"),
	AST_CLI_DEFINE(handle_cli_test,"Specify/Query voter instance test mode"),
	AST_CLI_DEFINE(handle_cli_prio,"Specify/Query voter client priority value"),
	AST_CLI_DEFINE(handle_cli_record,"Enable/Specify (or disable) voter recording file"),
	AST_CLI_DEFINE(handle_cli_tone,"Sets/Queries Tx CTCSS level for specified chan_voter instance"),
	AST_CLI_DEFINE(handle_cli_reload,"Reloads chan_voter parameters"),
	AST_CLI_DEFINE(handle_cli_display,"Displays voter (instance) clients"),
} ;

#endif

#include "xpmr/xpmr.c"

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
	ast_cli_unregister(&cli_prio);
	ast_cli_unregister(&cli_record);
	ast_cli_unregister(&cli_tone);
	ast_cli_unregister(&cli_reload);
	ast_cli_unregister(&cli_display);
#endif
	/* First, take us out of the channel loop */
	ast_channel_unregister(&voter_tech);
	if (nullfd != -1) close(nullfd);
	return 0;
}

	
static void voter_xmit_master(void)
{
struct voter_client *client;
struct voter_pvt *p;
struct timeval tv;

	for(client = clients; client; client = client->next)
	{
		if (!client->respdigest) continue;
		if (!client->heardfrom) continue;
		if (!client->mix) continue;
		client->txseqno++;
		if (client->rxseqno)
		{
			if ((!client->doadpcm) && (!client->donulaw)) client->rxseqno++;
			else
			{
				if (client->rxseq40ms) client->rxseqno += 2;
				client->rxseq40ms = !client->rxseq40ms;
			}
		}
	}
	for(p = pvts; p; p = p->next) 
	{
		ast_mutex_lock(&p->xmit_lock);
		ast_cond_signal(&p->xmit_cond);
		ast_mutex_unlock(&p->xmit_lock);
	}
	gettimeofday(&tv,NULL);
	for(client = clients; client; client = client->next)
	{
		if (!client->dynamic) continue;
		if (ast_tvzero(client->lastdyntime)) continue;
		if (ast_tvdiff_ms(tv,client->lastdyntime) > dyntime)
		{
			if (option_verbose >= 3) ast_verbose(VERBOSE_PREFIX_3 
				"DYN client %s past lease time\n",client->name);
			memset(&client->lastdyntime,0,sizeof(client->lastheardtime));
			memset(&client->sin,0,sizeof(client->sin));
		}
	}
	return;
}


/* Maintain a relative time source that is *not* dependent on system time of day */

static void *voter_timer(void *data)
{
	char buf[FRAME_SIZE];
	int	i;
	time_t	t;
	struct voter_pvt *p;
	struct voter_client *client,*client1;
	struct timeval tv;

	while(run_forever && (!ast_shutting_down()))
	{
		i = read(voter_timing_fd,buf,sizeof(buf));
		if (i != FRAME_SIZE)
		{
			ast_log(LOG_ERROR,"error in read() for voter timer\n");
			pthread_exit(NULL);
		}
		ast_mutex_lock(&voter_lock);
		time(&t);
		if (!hasmaster) master_time.vtime_sec = (uint32_t) t;
		voter_timing_count++;
		if (!hasmaster)
		{
			for(p = pvts; p; p = p->next)
			{
				memset(p->buf + AST_FRIENDLY_OFFSET,0xff,FRAME_SIZE);
				voter_mix_and_send(p,NULL,0);
			}
			voter_xmit_master();
			gettimeofday(&tv,NULL);
			for(client = clients; client; client = client->next)
			{
				if (!ast_tvzero(client->lastheardtime) && (ast_tvdiff_ms(tv,client->lastheardtime) > CLIENT_TIMEOUT_MS))
				{
					if (option_verbose >= 3) ast_verbose(VERBOSE_PREFIX_3 "Voter client %s disconnect (timeout)\n",client->name);
					client->heardfrom = 0;
					client->respdigest = 0;
					client->lastheardtime.tv_sec = client->lastheardtime.tv_usec = 0;
				}
			}
			if (check_client_sanity)
			{
				for(client = clients; client; client = client->next)
				{
					if (!client->respdigest) continue;
					for(client1 = client->next; client1; client1 = client1->next)
					{
						if ((client1->sin.sin_addr.s_addr == client->sin.sin_addr.s_addr) &&
							(client1->sin.sin_port == client->sin.sin_port))
						{
							if (!client1->respdigest) continue;
							client->respdigest = 0;
							client->heardfrom = 0;
							client1->respdigest = 0;
							client1->heardfrom = 0;
						}
					}
				}
			}
		}
		ast_mutex_unlock(&voter_lock);
	}
	return(NULL);
}	

static void *voter_reader(void *data)
{
 	char buf[4096],timestr[100],hasmastered,*cp,*cp1;
	char gps0[300],gps1[300],gps2[300];
	struct sockaddr_in sin,sin_stream;
	struct voter_pvt *p;
	int i,j,k,ms,maxrssi,master_port;
	struct ast_frame *f1,fr;
        socklen_t fromlen;
	ssize_t recvlen;
	struct timeval tv,timetv;
	FILE *gpsfp;
	struct voter_client *client,*client1,*maxclient;
	VOTER_PACKET_HEADER *vph;
	VOTER_GPS *vgp;
	VOTER_REC rec;
	VOTER_STREAM stream;
	time_t timestuff,t;
	unsigned long my_voter_time;
	short  silbuf[FRAME_SIZE];
#ifdef	ADPCM_LOOPBACK
#pragma pack(push)
#pragma pack(1)
	struct {
		VOTER_PACKET_HEADER vp;
		char rssi;
		char audio[FRAME_SIZE + 3];
	} audiopacket;
#pragma pack(pop)
#endif
	struct {
		VOTER_PACKET_HEADER vp;
		char flags;
	} authpacket;

	if (option_verbose > 2) ast_verbose(VERBOSE_PREFIX_3 "voter: reader thread started.\n");
	ast_mutex_lock(&voter_lock);
	master_port = 0;
	while(run_forever && (!ast_shutting_down()))
	{
		my_voter_time = voter_timing_count;
		ast_mutex_unlock(&voter_lock);
		ms = 50;
		i = ast_waitfor_n_fd(&udp_socket, 1, &ms,NULL);
		ast_mutex_lock(&voter_lock);
		if (i == -1)
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
				AST_FRAME_DATA(fr) =  0;
				fr.src = type;
				fr.offset = 0;
				fr.mallocd=0;
				fr.delivery.tv_sec = 0;
				fr.delivery.tv_usec = 0;
				ast_queue_frame(p->owner,&fr);
				p->rxkey = 0;
				p->lastwon = NULL;
			}
		}
		if (i < 0) continue;
		if (i == udp_socket) /* if we get a packet */
		{
			fromlen = sizeof(struct sockaddr_in);
			recvlen = recvfrom(udp_socket,buf,sizeof(buf) - 1,0,
				(struct sockaddr *)&sin,&fromlen);
			if (recvlen >= sizeof(VOTER_PACKET_HEADER)) /* if set got something worthwile */
			{
				vph = (VOTER_PACKET_HEADER *)buf;
				if (debug > 3) ast_verbose("Got rx packet, len %d payload %d challenge %s digest %08x\n",(int)recvlen,ntohs(vph->payload_type),vph->challenge,ntohl(vph->digest));
				client = NULL;
				if ((!check_client_sanity) && master_port) sin.sin_port = htons(master_port);
				if (vph->digest)
				{
					gettimeofday(&tv,NULL);
					/* first see if client is not a dynamic one */
					for(client = clients; client; client = client->next)
					{
						if (client->dynamic) continue;
						if (client->digest == htonl(vph->digest)) break;
					}
					/* if not found as non-dynamic, try it as existing dynamic */					
					if (!client)
					{
						for(client = clients; client; client = client->next)
						{
							if (!client->dynamic) continue;
							if (ast_tvzero(client->lastdyntime)) continue;
							if (ast_tvdiff_ms(tv,client->lastdyntime) > dyntime)
							{
								if (option_verbose >= 3) ast_verbose(VERBOSE_PREFIX_3 
									"DYN client %s past lease time\n",client->name);
								memset(&client->lastdyntime,0,sizeof(client->lastheardtime));
								memset(&client->sin,0,sizeof(client->sin));
								continue;
							}
							if (client->digest != htonl(vph->digest)) continue;
							if (client->sin.sin_addr.s_addr != sin.sin_addr.s_addr) continue;
							if (client->sin.sin_port != sin.sin_port) continue;
							if (option_verbose > 4) ast_verbose(VERBOSE_PREFIX_3 
								"Using existing Dynamic client %s for %s:%d\n",client->name,ast_inet_ntoa(sin.sin_addr),ntohs(sin.sin_port));
							break;
						}
					}
					/* if still now found, try as new dynamic */
					if (!client)
					{
						for(client = clients; client; client = client->next)
						{
							if (!client->dynamic) continue;
							if (!ast_tvzero(client->lastdyntime)) continue;
							if (client->digest != htonl(vph->digest)) continue;
							/* okay, we found an empty dynamic slot with proper digest */
							gettimeofday(&client->lastdyntime,NULL);
							client->sin = sin;
							if (option_verbose >= 3) ast_verbose(VERBOSE_PREFIX_3 
								"Bound new Dynamic client %s to %s:%d\n",client->name,ast_inet_ntoa(sin.sin_addr),ntohs(sin.sin_port));
							break;
						}
					}
					if ((debug >= 3) && client && (!client->ismaster) && ntohs(vph->payload_type) == VOTER_PAYLOAD_ULAW)
					{
						timestuff = (time_t) ntohl(vph->curtime.vtime_sec);
						strftime(timestr,sizeof(timestr) - 1,"%D %T",localtime((time_t *)&timestuff));
						ast_verbose("Time:      %s.%03d, (%s) RSSI: %d\n",timestr,ntohl(vph->curtime.vtime_nsec) / 1000000,client->name,(unsigned char)*(buf + sizeof(VOTER_PACKET_HEADER)));
					}
					if (client)
					{
						if (check_client_sanity)
						{
							if ((client->sin.sin_addr.s_addr && (client->sin.sin_addr.s_addr != sin.sin_addr.s_addr)) ||
								(client->sin.sin_port && (client->sin.sin_port != sin.sin_port))) client->heardfrom = 0;
						} 
						gettimeofday(&client->lastdyntime,NULL);
						client->respdigest = crc32_bufs((char*)vph->challenge,password);
						client->sin = sin;
						if ((!client->ismaster) && hasmaster)
						{
							if (last_master_count && (voter_timing_count > (last_master_count + MAX_MASTER_COUNT)))
							{
								ast_log(LOG_NOTICE,"Voter lost master timing source!!\n");
								last_master_count = 0;
								master_time.vtime_sec = 0;
								for(client1 = client->next; client1; client1 = client1->next)
								{
									memset(client1->audio,0xff,client1->buflen);
									memset(client1->rssi,0,client1->buflen);
								}
								for(p = pvts; p; p = p->next)
								{
									if (p->rxkey)
									{
										memset(&fr,0,sizeof(fr));
										fr.datalen = 0;
										fr.samples = 0;
										fr.frametype = AST_FRAME_CONTROL;
										fr.subclass = AST_CONTROL_RADIO_UNKEY;
										AST_FRAME_DATA(fr) =  0;
										fr.src = type;
										fr.offset = 0;
										fr.mallocd=0;
										fr.delivery.tv_sec = 0;
										fr.delivery.tv_usec = 0;
										ast_queue_frame(p->owner,&fr);
									}
									p->lastwon = NULL;
									p->rxkey = 0;
									ast_mutex_lock(&p->txqlock);
									while((f1 = AST_LIST_REMOVE_HEAD(&p->txq,frame_list)) != NULL) ast_frfree(f1);
									ast_mutex_unlock(&p->txqlock);
								}
								continue;
							}
							if (!master_time.vtime_sec) continue;
						}
					}
					/* if we know the dude, find the connection his audio belongs to and send it there */
					if (client && client->heardfrom  && 
					    (((ntohs(vph->payload_type) == VOTER_PAYLOAD_ULAW) && 
						(recvlen == (sizeof(VOTER_PACKET_HEADER) + FRAME_SIZE + 1))) ||
					    ((ntohs(vph->payload_type) == VOTER_PAYLOAD_ADPCM) && 
						(recvlen == (sizeof(VOTER_PACKET_HEADER) + FRAME_SIZE + 4))) ||
					    ((ntohs(vph->payload_type) == VOTER_PAYLOAD_NULAW) && 
						(recvlen == (sizeof(VOTER_PACKET_HEADER) + FRAME_SIZE + 1)))))
					{
						for(p = pvts; p; p = p->next)
						{
							if (p->nodenum == client->nodenum) break;
						}
						if (p) /* if we found 'em */
						{
							long long btime,ptime,difftime;
							int index,flen;

							gettimeofday(&client->lastheardtime,NULL);
							if (client->ismaster)
							{

								if (!master_time.vtime_sec)
								{
									for(p = pvts; p; p = p->next)
									{
										ast_mutex_lock(&p->txqlock);
										while((f1 = AST_LIST_REMOVE_HEAD(&p->txq,frame_list)) != NULL) ast_frfree(f1);
										ast_mutex_unlock(&p->txqlock);
									}
								}
								last_master_count = voter_timing_count;
								master_time.vtime_sec = ntohl(vph->curtime.vtime_sec);
								master_time.vtime_nsec = ntohl(vph->curtime.vtime_nsec);
								if (!master_port) master_port = ntohs(sin.sin_port);
							}
							else
							{
								if (!master_time.vtime_sec) continue;
							}
							if (client->mix)
							{
								if (ntohl(vph->curtime.vtime_nsec) > client->rxseqno)
								{
									client->rxseqno = 0;
									client->rxseqno_40ms = 0;
									client->rxseq40ms = 0;
									client->drain40ms = 0;
								}
								if (client->txseqno > (client->txseqno_rxkeyed + 4))
								{
									client->rxseqno = 0;
									client->rxseqno_40ms = 0;
									client->rxseq40ms = 0;
									client->drain40ms = 0;
								}
								client->txseqno_rxkeyed = client->txseqno;
								if  (!client->rxseqno) 	client->rxseqno_40ms = client->rxseqno = ntohl(vph->curtime.vtime_nsec);
								if ((!client->doadpcm) && (!client->donulaw))
									index = ntohl(vph->curtime.vtime_nsec) - client->rxseqno;
								else
									index = ntohl(vph->curtime.vtime_nsec) - client->rxseqno_40ms;
								index *= FRAME_SIZE;
								index += BUFDELAY(client);
								index -= (FRAME_SIZE * 4);
								if (debug >= 3) 
								{
									if ((!client->doadpcm) && (!client->donulaw))
										ast_verbose("mix client (Mulaw) %s index: %d their seq: %d our seq: %d\n",
											client->name,index,ntohl(vph->curtime.vtime_nsec),client->rxseqno);
									else
										ast_verbose("mix client (ADPCM/Nulaw) %s index: %d their seq: %d our seq: %d\n",
											client->name,index,ntohl(vph->curtime.vtime_nsec),client->rxseqno_40ms);
								}
							}
							else
							{
								btime = ((long long)master_time.vtime_sec * 1000000000LL) + master_time.vtime_nsec;
								btime += 40000000;
								ptime = ((long long)ntohl(vph->curtime.vtime_sec) * 1000000000LL) + ntohl(vph->curtime.vtime_nsec);
								difftime = (ptime - btime) + (BUFDELAY(client) * 125000LL);
								difftime -= puckoffset(client);
								index = (int)((long long)difftime / 125000LL);
								if ((debug >= 3) && (!client->ismaster))
								{
									timestuff = (time_t) master_time.vtime_sec;
									strftime(timestr,sizeof(timestr) - 1,"%D %T",localtime((time_t *)&timestuff));
									ast_verbose("DrainTime: %s.%03d\n",timestr,master_time.vtime_nsec / 1000000);
									gettimeofday(&timetv,NULL);
									timestuff = (time_t) timetv.tv_sec;
									strftime(timestr,sizeof(timestr) - 1,"%D %T",localtime((time_t *)&timestuff));
									ast_verbose("SysTime:   %s.%03d, diff: %lld,index: %d\n",timestr,(int)timetv.tv_usec / 1000,btime - ptime,index);
								}
							}
							/* if in bounds */
							if ((index > 0) && (index < (client->buflen - (FRAME_SIZE * 2))))
							{

								f1 = NULL;
								/* if no RSSI, just make it quiet */
								if (!buf[sizeof(VOTER_PACKET_HEADER)])
								{
									for(i = 0; i < FRAME_SIZE; i++) 
										buf[sizeof(VOTER_PACKET_HEADER) + i + 1] = 0xff;
								}
								/* if otherwise (RSSI > 0), if ADPCM, translate it */
								else if (ntohs(vph->payload_type) == VOTER_PAYLOAD_ADPCM)
								{

#ifdef	ADPCM_LOOPBACK
									memset(&audiopacket,0,sizeof(audiopacket));
									strcpy((char *)audiopacket.vp.challenge,challenge);
									audiopacket.vp.payload_type = htons(3);
									audiopacket.rssi = 0;
									memcpy(audiopacket.audio, buf + sizeof(VOTER_PACKET_HEADER) + 1,FRAME_SIZE + 3);
									audiopacket.vp.curtime.vtime_sec = htonl(master_time.vtime_sec);
									audiopacket.vp.curtime.vtime_nsec = htonl(master_time.vtime_nsec);
									audiopacket.vp.digest = htonl(client->respdigest);
									sendto(udp_socket, &audiopacket, sizeof(audiopacket),0,(struct sockaddr *)&client->sin,sizeof(client->sin));
#endif

									memset(&fr,0,sizeof(struct ast_frame));
								        fr.frametype = AST_FRAME_VOICE;
								        fr.subclass = AST_FORMAT_ADPCM;
								        fr.datalen = ADPCM_FRAME_SIZE;
								        fr.samples = FRAME_SIZE * 2;
								        AST_FRAME_DATA(fr) =  buf + sizeof(VOTER_PACKET_HEADER) + 1;
								        fr.src = type;
								        fr.offset = 0;
								        fr.mallocd = 0;
								        fr.delivery.tv_sec = 0;
								        fr.delivery.tv_usec = 0;
									f1 = ast_translate(p->adpcmin,&fr,0);
								}
								/* if otherwise (RSSI > 0), if NULAW, translate it */
								else if (ntohs(vph->payload_type) == VOTER_PAYLOAD_NULAW)
								{

									short s,xbuf[FRAME_SIZE * 2];
#ifdef	NULAW_LOOPBACK
									memset(&audiopacket,0,sizeof(audiopacket));
									strcpy((char *)audiopacket.vp.challenge,challenge);
									audiopacket.vp.payload_type = htons(4);
									audiopacket.rssi = 0;
									memcpy(audiopacket.audio, buf + sizeof(VOTER_PACKET_HEADER) + 1,FRAME_SIZE);
									audiopacket.vp.curtime.vtime_sec = htonl(master_time.vtime_sec);
									audiopacket.vp.curtime.vtime_nsec = htonl(master_time.vtime_nsec);
									audiopacket.vp.digest = htonl(client->respdigest);
									sendto(udp_socket, &audiopacket, sizeof(audiopacket),0,(struct sockaddr *)&client->sin,sizeof(client->sin));
#endif

									for(i = 0; i < FRAME_SIZE * 2; i += 2)
									{
 										s = (AST_MULAW((int)(unsigned char)
											buf[sizeof(VOTER_PACKET_HEADER) + 1 + (i >> 1)])) / 2;
										xbuf[i] = lpass4(s,p->rlpx,p->rlpy);
										xbuf[i + 1] = lpass4(s,p->rlpx,p->rlpy);
									}
									memset(&fr,0,sizeof(struct ast_frame));
								        fr.frametype = AST_FRAME_VOICE;
								        fr.subclass = AST_FORMAT_SLINEAR;
								        fr.datalen = FRAME_SIZE * 4;
								        fr.samples = FRAME_SIZE * 2;
								        AST_FRAME_DATA(fr) = xbuf;
								        fr.src = type;
								        fr.offset = 0;
								        fr.mallocd = 0;
								        fr.delivery.tv_sec = 0;
								        fr.delivery.tv_usec = 0;
									f1 = ast_translate(p->nuin,&fr,0);
								}
								if ((!client->doadpcm) && (!client->donulaw))
									index = (index + client->drainindex) % client->buflen;
								else
									index = (index + client->drainindex_40ms) % client->buflen;
								flen = (f1) ? f1->datalen : FRAME_SIZE;
								i = (int)client->buflen - (index + flen);
								if (i >= 0)
								{
									memcpy(client->audio + index,
										((f1) ? AST_FRAME_DATAP(f1) : buf + sizeof(VOTER_PACKET_HEADER) + 1),flen);
									memset(client->rssi + index,buf[sizeof(VOTER_PACKET_HEADER)],flen);
								}
								else
								{
									memcpy(client->audio + index,
										((f1) ? AST_FRAME_DATAP(f1) : buf + sizeof(VOTER_PACKET_HEADER) + 1),flen + i);
									memset(client->rssi + index,buf[sizeof(VOTER_PACKET_HEADER)],flen + i);
									memcpy(client->audio,
										((f1) ? AST_FRAME_DATAP(f1) : buf + sizeof(VOTER_PACKET_HEADER) + 1) + (flen + i),-i);
									memset(client->rssi,buf[sizeof(VOTER_PACKET_HEADER)],-i);
								}
								if (f1) ast_frfree(f1);
                                                        } 
							else if (client->mix)
							{
								client->rxseqno = 0;
								client->rxseqno_40ms = 0;
								client->rxseq40ms = 0;
								client->drain40ms = 0;
								if (debug >= 3) ast_verbose("mix client %s outa bounds, resetting!!\n",client->name);
                                                        }
							if (client->ismaster)
							{
								gettimeofday(&tv,NULL);
								for(client = clients; client; client = client->next)
								{
									if (!ast_tvzero(client->lastheardtime) && (ast_tvdiff_ms(tv,client->lastheardtime) > CLIENT_TIMEOUT_MS))
									{
										if (option_verbose >= 3) ast_verbose(VERBOSE_PREFIX_3 "Voter client %s disconnect (timeout)\n",client->name);
										client->heardfrom = 0;
										client->respdigest = 0;
									}
									if (!client->heardfrom) client->lastheardtime.tv_sec = client->lastheardtime.tv_usec = 0;
								}
								if (check_client_sanity)
								{
									for(client = clients; client; client = client->next)
									{
										if (!client->respdigest) continue;
										for(client1 = client->next; client1; client1 = client1->next)
										{
											if ((client1->sin.sin_addr.s_addr == client->sin.sin_addr.s_addr) &&
												(client1->sin.sin_port == client->sin.sin_port))
											{
												if (!client1->respdigest) continue;
												client->respdigest = 0;
												client->heardfrom = 0;
												client1->respdigest = 0;
												client1->heardfrom = 0;
											}
										}
									}
								}
								hasmastered = 0;
								voter_xmit_master();
								for(p = pvts; p; p = p->next)
								{
									char startagain;

									startagain = 0;
									maxrssi = 0;
									maxclient = NULL;
									for(client = clients; client; client = (startagain) ? clients : client->next)
									{
										int maxprio,thisprio;

										startagain = 0;
										if (client->nodenum != p->nodenum) continue;
										if (client->mix) continue;
										if (client->prio_override == -1) continue;
										k = 0;
										i = (int)client->buflen - ((int)client->drainindex + FRAME_SIZE);
										if (i >= 0)
										{
											for(j = client->drainindex; j < client->drainindex + FRAME_SIZE; j++)
											{
												k += client->rssi[j];
											}
										}
										else
										{
											for(j = client->drainindex; j < client->drainindex + (FRAME_SIZE + i); j++)
											{
												k += client->rssi[j];
											}
											for(j = 0; j < -i; j++)
											{
												k += client->rssi[j];
											}
										}			
										client->lastrssi = k / FRAME_SIZE; 
										maxprio = thisprio = 0;
										if (maxclient)
										{
											if (maxclient->prio_override > -2) 
												maxprio = maxclient->prio_override;
											else
												maxprio = maxclient->prio;
										}
										if (client->prio_override > -2)
											thisprio = client->prio_override;
										else
											thisprio = client->prio;
										if (((client->lastrssi > maxrssi) && (thisprio == maxprio))
											 || (client->lastrssi && (thisprio > maxprio)))
										{
											maxrssi =  client->lastrssi;
											maxclient = client;
											if (thisprio > maxprio) startagain = 1;
										}
									}
									for(client = clients; client; client =  client->next)
									{
										if (client->nodenum != p->nodenum) continue;
										if (client->mix) continue;
										if (client->prio_override == -1) continue;
										i = (int)client->buflen - ((int)client->drainindex + FRAME_SIZE);
										if (i >= 0)
										{
											for(j = client->drainindex; j < client->drainindex + FRAME_SIZE; j++)
											{
												client->rssi[j] = 0;
											}
										}
										else
										{
											for(j = client->drainindex; j < client->drainindex + (FRAME_SIZE + i); j++)
											{
												client->rssi[j] = 0;
											}
											for(j = 0; j < -i; j++)
											{
												client->rssi[j] = 0;
											}
										}			
									}
									if (!maxclient) maxrssi = 0;
									memset(p->buf + AST_FRIENDLY_OFFSET,0xff,FRAME_SIZE);
									if (maxclient)
									{
										int maxprio,lastprio;

										if (maxclient->prio_override > -2) 
											maxprio = maxclient->prio_override;
										else
											maxprio = maxclient->prio;
										lastprio = 0;
										if (p->lastwon)
										{
											if (p->lastwon->prio_override > -2)
												lastprio = p->lastwon->prio_override;
											else
												lastprio = p->lastwon->prio;
										}
										/* if not on same client, and we have thresholds, and priority appropriate */
										if (p->lastwon && p->nthresholds && (maxprio <= lastprio))
										{
											/* go thru all the thresholds */
											for(i = 0; i < p->nthresholds; i++)
											{
												/* if meets criteria */
												if (p->lastwon->lastrssi >= p->rssi_thresh[i])
												{
													/* if not at same threshold, change to new one */
													if ((i + 1) != p->threshold)
													{
														p->threshold = i + 1;
														p->threshcount = 0;
														if (debug >= 3) ast_verbose("New threshold %d, client %s, rssi %d\n",p->threshold,p->lastwon->name,p->lastwon->lastrssi);
													} 
												 	/* at the same threshold still, if count is enabled and is met */
													else if (p->count_thresh[i] && (p->threshcount++ >= p->count_thresh[i]))
													{
														if (debug >= 3) ast_verbose("Threshold %d time (%d) excedded, client %s, rssi %d\n",p->threshold,p->count_thresh[i],p->lastwon->name,p->lastwon->lastrssi);
														p->threshold = 0;
														p->threshcount = 0;
														p->lingercount = 0;
														continue;
													}
													p->lingercount = 0;
													maxclient = p->lastwon;
													maxrssi = maxclient->lastrssi;
													break;
												}
												/* if doesnt match any criteria */
												if (i == (p->nthresholds - 1))
												{
													if ((debug >= 3) && p->threshold) ast_verbose("Nothing matches criteria any more\n");
													if (p->threshold) p->lingercount = p->linger_thresh[p->threshold - 1];
													p->threshold = 0;
													p->threshcount = 0;
												}
											}
										}
										if (p->lingercount) 
										{
											if (debug >= 3) ast_verbose("Lingering on client %s, rssi %d, Maxclient is %s, rssi %d\n",p->lastwon->name,p->lastwon->lastrssi,maxclient->name,maxrssi);
											p->lingercount--;
											maxclient = p->lastwon;
											maxrssi = maxclient->lastrssi;
										}
										if (p->voter_test > 0) /* perform cyclic selection */
										{
											/* see how many are eligible */
											for(i = 0,client = clients; client; client = client->next)
											{
												if (client->nodenum != p->nodenum) continue;
												if (client->mix) continue;
												if (client->lastrssi == maxrssi) i++;
											}
											if (p->voter_test == 1)
											{
												p->testindex = random() % i;
											}
											else
											{
												p->testcycle++;
												if (p->testcycle >= (p->voter_test - 1))
												{
													p->testcycle = 0;
													p->testindex++;
													if (p->testindex >= i) p->testindex = 0;
												}
											}
											for(i = 0,client = clients; client; client = client->next)
											{
												if (client->nodenum != p->nodenum) continue;
												if (client->mix) continue;
												if (client->lastrssi != maxrssi) continue;
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
											memset(silbuf,0,sizeof(silbuf));
											memset(&fr,0,sizeof(struct ast_frame));
										        fr.frametype = AST_FRAME_VOICE;
										        fr.subclass = AST_FORMAT_SLINEAR;
										        fr.datalen = FRAME_SIZE * 2;
										        fr.samples = FRAME_SIZE;
										        AST_FRAME_DATA(fr) =  silbuf;
										        fr.src = type;
										        fr.offset = 0;
										        fr.mallocd = 0;
										        fr.delivery.tv_sec = 0;
										        fr.delivery.tv_usec = 0;
											p->threshold = 0;
											p->threshcount = 0;
											p->lingercount = 0;
											p->winner = 0;
											incr_drainindex(p);
											ast_queue_frame(p->owner,&fr);
											continue;
										}
										i = (int)maxclient->buflen - ((int)maxclient->drainindex + FRAME_SIZE);
										if (i >= 0)
										{
											memcpy(p->buf + AST_FRIENDLY_OFFSET,maxclient->audio + maxclient->drainindex,FRAME_SIZE);
										}
										else
										{
											memcpy(p->buf + AST_FRIENDLY_OFFSET,maxclient->audio + maxclient->drainindex,FRAME_SIZE + i);
											memcpy(p->buf + AST_FRIENDLY_OFFSET + (maxclient->buflen - i),maxclient->audio,-i);
										}
										for(client = clients; client; client = client->next)
										{
											if (client->nodenum != p->nodenum) continue;
											if (client->mix) continue;
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
													memcpy(rec.audio,client->audio + client->drainindex,FRAME_SIZE);
												}
												else
												{
													memcpy(rec.audio,client->audio + client->drainindex,FRAME_SIZE + i);
													memset(client->audio + client->drainindex,0xff,FRAME_SIZE + i);
													memcpy(rec.audio + FRAME_SIZE + i,client->audio,-i);
													memset(client->audio + client->drainindex,0xff,FRAME_SIZE + i);
												}
												fwrite(&rec,1,sizeof(rec),recfp);
											}
											if (i >= 0)
											{
												memset(client->audio + client->drainindex,0xff,FRAME_SIZE);
											}
											else
											{
												memset(client->audio + client->drainindex,0xff,FRAME_SIZE + i);
												memset(client->audio,0xff,-i);
											}
										}
										if ((!p->duplex) && p->txkey)
										{
											p->rxkey = 0;
											p->lastwon = NULL;
											memset(silbuf,0,sizeof(silbuf));
											memset(&fr,0,sizeof(struct ast_frame));
										        fr.frametype = AST_FRAME_VOICE;
										        fr.subclass = AST_FORMAT_SLINEAR;
										        fr.datalen = FRAME_SIZE * 2;
										        fr.samples = FRAME_SIZE;
										        AST_FRAME_DATA(fr) =  silbuf;
										        fr.src = type;
										        fr.offset = 0;
										        fr.mallocd = 0;
										        fr.delivery.tv_sec = 0;
										        fr.delivery.tv_usec = 0;
											p->threshold = 0;
											p->threshcount = 0;
											p->lingercount = 0;
											p->winner = 0;
											incr_drainindex(p);
											ast_queue_frame(p->owner,&fr);
											continue;
										}
										if (p->plfilter) 
										{
											for(i = 0; i < FRAME_SIZE; i++)
											{
												j = p->buf[AST_FRIENDLY_OFFSET + i] & 0xff;
												p->buf[AST_FRIENDLY_OFFSET + i] = 
													AST_LIN2MU(hpass6(AST_MULAW(j),p->hpx,p->hpy));
											}
										}
										stream.curtime = master_time;
										memcpy(stream.audio,p->buf + AST_FRIENDLY_OFFSET,FRAME_SIZE);
										sprintf(stream.str,"%s",maxclient->name);
										for(client = clients; client; client = client->next)
										{
											if (client->nodenum != p->nodenum) continue;
											sprintf(stream.str + strlen(stream.str),",%s=%d",client->name,client->lastrssi);
										}
										for(i = 0; i < p->nstreams; i++)
										{
											cp = ast_strdup(p->streams[i]);
											if (!cp)
											{
												ast_log(LOG_NOTICE,"Malloc() failed!!\n");
												break;
											}
											cp1 = strchr(cp,':');
											if (cp1)
											{
												*cp1 = 0;
												j = atoi(cp1 + 1);
											} else j = listen_port;
											sin_stream.sin_family = AF_INET;
											sin_stream.sin_addr.s_addr = inet_addr(cp);
											sin_stream.sin_port = htons(j);
											sendto(udp_socket, &stream, sizeof(stream),0,(struct sockaddr *)&sin_stream,sizeof(sin_stream));
											ast_free(cp);
										}
										if (maxclient != p->lastwon)
										{
											p->lastwon = maxclient;
											if (debug > 0)
												ast_verbose("Voter client %s selected for node %d\n",maxclient->name,p->nodenum);
											memset(&fr,0,sizeof(fr));
											fr.datalen = strlen(maxclient->name) + 1;
											fr.samples = 0;
											fr.frametype = AST_FRAME_TEXT;
											fr.subclass = 0;
											AST_FRAME_DATA(fr) =  maxclient->name;
											fr.src = type;
											fr.offset = 0;
											fr.mallocd=0;
											fr.delivery.tv_sec = 0;
											fr.delivery.tv_usec = 0;
											ast_queue_frame(p->owner,&fr);
										}
										if (debug > 1) ast_verbose("Sending from client %s RSSI %d\n",maxclient->name,maxrssi);
									}
									if ((!p->duplex) && p->txkey)
									{
										p->rxkey = 0;
										p->lastwon = NULL;
										memset(silbuf,0,sizeof(silbuf));
										memset(&fr,0,sizeof(struct ast_frame));
									        fr.frametype = AST_FRAME_VOICE;
									        fr.subclass = AST_FORMAT_SLINEAR;
									        fr.datalen = FRAME_SIZE * 2;
									        fr.samples = FRAME_SIZE;
									        AST_FRAME_DATA(fr) =  silbuf;
									        fr.src = type;
									        fr.offset = 0;
									        fr.mallocd = 0;
									        fr.delivery.tv_sec = 0;
									        fr.delivery.tv_usec = 0;
										p->threshold = 0;
										p->threshcount = 0;
										p->lingercount = 0;
										p->winner = 0;
										incr_drainindex(p);
										ast_queue_frame(p->owner,&fr);
										continue;
									}
									if (!voter_mix_and_send(p,maxclient,maxrssi)) continue;
								}
							}
						}
						else
						{
							if (debug > 1) ast_verbose("Request for voter client %s to unknown node %d\n",
								client->name,client->nodenum);
						}
						continue;
					} 
					/* if we know the dude, find the connection his audio belongs to and send it there */
					if (client && client->heardfrom && (ntohs(vph->payload_type) == VOTER_PAYLOAD_GPS) && 
					    ((recvlen == sizeof(VOTER_PACKET_HEADER)) ||
					       (recvlen == (sizeof(VOTER_PACKET_HEADER) + sizeof(VOTER_GPS))) ||
						(recvlen == ((sizeof(VOTER_PACKET_HEADER) + sizeof(VOTER_GPS)) - 1))))

					{
						gettimeofday(&client->lastheardtime,NULL);
						client->lastgpstime.vtime_sec = ntohl(vph->curtime.vtime_sec);
						client->lastgpstime.vtime_nsec = ntohl(vph->curtime.vtime_nsec);
						if (client->ismaster)
						{
							mastergps_time.vtime_sec = ntohl(vph->curtime.vtime_sec);
							mastergps_time.vtime_nsec = ntohl(vph->curtime.vtime_nsec);
						}
						client->lastmastergpstime.vtime_sec = mastergps_time.vtime_sec;
						client->lastmastergpstime.vtime_nsec = mastergps_time.vtime_nsec;
						if (debug >= 3) 
						{
							gettimeofday(&timetv,NULL);
							timestuff = (time_t) ntohl(vph->curtime.vtime_sec);
							strftime(timestr,sizeof(timestr) - 1,"%D %T",localtime((time_t *)&timestuff));
	

							ast_verbose("GPSTime (%s):   %s.%09d\n",client->name,timestr,ntohl(vph->curtime.vtime_nsec));
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
						if (recvlen == sizeof(VOTER_PACKET_HEADER))
						{
							if (debug > 1) ast_verbose("Got GPS Keepalive from (%s)\n",
								client->name);
						}
						else
						{
							vgp = (VOTER_GPS *)(buf + sizeof(VOTER_PACKET_HEADER));
							if (client->gpsid)
							{
								snprintf(gps1,sizeof(gps1) - 1,GPS_WORK_FILE,client->gpsid);
								snprintf(gps2,sizeof(gps2) - 1,GPS_DATA_FILE,client->gpsid);
								gpsfp = fopen(gps1,"w");
								if (!gpsfp)
								{
									ast_log(LOG_ERROR,"Unable to open GPS work file %s!!\n",gps1);
									continue;
								}
								time(&t);
								fprintf(gpsfp,"%u %s %s %sM\n",(unsigned int) t,vgp->lat,vgp->lon,vgp->elev);
								fclose(gpsfp);
								snprintf(gps0,sizeof(gps0) - 1,"/bin/mv %s %s > /dev/null 2>&1",gps1,gps2);
								ast_safe_system(gps0);
							}
							if (debug > 1) ast_verbose("Got GPS (%s): Lat: %s, Lon: %s, Elev: %s\n",
								client->name,vgp->lat,vgp->lon,vgp->elev);
						}
						continue;
					}
					if (client) client->heardfrom = 1;
				}
				/* otherwise, we just need to send an empty packet to the dude */
				memset(&authpacket,0,sizeof(authpacket));
				if (client)
				{
					client->txseqno = 0;
					client->txseqno_rxkeyed = 0;
					client->rxseqno = 0;
					client->rxseqno_40ms = 0;
					client->rxseq40ms = 0;
					client->drain40ms = 0;
				}
				strcpy((char *)authpacket.vp.challenge,challenge);
				gettimeofday(&tv,NULL);
				authpacket.vp.curtime.vtime_sec = htonl(tv.tv_sec);
				authpacket.vp.curtime.vtime_nsec = htonl(tv.tv_usec * 1000);
				/* make our digest based on their challenge */
				authpacket.vp.digest = htonl(crc32_bufs((char*)vph->challenge,password));
				authpacket.flags = 0;
				if (client)
				{
					client->mix = 0;
					/* if client is sending options */
  					if ((recvlen > sizeof(VOTER_PACKET_HEADER)) && (!vph->payload_type))
					{
						if (buf[sizeof(VOTER_PACKET_HEADER)] & 32) client->mix = 1;
					}
					if ((!client->mix) && (!hasmaster))
					{
						time(&t);
						if (t >= (client->warntime + CLIENT_WARN_SECS))
						{
							client->warntime = t;
							ast_log(LOG_WARNING,"Voter client %s attempting to authenticate as GPS-timing-based with no master timing source defined!!\n",
								client->name);
						}
						authpacket.vp.digest = 0;
						client->heardfrom = 0;
						client->respdigest = 0;
					}
					else
					{
						if (client->ismaster) authpacket.flags |= 2 | 8;
						if (client->doadpcm) authpacket.flags |= 16;
						if (client->mix) authpacket.flags |= 32;
						if (client->nodeemp) authpacket.flags |= 1;
						if (client->noplfilter) authpacket.flags |= 4;
					}
				}
				if (debug > 1) ast_verbose("sending packet challenge %s digest %08x password %s\n",authpacket.vp.challenge,ntohl(authpacket.vp.digest),password);
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

#ifdef	OLD_ASTERISK
int reload()
#else
static int reload(void)
#endif
{
#ifdef  NEW_ASTERISK
        struct ast_flags zeroflag = {0};
#endif
	unsigned int mynode;
	int i,n,instance_buflen,buflen;
	char *val,*ctg,*cp,*cp1,*cp2,*strs[40],newclient,data[100],oldctcss[100];
	struct voter_pvt *p;
	struct voter_client *client,*client1;
	struct ast_config *cfg = NULL;
	struct ast_variable *v;

	
	ast_mutex_lock(&voter_lock);
	for(client = clients; client; client = client->next)
	{
		client->reload = 0;
		client->old_buflen = client->buflen;
	}

#ifdef  NEW_ASTERISK
        if (!(cfg = ast_config_load(config,zeroflag))) {
#else
        if (!(cfg = ast_config_load(config))) {
#endif
                ast_log(LOG_ERROR, "Unable to load config %s\n", config);
		ast_mutex_unlock(&voter_lock);
		return -1;
        }

        val = (char *) ast_variable_retrieve(cfg,"general","password"); 
	if (val) ast_copy_string(password,val,sizeof(password) - 1);
	else password[0] = 0;

        val = (char *) ast_variable_retrieve(cfg,"general","context"); 
	if (val) ast_copy_string(context,val,sizeof(context) - 1);
	else context[0] = 0;

        val = (char *) ast_variable_retrieve(cfg,"general","buflen"); 
	if (val) buflen = strtoul(val,NULL,0) * 8; else buflen = DEFAULT_BUFLEN * 8;

        val = (char *) ast_variable_retrieve(cfg,"general","sanity"); 
	if (val) check_client_sanity = ast_true(val); else check_client_sanity = 1;

        val = (char *) ast_variable_retrieve(cfg,"general","puckit"); 
	if (val) puckit = ast_true(val); else puckit = 0;

        val = (char *) ast_variable_retrieve(cfg,"general","dyntime"); 
	if (val) dyntime = strtoul(val,NULL,0); else dyntime = DEFAULT_DYNTIME;

	if (buflen < (FRAME_SIZE * 2)) buflen = FRAME_SIZE * 2;

	for(p = pvts; p; p = p->next)
	{
		oldctcss[0] = 0;
		strcpy(oldctcss,p->txctcssfreq);
		sprintf(data,"%d",p->nodenum);
		if (ast_variable_browse(cfg, data) == NULL) continue;
	        val = (char *) ast_variable_retrieve(cfg,(char *)data,"linger"); 
		if (val) p->linger = atoi(val); else p->linger = DEFAULT_LINGER;
	        val = (char *) ast_variable_retrieve(cfg,(char *)data,"plfilter"); 
		if (val) p->plfilter = ast_true(val); else p->plfilter = 0;
	        val = (char *) ast_variable_retrieve(cfg,(char *)data,"duplex"); 
		if (val) p->duplex = ast_true(val); else p->duplex = 1;
	        val = (char *) ast_variable_retrieve(cfg,(char *)data,"streams"); 
		if (p->nstreams && p->streams[0]) ast_free(p->streams[0]);
		p->nstreams = 0;
		if (val)
		{
			cp = ast_strdup(val);
			p->nstreams = finddelim(cp,p->streams,MAXSTREAMS);
		}		
	        val = (char *) ast_variable_retrieve(cfg,(char *)data,"txctcss"); 
		if (val) ast_copy_string(p->txctcssfreq,val,sizeof(p->txctcssfreq)); else p->txctcssfreq[0] = 0;
	        val = (char *) ast_variable_retrieve(cfg,(char *)data,"txctcsslevel"); 
		if (val) p->txctcsslevel = atoi(val); else p->txctcsslevel = 62;
		p->txtoctype = TOC_NONE;
	        val = (char *) ast_variable_retrieve(cfg,(char *)data,"txtoctype"); 
		if (val)
		{
			if (!strcasecmp(val,"phase")) p->txtoctype = TOC_PHASE;
			else if (!strcasecmp(val,"notone")) p->txtoctype = TOC_NOTONE;
		}
		p->nthresholds = 0;
	        val = (char *) ast_variable_retrieve(cfg,(char *)data,"thresholds"); 
		if (val)
		{
			cp = ast_strdup(val);
			p->nthresholds = finddelim(cp,strs,MAXTHRESHOLDS);
			for(i = 0; i < p->nthresholds; i++)
			{
				cp1 = strchr(strs[i],'=');
				p->linger_thresh[i] = p->linger;
				if (cp1)
				{
					*cp1 = 0;
					cp2 = strchr(cp1 + 1,':');
					if (cp2)
					{
						*cp2 = 0;
						if (cp2[1]) p->linger_thresh[i] = (uint16_t)atoi(cp2 + 1);
					}
					if (cp1[1]) p->count_thresh[i] = (uint16_t)atoi(cp1 + 1);
				}
				p->rssi_thresh[i] = (uint8_t)atoi(strs[i]);
			}
			ast_free(cp);
		}		
		/* if new CTCSS freq */
		if (p->txctcssfreq[0] && strcmp(oldctcss,p->txctcssfreq))
		{
			t_pmr_chan tChan;

			memset(&tChan,0,sizeof(t_pmr_chan));

			tChan.pTxCodeDefault = p->txctcssfreq;
			tChan.pTxCodeSrc     = p->txctcssfreq;
			tChan.pRxCodeSrc     = p->txctcssfreq;
			tChan.txMod = 2;
			tChan.txMixA = TX_OUT_COMPOSITE;
			tChan.b.txboost = 1;
			if (p->pmrChan) destroyPmrChannel(p->pmrChan);
			p->pmrChan = createPmrChannel(&tChan,FRAME_SIZE);
			p->pmrChan->radioDuplex = 1;//o->radioduplex;
			p->pmrChan->b.loopback=0; 
			p->pmrChan->b.radioactive= 1;
			p->pmrChan->txrxblankingtime = 0;
			p->pmrChan->rxCpuSaver = 0;
			p->pmrChan->txCpuSaver = 0;
			*(p->pmrChan->prxSquelchAdjust) = 0;
			*(p->pmrChan->prxVoiceAdjust) = 0;
			*(p->pmrChan->prxCtcssAdjust) = 0;
			p->pmrChan->rxCtcss->relax = 0;
			p->pmrChan->txTocType = p->txtoctype;
			p->pmrChan->spsTxOutA->outputGain = 250;
			*p->pmrChan->ptxCtcssAdjust = p->txctcsslevel;
			p->pmrChan->pTxCodeDefault = p->txctcssfreq;
			p->pmrChan->pTxCodeSrc = p->txctcssfreq;
	//		p->pmrChan->txfreq = p->txctcssfreq;
		}
	}
	hasmaster = 0;
	ctg = NULL;
        while ( (ctg = ast_category_browse(cfg, ctg)) != NULL)
	{
		if (ctg == NULL) continue;
		if (!strcmp(ctg,"general")) continue;
		mynode = strtoul(ctg,NULL,0);
	        val = (char *) ast_variable_retrieve(cfg,ctg,"buflen"); 
		if (val) instance_buflen = strtoul(val,NULL,0) * 8;
		else instance_buflen = buflen;
		if (instance_buflen < (FRAME_SIZE * 2)) instance_buflen = FRAME_SIZE * 2;
		for (v = ast_variable_browse(cfg, ctg); v; v = v->next)
		{
			if (!strcmp(v->name,"txctcsslevel")) continue;
			if (!strcmp(v->name,"txctcss")) continue;
			if (!strcmp(v->name,"txtoctype")) continue;
			if (!strcmp(v->name,"streams")) continue;
			if (!strcmp(v->name,"thresholds")) continue;
			if (!strcmp(v->name,"plfilter")) continue;
			if (!strcmp(v->name,"duplex")) continue;
			if (!strcmp(v->name,"linger")) continue;
			if (!strncasecmp(v->name,"transmit",8)) continue;
			if (!strncasecmp(v->name,"master",6)) continue;
			if (!strncasecmp(v->name,"adpcm",5)) continue;
			if (!strncasecmp(v->name,"nulaw",5)) continue;
			if (!strncasecmp(v->name,"dynamic",7)) continue;
			if (!strncasecmp(v->name,"gpsid",5)) continue;
			if (!strncasecmp(v->name,"buflen",6)) continue;
			if (!strncasecmp(v->name,"nodeemp",7)) continue;
			if (!strncasecmp(v->name,"noplfilter",10)) continue;
			if (!strncasecmp(v->name,"prio",4)) continue;
			cp = ast_strdup(v->value);
			if (!cp)
			{
				ast_log(LOG_ERROR,"Cant Malloc()\n");
                                close(udp_socket);
                                ast_config_destroy(cfg);
				ast_mutex_unlock(&voter_lock);
				return -1;
			}
			n = finddelim(cp,strs,40);
			if (n < 1) continue;
			/* see if we "know" this client already */
			for(client = clients; client; client = client->next)
			{
				if (client->dynamic) continue;
				/* if this is the one whose digest matches one currently being looked at */
				if (client->digest == crc32_bufs(challenge,strs[0]))
				{
					/* if has moved to another instance, free this one, and treat as new */
					if (client->nodenum != strtoul(ctg,NULL,0))
					{
						client->reload = 0;
						client = NULL;
					}
					break;
				}
			}
			newclient = 0;
			/* if a new one, alloc its space */
			if (!client)
			{
				client = (struct voter_client *)ast_malloc(sizeof(struct voter_client));
				if (!client)
				{
					ast_log(LOG_ERROR,"Cant malloc()\n");
					ast_free(cp);
			                close(udp_socket);
					ast_config_destroy(cfg);
					ast_mutex_unlock(&voter_lock);
					return -1;
				}
				memset(client,0,sizeof(struct voter_client));
				client->prio_override = -2;
				ast_copy_string(client->name,v->name,VOTER_NAME_LEN - 1);
				newclient = 1;
			}
			client->reload = 1;
			client->buflen = instance_buflen;
			client->nodenum = strtoul(ctg,NULL,0);
			for(i = 1; i < n; i++)
			{
				if (!strcasecmp(strs[i],"transmit"))
					client->totransmit = 1;
				else if (!strcasecmp(strs[i],"master"))
				{
                                        client->ismaster = 1;
					hasmaster = 1;
				}
				else if (!strcasecmp(strs[i],"adpcm"))
                                        client->doadpcm = 1;
				else if (!strcasecmp(strs[i],"nulaw"))
                                        client->donulaw = 1;
				else if (!strcasecmp(strs[i],"nodeemp"))
                                        client->nodeemp = 1;
				else if (!strcasecmp(strs[i],"noplfilter"))
                                        client->noplfilter = 1;
				else if (!strcasecmp(strs[i],"dynamic"))
                                        client->dynamic = 1;
				else if (!strncasecmp(strs[i],"gpsid",5))
				{
					cp1 = strchr(strs[i],'=');
					if (!cp1)
					{
						client->gpsid = ast_strdup("");
					}
					else
					{
						client->gpsid = ast_strdup(cp1);
						*client->gpsid = '_';
					}
				}
				else if (!strncasecmp(strs[i],"prio",4))
				{
					cp1 = strchr(strs[i],'=');
					if (cp1)
					{
						client->prio = strtoul(cp1 + 1,NULL,0);
						if (client->prio < -1) client->prio = 0;
					}
				}
			}
			client->buflen -= client->buflen % (FRAME_SIZE * 2);
			client->digest = crc32_bufs(challenge,strs[0]);
			ast_free(cp);
			if (client->old_buflen && (client->buflen != client->old_buflen))
				client->drainindex = 0;
			if (client->audio && client->old_buflen && (client->buflen != client->old_buflen))
			{
				client->audio = (uint8_t *)ast_realloc(client->audio,client->buflen);
				if (!client->audio)
				{
					ast_log(LOG_ERROR,"Cant realloc()\n");
			                close(udp_socket);
					ast_config_destroy(cfg);
					ast_mutex_unlock(&voter_lock);
					return -1;
				}
				memset(client->audio,0xff,client->buflen);
			}
			else if (!client->audio) 
			{
				client->audio = (uint8_t *)ast_malloc(client->buflen);
				if (!client->audio)
				{
					ast_log(LOG_ERROR,"Cant malloc()\n");
			                close(udp_socket);
					ast_config_destroy(cfg);
					ast_mutex_unlock(&voter_lock);
					return -1;
				}
				memset(client->audio,0xff,client->buflen);
			}
			if (client->rssi && client->old_buflen && (client->buflen != client->old_buflen))
			{
				client->rssi = (uint8_t *)ast_realloc(client->rssi,client->buflen);
				if (!client->rssi)
				{
					ast_log(LOG_ERROR,"Cant realloc()\n");
			                close(udp_socket);
					ast_config_destroy(cfg);
					ast_mutex_unlock(&voter_lock);
					return -1;
				}
				memset(client->rssi,0,client->buflen);
			}
			else if (!client->rssi)
			{
				client->rssi = (uint8_t *)ast_malloc(client->buflen);
				if (!client->rssi)
				{
					ast_log(LOG_ERROR,"Cant malloc()\n");
			                close(udp_socket);
					ast_config_destroy(cfg);
					ast_mutex_unlock(&voter_lock);
					return -1;
				}
				memset(client->rssi,0,client->buflen);
			}
			/* if a new client, add it into list */
			if (newclient)
			{
				if (clients == NULL) clients = client;
				else
				{
					for(client1 = clients; client1->next; client1 = client1->next) ;
					client1->next = client;
				}
			}
		}
	}
	ast_config_destroy(cfg);
	for(client = clients; client; client = client->next)
	{
		if (!client->reload) continue;
		if (client->digest == 0)
		{
			ast_log(LOG_ERROR,"Can Not Load chan_voter -- VOTER client %s has invalid authentication digest (can not be 0)!!!\n",client->name);
			ast_mutex_unlock(&voter_lock);
			return -1;
		}
		if (client->dynamic) continue;
		for(client1 = clients; client1; client1 = client1->next)
		{
			if (!client1->reload) continue;
			if (client == client1) continue;
			if (client->digest == client1->digest)
			{
				ast_log(LOG_ERROR,"Can Not Load chan_voter -- VOTER clients %s and %s have same authentication digest!!!\n",client->name,client1->name);
				ast_mutex_unlock(&voter_lock);
				return -1;
			}
		}
	}
	/* remove all the clients that are no longer in the config */
	for(client = clients; client; client = client->next)
	{
		if (client->reload) continue;
		if (client->audio) ast_free(client->audio);
		if (client->rssi) ast_free(client->rssi);
		if (client->gpsid) ast_free(client->gpsid);
		for(client1 = clients; client1; client1 = client1->next)
		{
			if (client1->next == client) break;
		}
		if (client1) client1->next = client->next;
		else clients = NULL;
		ast_free(client);
		client = clients;
	}
	ast_mutex_unlock(&voter_lock);
	return(0);
}

#ifndef	OLD_ASTERISK
static
#endif
int load_module(void)
{

	pthread_attr_t attr;
	struct sockaddr_in sin;
	int i,bs;
	struct ast_config *cfg = NULL;
	char *val;
#ifdef  NEW_ASTERISK
        struct ast_flags zeroflag = {0};
#endif


	snprintf(challenge, sizeof(challenge), "%ld", ast_random());
	hasmaster = 0;

#ifdef  NEW_ASTERISK
        if (!(cfg = ast_config_load(config,zeroflag))) {
#else
        if (!(cfg = ast_config_load(config))) {
#endif
                ast_log(LOG_ERROR, "Unable to load config %s\n", config);
		ast_mutex_unlock(&voter_lock);
		return 1;
        }

	if ((udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	{
		ast_log(LOG_ERROR,"Unable to create new socket for voter audio connection\n");
		ast_config_destroy(cfg);
                return AST_MODULE_LOAD_DECLINE;
	}
	memset((char *) &sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
        val = (char *) ast_variable_retrieve(cfg,"general","port"); 
	if (val) listen_port = (uint16_t) strtoul(val,NULL,0);

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
                return AST_MODULE_LOAD_DECLINE;
	}

	i = fcntl(udp_socket,F_GETFL,0);              // Get socket flags
	fcntl(udp_socket,F_SETFL,i | O_NONBLOCK);   // Add non-blocking flag

	voter_timing_fd = open(DAHDI_PSEUDO_DEV_NAME,O_RDWR);
	if (voter_timing_fd == -1)
	{
		ast_log(LOG_ERROR,"Cant open DAHDI timing channel\n");
                close(udp_socket);
		ast_config_destroy(cfg);
                return AST_MODULE_LOAD_DECLINE;
	}
	bs = FRAME_SIZE;
	if (ioctl(voter_timing_fd, DAHDI_SET_BLOCKSIZE, &bs) == -1) 
	{
		ast_log(LOG_WARNING, "Unable to set blocksize '%d': %s\n", bs,  strerror(errno));
		close(voter_timing_fd);
                close(udp_socket);
		ast_config_destroy(cfg);
                return AST_MODULE_LOAD_DECLINE;
	}
	ast_config_destroy(cfg);


	if (reload()) return AST_MODULE_LOAD_DECLINE;

#ifdef	NEW_ASTERISK
	ast_cli_register_multiple(voter_cli,sizeof(voter_cli) / 
		sizeof(struct ast_cli_entry));
#else
	/* Register cli extensions */
	ast_cli_register(&cli_debug);
	ast_cli_register(&cli_test);
	ast_cli_register(&cli_prio);
	ast_cli_register(&cli_record);
	ast_cli_register(&cli_tone);
	ast_cli_register(&cli_reload);
	ast_cli_register(&cli_display);
#endif

        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        ast_pthread_create(&voter_reader_thread,&attr,voter_reader,NULL);
        ast_pthread_create(&voter_timer_thread,&attr,voter_timer,NULL);

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
AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "radio Voter channel driver",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
#endif
