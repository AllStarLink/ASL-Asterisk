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
 * \brief PBX channel monitoring
 *
 * \author Mark Spencer <markster@digium.com>
 */
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 227944 $")

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <libgen.h>

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/logger.h"
#include "asterisk/file.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/manager.h"
#include "asterisk/cli.h"
#include "asterisk/monitor.h"
#include "asterisk/app.h"
#include "asterisk/utils.h"
#include "asterisk/config.h"
#include "asterisk/options.h"

AST_MUTEX_DEFINE_STATIC(monitorlock);

#define LOCK_IF_NEEDED(lock, needed) do { \
	if (needed) \
		ast_channel_lock(lock); \
	} while(0)

#define UNLOCK_IF_NEEDED(lock, needed) do { \
	if (needed) \
		ast_channel_unlock(lock); \
	} while (0)

static unsigned long seq = 0;

static char *monitor_synopsis = "Monitor a channel";

static char *monitor_descrip = "Monitor([file_format[:urlbase]|[fname_base]|[options]]):\n"
"Used to start monitoring a channel. The channel's input and output\n"
"voice packets are logged to files until the channel hangs up or\n"
"monitoring is stopped by the StopMonitor application.\n"
"  file_format		optional, if not set, defaults to \"wav\"\n"
"  fname_base		if set, changes the filename used to the one specified.\n"
"  options:\n"
"    m   - when the recording ends mix the two leg files into one and\n"
"          delete the two leg files.  If the variable MONITOR_EXEC is set, the\n"
"          application referenced in it will be executed instead of\n"
#ifdef HAVE_SOXMIX
"          soxmix and the raw leg files will NOT be deleted automatically.\n"
"          soxmix or MONITOR_EXEC is handed 3 arguments, the two leg files\n"
#else
"          sox and the raw leg files will NOT be deleted automatically.\n"
"          sox or MONITOR_EXEC is handed 3 arguments, the two leg files\n"
#endif
"          and a target mixed file name which is the same as the leg file names\n"
"          only without the in/out designator.\n"
"          If MONITOR_EXEC_ARGS is set, the contents will be passed on as\n"
"          additional arguements to MONITOR_EXEC\n"
"          Both MONITOR_EXEC and the Mix flag can be set from the\n"
"          administrator interface\n"
"\n"
"    b   - Don't begin recording unless a call is bridged to another channel\n"
"\nReturns -1 if monitor files can't be opened or if the channel is already\n"
"monitored, otherwise 0.\n"
;

static char *stopmonitor_synopsis = "Stop monitoring a channel";

static char *stopmonitor_descrip = "StopMonitor\n"
	"Stops monitoring a channel. Has no effect if the channel is not monitored\n";

static char *changemonitor_synopsis = "Change monitoring filename of a channel";

static char *changemonitor_descrip = "ChangeMonitor(filename_base)\n"
	"Changes monitoring filename of a channel. Has no effect if the channel is not monitored\n"
	"The argument is the new filename base to use for monitoring this channel.\n";

static char *pausemonitor_synopsis = "Pause monitoring of a channel";

static char *pausemonitor_descrip = "PauseMonitor\n"
	"Pauses monitoring of a channel until it is re-enabled by a call to UnpauseMonitor.\n";

static char *unpausemonitor_synopsis = "Unpause monitoring of a channel";

static char *unpausemonitor_descrip = "UnpauseMonitor\n"
	"Unpauses monitoring of a channel on which monitoring had\n"
	"previously been paused with PauseMonitor.\n";

static int ast_monitor_set_state(struct ast_channel *chan, int state)
{
	LOCK_IF_NEEDED(chan, 1);
	if (!chan->monitor) {
		UNLOCK_IF_NEEDED(chan, 1);
		return -1;
	}
	chan->monitor->state = state;
	UNLOCK_IF_NEEDED(chan, 1);
	return 0;
}

