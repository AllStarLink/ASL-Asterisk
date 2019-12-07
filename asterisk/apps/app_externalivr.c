/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Kevin P. Fleming <kpfleming@digium.com>
 *
 * Portions taken from the file-based music-on-hold work
 * created by Anthony Minessale II in res_musiconhold.c
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
 * \brief External IVR application interface
 *
 * \author Kevin P. Fleming <kpfleming@digium.com>
 *
 * \note Portions taken from the file-based music-on-hold work
 * created by Anthony Minessale II in res_musiconhold.c
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<depend>working_fork</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 172438 $")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#ifdef HAVE_CAP
#include <sys/capability.h>
#endif /* HAVE_CAP */

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/linkedlists.h"
#include "asterisk/app.h"
#include "asterisk/utils.h"
#include "asterisk/options.h"

static const char *app = "ExternalIVR";

static const char *synopsis = "Interfaces with an external IVR application";

static const char *descrip = 
"  ExternalIVR(command[|arg[|arg...]]): Forks a process to run the supplied command,\n"
"and starts a generator on the channel. The generator's play list is\n"
"controlled by the external application, which can add and clear entries\n"
"via simple commands issued over its stdout. The external application\n"
"will receive all DTMF events received on the channel, and notification\n"
"if the channel is hung up. The application will not be forcibly terminated\n"
"when the channel is hung up.\n"
"See doc/externalivr.txt for a protocol specification.\n";

/* XXX the parser in gcc 2.95 gets confused if you don't put a space between 'name' and the comma */
#define ast_chan_log(level, channel, format, ...) ast_log(level, "%s: " format, channel->name , ## __VA_ARGS__)

struct playlist_entry {
	AST_LIST_ENTRY(playlist_entry) list;
	char filename[1];
};

struct ivr_localuser {
	struct ast_channel *chan;
	AST_LIST_HEAD(playlist, playlist_entry) playlist;
	AST_LIST_HEAD(finishlist, playlist_entry) finishlist;
	int abort_current_sound;
	int playing_silence;
	int option_autoclear;
};


struct gen_state {
	struct ivr_localuser *u;
	struct ast_filestream *stream;
	struct playlist_entry *current;
	int sample_queue;
};

static void send_child_event(FILE *handle, const char event, const char *data,
			     const struct ast_channel *chan)
{
	char tmp[256];

	if (!data) {
		snprintf(tmp, sizeof(tmp), "%c,%10d", event, (int)time(NULL));
	} else {
		snprintf(tmp, sizeof(tmp), "%c,%10d,%s", event, (int)time(NULL), data);
	}

	fprintf(handle, "%s\n", tmp);
	ast_chan_log(LOG_DEBUG, chan, "sent '%s'\n", tmp);
}

static void *gen_alloc(struct ast_channel *chan, void *params)
{
	struct ivr_localuser *u = params;
	struct gen_state *state;
	
	if (!(state = ast_calloc(1, sizeof(*state))))
		return NULL;

	state->u = u;

	return state;
}

static void gen_closestream(struct gen_state *state)
{
	if (!state->stream)
		return;

	ast_closestream(state->stream);
	state->u->chan->stream = NULL;
	state->stream = NULL;
}

static void gen_release(struct ast_channel *chan, void *data)
{
	struct gen_state *state = data;

	gen_closestream(state);
	free(data);
}

/* caller has the playlist locked */
static int gen_nextfile(struct gen_state *state)
{
	struct ivr_localuser *u = state->u;
	char *file_to_stream;
	
	u->abort_current_sound = 0;
	u->playing_silence = 0;
	gen_closestream(state);

	while (!state->stream) {
		state->current = AST_LIST_REMOVE_HEAD(&u->playlist, list);
		if (state->current) {
			file_to_stream = state->current->filename;
		} else {
			file_to_stream = "silence/10";
			u->playing_silence = 1;
		}

		if (!(state->stream = ast_openstream_full(u->chan, file_to_stream, u->chan->language, 1))) {
			ast_chan_log(LOG_WARNING, u->chan, "File '%s' could not be opened: %s\n", file_to_stream, strerror(errno));
			if (!u->playing_silence) {
				continue;
			} else { 
				break;
			}
		}
	}

	return (!state->stream);
}

static struct ast_frame *gen_readframe(struct gen_state *state)
{
	struct ast_frame *f = NULL;
	struct ivr_localuser *u = state->u;
	
