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
 * This file contains a no-op memory allocation drivers for use when
 * SQLITE_ZERO_MALLOC is defined.  The allocation drivers implemented
 * here always fail.  SQLite will not operate with these drivers.  These
 * are merely placeholders.  Real drivers must be substituted using
 * sqlite3_config() before SQLite will operate.
 */
#include "sqliteInt.h"

/*
 * This version of the memory allocator is the default.  It is
 * used when no other memory allocator is specified using compile-time
 * macros.
 */
#ifdef SQLITE_ZERO_MALLOC

/*
 * No-op versions of all memory allocation routines
 */
static void *
sqlite3MemMalloc(int nByte)
{
	return 0;
}

static void
sqlite3MemFree(void *pPrior)
{
	return;
}

static void *
sqlite3MemRealloc(void *pPrior, int nByte)
{
	return 0;
}

static int
sqlite3MemSize(void *pPrior)
{
	return 0;
}

static int
sqlite3MemRoundup(int n)
{
	return n;
}

static int
sqlite3MemInit(void *NotUsed)
{
	return SQLITE_OK;
}

static void
sqlite3MemShutdown(void *NotUsed)
{
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

#endif				/* SQLITE_ZERO_MALLOC */
