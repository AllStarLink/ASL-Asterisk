/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Matthew Fredrickson <creslin@digium.com>
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
 * \brief Trivial application to record a sound file
 *
 * \author Matthew Fredrickson <creslin@digium.com>
 *
 * \ingroup applications
 */
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 211528 $")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/dsp.h"
#include "asterisk/utils.h"
#include "asterisk/options.h"
#include "asterisk/app.h"


static char *app = "Record";

static char *synopsis = "Record to a file";

static char *descrip = 
"  Record(filename.format|silence[|maxduration][|options])\n\n"
"Records from the channel into a given filename. If the file exists it will\n"
"be overwritten.\n"
"- 'format' is the format of the file type to be recorded (wav, gsm, etc).\n"
"- 'silence' is the number of seconds of silence to allow before returning.\n"
"- 'maxduration' is the maximum recording duration in seconds. If missing\n"
"or 0 there is no maximum.\n"
"- 'options' may contain any of the following letters:\n"
"     'a' : append to existing recording rather than replacing\n"
"     'n' : do not answer, but record anyway if line not yet answered\n"
"     'q' : quiet (do not play a beep tone)\n"
"     's' : skip recording if the line is not yet answered\n"
"     't' : use alternate '*' terminator key (DTMF) instead of default '#'\n"
"     'x' : ignore all terminator keys (DTMF) and keep recording until hangup\n"
"\n"
"If filename contains '%d', these characters will be replaced with a number\n"
"incremented by one each time the file is recorded. A channel variable\n"
"named RECORDED_FILE will also be set, which contains the final filemname.\n\n"
"Use 'core show file formats' to see the available formats on your system\n\n"
"User can press '#' to terminate the recording and continue to the next priority.\n\n"
"If the user should hangup during a recording, all data will be lost and the\n"
"application will teminate. \n";


