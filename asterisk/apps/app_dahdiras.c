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
 * \brief Execute an ISDN RAS
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */

/*** MODULEINFO
	<depend>dahdi</depend>
	<depend>working_fork</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 178266 $")

#include <sys/ioctl.h>
#include <sys/wait.h>
#ifdef __linux__
#include <sys/signal.h>
#else
#include <signal.h>
#endif /* __linux__ */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>

#include "asterisk/dahdi_compat.h"

#ifdef HAVE_CAP
#include <sys/capability.h>
#endif /* HAVE_CAP */

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/options.h"

static char *dahdi_app = "DAHDIRAS";
static char *zap_app = "ZapRAS";

static char *dahdi_synopsis = "Executes DAHDI ISDN RAS application";
static char *zap_synopsis = "Executes Zaptel ISDN RAS application";

static char *dahdi_descrip =
"  DAHDIRAS(args): Executes a RAS server using pppd on the given channel.\n"
"The channel must be a clear channel (i.e. PRI source) and a DAHDI\n"
"channel to be able to use this function (no modem emulation is included).\n"
"Your pppd must have the DAHDI plugin available. Arguments should be\n"
"separated by | characters.\n";

static char *zap_descrip =
"  ZapRAS(args): Executes a RAS server using pppd on the given channel.\n"
"The channel must be a clear channel (i.e. PRI source) and a Zaptel\n"
"channel to be able to use this function (no modem emulation is included).\n"
"Your pppd must have the Zaptel plugin available. Arguments should be\n"
"separated by | characters.\n";

#define PPP_MAX_ARGS	32
#define PPP_EXEC	"/usr/sbin/pppd"

static pid_t spawn_ras(struct ast_channel *chan, char *args)
{
	pid_t pid;
	int x;	
	char *c;

	char *argv[PPP_MAX_ARGS];
	int argc = 0;
	char *stringp=NULL;
	sigset_t fullset, oldset;
#ifdef HAVE_CAP
	cap_t cap;
#endif

	sigfillset(&fullset);
	pthread_sigmask(SIG_BLOCK, &fullset, &oldset);

	/* Start by forking */
	pid = fork();
	if (pid) {
		pthread_sigmask(SIG_SETMASK, &oldset, NULL);
		return pid;
	}

#ifdef HAVE_CAP
	cap = cap_from_text("cap_net_admin-eip");

	if (cap_set_proc(cap)) {
		/* Careful with order! Logging cannot happen after we close FDs */
		ast_log(LOG_WARNING, "Unable to remove capabilities.\n");
	}
	cap_free(cap);
#endif

	/* Restore original signal handlers */
	for (x=0;x<NSIG;x++)
		signal(x, SIG_DFL);

	pthread_sigmask(SIG_UNBLOCK, &fullset, NULL);

	/* Execute RAS on File handles */
	dup2(chan->fds[0], STDIN_FILENO);

	/* Drop high priority */
	if (ast_opt_high_priority)
		ast_set_priority(0);

	/* Close other file descriptors */
	for (x=STDERR_FILENO + 1;x<1024;x++) 
		close(x);

	/* Reset all arguments */
	memset(argv, 0, sizeof(argv));

	/* First argument is executable, followed by standard
	   arguments for DAHDI PPP */
	argv[argc++] = PPP_EXEC;
	argv[argc++] = "nodetach";

	/* And all the other arguments */
	stringp=args;
	c = strsep(&stringp, "|");
	while(c && strlen(c) && (argc < (PPP_MAX_ARGS - 4))) {
		argv[argc++] = c;
		c = strsep(&stringp, "|");
	}

	argv[argc++] = "plugin";
#ifdef HAVE_ZAPTEL
	argv[argc++] = "zaptel.so";
#else
	argv[argc++] = "dahdi.so";
#endif
	argv[argc++] = "stdin";

	/* Finally launch PPP */
	execv(PPP_EXEC, argv);
	fprintf(stderr, "Failed to exec PPPD!\n");
	exit(1);
}

