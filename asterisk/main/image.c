/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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
 * \brief Image Management
 *
 * \author Mark Spencer <markster@digium.com> 
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 47051 $")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

#include "asterisk/sched.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/logger.h"
#include "asterisk/file.h"
#include "asterisk/image.h"
#include "asterisk/translate.h"
#include "asterisk/cli.h"
#include "asterisk/lock.h"

/* XXX Why don't we just use the formats struct for this? */
static AST_LIST_HEAD_STATIC(imagers, ast_imager);

int ast_image_register(struct ast_imager *img)
{
	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "Registered format '%s' (%s)\n", img->name, img->desc);
	AST_LIST_LOCK(&imagers);
	AST_LIST_INSERT_HEAD(&imagers, img, list);
	AST_LIST_UNLOCK(&imagers);
	return 0;
}

void ast_image_unregister(struct ast_imager *img)
{
	struct ast_imager *i;
	
	AST_LIST_LOCK(&imagers);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&imagers, i, list) {	
		if (i == img) {
			AST_LIST_REMOVE_CURRENT(&imagers, list);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END
	AST_LIST_UNLOCK(&imagers);
	if (i && (option_verbose > 1))
		ast_verbose(VERBOSE_PREFIX_2 "Unregistered format '%s' (%s)\n", img->name, img->desc);
}

int ast_supports_images(struct ast_channel *chan)
{
	if (!chan || !chan->tech)
		return 0;
	if (!chan->tech->send_image)
		return 0;
	return 1;
}

static int file_exists(char *filename)
{
	int res;
	struct stat st;
	res = stat(filename, &st);
	if (!res)
		return st.st_size;
	return 0;
}

static void make_filename(char *buf, int len, char *filename, const char *preflang, char *ext)
{
	if (filename[0] == '/') {
		if (!ast_strlen_zero(preflang))
			snprintf(buf, len, "%s-%s.%s", filename, preflang, ext);
		else
			snprintf(buf, len, "%s.%s", filename, ext);
	} else {
		if (!ast_strlen_zero(preflang))
			snprintf(buf, len, "%s/%s/%s-%s.%s", ast_config_AST_DATA_DIR, "images", filename, preflang, ext);
		else
			snprintf(buf, len, "%s/%s/%s.%s", ast_config_AST_DATA_DIR, "images", filename, ext);
	}
}

struct ast_frame *ast_read_image(char *filename, const char *preflang, int format)
{
	struct ast_imager *i;
	char buf[256];
	char tmp[80];
	char *e;
	struct ast_imager *found = NULL;
	int fd;
	int len=0;
	struct ast_frame *f = NULL;
	
	AST_LIST_LOCK(&imagers);
	AST_LIST_TRAVERSE(&imagers, i, list) {
		if (i->format & format) {
			char *stringp=NULL;
			ast_copy_string(tmp, i->exts, sizeof(tmp));
			stringp=tmp;
			e = strsep(&stringp, "|");
			while(e) {
				make_filename(buf, sizeof(buf), filename, preflang, e);
				if ((len = file_exists(buf))) {
					found = i;
					break;
				}
				make_filename(buf, sizeof(buf), filename, NULL, e);
				if ((len = file_exists(buf))) {
					found = i;
					break;
				}
				e = strsep(&stringp, "|");
			}
		}
		if (found)
			break;	
	}

	if (found) {
		fd = open(buf, O_RDONLY);
		if (fd > -1) {
			if (!found->identify || found->identify(fd)) {
				/* Reset file pointer */
				lseek(fd, 0, SEEK_SET);
				f = found->read_image(fd,len); 
			} else
				ast_log(LOG_WARNING, "%s does not appear to be a %s file\n", buf, found->name);
			close(fd);
		} else
			ast_log(LOG_WARNING, "Unable to open '%s': %s\n", buf, strerror(errno));
	} else
		ast_log(LOG_WARNING, "Image file '%s' not found\n", filename);
	
	AST_LIST_UNLOCK(&imagers);
	
	return f;
}

int ast_send_image(struct ast_channel *chan, char *filename)
{
	struct ast_frame *f;
	int res = -1;
	if (chan->tech->send_image) {
		f = ast_read_image(filename, chan->language, -1);
		if (f) {
			res = chan->tech->send_image(chan, f);
			ast_frfree(f);
		}
	}
	return res;
}

static int show_image_formats_deprecated(int fd, int argc, char *argv[])
{
#define FORMAT "%10s %10s %50s %10s\n"
#define FORMAT2 "%10s %10s %50s %10s\n"
	struct ast_imager *i;
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_cli(fd, FORMAT, "Name", "Extensions", "Description", "Format");
	AST_LIST_TRAVERSE(&imagers, i, list)
		ast_cli(fd, FORMAT2, i->name, i->exts, i->desc, ast_getformatname(i->format));
	return RESULT_SUCCESS;
}

static int show_image_formats(int fd, int argc, char *argv[])
{
#define FORMAT "%10s %10s %50s %10s\n"
#define FORMAT2 "%10s %10s %50s %10s\n"
	struct ast_imager *i;
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	ast_cli(fd, FORMAT, "Name", "Extensions", "Description", "Format");
	AST_LIST_TRAVERSE(&imagers, i, list)
		ast_cli(fd, FORMAT2, i->name, i->exts, i->desc, ast_getformatname(i->format));
	return RESULT_SUCCESS;
}

struct ast_cli_entry cli_show_image_formats_deprecated = {
	{ "show", "image", "formats" },
	show_image_formats_deprecated, NULL,
	NULL };

struct ast_cli_entry cli_image[] = {
	{ { "core", "show", "image", "formats" },
	show_image_formats, "Displays image formats",
	"Usage: core show image formats\n"
	"       displays currently registered image formats (if any)\n", NULL, &cli_show_image_formats_deprecated },
};

int ast_image_init(void)
{
	ast_cli_register_multiple(cli_image, sizeof(cli_image) / sizeof(struct ast_cli_entry));
	return 0;
}
