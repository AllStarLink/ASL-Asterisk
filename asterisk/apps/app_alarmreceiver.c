/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C)  2004 - 2005 Steve Rodgers
 *
 * Steve Rodgers <hwstar@rodgers.sdcoxmail.com>
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
 * \brief Central Station Alarm receiver for Contact ID format Alarm Panels
 * \author Steve Rodgers <hwstar@rodgers.sdcoxmail.com>
 *
 * *** WARNING *** WARNING *** WARNING *** WARNING *** WARNING *** WARNING *** WARNING *** WARNING ***
 *
 * Use at your own risk. Please consult the GNU GPL license document included with Asterisk.         *
 *
 * *** WARNING *** WARNING *** WARNING *** WARNING *** WARNING *** WARNING *** WARNING *** WARNING ***
 *
 * \ingroup applications
 */

/*
20110710 1933 EDT "Steven Henke"<sph@xelatec.com> Updated and modified

arecord -D default:CARD=U0xd8c0x0c -vv -r48000 -fS16_LE -c1 iti_`date +'%Y%m%d-%H%M%S'`.wav

arecord -vv -r48000 -fS16_LE -c1 iti_`date +'%Y%m%d-%H%M%S'`.wav

dtmf2num  -r 48000 1 16 iti_20110708-104051.wav

Mitel Zarlink CM7291 test tape download
*/

#define  DEBUG_CHECKSUM		1
#define  DEBUG_DEV			1

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 211528 $")

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/time.h>

#include "asterisk/indications.h"

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/ulaw.h"
#include "asterisk/options.h"
#include "asterisk/app.h"
#include "asterisk/dsp.h"
#include "asterisk/config.h"
#include "asterisk/localtime.h"
#include "asterisk/callerid.h"
#include "asterisk/astdb.h"
#include "asterisk/utils.h"

#define ALMRCV_CONFIG "alarmreceiver.conf"
#define A_CONTACT_ID "A_CONTACT_ID"

struct event_node{
	char data[17];
	struct event_node *next;
};

typedef struct event_node event_node_t;

static char *app = "AlarmReceiver";

static char *synopsis = "Provide support for receiving alarm reports from a burglar or fire alarm panel";
static char *descrip =
"  AlarmReceiver(): Only 1 signalling format is supported at this time: \n"
"Contact ID. This application should be called whenever there is an alarm\n"
"panel calling in to dump its events. The application will handshake with the\n"
"alarm panel, and receive events, validate them, handshake them, and store them\n"
"until the panel hangs up. Once the panel hangs up, the application will run the\n"
"system command specified by the eventcmd setting in alarmreceiver.conf and pipe\n"
"the events to the standard input of the application. The configuration file also\n"
"contains settings for DTMF timing, and for the loudness of the acknowledgement\n"
"tones.\n";

/* Config Variables */

static int fdtimeout = 2000;
static int sdtimeout = 200;
static int toneloudness = 4096;
static int log_individual_events = 0;
static char event_spool_dir[128] = {'\0'};
static char event_app[128] = {'\0'};
static char db_family[128] = {'\0'};
static char time_stamp_format[128] = {"%a %b %d, %Y @ %H:%M:%S %Z"};

/* Misc variables */

static char event_file[14] = "/event-XXXXXX";

/*
* Attempt to access a database variable and increment it,
* provided that the user defined db-family in alarmreceiver.conf
* The alarmreceiver app will write statistics to a few variables
* in this family if it is defined. If the new key doesn't exist in the
* family, then create it and set its value to 1.
*/

static void database_increment( char *key )
{
	int res = 0;
	unsigned v;
	char value[16];

	if (ast_strlen_zero(db_family))
		return; /* If not defined, don't do anything */

	res = ast_db_get(db_family, key, value, sizeof(value) - 1);

	if(res){
		if(option_verbose >= 4)
			ast_verbose(VERBOSE_PREFIX_4 "AlarmReceiver: Creating database entry %s and setting to 1\n", key);
		/* Guess we have to create it */
		res = ast_db_put(db_family, key, "1");
		return;
	}

	sscanf(value, "%30u", &v);
	v++;

	if(option_verbose >= 4)
		ast_verbose(VERBOSE_PREFIX_4 "AlarmReceiver: My new value for %s: %u\n", key, v);

	snprintf(value, sizeof(value), "%u", v);

	res = ast_db_put(db_family, key, value);

	if((res)&&(option_verbose >= 4))
		ast_verbose(VERBOSE_PREFIX_4 "AlarmReceiver: database_increment write error\n");

	return;
}

