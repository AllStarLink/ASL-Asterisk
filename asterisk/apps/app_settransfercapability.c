/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Frank Sautter, levigo holding gmbh, www.levigo.de
 *
 * Frank Sautter - asterisk+at+sautter+dot+com 
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
 * \brief App to set the ISDN Transfer Capability
 *
 * \author Frank Sautter - asterisk+at+sautter+dot+com 
 * 
 * \ingroup applications
 */
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 58939 $")

#include <string.h>
#include <stdlib.h>

#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/options.h"
#include "asterisk/transcap.h"


static char *app = "SetTransferCapability";

static char *synopsis = "Set ISDN Transfer Capability";


static struct {	int val; char *name; } transcaps[] = {
	{ AST_TRANS_CAP_SPEECH,				"SPEECH" },
	{ AST_TRANS_CAP_DIGITAL,			"DIGITAL" },
	{ AST_TRANS_CAP_RESTRICTED_DIGITAL,	"RESTRICTED_DIGITAL" },
	{ AST_TRANS_CAP_3_1K_AUDIO,			"3K1AUDIO" },
	{ AST_TRANS_CAP_DIGITAL_W_TONES,	"DIGITAL_W_TONES" },
	{ AST_TRANS_CAP_VIDEO,				"VIDEO" },
};

static char *descrip = 
"  SetTransferCapability(transfercapability): Set the ISDN Transfer \n"
"Capability of a call to a new value.\n"
"Valid Transfer Capabilities are:\n"
"\n"
"  SPEECH             : 0x00 - Speech (default, voice calls)\n"
"  DIGITAL            : 0x08 - Unrestricted digital information (data calls)\n"
"  RESTRICTED_DIGITAL : 0x09 - Restricted digital information\n"
"  3K1AUDIO           : 0x10 - 3.1kHz Audio (fax calls)\n"
"  DIGITAL_W_TONES    : 0x11 - Unrestricted digital information with tones/announcements\n"
"  VIDEO              : 0x18 - Video\n"
"\n"
"This application is deprecated in favor of Set(CHANNEL(transfercapability)=...)\n"
;

static int settransfercapability_exec(struct ast_channel *chan, void *data)
{
	char *tmp = NULL;
	struct ast_module_user *u;
	int x;
	char *opts;
	int transfercapability = -1;
	static int dep_warning = 0;
	
	u = ast_module_user_add(chan);

	if (!dep_warning) {
		dep_warning = 1;
		ast_log(LOG_WARNING, "SetTransferCapability is deprecated.  Please use CHANNEL(transfercapability) instead.\n");
	}

	if (data)
		tmp = ast_strdupa(data);
	else
		tmp = "";

	opts = strchr(tmp, '|');
	if (opts)
		*opts = '\0';
	
	for (x = 0; x < (sizeof(transcaps) / sizeof(transcaps[0])); x++) {
		if (!strcasecmp(transcaps[x].name, tmp)) {
			transfercapability = transcaps[x].val;
			break;
		}
	}
	if (transfercapability < 0) {
		ast_log(LOG_WARNING, "'%s' is not a valid transfer capability (see 'show application SetTransferCapability')\n", tmp);
		ast_module_user_remove(u);
		return 0;
	}
		
	chan->transfercapability = (unsigned short)transfercapability;
	
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Setting transfer capability to: 0x%.2x - %s.\n", transfercapability, tmp);			
	
	ast_module_user_remove(u);

	return 0;
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
	return ast_register_application(app, settransfercapability_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Set ISDN Transfer Capability");
