/*
 * SpanDSP - a series of DSP components for telephony
 *
 * echo.c - An echo cancellor, suitable for electrical and acoustic
 *	    cancellation. This code does not currently comply with
 *	    any relevant standards (e.g. G.164/5/7/8). One day....
 *
 * Written by Steve Underwood <steveu@coppice.org>
 *
 * Copyright (C) 2001 Steve Underwood
 *
 * Based on a bit from here, a bit from there, eye of toad,
 * ear of bat, etc - plus, of course, my own 2 cents.
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

/* TODO:
   Finish the echo suppressor option, however nasty suppression may be
   Add an option to reintroduce side tone at -24dB under appropriate conditions.
   Improve double talk detector (iterative!)
*/

#ifndef _ZAPTEL_SEC_H
#define _ZAPTEL_SEC_H

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/slab.h>
#define MALLOC(a) kmalloc((a), GFP_KERNEL)
#define FREE(a) kfree(a)
#else
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#define MALLOC(a) malloc(a)
#define FREE(a) free(a)
#endif

#include "fir.h"

#ifndef NULL
#define NULL 0
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE (!FALSE)
#endif

#define NONUPDATE_DWELL_TIME	600 	/* 600 samples, or 75ms */

struct echo_can_state
{
    int tx_power;
    int rx_power;
    int clean_rx_power;

    int rx_power_threshold;
    int nonupdate_dwell;

    fir16_state_t fir_state;
    int16_t *fir_taps16;	/* 16-bit version of FIR taps */
    int32_t *fir_taps32;	 /* 32-bit version of FIR taps */

    int curr_pos;
	
    int taps;
    int tap_mask;
    int use_nlp;
    int use_suppressor;
    
    int32_t supp_test1;
    int32_t supp_test2;
    int32_t supp1;
    int32_t supp2;

    int32_t latest_correction;  /* Indication of the magnitude of the latest
    				   adaption, or a code to indicate why adaption
				   was skipped, for test purposes */
};

static void echo_can_free(struct echo_can_state *ec);
static int16_t echo_can_update(struct echo_can_state *ec, int16_t tx, int16_t rx);

static void echo_can_init(void)
{
	printk("Zaptel Echo Canceller: STEVE2%s\n", ZAPTEL_ECHO_AGGRESSIVE);
}

static void echo_can_identify(char *buf, size_t len)
{
	zap_copy_string(buf, "STEVE2", len);
}

static void echo_can_shutdown(void)
{
}

/*
 * According to Jim...
 */
#define MIN_TX_POWER_FOR_ADAPTION   512
#define MIN_RX_POWER_FOR_ADAPTION   64

/*
 * According to Steve...
 */
/* #define MIN_TX_POWER_FOR_ADAPTION 4096
#define MIN_RX_POWER_FOR_ADAPTION 64 */

static int echo_can_create(struct zt_echocanparams *ecp, struct zt_echocanparam *p,
			   struct echo_can_state **ec)
{
	size_t size;

	if (ecp->param_count > 0) {
		printk(KERN_WARNING "SEC-2 echo canceler does not support parameters; failing request\n");
		return -EINVAL;
	}

	size = sizeof(**ec) + ecp->tap_length * sizeof(int32_t) + ecp->tap_length * 3 * sizeof(int16_t);
	
	if (!(*ec = MALLOC(size)))
		return -ENOMEM;
	
	memset(*ec, 0, size);

	(*ec)->taps = ecp->tap_length;
	(*ec)->curr_pos = ecp->tap_length - 1;
	(*ec)->tap_mask = ecp->tap_length - 1;
	(*ec)->fir_taps32 = (int32_t *) (*ec + sizeof(**ec));
	(*ec)->fir_taps16 = (int16_t *) (*ec + sizeof(**ec) + ecp->tap_length * sizeof(int32_t));
	/* Create FIR filter */
	fir16_create(&(*ec)->fir_state, (*ec)->fir_taps16, (*ec)->taps);
	(*ec)->rx_power_threshold = 10000000;
	(*ec)->use_suppressor = FALSE;
	/* Non-linear processor - a fancy way to say "zap small signals, to avoid
	   accumulating noise". */
	(*ec)->use_nlp = FALSE;