#if 0
static int send_dtmf(struct ast_channel *chan, int tone_index, int duration, int tldn)
{

static char* dtmf_tones[] = {
	"!941+1336/200,!0/100",		/* 0 */
	"!697+1209/200,!0/100",		/* 1 */
	"!697+1336/200,!0/100",		/* 2 */
	"!697+1477/200,!0/100",		/* 3 */
	"!770+1209/200,!0/100",		/* 4 */
	"!770+1336/200,!0/100",		/* 5 */
	"!770+1477/200,!0/100",		/* 6 */
	"!852+1209/200,!0/100",		/* 7 */
	"!852+1336/900,!0/100",		/* 8 */
	"!852+1477/200,!0/100",		/* 9 */
	"!697+1633/200,!0/100",		/* A */
	"!770+1633/200,!0/100",		/* B */
	"!852+1633/200,!0/100",		/* C */
	"!941+1633/200,!0/100",		/* D */
	"!941+1209/200,!0/100",		/* * */
	"!941+1477/200,!0/100" };	/* # */

	ast_playtones_start(chan, 0, dtmf_tones[i], 0);
	ast_safe_sleep(chan,121);

	return(0);
}
#endif


/*
* Write the metadata to the log file
*/

static int write_metadata( FILE *logfile, char *signalling_type, struct ast_channel *chan)
{
	int res = 0;
	time_t t;
	struct tm now;
	char *cl,*cn;
	char workstring[80];
	char timestamp[80];

	/* Extract the caller ID location */
	if (chan->cid.cid_num)
		ast_copy_string(workstring, chan->cid.cid_num, sizeof(workstring));
	workstring[sizeof(workstring) - 1] = '\0';

	ast_callerid_parse(workstring, &cn, &cl);
	if (cl)
		ast_shrink_phone_number(cl);


	/* Get the current time */

	time(&t);
	ast_localtime(&t, &now, NULL);

	/* Format the time */

	strftime(timestamp, sizeof(timestamp), time_stamp_format, &now);

	res = fprintf(logfile, "\n\n[metadata]\n\n");

	if(res >= 0)
		res = fprintf(logfile, "PROTOCOL=%s\n", signalling_type);

	if(res >= 0)
		res = fprintf(logfile, "CALLINGFROM=%s\n", (!cl) ? "<unknown>" : cl);

	if(res >- 0)
		res = fprintf(logfile, "CALLERNAME=%s\n", (!cn) ? "<unknown>" : cn);

	if(res >= 0)
		res = fprintf(logfile, "TIMESTAMP=%s\n\n", timestamp);

	if(res >= 0)
	{
		res = fprintf(logfile, "[events]\n\n");

	}

	if(res < 0){
		ast_verbose(VERBOSE_PREFIX_4 "AlarmReceiver: can't write metadata\n");

		ast_log(LOG_DEBUG,"AlarmReceiver: can't write metadata\n");
	}
	else
		res = 0;

	return res;
}

/*
* Write a single event to the log file
*/

static int write_event( FILE *logfile,  event_node_t *event)
{
	int res = 0;

	if( fprintf(logfile, "%s\n", event->data) < 0)
		res = -1;

	return res;
}


