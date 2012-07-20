/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Based on frompcm.c and topcm.c from the Emiliano MIPL browser/
 * interpreter.  See http://www.bsdtelephony.com.mx
 *
 * Copyright (c) 2001 - 2005 Digium, Inc.
 * All rights reserved.
 *
 * Karl Sackett <krs@linux-support.net>, 2001-03-21
 * Jim Dixon <jim@lambdatel.com>, 2008-08-28 (Added IRLP Functionality)
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
 * \brief codec_adpcm.c - translate between signed linear and Dialogic ADPCM
 * or IMA PCM (for IRLP).
 *
 * This sends the 3 byte ADPCM state info (and expects to receive it too)
 * from the silly IRLP system at the end of the data block. In other words,
 * instead of the datalen being samples / 2 (encoded), its 
 * (samples / 2) + 3, with the state info added to the end of the data block.
 * 
 * \ingroup codecs
 */

#define	ADPCM_IRLP

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 141669 $")

#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asterisk/lock.h"
#include "asterisk/logger.h"
#include "asterisk/linkedlists.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/options.h"
#include "asterisk/translate.h"
#include "asterisk/channel.h"
#include "asterisk/utils.h"
#include "../astver.h"

#define BUFFER_SAMPLES   8096	/* size for the translation buffers */

#ifdef	ADPCM_IRLP

/***********************************************************
For IMA ADPCM Codec, for IRLP use:

Copyright 1992 by Stichting Mathematisch Centrum, Amsterdam, The
Netherlands.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its 
documentation for any purpose and without fee is hereby granted, 
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in 
supporting documentation, and that the names of Stichting Mathematisch
Centrum or CWI not be used in advertising or publicity pertaining to
distribution of the software without specific, written prior permission.

STICHTING MATHEMATISCH CENTRUM DISCLAIMS ALL WARRANTIES WITH REGARD TO
THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS, IN NO EVENT SHALL STICHTING MATHEMATISCH CENTRUM BE LIABLE
FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

******************************************************************/

/*
** Intel/DVI ADPCM coder/decoder.
**
** The algorithm for this coder was taken from the IMA Compatability Project
** proceedings, Vol 2, Number 2; May 1992.
**
** Version 1.2, 18-Dec-92.
**
** Change log:
** - Fixed a stupid bug, where the delta was computed as
**   stepsize*code/4 in stead of stepsize*(code+0.5)/4.
** - There was an off-by-one error causing it to pick
**   an incorrect delta once in a blue moon.
** - The NODIVMUL define has been removed. Computations are now always done
**   using shifts, adds and subtracts. It turned out that, because the standard
**   is defined using shift/add/subtract, you needed bits of fixup code
**   (because the div/mul simulation using shift/add/sub made some rounding
**   errors that real div/mul don't make) and all together the resultant code
**   ran slower than just using the shifts all the time.
** - Changed some of the variable names to be more meaningful.
*/

struct adpcm_state {
    short	valprev;	/* Previous output value */
    char	index;		/* Index into stepsize table */
};

/* Intel ADPCM step variation table */
static int indexTable[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8,
};

