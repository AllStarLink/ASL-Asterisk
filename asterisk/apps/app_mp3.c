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
 * \brief Silly application to play an MP3 file -- uses mpg123
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */
 
/*** MODULEINFO
	<depend>working_fork</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 182810 $")

#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#ifdef HAVE_CAP
#include <sys/capability.h>
#endif /* HAVE_CAP */

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/frame.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/options.h"

#define LOCAL_MPG_123 "/usr/local/bin/mpg123"
#define MPG_123 "/usr/bin/mpg123"

static char *app = "MP3Player";

static char *synopsis = "Play an MP3 file or stream";

static char *descrip = 
"  MP3Player(location) Executes mpg123 to play the given location,\n"
"which typically would be a filename or a URL. User can exit by pressing\n"
"any key on the dialpad, or by hanging up."; 


static int mp3play(char *filename, int fd)
{
	int res;
	int x;
	sigset_t fullset, oldset;
#ifdef HAVE_CAP
	cap_t cap;
#endif

	sigfillset(&fullset);
	pthread_sigmask(SIG_BLOCK, &fullset, &oldset);

	res = fork();
	if (res < 0) 
		ast_log(LOG_WARNING, "Fork failed\n");
	if (res) {
		pthread_sigmask(SIG_SETMASK, &oldset, NULL);
		return res;
	}
#ifdef HAVE_CAP
	cap = cap_from_text("cap_net_admin-eip");

	if (cap_set_proc(cap)) {
		/* Careful with order! Logging cannot happen after we close FDs */
		ast_log(LOG_WARNING, "Unable to remove capabilities.\n");
	}
	cap_free(cap);
#endif
	if (ast_opt_high_priority)
		ast_set_priority(0);
	signal(SIGPIPE, SIG_DFL);
	pthread_sigmask(SIG_UNBLOCK, &fullset, NULL);

	dup2(fd, STDOUT_FILENO);
	for (x=STDERR_FILENO + 1;x<256;x++) {
		close(x);
	}
	/* Execute mpg123, but buffer if it's a net connection */
	if (!strncasecmp(filename, "http://", 7)) {
		/* Most commonly installed in /usr/local/bin */
	    execl(LOCAL_MPG_123, "mpg123", "-q", "-s", "-b", "1024", "-f", "8192", "--mono", "-r", "8000", filename, (char *)NULL);
		/* But many places has it in /usr/bin */
	    execl(MPG_123, "mpg123", "-q", "-s", "-b", "1024","-f", "8192", "--mono", "-r", "8000", filename, (char *)NULL);
		/* As a last-ditch effort, try to use PATH */
	    execlp("mpg123", "mpg123", "-q", "-s", "-b", "1024",  "-f", "8192", "--mono", "-r", "8000", filename, (char *)NULL);
	}
	else {
		/* Most commonly installed in /usr/local/bin */
	    execl(MPG_123, "mpg123", "-q", "-s", "-f", "8192", "--mono", "-r", "8000", filename, (char *)NULL);
		/* But many places has it in /usr/bin */
	    execl(LOCAL_MPG_123, "mpg123", "-q", "-s", "-f", "8192", "--mono", "-r", "8000", filename, (char *)NULL);
		/* As a last-ditch effort, try to use PATH */
	    execlp("mpg123", "mpg123", "-q", "-s", "-f", "8192", "--mono", "-r", "8000", filename, (char *)NULL);
	}
	ast_log(LOG_WARNING, "Execute of mpg123 failed\n");
	_exit(0);
}

static int timed_read(int fd, void *data, int datalen, int timeout)
{
	int res;
	struct pollfd fds[1];
	fds[0].fd = fd;
	fds[0].events = POLLIN;
	res = ast_poll(fds, 1, timeout);
	if (res < 1) {
		ast_log(LOG_NOTICE, "Poll timed out/errored out with %d\n", res);
		return -1;
	}
	return read(fd, data, datalen);
	
}

static int mp3_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct ast_module_user *u;
	int fds[2];
	int ms = -1;
	int pid = -1;
	int owriteformat;
	int timeout = 2000;
	struct timeval next;
	struct ast_frame *f;
	struct myframe {
		struct ast_frame f;
		char offset[AST_FRIENDLY_OFFSET];
		short frdata[160];
	} myf;
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "MP3 Playback requires an argument (filename)\n");
		return -1;
	}

	u = ast_module_user_add(chan);

	if (pipe(fds)) {
		ast_log(LOG_WARNING, "Unable to create pipe\n");
		ast_module_user_remove(u);
		return -1;
	}
	
	ast_stopstream(chan);

	owriteformat = chan->writeformat;
	res = ast_set_write_format(chan, AST_FORMAT_SLINEAR);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set write format to signed linear\n");
		ast_module_user_remove(u);
		return -1;
	}
	
	res = mp3play((char *)data, fds[1]);
	if (!strncasecmp((char *)data, "http://", 7)) {
		timeout = 10000;
	}
	/* Wait 1000 ms first */
	next = ast_tvnow();
	next.tv_sec += 1;
	if (res >= 0) {
		pid = res;
		/* Order is important -- there's almost always going to be mp3...  we want to prioritize the
		   user */
		for (;;) {
			ms = ast_tvdiff_ms(next, ast_tvnow());
			if (ms <= 0) {
				res = timed_read(fds[0], myf.frdata, sizeof(myf.frdata), timeout);
				if (res > 0) {
					myf.f.frametype = AST_FRAME_VOICE;
					myf.f.subclass = AST_FORMAT_SLINEAR;
					myf.f.datalen = res;
					myf.f.samples = res / 2;
					myf.f.mallocd = 0;
					myf.f.offset = AST_FRIENDLY_OFFSET;
					myf.f.src = __PRETTY_FUNCTION__;
					myf.f.delivery.tv_sec = 0;
					myf.f.delivery.tv_usec = 0;
					myf.f.data = myf.frdata;
					if (ast_write(chan, &myf.f) < 0) {
						res = -1;
						break;
					}
				} else {
					ast_log(LOG_DEBUG, "No more mp3\n");
					res = 0;
					break;
				}
				next = ast_tvadd(next, ast_samp2tv(myf.f.samples, 8000));
			} else {
				ms = ast_waitfor(chan, ms);
				if (ms < 0) {
					ast_log(LOG_DEBUG, "Hangup detected\n");
					res = -1;
					break;
				}
				if (ms) {
					f = ast_read(chan);
					if (!f) {
						ast_log(LOG_DEBUG, "Null frame == hangup() detected\n");
						res = -1;
						break;
					}
					if (f->frametype == AST_FRAME_DTMF) {
						ast_log(LOG_DEBUG, "User pressed a key\n");
						ast_frfree(f);
						res = 0;
						break;
					}
					ast_frfree(f);
				} 
			}
		}
	}
	close(fds[0]);
	close(fds[1]);
	
	if (pid > -1)
		kill(pid, SIGKILL);
	if (!res && owriteformat)
		ast_set_write_format(chan, owriteformat);

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
	return ast_register_application(app, mp3_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Silly MP3 Application");
