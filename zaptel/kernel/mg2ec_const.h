/*
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2 as published by the
 * Free Software Foundation. See the LICENSE file included with
 * this program for more details.
 */

/* 
   Important constants for tuning mg2 echo can
 */
#ifndef _MG2_CONST_H
#define _MG2_CONST_H


/* Convergence (aka. adaptation) speed -- higher means slower */
#define DEFAULT_BETA1_I 2048

/* Constants for various power computations */
#define DEFAULT_SIGMA_LY_I 7
#define DEFAULT_SIGMA_LU_I 7
#define DEFAULT_ALPHA_ST_I 5 		/* near-end speech detection sensitivity factor */
#define DEFAULT_ALPHA_YT_I 5

#define DEFAULT_CUTOFF_I 128

/* Define the near-end speech hangover counter: if near-end speech 
 *  is declared, hcntr is set equal to hangt (see pg. 432)
 */
#define DEFAULT_HANGT 600  		/* in samples, so 600 samples = 75ms */

/* define the residual error suppression threshold */
#define DEFAULT_SUPPR_I 16		/* 16 = -24db */

/* This is the minimum reference signal power estimate level 
 *  that will result in filter adaptation.
 * If this is too low then background noise will cause the filter 
 *  coefficients to constantly be updated.
 */
#define MIN_UPDATE_THRESH_I 2048

/* The number of samples used to update coefficients using the
 *  the block update method (M). It should be related back to the 
 *  length of the echo can.
 * ie. it only updates coefficients when (sample number MOD default_m) = 0
 *
 *  Getting this wrong may cause an oops. Consider yourself warned!
 */
#define DEFAULT_M 16		  	/* every 16th sample */

/* If AGGRESSIVE supression is enabled, then we start cancelling residual 
 * echos again even while there is potentially the very end of a near-side 
 *  signal present.
 * This defines how many samples of DEFAULT_HANGT can remain before we
 *  kick back in
 */
#define AGGRESSIVE_HCNTR 160		/* in samples, so 160 samples = 20ms */

/* Treat sample as error if it has a different sign as the
 * input signal and is this number larger in ABS() as
 * the input-signal */
#define MAX_SIGN_ERROR 3000

/* Number of coefficients really used for calculating the
 * simulated echo. The value specifies how many of the
 * biggest coefficients are used for calculating rs.
 * This helps on long echo-tails by artificially limiting
 * the number of coefficients for the calculation and
 * preventing overflows.
 * Comment this to deactivate the code */
#define USED_COEFFS 64

/* Backup coefficients every this number of samples */
#define BACKUP 256

/***************************************************************/
/* The following knobs are not implemented in the current code */

/* we need a dynamic level of suppression varying with the ratio of the 
   power of the echo to the power of the reference signal this is 
   done so that we have a  smoother background. 		
   we have a higher suppression when the power ratio is closer to
   suppr_ceil and reduces logarithmically as we approach suppr_floor.
 */
#define SUPPR_FLOOR -64
#define SUPPR_CEIL -24

/* in a second departure, we calculate the residual error suppression
 * as a percentage of the reference signal energy level. The threshold
 * is defined in terms of dB below the reference signal.
 */
#define RES_SUPR_FACTOR -20


#endif /* _MG2_CONST_H */

