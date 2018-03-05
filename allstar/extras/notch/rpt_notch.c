/*
 *
 * app_rpt Radio Repeater / Remote Base program receive notch filter support
 * 
 * Extracted from:
 * mkfilter -- given n, compute recurrence relation
 * to implement Butterworth, Bessel or Chebyshev filter of order n
 * A.J. Fisher, University of York   <fisher@minster.york.ac.uk>
 * September 1992. May be used for any non-commercial use.
 *
*/

#include <math.h>

#define	__RPT_NOTCH

#ifndef __COMPLEX_H__
#define __COMPLEX_H__

typedef struct {
	double re, im;
} c_complex;

typedef struct {
	double re, im;
} complex;

#ifndef false
#define false 0
#endif

#ifndef true
#define true 1
#endif

#define C_HYPOT(z) hypot(z.im, z.re)
#define C_ATAN2(z) atan2(z.im, z.re)

static complex cconj(complex z) {
	z.im = -z.im;
	return z;
};

/*

inline complex operator / (complex z, double a)
  { z.re /= a; z.im /= a;
    return z;
  }

inline void operator /= (complex &z, double a)
  { z = z / a;
  }

inline complex operator - (complex z1, complex z2)
  { z1.re -= z2.re;
    z1.im -= z2.im;
    return z1;
  }

inline complex operator - (complex z)
  { return 0.0 - z;
  }

inline bool operator == (complex z1, complex z2)
  { return (z1.re == z2.re) && (z1.im == z2.im);
  }

inline complex sqr(complex z)
  { return z*z;
  }

*/

#endif/*__COMPLEX_H__*/
/* mkfilter -- given n, compute recurrence relation
   to implement Butterworth, Bessel or Chebyshev filter of order n
   A.J. Fisher, University of York   <fisher@minster.york.ac.uk>
   September 1992 */

#include <string.h>

/* Header file */

#define unless(x)   if(!(x))
#define until(x)    while(!(x))

#define VERSION	    "4.6"
#undef	PI
#define PI	    3.14159265358979323846  /* Microsoft C++ does not define M_PI ! */
#ifndef	TWOPI
#define TWOPI	    (2.0 * PI)
#endif
#define EPS	    1e-10
#define MAXORDER    10
#define MAXPZ	    512	    /* .ge. 2*MAXORDER, to allow for doubling of poles in BP filter;
			       high values needed for FIR filters */
#define MAXSTRING   256

typedef int bool;

/* typedef void (*proc)(); */
typedef unsigned int uint;

extern const char *progname;

extern void readdata(char*, double *, int *, double*, int *, double*);

/* mkfilter -- given n, compute recurrence relation
   to implement Butterworth, Bessel or Chebyshev filter of order n
   A.J. Fisher, University of York   <fisher@minster.york.ac.uk>
   September 1992 */

/* Routines for complex arithmetic */

#include <math.h>

static complex eval(complex[], int, complex);


static void complex_init(complex *c, double r, double i) {
	c->re = r;
	c->im = i;
	return;
}

/* New C Functions */
static complex complex_new(double r, double i) {
	complex z;
	complex_init(&z, r, i);
	return z;
}

static complex make_complex(double a, double b) {
	complex c;
	c.re = a;
	c.im = b;
	return c;
}

static complex complex_num_inv(complex c) {
	return make_complex(-c.re, -c.im);
}

static complex
expj(double theta) {
	return complex_new(cos(theta), sin(theta));
}


static complex
complex_mul_dbl(complex c, double d) {
	c.re *= d;
	c.im *= d;
	return c;
}

static complex
complex_mul_cplx(complex a, complex b) {
	return complex_new((a.re * b.re) - (a.im * b.im),
			   (a.re * b.im) + (a.im * b.re));
}

static complex
complex_add_cplx(complex a, complex b) {
	a.re += b.re;
	a.im += b.im;
	return a;
}

static complex
complex_div_cplx(complex a, complex b) {
	double mag = (b.re * b.re) + (b.im * b.im);
	return complex_new(((a.re * b.re) + (a.im * b.im)) / mag,
			   ((a.im * b.re) - (a.re * b.im)) / mag);
}

static complex
eval(complex coeffs[], int npz, complex z) {
	/* evaluate polynomial in z, substituting for z */
	int i;
	complex sum = complex_new(0.0, 0.0);
	for (i = npz; i >= 0; i--)
		sum = complex_add_cplx(complex_mul_cplx(sum, z), coeffs[i]);
	return sum;
}



static complex
evaluate(complex topco[], int nz, complex botco[], int np, complex z) {
	/* evaluate response, substituting for z */
	return complex_div_cplx(eval(topco, nz, z), eval(botco, np, z));
}

/* mknotch -- Make IIR notch filter parameters, based upon mkfilter;
   A.J. Fisher, University of York   <fisher@minster.york.ac.uk>
   September 1992 */

#include <stdio.h>
#include <math.h>
#include <string.h>

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

typedef struct {
	complex poles[MAXPZ], zeros[MAXPZ];
	int numpoles, numzeros;
} pzrep;

