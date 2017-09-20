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
 * This file contains pre-processor directives related to operating system
 * detection and/or setup.
 */
#ifndef SQLITE_OS_SETUP_H
#define SQLITE_OS_SETUP_H

/*
 * Figure out if we are dealing with Unix, Windows, or some other operating
 * system.
 *
 * After the following block of preprocess macros, all of SQLITE_OS_UNIX,
 * SQLITE_OS_WIN, and SQLITE_OS_OTHER will defined to either 1 or 0.  One of
 * the three will be 1.  The other two will be 0.
 */
#if defined(SQLITE_OS_OTHER)
#if SQLITE_OS_OTHER==1
#undef SQLITE_OS_UNIX
#define SQLITE_OS_UNIX 0
#undef SQLITE_OS_WIN
#define SQLITE_OS_WIN 0
#else
#undef SQLITE_OS_OTHER
#endif
#endif
#if !defined(SQLITE_OS_UNIX) && !defined(SQLITE_OS_OTHER)
#define SQLITE_OS_OTHER 0
#ifndef SQLITE_OS_WIN
#if defined(_WIN32) || defined(WIN32) || defined(__CYGWIN__) || \
        defined(__MINGW32__) || defined(__BORLANDC__)
#define SQLITE_OS_WIN 1
#define SQLITE_OS_UNIX 0
#else
#define SQLITE_OS_WIN 0
#define SQLITE_OS_UNIX 1
#endif
#else
#define SQLITE_OS_UNIX 0
#endif
#else
#ifndef SQLITE_OS_WIN
#define SQLITE_OS_WIN 0
#endif
#endif

#endif				/* SQLITE_OS_SETUP_H */