/*
* If we are configured to log events, do so here.
*
*/
static int log_events(struct ast_channel *chan,  char *signalling_type, event_node_t *event)
{
	int res = 0;
	char workstring[sizeof(event_spool_dir)+sizeof(event_file)] = "";
	int fd;
	FILE *logfile;
	event_node_t *elp = event;

	if(DEBUG_DEV)ast_log(LOG_NOTICE,"start %s\n",event->data);

	if (!ast_strlen_zero(event_spool_dir)) {

		/* Make a template */
		ast_copy_string(workstring, event_spool_dir, sizeof(workstring));
		strncat(workstring, event_file, sizeof(workstring) - strlen(workstring) - 1);

		/* Make the temporary file */
		fd = mkstemp(workstring);

		if(fd == -1){
			ast_verbose(VERBOSE_PREFIX_4 "AlarmReceiver: can't make temporary file\n");
			ast_log(LOG_DEBUG,"AlarmReceiver: can't make temporary file\n");
			res = -1;
		}

		if(!res){
			logfile = fdopen(fd, "w");
			if(logfile){
				/* Write the file */
				res = write_metadata(logfile, signalling_type, chan);
				if(!res)
					while((!res) && (elp != NULL)){
						res = write_event(logfile, elp);
						elp = elp->next;
					}
				if(!res){
					if(fflush(logfile) == EOF)
						res = -1;
					if(!res){
						if(fclose(logfile) == EOF)
							res = -1;
					}
				}
			}
			else
				res = -1;
		}
	}

	return res;
}