/* Start monitoring a channel */
int ast_monitor_start(	struct ast_channel *chan, const char *format_spec,
		const char *fname_base, int need_lock)
{
	int res = 0;
	char tmp[256];

	LOCK_IF_NEEDED(chan, need_lock);

	if (!(chan->monitor)) {
		struct ast_channel_monitor *monitor;
		char *channel_name, *p;

		/* Create monitoring directory if needed */
		if (mkdir(ast_config_AST_MONITOR_DIR, 0770) < 0) {
			if (errno != EEXIST) {
				ast_log(LOG_WARNING, "Unable to create audio monitor directory: %s\n",
					strerror(errno));
			}
		}

		if (!(monitor = ast_calloc(1, sizeof(*monitor)))) {
			UNLOCK_IF_NEEDED(chan, need_lock);
			return -1;
		}

		/* Determine file names */
		if (!ast_strlen_zero(fname_base)) {
			int directory = strchr(fname_base, '/') ? 1 : 0;
			const char *absolute = *fname_base == '/' ? "" : "/";
			/* try creating the directory just in case it doesn't exist */
			if (directory) {
				char *name = strdup(fname_base);
				snprintf(tmp, sizeof(tmp), "mkdir -p \"%s\"",dirname(name));
				free(name);
				ast_safe_system(tmp);
			}
			snprintf(monitor->read_filename, FILENAME_MAX, "%s%s%s-in",
						directory ? "" : ast_config_AST_MONITOR_DIR, absolute, fname_base);
			snprintf(monitor->write_filename, FILENAME_MAX, "%s%s%s-out",
						directory ? "" : ast_config_AST_MONITOR_DIR, absolute, fname_base);
			snprintf(monitor->filename_base, FILENAME_MAX, "%s/%s",
					 ast_config_AST_MONITOR_DIR, fname_base);
		} else {
			ast_mutex_lock(&monitorlock);
			snprintf(monitor->read_filename, FILENAME_MAX, "%s/audio-in-%ld",
						ast_config_AST_MONITOR_DIR, seq);
			snprintf(monitor->write_filename, FILENAME_MAX, "%s/audio-out-%ld",
						ast_config_AST_MONITOR_DIR, seq);
			seq++;
			ast_mutex_unlock(&monitorlock);

			channel_name = ast_strdupa(chan->name);
			while ((p = strchr(channel_name, '/'))) {
				*p = '-';
			}
			snprintf(monitor->filename_base, FILENAME_MAX, "%s/%d-%s",
					 ast_config_AST_MONITOR_DIR, (int)time(NULL), channel_name);
			monitor->filename_changed = 1;
		}

		monitor->stop = ast_monitor_stop;

		/* Determine file format */
		if (!ast_strlen_zero(format_spec)) {
			monitor->format = strdup(format_spec);
		} else {
			monitor->format = strdup("wav");
		}
		
		/* open files */
		if (ast_fileexists(monitor->read_filename, NULL, NULL) > 0) {
			ast_filedelete(monitor->read_filename, NULL);
		}
		if (!(monitor->read_stream = ast_writefile(monitor->read_filename,
						monitor->format, NULL,
						O_CREAT|O_TRUNC|O_WRONLY, 0, 0644))) {
			ast_log(LOG_WARNING, "Could not create file %s\n",
						monitor->read_filename);
			free(monitor);
			UNLOCK_IF_NEEDED(chan, need_lock);
			return -1;
		}
		if (ast_fileexists(monitor->write_filename, NULL, NULL) > 0) {
			ast_filedelete(monitor->write_filename, NULL);
		}
		if (!(monitor->write_stream = ast_writefile(monitor->write_filename,
						monitor->format, NULL,
						O_CREAT|O_TRUNC|O_WRONLY, 0, 0644))) {
			ast_log(LOG_WARNING, "Could not create file %s\n",
						monitor->write_filename);
			ast_closestream(monitor->read_stream);
			free(monitor);
			UNLOCK_IF_NEEDED(chan, need_lock);
			return -1;
		}
		chan->monitor = monitor;
		ast_monitor_set_state(chan, AST_MONITOR_RUNNING);
		/* so we know this call has been monitored in case we need to bill for it or something */
		pbx_builtin_setvar_helper(chan, "__MONITORED","true");
	} else {
		ast_log(LOG_DEBUG,"Cannot start monitoring %s, already monitored\n",
					chan->name);
		res = -1;
	}

	UNLOCK_IF_NEEDED(chan, need_lock);

	return res;
}

/*
 * The file format extensions that Asterisk uses are not all the same as that
 * which soxmix expects.  This function ensures that the format used as the
 * extension on the filename is something soxmix will understand.
 */
static const char *get_soxmix_format(const char *format)
{
	const char *res = format;

	if (!strcasecmp(format,"ulaw"))
		res = "ul";
	if (!strcasecmp(format,"alaw"))
		res = "al";
	
	return res;
}