static int record_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	int count = 0;
	int percentflag = 0;
	char *filename, *ext = NULL, *silstr, *maxstr, *options;
	char *vdata, *p;
	int i = 0;
	char tmp[256];

	struct ast_filestream *s = '\0';
	struct ast_module_user *u;
	struct ast_frame *f = NULL;
	
	struct ast_dsp *sildet = NULL;   	/* silence detector dsp */
	int totalsilence = 0;
	int dspsilence = 0;
	int silence = 0;		/* amount of silence to allow */
	int gotsilence = 0;		/* did we timeout for silence? */
	int maxduration = 0;		/* max duration of recording in milliseconds */
	int gottimeout = 0;		/* did we timeout for maxduration exceeded? */
	int option_skip = 0;
	int option_noanswer = 0;
	int option_append = 0;
	int terminator = '#';
	int option_quiet = 0;
	int rfmt = 0;
	int flags;
	int waitres;
	struct ast_silence_generator *silgen = NULL;
	
	/* The next few lines of code parse out the filename and header from the input string */
	if (ast_strlen_zero(data)) { /* no data implies no filename or anything is present */
		ast_log(LOG_WARNING, "Record requires an argument (filename)\n");
		return -1;
	}

	u = ast_module_user_add(chan);

	/* Yay for strsep being easy */
	vdata = ast_strdupa(data);

	p = vdata;
	filename = strsep(&p, "|");
	silstr = strsep(&p, "|");
	maxstr = strsep(&p, "|");	
	options = strsep(&p, "|");
	
	if (filename) {
		if (strstr(filename, "%d"))
			percentflag = 1;
		ext = strrchr(filename, '.'); /* to support filename with a . in the filename, not format */
		if (!ext)
			ext = strchr(filename, ':');
		if (ext) {
			*ext = '\0';
			ext++;
		}
	}
	if (!ext) {
		ast_log(LOG_WARNING, "No extension specified to filename!\n");
		ast_module_user_remove(u);
		return -1;
	}
	if (silstr) {
		if ((sscanf(silstr, "%30d", &i) == 1) && (i > -1)) {
			silence = i * 1000;
		} else if (!ast_strlen_zero(silstr)) {
			ast_log(LOG_WARNING, "'%s' is not a valid silence duration\n", silstr);
		}
	}
	
	if (maxstr) {
		if ((sscanf(maxstr, "%30d", &i) == 1) && (i > -1))
			/* Convert duration to milliseconds */
			maxduration = i * 1000;
		else if (!ast_strlen_zero(maxstr))
			ast_log(LOG_WARNING, "'%s' is not a valid maximum duration\n", maxstr);
	}
	if (options) {
		/* Retain backwards compatibility with old style options */
		if (!strcasecmp(options, "skip"))
			option_skip = 1;
		else if (!strcasecmp(options, "noanswer"))
			option_noanswer = 1;
		else {
			if (strchr(options, 's'))
				option_skip = 1;
			if (strchr(options, 'n'))
				option_noanswer = 1;
			if (strchr(options, 'a'))
				option_append = 1;
			if (strchr(options, 't'))
				terminator = '*';
			if (strchr(options, 'x'))
				terminator = 0;
			if (strchr(options, 'q'))
				option_quiet = 1;
		}
	}
	
	/* done parsing */
	
	/* these are to allow the use of the %d in the config file for a wild card of sort to
	  create a new file with the inputed name scheme */
	if (percentflag) {
		AST_DECLARE_APP_ARGS(fname,
			AST_APP_ARG(piece)[100];
		);
		char *tmp2 = ast_strdupa(filename);
		char countstring[15];
		int i;

		/* Separate each piece out by the format specifier */
		AST_NONSTANDARD_APP_ARGS(fname, tmp2, '%');
		do {
			int tmplen;
			/* First piece has no leading percent, so it's copied verbatim */
			ast_copy_string(tmp, fname.piece[0], sizeof(tmp));
			tmplen = strlen(tmp);
			for (i = 1; i < fname.argc; i++) {
				if (fname.piece[i][0] == 'd') {
					/* Substitute the count */
					snprintf(countstring, sizeof(countstring), "%d", count);
					ast_copy_string(tmp + tmplen, countstring, sizeof(tmp) - tmplen);
					tmplen += strlen(countstring);
				} else if (tmplen + 2 < sizeof(tmp)) {
					/* Unknown format specifier - just copy it verbatim */
					tmp[tmplen++] = '%';
					tmp[tmplen++] = fname.piece[i][0];
				}
				/* Copy the remaining portion of the piece */
				ast_copy_string(tmp + tmplen, &(fname.piece[i][1]), sizeof(tmp) - tmplen);
			}
			count++;
		} while (ast_fileexists(tmp, ext, chan->language) > 0);
		pbx_builtin_setvar_helper(chan, "RECORDED_FILE", tmp);
	} else
		ast_copy_string(tmp, filename, sizeof(tmp));
	/* end of routine mentioned */
	
	
	
	if (chan->_state != AST_STATE_UP) {
		if (option_skip) {
			/* At the user's option, skip if the line is not up */
			ast_module_user_remove(u);
			return 0;
		} else if (!option_noanswer) {
			/* Otherwise answer unless we're supposed to record while on-hook */
			res = ast_answer(chan);
		}
	}
	
	if (res) {
		ast_log(LOG_WARNING, "Could not answer channel '%s'\n", chan->name);
		goto out;
	}
	
	if (!option_quiet) {
		/* Some code to play a nice little beep to signify the start of the record operation */
		res = ast_streamfile(chan, "beep", chan->language);
		if (!res) {
			res = ast_waitstream(chan, "");
		} else {
			ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", chan->name);
		}
		ast_stopstream(chan);
	}
		
	/* The end of beep code.  Now the recording starts */
		
	if (silence > 0) {
		rfmt = chan->readformat;
		res = ast_set_read_format(chan, AST_FORMAT_SLINEAR);
		if (res < 0) {
			ast_log(LOG_WARNING, "Unable to set to linear mode, giving up\n");
			ast_module_user_remove(u);
			return -1;
		}
		sildet = ast_dsp_new();
		if (!sildet) {
			ast_log(LOG_WARNING, "Unable to create silence detector :(\n");
			ast_module_user_remove(u);
			return -1;
		}
		ast_dsp_set_threshold(sildet, 256);
	} 
		
		
	flags = option_append ? O_CREAT|O_APPEND|O_WRONLY : O_CREAT|O_TRUNC|O_WRONLY;
	s = ast_writefile( tmp, ext, NULL, flags , 0, 0644);
		
	if (!s) {
		ast_log(LOG_WARNING, "Could not create file %s\n", filename);
		goto out;
	}

	if (ast_opt_transmit_silence)
		silgen = ast_channel_start_silence_generator(chan);
	
	/* Request a video update */
	ast_indicate(chan, AST_CONTROL_VIDUPDATE);
	
	if (maxduration <= 0)
		maxduration = -1;
	
	while ((waitres = ast_waitfor(chan, maxduration)) > -1) {
		if (maxduration > 0) {
			if (waitres == 0) {
				gottimeout = 1;
				break;
			}
			maxduration = waitres;
		}
		
		f = ast_read(chan);
		if (!f) {
			res = -1;
			break;
		}
		if (f->frametype == AST_FRAME_VOICE) {
			res = ast_writestream(s, f);
			
			if (res) {
				ast_log(LOG_WARNING, "Problem writing frame\n");
				ast_frfree(f);
				break;
			}
			
			if (silence > 0) {
				dspsilence = 0;
				ast_dsp_silence(sildet, f, &dspsilence);
				if (dspsilence) {
					totalsilence = dspsilence;
				} else {
					totalsilence = 0;
				}
				if (totalsilence > silence) {
					/* Ended happily with silence */
					ast_frfree(f);
					gotsilence = 1;
					break;
				}
			}
		} else if (f->frametype == AST_FRAME_VIDEO) {
			res = ast_writestream(s, f);
			
			if (res) {
				ast_log(LOG_WARNING, "Problem writing frame\n");
				ast_frfree(f);
				break;
			}
		} else if ((f->frametype == AST_FRAME_DTMF) &&
		    (f->subclass == terminator)) {
			ast_frfree(f);
			break;
		}
		ast_frfree(f);
	}
	if (!f) {
		ast_log(LOG_DEBUG, "Got hangup\n");
		res = -1;
	}
			
	if (gotsilence) {
		ast_stream_rewind(s, silence-1000);
		ast_truncstream(s);
	} else if (!gottimeout) {
		/* Strip off the last 1/4 second of it */
		ast_stream_rewind(s, 250);
		ast_truncstream(s);
	}
	ast_closestream(s);

	if (silgen)
		ast_channel_stop_silence_generator(chan, silgen);
	
 out:
	if ((silence > 0) && rfmt) {
		res = ast_set_read_format(chan, rfmt);
		if (res)
			ast_log(LOG_WARNING, "Unable to restore read format on '%s'\n", chan->name);
		if (sildet)
			ast_dsp_free(sildet);
	}

	ast_module_user_remove(u);

	return res;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);
	
	ast_module_user_hangup_all();

	return res;	
}

static int load_module(void)
{
	return ast_register_application(app, record_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Trivial Record Application");
