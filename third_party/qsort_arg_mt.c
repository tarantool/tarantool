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

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#ifndef HAVE_OPENMP
#error "HAVE_OPENMP macro is not defined"
#endif

#define min(a, b)   (a) < (b) ? a : b

static char *med3(char *a, char *b, char *c,
	 int (*cmp)(const void *a, const void *b, void *arg), void *arg);
static void swapfunc(char *, char *, size_t, int);

/**
 * @brief Reduce the current number of threads in the thread pool to the
 * bare minimum. Doesn't prevent the pool from spawning new threads later
 * if demand mounts.
 */
static void
thread_pool_trim()
{
	/*
	 * Trim OpenMP thread pool.
	 * Though we lack the direct control the workaround below works for
	 * GNU OpenMP library. The library stops surplus threads on entering
	 * a parallel region. Can't go below 2 threads due to the
	 * implementation quirk.
	 */
#pragma omp parallel num_threads(2)
	;
}


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

static void
qsort_arg_mt_internal(void *a, size_t n, intptr_t es,
	     int (*cmp)(const void *a, const void *b, void *arg), void *arg)
{
	char *pa, *pb, *pc, *pd, *pl, *pm, *pn;
	intptr_t d, r, swaptype, presorted;

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
			d = (n / 8) * es;
			pl = med3(pl, pl + d, pl + 2 * d, cmp, arg);
			pm = med3(pm - d, pm, pm + d, cmp, arg);
			pn = med3(pn - 2 * d, pn - d, pn, cmp, arg);
		}
		pm = med3(pl, pm, pn, cmp, arg);
	}
	swap((char*)a, pm);
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
	r = min(pa - (char *) a, pb - pa);
	vecswap((char*)a, pb - r, r);
	r = min(pd - pc, pn - pd - es);
	vecswap(pb, pn - r, r);
	if ((r = pb - pa) > es) {
#pragma omp task
		qsort_arg_mt_internal(a, r / es, es, cmp, arg);
	}
	if ((r = pd - pc) > es)
	{
		/* Iterate rather than recurse to save stack space */
		a = pn - r;
		n = r / es;
		goto loop;
	}
}

void
qsort_arg_mt(void *a, size_t n, size_t es,
	     int (*cmp)(const void *a, const void *b, void *arg), void *arg)
{
#pragma omp parallel
	{
#pragma omp single
		qsort_arg_mt_internal(a, n, es, cmp, arg);
	}
	thread_pool_trim();
}

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */
