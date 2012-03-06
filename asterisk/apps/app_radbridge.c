/* #define	OLD_ASTERISK */
/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Copyright (C) 2012, Jim Dixon/WB6NIL
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
 * \brief Radio Device Bridging Application
 * 
 * \author Jim Dixon, WB6NIL <jim@lambdatel.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
 ***/

/* This application is intended to provide a generic "bridging"
   functionality for radio-oriented Asterisk channels.

   The idea here is that certain radio applications lend themselves
   to needing a "generic" (passes audio and tx/rx keying completely
   transparently) to and from two or more endpoints.

   An example of such an appication might be replacing (or providing
   equivlent new functionaliy of) one or more full-duplex UHF control
   links with an IP-based one(s).

   app_radbridge allows multiple instances of radio-oriented channels
   transparently "bridged" together.

   The config file (radbridge.conf) allows specification of each
   "bridging" instance and what radio-oriented Asterisk channels
   are to be associated with it. There is no need for any other
   specificaiton of configuration.

   The file format for radbridge.conf departs somewhat from the standard
   usage (at least for radio stuff) of the Asterisk config file architecture.

   In most other radio-oriented cases, an instance of whatever it is
   gets defined as a "Stanza" in the config file. In this case, because
   of limitations in the way that Asterisk parses config files, that 
   was not possible. Instead, all config information goes into the
   [general] stanza, and each line of config info defines a different
   instance of radio bridging.  For example:

[general]

instance1 = Voter/1234,Radio/5678
instance2 = Voter/1235,Radio/5679

    This example would define two radio bridging instances "instance1"
    and "instance2", each with 2 channels associated with them.

    The supported channel types are "Voter" (chan_voter), "Radio"
    (chan_usbradio), "SimpleUsb" (chan_simpleusb), and "Zap" ("Dahdi")
    (chan_dahdi, for the couple of devices that use this channel
    driver, such as the PCIRadio card, and separate analog channels
    using an ARIB board or such).

    Rather then "adding" (which would actually be a lot of *removing*
    optionally) this functionality in app_rpt, it made more sense to
    create a separate application specifically for this purpose. In
    addition, this allows for a system that is merely provided for
    doing this bridging application and nothing else radio-oriented,
    to not have to run app_rpt at all. 
    

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
#include <ctype.h>
#include <search.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>

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
#include "asterisk/channel.h"

#ifdef OLD_ASTERISK
#define ast_free free
#define ast_malloc malloc
#endif

#define MAXBRIDGES 100
#define	MAXCHANS 100
#define	RXDELAY 40

#define	FRAME_SIZE 160

struct radbridge {
	char *name;
	char *channames[MAXCHANS];
	struct ast_channel *chans[MAXCHANS];
	int nchans;
	int zapchans[MAXCHANS];
	char isnativezap[MAXCHANS];
	int zapconf;
	pthread_t thread;
	time_t lastthreadrestarttime;
	int threadrestarts;
	int scram;
	char tx[MAXBRIDGES];
	char rx[MAXBRIDGES];
	struct timeval lastrx[MAXBRIDGES];
} radbridge_vars[MAXBRIDGES];

#define	is_mixminus(p) (p->nchans > 2)

static char *config = "radbridge.conf";

static char *app = "RADBRIDGE";

static char *synopsis = "Radio Device Bridging Module";

static char *descrip = "Bridges Radio Devices\n";

int nbridges = 0;

int run_forever = 1;

pthread_t radbridge_master_thread;

/*
* Break up a delimited string into a table of substrings
*
* str - delimited string ( will be modified )
* strp- list of pointers to substrings (this is built by this function), NULL will be placed at end of list
* limit- maximum number of substrings to process
* delim- user specified delimeter
* quote- user specified quote for escaping a substring. Set to zero to escape nothing.
*
* Note: This modifies the string str, be suer to save an intact copy if you need it later.
*
* Returns number of substrings found.
*/
	

static int explode_string(char *str, char *strp[], int limit, char delim, char quote)
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
		if(quote)
		{
                	if (*str == quote)
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
		}	
                if ((*str == delim) && (!inquo))
                {
                        *str = 0;
			l++;
                        strp[i++] = str + 1;
                }
        }
        strp[i] = 0;
        return(i);

}