static int stepsizeTable[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

#ifdef NEW_ASTERISK

/* Sample frame data */
#include "asterisk/slin.h"
#include "ex_adpcm.h"


static void
adpcm_coder(short *indata, char *outdata, int len, struct adpcm_state *state)
{
    short *inp;			/* Input buffer pointer */
    signed char *outp;		/* output buffer pointer */
    int val;			/* Current input sample value */
    int sign;			/* Current adpcm sign bit */
    int delta;			/* Current adpcm output value */
    int diff;			/* Difference between val and valprev */
    int step;			/* Stepsize */
    int valpred;		/* Predicted output value */
    int vpdiff;			/* Current change to valpred */
    int index;			/* Current step change index */
    int outputbuffer;		/* place to keep previous 4-bit value */
    int bufferstep;		/* toggle between outputbuffer/output */

    outp = (signed char *)outdata;
    inp = indata;

    valpred = state->valprev;
    index = state->index;
    step = stepsizeTable[index];
    
    bufferstep = 1;
    outputbuffer = 0;

    for ( ; len > 0 ; len-- ) {
	val = *inp++;

	/* Step 1 - compute difference with previous value */
	diff = val - valpred;
	sign = (diff < 0) ? 8 : 0;
	if ( sign ) diff = (-diff);

	/* Step 2 - Divide and clamp */
	/* Note:
	** This code *approximately* computes:
	**    delta = diff*4/step;
	**    vpdiff = (delta+0.5)*step/4;
	** but in shift step bits are dropped. The net result of this is
	** that even if you have fast mul/div hardware you cannot put it to
	** good use since the fixup would be too expensive.
	*/
	delta = 0;
	vpdiff = (step >> 3);
	
	if ( diff >= step ) {
	    delta = 4;
	    diff -= step;
	    vpdiff += step;
	}
	step >>= 1;
	if ( diff >= step  ) {
	    delta |= 2;
	    diff -= step;
	    vpdiff += step;
	}
	step >>= 1;
	if ( diff >= step ) {
	    delta |= 1;
	    vpdiff += step;
	}

	/* Step 3 - Update previous value */
	if ( sign )
	  valpred -= vpdiff;
	else
	  valpred += vpdiff;

	/* Step 4 - Clamp previous value to 16 bits */
	if ( valpred > 32767 )
	  valpred = 32767;
	else if ( valpred < -32768 )
	  valpred = -32768;

	/* Step 5 - Assemble value, update index and step values */
	delta |= sign;
	
	index += indexTable[delta];
	if ( index < 0 ) index = 0;
	if ( index > 88 ) index = 88;
	step = stepsizeTable[index];

	/* Step 6 - Output value */
	if ( bufferstep ) {
	    outputbuffer = (delta << 4) & 0xf0;
	} else {
	    *outp++ = (delta & 0x0f) | outputbuffer;
	}
	bufferstep = !bufferstep;
    }

    /* Output last step, if needed */
    if ( !bufferstep )
      *outp++ = outputbuffer;
    
    state->valprev = valpred;
    state->index = index;
}

static void
adpcm_decoder(char *indata, short *outdata, int len, struct adpcm_state *state)
{
    signed char *inp;		/* Input buffer pointer */
    short *outp;		/* output buffer pointer */
    int sign;			/* Current adpcm sign bit */
    int delta;			/* Current adpcm output value */
    int step;			/* Stepsize */
    int valpred;		/* Predicted value */
    int vpdiff;			/* Current change to valpred */
    int index;			/* Current step change index */
    int inputbuffer;		/* place to keep next 4-bit value */
    int bufferstep;		/* toggle between inputbuffer/input */

    outp = outdata;
    inp = (signed char *)indata;

    valpred = state->valprev;
    index = state->index;
    step = stepsizeTable[index];

    bufferstep = 0;
    inputbuffer = 0;
    
    for ( ; len > 0 ; len-- ) {
	
	/* Step 1 - get the delta value */
	if ( bufferstep ) {
	    delta = inputbuffer & 0xf;
	} else {
	    inputbuffer = *inp++;
	    delta = (inputbuffer >> 4) & 0xf;
	}
	bufferstep = !bufferstep;

	/* Step 2 - Find new index value (for later) */
	index += indexTable[delta];
	if ( index < 0 ) index = 0;
	if ( index > 88 ) index = 88;

	/* Step 3 - Separate sign and magnitude */
	sign = delta & 8;
	delta = delta & 7;

	/* Step 4 - Compute difference and new predicted value */
	/*
	** Computes 'vpdiff = (delta+0.5)*step/4', but see comment
	** in adpcm_coder.
	*/
	vpdiff = step >> 3;
	if ( delta & 4 ) vpdiff += step;
	if ( delta & 2 ) vpdiff += step>>1;
	if ( delta & 1 ) vpdiff += step>>2;

	if ( sign )
	  valpred -= vpdiff;
	else
	  valpred += vpdiff;

	/* Step 5 - clamp output value */
	if ( valpred > 32767 )
	  valpred = 32767;
	else if ( valpred < -32768 )
	  valpred = -32768;

	/* Step 6 - Update step value */
	step = stepsizeTable[index];

	/* Step 7 - Output value */
	*outp++ = valpred;
    }

    state->valprev = valpred;
    state->index = index;
}

/*----------------- Asterisk-codec glue ------------*/

/*! \brief Workspace for translating signed linear signals to ADPCM. */
struct adpcm_encoder_pvt {
	struct adpcm_state state;
	int16_t inbuf[BUFFER_SAMPLES];	/* Unencoded signed linear values */
};

/*! \brief Workspace for translating ADPCM signals to signed linear. */
struct adpcm_decoder_pvt {
	struct adpcm_state state;
};

/*! \brief decode 4-bit adpcm frame data and store in output buffer */
static int adpcmtolin_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct adpcm_decoder_pvt *tmp = pvt->pvt;
	int x = f->datalen;
	unsigned char *cp = f->data.ptr;
	int16_t *dst = pvt->outbuf.i16 + pvt->samples;

	if (x > (f->samples / 2))
	{
		cp += (x - 3);
		tmp->state.valprev = (cp[0] << 8) + cp[1];
		tmp->state.index = cp[2];
	}	
	adpcm_decoder(f->data.ptr,dst,f->samples,&tmp->state);
	pvt->samples += f->samples;
	pvt->datalen += f->samples * 2;
	return 0;
}

