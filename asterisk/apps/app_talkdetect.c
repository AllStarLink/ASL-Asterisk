/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Playback a file with audio detect
 *
 * \author Mark Spencer <markster@digium.com>
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
#include "asterisk/utils.h"
#include "asterisk/dsp.h"

static char *app = "BackgroundDetect";

static char *synopsis = "Background a file with talk detect";

static char *descrip = 
"  BackgroundDetect(filename[|sil[|min|[max]]]):  Plays  back  a  given\n"
"filename, waiting for interruption from a given digit (the digit must\n"
"start the beginning of a valid extension, or it will be ignored).\n"
"During the playback of the file, audio is monitored in the receive\n"
"direction, and if a period of non-silence which is greater than 'min' ms\n"
"yet less than 'max' ms is followed by silence for at least 'sil' ms then\n"
"the audio playback is aborted and processing jumps to the 'talk' extension\n"
"if available.  If unspecified, sil, min, and max default to 1000, 100, and\n"
"infinity respectively.\n";


static int background_detect_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct ast_module_user *u;
	char *tmp;
	char *options;
	char *stringp;
	struct ast_frame *fr;
	int notsilent=0;
	struct timeval start = { 0, 0};
	int sil = 1000;
	int min = 100;
	int max = -1;
	int x;
	int origrformat=0;
	struct ast_dsp *dsp;
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "BackgroundDetect requires an argument (filename)\n");
		return -1;
	}

	u = ast_module_user_add(chan);

	tmp = ast_strdupa(data);

	stringp=tmp;
	strsep(&stringp, "|");
	options = strsep(&stringp, "|");
	if (options) {
		if ((sscanf(options, "%30d", &x) == 1) && (x > 0))
			sil = x;
		options = strsep(&stringp, "|");
		if (options) {
			if ((sscanf(options, "%30d", &x) == 1) && (x > 0))
				min = x;
			options = strsep(&stringp, "|");
			if (options) {
				if ((sscanf(options, "%30d", &x) == 1) && (x > 0))
					max = x;
			}
		}
	}
	ast_log(LOG_DEBUG, "Preparing detect of '%s', sil=%d,min=%d,max=%d\n", 
						tmp, sil, min, max);
	if (chan->_state != AST_STATE_UP) {
		/* Otherwise answer unless we're supposed to send this while on-hook */
		res = ast_answer(chan);
	}
	if (!res) {
		origrformat = chan->readformat;
		if ((res = ast_set_read_format(chan, AST_FORMAT_SLINEAR))) 
			ast_log(LOG_WARNING, "Unable to set read format to linear!\n");
	}
	if (!(dsp = ast_dsp_new())) {
		ast_log(LOG_WARNING, "Unable to allocate DSP!\n");
		res = -1;
	}
	if (!res) {
		ast_stopstream(chan);
		res = ast_streamfile(chan, tmp, chan->language);
		if (!res) {
			while(chan->stream) {
				res = ast_sched_wait(chan->sched);
				if ((res < 0) && !chan->timingfunc) {
					res = 0;
					break;
				}
				if (res < 0)
					res = 1000;
				res = ast_waitfor(chan, res);
				if (res < 0) {
					ast_log(LOG_WARNING, "Waitfor failed on %s\n", chan->name);
					break;
				} else if (res > 0) {
					fr = ast_read(chan);
					if (!fr) {
						res = -1;
						break;
					} else if (fr->frametype == AST_FRAME_DTMF) {
						char t[2];
						t[0] = fr->subclass;
						t[1] = '\0';
						if (ast_canmatch_extension(chan, chan->context, t, 1, chan->cid.cid_num)) {
							/* They entered a valid  extension, or might be anyhow */
							res = fr->subclass;
							ast_frfree(fr);
							break;
						}
					} else if ((fr->frametype == AST_FRAME_VOICE) && (fr->subclass == AST_FORMAT_SLINEAR)) {
						int totalsilence;
						int ms;
						res = ast_dsp_silence(dsp, fr, &totalsilence);
						if (res && (totalsilence > sil)) {
							/* We've been quiet a little while */
							if (notsilent) {
								/* We had heard some talking */
								ms = ast_tvdiff_ms(ast_tvnow(), start);
								ms -= sil;
								if (ms < 0)
									ms = 0;
								if ((ms > min) && ((max < 0) || (ms < max))) {
									char ms_str[10];
									ast_log(LOG_DEBUG, "Found qualified token of %d ms\n", ms);

									/* Save detected talk time (in milliseconds) */ 
									sprintf(ms_str, "%d", ms );	
									pbx_builtin_setvar_helper(chan, "TALK_DETECTED", ms_str);
									
									ast_goto_if_exists(chan, chan->context, "talk", 1);
									res = 0;
									ast_frfree(fr);
									break;
								} else
									ast_log(LOG_DEBUG, "Found unqualified token of %d ms\n", ms);
								notsilent = 0;
							}
						} else {
							if (!notsilent) {
								/* Heard some audio, mark the begining of the token */
								start = ast_tvnow();
								ast_log(LOG_DEBUG, "Start of voice token!\n");
								notsilent = 1;
							}
						}
						
					}
					ast_frfree(fr);
				}
				ast_sched_runq(chan->sched);
			}
			ast_stopstream(chan);
		} else {
			ast_log(LOG_WARNING, "ast_streamfile failed on %s for %s\n", chan->name, (char *)data);
			res = 0;
		}
	}
	if (res > -1) {
		if (origrformat && ast_set_read_format(chan, origrformat)) {
			ast_log(LOG_WARNING, "Failed to restore read format for %s to %s\n", 
				chan->name, ast_getformatname(origrformat));
		}
	}
	if (dsp)
		ast_dsp_free(dsp);
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
	return ast_register_application(app, background_detect_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Playback with Talk Detection");