static void dokey(struct radbridge *mybridge)
{
int	i,j,rx;
struct	ast_frame fr;

	for(i = 0; i < mybridge->nchans; i++)
	{
		rx = 0;
		for(j = 0; j < mybridge->nchans; j++)
		{
			if (i == j) continue;
			rx |= mybridge->rx[j] | (!ast_tvzero(mybridge->lastrx[j]));
		}
		if (rx && (!mybridge->tx[i]))
		{
			mybridge->tx[i] = 1;
			memset(&fr,0,sizeof(struct ast_frame));
		        fr.frametype = AST_FRAME_CONTROL;
		        fr.subclass = AST_CONTROL_RADIO_KEY;
			ast_write(mybridge->chans[i],&fr);		
			if (option_verbose > 4)
				ast_verbose(VERBOSE_PREFIX_3 "radbridge %s TX KEY\n",mybridge->channames[i]);
		}
		else if ((!rx) && mybridge->tx[i])
		{
			mybridge->tx[i] = 0;
			memset(&fr,0,sizeof(struct ast_frame));
		        fr.frametype = AST_FRAME_CONTROL;
		        fr.subclass = AST_CONTROL_RADIO_UNKEY;
			ast_write(mybridge->chans[i],&fr);			
			if (option_verbose > 4)
				ast_verbose(VERBOSE_PREFIX_3 "radbridge %s TX UNKEY\n",mybridge->channames[i]);
		}
	}
	return;
}


