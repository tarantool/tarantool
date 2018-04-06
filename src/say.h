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
#include <trivia/util.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h> /* pid_t */
#include <tarantool_ev.h>
#include "small/rlist.h"
#include "fiber_cond.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

extern pid_t log_pid;

/** \cond public */

/** Log levels */
enum say_level {
	S_FATAL,		/* do not use this value directly */
	S_SYSERROR,
	S_ERROR,
	S_CRIT,
	S_WARN,
	S_INFO,
	S_VERBOSE,
	S_DEBUG
};

/** Log formats */
enum say_format {
	SF_PLAIN,
	SF_JSON,
	say_format_MAX
};

extern int log_level;

static inline bool
say_log_level_is_enabled(int level)
{
       return level <= log_level;
}

/** \endcond public */

extern enum say_format log_format;

enum say_logger_type {
	/**
	 * Before the app server core is initialized, we do not
	 * decorate output and simply print every message to
	 * stdout intact.
	 */
	SAY_LOGGER_BOOT,
	/**
	 * The core has initialized and we can decorate output
	 * with pid, thread/fiber id, time, etc.
	 */
	SAY_LOGGER_STDERR,
	/** box.cfg option to log to file. */
	SAY_LOGGER_FILE,
	/** box.cfg option to log to another process via a pipe */
	SAY_LOGGER_PIPE,
	/** box.cfg option to log to syslog. */
	SAY_LOGGER_SYSLOG
};

enum syslog_facility {
	SYSLOG_KERN = 0,
	SYSLOG_USER,
	SYSLOG_MAIL,
	SYSLOG_DAEMON,
	SYSLOG_AUTH,
	SYSLOG_INTERN,
	SYSLOG_LPR,
	SYSLOG_NEWS,
	SYSLOG_UUCP,
	SYSLOG_CLOCK,
	SYSLOG_AUTHPRIV,
	SYSLOG_FTP,
	SYSLOG_NTP,
	SYSLOG_AUDIT,
	SYSLOG_ALERT,
	SYSLOG_CRON,
	SYSLOG_LOCAL0,
	SYSLOG_LOCAL1,
	SYSLOG_LOCAL2,
	SYSLOG_LOCAL3,
	SYSLOG_LOCAL4,
	SYSLOG_LOCAL5,
	SYSLOG_LOCAL6,
	SYSLOG_LOCAL7,
	syslog_facility_MAX,
};

struct log;

typedef int (*log_format_func_t)(struct log *log, char *buf, int len, int level,
				 const char *filename, int line, const char *error,
				 const char *format, va_list ap);

/**
 * A log object. There is a singleton for the default log.
 */
struct log {
	int fd;
	/** The current log level. */
	int level;
	enum say_logger_type type;
	/** path to file if logging to file. */
	char *path;
	bool nonblock;
	log_format_func_t format_func;
	/** pid of the process if logging to pipe. */
	pid_t pid;
	/* Application identifier used to group syslog messages. */
	char *syslog_ident;
	/**
	 * Used to wake up the main logger thread from a eio thread.
	 */
	ev_async log_async;
	/**
	 * Conditional variable securing variable below
	 * from concurrent usage.
	 */
	struct fiber_cond rotate_cond;
	/** Counter identifying number of threads executing log_rotate. */
	int rotating_threads;
	enum syslog_facility syslog_facility;
	struct rlist in_log_list;
};

/**
 * Create a new log object.
 * @param log		log to initialize
 * @param init_str	box.cfg log option
 * @param nonblock	box.cfg non-block option
 * @return 0 on success, -1 on system error, the error is saved in
 * the diagnostics area
 */
int
log_create(struct log *log, const char *init_str, int nonblock);

void
log_destroy(struct log *log);

/** Perform log write. */
int
log_say(struct log *log, int level, const char *filename,
	int line, const char *error, const char *format, ...);

/**
 * Set log level. Can be used dynamically.
 *
 * @param log   log object
 * @param level level to set
 */
void
log_set_level(struct log *log, enum say_level level);

/**
 * Set log format. Can be used dynamically.
 *
 * @param log		log object
 * @param format_func	function to format log messages
 */
void
log_set_format(struct log *log, log_format_func_t format_func);

/**
 * Set log level for the default logger. Can be used dynamically.
 * @param format	constant level
 */
void
say_set_log_level(int new_level);

/**
 * Set log format for default logger. Can be used dynamically.
 *
 * Can't be applied in case syslog or boot (will be ignored)
 * @param say format
 */
void
say_set_log_format(enum say_format format);

/**
 * Return say format by name.
 *
 * @param format_name format name.
 * @retval say_format_MAX on error
 * @retval say_format otherwise
 */
enum say_format
say_format_by_name(const char *format);

struct ev_loop;
struct ev_signal;

void
say_logrotate(struct ev_loop *, struct ev_signal *, int /* revents */);

