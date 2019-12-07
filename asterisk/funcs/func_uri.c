/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Created by Olle E. Johansson, Edvina.net 
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
 * \brief URI encoding / decoding
 *
 * \author Olle E. Johansson <oej@edvina.net>
 * 
 * \note For now this code only supports 8 bit characters, not unicode,
         which we ultimately will need to support.
 * 
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 47625 $")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

/*! \brief uriencode: Encode URL according to RFC 2396 */
static int uriencode(struct ast_channel *chan, char *cmd, char *data,
		     char *buf, size_t len)
{
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Syntax: URIENCODE(<data>) - missing argument!\n");
		return -1;
	}

	ast_uri_encode(data, buf, len, 1);

	return 0;
}

/*!\brief uridecode: Decode URI according to RFC 2396 */
static int uridecode(struct ast_channel *chan, char *cmd, char *data,
		     char *buf, size_t len)
{
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Syntax: URIDECODE(<data>) - missing argument!\n");
		return -1;
	}

	ast_copy_string(buf, data, len);
	ast_uri_decode(buf);

	return 0;
}

static struct ast_custom_function urldecode_function = {
	.name = "URIDECODE",
	.synopsis = "Decodes a URI-encoded string according to RFC 2396.",
	.syntax = "URIDECODE(<data>)",
	.read = uridecode,
};

static struct ast_custom_function urlencode_function = {
	.name = "URIENCODE",
	.synopsis = "Encodes a string to URI-safe encoding according to RFC 2396.",
	.syntax = "URIENCODE(<data>)",
	.read = uriencode,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&urldecode_function)
		|| ast_custom_function_unregister(&urlencode_function);
}

static int load_module(void)
{
	return ast_custom_function_register(&urldecode_function)
		|| ast_custom_function_register(&urlencode_function);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "URI encode/decode dialplan functions");
