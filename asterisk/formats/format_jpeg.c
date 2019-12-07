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
 * \brief JPEG File format support.
 * 
 * \arg File name extension: jpeg, jpg
 * \ingroup formats
 */
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 233782 $")

#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "asterisk/channel.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/sched.h"
#include "asterisk/module.h"
#include "asterisk/image.h"
#include "asterisk/lock.h"
#include "asterisk/endian.h"

static struct ast_frame *jpeg_read_image(int fd, int len)
{
	struct ast_frame fr;
	int res;
	char buf[65536];
	if (len > sizeof(buf) || len < 0) {
		ast_log(LOG_WARNING, "JPEG image too large to read\n");
		return NULL;
	}
	res = read(fd, buf, len);
	if (res < len) {
		ast_log(LOG_WARNING, "Only read %d of %d bytes: %s\n", res, len, strerror(errno));
	}
	memset(&fr, 0, sizeof(fr));
	fr.frametype = AST_FRAME_IMAGE;
	fr.subclass = AST_FORMAT_JPEG;
	fr.data = buf;
	fr.src = "JPEG Read";
	fr.datalen = len;
	return ast_frisolate(&fr);
}

static int jpeg_identify(int fd)
{
	char buf[10];
	int res;
	res = read(fd, buf, sizeof(buf));
	if (res < sizeof(buf))
		return 0;
	if (memcmp(buf + 6, "JFIF", 4))
		return 0;
	return 1;
}

static int jpeg_write_image(int fd, struct ast_frame *fr)
{
	int res=0;
	if (fr->frametype != AST_FRAME_IMAGE) {
		ast_log(LOG_WARNING, "Not an image\n");
		return -1;
	}
	if (fr->subclass != AST_FORMAT_JPEG) {
		ast_log(LOG_WARNING, "Not a jpeg image\n");
		return -1;
	}
	if (fr->datalen) {
		res = write(fd, fr->data, fr->datalen);
		if (res != fr->datalen) {
			ast_log(LOG_WARNING, "Only wrote %d of %d bytes: %s\n", res, fr->datalen, strerror(errno));
			return -1;
		}
	}
	return res;
}

static struct ast_imager jpeg_format = {
	"jpg",
	"JPEG (Joint Picture Experts Group)",
	"jpg|jpeg",
	AST_FORMAT_JPEG,
	jpeg_read_image,
	jpeg_identify,
	jpeg_write_image,
};

static int load_module(void)
{
	return ast_image_register(&jpeg_format);
}

static int unload_module(void)
{
	ast_image_unregister(&jpeg_format);

	return 0;
}	

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_FIRST, "JPEG (Joint Picture Experts Group) Image Format",
	.load = load_module,
	.unload = unload_module,
);
