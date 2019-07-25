#ifndef TARANTOOL_SYSTEMD_H_INCLUDED
#define TARANTOOL_SYSTEMD_H_INCLUDED
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

#include <stdarg.h>

#include "trivia/config.h"

/*
 * Linux supports MSG_NOSIGNAL flag for sendmsg.
 * macOS lacks it, but has SO_NOSIGPIPE for setsockopt to achieve same behaviour.
 */
#ifdef MSG_NOSIGNAL
# define SYSTEMD_MSG_NOSIGNAL MSG_NOSIGNAL
#else
# define SYSTEMD_MSG_NOSIGNAL 0
# include <sys/socket.h>
# ifdef SO_NOSIGPIPE
#  define SYSTEMD_USE_SO_NOSIGPIPE
# else
#  error "No way to block SIGPIPE in sendmsg!"
# endif
#endif

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Open connection with systemd daemon (using unix socket located in
 * "NOTIFY_SOCKET" environmnent variable)
 *
 * \return  1 on non-systemd plaformts
 * \return -1 on errors
 * \return  0 on sucess
 */
int
systemd_init();

/**
 * Close connection with systemd daemon
 */
void
systemd_free();

/**
 * Send message to systemd
 *
 * \param message message to send to systemd
 *
 * \return  0 on non-systemd platforms
 * \return -1 on errors (more information in errno)
 * \return >0 on ok
 */
int
systemd_notify(const char *message);

/**
 * send message and format it using va_list
 *
 * \param format format string
 * \param ap     arguments for formatting
 *
 * \return  0 on non-systemd platforms
 * \return -1 on errors (more information in errno)
 * \return >0 on ok
 */
int
systemd_vsnotify(const char *format, va_list ap);

/**
 * Send message and format it using varargs
 *
 * \param format format string
 * \param ...    arguments for formatting
 *
 * \return  0 on non-systemd platforms
 * \return -1 on errors (more information in errno)
 * \return >0 on ok
 */
int
systemd_snotify(const char *format, ...);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_SYSTEMD_H_INCLUDED */