/* Stop monitoring a channel */
int ast_monitor_stop(struct ast_channel *chan, int need_lock)
{
	int delfiles = 0;

	LOCK_IF_NEEDED(chan, need_lock);

	if (chan->monitor) {
		char filename[ FILENAME_MAX ];

		if (chan->monitor->read_stream) {
			ast_closestream(chan->monitor->read_stream);
		}
		if (chan->monitor->write_stream) {
			ast_closestream(chan->monitor->write_stream);
		}

		if (chan->monitor->filename_changed && !ast_strlen_zero(chan->monitor->filename_base)) {
			if (ast_fileexists(chan->monitor->read_filename,NULL,NULL) > 0) {
				snprintf(filename, FILENAME_MAX, "%s-in", chan->monitor->filename_base);
				if (ast_fileexists(filename, NULL, NULL) > 0) {
					ast_filedelete(filename, NULL);
				}
				ast_filerename(chan->monitor->read_filename, filename, chan->monitor->format);
			} else {
				ast_log(LOG_WARNING, "File %s not found\n", chan->monitor->read_filename);
			}

			if (ast_fileexists(chan->monitor->write_filename,NULL,NULL) > 0) {
				snprintf(filename, FILENAME_MAX, "%s-out", chan->monitor->filename_base);
				if (ast_fileexists(filename, NULL, NULL) > 0) {
					ast_filedelete(filename, NULL);
				}
				ast_filerename(chan->monitor->write_filename, filename, chan->monitor->format);
			} else {
				ast_log(LOG_WARNING, "File %s not found\n", chan->monitor->write_filename);
			}
		}

		if (chan->monitor->joinfiles && !ast_strlen_zero(chan->monitor->filename_base)) {
			char tmp[1024];
			char tmp2[1024];
			const char *format = !strcasecmp(chan->monitor->format,"wav49") ? "WAV" : chan->monitor->format;
			char *name = chan->monitor->filename_base;
			int directory = strchr(name, '/') ? 1 : 0;
			char *dir = directory ? "" : ast_config_AST_MONITOR_DIR;
			const char *execute, *execute_args;
			const char *absolute = *name == '/' ? "" : "/";

			/* Set the execute application */
			execute = pbx_builtin_getvar_helper(chan, "MONITOR_EXEC");
			if (ast_strlen_zero(execute)) {
#ifdef HAVE_SOXMIX
				execute = "nice -n 19 soxmix";
#else
				execute = "nice -n 19 sox -m";
#endif
				format = get_soxmix_format(format);
				delfiles = 1;
			} 
			execute_args = pbx_builtin_getvar_helper(chan, "MONITOR_EXEC_ARGS");
			if (ast_strlen_zero(execute_args)) {
				execute_args = "";
			}
			
			snprintf(tmp, sizeof(tmp), "%s \"%s%s%s-in.%s\" \"%s%s%s-out.%s\" \"%s%s%s.%s\" %s &", execute, dir, absolute, name, format, dir, absolute, name, format, dir, absolute, name, format,execute_args);
			if (delfiles) {
				snprintf(tmp2,sizeof(tmp2), "( %s& rm -f \"%s%s%s-\"* ) &",tmp, dir, absolute, name); /* remove legs when done mixing */
				ast_copy_string(tmp, tmp2, sizeof(tmp));
			}
			ast_log(LOG_DEBUG,"monitor executing %s\n",tmp);
			if (ast_safe_system(tmp) == -1)
				ast_log(LOG_WARNING, "Execute of %s failed.\n",tmp);
		}
		
		free(chan->monitor->format);
		free(chan->monitor);
		chan->monitor = NULL;
	}

	UNLOCK_IF_NEEDED(chan, need_lock);

	return 0;
}


/* Pause monitoring of a channel */
int ast_monitor_pause(struct ast_channel *chan)
{
	return ast_monitor_set_state(chan, AST_MONITOR_PAUSED);
}

/* Unpause monitoring of a channel */
int ast_monitor_unpause(struct ast_channel *chan)
{
	return ast_monitor_set_state(chan, AST_MONITOR_RUNNING);
}

static int pause_monitor_exec(struct ast_channel *chan, void *data)
{
	return ast_monitor_pause(chan);
}

static int unpause_monitor_exec(struct ast_channel *chan, void *data)
{
	return ast_monitor_unpause(chan);
}

