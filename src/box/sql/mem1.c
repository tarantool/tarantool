/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 *
 * This file contains low-level memory allocation drivers for when
 * SQLite will use the standard C-library malloc/realloc/free interface
 * to obtain the memory it needs.
 *
 * This file contains implementations of the low-level memory allocation
 * routines specified in the sqlite3_mem_methods object.  The content of
 * this file is only used if SQLITE_SYSTEM_MALLOC is defined.  The
 * SQLITE_SYSTEM_MALLOC macro is defined automatically if
 * SQLITE_MEMDEBUG macro is not defined.  The
 * default configuration is to use memory allocation routines in this
 * file.
 *
 * C-preprocessor macro summary:
 *
 *    HAVE_MALLOC_USABLE_SIZE     The configure script sets this symbol if
 *                                the malloc_usable_size() interface exists
 *                                on the target platform.  Or, this symbol
 *                                can be set manually, if desired.
 *                                If an equivalent interface exists by
 *                                a different name, using a separate -D
 *                                option to rename it.
 *
 *    SQLITE_WITHOUT_ZONEMALLOC   Some older macs lack support for the zone
 *                                memory allocator.  Set this symbol to enable
 *                                building on older macs.
 *
 */
#include "sqliteInt.h"

/*
 * This version of the memory allocator is the default.  It is
 * used when no other memory allocator is specified using compile-time
 * macros.
 */
#ifdef SQLITE_SYSTEM_MALLOC
#if defined(__APPLE__) && !defined(SQLITE_WITHOUT_ZONEMALLOC)

/*
 * Use the zone allocator available on apple products unless the
 * SQLITE_WITHOUT_ZONEMALLOC symbol is defined.
 */
#include <sys/sysctl.h>
#include <malloc/malloc.h>
#include <libkern/OSAtomic.h>
static malloc_zone_t *_sqliteZone_;
#define SQLITE_MALLOC(x) malloc_zone_malloc(_sqliteZone_, (x))
#define SQLITE_FREE(x) malloc_zone_free(_sqliteZone_, (x));
#define SQLITE_REALLOC(x,y) malloc_zone_realloc(_sqliteZone_, (x), (y))
#define SQLITE_MALLOCSIZE(x) \
        (_sqliteZone_ ? _sqliteZone_->size(_sqliteZone_,x) : malloc_size(x))

#else				/* if not __APPLE__ */

/*
 * Use standard C library malloc and free on non-Apple systems.
 * Also used by Apple systems if SQLITE_WITHOUT_ZONEMALLOC is defined.
 */
#define SQLITE_MALLOC(x)             malloc(x)
#define SQLITE_FREE(x)               free(x)
#define SQLITE_REALLOC(x,y)          realloc((x),(y))

/*
 * The malloc.h header file is needed for malloc_usable_size() function
 * on some systems (e.g. Linux).
 */
#if HAVE_MALLOC_H && HAVE_MALLOC_USABLE_SIZE
#define SQLITE_USE_MALLOC_H 1
#define SQLITE_USE_MALLOC_USABLE_SIZE 1
#endif

/*
 * Include the malloc.h header file, if necessary.  Also set define macro
 * SQLITE_MALLOCSIZE to the appropriate function name, which is
 * malloc_usable_size() for most systems (e.g. Linux).
 * The memory size function can always be overridden manually by defining
 * the macro SQLITE_MALLOCSIZE to the desired function name.
 */
#if defined(SQLITE_USE_MALLOC_H)
#include <malloc.h>
#if defined(SQLITE_USE_MALLOC_USABLE_SIZE)
#if !defined(SQLITE_MALLOCSIZE)
#define SQLITE_MALLOCSIZE(x)   malloc_usable_size(x)
#endif
#elif defined(SQLITE_USE_MSIZE)
#if !defined(SQLITE_MALLOCSIZE)
#define SQLITE_MALLOCSIZE      _msize
#endif
#endif
#endif				/* defined(SQLITE_USE_MALLOC_H) */

#endif				/* __APPLE__ or not __APPLE__ */

/*
 * Like malloc(), but remember the size of the allocation
 * so that we can find it later using sqlite3MemSize().
 *
 * For this low-level routine, we are guaranteed that nByte>0 because
 * cases of nByte<=0 will be intercepted and dealt with by higher level
 * routines.
 */
static void *
sqlite3MemMalloc(int nByte)
{
#ifdef SQLITE_MALLOCSIZE
	void *p = SQLITE_MALLOC(nByte);
	if (p == 0) {
		testcase(sqlite3GlobalConfig.xLog != 0);
		sqlite3_log(SQLITE_NOMEM,
			    "failed to allocate %u bytes of memory", nByte);
	}
	return p;
#else
	sqlite3_int64 *p;
	assert(nByte > 0);
	nByte = ROUND8(nByte);
	p = SQLITE_MALLOC(nByte + 8);
	if (p) {
		p[0] = nByte;
		p++;
	} else {
		testcase(sqlite3GlobalConfig.xLog != 0);
		sqlite3_log(SQLITE_NOMEM,
			    "failed to allocate %u bytes of memory", nByte);
	}
	return (void *)p;
#endif
}