static pzrep zplane;
static double raw_alpha1, raw_alpha2;
static complex dc_gain, fc_gain, hf_gain;
static uint options;
static double qfactor;
static bool infq;
static uint polemask;
static double xcoeffs[MAXPZ+1], ycoeffs[MAXPZ+1];

static void expand(complex pz[], int npz, complex coeffs[]) ;

static void compute_bpres(void)
{
	double theta = TWOPI * raw_alpha1; /* where we want the peak to be */
	/* compute Z-plane pole & zero positions for bandpass resonator */
	zplane.numpoles = zplane.numzeros = 2;
	complex_init(&zplane.zeros[0], 1.0, 0.0);
	complex_init(&zplane.zeros[1], -1.0, 0.0);
	
	if (infq) { /* oscillator */
		complex zp = expj(theta);
		zplane.poles[0] = zp; zplane.poles[1] = cconj(zp);
	} else { /* must iterate to find exact pole positions */
		complex topcoeffs[MAXPZ+1];
		double r = exp(-theta / (2.0 * qfactor));
		double thm = theta, th1 = 0.0, th2 = PI;
		bool cvg = false;
		int i;
		expand(zplane.zeros, zplane.numzeros, topcoeffs);
		for (i=0; i < 50 && !cvg; i++) {
			complex zp,botcoeffs[MAXPZ+1],g;
			double phi;
			zp = complex_mul_dbl(expj(thm), r);
			zplane.poles[0] = zp; zplane.poles[1] = cconj(zp);
			expand(zplane.poles, zplane.numpoles, botcoeffs);
			g = evaluate(topcoeffs, zplane.numzeros, botcoeffs, zplane.numpoles, expj(theta));
			phi = g.im / g.re; /* approx to atan2 */
			if (phi > 0.0)
				th2 = thm;
			else
				th1 = thm;
			if (fabs(phi) < EPS)
				cvg = true;
			thm = 0.5 * (th1+th2);
		}
		if (!cvg)
			fprintf(stderr, "mkfilter: warning: failed to converge\n");
	}
}

static void compute_notch(void)
{
	/* compute Z-plane pole & zero positions for bandstop resonator (notch filter) */
	double theta;
	complex zz;
	compute_bpres();		/* iterate to place poles */
	theta = TWOPI * raw_alpha1;
	zz = expj(theta);	/* place zeros exactly */
	zplane.zeros[0] = zz; zplane.zeros[1] = cconj(zz);
}

static void expandpoly(void)
{
	/* given Z-plane poles & zeros, compute top & bot polynomials in Z, and
	 * then recurrence relation */
	complex topcoeffs[MAXPZ+1], botcoeffs[MAXPZ+1]; int i;
	double theta;
	
	expand(zplane.zeros, zplane.numzeros, topcoeffs);
	expand(zplane.poles, zplane.numpoles, botcoeffs);
	
	dc_gain = evaluate(topcoeffs, zplane.numzeros, botcoeffs, zplane.numpoles, make_complex(1.0, 0.0));

	theta = TWOPI * 0.5 * (raw_alpha1 + raw_alpha2); /* "jwT" for centre freq. */

	fc_gain = evaluate(topcoeffs, zplane.numzeros, botcoeffs, zplane.numpoles,
			   expj(theta));
	hf_gain = evaluate(topcoeffs, zplane.numzeros, botcoeffs, zplane.numpoles, make_complex(-1.0, 0.0));

	for (i = 0; i <= zplane.numzeros; i++)
		xcoeffs[i] = +(topcoeffs[i].re / botcoeffs[zplane.numpoles].re);
	for (i = 0; i <= zplane.numpoles; i++)
		ycoeffs[i] = -(botcoeffs[i].re / botcoeffs[zplane.numpoles].re);
}


static void multin(complex w, int npz, complex coeffs[]) {
	/* multiply factor (z-w) into coeffs */
	complex nw = complex_num_inv(w);
	int i;
	for (i = npz; i >= 1; i--)
		coeffs[i] = complex_add_cplx(complex_mul_cplx(nw, coeffs[i]), coeffs[i-1]);

	coeffs[0] = complex_mul_cplx(nw, coeffs[0]);
}

static void expand(complex pz[], int npz, complex coeffs[]) {
	/* compute product of poles or zeros as a polynomial of z */
	int i;
	complex_init(&coeffs[0], 1.0, 0.0);

	for (i=0; i < npz; i++)
		complex_init(&coeffs[i+1], 0.0, 0.0);

	for (i=0; i < npz; i++)
		multin(pz[i], npz, coeffs);

	/* check computed coeffs of z^k are all real */
	for (i=0; i < npz+1; i++) {
		if (fabs(coeffs[i].im) > EPS) {
			fprintf(stderr, "mkfilter: coeff of z^%d is not real; poles/zeros are not complex conjugates\n", i);
			return;
		}
	}
}

static void rpt_mknotch(float freq,float bw,float *g, float *p1, float *p2, float *p3)
{

    options = opt_re;
    qfactor = freq / bw;
    infq = false;
    raw_alpha1 = freq / 8000.0;
    polemask = ~0;

    compute_notch();
    expandpoly();

    *g = C_HYPOT(dc_gain);
    *p1 = xcoeffs[1];
    *p2 = ycoeffs[0];
    *p3 = ycoeffs[1];
}