/*! \brief fill input buffer with 16-bit signed linear PCM values. */
static int lintoadpcm_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct adpcm_encoder_pvt *tmp = pvt->pvt;

	memcpy(&tmp->inbuf[pvt->samples], f->data.ptr, f->datalen);
	pvt->samples += f->samples;
	return 0;
}

/*! \brief convert inbuf and store into frame */
static struct ast_frame *lintoadpcm_frameout(struct ast_trans_pvt *pvt)
{
	struct adpcm_encoder_pvt *tmp = pvt->pvt;
	struct ast_frame *f;
	int samples = pvt->samples;	/* save original number */
	int x;
	char *cp;
	struct adpcm_state istate;
  
	if (samples < 2)
		return NULL;

	pvt->samples &= ~1; /* atomic size is 2 samples */
	istate = tmp->state;
	adpcm_coder(tmp->inbuf, pvt->outbuf.c, pvt->samples,&tmp->state);
	x = pvt->samples / 2;
	cp = pvt->outbuf.c;
	cp[x] = (istate.valprev & 0xff00) >> 8;
	cp[x + 1] = istate.valprev & 0xff;
	cp[x + 2] = istate.index; 
	f = ast_trans_frameout(pvt, x + 3, 0);

	/*
	 * If there is a left over sample, move it to the beginning
	 * of the input buffer.
	 */

	if (samples & 1) {	/* move the leftover sample at beginning */
		tmp->inbuf[0] = tmp->inbuf[samples - 1];
		pvt->samples = 1;
	}
	return f;
}

#else

#include "slin_adpcm_ex.h"
#include "adpcm_slin_ex.h"

