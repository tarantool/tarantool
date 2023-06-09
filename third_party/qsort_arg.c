/*
 * Imported from PostgreSQL sources by Teodor Sigaev <teodor@sigaev.ru>, <sigaev@corp.mail.ru>
 */

/*
 *	qsort_arg.c: qsort with a passthrough "void *" argument
 *
 *	Modifications from vanilla NetBSD source:
 *	  Add do ... while() macro fix
 *	  Remove __inline, _DIAGASSERTs, __P
 *	  Remove ill-considered "swap_cnt" switch to insertion sort,
 *	  in favor of a simple check for presorted input.
 *
 *	CAUTION: if you change this file, see also qsort.c
 *
 *	$PostgreSQL: pgsql/src/port/qsort_arg.c,v 1.4 2007/03/18 05:36:50 neilc Exp $
 */

/*	$NetBSD: qsort.c,v 1.13 2003/08/07 16:43:42 agc Exp $	*/

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	  notice, this list of conditions and the following disclaimer in the
 *	  documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *	  may be used to endorse or promote products derived from this software
 *	  without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.	IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <qsort_arg.h>
#include <stdint.h>

#define min(a, b)   (a) < (b) ? a : b

static char *med3(char *a, char *b, char *c,
	 int (*cmp)(const void *a, const void *b, void *arg), void *arg);
static void swapfunc(char *, char *, size_t, int);

/*
 * Qsort routine based on J. L. Bentley and M. D. McIlroy,
 * "Engineering a sort function",
 * Software--Practice and Experience 23 (1993) 1249-1265.
 * We have modified their original by adding a check for already-sorted input,
 * which seems to be a win per discussions on pgsql-hackers around 2006-03-21.
 */
#define swapcode(TYPE, parmi, parmj, n) \
do {		\
	size_t i = (n) / sizeof (TYPE);			\
	TYPE *pi = (TYPE *)(void *)(parmi);			\
	TYPE *pj = (TYPE *)(void *)(parmj);			\
	do {						\
		TYPE	t = *pi;			\
		*pi++ = *pj;				\
		*pj++ = t;				\
		} while (--i > 0);				\
} while (0)

#define SWAPINIT(a, es) swaptype = ((char *)(a) - (char *)0) % sizeof(long) || \
	(es) % sizeof(long) ? 2 : (es) == sizeof(long)? 0 : 1;

static void
swapfunc(char *a, char *b, size_t n, int swaptype)
{
	if (swaptype <= 1)
		swapcode(long, a, b, n);
	else
		swapcode(char, a, b, n);
}

#define swap(a, b)						\
	if (swaptype == 0) {					\
		long t = *(long *)(void *)(a);			\
		*(long *)(void *)(a) = *(long *)(void *)(b);	\
		*(long *)(void *)(b) = t;			\
	} else							\
		swapfunc(a, b, es, swaptype)

#define vecswap(a, b, n) if ((n) > 0) swapfunc((a), (b), (size_t)(n), swaptype)

static char *
med3(char *a, char *b, char *c, int (*cmp)(const void *a, const void *b, void *arg), void *arg)
{
	return cmp(a, b, arg) < 0 ?
		(cmp(b, c, arg) < 0 ? b : (cmp(a, c, arg) < 0 ? c : a))
		: (cmp(b, c, arg) > 0 ? b : (cmp(a, c, arg) < 0 ? a : c));
}

/**
 * Single-thread version of qsort.
 */
void
qsort_arg(void *a, size_t n, size_t es, int (*cmp)(const void *a, const void *b, void *arg), void *arg)
{
	char	   *pa,
			   *pb,
			   *pc,
			   *pd,
			   *pl,
			   *pm,
			   *pn;
	size_t			d1,
				d2;
	intptr_t		r,
				swaptype,
				presorted;

loop:SWAPINIT(a, es);
	if (n < 7)
	{
		for (pm = (char *) a + es; pm < (char *) a + n * es; pm += es)
			for (pl = pm; pl > (char *) a && cmp(pl - es, pl, arg) > 0;
				 pl -= es)
				swap(pl, pl - es);
		return;
	}
	presorted = 1;
	for (pm = (char *) a + es; pm < (char *) a + n * es; pm += es)
	{
		if (cmp(pm - es, pm, arg) > 0)
		{
			presorted = 0;
			break;
		}
	}
	if (presorted)
		return;
	pm = (char *) a + (n / 2) * es;
	if (n > 7)
	{
		pl = (char *) a;
		pn = (char *) a + (n - 1) * es;
		if (n > 40)
		{
			size_t d = (n / 8) * es;
			pl = med3(pl, pl + d, pl + 2 * d, cmp, arg);
			pm = med3(pm - d, pm, pm + d, cmp, arg);
			pn = med3(pn - 2 * d, pn - d, pn, cmp, arg);
		}
		pm = med3(pl, pm, pn, cmp, arg);
	}
	swap(a, pm);
	pa = pb = (char *) a + es;
	pc = pd = (char *) a + (n - 1) * es;
	for (;;)
	{
		while (pb <= pc && (r = cmp(pb, a, arg)) <= 0)
		{
			if (r == 0)
			{
				swap(pa, pb);
				pa += es;
			}
			pb += es;
		}
		while (pb <= pc && (r = cmp(pc, a, arg)) >= 0)
		{
			if (r == 0)
			{
				swap(pc, pd);
				pd -= es;
			}
			pc -= es;
		}
		if (pb > pc)
			break;
		swap(pb, pc);
		pb += es;
		pc -= es;
	}
	pn = (char *) a + n * es;
	d1 = min(pa - (char *) a, pb - pa);
	vecswap(a, pb - d1, d1);
	d1 = min(pd - pc, pn - pd - es);
	vecswap(pb, pn - d1, d1);
	d1 = pb - pa;
	d2 = pd - pc;
	if (d1 <= d2)
	{
		/* Recurse on left partition, then iterate on right partition */
		if (d1 > es)
			qsort_arg(a, d1 / es, es, cmp, arg);
		if (d2 > es)
		{
			/* Iterate rather than recurse to save stack space */
			/* qsort(pn - d2, d2 / es, es, cmp); */
			a = pn - d2;
			n = d2 / es;
			goto loop;
		}
	}
	else
	{
		/* Recurse on right partition, then iterate on left partition */
		if (d2 > es)
			qsort_arg(pn - d2, d2 / es, es, cmp, arg);
		if (d1 > es)
		{
			/* Iterate rather than recurse to save stack space */
			/* qsort(a, d1 / es, es, cmp); */
			n = d1 / es;
			goto loop;
		}
	}
}
