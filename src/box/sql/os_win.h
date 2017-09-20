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
 * This file contains code that is specific to Windows.
 */
#ifndef SQLITE_OS_WIN_H
#define SQLITE_OS_WIN_H

/*
 * Include the primary Windows SDK header file.
 */
#include "windows.h"

#ifdef __CYGWIN__
#include <sys/cygwin.h>
#include <errno.h>		/* amalgamator: dontcache */
#endif

/*
 * Determine if we are dealing with Windows NT.
 *
 * We ought to be able to determine if we are compiling for Windows 9x or
 * Windows NT using the _WIN32_WINNT macro as follows:
 *
 * #if defined(_WIN32_WINNT)
 * # define SQLITE_OS_WINNT 1
 * #else
 * # define SQLITE_OS_WINNT 0
 * #endif
 *
 * However, Visual Studio 2005 does not set _WIN32_WINNT by default, as
 * it ought to, so the above test does not work.  We'll just assume that
 * everything is Windows NT unless the programmer explicitly says otherwise
 * by setting SQLITE_OS_WINNT to 0.
 */
#if SQLITE_OS_WIN && !defined(SQLITE_OS_WINNT)
#define SQLITE_OS_WINNT 1
#endif

/*
 * Determine if we are dealing with Windows CE - which has a much reduced
 * API.
 */
#if defined(_WIN32_WCE)
#define SQLITE_OS_WINCE 1
#else
#define SQLITE_OS_WINCE 0
#endif

/*
 * Determine if we are dealing with WinRT, which provides only a subset of
 * the full Win32 API.
 */
#if !defined(SQLITE_OS_WINRT)
#define SQLITE_OS_WINRT 0
#endif

/*
 * For WinCE, some API function parameters do not appear to be declared as
 * volatile.
 */
#if SQLITE_OS_WINCE
#define SQLITE_WIN32_VOLATILE
#else
#define SQLITE_WIN32_VOLATILE volatile
#endif

/*
 * For some Windows sub-platforms, the _beginthreadex() / _endthreadex()
 * functions are not available (e.g. those not using MSVC, Cygwin, etc).
 */
#if SQLITE_OS_WIN && !SQLITE_OS_WINCE && !SQLITE_OS_WINRT && \
    SQLITE_THREADSAFE>0 && !defined(__CYGWIN__)
#define SQLITE_OS_WIN_THREADS 1
#else
#define SQLITE_OS_WIN_THREADS 0
#endif

#endif				/* SQLITE_OS_WIN_H */