static void
adpcm_coder(short *indata, char *outdata, int len, struct adpcm_state *state)
{
    short *inp;			/* Input buffer pointer */
    signed char *outp;		/* output buffer pointer */
    int val;			/* Current input sample value */
    int sign;			/* Current adpcm sign bit */
    int delta;			/* Current adpcm output value */
    int diff;			/* Difference between val and valprev */
    int step;			/* Stepsize */
    int valpred;		/* Predicted output value */
    int vpdiff;			/* Current change to valpred */
    int index;			/* Current step change index */
    int outputbuffer;		/* place to keep previous 4-bit value */
    int bufferstep;		/* toggle between outputbuffer/output */

    outp = (signed char *)outdata;
    inp = indata;

    valpred = state->valprev;
    index = state->index;
    step = stepsizeTable[index];
    
    bufferstep = 1;
    outputbuffer = 0;

    for ( ; len > 0 ; len-- ) {
	val = *inp++;

	/* Step 1 - compute difference with previous value */
	diff = val - valpred;
	sign = (diff < 0) ? 8 : 0;
	if ( sign ) diff = (-diff);

	/* Step 2 - Divide and clamp */
	/* Note:
	** This code *approximately* computes:
	**    delta = diff*4/step;
	**    vpdiff = (delta+0.5)*step/4;
	** but in shift step bits are dropped. The net result of this is
	** that even if you have fast mul/div hardware you cannot put it to
	** good use since the fixup would be too expensive.
	*/
	delta = 0;
	vpdiff = (step >> 3);
	
	if ( diff >= step ) {
	    delta = 4;
	    diff -= step;
	    vpdiff += step;
	}
	step >>= 1;
	if ( diff >= step  ) {
	    delta |= 2;
	    diff -= step;
	    vpdiff += step;
	}
	step >>= 1;
	if ( diff >= step ) {
	    delta |= 1;
	    vpdiff += step;
	}

	/* Step 3 - Update previous value */
	if ( sign )
	  valpred -= vpdiff;
	else
	  valpred += vpdiff;

	/* Step 4 - Clamp previous value to 16 bits */
	if ( valpred > 32767 )
	  valpred = 32767;
	else if ( valpred < -32768 )
	  valpred = -32768;

	/* Step 5 - Assemble value, update index and step values */
	delta |= sign;
	
	index += indexTable[delta];
	if ( index < 0 ) index = 0;
	if ( index > 88 ) index = 88;
	step = stepsizeTable[index];

	/* Step 6 - Output value */
	if ( bufferstep ) {
	    outputbuffer = (delta << 4) & 0xf0;
	} else {
	    *outp++ = (delta & 0x0f) | outputbuffer;
	}
	bufferstep = !bufferstep;
    }

    /* Output last step, if needed */
    if ( !bufferstep )
      *outp++ = outputbuffer;
    
    state->valprev = valpred;
    state->index = index;
}

static void
adpcm_decoder(char *indata, short *outdata, int len, struct adpcm_state *state)
{
    signed char *inp;		/* Input buffer pointer */
    short *outp;		/* output buffer pointer */
    int sign;			/* Current adpcm sign bit */
    int delta;			/* Current adpcm output value */
    int step;			/* Stepsize */
    int valpred;		/* Predicted value */
    int vpdiff;			/* Current change to valpred */
    int index;			/* Current step change index */
    int inputbuffer;		/* place to keep next 4-bit value */
    int bufferstep;		/* toggle between inputbuffer/input */

    outp = outdata;
    inp = (signed char *)indata;

    valpred = state->valprev;
    index = state->index;
    step = stepsizeTable[index];

    bufferstep = 0;
    inputbuffer = 0;
    
    for ( ; len > 0 ; len-- ) {
	
	/* Step 1 - get the delta value */
	if ( bufferstep ) {
	    delta = inputbuffer & 0xf;
	} else {
	    inputbuffer = *inp++;
	    delta = (inputbuffer >> 4) & 0xf;
	}
	bufferstep = !bufferstep;

	/* Step 2 - Find new index value (for later) */
	index += indexTable[delta];
	if ( index < 0 ) index = 0;
	if ( index > 88 ) index = 88;

	/* Step 3 - Separate sign and magnitude */
	sign = delta & 8;
	delta = delta & 7;

	/* Step 4 - Compute difference and new predicted value */
	/*
	** Computes 'vpdiff = (delta+0.5)*step/4', but see comment
	** in adpcm_coder.
	*/
	vpdiff = step >> 3;
	if ( delta & 4 ) vpdiff += step;
	if ( delta & 2 ) vpdiff += step>>1;
	if ( delta & 1 ) vpdiff += step>>2;

	if ( sign )
	  valpred -= vpdiff;
	else
	  valpred += vpdiff;

	/* Step 5 - clamp output value */
	if ( valpred > 32767 )
	  valpred = 32767;
	else if ( valpred < -32768 )
	  valpred = -32768;

	/* Step 6 - Update step value */
	step = stepsizeTable[index];

	/* Step 7 - Output value */
	*outp++ = valpred;
    }

    state->valprev = valpred;
    state->index = index;
}

/*----------------- Asterisk-codec glue ------------*/

/*! \brief Workspace for translating signed linear signals to ADPCM. */
struct adpcm_encoder_pvt {
	struct adpcm_state state;
	int16_t inbuf[BUFFER_SAMPLES];	/* Unencoded signed linear values */
};