	if (u->abort_current_sound ||
	    (u->playing_silence && AST_LIST_FIRST(&u->playlist))) {
		gen_closestream(state);
		AST_LIST_LOCK(&u->playlist);
		gen_nextfile(state);
		AST_LIST_UNLOCK(&u->playlist);
	}

	if (!(state->stream && (f = ast_readframe(state->stream)))) {
		if (state->current) {
			AST_LIST_LOCK(&u->finishlist);
			AST_LIST_INSERT_TAIL(&u->finishlist, state->current, list);
			AST_LIST_UNLOCK(&u->finishlist);
			state->current = NULL;
		}
		if (!gen_nextfile(state))
			f = ast_readframe(state->stream);
	}

	return f;
}

static int gen_generate(struct ast_channel *chan, void *data, int len, int samples)
{
	struct gen_state *state = data;
	struct ast_frame *f = NULL;
	int res = 0;

	state->sample_queue += samples;

	while (state->sample_queue > 0) {
		if (!(f = gen_readframe(state)))
			return -1;

		res = ast_write(chan, f);
		ast_frfree(f);
		if (res < 0) {
			ast_chan_log(LOG_WARNING, chan, "Failed to write frame: %s\n", strerror(errno));
			return -1;
		}
		state->sample_queue -= f->samples;
	}

	return res;
}

static struct ast_generator gen =
{
	alloc: gen_alloc,
	release: gen_release,
	generate: gen_generate,
};

static struct playlist_entry *make_entry(const char *filename)
{
	struct playlist_entry *entry;
	
	if (!(entry = ast_calloc(1, sizeof(*entry) + strlen(filename) + 10))) /* XXX why 10 ? */
		return NULL;

	strcpy(entry->filename, filename);

	return entry;
}