/** Init default logger. */
void
say_logger_init(const char *init_str,
		int log_level, int nonblock,
		const char *log_format,
		int background);

/** Free default logger */
void
say_logger_free();

CFORMAT(printf, 5, 0) void
vsay(int level, const char *filename, int line, const char *error,
     const char *format, va_list ap);

/** \cond public */
typedef void (*sayfunc_t)(int, const char *, int, const char *,
		    const char *, ...);

/** Internal function used to implement say() macros */
CFORMAT(printf, 5, 0) extern sayfunc_t _say;

/**
 * Format and print a message to Tarantool log file.
 *
 * \param level (int) - log level (see enum \link say_level \endlink)
 * \param file (const char * ) - file name to print
 * \param line (int) - line number to print
 * \param format (const char * ) - printf()-like format string
 * \param ... - format arguments
 * \sa printf()
 * \sa enum say_level
 */
#define say_file_line(level, file, line, format, ...) ({ \
	if (say_log_level_is_enabled(level)) \
		_say(level, file, line, format, ##__VA_ARGS__); })

/**
 * Format and print a message to Tarantool log file.
 *
 * \param level (int) - log level (see enum \link say_level \endlink)
 * \param format (const char * ) - printf()-like format string
 * \param ... - format arguments
 * \sa printf()
 * \sa enum say_level
 */
#define say(level, format, ...) ({ \
	say_file_line(level, __FILE__, __LINE__, format, ##__VA_ARGS__); })

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
#define say_verbose(format, ...) say(S_VERBOSE, NULL, format, ##__VA_ARGS__)
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
 * Format and print a message to Tarantool log file.
 *
 * \param log (struct log *) - logger object
 * \param level (int) - log level (see enum \link say_level \endlink)
 * \param format (const char * ) - printf()-like format string
 * \param ... - format arguments
 * \sa printf()
 * \sa enum say_level
 */
#define log_say_level(log, _level, format, ...) ({ 	\
	if (_level <= log->level) 			\
		log_say(log, _level, __FILE__, __LINE__,\
		format, ##__VA_ARGS__); })


/**
 * Format and print a message to specified logger.
 *
 * \param log (struct log *) - logger object
 * \param format (const char * ) - printf()-like format string
 * \param ... - format arguments
 * \sa printf()
 * \sa enum say_level
 * Example:
 * \code
 * log_say_info("Some useful information: %s", status);
 * \endcode
 */
#define log_say_error(log, format, ...) \
	log_say_level(log, S_ERROR, NULL, format, ##__VA_ARGS__)

/** \copydoc log_say_error() */
#define log_say_crit(log, format, ...) \
	log_say_level(log, S_CRIT, NULL, format, ##__VA_ARGS__)

/** \copydoc log_say_error() */
#define log_say_warn(log, format, ...) \
	log_say_level(log, S_WARN, NULL, format, ##__VA_ARGS__)

/** \copydoc log_say_error() */
#define log_say_info(log, format, ...) \
	log_say_level(log, S_INFO, NULL, format, ##__VA_ARGS__)

/** \copydoc log_say_error() */
#define log_say_verbose(log, format, ...) \
	log_say_level(log, S_VERBOSE, NULL, format, ##__VA_ARGS__)

/** \copydoc log_say_error() */
#define log_say_debug(log, format, ...) \
	log_say_level(log, S_DEBUG, NULL, format, ##__VA_ARGS__)

/** \copydoc log_say_error(). */
#define log_say_syserror(log, format, ...) \
	log_say_level(log, S_SYSERROR, strerror(errno), format, ##__VA_ARGS__)

/**
 * validates logger init string;
 * @returns 0 if validation passed or -1
 *           with an error message written to diag
 */
int
say_check_init_str(const char *str);

/* internals, for unit testing */

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
	enum syslog_facility facility;
	/* Input copy (content unspecified). */
	char *copy;
};

/**
 * Parse syslog logger init string (without the prefix)
 * @retval -1  error, message is in diag
 * @retval  0  success
 */
int
say_parse_syslog_opts(const char *init_str,
		      struct say_syslog_opts *opts);

/** Release memory allocated by the option parser. */
void
say_free_syslog_opts(struct say_syslog_opts *opts);

/**
 * Format functions
 * @param log logger structure
 * @param buf buffer where the formatted message should be written to
 * @param len size of buffer
 * @param level log level of message
 * @param filename name of file where log was called
 * @param line name of file where log was called
 * @param error error in case of system errors
 * @param format format of message
 * @param ap message parameters
 * @return number of bytes written to buf
 */
int
say_format_json(struct log *log, char *buf, int len, int level,
		const char *filename, int line, const char *error,
		const char *format, va_list ap);
int
say_format_plain(struct log *log, char *buf, int len, int level,
		 const char *filename, int line, const char *error,
		 const char *format, va_list ap);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_SAY_H_INCLUDED */