static int receive_contact_id( struct ast_channel *chan, void *data, int fdto, int sdto, int tldn, event_node_t **ehead)
{
	int i,j;
	int res = 0;
	int checksum;
	char event[17];
	event_node_t *enew, *elp;
	int events_received = 0;
	int cidstate=0,done=0;
	int frtime=0;
	int wres=0;

	int remaining = 80;
	int cid_format_error = 0;

	struct ast_frame *f = NULL;
	struct ast_silence_generator *silgen = NULL;

	struct timeval cidstart;
	struct timeval timelastdigit;
	struct timeval timehack;

	char   digit_string[256];

	static char digit_map[15] = "0123456789*#ABC";
    static unsigned char digit_weights[15] = {10,1,2,3,4,5,6,7,8,9,11,12,13,14,15};

	if(DEBUG_DEV)ast_log(LOG_NOTICE,"start fdto=%i, sdto=%i\n",fdto,sdto);

	database_increment("calls-received");

	i=0;
	checksum=0;
	cid_format_error = 0;
	cidstart = ast_tvnow();
	timelastdigit = ast_tvnow();
	memset(digit_string,0,sizeof(digit_string));

	while (!done && ((wres = ast_waitfor(chan,remaining)) > -1) && (f = ast_read(chan)))

	//while ( !done &&  0 < (remaining = ast_waitfor(chan, remaining)) )
	{
		//f = ast_read(chan);

		if (!f) {
			/* didn't get a frame. something must be wrong on the channel. so exit. */
			ast_log(LOG_ERROR, "failed to get a frame\n");
			done=1;
			continue;
		}

		if(cidstate==0 && (ast_tvdiff_ms(ast_tvnow(), cidstart) > 500) )
		{
			if(DEBUG_DEV)ast_log(LOG_NOTICE, "send ack\n");
			cidstate=1;
			ast_playtones_start(chan,0,"!1400/100,!0/100,!2300/100",0);
			//while(chan->generatordata){ast_safe_sleep(chan,20);}
			ast_safe_sleep(chan,322);
		}

		silgen = ast_channel_start_silence_generator(chan);

		if (f->frametype == AST_FRAME_DTMF_END){
			timehack = ast_tvnow();
			if(DEBUG_DEV)
				ast_log(LOG_NOTICE, "%i %i rx END dtmf=%c, i=%i\n",
					ast_tvdiff_ms(timehack, cidstart), ast_tvdiff_ms(timehack, timelastdigit),f->subclass,i);
			timelastdigit=timehack;
			digit_string[i++] = f->subclass;  /* save digit */
			digit_string[i] = '\0';
		}

		/* If call hung up */
		if ((f->frametype == AST_FRAME_CONTROL) &&
		    (f->subclass == AST_CONTROL_HANGUP)){
			if(DEBUG_DEV)ast_log(LOG_NOTICE, "got hangup on channel\n");
			res = -1;
			done=1;
			if(f)ast_frfree(f);
			continue;
		}

		/* if too long for first dtmf digit */
		if( cidstate==1 && i==0 && (ast_tvdiff_ms(ast_tvnow(),cidstart)>fdto) )
		{
            if(DEBUG_DEV)ast_log(LOG_NOTICE, "time out for first dtmf digit\n");
			if(f)ast_frfree(f);
			done=1;

			continue;
		}

		/* if too long between dtmf digits */
		if( cidstate==1 && i && (ast_tvdiff_ms(ast_tvnow(),timelastdigit)>sdto) )
		{
            if(DEBUG_DEV)ast_log(LOG_NOTICE, "time out between dtmf digits\n");
			if(f)ast_frfree(f);
			done=1;
			continue;
		}

		if(cidstate==1 && i >= 16)
		{
			/* Calculate checksum */
			cid_format_error = 0;
			if(DEBUG_DEV)ast_log(LOG_NOTICE, "got %i digits, checksum on %s\n",i,digit_string);

			for(j = 0, checksum = 0; j < 16; j++)
			{
				for(i = 0 ; i < sizeof(digit_map) ; i++)
				{
					if(digit_map[i] == digit_string[j])
					{
						//ast_log(LOG_NOTICE, "c=%c  dw=%i\n", digit_string[j],digit_weights[i]);
						break;
					}
				}
				if(i == 16)
				{
					if(DEBUG_DEV)ast_log(LOG_ERROR, "Got bogus dtmf character %c \n",digit_string[j]);
					break;
				}
				checksum += digit_weights[i];
				//ast_log(LOG_NOTICE, "Checksum %c %i %i\n",digit_string[j],digit_weights[i],checksum);
			}

			if(i == 16){
				ast_log(LOG_WARNING, "got invalid character with value 16\n");
				if(option_verbose >= 2)
					ast_verbose(VERBOSE_PREFIX_2 "AlarmReceiver: Bad DTMF character %c, trying again\n", event[j]);
				continue; /* Bad character */
			}

			/* Checksum is mod(15) of the total */
			checksum = checksum % 15;

			if (!DEBUG_CHECKSUM && checksum) {
				database_increment("checksum-errors");
				if (option_verbose >= 2)
					ast_verbose(VERBOSE_PREFIX_2 "AlarmReceiver: Nonzero checksum\n");
				if(DEBUG_DEV)ast_log(LOG_NOTICE, "Nonzero checksum %i \n",checksum);
				cidstate=3;
			}
			else
			{
				if(DEBUG_DEV)ast_log(LOG_NOTICE, "Passed w/checksum=%i \n",checksum);
				cidstate=2;
			}

			/* validate format */
			if( !DEBUG_CHECKSUM && cidstate==2 && strncmp(digit_string+4,"18",2) && strncmp(digit_string+4,"98",2 )
			  )
			{
				ast_log(LOG_WARNING, "CID message format error\n");
				cid_format_error=1;
				cidstate=3;
				database_increment("format-errors");
				if(option_verbose >= 2)
					ast_verbose(VERBOSE_PREFIX_2 "AlarmReceiver: Wrong message type\n");
				ast_log(LOG_DEBUG, "AlarmReceiver: Wrong message type\n");
			}

			cidstart = ast_tvnow();
		}

		#if 0
		/* test code to send kissoff at a fixed time regardless of activity or state */
		if(cidstate==1 && (ast_tvdiff_ms(ast_tvnow(), cidstart) > 3750) )
		{
			if(DEBUG_DEV)ast_log(LOG_NOTICE,"forced kissoff\n");
			cidstate=2;
		}
		#endif

		/*	Here with valid event received */
		if(cidstate==2)
		{
			int fd;
		    char calarm[128];
			time_t myt;
 			char mydate[128];
			time(&myt);

			if(DEBUG_DEV)ast_log(LOG_NOTICE,"sending kissoff \n");
			ast_channel_stop_silence_generator(chan, silgen);
			silgen = NULL;
			ast_playtones_start(chan,0,"!0/60,!1400/900",0);
			while(chan->generatordata){ast_safe_sleep(chan,20);}
			cidstate = 3;

			events_received++;

			if(DEBUG_DEV)ast_log(LOG_NOTICE,"queue event %i \n",events_received);

			/* Queue the Event in the Event Structure for this Call */
			if (!(enew = ast_calloc(1, sizeof(*enew)))) {
				res = -1;
				break;
			}

			enew->next = NULL;
			ast_copy_string(enew->data, digit_string, sizeof(enew->data));

			/* Attach the event to end of list */
			if(*ehead == NULL){
				*ehead = enew;
			}
			else{
				for(elp = *ehead; elp->next != NULL; elp = elp->next)
				;

				elp->next = enew;
			}

			if(res > 0)
				res = 0;

			/* Support the option of logging single events */
			if( 0 && (res == 0) && (log_individual_events))
				res = log_events(chan, A_CONTACT_ID, enew);

			/* log the event to the alarmreceiver log */
		    strftime(mydate,sizeof(mydate) - 1,"%Y%m%d%H%M%S", localtime(&myt));
			sprintf(calarm,"%s|%s|\n",mydate,digit_string);
			fd = open("/var/log/alarmreceiver.log",O_WRONLY | O_CREAT | O_APPEND,0600);
			if (fd == -1)
			{
				ast_log(LOG_ERROR,"Cannot open log file for write\n");
			}
			else if (write(fd,calarm,strlen(calarm)) != strlen(calarm))
			{
				ast_log(LOG_ERROR,"Cannot write node log file for write");
			}
			close(fd);
		}

		/*	Here to restart the search for a message in the call */
		if(cidstate==3)
		{
            if(DEBUG_DEV)ast_log(LOG_NOTICE,"handle cidstat==3 events_received=%i\n",events_received);
			i=0;
            j=0;
            /* skip ack since receiving message n>0 */
            cidstate=1;
            remaining=10000;
			checksum=0;
			cid_format_error = 0;
			cidstart = ast_tvnow();
			timelastdigit = ast_tvnow();
			memset(digit_string,0,sizeof(digit_string));
		}

		if((ast_tvdiff_ms(ast_tvnow(), cidstart) > 11000))
		{
			if(DEBUG_DEV)ast_log(LOG_NOTICE, "call timeout\n");
			done=1;
		}

		if(f)ast_frfree(f);
    }

	if(DEBUG_DEV)ast_log(LOG_NOTICE, "exited loop\n");

	if(remaining>=10000)
	{
		if(DEBUG_DEV)ast_log(LOG_NOTICE, "no time remaining=%i\n",remaining);
	}
	return(0);
}