static int app_exec(struct ast_channel *chan, void *data)
{
	struct ast_module_user *lu;
	struct playlist_entry *entry;
	const char *args = data;
	int child_stdin[2] = { 0,0 };
	int child_stdout[2] = { 0,0 };
	int child_stderr[2] = { 0,0 };
	int res = -1;
	int test_available_fd = -1;
	int gen_active = 0;
	int pid;
	char *argv[32];
	int argc = 1;
	char *buf, *command;
	FILE *child_commands = NULL;
	FILE *child_errors = NULL;
	FILE *child_events = NULL;
	struct ivr_localuser foo = {
		.playlist = AST_LIST_HEAD_INIT_VALUE,
		.finishlist = AST_LIST_HEAD_INIT_VALUE,
	};
	struct ivr_localuser *u = &foo;
	sigset_t fullset, oldset;

	lu = ast_module_user_add(chan);

	sigfillset(&fullset);
	pthread_sigmask(SIG_BLOCK, &fullset, &oldset);

	u->abort_current_sound = 0;
	u->chan = chan;
	
	if (ast_strlen_zero(args)) {
		ast_log(LOG_WARNING, "ExternalIVR requires a command to execute\n");
		ast_module_user_remove(lu);
		return -1;	
	}

	buf = ast_strdupa(data);

	argc = ast_app_separate_args(buf, '|', argv, sizeof(argv) / sizeof(argv[0]));

	if (pipe(child_stdin)) {
		ast_chan_log(LOG_WARNING, chan, "Could not create pipe for child input: %s\n", strerror(errno));
		goto exit;
	}

	if (pipe(child_stdout)) {
		ast_chan_log(LOG_WARNING, chan, "Could not create pipe for child output: %s\n", strerror(errno));
		goto exit;
	}

	if (pipe(child_stderr)) {
		ast_chan_log(LOG_WARNING, chan, "Could not create pipe for child errors: %s\n", strerror(errno));
		goto exit;
	}

	if (chan->_state != AST_STATE_UP) {
		ast_answer(chan);
	}

	if (ast_activate_generator(chan, &gen, u) < 0) {
		ast_chan_log(LOG_WARNING, chan, "Failed to activate generator\n");
		goto exit;
	} else
		gen_active = 1;

	pid = fork();
	if (pid < 0) {
		ast_log(LOG_WARNING, "Failed to fork(): %s\n", strerror(errno));
		goto exit;
	}

	if (!pid) {
		/* child process */
		int i;
#ifdef HAVE_CAP
		cap_t cap = cap_from_text("cap_net_admin-eip");

		if (cap_set_proc(cap)) {
			/* Careful with order! Logging cannot happen after we close FDs */
			ast_log(LOG_WARNING, "Unable to remove capabilities.\n");
		}
		cap_free(cap);
#endif

		signal(SIGPIPE, SIG_DFL);
		pthread_sigmask(SIG_UNBLOCK, &fullset, NULL);

		if (ast_opt_high_priority)
			ast_set_priority(0);

		dup2(child_stdin[0], STDIN_FILENO);
		dup2(child_stdout[1], STDOUT_FILENO);
		dup2(child_stderr[1], STDERR_FILENO);
		for (i = STDERR_FILENO + 1; i < 1024; i++)
			close(i);
		execv(argv[0], argv);
		fprintf(stderr, "Failed to execute '%s': %s\n", argv[0], strerror(errno));
		_exit(1);
	} else {
		/* parent process */
		int child_events_fd = child_stdin[1];
		int child_commands_fd = child_stdout[0];
		int child_errors_fd = child_stderr[0];
		struct ast_frame *f;
		int ms;
		int exception;
		int ready_fd;
		int waitfds[2] = { child_errors_fd, child_commands_fd };
		struct ast_channel *rchan;

		pthread_sigmask(SIG_SETMASK, &oldset, NULL);

		close(child_stdin[0]);
		child_stdin[0] = 0;
		close(child_stdout[1]);
		child_stdout[1] = 0;
		close(child_stderr[1]);
		child_stderr[1] = 0;

		if (!(child_events = fdopen(child_events_fd, "w"))) {
			ast_chan_log(LOG_WARNING, chan, "Could not open stream for child events\n");
			goto exit;
		}

		if (!(child_commands = fdopen(child_commands_fd, "r"))) {
			ast_chan_log(LOG_WARNING, chan, "Could not open stream for child commands\n");
			goto exit;
		}

		if (!(child_errors = fdopen(child_errors_fd, "r"))) {
			ast_chan_log(LOG_WARNING, chan, "Could not open stream for child errors\n");
			goto exit;
		}

		test_available_fd = open("/dev/null", O_RDONLY);

		setvbuf(child_events, NULL, _IONBF, 0);
		setvbuf(child_commands, NULL, _IONBF, 0);
		setvbuf(child_errors, NULL, _IONBF, 0);

		res = 0;

		while (1) {
			if (ast_test_flag(chan, AST_FLAG_ZOMBIE)) {
				ast_chan_log(LOG_NOTICE, chan, "Is a zombie\n");
				res = -1;
				break;
			}

			if (ast_check_hangup(chan)) {
				ast_chan_log(LOG_NOTICE, chan, "Got check_hangup\n");
				send_child_event(child_events, 'H', NULL, chan);
				res = -1;
				break;
			}

			ready_fd = 0;
			ms = 100;
			errno = 0;
			exception = 0;

			rchan = ast_waitfor_nandfds(&chan, 1, waitfds, 2, &exception, &ready_fd, &ms);

			if (!AST_LIST_EMPTY(&u->finishlist)) {
				AST_LIST_LOCK(&u->finishlist);
				while ((entry = AST_LIST_REMOVE_HEAD(&u->finishlist, list))) {
					send_child_event(child_events, 'F', entry->filename, chan);
					free(entry);
				}
				AST_LIST_UNLOCK(&u->finishlist);
			}

			if (rchan) {
				/* the channel has something */
				f = ast_read(chan);
				if (!f) {
					ast_chan_log(LOG_NOTICE, chan, "Returned no frame\n");
					send_child_event(child_events, 'H', NULL, chan);
					res = -1;
					break;
				}

				if (f->frametype == AST_FRAME_DTMF) {
					send_child_event(child_events, f->subclass, NULL, chan);
					if (u->option_autoclear) {
						if (!u->abort_current_sound && !u->playing_silence)
							send_child_event(child_events, 'T', NULL, chan);
						AST_LIST_LOCK(&u->playlist);
						while ((entry = AST_LIST_REMOVE_HEAD(&u->playlist, list))) {
							send_child_event(child_events, 'D', entry->filename, chan);
							free(entry);
						}
						if (!u->playing_silence)
							u->abort_current_sound = 1;
						AST_LIST_UNLOCK(&u->playlist);
					}
				} else if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_HANGUP)) {
					ast_chan_log(LOG_NOTICE, chan, "Got AST_CONTROL_HANGUP\n");
					send_child_event(child_events, 'H', NULL, chan);
					ast_frfree(f);
					res = -1;
					break;
				}
				ast_frfree(f);
			} else if (ready_fd == child_commands_fd) {
				char input[1024];

				if (exception || feof(child_commands)) {
					ast_chan_log(LOG_WARNING, chan, "Child process went away\n");
					res = -1;
					break;
				}

				if (!fgets(input, sizeof(input), child_commands))
					continue;

				command = ast_strip(input);

				ast_chan_log(LOG_DEBUG, chan, "got command '%s'\n", input);

				if (strlen(input) < 4)
					continue;

				if (input[0] == 'S') {
					if (ast_fileexists(&input[2], NULL, u->chan->language) == -1) {
						ast_chan_log(LOG_WARNING, chan, "Unknown file requested '%s'\n", &input[2]);
						send_child_event(child_events, 'Z', NULL, chan);
						strcpy(&input[2], "exception");
					}
					if (!u->abort_current_sound && !u->playing_silence)
						send_child_event(child_events, 'T', NULL, chan);
					AST_LIST_LOCK(&u->playlist);
					while ((entry = AST_LIST_REMOVE_HEAD(&u->playlist, list))) {
						send_child_event(child_events, 'D', entry->filename, chan);
						free(entry);
					}
					if (!u->playing_silence)
						u->abort_current_sound = 1;
					entry = make_entry(&input[2]);
					if (entry)
						AST_LIST_INSERT_TAIL(&u->playlist, entry, list);
					AST_LIST_UNLOCK(&u->playlist);
				} else if (input[0] == 'A') {
					if (ast_fileexists(&input[2], NULL, u->chan->language) == -1) {
						ast_chan_log(LOG_WARNING, chan, "Unknown file requested '%s'\n", &input[2]);
						send_child_event(child_events, 'Z', NULL, chan);
						strcpy(&input[2], "exception");
					}
					entry = make_entry(&input[2]);
					if (entry) {
						AST_LIST_LOCK(&u->playlist);
						AST_LIST_INSERT_TAIL(&u->playlist, entry, list);
						AST_LIST_UNLOCK(&u->playlist);
					}
				} else if (input[0] == 'H') {
					ast_chan_log(LOG_NOTICE, chan, "Hanging up: %s\n", &input[2]);
					send_child_event(child_events, 'H', NULL, chan);
					break;
				} else if (input[0] == 'O') {
					if (!strcasecmp(&input[2], "autoclear"))
						u->option_autoclear = 1;
					else if (!strcasecmp(&input[2], "noautoclear"))
						u->option_autoclear = 0;
					else
						ast_chan_log(LOG_WARNING, chan, "Unknown option requested '%s'\n", &input[2]);
				}
			} else if (ready_fd == child_errors_fd) {
				char input[1024];

				if (exception || (dup2(child_commands_fd, test_available_fd) == -1) || feof(child_errors)) {
					ast_chan_log(LOG_WARNING, chan, "Child process went away\n");
					res = -1;
					break;
				}

				if (fgets(input, sizeof(input), child_errors)) {
					command = ast_strip(input);
					ast_chan_log(LOG_NOTICE, chan, "stderr: %s\n", command);
				}
			} else if ((ready_fd < 0) && ms) { 
				if (errno == 0 || errno == EINTR)
					continue;

				ast_chan_log(LOG_WARNING, chan, "Wait failed (%s)\n", strerror(errno));
				break;
			}
		}
	}

 exit:
	if (gen_active)
		ast_deactivate_generator(chan);

	if (child_events)
		fclose(child_events);

	if (child_commands)
		fclose(child_commands);

	if (child_errors)
		fclose(child_errors);

	if (test_available_fd > -1) {
		close(test_available_fd);
	}

	if (child_stdin[0])
		close(child_stdin[0]);

	if (child_stdin[1])
		close(child_stdin[1]);

	if (child_stdout[0])
		close(child_stdout[0]);

	if (child_stdout[1])
		close(child_stdout[1]);

	if (child_stderr[0])
		close(child_stderr[0]);

	if (child_stderr[1])
		close(child_stderr[1]);

	while ((entry = AST_LIST_REMOVE_HEAD(&u->playlist, list)))
		free(entry);

	ast_module_user_remove(lu);

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
	return ast_register_application(app, app_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "External IVR Interface Application");
