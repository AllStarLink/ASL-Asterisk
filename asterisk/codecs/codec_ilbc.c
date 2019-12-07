/*
 * Asterisk -- An open source telephony toolkit.
 *
 * The iLBC code is from The IETF code base and is copyright The Internet Society (2004)
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
 * \brief Translate between signed linear and Internet Low Bitrate Codec
 * 
 * \ingroup codecs
 */

/*** MODULEINFO
	<defaultenabled>no</defaultenabled>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 228418 $")

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>

#include "asterisk/lock.h"
#include "asterisk/translate.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/utils.h"

#include "ilbc/iLBC_encode.h"
#include "ilbc/iLBC_decode.h"

/* Sample frame data */
#include "slin_ilbc_ex.h"
#include "ilbc_slin_ex.h"

#define USE_ILBC_ENHANCER	0
#define ILBC_MS 			30
/* #define ILBC_MS			20 */

#define	ILBC_FRAME_LEN	50	/* apparently... */
#define	ILBC_SAMPLES	240	/* 30ms at 8000 hz */
#define	BUFFER_SAMPLES	8000

struct ilbc_coder_pvt {
	iLBC_Enc_Inst_t enc;
	iLBC_Dec_Inst_t dec;
	/* Enough to store a full second */
	int16_t buf[BUFFER_SAMPLES];
};

static int lintoilbc_new(struct ast_trans_pvt *pvt)
{
	struct ilbc_coder_pvt *tmp = pvt->pvt;

	initEncode(&tmp->enc, ILBC_MS);

	return 0;
}

static int ilbctolin_new(struct ast_trans_pvt *pvt)
{
	struct ilbc_coder_pvt *tmp = pvt->pvt;

	initDecode(&tmp->dec, ILBC_MS, USE_ILBC_ENHANCER);

	return 0;
}

static struct ast_frame *lintoilbc_sample(void)
{
	static struct ast_frame f;
	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_SLINEAR;
	f.datalen = sizeof(slin_ilbc_ex);
	f.samples = sizeof(slin_ilbc_ex)/2;
	f.mallocd = 0;
	f.offset = 0;
	f.src = __PRETTY_FUNCTION__;
	f.data = slin_ilbc_ex;
	return &f;
}

static struct ast_frame *ilbctolin_sample(void)
{
	static struct ast_frame f;
	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_ILBC;
	f.datalen = sizeof(ilbc_slin_ex);
	/* All frames are 30 ms long */
	f.samples = ILBC_SAMPLES;
	f.mallocd = 0;
	f.offset = 0;
	f.src = __PRETTY_FUNCTION__;
	f.data = ilbc_slin_ex;
	return &f;
}

/*! \brief decode a frame and store in outbuf */
static int ilbctolin_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct ilbc_coder_pvt *tmp = pvt->pvt;
	int plc_mode = 1; /* 1 = normal data, 0 = plc */
	/* Assuming there's space left, decode into the current buffer at
	   the tail location.  Read in as many frames as there are */
	int x,i;
	int16_t *dst = (int16_t *)pvt->outbuf;
	float tmpf[ILBC_SAMPLES];

	if (!f->data && f->datalen) {
		ast_log(LOG_DEBUG, "issue 16070, ILIB ERROR. data = NULL datalen = %d src = %s\n", f->datalen, f->src ? f->src : "no src set");
		f->datalen = 0;
	}

	if (f->datalen == 0) { /* native PLC, set fake f->datalen and clear plc_mode */
		f->datalen = ILBC_FRAME_LEN;
		f->samples = ILBC_SAMPLES;
		plc_mode = 0;	/* do native plc */
		pvt->samples += ILBC_SAMPLES;
	}

	if (f->datalen % ILBC_FRAME_LEN) {
		ast_log(LOG_WARNING, "Huh?  An ilbc frame that isn't a multiple of 50 bytes long from %s (%d)?\n", f->src, f->datalen);
		return -1;
	}
	
	for (x=0; x < f->datalen ; x += ILBC_FRAME_LEN) {
		if (pvt->samples + ILBC_SAMPLES > BUFFER_SAMPLES) {	
			ast_log(LOG_WARNING, "Out of buffer space\n");
			return -1;
		}		
		iLBC_decode(tmpf, plc_mode ? f->data + x : NULL, &tmp->dec, plc_mode);
		for ( i=0; i < ILBC_SAMPLES; i++)
			dst[pvt->samples + i] = tmpf[i];
		pvt->samples += ILBC_SAMPLES;
		pvt->datalen += 2*ILBC_SAMPLES;
	}
	return 0;
}

/*! \brief store a frame into a temporary buffer, for later decoding */
static int lintoilbc_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct ilbc_coder_pvt *tmp = pvt->pvt;

	/* Just add the frames to our stream */
	/* XXX We should look at how old the rest of our stream is, and if it
	   is too old, then we should overwrite it entirely, otherwise we can
	   get artifacts of earlier talk that do not belong */
	memcpy(tmp->buf + pvt->samples, f->data, f->datalen);
	pvt->samples += f->samples;
	return 0;
}

/*! \brief encode the temporary buffer and generate a frame */
static struct ast_frame *lintoilbc_frameout(struct ast_trans_pvt *pvt)
{
	struct ilbc_coder_pvt *tmp = pvt->pvt;
	int datalen = 0;
	int samples = 0;

	/* We can't work on anything less than a frame in size */
	if (pvt->samples < ILBC_SAMPLES)
		return NULL;
	while (pvt->samples >= ILBC_SAMPLES) {
		float tmpf[ILBC_SAMPLES];
		int i;

		/* Encode a frame of data */
		for (i = 0 ; i < ILBC_SAMPLES ; i++)
			tmpf[i] = tmp->buf[samples + i];
		iLBC_encode((unsigned char *) pvt->outbuf + datalen, tmpf, &tmp->enc);

		datalen += ILBC_FRAME_LEN;
		samples += ILBC_SAMPLES;
		pvt->samples -= ILBC_SAMPLES;
	}

	/* Move the data at the end of the buffer to the front */
	if (pvt->samples)
		memmove(tmp->buf, tmp->buf + samples, pvt->samples * 2);

	return ast_trans_frameout(pvt, datalen, samples);
}

static struct ast_translator ilbctolin = {
	.name = "ilbctolin", 
	.srcfmt = AST_FORMAT_ILBC,
	.dstfmt = AST_FORMAT_SLINEAR,
	.newpvt = ilbctolin_new,
	.framein = ilbctolin_framein,
	.sample = ilbctolin_sample,
	.desc_size = sizeof(struct ilbc_coder_pvt),
	.buf_size = BUFFER_SAMPLES * 2,
	.native_plc = 1,
};

static struct ast_translator lintoilbc = {
	.name = "lintoilbc", 
	.srcfmt = AST_FORMAT_SLINEAR,
	.dstfmt = AST_FORMAT_ILBC,
	.newpvt = lintoilbc_new,
	.framein = lintoilbc_framein,
	.frameout = lintoilbc_frameout,
	.sample = lintoilbc_sample,
	.desc_size = sizeof(struct ilbc_coder_pvt),
	.buf_size = (BUFFER_SAMPLES * ILBC_FRAME_LEN + ILBC_SAMPLES - 1) / ILBC_SAMPLES,
};

static int unload_module(void)
{
	int res;

	res = ast_unregister_translator(&lintoilbc);
	res |= ast_unregister_translator(&ilbctolin);

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_register_translator(&ilbctolin);
	if (!res) 
		res=ast_register_translator(&lintoilbc);
	else
		ast_unregister_translator(&ilbctolin);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "iLBC Coder/Decoder");