/*! \brief Workspace for translating ADPCM signals to signed linear. */
struct adpcm_decoder_pvt {
	struct adpcm_state state;
};

/*! \brief decode 4-bit adpcm frame data and store in output buffer */
static int adpcmtolin_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct adpcm_decoder_pvt *tmp = pvt->pvt;
	int16_t *dst = (int16_t *)pvt->outbuf + pvt->samples;
	int x = f->datalen;
	char *cp = f->data;	

	if (x > (f->samples / 2))
	{
		cp += (x - 3);
		tmp->state.valprev = (cp[0] << 8) + cp[1];
		tmp->state.index = cp[2];
	}	
	adpcm_decoder(f->data,dst,f->samples,&tmp->state);
	pvt->samples += f->samples;
	pvt->datalen += f->samples * 2;
	return 0;
}

/*! \brief fill input buffer with 16-bit signed linear PCM values. */
static int lintoadpcm_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct adpcm_encoder_pvt *tmp = pvt->pvt;

	memcpy(&tmp->inbuf[pvt->samples], f->data, f->datalen);
	pvt->samples += f->samples;
	return 0;
}

/*! \brief convert inbuf and store into frame */
static struct ast_frame *lintoadpcm_frameout(struct ast_trans_pvt *pvt)
{
	struct adpcm_encoder_pvt *tmp = pvt->pvt;
	struct ast_frame *f;
	int samples = pvt->samples;	/* save original number */
	int x;
	char *cp;
	struct adpcm_state istate;
  
	if (samples < 2)
		return NULL;

	pvt->samples &= ~1; /* atomic size is 2 samples */
	istate = tmp->state;
	adpcm_coder(tmp->inbuf, pvt->outbuf, pvt->samples,&tmp->state);
	x = pvt->samples / 2;
	cp = pvt->outbuf;
	cp[x] = (istate.valprev & 0xff00) >> 8;
	cp[x + 1] = istate.valprev & 0xff;
	cp[x + 2] = istate.index; 
	f = ast_trans_frameout(pvt, x + 3, 0);

	/*
	 * If there is a left over sample, move it to the beginning
	 * of the input buffer.
	 */

	if (samples & 1) {	/* move the leftover sample at beginning */
		tmp->inbuf[0] = tmp->inbuf[samples - 1];
		pvt->samples = 1;
	}
	return f;
}


#endif /* NEW_ASTERISK */

#else /* ADPCM_IRLP */

/* define NOT_BLI to use a faster but not bit-level identical version */
/* #define NOT_BLI */


/* Sample frame data */

#include "slin_adpcm_ex.h"
#include "adpcm_slin_ex.h"

/*
 * Step size index shift table 
 */