/* Change monitoring filename of a channel */
int ast_monitor_change_fname(struct ast_channel *chan, const char *fname_base, int need_lock)
{
	char tmp[256];
	if (ast_strlen_zero(fname_base)) {
		ast_log(LOG_WARNING, "Cannot change monitor filename of channel %s to null\n", chan->name);
		return -1;
	}

	LOCK_IF_NEEDED(chan, need_lock);

	if (chan->monitor) {
		int directory = strchr(fname_base, '/') ? 1 : 0;
		const char *absolute = *fname_base == '/' ? "" : "/";
		char tmpstring[sizeof(chan->monitor->filename_base)] = "";
		int i, fd[2] = { -1, -1 }, doexit = 0;

		/* before continuing, see if we're trying to rename the file to itself... */
		snprintf(tmpstring, sizeof(tmpstring), "%s%s%s", directory ? "" : ast_config_AST_MONITOR_DIR, absolute, fname_base);

		/*!\note We cannot just compare filenames, due to symlinks, relative
		 * paths, and other possible filesystem issues.  We could use
		 * realpath(3), but its use is discouraged.  However, if we try to
		 * create the same file from two different paths, the second will
		 * fail, and so we have our notification that the filenames point to
		 * the same path.
		 *
		 * Remember, also, that we're using the basename of the file (i.e.
		 * the file without the format suffix), so it does not already exist
		 * and we aren't interfering with the recording itself.
		 */
		if (option_debug > 2) {
			ast_log(LOG_DEBUG, "comparing tmpstring %s to filename_base %s\n", tmpstring, chan->monitor->filename_base);
		}
		if ((fd[0] = open(tmpstring, O_CREAT | O_WRONLY, 0644)) < 0 ||
			(fd[1] = open(chan->monitor->filename_base, O_CREAT | O_EXCL | O_WRONLY, 0644)) < 0) {
			if (fd[0] < 0) {
				ast_log(LOG_ERROR, "Unable to compare filenames: %s\n", strerror(errno));
			} else {
				if (option_debug > 2) {
					ast_log(LOG_DEBUG, "No need to rename monitor filename to itself\n");
				}
			}
			doexit = 1;
		}

		/* Cleanup temporary files */
		for (i = 0; i < 2; i++) {
			if (fd[i] >= 0) {
				while (close(fd[i]) < 0 && errno == EINTR);
			}
		}
		unlink(tmpstring);
		unlink(chan->monitor->filename_base);

		if (doexit) {
			UNLOCK_IF_NEEDED(chan, need_lock);
			return 0;
		}

		/* try creating the directory just in case it doesn't exist */
		if (directory) {
			char *name = strdup(fname_base);
			snprintf(tmp, sizeof(tmp), "mkdir -p %s",dirname(name));
			free(name);
			ast_safe_system(tmp);
		}

		ast_copy_string(chan->monitor->filename_base, tmpstring, sizeof(chan->monitor->filename_base));
		chan->monitor->filename_changed = 1;
	} else {
		ast_log(LOG_WARNING, "Cannot change monitor filename of channel %s to %s, monitoring not started\n", chan->name, fname_base);
	}

	UNLOCK_IF_NEEDED(chan, need_lock);

	return 0;
}

static int start_monitor_exec(struct ast_channel *chan, void *data)
{
	char *arg = NULL;
	char *format = NULL;
	char *fname_base = NULL;
	char *options = NULL;
	char *delay = NULL;
	char *urlprefix = NULL;
	char tmp[256];
	int joinfiles = 0;
	int waitforbridge = 0;
	int res = 0;
	
	/* Parse arguments. */
	if (!ast_strlen_zero((char*)data)) {
		arg = ast_strdupa((char*)data);
		format = arg;
		fname_base = strchr(arg, '|');
		if (fname_base) {
			*fname_base = 0;
			fname_base++;
			if ((options = strchr(fname_base, '|'))) {
				*options = 0;
				options++;
				if (strchr(options, 'm'))
					joinfiles = 1;
				if (strchr(options, 'b'))
					waitforbridge = 1;
			}
		}
		arg = strchr(format,':');
		if (arg) {
			*arg++ = 0;
			urlprefix = arg;
		}
	}
	if (urlprefix) {
		snprintf(tmp,sizeof(tmp) - 1,"%s/%s.%s",urlprefix,fname_base,
			((strcmp(format,"gsm")) ? "wav" : "gsm"));
		if (!chan->cdr && !(chan->cdr = ast_cdr_alloc()))
			return -1;
		ast_cdr_setuserfield(chan, tmp);
	}
	if (waitforbridge) {
		/* We must remove the "b" option if listed.  In principle none of
		   the following could give NULL results, but we check just to
		   be pedantic. Reconstructing with checks for 'm' option does not
		   work if we end up adding more options than 'm' in the future. */
		delay = ast_strdupa(data);
		options = strrchr(delay, '|');
		if (options) {
			arg = strchr(options, 'b');
			if (arg) {
				*arg = 'X';
				pbx_builtin_setvar_helper(chan,"AUTO_MONITOR",delay);
			}
		}
		return 0;
	}

	res = ast_monitor_start(chan, format, fname_base, 1);
	if (res < 0)
		res = ast_monitor_change_fname(chan, fname_base, 1);
	ast_monitor_setjoinfiles(chan, joinfiles);

	return res;
}

