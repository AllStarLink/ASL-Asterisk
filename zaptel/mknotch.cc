/* mknotch -- Make IIR notch filter parameters, based upon mkfilter;
   A.J. Fisher, University of York   <fisher@minster.york.ac.uk>
   September 1992 */

#include <stdio.h>
#include <math.h>
#include <string.h>

#include "mkfilter.h"
#include "complex.h"

#define opt_be 0x00001	/* -Be		Bessel characteristic	       */
#define opt_bu 0x00002	/* -Bu		Butterworth characteristic     */
#define opt_ch 0x00004	/* -Ch		Chebyshev characteristic       */
#define opt_re 0x00008	/* -Re		Resonator		       */
#define opt_pi 0x00010	/* -Pi		proportional-integral	       */

#define opt_lp 0x00020	/* -Lp		lowpass			       */
#define opt_hp 0x00040	/* -Hp		highpass		       */
#define opt_bp 0x00080	/* -Bp		bandpass		       */
#define opt_bs 0x00100	/* -Bs		bandstop		       */
#define opt_ap 0x00200	/* -Ap		allpass			       */

#define opt_a  0x00400	/* -a		alpha value		       */
#define opt_l  0x00800	/* -l		just list filter parameters    */
#define opt_o  0x01000	/* -o		order of filter		       */
#define opt_p  0x02000	/* -p		specified poles only	       */
#define opt_w  0x04000	/* -w		don't pre-warp		       */
#define opt_z  0x08000	/* -z		use matched z-transform	       */
#define opt_Z  0x10000	/* -Z		additional zero		       */

struct pzrep
  { complex poles[MAXPZ], zeros[MAXPZ];
    int numpoles, numzeros;
  };

static pzrep splane, zplane;
static double raw_alpha1, raw_alpha2;
static complex dc_gain, fc_gain, hf_gain;
static uint options;
static double qfactor;
static bool infq;
static uint polemask;
static double xcoeffs[MAXPZ+1], ycoeffs[MAXPZ+1];

static void compute_notch();
static void expandpoly(), expand(complex[], int, complex[]), multin(complex, int, complex[]);

static void compute_bpres()
  { /* compute Z-plane pole & zero positions for bandpass resonator */
    zplane.numpoles = zplane.numzeros = 2;
    zplane.zeros[0] = 1.0; zplane.zeros[1] = -1.0;
    double theta = TWOPI * raw_alpha1; /* where we want the peak to be */
    if (infq)
      { /* oscillator */
	complex zp = expj(theta);
	zplane.poles[0] = zp; zplane.poles[1] = cconj(zp);
      }
    else
      { /* must iterate to find exact pole positions */
	complex topcoeffs[MAXPZ+1]; expand(zplane.zeros, zplane.numzeros, topcoeffs);
	double r = exp(-theta / (2.0 * qfactor));
	double thm = theta, th1 = 0.0, th2 = PI;
	bool cvg = false;
	for (int i=0; i < 50 && !cvg; i++)
	  { complex zp = r * expj(thm);
	    zplane.poles[0] = zp; zplane.poles[1] = cconj(zp);
	    complex botcoeffs[MAXPZ+1]; expand(zplane.poles, zplane.numpoles, botcoeffs);
	    complex g = evaluate(topcoeffs, zplane.numzeros, botcoeffs, zplane.numpoles, expj(theta));
	    double phi = g.im / g.re; /* approx to atan2 */
	    if (phi > 0.0) th2 = thm; else th1 = thm;
	    if (fabs(phi) < EPS) cvg = true;
	    thm = 0.5 * (th1+th2);
	  }
	unless (cvg) fprintf(stderr, "mkfilter: warning: failed to converge\n");
      }
  }

static void compute_notch()
  { /* compute Z-plane pole & zero positions for bandstop resonator (notch filter) */
    compute_bpres();		/* iterate to place poles */
    double theta = TWOPI * raw_alpha1;
    complex zz = expj(theta);	/* place zeros exactly */
    zplane.zeros[0] = zz; zplane.zeros[1] = cconj(zz);
  }

static void expandpoly() /* given Z-plane poles & zeros, compute top & bot polynomials in Z, and then recurrence relation */
  { complex topcoeffs[MAXPZ+1], botcoeffs[MAXPZ+1]; int i;
    expand(zplane.zeros, zplane.numzeros, topcoeffs);
    expand(zplane.poles, zplane.numpoles, botcoeffs);
    dc_gain = evaluate(topcoeffs, zplane.numzeros, botcoeffs, zplane.numpoles, 1.0);
    double theta = TWOPI * 0.5 * (raw_alpha1 + raw_alpha2); /* "jwT" for centre freq. */
    fc_gain = evaluate(topcoeffs, zplane.numzeros, botcoeffs, zplane.numpoles, expj(theta));
    hf_gain = evaluate(topcoeffs, zplane.numzeros, botcoeffs, zplane.numpoles, -1.0);
    for (i = 0; i <= zplane.numzeros; i++) xcoeffs[i] = +(topcoeffs[i].re / botcoeffs[zplane.numpoles].re);
    for (i = 0; i <= zplane.numpoles; i++) ycoeffs[i] = -(botcoeffs[i].re / botcoeffs[zplane.numpoles].re);
  }


static void expand(complex pz[], int npz, complex coeffs[])
  { /* compute product of poles or zeros as a polynomial of z */
    int i;
    coeffs[0] = 1.0;
    for (i=0; i < npz; i++) coeffs[i+1] = 0.0;
    for (i=0; i < npz; i++) multin(pz[i], npz, coeffs);
    /* check computed coeffs of z^k are all real */
    for (i=0; i < npz+1; i++)
      { if (fabs(coeffs[i].im) > EPS)
	  { fprintf(stderr, "mkfilter: coeff of z^%d is not real; poles/zeros are not complex conjugates\n", i);
	    exit(1);
	  }
      }
  }

static void multin(complex w, int npz, complex coeffs[])
  { /* multiply factor (z-w) into coeffs */
    complex nw = -w;
    for (int i = npz; i >= 1; i--) coeffs[i] = (nw * coeffs[i]) + coeffs[i-1];
    coeffs[0] = nw * coeffs[0];
  }

extern "C" void mknotch(float freq,float bw,long *p1, long *p2, long *p3);

void mknotch(float freq,float bw,long *p1, long *p2, long *p3)
{
#define	NB 14

    options = opt_re;
    qfactor = freq / bw;
    infq = false;
    raw_alpha1 = freq / 8000.0;
    polemask = ~0;

    compute_notch();
    expandpoly();

    float fsh = (float) (1 << NB);
    *p1 = (long)(xcoeffs[1] * fsh);
    *p2 = (long)(ycoeffs[0] * fsh);
    *p3 = (long)(ycoeffs[1] * fsh);
}