	return 0;
}
/*- End of function --------------------------------------------------------*/

static inline void echo_can_free(struct echo_can_state *ec)
{
	fir16_free(&ec->fir_state);
	FREE(ec);
}
/*- End of function --------------------------------------------------------*/

static inline int16_t echo_can_update(struct echo_can_state *ec, int16_t tx, int16_t rx)
{
    int offset1;
    int offset2;
    int32_t echo_value;
    int clean_rx;
    int nsuppr;
    int i;
    int correction;

    /* Evaluate the echo - i.e. apply the FIR filter */
    /* Assume the gain of the FIR does not exceed unity. Exceeding unity
       would seem like a rather poor thing for an echo cancellor to do :)
       This means we can compute the result with a total disregard for
       overflows. 16bits x 16bits -> 31bits, so no overflow can occur in
       any multiply. While accumulating we may overflow and underflow the
       32 bit scale often. However, if the gain does not exceed unity,
       everything should work itself out, and the final result will be
       OK, without any saturation logic. */
    /* Overflow is very much possible here, and we do nothing about it because
       of the compute costs */
    /* 16 bit coeffs for the LMS give lousy results (maths good, actual sound
       bad!), but 32 bit coeffs require some shifting. On balance 32 bit seems
       best */
    echo_value = fir16 (&ec->fir_state, tx);

    /* And the answer is..... */
    clean_rx = rx - echo_value;

    /* That was the easy part. Now we need to adapt! */
    if (ec->nonupdate_dwell > 0)
    	ec->nonupdate_dwell--;

    /* If there is very little being transmitted, any attempt to train is
       futile. We would either be training on the far end's noise or signal,
       the channel's own noise, or our noise. Either way, this is hardly good
       training, so don't do it (avoid trouble). */
    /* If the received power is very low, either we are sending very little or
       we are already well adapted. There is little point in trying to improve
       the adaption under these circumstanceson, so don't do it (reduce the
       compute load). */
    if (ec->tx_power > MIN_TX_POWER_FOR_ADAPTION
    	&&
	ec->rx_power > MIN_RX_POWER_FOR_ADAPTION)
    {
    	/* This is a really crude piece of decision logic, but it does OK
	   for now. */
    	if (ec->tx_power > 2*ec->rx_power)
	{
            /* There is no far-end speech detected */
            if (ec->nonupdate_dwell == 0)
	    {
	    	/* ... and we are not in the dwell time from previous speech. */
		//nsuppr = saturate((clean_rx << 16)/ec->tx_power);
		nsuppr = clean_rx >> 3;

		/* Update the FIR taps */
                offset2 = ec->curr_pos + 1;
                offset1 = ec->taps - offset2;
		ec->latest_correction = 0;
                for (i = ec->taps - 1;  i >= offset1;  i--)
		{
		    correction = ec->fir_state.history[i - offset1]*nsuppr;
                    /* Leak to avoid false training on signals with multiple
                       strong correlations. */
                    ec->fir_taps32[i] -= (ec->fir_taps32[i] >> 12);
		    ec->fir_taps32[i] += correction;
		    ec->fir_state.coeffs[i] = ec->fir_taps32[i] >> 15;
		    ec->latest_correction += abs(correction);
        	}
                for (  ;  i >= 0;  i--)
		{
		    correction = ec->fir_state.history[i + offset2]*nsuppr;
                    /* Leak to avoid false training on signals with multiple
                       strong correlations. */
                    ec->fir_taps32[i] -= (ec->fir_taps32[i] >> 12);
		    ec->fir_taps32[i] += correction;
                    ec->fir_state.coeffs[i] = ec->fir_taps32[i] >> 15;
		    ec->latest_correction += abs(correction);
    	    	}
    	    }
	    else
	    {
        	ec->latest_correction = -1;
    	    }
	}
	else
	{
            ec->nonupdate_dwell = NONUPDATE_DWELL_TIME;
    	    ec->latest_correction = -2;
	}
    }
    else
    {
        ec->nonupdate_dwell = 0;
        ec->latest_correction = -3;
    }
    /* Calculate short term power levels using very simple single pole IIRs */
    /* TODO: Is the nasty modulus approach the fastest, or would a real
       tx*tx power calculation actually be faster? */
    ec->tx_power += ((abs(tx) - ec->tx_power) >> 5);
    ec->rx_power += ((abs(rx) - ec->rx_power) >> 5);
    ec->clean_rx_power += ((abs(clean_rx) - ec->clean_rx_power) >> 5);

#if defined(XYZZY)
    if (ec->use_suppressor)
    {
    	ec->supp_test1 += (ec->fir_state.history[ec->curr_pos] - ec->fir_state.history[(ec->curr_pos - 7) & ec->tap_mask]);
    	ec->supp_test2 += (ec->fir_state.history[(ec->curr_pos - 24) & ec->tap_mask] - ec->fir_state.history[(ec->curr_pos - 31) & ec->tap_mask]);
    	if (ec->supp_test1 > 42  &&  ec->supp_test2 > 42)
    	    supp_change = 25;
    	else
    	    supp_change = 50;
    	supp = supp_change + k1*ec->supp1 + k2*ec->supp2;
	ec->supp2 = ec->supp1;
	ec->supp1 = supp;
	clean_rx *= (1 - supp);
    }
#endif

    if (ec->use_nlp  &&  ec->rx_power < 32)
    	clean_rx = 0;

    /* Roll around the rolling buffer */
    if (ec->curr_pos <= 0)
    	ec->curr_pos = ec->taps;
    ec->curr_pos--;

    return  clean_rx;
}

