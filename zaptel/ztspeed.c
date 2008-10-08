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
 * 
 * Generic speed test -- Run an infinite loop and
 * see how high we can count (in 5 seconds).  You 
 * can use this to measure how much CPU zaptel REALLY 
 * is taking.
 * 
 * MUST BE COMPILED WITHOUT OPTIMIZATION
 *
 */

#include <stdio.h>
#include <sys/signal.h>
#include <unistd.h>
#include <stdlib.h>

static long count=0;

static void alm(int sig)
{
	printf("Count: %ld\n", count);
	exit(0);
}


int main(int argc, char *argv[])
{
	int a=0,b=0,c;
	signal(SIGALRM, alm);
	alarm(5);
	for (;;) {
		for (c=0;c<1000;c++)
			a = a * b;
		count++;
	}
}