/*
* This is the main function called by Asterisk Core whenever the App is invoked in the extension logic.
* This function will always return 0.
*/

static int alarmreceiver_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	int adone = 0;
	int getcnt = 0;

	struct ast_module_user *u;
	event_node_t *elp, *efree;
	event_node_t *event_head = NULL;

	char signalling_type[64] = "";
	char amess[256];

	char *amp=amess;

	u = ast_module_user_add(chan);

	/* Set write and read formats to AST_FORMAT_ULAW */

	if(option_verbose >= 4)
		ast_verbose(VERBOSE_PREFIX_4 "AlarmReceiver: Setting read and write formats to ULAW\n");

	if (ast_set_write_format(chan,AST_FORMAT_ULAW)){
		ast_log(LOG_WARNING, "AlarmReceiver: Unable to set write format to Mu-law on %s\n",chan->name);
		ast_module_user_remove(u);
		return -1;
	}

	if (ast_set_read_format(chan,AST_FORMAT_ULAW)){
		ast_log(LOG_WARNING, "AlarmReceiver: Unable to set read format to Mu-law on %s\n",chan->name);
		ast_module_user_remove(u);
		return -1;
	}

	ast_channel_setoption(chan,AST_OPTION_RELAXDTMF,&res,sizeof(char),0);

	/* Set default values for this invocation of the application */
	ast_copy_string(signalling_type, A_CONTACT_ID, sizeof(signalling_type));

	/* Answer the channel if it is not already */
	if(option_verbose >= 4)
		ast_verbose(VERBOSE_PREFIX_4 "AlarmReceiver: Answering channel\n");

	if (chan->_state != AST_STATE_UP) {

		res = ast_answer(chan);

		if (res) {
			ast_module_user_remove(u);
			return -1;
		}
	}

	if(!res){

		/* Determine the protocol to receive in advance */
		/* Note: CID is the only one supported at this time */
		/* Others may be added later */

		if(!strcmp(signalling_type, A_CONTACT_ID))
			receive_contact_id(chan, data, fdtimeout, sdtimeout, toneloudness, &event_head);
			//getcnt++;
			//if(getcnt>2)adone=1;
		else
			res = -1;
	}


	#if 1
	/* Events queued by receiver, write them all out here if so configured */
	if((!res) && (log_individual_events == 0)){
		res = log_events(chan, signalling_type, event_head);
	}
	#endif


	#if 1
	/*
	* Free up the data allocated in our linked list
	*/
	res=0;
	amess[0]=0;
	for(elp = event_head; (elp != NULL);){
		efree = elp;
		res++;
		if(DEBUG_DEV)ast_log(LOG_NOTICE,"event free %i %s\n", res,elp->data);

		ast_copy_string(amp, elp->data, sizeof(elp->data));
		amp+=(sizeof(elp->data)-1);
		*amp=',';
		amp++;
		elp = elp->next;
		free(efree);
	}
	*amp=0;
	if(DEBUG_DEV)ast_log(LOG_NOTICE,"event free %i %s\n", res,amess);

	#endif



	#if 1
	/*
	* If configured, exec a command line at the end of the call
	*/
	if( !ast_strlen_zero(event_app) && !ast_strlen_zero(amess) ){

		char caCmd[256];
		//sprintf(caCmd,"%s %s",event_app,event_head->data);
        sprintf(caCmd,"%s %s",event_app,amess);
		if(DEBUG_DEV)ast_log(LOG_NOTICE,"executing: %s\n", caCmd);
		ast_safe_system(caCmd);
	}

	#endif

	ast_module_user_remove(u);

	return 0;
}