#if 0
static inline int16_t echo_can_update(struct echo_can_state *ec, int16_t tx, int16_t rx)
{
    int offset;
    int limit;
    int32_t echo_value;
    int clean_rx;
    int nsuppr;
    int i;
    int correction;

    ec->tx_history[ec->curr_pos] = tx;

    /* Evaluate the echo - i.e. apply the FIR filter */
    /* Assume the gain of the FIR does not exceed unity. Exceeding unity
       would seem like a rather poor thing for an echo cancellor to do :)
       This means we can compute the result with a total disregard for
       overflows. 16bits x 16bits -> 31bits, so no overflow can occur in
       any multiply. While accumulating we may overflow and underflow the
       32 bit scale often. However, if the gain does not exceed unity,
       everything should work itself out, and the final result will be
       OK, without any saturation logic. */
    /* Overflow is very much possible here, and we do nothing about it because
       of the compute costs */
    /* 16 bit coeffs for the LMS give lousy results (maths good, actual sound
       bad!), but 32 bit coeffs require some shifting. On balance 32 bit seems
       best */
    offset = ec->curr_pos;
    limit = ec->taps - offset;
    echo_value = 0;
    for (i = 0;  i < limit;  i++)
        echo_value += (ec->fir_taps[i] >> 16)*ec->tx_history[i + offset];
    offset = ec->taps - ec->curr_pos;
    for (  ;  i < ec->taps;  i++)
        echo_value += (ec->fir_taps[i] >> 16)*ec->tx_history[i - offset];
    echo_value >>= 16;

    /* And the answer is..... */
    clean_rx = rx - echo_value;

    /* That was the easy part. Now we need to adapt! */
    if (ec->nonupdate_dwell > 0)
    	ec->nonupdate_dwell--;

    /* If there is very little being transmitted, any attempt to train is
       futile. We would either be training on the far end's noise or signal,
       the channel's own noise, or our noise. Either way, this is hardly good
       training, so don't do it (avoid trouble). */
    /* If the received power is very low, either we are sending very little or
       we are already well adapted. There is little point in trying to improve
       the adaption under these circumstanceson, so don't do it (reduce the
       compute load). */
    if (ec->tx_power > MIN_TX_POWER_FOR_ADAPTION
    	&&
	ec->rx_power > MIN_RX_POWER_FOR_ADAPTION)
    {
    	/* This is a really crude piece of decision logic, but it does OK
	   for now. */
    	if (ec->tx_power > 2*ec->rx_power)
	{
            /* There is no far-end speech detected */
            if (ec->nonupdate_dwell == 0)
	    {
	    	/* ... and we are not in the dwell time from previous speech. */
		//nsuppr = saturate((clean_rx << 16)/ec->tx_power);
		nsuppr = clean_rx >> 3;

		/* Update the FIR taps */
    	        offset = ec->curr_pos;
    	    	limit = ec->taps - offset;
		ec->latest_correction = 0;
    	    	for (i = 0;  i < limit;  i++)
		{
		    correction = ec->tx_history[i + offset]*nsuppr;
		    ec->fir_taps[i] += correction;
		    //ec->latest_correction += abs(correction);
        	}
		offset = ec->taps - ec->curr_pos;
    		for (  ;  i < ec->taps;  i++)
		{
		    correction = ec->tx_history[i - offset]*nsuppr;
		    ec->fir_taps[i] += correction;
		    //ec->latest_correction += abs(correction);
    	    	}
    	    }
	    else
	    {
        	ec->latest_correction = -3;
    	    }
	}
	else
	{
            ec->nonupdate_dwell = NONUPDATE_DWELL_TIME;
    	    ec->latest_correction = -2;
	}
    }
    else
    {
        ec->nonupdate_dwell = 0;
        ec->latest_correction = -1;
    }
    /* Calculate short term power levels using very simple single pole IIRs */
    /* TODO: Is the nasty modulus approach the fastest, or would a real
       tx*tx power calculation actually be faster? */
    ec->tx_power += ((abs(tx) - ec->tx_power) >> 5);
    ec->rx_power += ((abs(rx) - ec->rx_power) >> 5);
    ec->clean_rx_power += ((abs(clean_rx) - ec->clean_rx_power) >> 5);

#if defined(XYZZY)
    if (ec->use_suppressor)
    {
    	ec->supp_test1 += (ec->tx_history[ec->curr_pos] - ec->tx_history[(ec->curr_pos - 7) & ec->tap_mask]);
    	ec->supp_test2 += (ec->tx_history[(ec->curr_pos - 24) & ec->tap_mask] - ec->tx_history[(ec->curr_pos - 31) & ec->tap_mask]);
    	if (ec->supp_test1 > 42  &&  ec->supp_test2 > 42)
    	    supp_change = 25;
    	else
    	    supp_change = 50;
    	supp = supp_change + k1*ec->supp1 + k2*ec->supp2;
	ec->supp2 = ec->supp1;
	ec->supp1 = supp;
	clean_rx *= (1 - supp);
    }
#endif

    if (ec->use_nlp  &&  ec->rx_power < 32)
    	clean_rx = 0;

    /* Roll around the rolling buffer */
    ec->curr_pos = (ec->curr_pos + 1) & ec->tap_mask;

    return clean_rx;
}
/*- End of function --------------------------------------------------------*/
#endif

static inline int echo_can_traintap(struct echo_can_state *ec, int pos, short val)
{
	/* Reset hang counter to avoid adjustments after
	   initial forced training */
	ec->nonupdate_dwell = ec->taps << 1;
	if (pos >= ec->taps)
		return 1;
	ec->fir_taps32[pos] = val << 17;
	ec->fir_taps16[pos] = val << 1;
	if (++pos >= ec->taps)
		return 1;
	return 0;
}

/*- End of file ------------------------------------------------------------*/
#endif
