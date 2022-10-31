#ifndef TARANTOOL_LIB_CORE_SAY_H_INCLUDED
#define TARANTOOL_LIB_CORE_SAY_H_INCLUDED
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
#include "ratelimit.h"
#include "tt_strerror.h"

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

/**
 * This function is called for every log which log level is not less than
 * log_level_flightrec.
 */
extern void
(*log_write_flightrec)(int level, const char *filename, int line,
		       const char *error, const char *format, va_list ap);

static inline bool
say_log_level_is_enabled(int level)
{
       return level <= log_level;
}

/** \endcond public */

extern enum say_format log_format;

enum say_syslog_server_type {
	SAY_SYSLOG_DEFAULT,
	SAY_SYSLOG_UNIX,
	SAY_SYSLOG_REMOTE
};

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
	/** The current file descriptor. */
	int fd;
	/** The current log level. */
	int level;
	enum say_logger_type type;
	/* Type of syslog destination. */
	enum say_syslog_server_type syslog_server_type;
	/** 
	 * Path to file if logging to file, socket
	 * or server address in case of syslog.
	 */
	char *path;
	bool nonblock;
	log_format_func_t format_func;
	/** pid of the process if logging to pipe. */
	pid_t pid;
	/* Application identifier used to group syslog messages. */
	char *syslog_ident;
	/**
	 * Counter identifying number of threads executing log_rotate.
	 * Protected by rotate_mutex as it is accessed from different
	 * threads.
	 */
	int rotating_threads;
	/** Mutex for accessing rotating_threads field. */
	pthread_mutex_t rotate_mutex;
	/** Condition that all rotation tasks are finished. */
	pthread_cond_t rotate_cond;
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

/** A utility function to handle va_list from different varargs functions. */
int
log_vsay(struct log *log, int level, const char *filename, int line,
	 const char *error, const char *format, va_list ap);

/** Perform log write. */
static inline int
log_say(struct log *log, int level, const char *filename,
	int line, const char *error, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	int total = log_vsay(log, level, filename, line, error, format, ap);
	va_end(ap);
	return total;
}

/**
 * Default logger type info.
 * @retval say_logger_type.
 */
enum say_logger_type
log_type(void);

/**
 * Accessors for default logger file descriptor.
 *
 * It is needed for decoupling of a logger file descriptor from
 * stderr in the popen implementation.
 *
 * Those functions break logger incapsulation, so use them with
 * caution.
 */
int
log_get_fd(void);
void
log_set_fd(int new_fd);

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
 * Set flight recorder log level.
 */
void
say_set_flightrec_log_level(int new_level);

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

/** Test if logger is initialized. */
bool
say_logger_initialized(void);

/** Free default logger */
void
say_logger_free(void);

/** \cond public */
typedef void (*sayfunc_t)(int, const char *, int, const char *,
		    const char *, ...);

/** Internal function used to implement say() macros */
CFORMAT(printf, 5, 6) extern sayfunc_t _say;

/**
 * Internal function that implements MT-Safe strerror().
 * It is used by say_syserror() macro.
 */
const char *
_say_strerror(int errnum);

/**
 * Format and print a message to Tarantool log file.
 *
 * \param level (int) - log level (see enum \link say_level \endlink)
 * \param file (const char * ) - file name to print
 * \param line (int) - line number to print
 * \param error (const char * ) - error description, may be NULL
 * \param format (const char * ) - printf()-like format string
 * \param ... - format arguments
 * \sa printf()
 * \sa enum say_level
 */
#define say_file_line(level, file, line, error, format, ...) ({ \
	if (say_log_level_is_enabled(level)) \
		_say(level, file, line, error, format, ##__VA_ARGS__); })

/**
 * Format and print a message to Tarantool log file.
 *
 * \param level (int) - log level (see enum \link say_level \endlink)
 * \param error (const char * ) - error description, may be NULL
 * \param format (const char * ) - printf()-like format string
 * \param ... - format arguments
 * \sa printf()
 * \sa enum say_level
 */
#define say(level, error, format, ...) ({ \
	say_file_line(level, __FILE__, __LINE__, error, format, ##__VA_ARGS__); })

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
#define say_syserror(format, ...) say(S_SYSERROR, _say_strerror(errno), \
				      format, ##__VA_ARGS__)
/** \endcond public */

#define panic_status(status, ...)	({ say(S_FATAL, NULL, __VA_ARGS__); exit(status); })
#define panic(...)			panic_status(EXIT_FAILURE, __VA_ARGS__)
#define panic_syserror(...)		({ \
	say(S_FATAL, tt_strerror(errno), __VA_ARGS__); \
	exit(EXIT_FAILURE); \
})

enum {
	/* 10 messages per 5 seconds. */
	SAY_RATELIMIT_INTERVAL = 5,
	SAY_RATELIMIT_BURST = 10,
};

/**
 * Wrapper around ratelimit_check() that prints a warning if some
 * messages are suppressed. It uses ev_monotonic_now() as a time
 * source.
 */
#define say_ratelimit_check(rl, level) ({				\
	int suppressed = 0;						\
	bool ret = ratelimit_check((rl), ev_monotonic_now(loop()),	\
				   &suppressed);			\
	if ((level) >= S_WARN && suppressed > 0)			\
		say_warn("%d messages suppressed", suppressed);		\
	ret;								\
})

/**
 * Same as say(), but rate limited. If this function is called more
 * often than SAY_RATELIMIT_BURST times per SAY_RATELIMIT_INTERVAL
 * seconds, extra messages are suppressed and a warning is printed
 * to the log.
 */
#define say_ratelimited(level, error, format, ...) ({			\
	static struct ratelimit rl =					\
		RATELIMIT_INITIALIZER(SAY_RATELIMIT_INTERVAL,		\
				      SAY_RATELIMIT_BURST);		\
	if (say_ratelimit_check(&rl, level))				\
		say(level, error, format, ##__VA_ARGS__);		\
})

#define say_crit_ratelimited(format, ...) \
        say_ratelimited(S_CRIT, NULL, format, ##__VA_ARGS__)

#define say_warn_ratelimited(format, ...) \
	say_ratelimited(S_WARN, NULL, format, ##__VA_ARGS__)

#define say_info_ratelimited(format, ...) \
        say_ratelimited(S_INFO, NULL, format, ##__VA_ARGS__)

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
	enum say_syslog_server_type server_type;
	const char *server_path;
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

/**
 * A type defining a callback that is called before or after
 * writing to stderr.
 */
typedef void
(*say_stderr_callback_t)(void);

/**
 * Set callback functions called before/after writing to stderr.
 */
void
say_set_stderr_callback(say_stderr_callback_t before,
			say_stderr_callback_t after);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LIB_CORE_SAY_H_INCLUDED */