static void *radbridge(void *data)
{

struct radbridge *mybridge = (struct radbridge *)data;
char *tele,tmpstr[300],val;
int	i,j,ms,bs,fds[MAXCHANS],nfds,winfd;
short	buf[FRAME_SIZE];
struct ast_channel *who,*cs[MAXCHANS];
struct ast_frame *f,fr;
struct dahdi_confinfo ci;  /* conference info */
struct dahdi_bufferinfo bi;
struct timeval now;

	mybridge->zapconf = -1;
	for(i = 0; i < mybridge->nchans; i++)
	{
		strncpy(tmpstr,mybridge->channames[i],sizeof(tmpstr) - 1);
		tele = strchr(tmpstr,'/');
		if (!tele)
		{
			ast_log(LOG_ERROR,"radbridge:channel Dial number (%s) must be in format tech/number\n",
				mybridge->channames[i]);
			mybridge->thread = AST_PTHREADT_STOP;
			pthread_exit(NULL);
		}
		*tele++ = 0;
		mybridge->chans[i] = ast_request(tmpstr,AST_FORMAT_SLINEAR,tele,NULL);
		if (!mybridge->chans[i])
		{
			ast_log(LOG_ERROR,"radbridge:Sorry unable to obtain channel\n");
			mybridge->thread = AST_PTHREADT_STOP;
			pthread_exit(NULL);
		}
		if (mybridge->chans[i]->_state == AST_STATE_BUSY)
		{
			ast_log(LOG_ERROR,"radbridge:Sorry unable to obtain channel\n");
			mybridge->thread = AST_PTHREADT_STOP;
			pthread_exit(NULL);
		}
		ast_set_read_format(mybridge->chans[i],AST_FORMAT_SLINEAR);
		ast_set_write_format(mybridge->chans[i],AST_FORMAT_SLINEAR);
#ifdef	AST_CDR_FLAG_POST_DISABLED
		if (mybridge->chans[i]->cdr)
		ast_set_flag(mybridge->chans[i]->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
#ifndef	NEW_ASTERISK
		mybridge->chans[i]->whentohangup = 0;
#endif
		mybridge->chans[i]->appl = "Appradbridge";
		mybridge->chans[i]->data = "(Bridge Channel)";
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "radbridge initiating call to %s/%s\n",
				tmpstr,tele);
		ast_call(mybridge->chans[i],tele,999);
		if (mybridge->chans[i]->_state != AST_STATE_UP)
		{
			ast_hangup(mybridge->chans[i]);
			mybridge->thread = AST_PTHREADT_STOP;
			pthread_exit(NULL);
		}
		val = 3;
		ast_channel_setoption(mybridge->chans[i],AST_OPTION_TONE_VERIFY,&val,sizeof(char),0);
		if (is_mixminus(mybridge))
		{
			if ((!strcasecmp(tmpstr,"zap")) || (!strcasecmp(tmpstr,"Dahdi")))
			{
				mybridge->zapchans[i] = mybridge->chans[i]->fds[0];
				mybridge->isnativezap[i] = 1;
			}
			else
			{
				mybridge->zapchans[i] = open(DAHDI_PSEUDO_DEV_NAME,O_RDWR);
				if (mybridge->zapchans[i] == -1)
				{
					ast_log(LOG_ERROR,"Can not open pseudo channel for radio bridging\n");
					ast_hangup(mybridge->chans[i]);
					mybridge->thread = AST_PTHREADT_STOP;
					pthread_exit(NULL);
				}
				bs = FRAME_SIZE;
				if (ioctl(mybridge->zapchans[i], DAHDI_SET_BLOCKSIZE, &bs) == -1) 
				{
					ast_log(LOG_WARNING, "Unable to set blocksize '%d': %s\n", bs,  strerror(errno));
					close(mybridge->zapchans[i]);
					ast_hangup(mybridge->chans[i]);
					mybridge->thread = AST_PTHREADT_STOP;
					pthread_exit(NULL);
				}
				bs = 1;
				if (ioctl(mybridge->zapchans[i], DAHDI_SETLINEAR, &bs) == -1) 
				{
					ast_log(LOG_WARNING, "Unable to linear mode  %s\n", strerror(errno));
					close(mybridge->zapchans[i]);
					ast_hangup(mybridge->chans[i]);
					mybridge->thread = AST_PTHREADT_STOP;
					pthread_exit(NULL);
				}
				/* Setup buffering information */
				memset(&bi, 0, sizeof(bi));
				bi.bufsize = FRAME_SIZE;
				bi.txbufpolicy = DAHDI_POLICY_IMMEDIATE;
				bi.rxbufpolicy = DAHDI_POLICY_IMMEDIATE;
				bi.numbufs = 4;
				if (ioctl(mybridge->zapchans[i], DAHDI_SET_BUFINFO, &bi))
				{
					ast_log(LOG_WARNING, "Unable to set buf policy:  %s\n", strerror(errno));
					close(mybridge->zapchans[i]);
					ast_hangup(mybridge->chans[i]);
					mybridge->thread = AST_PTHREADT_STOP;
					pthread_exit(NULL);
				}
				ci.chan = 0;
				ci.confno = mybridge->zapconf;
				ci.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER | DAHDI_CONF_LISTENER;
				if (ioctl(mybridge->zapchans[i],DAHDI_SETCONF,&ci) == -1)
				{
					ast_log(LOG_WARNING, "Unable to add psuedo to conf\n");
					close(mybridge->zapchans[i]);
					ast_hangup(mybridge->chans[i]);
					mybridge->thread = AST_PTHREADT_STOP;
					pthread_exit(NULL);
				}
				mybridge->zapconf = ci.confno;
			}
		}
	}
	while(run_forever && (!ast_shutting_down()))
	{
		for(i = 0; i < mybridge->nchans; i++)
		{
			if (ast_check_hangup(mybridge->chans[i]))
			{
				run_forever = 0;
				break;
			}
			int s = -(-i - mybridge->scram - 1) % mybridge->nchans;
			cs[i] = mybridge->chans[s];
		}
		if (!run_forever) break;
		mybridge->scram++;
		nfds = 0;
		if (is_mixminus(mybridge))
		{
			for(i = 0; i < mybridge->nchans; i++)
			{
				if (mybridge->isnativezap[i]) continue;
				fds[nfds++] = mybridge->zapchans[i];
			}
		}
		ms = 100;
		who = ast_waitfor_nandfds(cs,mybridge->nchans,fds,nfds,NULL,&winfd,&ms);
		for(i = 0; i < mybridge->nchans; i++)
		{
			if (ast_check_hangup(mybridge->chans[i]))
			{
				run_forever = 0;
				break;
			}
		}
		if (!run_forever) break;
		gettimeofday(&now,NULL);
		if (who != NULL)
		{
			f = ast_read(who);
			if (!f)
			{
				ast_log(LOG_ERROR,"radbridge channel %s hung up!!\n",who->name);
				mybridge->thread = AST_PTHREADT_STOP;
				pthread_exit(NULL);
			}
			if (!is_mixminus(mybridge))
			{
				if ((f->frametype == AST_FRAME_VOICE) ||
				    ((f->frametype == AST_FRAME_CONTROL) && 
					((f->subclass == AST_CONTROL_RADIO_KEY) || (f->subclass == AST_CONTROL_RADIO_UNKEY))))
				{
					for(j = 0; j < mybridge->nchans; j++)
					{
						if (mybridge->chans[j] == who) continue;
						ast_write(mybridge->chans[j],f);
					}
				}
			}
			else
			{
				for(i = 0; i < mybridge->nchans; i++)
				{
					if (mybridge->chans[i] == who) break;
				}
				if (i >= mybridge->nchans)
				{
					ast_log(LOG_WARNING,"Cannot find match for %s\n",who->name);
					ast_frfree(f);
					continue;
				}
				if (f->frametype == AST_FRAME_VOICE)
				{
					if (!mybridge->isnativezap[i])
						write(mybridge->zapchans[i],AST_FRAME_DATAP(f),f->datalen);
					if ((!mybridge->rx[i]) && (!ast_tvzero(mybridge->lastrx[i])) &&
						(ast_tvdiff_ms(now,mybridge->lastrx[i]) >= RXDELAY))
					{
						memset(&mybridge->lastrx[i],0,sizeof(mybridge->lastrx[i]));
						dokey(mybridge);
					}
				}
				if (f->frametype == AST_FRAME_CONTROL)
				{
					if (f->subclass == AST_CONTROL_RADIO_KEY)
					{
						mybridge->rx[i] = 1;
						memset(&mybridge->lastrx[i],0,sizeof(mybridge->lastrx[i]));
						dokey(mybridge);
					}
					if (f->subclass == AST_CONTROL_RADIO_UNKEY)
					{
						mybridge->rx[i] = 0;
						mybridge->lastrx[i] = now;
					}
				}
			}
			if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_HANGUP))
			{
				ast_log(LOG_ERROR,"radbridge channel %s hung up!!\n",who->name);
				mybridge->thread = AST_PTHREADT_STOP;
				pthread_exit(NULL);
			}
			ast_frfree(f);
		}
		if (winfd >= 0)
		{
			for(i = 0; i < mybridge->nchans; i++)
			{
				if (winfd == mybridge->zapchans[i]) break;
			}
			if (i >= mybridge->nchans)
			{
				ast_log(LOG_WARNING,"Cannot find zap chan for fd %d\n",winfd);
				continue;
			}
			j = read(winfd,buf,FRAME_SIZE * 2);
			if (j != (FRAME_SIZE * 2))
			{
				ast_log(LOG_ERROR,"radbridge pseudo channel %s hung up!!\n",mybridge->channames[i]);
				mybridge->thread = AST_PTHREADT_STOP;
				pthread_exit(NULL);
			}
			memset(&fr,0,sizeof(struct ast_frame));
		        fr.frametype = AST_FRAME_VOICE;
		        fr.subclass = AST_FORMAT_SLINEAR;
		        fr.datalen = FRAME_SIZE * 2;
		        fr.samples = FRAME_SIZE;
		        AST_FRAME_DATA(fr) =  buf;
		        fr.src = app;
		        fr.offset = 0;
		        fr.mallocd = 0;
		        fr.delivery.tv_sec = 0;
		        fr.delivery.tv_usec = 0;
			ast_write(mybridge->chans[i],&fr);			
		}
	}
	for(i = 0; i < mybridge->nchans; i++)
	{
		ast_hangup(mybridge->chans[i]);
	}
	pthread_exit(NULL);
}

