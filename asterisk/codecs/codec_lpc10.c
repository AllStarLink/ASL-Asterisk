/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * The lpc10 code is from a library used by nautilus, modified to be a bit
 * nicer to the compiler.
 * See http://www.arl.wustl.edu/~jaf/ 
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
 * \brief Translate between signed linear and LPC10 (Linear Predictor Code)
 *
 * \ingroup codecs
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 42477 $")

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>

#include "asterisk/lock.h"
#include "asterisk/translate.h"
#include "asterisk/config.h"
#include "asterisk/options.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/utils.h"

#include "lpc10/lpc10.h"

/* Sample frame data */
#include "slin_lpc10_ex.h"
#include "lpc10_slin_ex.h"

/* We use a very strange format here...  I have no idea why...  The frames are 180
   samples long, which isn't even an even number of milliseconds...  Not only that
   but we hvae to waste two bits of each frame to keep them ending on a byte boundary
   because the frames are 54 bits long */

#define LPC10_BYTES_IN_COMPRESSED_FRAME (LPC10_BITS_IN_COMPRESSED_FRAME + 7)/8

#define	BUFFER_SAMPLES	8000

struct lpc10_coder_pvt {
	union {
		struct lpc10_encoder_state *enc;
		struct lpc10_decoder_state *dec;
	} lpc10;
	/* Enough to store a full second */
	short buf[BUFFER_SAMPLES];
	int longer;
};

static int lpc10_enc_new(struct ast_trans_pvt *pvt)
{
	struct lpc10_coder_pvt *tmp = pvt->pvt;

	return (tmp->lpc10.enc = create_lpc10_encoder_state()) ? 0 : -1;
}

static int lpc10_dec_new(struct ast_trans_pvt *pvt)
{
	struct lpc10_coder_pvt *tmp = pvt->pvt;

	return (tmp->lpc10.dec = create_lpc10_decoder_state()) ? 0 : -1;
}

static struct ast_frame *lintolpc10_sample(void)
{
	static struct ast_frame f;
	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_SLINEAR;
	f.datalen = sizeof(slin_lpc10_ex);
	/* Assume 8000 Hz */
	f.samples = LPC10_SAMPLES_PER_FRAME;
	f.mallocd = 0;
	f.offset = 0;
	f.src = __PRETTY_FUNCTION__;
	f.data = slin_lpc10_ex;
	return &f;
}

static struct ast_frame *lpc10tolin_sample(void)
{
	static struct ast_frame f;
	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_LPC10;
	f.datalen = sizeof(lpc10_slin_ex);
	/* All frames are 22 ms long (maybe a little more -- why did he choose
	   LPC10_SAMPLES_PER_FRAME sample frames anyway?? */
	f.samples = LPC10_SAMPLES_PER_FRAME;
	f.mallocd = 0;
	f.offset = 0;
	f.src = __PRETTY_FUNCTION__;
	f.data = lpc10_slin_ex;
	return &f;
}

static void extract_bits(INT32 *bits, unsigned char *c)
{
	int x;
	for (x=0;x<LPC10_BITS_IN_COMPRESSED_FRAME;x++) {
		if (*c & (0x80 >> (x & 7)))
			bits[x] = 1;
		else
			bits[x] = 0;
		if ((x & 7) == 7)
			c++;
	}
}

/* XXX note lpc10_encode() produces one bit per word in bits[] */
static void build_bits(unsigned char *c, INT32 *bits)
{
	unsigned char mask=0x80;
	int x;
	*c = 0;
	for (x=0;x<LPC10_BITS_IN_COMPRESSED_FRAME;x++) {
		if (bits[x])
			*c |= mask;
		mask = mask >> 1;
		if ((x % 8)==7) {
			c++;
			*c = 0;
			mask = 0x80;
		}
	}
}

static int lpc10tolin_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct lpc10_coder_pvt *tmp = pvt->pvt;
	int16_t *dst = (int16_t *)pvt->outbuf;
	int len = 0;

	while (len + LPC10_BYTES_IN_COMPRESSED_FRAME <= f->datalen) {
		int x;
		float tmpbuf[LPC10_SAMPLES_PER_FRAME];
		INT32 bits[LPC10_BITS_IN_COMPRESSED_FRAME]; /* XXX see note */
		if (pvt->samples + LPC10_SAMPLES_PER_FRAME > BUFFER_SAMPLES) {
			ast_log(LOG_WARNING, "Out of buffer space\n");
			return -1;
		}
		extract_bits(bits, f->data + len);
		if (lpc10_decode(bits, tmpbuf, tmp->lpc10.dec)) {
			ast_log(LOG_WARNING, "Invalid lpc10 data\n");
			return -1;
		}
		for (x=0;x<LPC10_SAMPLES_PER_FRAME;x++) {
			/* Convert to a short between -1.0 and 1.0 */
			dst[pvt->samples + x] = (int16_t)(32768.0 * tmpbuf[x]);
		}

		pvt->samples += LPC10_SAMPLES_PER_FRAME;
		pvt->datalen += 2*LPC10_SAMPLES_PER_FRAME;
		len += LPC10_BYTES_IN_COMPRESSED_FRAME;
	}
	if (len != f->datalen) 
		printf("Decoded %d, expected %d\n", len, f->datalen);
	return 0;
}

