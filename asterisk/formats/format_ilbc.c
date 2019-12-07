/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Brian K. West <brian@bkw.org>
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
 * \brief Save to raw, headerless iLBC data.
 * \arg File name extension: ilbc
 * \ingroup formats
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 233782 $")

#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/sched.h"
#include "asterisk/module.h"
#include "asterisk/endian.h"

/* Some Ideas for this code came from makeg729e.c by Jeffrey Chilton */

/* Portions of the conversion code are by guido@sienanet.it */

#define	ILBC_BUF_SIZE	50	/* One Real iLBC Frame */
#define	ILBC_SAMPLES	240

static struct ast_frame *ilbc_read(struct ast_filestream *s, int *whennext)
{
	int res;
	/* Send a frame from the file to the appropriate channel */
	s->fr.frametype = AST_FRAME_VOICE;
	s->fr.subclass = AST_FORMAT_ILBC;
	s->fr.mallocd = 0;
	AST_FRAME_SET_BUFFER(&s->fr, s->buf, AST_FRIENDLY_OFFSET, ILBC_BUF_SIZE);
	if ((res = fread(s->fr.data, 1, s->fr.datalen, s->f)) != s->fr.datalen) {
		if (res)
			ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		return NULL;
	}
	*whennext = s->fr.samples = ILBC_SAMPLES;
	return &s->fr;
}

static int ilbc_write(struct ast_filestream *fs, struct ast_frame *f)
{
	int res;
	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass != AST_FORMAT_ILBC) {
		ast_log(LOG_WARNING, "Asked to write non-iLBC frame (%d)!\n", f->subclass);
		return -1;
	}
	if (f->datalen % 50) {
		ast_log(LOG_WARNING, "Invalid data length, %d, should be multiple of 50\n", f->datalen);
		return -1;
	}
	if ((res = fwrite(f->data, 1, f->datalen, fs->f)) != f->datalen) {
			ast_log(LOG_WARNING, "Bad write (%d/50): %s\n", res, strerror(errno));
			return -1;
	}
	return 0;
}

static int ilbc_seek(struct ast_filestream *fs, off_t sample_offset, int whence)
{
	long bytes;
	off_t min,cur,max,offset=0;
	min = 0;
	cur = ftello(fs->f);
	fseeko(fs->f, 0, SEEK_END);
	max = ftello(fs->f);
	
	bytes = ILBC_BUF_SIZE * (sample_offset / ILBC_SAMPLES);
	if (whence == SEEK_SET)
		offset = bytes;
	else if (whence == SEEK_CUR || whence == SEEK_FORCECUR)
		offset = cur + bytes;
	else if (whence == SEEK_END)
		offset = max - bytes;
	if (whence != SEEK_FORCECUR) {
		offset = (offset > max)?max:offset;
	}
	/* protect against seeking beyond begining. */
	offset = (offset < min)?min:offset;
	if (fseeko(fs->f, offset, SEEK_SET) < 0)
		return -1;
	return 0;
}

static int ilbc_trunc(struct ast_filestream *fs)
{
	/* Truncate file to current length */
	if (ftruncate(fileno(fs->f), ftello(fs->f)) < 0)
		return -1;
	return 0;
}

static off_t ilbc_tell(struct ast_filestream *fs)
{
	off_t offset = ftello(fs->f);
	return (offset/ILBC_BUF_SIZE)*ILBC_SAMPLES;
}

static const struct ast_format ilbc_f = {
	.name = "iLBC",
	.exts = "ilbc",
	.format = AST_FORMAT_ILBC,
	.write = ilbc_write,
	.seek = ilbc_seek,
	.trunc = ilbc_trunc,
	.tell = ilbc_tell,
	.read = ilbc_read,
	.buf_size = ILBC_BUF_SIZE + AST_FRIENDLY_OFFSET,
};

static int load_module(void)
{
	return ast_format_register(&ilbc_f);
}

static int unload_module(void)
{
	return ast_format_unregister(ilbc_f.name);
}	

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_FIRST, "Raw iLBC data",
	.load = load_module,
	.unload = unload_module,
);