static void *radbridge_master(void *ignore)
{
int	i,n;
struct ast_config *cfg;
struct ast_variable *v;
char *cp;
pthread_attr_t attr;


	n = 0;
#ifndef OLD_ASTERISK
	/* wait until asterisk starts */
        while(!ast_test_flag(&ast_options,AST_OPT_FLAG_FULLY_BOOTED))
                usleep(250000);
#endif
#ifdef	NEW_ASTERISK
	cfg = ast_config_load(config,config_flags);
#else
	cfg = ast_config_load(config);
#endif
	if (!cfg) {
		ast_log(LOG_NOTICE, "Unable to open radio bridging  configuration rpt.conf.  Radio Bridging disabled.\n");
		pthread_exit(NULL);
	}

	for (v = ast_variable_browse(cfg, "general"); v; v = v->next)
	{
		memset(&radbridge_vars[n],0,sizeof(radbridge_vars[n]));
		radbridge_vars[n].name = ast_strdup(v->name);
		cp = ast_strdup(v->value);
		if (!cp)
		{
			ast_log(LOG_ERROR,"Cant Malloc!!\n");
			pthread_exit(NULL);
		}			
		radbridge_vars[n].nchans = explode_string(cp,radbridge_vars[n].channames,MAXCHANS,',','\"');
		n++;
	}
	nbridges = n;
	ast_config_destroy(cfg);

	/* start em all */
	for(i = 0; i < n; i++)
	{
	        pthread_attr_init(&attr);
	        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		ast_pthread_create(&radbridge_vars[i].thread,&attr,radbridge,(void *) &radbridge_vars[i]);
	}
	usleep(500000);
	while(!ast_shutting_down())
	{
		/* Now monitor each thread, and restart it if necessary */
		for(i = 0; i < nbridges; i++)
		{ 
			int rv;
			if ((radbridge_vars[i].thread == AST_PTHREADT_STOP) 
				|| (radbridge_vars[i].thread == AST_PTHREADT_NULL))
				rv = -1;
			else
				rv = pthread_kill(radbridge_vars[i].thread,0);
			if (rv)
			{
				if(time(NULL) - radbridge_vars[i].lastthreadrestarttime <= 5)
				{
					if(radbridge_vars[i].threadrestarts >= 5)
					{
						ast_log(LOG_ERROR,"Continual RPT thread restarts, killing Asterisk\n");
						exit(1); /* Stuck in a restart loop, kill Asterisk and start over */
					}
					else
					{
						ast_log(LOG_NOTICE,"RPT thread restarted on %s\n",radbridge_vars[i].name);
						radbridge_vars[i].threadrestarts++;
					}
				}
				else
					radbridge_vars[i].threadrestarts = 0;

				radbridge_vars[i].lastthreadrestarttime = time(NULL);
			        pthread_attr_init(&attr);
	 		        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
				ast_pthread_create(&radbridge_vars[i].thread,&attr,radbridge,(void *) &radbridge_vars[i]);
				ast_log(LOG_WARNING, "radbridge_thread restarted on node %s\n", radbridge_vars[i].name);
			}

		}
		usleep(2000000);
	}
	pthread_exit(NULL);
}





static int radbridge_exec(struct ast_channel *chan, void *data)
{
	return 0;
}


#ifdef	OLD_ASTERISK
int unload_module()
#else
static int unload_module(void)
#endif
{
int	res;

	run_forever = 0;
	res = ast_unregister_application(app);
	return res;
}

#ifndef	OLD_ASTERISK
static
#endif
int load_module(void)
{
	int res;
	pthread_attr_t attr;
	struct ast_config *cfg;

#ifdef	NEW_ASTERISK
	cfg = ast_config_load(config,config_flags);
#else
	cfg = ast_config_load(config);
#endif
	if (!cfg) {
		ast_log(LOG_NOTICE, "Unable to open radio bridging  configuration rpt.conf.  Radio Bridging disabled.\n");
		return(AST_MODULE_LOAD_DECLINE);
	}
	ast_config_destroy(cfg);

	memset(&radbridge_vars,0,sizeof(radbridge_vars));
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        ast_pthread_create(&radbridge_master_thread,&attr,radbridge_master,NULL);
	res = ast_register_application(app, radbridge_exec, synopsis, descrip);
	return res;
}

#ifdef	OLD_ASTERISK
char *description()
{
	return (char *)radbridge_tech.description;
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
AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Radio Bridging interface module");
#endif