static int stop_monitor_exec(struct ast_channel *chan, void *data)
{
	return ast_monitor_stop(chan, 1);
}

static int change_monitor_exec(struct ast_channel *chan, void *data)
{
	return ast_monitor_change_fname(chan, (const char*)data, 1);
}

static char start_monitor_action_help[] =
"Description: The 'Monitor' action may be used to record the audio on a\n"
"  specified channel.  The following parameters may be used to control\n"
"  this:\n"
"  Channel     - Required.  Used to specify the channel to record.\n"
"  File        - Optional.  Is the name of the file created in the\n"
"                monitor spool directory.  Defaults to the same name\n"
"                as the channel (with slashes replaced with dashes).\n"
"  Format      - Optional.  Is the audio recording format.  Defaults\n"
"                to \"wav\".\n"
"  Mix         - Optional.  Boolean parameter as to whether to mix\n"
"                the input and output channels together after the\n"
"                recording is finished.\n";

static int start_monitor_action(struct mansession *s, const struct message *m)
{
	struct ast_channel *c = NULL;
	const char *name = astman_get_header(m, "Channel");
	const char *fname = astman_get_header(m, "File");
	const char *format = astman_get_header(m, "Format");
	const char *mix = astman_get_header(m, "Mix");
	char *d;
	
	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	c = ast_get_channel_by_name_locked(name);
	if (!c) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}

	if (ast_strlen_zero(fname)) {
		/* No filename base specified, default to channel name as per CLI */		
		fname = ast_strdupa(c->name);
		/* Channels have the format technology/channel_name - have to replace that /  */
		if ((d = strchr(fname, '/'))) 
			*d = '-';
	}
	
	if (ast_monitor_start(c, format, fname, 1)) {
		if (ast_monitor_change_fname(c, fname, 1)) {
			astman_send_error(s, m, "Could not start monitoring channel");
			ast_channel_unlock(c);
			return 0;
		}
	}

	if (ast_true(mix)) {
		ast_monitor_setjoinfiles(c, 1);
	}

	ast_channel_unlock(c);
	astman_send_ack(s, m, "Started monitoring channel");
	return 0;
}

static char stop_monitor_action_help[] =
"Description: The 'StopMonitor' action may be used to end a previously\n"
"  started 'Monitor' action.  The only parameter is 'Channel', the name\n"
"  of the channel monitored.\n";

static int stop_monitor_action(struct mansession *s, const struct message *m)
{
	struct ast_channel *c = NULL;
	const char *name = astman_get_header(m, "Channel");
	int res;
	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	c = ast_get_channel_by_name_locked(name);
	if (!c) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}
	res = ast_monitor_stop(c, 1);
	ast_channel_unlock(c);
	if (res) {
		astman_send_error(s, m, "Could not stop monitoring channel");
		return 0;
	}
	astman_send_ack(s, m, "Stopped monitoring channel");
	return 0;
}

static char change_monitor_action_help[] =
"Description: The 'ChangeMonitor' action may be used to change the file\n"
"  started by a previous 'Monitor' action.  The following parameters may\n"
"  be used to control this:\n"
"  Channel     - Required.  Used to specify the channel to record.\n"
"  File        - Required.  Is the new name of the file created in the\n"
"                monitor spool directory.\n";

static int change_monitor_action(struct mansession *s, const struct message *m)
{
	struct ast_channel *c = NULL;
	const char *name = astman_get_header(m, "Channel");
	const char *fname = astman_get_header(m, "File");
	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	if (ast_strlen_zero(fname)) {
		astman_send_error(s, m, "No filename specified");
		return 0;
	}
	c = ast_get_channel_by_name_locked(name);
	if (!c) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}
	if (ast_monitor_change_fname(c, fname, 1)) {
		astman_send_error(s, m, "Could not change monitored filename of channel");
		ast_channel_unlock(c);
		return 0;
	}
	ast_channel_unlock(c);
	astman_send_ack(s, m, "Changed monitor filename");
	return 0;
}