static int lintolpc10_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct lpc10_coder_pvt *tmp = pvt->pvt;

	/* Just add the frames to our stream */
	if (pvt->samples + f->samples > BUFFER_SAMPLES) {
		ast_log(LOG_WARNING, "Out of buffer space\n");
		return -1;
	}
	memcpy(tmp->buf + pvt->samples, f->data, f->datalen);
	pvt->samples += f->samples;
	return 0;
}

static struct ast_frame *lintolpc10_frameout(struct ast_trans_pvt *pvt)
{
	struct lpc10_coder_pvt *tmp = pvt->pvt;
	int x;
	int datalen = 0;	/* output frame */
	int samples = 0;	/* output samples */
	float tmpbuf[LPC10_SAMPLES_PER_FRAME];
	INT32 bits[LPC10_BITS_IN_COMPRESSED_FRAME];	/* XXX what ??? */
	/* We can't work on anything less than a frame in size */
	if (pvt->samples < LPC10_SAMPLES_PER_FRAME)
		return NULL;
	while (pvt->samples >=  LPC10_SAMPLES_PER_FRAME) {
		/* Encode a frame of data */
		for (x=0;x<LPC10_SAMPLES_PER_FRAME;x++)
			tmpbuf[x] = (float)tmp->buf[x + samples] / 32768.0;
		lpc10_encode(tmpbuf, bits, tmp->lpc10.enc);
		build_bits((unsigned char *) pvt->outbuf + datalen, bits);
		datalen += LPC10_BYTES_IN_COMPRESSED_FRAME;
		samples += LPC10_SAMPLES_PER_FRAME;
		pvt->samples -= LPC10_SAMPLES_PER_FRAME;
		/* Use one of the two left over bits to record if this is a 22 or 23 ms frame...
		   important for IAX use */
		tmp->longer = 1 - tmp->longer;
	}
	/* Move the data at the end of the buffer to the front */
	if (pvt->samples)
		memmove(tmp->buf, tmp->buf + samples, pvt->samples * 2);
	return ast_trans_frameout(pvt, datalen, samples);
}


static void lpc10_destroy(struct ast_trans_pvt *arg)
{
	struct lpc10_coder_pvt *pvt = arg->pvt;
	/* Enc and DEC are both just allocated, so they can be freed */
	free(pvt->lpc10.enc);
}

static struct ast_translator lpc10tolin = {
	.name = "lpc10tolin", 
	.srcfmt = AST_FORMAT_LPC10,
	.dstfmt = AST_FORMAT_SLINEAR,
	.newpvt = lpc10_dec_new,
	.framein = lpc10tolin_framein,
	.destroy = lpc10_destroy,
	.sample = lpc10tolin_sample,
	.desc_size = sizeof(struct lpc10_coder_pvt),
	.buffer_samples = BUFFER_SAMPLES,
	.plc_samples = LPC10_SAMPLES_PER_FRAME,
	.buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_translator lintolpc10 = {
	.name = "lintolpc10", 
	.srcfmt = AST_FORMAT_SLINEAR,
	.dstfmt = AST_FORMAT_LPC10,
	.newpvt = lpc10_enc_new,
	.framein = lintolpc10_framein,
	.frameout = lintolpc10_frameout,
	.destroy = lpc10_destroy,
	.sample = lintolpc10_sample,
	.desc_size = sizeof(struct lpc10_coder_pvt),
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = LPC10_BYTES_IN_COMPRESSED_FRAME * (1 + BUFFER_SAMPLES / LPC10_SAMPLES_PER_FRAME),
};

static void parse_config(void)
{
        struct ast_variable *var;
        struct ast_config *cfg = ast_config_load("codecs.conf");
	if (!cfg)
		return;
	for (var = ast_variable_browse(cfg, "plc"); var; var = var->next) {
	       if (!strcasecmp(var->name, "genericplc")) {
			lpc10tolin.useplc = ast_true(var->value) ? 1 : 0;
			if (option_verbose > 2)
			       ast_verbose(VERBOSE_PREFIX_3 "codec_lpc10: %susing generic PLC\n",
					lpc10tolin.useplc ? "" : "not ");
		}
        }
	ast_config_destroy(cfg);
}

static int reload(void)
{
        parse_config();

        return 0;
}


static int unload_module(void)
{
	int res;

	res = ast_unregister_translator(&lintolpc10);
	res |= ast_unregister_translator(&lpc10tolin);

	return res;
}

static int load_module(void)
{
	int res;

	parse_config();
	res=ast_register_translator(&lpc10tolin);
	if (!res) 
		res=ast_register_translator(&lintolpc10);
	else
		ast_unregister_translator(&lpc10tolin);

	return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "LPC10 2.4kbps Coder/Decoder",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