static int indsft[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

/*
 * Step size table, where stpsz[i]=floor[16*(11/10)^i]
 */

static int stpsz[49] = {
  16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73,
  80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279,
  307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
  1060, 1166, 1282, 1411, 1552
};

/*
 * Decoder/Encoder state
 *   States for both encoder and decoder are synchronized
 */
struct adpcm_state {
	int ssindex;
	int signal;
	int zero_count;
	int next_flag;
};

/*
 * Decode(encoded)
 *  Decodes the encoded nibble from the adpcm file.
 *
 * Results:
 *  Returns the encoded difference.
 *
 * Side effects:
 *  Sets the index to the step size table for the next encode.
 */

static inline short decode(int encoded, struct adpcm_state *state)
{
	int diff;
	int step;
	int sign;

	step = stpsz[state->ssindex];

	sign = encoded & 0x08;
	encoded &= 0x07;
#ifdef NOT_BLI
	diff = (((encoded << 1) + 1) * step) >> 3;
#else /* BLI code */
	diff = step >> 3;
	if (encoded & 4)
		diff += step;
	if (encoded & 2)
		diff += step >> 1;
	if (encoded & 1)
		diff += step >> 2;
	if ((encoded >> 1) & step & 0x1)
		diff++;
#endif
	if (sign)
		diff = -diff;

	if (state->next_flag & 0x1)
		state->signal -= 8;
	else if (state->next_flag & 0x2)
		state->signal += 8;

	state->signal += diff;

	if (state->signal > 2047)
		state->signal = 2047;
	else if (state->signal < -2047)
		state->signal = -2047;

	state->next_flag = 0;

#ifdef AUTO_RETURN
	if (encoded)
		state->zero_count = 0;
	else if (++(state->zero_count) == 24) {
		state->zero_count = 0;
		if (state->signal > 0)
			state->next_flag = 0x1;
		else if (state->signal < 0)
			state->next_flag = 0x2;
	}
#endif

	state->ssindex += indsft[encoded];
	if (state->ssindex < 0)
		state->ssindex = 0;
	else if (state->ssindex > 48)
		state->ssindex = 48;

	return state->signal << 4;
}

/*
 * Adpcm
 *  Takes a signed linear signal and encodes it as ADPCM
 *  For more information see http://support.dialogic.com/appnotes/adpcm.pdf
 *
 * Results:
 *  Foo.
 *
 * Side effects:
 *  signal gets updated with each pass.
 */

static inline int adpcm(short csig, struct adpcm_state *state)
{
	int diff;
	int step;
	int encoded;

	/* 
	 * Clip csig if too large or too small
	 */
	csig >>= 4;

	step = stpsz[state->ssindex];
	diff = csig - state->signal;

#ifdef NOT_BLI
	if (diff < 0) {
		encoded = (-diff << 2) / step;
		if (encoded > 7)
			encoded = 7;
		encoded |= 0x08;
	} else {
		encoded = (diff << 2) / step;
		if (encoded > 7)
			encoded = 7;
	}
#else /* BLI code */
	if (diff < 0) {
		encoded = 8;
		diff = -diff;
	} else
		encoded = 0;
	if (diff >= step) {
		encoded |= 4;
		diff -= step;
	}
	step >>= 1;
	if (diff >= step) {
		encoded |= 2;
		diff -= step;
	}
	step >>= 1;
	if (diff >= step)
		encoded |= 1;
#endif /* NOT_BLI */

	/* feedback to state */
	decode(encoded, state);
	
	return encoded;
}

/*----------------- Asterisk-codec glue ------------*/

/*! \brief Workspace for translating signed linear signals to ADPCM. */
struct adpcm_encoder_pvt {
	struct adpcm_state state;
	int16_t inbuf[BUFFER_SAMPLES];	/* Unencoded signed linear values */
};

/*! \brief Workspace for translating ADPCM signals to signed linear. */
struct adpcm_decoder_pvt {
	struct adpcm_state state;
};

/*! \brief decode 4-bit adpcm frame data and store in output buffer */
static int adpcmtolin_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct adpcm_decoder_pvt *tmp = pvt->pvt;
	int x = f->datalen;
	unsigned char *src = f->data;
	int16_t *dst = (int16_t *)pvt->outbuf + pvt->samples;

	while (x--) {
		*dst++ = decode((*src >> 4) & 0xf, &tmp->state);
		*dst++ = decode(*src++ & 0x0f, &tmp->state);
	}
	pvt->samples += f->samples;
	pvt->datalen += 2*f->samples;
	return 0;
}

/*! \brief fill input buffer with 16-bit signed linear PCM values. */
static int lintoadpcm_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct adpcm_encoder_pvt *tmp = pvt->pvt;

	memcpy(&tmp->inbuf[pvt->samples], f->data, f->datalen);
	pvt->samples += f->samples;
	return 0;
}

/*! \brief convert inbuf and store into frame */
static struct ast_frame *lintoadpcm_frameout(struct ast_trans_pvt *pvt)
{
	struct adpcm_encoder_pvt *tmp = pvt->pvt;
	struct ast_frame *f;
	int i;
	int samples = pvt->samples;	/* save original number */
  
	if (samples < 2)
		return NULL;

	pvt->samples &= ~1; /* atomic size is 2 samples */

	for (i = 0; i < pvt->samples; i += 2) {
		pvt->outbuf[i/2] =
			(adpcm(tmp->inbuf[i  ], &tmp->state) << 4) |
			(adpcm(tmp->inbuf[i+1], &tmp->state)     );
	};

	f = ast_trans_frameout(pvt, pvt->samples/2, 0);

	/*
	 * If there is a left over sample, move it to the beginning
	 * of the input buffer.
	 */

