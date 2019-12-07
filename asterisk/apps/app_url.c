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
 * \brief App to transmit a URL
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 43445 $")

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
#include "asterisk/image.h"
#include "asterisk/options.h"

static char *app = "SendURL";

static char *synopsis = "Send a URL";

static char *descrip = 
"  SendURL(URL[|option]): Requests client go to URL (IAX2) or sends the \n"
"URL to the client (other channels).\n"
"Result is returned in the SENDURLSTATUS channel variable:\n"
"    SUCCESS       URL successfully sent to client\n"
"    FAILURE       Failed to send URL\n"
"    NOLOAD        Client failed to load URL (wait enabled)\n"
"    UNSUPPORTED   Channel does not support URL transport\n"
"\n"
"If the option 'wait' is specified, execution will wait for an\n"
"acknowledgement that the URL has been loaded before continuing\n"
"\n"
"If jumping is specified as an option (the 'j' flag), the client does not\n"
"support Asterisk \"html\" transport, and there exists a step with priority\n"
"n + 101, then execution will continue at that step.\n"
"\n"
"SendURL continues normally if the URL was sent correctly or if the channel\n"
"does not support HTML transport.  Otherwise, the channel is hung up.\n";


static int sendurl_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct ast_module_user *u;
	char *tmp;
	char *options;
	int local_option_wait=0;
	int local_option_jump = 0;
	struct ast_frame *f;
	char *stringp=NULL;
	char *status = "FAILURE";
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SendURL requires an argument (URL)\n");
		pbx_builtin_setvar_helper(chan, "SENDURLSTATUS", status);
		return -1;
	}

	u = ast_module_user_add(chan);

	tmp = ast_strdupa(data);

	stringp=tmp;
	strsep(&stringp, "|");
	options = strsep(&stringp, "|");
	if (options && !strcasecmp(options, "wait"))
		local_option_wait = 1;
	if (options && !strcasecmp(options, "j"))
		local_option_jump = 1;
	
	if (!ast_channel_supports_html(chan)) {
		/* Does not support transport */
		if (local_option_jump || ast_opt_priority_jumping)
			 ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
		pbx_builtin_setvar_helper(chan, "SENDURLSTATUS", "UNSUPPORTED");
		ast_module_user_remove(u);
		return 0;
	}
	res = ast_channel_sendurl(chan, tmp);
	if (res == -1) {
		pbx_builtin_setvar_helper(chan, "SENDURLSTATUS", "FAILURE");
		ast_module_user_remove(u);
		return res;
	}
	status = "SUCCESS";
	if (local_option_wait) {
		for(;;) {
			/* Wait for an event */
			res = ast_waitfor(chan, -1);
			if (res < 0) 
				break;
			f = ast_read(chan);
			if (!f) {
				res = -1;
				status = "FAILURE";
				break;
			}
			if (f->frametype == AST_FRAME_HTML) {
				switch(f->subclass) {
				case AST_HTML_LDCOMPLETE:
					res = 0;
					ast_frfree(f);
					status = "NOLOAD";
					goto out;
					break;
				case AST_HTML_NOSUPPORT:
					/* Does not support transport */
					status ="UNSUPPORTED";
					if (local_option_jump || ast_opt_priority_jumping)
			 			ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
					res = 0;
					ast_frfree(f);
					goto out;
					break;
				default:
					ast_log(LOG_WARNING, "Don't know what to do with HTML subclass %d\n", f->subclass);
				};
			}
			ast_frfree(f);
		}
	} 
out:	
	pbx_builtin_setvar_helper(chan, "SENDURLSTATUS", status);
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
	return ast_register_application(app, sendurl_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Send URL Applications");