static void run_ras(struct ast_channel *chan, char *args)
{
	pid_t pid;
	int status;
	int res;
	int signalled = 0;
	struct dahdi_bufferinfo savebi;
	int x;
	
	res = ioctl(chan->fds[0], DAHDI_GET_BUFINFO, &savebi);
	if(res) {
		ast_log(LOG_WARNING, "Unable to check buffer policy on channel %s\n", chan->name);
		return;
	}

	pid = spawn_ras(chan, args);
	if (pid < 0) {
		ast_log(LOG_WARNING, "Failed to spawn RAS\n");
	} else {
		for (;;) {
			res = wait4(pid, &status, WNOHANG, NULL);
			if (!res) {
				/* Check for hangup */
				if (chan->_softhangup && !signalled) {
					ast_log(LOG_DEBUG, "Channel '%s' hungup.  Signalling RAS at %d to die...\n", chan->name, pid);
					kill(pid, SIGTERM);
					signalled=1;
				}
				/* Try again */
				sleep(1);
				continue;
			}
			if (res < 0) {
				ast_log(LOG_WARNING, "wait4 returned %d: %s\n", res, strerror(errno));
			}
			if (option_verbose > 2) {
				if (WIFEXITED(status)) {
					ast_verbose(VERBOSE_PREFIX_3 "RAS on %s terminated with status %d\n", chan->name, WEXITSTATUS(status));
				} else if (WIFSIGNALED(status)) {
					ast_verbose(VERBOSE_PREFIX_3 "RAS on %s terminated with signal %d\n", 
						 chan->name, WTERMSIG(status));
				} else {
					ast_verbose(VERBOSE_PREFIX_3 "RAS on %s terminated weirdly.\n", chan->name);
				}
			}
			/* Throw back into audio mode */
			x = 1;
			ioctl(chan->fds[0], DAHDI_AUDIOMODE, &x);

			/* Restore saved values */
			res = ioctl(chan->fds[0], DAHDI_SET_BUFINFO, &savebi);
			if (res < 0) {
				ast_log(LOG_WARNING, "Unable to set buffer policy on channel %s\n", chan->name);
			}
			break;
		}
	}
}

static int exec(struct ast_channel *chan, void *data)
{
	int res=-1;
	char *args;
	struct ast_module_user *u;
	struct dahdi_params ztp;

	if (!data) 
		data = "";

	u = ast_module_user_add(chan);

	args = ast_strdupa(data);
	
	/* Answer the channel if it's not up */
	if (chan->_state != AST_STATE_UP)
		ast_answer(chan);
	if (strcasecmp(chan->tech->type, dahdi_chan_name)) {
		if (option_verbose > 1)
			ast_verbose(VERBOSE_PREFIX_2 "Channel %s is not a %s channel\n", chan->name, dahdi_chan_name);
		sleep(2);
	} else {
		memset(&ztp, 0, sizeof(ztp));
		if (ioctl(chan->fds[0], DAHDI_GET_PARAMS, &ztp)) {
			ast_log(LOG_WARNING, "Unable to get parameters\n");
		} else if (ztp.sigtype != DAHDI_SIG_CLEAR) {
			if (option_verbose > 1)
				ast_verbose(VERBOSE_PREFIX_2 "Channel %s is not a clear channel\n", chan->name);
		} else {
			/* Everything should be okay.  Run PPP. */
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Starting RAS on %s\n", chan->name);
			/* Execute RAS */
			run_ras(chan, args);
		}
	}
	ast_module_user_remove(u);
	return res;
}

static int exec_warn(struct ast_channel *chan, void *data)
{
	ast_log(LOG_WARNING, "Use of the command %s is deprecated, please use %s instead.\n", zap_app, dahdi_app);

	return exec(chan, data);
}

static int unload_module(void) 
{
	int res = 0;

	if (*dahdi_chan_mode == CHAN_DAHDI_PLUS_ZAP_MODE) {
		res |= ast_unregister_application(dahdi_app);
	}

	res |= ast_unregister_application(zap_app);
	
	ast_module_user_hangup_all();

	return res;
}

static int load_module(void)
{
	int res = 0;

	if (*dahdi_chan_mode == CHAN_DAHDI_PLUS_ZAP_MODE) {
		res |= ast_register_application(dahdi_app, exec, dahdi_synopsis, dahdi_descrip);
	}

	res |= ast_register_application(zap_app, exec_warn, zap_synopsis, zap_descrip);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "DAHDI RAS Application");