/*
* Load the configuration from the configuration file
*/
static int load_config(void)
{
	struct ast_config *cfg;
	const char *p;

	/* Read in the config file */

	cfg = ast_config_load(ALMRCV_CONFIG);

	if(!cfg){

		if(option_verbose >= 4)
			ast_verbose(VERBOSE_PREFIX_4 "AlarmReceiver: No config file\n");
		return 0;
	}
	else{

		p = ast_variable_retrieve(cfg, "general", "eventcmd");

		if(p){
			ast_copy_string(event_app, p, sizeof(event_app));
			event_app[sizeof(event_app) - 1] = '\0';
		}

		p = ast_variable_retrieve(cfg, "general", "loudness");
		if(p){
			toneloudness = atoi(p);
			if(toneloudness < 100)
				toneloudness = 100;
			if(toneloudness > 8192)
				toneloudness = 8192;
		}
		p = ast_variable_retrieve(cfg, "general", "fdtimeout");
		if(p){
			fdtimeout = atoi(p);
			if(fdtimeout < 1000)
				fdtimeout = 1000;
			if(fdtimeout > 10000)
				fdtimeout = 10000;
		}

		p = ast_variable_retrieve(cfg, "general", "sdtimeout");
		if(p){
			sdtimeout = atoi(p);
			if(sdtimeout < 110)
				sdtimeout = 110;
			if(sdtimeout > 4000)
				sdtimeout = 4000;
		}

		p = ast_variable_retrieve(cfg, "general", "logindividualevents");
		if(p){
			log_individual_events = ast_true(p);
		}

		p = ast_variable_retrieve(cfg, "general", "eventspooldir");

		if(p){
			ast_copy_string(event_spool_dir, p, sizeof(event_spool_dir));
			event_spool_dir[sizeof(event_spool_dir) - 1] = '\0';
		}

		p = ast_variable_retrieve(cfg, "general", "timestampformat");

		if(p){
			ast_copy_string(time_stamp_format, p, sizeof(time_stamp_format));
			time_stamp_format[sizeof(time_stamp_format) - 1] = '\0';
		}

		p = ast_variable_retrieve(cfg, "general", "db-family");

		if(p){
			ast_copy_string(db_family, p, sizeof(db_family));
			db_family[sizeof(db_family) - 1] = '\0';
		}
		ast_config_destroy(cfg);
	}
	return 1;

}

/*
* These functions are required to implement an Asterisk App.
*/
static int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);

	ast_module_user_hangup_all();

	return res;
}

static int load_module(void)
{
	if(load_config())
		return ast_register_application(app, alarmreceiver_exec, synopsis, descrip);
	else
		return AST_MODULE_LOAD_DECLINE;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Alarm Receiver for Asterisk");
