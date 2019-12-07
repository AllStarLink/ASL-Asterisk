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
 * \brief Trivial application to control playback of a sound file
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 86754 $")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/utils.h"
#include "asterisk/options.h"

static const char *app = "ControlPlayback";

static const char *synopsis = "Play a file with fast forward and rewind";

static const char *descrip = 
"  ControlPlayback(file[|skipms[|ff[|rew[|stop[|pause[|restart|options]]]]]]]):\n"
"This application will play back the given filename. By default, the '*' key\n"
"can be used to rewind, and the '#' key can be used to fast-forward.\n"
"Parameters:\n"
"  skipms  - This is number of milliseconds to skip when rewinding or\n"
"            fast-forwarding.\n"
"  ff      - Fast-forward when this DTMF digit is received.\n"
"  rew     - Rewind when this DTMF digit is received.\n"
"  stop    - Stop playback when this DTMF digit is received.\n"
"  pause   - Pause playback when this DTMF digit is received.\n"
"  restart - Restart playback when this DTMF digit is received.\n"
"Options:\n"
"  j - Jump to priority n+101 if the requested file is not found.\n"
"This application sets the following channel variable upon completion:\n"
"  CPLAYBACKSTATUS -  This variable contains the status of the attempt as a text\n"
"                     string, one of: SUCCESS | USERSTOPPED | ERROR\n";


static int is_on_phonepad(char key)
{
	return key == 35 || key == 42 || (key >= 48 && key <= 57);
}

static int controlplayback_exec(struct ast_channel *chan, void *data)
{
	int res = 0, priority_jump = 0;
	int skipms = 0;
	struct ast_module_user *u;
	char *tmp;
	int argc;
	char *argv[8];
	enum arg_ids {
		arg_file = 0,
		arg_skip = 1,
		arg_fwd = 2,
		arg_rev = 3,
		arg_stop = 4,
		arg_pause = 5,
		arg_restart = 6,
		options = 7,
	};
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "ControlPlayback requires an argument (filename)\n");
		return -1;
	}

	u = ast_module_user_add(chan);
	
	tmp = ast_strdupa(data);
	memset(argv, 0, sizeof(argv));

	argc = ast_app_separate_args(tmp, '|', argv, sizeof(argv) / sizeof(argv[0]));

	if (argc < 1) {
		ast_log(LOG_WARNING, "ControlPlayback requires an argument (filename)\n");
		ast_module_user_remove(u);
		return -1;
	}

	skipms = argv[arg_skip] ? atoi(argv[arg_skip]) : 3000;
	if (!skipms)
		skipms = 3000;

	if (!argv[arg_fwd] || !is_on_phonepad(*argv[arg_fwd]))
		argv[arg_fwd] = "#";
	if (!argv[arg_rev] || !is_on_phonepad(*argv[arg_rev]))
		argv[arg_rev] = "*";
	if (argv[arg_stop] && !is_on_phonepad(*argv[arg_stop]))
		argv[arg_stop] = NULL;
	if (argv[arg_pause] && !is_on_phonepad(*argv[arg_pause]))
		argv[arg_pause] = NULL;
	if (argv[arg_restart] && !is_on_phonepad(*argv[arg_restart]))
		argv[arg_restart] = NULL;

	if (argv[options]) {
		if (strchr(argv[options], 'j'))
			priority_jump = 1;
	}

	res = ast_control_streamfile(chan, argv[arg_file], argv[arg_fwd], argv[arg_rev], argv[arg_stop], argv[arg_pause], argv[arg_restart], skipms);

	/* If we stopped on one of our stop keys, return 0  */
	if (res > 0 && argv[arg_stop] && strchr(argv[arg_stop], res)) {
		res = 0;
		pbx_builtin_setvar_helper(chan, "CPLAYBACKSTATUS", "USERSTOPPED");
	} else {
		if (res < 0) {
			if (priority_jump || ast_opt_priority_jumping) {
				if (ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101)) {
					ast_log(LOG_WARNING, "ControlPlayback tried to jump to priority n+101 as requested, but priority didn't exist\n");
				}
			}
			res = 0;
			pbx_builtin_setvar_helper(chan, "CPLAYBACKSTATUS", "ERROR");
		} else
			pbx_builtin_setvar_helper(chan, "CPLAYBACKSTATUS", "SUCCESS");
	}

	ast_module_user_remove(u);

	return res;
}

static int unload_module(void)
{
	int res;
	res = ast_unregister_application(app);
	return res;
}

static int load_module(void)
{
	return ast_register_application(app, controlplayback_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Control Playback Application");