/*
 * Like free() but works for allocations obtained from sqlite3MemMalloc()
 * or sqlite3MemRealloc().
 *
 * For this low-level routine, we already know that pPrior!=0 since
 * cases where pPrior==0 will have been intecepted and dealt with
 * by higher-level routines.
 */
static void
sqlite3MemFree(void *pPrior)
{
#ifdef SQLITE_MALLOCSIZE
	SQLITE_FREE(pPrior);
#else
	sqlite3_int64 *p = (sqlite3_int64 *) pPrior;
	assert(pPrior != 0);
	p--;
	SQLITE_FREE(p);
#endif
}

/*
 * Report the allocated size of a prior return from xMalloc()
 * or xRealloc().
 */
static int
sqlite3MemSize(void *pPrior)
{
#ifdef SQLITE_MALLOCSIZE
	assert(pPrior != 0);
	return (int)SQLITE_MALLOCSIZE(pPrior);
#else
	sqlite3_int64 *p;
	assert(pPrior != 0);
	p = (sqlite3_int64 *) pPrior;
	p--;
	return (int)p[0];
#endif
}

/*
 * Like realloc().  Resize an allocation previously obtained from
 * sqlite3MemMalloc().
 *
 * For this low-level interface, we know that pPrior!=0.  Cases where
 * pPrior==0 while have been intercepted by higher-level routine and
 * redirected to xMalloc.  Similarly, we know that nByte>0 because
 * cases where nByte<=0 will have been intercepted by higher-level
 * routines and redirected to xFree.
 */
static void *
sqlite3MemRealloc(void *pPrior, int nByte)
{
#ifdef SQLITE_MALLOCSIZE
	void *p = SQLITE_REALLOC(pPrior, nByte);
	if (p == 0) {
		testcase(sqlite3GlobalConfig.xLog != 0);
		sqlite3_log(SQLITE_NOMEM,
			    "failed memory resize %u to %u bytes",
			    SQLITE_MALLOCSIZE(pPrior), nByte);
	}
	return p;
#else
	sqlite3_int64 *p = (sqlite3_int64 *) pPrior;
	assert(pPrior != 0 && nByte > 0);
	assert(nByte == ROUND8(nByte));	/* EV: R-46199-30249 */
	p--;
	p = SQLITE_REALLOC(p, nByte + 8);
	if (p) {
		p[0] = nByte;
		p++;
	} else {
		testcase(sqlite3GlobalConfig.xLog != 0);
		sqlite3_log(SQLITE_NOMEM,
			    "failed memory resize %u to %u bytes",
			    sqlite3MemSize(pPrior), nByte);
	}
	return (void *)p;
#endif
}

/*
 * Round up a request size to the next valid allocation size.
 */
static int
sqlite3MemRoundup(int n)
{
	return ROUND8(n);
}

/*
 * Initialize this module.
 */
static int
sqlite3MemInit(void *NotUsed)
{
#if defined(__APPLE__) && !defined(SQLITE_WITHOUT_ZONEMALLOC)
	int cpuCount;
	size_t len;
	if (_sqliteZone_) {
		return SQLITE_OK;
	}
	len = sizeof(cpuCount);
	/* One usually wants to use hw.acctivecpu for MT decisions, but not here */
	sysctlbyname("hw.ncpu", &cpuCount, &len, NULL, 0);
	if (cpuCount > 1) {
		/* defer MT decisions to system malloc */
		_sqliteZone_ = malloc_default_zone();
	} else {
		/* only 1 core, use our own zone to contention over global locks,
		 * e.g. we have our own dedicated locks
		 */
		malloc_zone_t *newzone = malloc_create_zone(4096, 0);
		malloc_set_zone_name(newzone, "Sqlite_Heap");
	}
#endif
	UNUSED_PARAMETER(NotUsed);
	return SQLITE_OK;
}

/*
 * Deinitialize this module.
 */
static void
sqlite3MemShutdown(void *NotUsed)
{
	UNUSED_PARAMETER(NotUsed);
	return;
}

/*
 * This routine is the only routine in this file with external linkage.
 *
 * Populate the low-level memory allocation function pointers in
 * sqlite3GlobalConfig.m with pointers to the routines in this file.
 */
void
sqlite3MemSetDefault(void)
{
	static const sqlite3_mem_methods defaultMethods = {
		sqlite3MemMalloc,
		sqlite3MemFree,
		sqlite3MemRealloc,
		sqlite3MemSize,
		sqlite3MemRoundup,
		sqlite3MemInit,
		sqlite3MemShutdown,
		0
	};
	sqlite3_config(SQLITE_CONFIG_MALLOC, &defaultMethods);
}

#endif				/* SQLITE_SYSTEM_MALLOC */
