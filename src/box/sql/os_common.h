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
 * This file contains macros and a little bit of code that is common to
 * all of the platform-specific files (os_*.c) and is #included into those
 * files.
 *
 * This file should be #included by the os_*.c files only.  It is not a
 * general purpose header file.
 */
#ifndef _OS_COMMON_H_
#define _OS_COMMON_H_

/*
 * At least two bugs have slipped in because we changed the MEMORY_DEBUG
 * macro to SQLITE_DEBUG and some older makefiles have not yet made the
 * switch.  The following code should catch this problem at compile-time.
 */
#ifdef MEMORY_DEBUG
#error "The MEMORY_DEBUG macro is obsolete.  Use SQLITE_DEBUG instead."
#endif

/*
 * Macros for performance tracing.  Normally turned off.  Only works
 * on i486 hardware.
 */
#ifdef SQLITE_PERFORMANCE_TRACE

/*
 * hwtime.h contains inline assembler code for implementing
 * high-performance timing routines.
 */
#include "hwtime.h"

static sqlite_uint64 g_start;
static sqlite_uint64 g_elapsed;
#define TIMER_START       g_start=sqlite3Hwtime()
#define TIMER_END         g_elapsed=sqlite3Hwtime()-g_start
#define TIMER_ELAPSED     g_elapsed
#else
#define TIMER_START
#define TIMER_END
#define TIMER_ELAPSED     ((sqlite_uint64)0)
#endif

/*
 * If we compile with the SQLITE_TEST macro set, then the following block
 * of code will give us the ability to simulate a disk I/O error.  This
 * is used for testing the I/O recovery logic.
 */
#if defined(SQLITE_TEST)
extern int sqlite3_io_error_hit;
extern int sqlite3_io_error_hardhit;
extern int sqlite3_io_error_pending;
extern int sqlite3_io_error_persist;
extern int sqlite3_io_error_benign;
extern int sqlite3_diskfull_pending;
extern int sqlite3_diskfull;
#define SimulateIOErrorBenign(X) sqlite3_io_error_benign=(X)
#define SimulateIOError(CODE)  \
  if( (sqlite3_io_error_persist && sqlite3_io_error_hit) \
       || sqlite3_io_error_pending-- == 1 )  \
              { local_ioerr(); CODE; }
static void
local_ioerr()
{
	IOTRACE(("IOERR\n"));
	sqlite3_io_error_hit++;
	if (!sqlite3_io_error_benign)
		sqlite3_io_error_hardhit++;
}

#define SimulateDiskfullError(CODE) \
   if( sqlite3_diskfull_pending ){ \
     if( sqlite3_diskfull_pending == 1 ){ \
       local_ioerr(); \
       sqlite3_diskfull = 1; \
       sqlite3_io_error_hit = 1; \
       CODE; \
     }else{ \
       sqlite3_diskfull_pending--; \
     } \
   }
#else
#define SimulateIOErrorBenign(X)
#define SimulateIOError(A)
#define SimulateDiskfullError(A)
#endif				/* defined(SQLITE_TEST) */

/*
 * When testing, keep a count of the number of open files.
 */
#if defined(SQLITE_TEST)
extern int sqlite3_open_file_count;
#define OpenCounter(X)  sqlite3_open_file_count+=(X)
#else
#define OpenCounter(X)
#endif				/* defined(SQLITE_TEST) */

#endif				/* !defined(_OS_COMMON_H_) */
