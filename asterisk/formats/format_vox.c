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
 * \brief Flat, binary, ADPCM vox file format.
 * \arg File name extensions: vox
 * 
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

#define BUF_SIZE	80		/* 80 bytes, 160 samples */
#define VOX_SAMPLES	160

static struct ast_frame *vox_read(struct ast_filestream *s, int *whennext)
{
	int res;

	/* Send a frame from the file to the appropriate channel */
	s->fr.frametype = AST_FRAME_VOICE;
	s->fr.subclass = AST_FORMAT_ADPCM;
	s->fr.mallocd = 0;
	AST_FRAME_SET_BUFFER(&s->fr, s->buf, AST_FRIENDLY_OFFSET, BUF_SIZE);
	if ((res = fread(s->fr.data, 1, s->fr.datalen, s->f)) < 1) {
		if (res)
			ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		return NULL;
	}
	*whennext = s->fr.samples = res * 2;
	s->fr.datalen = res;
	return &s->fr;
}

static int vox_write(struct ast_filestream *s, struct ast_frame *f)
{
	int res;
	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass != AST_FORMAT_ADPCM) {
		ast_log(LOG_WARNING, "Asked to write non-ADPCM frame (%d)!\n", f->subclass);
		return -1;
	}
	if ((res = fwrite(f->data, 1, f->datalen, s->f)) != f->datalen) {
			ast_log(LOG_WARNING, "Bad write (%d/%d): %s\n", res, f->datalen, strerror(errno));
			return -1;
	}
	return 0;
}

static int vox_seek(struct ast_filestream *fs, off_t sample_offset, int whence)
{
     off_t offset=0,min,cur,max,distance;
	
     min = 0;
     cur = ftello(fs->f);
     fseeko(fs->f, 0, SEEK_END);
	 max = ftello(fs->f);
	 
     /* have to fudge to frame here, so not fully to sample */
     distance = sample_offset/2;
     if(whence == SEEK_SET)
	  offset = distance;
     else if(whence == SEEK_CUR || whence == SEEK_FORCECUR)
	  offset = distance + cur;
     else if(whence == SEEK_END)
	  offset = max - distance;
     if (whence != SEEK_FORCECUR) {
	  offset = (offset > max)?max:offset;
	  offset = (offset < min)?min:offset;
     }
     return fseeko(fs->f, offset, SEEK_SET);
}

static int vox_trunc(struct ast_filestream *fs)
{
     return ftruncate(fileno(fs->f), ftello(fs->f));
}

static off_t vox_tell(struct ast_filestream *fs)
{
     off_t offset;
     offset = ftello(fs->f) << 1;
     return offset; 
}

static const struct ast_format vox_f = {
	.name = "vox",
	.exts = "vox",
	.format = AST_FORMAT_ADPCM,
	.write = vox_write,
	.seek = vox_seek,
	.trunc = vox_trunc,
	.tell = vox_tell,
	.read = vox_read,
	.buf_size = BUF_SIZE + AST_FRIENDLY_OFFSET,
};

static int load_module(void)
{
	return ast_format_register(&vox_f);
}

static int unload_module(void)
{
	return ast_format_unregister(vox_f.name);
}	

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_FIRST, "Dialogic VOX (ADPCM) File Format",
	.load = load_module,
	.unload = unload_module,
);
