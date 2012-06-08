#ifndef TARANTOOL_SAY_H_INCLUDED
#define TARANTOOL_SAY_H_INCLUDED
/*
 * Copyright (C) 2010 Mail.RU
 * Copyright (C) 2010 Yuriy Vostrikov
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>

#include <util.h> /* for FORMAT_PRINTF */

enum say_level {
	S_FATAL,		/* do not this value use directly */
	S_ERROR,
	S_CRIT,
	S_WARN,
	S_INFO,
	S_DEBUG
};

extern int sayfd;
extern pid_t logger_pid;

void say_logger_init(int nonblock);
void vsay(int level, const char *filename, int line, const char *error,
	  const char *format, va_list ap)
    __attribute__ ((format(FORMAT_PRINTF, 5, 0)));
void _say(int level, const char *filename, int line, const char *error,
	  const char *format, ...)
    __attribute__ ((format(FORMAT_PRINTF, 5, 6)));

#define say(level, ...) ({ _say(level, __FILE__, __LINE__, __VA_ARGS__); })

#define panic_status(status, ...)	({ say(S_FATAL, NULL, __VA_ARGS__); exit(status); })
#define panic(...)			panic_status(EXIT_FAILURE, __VA_ARGS__)
#define panic_syserror(...)		({ say(S_FATAL, strerror(errno), __VA_ARGS__); exit(EXIT_FAILURE); })
#define say_syserror(...)		say(S_ERROR, strerror(errno), __VA_ARGS__)
#define say_error(...)			say(S_ERROR, NULL, __VA_ARGS__)
#define say_crit(...)			say(S_CRIT, NULL, __VA_ARGS__)
#define say_warn(...)			say(S_WARN, NULL, __VA_ARGS__)
#define say_info(...)			say(S_INFO, NULL, __VA_ARGS__)
#define say_debug(...)			say(S_DEBUG, NULL, __VA_ARGS__)



#endif /* TARANTOOL_SAY_H_INCLUDED */