void ast_monitor_setjoinfiles(struct ast_channel *chan, int turnon)
{
	if (chan->monitor)
		chan->monitor->joinfiles = turnon;
}

#define IS_NULL_STRING(string) ((!(string)) || (ast_strlen_zero((string))))

enum MONITOR_PAUSING_ACTION
{
	MONITOR_ACTION_PAUSE,
	MONITOR_ACTION_UNPAUSE
};
	  
static int do_pause_or_unpause(struct mansession *s, const struct message *m, int action)
{
	struct ast_channel *c = NULL;
	const char *name = astman_get_header(m, "Channel");
	
	if (IS_NULL_STRING(name)) {
		astman_send_error(s, m, "No channel specified");
		return -1;
	}
	
	c = ast_get_channel_by_name_locked(name);
	if (!c) {
		astman_send_error(s, m, "No such channel");
		return -1;
	}

	if (action == MONITOR_ACTION_PAUSE)
		ast_monitor_pause(c);
	else
		ast_monitor_unpause(c);
	
	ast_channel_unlock(c);
	astman_send_ack(s, m, (action == MONITOR_ACTION_PAUSE ? "Paused monitoring of the channel" : "Unpaused monitoring of the channel"));
	return 0;	
}

static char pause_monitor_action_help[] =
	"Description: The 'PauseMonitor' action may be used to temporarily stop the\n"
	" recording of a channel.  The following parameters may\n"
	" be used to control this:\n"
	"  Channel     - Required.  Used to specify the channel to record.\n";

static int pause_monitor_action(struct mansession *s, const struct message *m)
{
	return do_pause_or_unpause(s, m, MONITOR_ACTION_PAUSE);
}

static char unpause_monitor_action_help[] =
	"Description: The 'UnpauseMonitor' action may be used to re-enable recording\n"
	"  of a channel after calling PauseMonitor.  The following parameters may\n"
	"  be used to control this:\n"
	"  Channel     - Required.  Used to specify the channel to record.\n";

static int unpause_monitor_action(struct mansession *s, const struct message *m)
{
	return do_pause_or_unpause(s, m, MONITOR_ACTION_UNPAUSE);
}
	

static int load_module(void)
{
	ast_register_application("Monitor", start_monitor_exec, monitor_synopsis, monitor_descrip);
	ast_register_application("StopMonitor", stop_monitor_exec, stopmonitor_synopsis, stopmonitor_descrip);
	ast_register_application("ChangeMonitor", change_monitor_exec, changemonitor_synopsis, changemonitor_descrip);
	ast_register_application("PauseMonitor", pause_monitor_exec, pausemonitor_synopsis, pausemonitor_descrip);
	ast_register_application("UnpauseMonitor", unpause_monitor_exec, unpausemonitor_synopsis, unpausemonitor_descrip);
	ast_manager_register2("Monitor", EVENT_FLAG_CALL, start_monitor_action, monitor_synopsis, start_monitor_action_help);
	ast_manager_register2("StopMonitor", EVENT_FLAG_CALL, stop_monitor_action, stopmonitor_synopsis, stop_monitor_action_help);
	ast_manager_register2("ChangeMonitor", EVENT_FLAG_CALL, change_monitor_action, changemonitor_synopsis, change_monitor_action_help);
	ast_manager_register2("PauseMonitor", EVENT_FLAG_CALL, pause_monitor_action, pausemonitor_synopsis, pause_monitor_action_help);
	ast_manager_register2("UnpauseMonitor", EVENT_FLAG_CALL, unpause_monitor_action, unpausemonitor_synopsis, unpause_monitor_action_help);

	return 0;
}

static int unload_module(void)
{
	ast_unregister_application("Monitor");
	ast_unregister_application("StopMonitor");
	ast_unregister_application("ChangeMonitor");
	ast_unregister_application("PauseMonitor");
	ast_unregister_application("UnpauseMonitor");
	ast_manager_unregister("Monitor");
	ast_manager_unregister("StopMonitor");
	ast_manager_unregister("ChangeMonitor");
	ast_manager_unregister("PauseMonitor");
	ast_manager_unregister("UnpauseMonitor");

	return 0;
}

/* usecount semantics need to be defined */
AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS, "Call Monitoring Resource",
		.load = load_module,
		.unload = unload_module,
		);
