/*
 * SpanDSP - a series of DSP components for telephony
 *
 * fir.h - General telephony FIR routines
 *
 * Written by Steve Underwood <steveu@coppice.org>
 *
 * Copyright (C) 2002 Steve Underwood
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#if !defined(_FIR_H_)
#define _FIR_H_

typedef struct
{
    int taps;
    int curr_pos;
    int16_t *coeffs;
    int16_t *history;
} fir16_state_t;

typedef struct
{
    int taps;
    int curr_pos;
    int32_t *coeffs;
    int16_t *history;
} fir32_state_t;

static inline void fir16_create (fir16_state_t *fir,
			         int16_t *coeffs,
    	    	    	         int taps)
{
    fir->taps = taps;
    fir->curr_pos = taps - 1;
    fir->coeffs = coeffs;
    fir->history = MALLOC (taps*sizeof (int16_t));
    if (fir->history)
        memset (fir->history, '\0', taps*sizeof (int16_t));
}
/*- End of function --------------------------------------------------------*/
    
static inline void fir16_free (fir16_state_t *fir)
{
    FREE (fir->history);
}
/*- End of function --------------------------------------------------------*/
    
static inline int16_t fir16 (fir16_state_t *fir, int16_t sample)
{
    int i;
    int offset1;
    int offset2;
    int32_t y;

    fir->history[fir->curr_pos] = sample;
    offset2 = fir->curr_pos + 1;
    offset1 = fir->taps - offset2;
    y = 0;
    for (i = fir->taps - 1;  i >= offset1;  i--)
        y += fir->coeffs[i]*fir->history[i - offset1];
    for (  ;  i >= 0;  i--)
        y += fir->coeffs[i]*fir->history[i + offset2];
    if (fir->curr_pos <= 0)
    	fir->curr_pos = fir->taps;
    fir->curr_pos--;
    return  y >> 15;
}
/*- End of function --------------------------------------------------------*/

static inline void fir32_create (fir32_state_t *fir,
			         int32_t *coeffs,
    	    	    	         int taps)
{
    fir->taps = taps;
    fir->curr_pos = taps - 1;
    fir->coeffs = coeffs;
    fir->history = MALLOC (taps*sizeof (int16_t));
    if (fir->history)
    	memset (fir->history, '\0', taps*sizeof (int16_t));
}
/*- End of function --------------------------------------------------------*/
    
static inline void fir32_free (fir32_state_t *fir)
{
    FREE (fir->history);
}
/*- End of function --------------------------------------------------------*/
    
static inline int16_t fir32 (fir32_state_t *fir, int16_t sample)
{
    int i;
    int offset1;
    int offset2;
    int32_t y;

    fir->history[fir->curr_pos] = sample;
    offset2 = fir->curr_pos + 1;
    offset1 = fir->taps - offset2;
    y = 0;
    for (i = fir->taps - 1;  i >= offset1;  i--)
        y += fir->coeffs[i]*fir->history[i - offset1];
    for (  ;  i >= 0;  i--)
        y += fir->coeffs[i]*fir->history[i + offset2];
    if (fir->curr_pos <= 0)
    	fir->curr_pos = fir->taps;
    fir->curr_pos--;
    return  y >> 15;
}
/*- End of function --------------------------------------------------------*/

#endif
/*- End of file ------------------------------------------------------------*/