	if (samples & 1) {	/* move the leftover sample at beginning */
		tmp->inbuf[0] = tmp->inbuf[samples - 1];
		pvt->samples = 1;
	}
	return f;
}

#endif /* ADPCM_IRLP */


#ifdef NEW_ASTERISK

static struct ast_translator adpcmtolin = {
	.name = "adpcmtolin",
	.srcfmt = AST_FORMAT_ADPCM,
	.dstfmt = AST_FORMAT_SLINEAR,
	.framein = adpcmtolin_framein,
	.sample = adpcm_sample,
	.desc_size = sizeof(struct adpcm_decoder_pvt),
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_translator lintoadpcm = {
	.name = "lintoadpcm",
	.srcfmt = AST_FORMAT_SLINEAR,
	.dstfmt = AST_FORMAT_ADPCM,
	.framein = lintoadpcm_framein,
	.frameout = lintoadpcm_frameout,
	.sample = slin8_sample,
	.desc_size = sizeof (struct adpcm_encoder_pvt),
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES/ 2,	/* 2 samples per byte */
};

#else /* NEW_ASTERISK */

/*! \brief AdpcmToLin_Sample */
static struct ast_frame *adpcmtolin_sample(void)
{
	static struct ast_frame f;
	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_ADPCM;
	f.datalen = sizeof(adpcm_slin_ex);
	f.samples = sizeof(adpcm_slin_ex) * 2;
	f.mallocd = 0;
	f.offset = 0;
	f.src = __PRETTY_FUNCTION__;
	f.data = adpcm_slin_ex;
	return &f;
}

/*! \brief LinToAdpcm_Sample */
static struct ast_frame *lintoadpcm_sample(void)
{
	static struct ast_frame f;
	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_SLINEAR;
	f.datalen = sizeof(slin_adpcm_ex);
	/* Assume 8000 Hz */
	f.samples = sizeof(slin_adpcm_ex) / 2;
	f.mallocd = 0;
	f.offset = 0;
	f.src = __PRETTY_FUNCTION__;
	f.data = slin_adpcm_ex;
	return &f;
}

static struct ast_translator adpcmtolin = {
	.name = "adpcmtolin",
	.srcfmt = AST_FORMAT_ADPCM,
	.dstfmt = AST_FORMAT_SLINEAR,
	.framein = adpcmtolin_framein,
	.sample = adpcmtolin_sample,
	.desc_size = sizeof(struct adpcm_decoder_pvt),
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES * 2,
	.plc_samples = 160,
};

static struct ast_translator lintoadpcm = {
	.name = "lintoadpcm",
	.srcfmt = AST_FORMAT_SLINEAR,
	.dstfmt = AST_FORMAT_ADPCM,
	.framein = lintoadpcm_framein,
	.frameout = lintoadpcm_frameout,
	.sample = lintoadpcm_sample,
	.desc_size = sizeof (struct adpcm_encoder_pvt),
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES/ 2,	/* 2 samples per byte */
};


static void parse_config(void)
{

	struct ast_config *cfg = ast_config_load("codecs.conf");
	struct ast_variable *var;
	if (cfg == NULL)
		return;
	for (var = ast_variable_browse(cfg, "plc"); var ; var = var->next) {
		if (!strcasecmp(var->name, "genericplc")) {
			adpcmtolin.useplc = ast_true(var->value) ? 1 : 0;
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "codec_adpcm: %susing generic PLC\n", adpcmtolin.useplc ? "" : "not ");
		}
	}
	ast_config_destroy(cfg);
}

#endif

/*! \brief standard module glue */
static int reload(void)
{
#ifndef NEW_ASTERISK
	parse_config();
#endif
	return 0;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_translator(&lintoadpcm);
	res |= ast_unregister_translator(&adpcmtolin);

	return res;
}

static int load_module(void)
{
	int res;

#ifndef NEW_ASTERISK
	parse_config();
#endif
	res = ast_register_translator(&adpcmtolin);
	if (!res)
		res = ast_register_translator(&lintoadpcm);
	else
		ast_unregister_translator(&adpcmtolin);

	return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Adaptive Differential PCM Coder/Decoder",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
