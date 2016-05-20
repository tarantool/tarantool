#ifndef TARANTOOL_SAY_H_INCLUDED
#define TARANTOOL_SAY_H_INCLUDED
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h> /* pid_t */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** \cond public */

/** Log levels */
enum say_level {
	S_FATAL,		/* do not this value use directly */
	S_SYSERROR,
	S_ERROR,
	S_CRIT,
	S_WARN,
	S_INFO,
	S_DEBUG
};

/** \endcond public */

extern pid_t logger_pid;

void
say_logrotate(int /* signo */);

void
say_set_log_level(int new_level);

/** Basic init. */
void say_init(const char *argv0);

/* Init logger. */
void say_logger_init(const char *init_str,
                     int log_level, int nonblock, int background);

void vsay(int level, const char *filename, int line, const char *error,
          const char *format, va_list ap)
          __attribute__ ((format(printf, 5, 0)));

/** \cond public */
typedef void (*sayfunc_t)(int, const char *, int, const char *,
		    const char *, ...);

/** Internal function used to implement say() macros */
extern sayfunc_t _say __attribute__ ((format(printf, 5, 6)));

/**
 * Format and print a message to Tarantool log file.
 *
 * \param level (int) - log level (see enum \link say_level \endlink)
 * \param format (const char * ) - printf()-like format string
 * \param ... - format arguments
 * \sa printf()
 * \sa enum say_level
 */
#define say(level, format, ...) ({ _say(level, __FILE__, __LINE__, format, \
	##__VA_ARGS__); })

/**
 * Format and print a message to Tarantool log file.
 *
 * \param format (const char * ) - printf()-like format string
 * \param ... - format arguments
 * \sa printf()
 * \sa enum say_level
 * Example:
 * \code
 * say_info("Some useful information: %s", status);
 * \endcode
 */
#define say_error(format, ...) say(S_ERROR, NULL, format, ##__VA_ARGS__)
/** \copydoc say_error() */
#define say_crit(format, ...) say(S_CRIT, NULL, format, ##__VA_ARGS__)
/** \copydoc say_error() */
#define say_warn(format, ...) say(S_WARN, NULL, format, ##__VA_ARGS__)
/** \copydoc say_error() */
#define say_info(format, ...) say(S_INFO, NULL, format, ##__VA_ARGS__)
/** \copydoc say_error() */
#define say_debug(format, ...) say(S_DEBUG, NULL, format, ##__VA_ARGS__)
/** \copydoc say_error(). */
#define say_syserror(format, ...) say(S_SYSERROR, strerror(errno), format, \
	##__VA_ARGS__)
/** \endcond public */

#define panic_status(status, ...)	({ say(S_FATAL, NULL, __VA_ARGS__); exit(status); })
#define panic(...)			panic_status(EXIT_FAILURE, __VA_ARGS__)
#define panic_syserror(...)		({ say(S_FATAL, strerror(errno), __VA_ARGS__); exit(EXIT_FAILURE); })

/**
 * validates logger init string;
 * @param[out] error - a malloc-ed error message
 * @returns 0 if validation passed or -1
 *           with an error message
 */
int
say_check_init_str(const char *str, char **error);

/* internals, for unit testing */

enum say_logger_type {
	SAY_LOGGER_STDERR,
	SAY_LOGGER_FILE,
	SAY_LOGGER_PIPE,
	SAY_LOGGER_SYSLOG
};

/**
 * Determine logger type and strip type prefix from init_str.
 *
 * @return	-1 on error, 0 on success
 */
int
say_parse_logger_type(const char **str, enum say_logger_type *type);

/** Syslog logger initialization params */
struct say_syslog_opts {
	const char *identity;
	const char *facility;
	/* Input copy (content unspecified). */
	char *copy;
};

/**
 * Parse syslog logger init string (without the prefix)
 * @retval -1  error, message is malloc-ed
 * @retval  0  success
 */
int
say_parse_syslog_opts(const char *init_str,
		      struct say_syslog_opts *opts,
		      char **error);

/** Release memory allocated by the option parser. */
void
say_free_syslog_opts(struct say_syslog_opts *opts);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_SAY_H_INCLUDED */
