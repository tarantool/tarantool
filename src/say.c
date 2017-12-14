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
#include "say.h"
#include "fiber.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>

pid_t log_pid = 0;
int log_level = S_INFO;
int log_format = SF_PLAIN;

static const char logger_syntax_reminder[] =
	"expecting a file name or a prefix, such as '|', 'pipe:', 'syslog:'";

static bool logger_background = true;
static int logger_nonblock;

static int log_fd = STDERR_FILENO;
static char *log_path; /* iff logger_type == SAY_LOGGER_FILE */
/* Application identifier used to group syslog messages. */
static char *syslog_ident = NULL;

static void
say_logger_boot(int level, const char *filename, int line, const char *error,
		const char *format, ...);
static void
say_logger_file(int level, const char *filename, int line, const char *error,
		const char *format, ...);
static void
say_logger_syslog(int level, const char *filename, int line, const char *error,
		  const char *format, ...);

static enum say_logger_type logger_type = SAY_LOGGER_BOOT;
sayfunc_t _say = say_logger_boot;

static const char level_chars[] = {
	[S_FATAL] = 'F',
	[S_SYSERROR] = '!',
	[S_ERROR] = 'E',
	[S_CRIT] = 'C',
	[S_WARN] = 'W',
	[S_INFO] = 'I',
	[S_VERBOSE] = 'V',
	[S_DEBUG] = 'D',
};

static char
level_to_char(int level)
{
	assert(level >= S_FATAL && level <= S_DEBUG);
	return level_chars[level];
}

static const char *level_strs[] = {
	[S_FATAL] = "FATAL",
	[S_SYSERROR] = "SYSERROR",
	[S_ERROR] = "ERROR",
	[S_CRIT] = "CRIT",
	[S_WARN] = "WARN",
	[S_INFO] = "INFO",
	[S_VERBOSE] = "VERBOSE",
	[S_DEBUG] = "DEBUG",
};

static const char*
level_to_string(int level)
{
	assert(level >= S_FATAL && level <= S_DEBUG);
	return level_strs[level];
}

static int
level_to_syslog_priority(int level)
{
	switch (level) {
	case S_FATAL:
		return LOG_ERR;
	case S_SYSERROR:
		return LOG_ERR;
	case S_ERROR:
		return LOG_ERR;
	case S_CRIT:
		return LOG_ERR;
	case S_WARN:
		return LOG_WARNING;
	case S_INFO:
		return LOG_INFO;
	case S_VERBOSE:
		return LOG_INFO;
	case S_DEBUG:
		return LOG_DEBUG;
	default:
		return LOG_ERR;
	}
}

void
say_set_log_level(int new_level)
{
	log_level = new_level;
}

void
say_set_log_format(enum say_format format)
{
	assert(format >= SF_PLAIN && format <= SF_JSON);
	log_format = format;
}

static const char *say_format_strs[] = {
	[SF_PLAIN] = "plain",
	[SF_JSON] = "json",
	[say_format_MAX] = "unknown"
};

enum say_format
say_format_by_name(const char *format)
{
	return STR2ENUM(say_format, format);
}

/**
 * Initialize the logger pipe: a standalone
 * process which is fed all log messages.
 */
static int
say_pipe_init(const char *init_str)
{
	int pipefd[2];
	char cmd[] = { "/bin/sh" };
	char args[] = { "-c" };
	char *argv[] = { cmd, args, (char *) init_str, NULL };
	char *envp[] = { NULL };
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);

	if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
		say_syserror("sigprocmask");

	if (pipe(pipefd) == -1) {
		diag_set(SystemError, "failed to create pipe");
		return -1;
	}

	/* flush buffers to avoid multiple output */
	/* https://github.com/tarantool/tarantool/issues/366 */
	fflush(stdout);
	fflush(stderr);
	log_pid = fork();
	if (log_pid == -1) {
		diag_set(SystemError, "failed to create process");
		return -1;
	}

	if (log_pid == 0) {
		sigprocmask(SIG_UNBLOCK, &mask, NULL);

		close(pipefd[1]);
		dup2(pipefd[0], STDIN_FILENO);
		/*
		 * Move to an own process group, to not
		 * receive signals from the controlling
		 * tty. This keeps the log open as long as
		 * the parent is around. When the parent
		 * dies, we get SIGPIPE and terminate.
		 */
		setpgid(0, 0);
		execve(argv[0], argv, envp); /* does not return */
		diag_set(SystemError, "can't start logger: %s", init_str);
		return -1;
	}
#ifndef TARGET_OS_DARWIN
	/*
	 * A courtesy to a DBA who might have
	 * misconfigured the logger option: check whether
	 * or not the logger process has started, and if
	 * it didn't, abort. Notice, that if the logger
	 * makes a slow start this is futile.
	 */
	struct timespec timeout;
	timeout.tv_sec = 0;
	timeout.tv_nsec = 1; /* Mostly to trigger preemption. */
	if (sigtimedwait(&mask, NULL, &timeout) == SIGCHLD) {
		diag_set(IllegalParams, "logger process died");
		return -1;
	}
#endif
	/* OK, let's hope for the best. */
	sigprocmask(SIG_UNBLOCK, &mask, NULL);
	close(pipefd[0]);
	log_fd = pipefd[1];
	say_info("started logging into a pipe, SIGHUP log rotation disabled");
	logger_type = SAY_LOGGER_PIPE;
	_say = say_logger_file;
	return 0;
}

/**
 * Rotate logs on SIGHUP
 */
void
say_logrotate(int signo)
{
	(void) signo;
	if (logger_type != SAY_LOGGER_FILE)
		return;
	int saved_errno = errno;
	int fd = open(log_path, O_WRONLY | O_APPEND | O_CREAT,
	              S_IRUSR | S_IWUSR | S_IRGRP);
	if (fd < 0)
		goto done;
	/* The whole charade's purpose is to avoid log_fd changing.
	 * Remember, we are a signal handler.*/
	dup2(fd, log_fd);
	close(fd);

	if (logger_background) {
		dup2(log_fd, STDOUT_FILENO);
		dup2(log_fd, STDERR_FILENO);
	}
	if (logger_nonblock) {
		int flags;
		if ( (flags = fcntl(log_fd, F_GETFL, 0)) < 0 ||
		    fcntl(log_fd, F_SETFL, flags | O_NONBLOCK) < 0)
			say_syserror("fcntl, fd=%i", log_fd);
	}
	char logrotate_message[] = "log file has been reopened\n";
	int r = write(log_fd,
	              logrotate_message, (sizeof logrotate_message) - 1);
	(void)r;
done:
	errno = saved_errno;
}

/**
 * Initialize logging to a file and set up a log
 * rotation signal.
 */
static int
say_file_init(const char *init_str)
{
	int fd;
	log_path = abspath(init_str);
	if (log_path == NULL) {
		diag_set(OutOfMemory, strlen(init_str), "malloc", "abspath");
		return -1;
	}
	fd = open(log_path, O_WRONLY | O_APPEND | O_CREAT,
		  S_IRUSR | S_IWUSR | S_IRGRP);
	if (fd < 0) {
		diag_set(SystemError, "can't open log file: %s", log_path);
		return -1;
	}
	log_fd = fd;
	signal(SIGHUP, say_logrotate); /* will access log_fd */
	logger_type = SAY_LOGGER_FILE;
	_say = say_logger_file;
	return 0;
}

/**
 * Connect to syslogd using UNIX socket.
 * @param path UNIX socket path.
 * @retval not 0 Socket descriptor.
 * @retval    -1 Socket error.
 */
static inline int
syslog_connect_unix(const char *path)
{
	int fd = socket(PF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0)
		return -1;
	struct sockaddr_un un;
	memset(&un, 0, sizeof(un));
	snprintf(un.sun_path, sizeof(un.sun_path), "%s", path);
	un.sun_family = AF_UNIX;
	if (connect(fd, (struct sockaddr *) &un, sizeof(un)) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static inline int
say_syslog_connect()
{
	/*
	 * Try two locations: '/dev/log' for Linux and
	 * '/var/run/syslog' for Mac.
	 */
	log_fd = syslog_connect_unix("/dev/log");
	if (log_fd < 0)
		return syslog_connect_unix("/var/run/syslog");
	return log_fd;
}

/** Initialize logging to syslog */
static int
say_syslog_init(const char *init_str)
{
	struct say_syslog_opts opts;

	if (say_parse_syslog_opts(init_str, &opts) < 0) {
		return -1;
	}

	if (opts.identity == NULL)
		syslog_ident = strdup("tarantool");
	else
		syslog_ident = strdup(opts.identity);
	say_free_syslog_opts(&opts);
	log_fd = say_syslog_connect();
	if (log_fd < 0) {
		/* syslog indent is freed in atexit(). */
		diag_set(SystemError, "syslog logger: %s", strerror(errno));
		return -1;
	}
	say_info("started logging to syslog, SIGHUP log rotation disabled");
	logger_type = SAY_LOGGER_SYSLOG;
	_say = say_logger_syslog;
	return 0;
}

/**
 * Initialize logging subsystem to use in daemon mode.
 */
static int
say_logger_do_init(const char *init_str, int level, int nonblock, const char *format, int background)
{
	log_level = level;
	say_set_log_format(say_format_by_name(format));
	logger_nonblock = nonblock;
	logger_background = background;
	setvbuf(stderr, NULL, _IONBF, 0);

	if (init_str != NULL) {
		enum say_logger_type type;
		if (say_parse_logger_type(&init_str, &type)) {
			diag_set(IllegalParams, logger_syntax_reminder);
			return -1;
		}
		int rc;
		switch (type) {
		case SAY_LOGGER_PIPE:
			rc = say_pipe_init(init_str);
			break;
		case SAY_LOGGER_SYSLOG:
			if (log_format != SF_PLAIN) {
				diag_set(IllegalParams, "logger: syslog does not "
						"support non-plain formats\n");
				return -1;
			}
			rc = say_syslog_init(init_str);
			break;
		case SAY_LOGGER_FILE:
		default:
			rc = say_file_init(init_str);
			break;
		}
		if (rc < 0) {
			return -1;
		}
		/*
		 * Set non-blocking mode if a non-default log
		 * output is set. Avoid setting stdout to
		 * non-blocking: this will garble interactive
		 * console output.
		 */
		if (logger_nonblock) {
			int flags;
			if ( (flags = fcntl(log_fd, F_GETFL, 0)) < 0 ||
				fcntl(log_fd, F_SETFL, flags | O_NONBLOCK) < 0)
				say_syserror("fcntl, fd=%i", log_fd);
		}
	} else {
		logger_type = SAY_LOGGER_STDERR;
		_say = say_logger_file;
	}

	if (background) {
		fflush(stderr);
		fflush(stdout);
		if (log_fd == STDERR_FILENO) {
			int fd = open("/dev/null", O_WRONLY);
			if (fd < 0) {
				diag_set(SystemError, "open /dev/null");
				return -1;
			}
			dup2(fd, STDERR_FILENO);
			dup2(fd, STDOUT_FILENO);
			close(fd);
		} else {
			dup2(log_fd, STDERR_FILENO);
			dup2(log_fd, STDOUT_FILENO);
		}
	}
	return 0;
}

void
say_logger_init(const char *init_str, int level, int nonblock,
		     const char *format, int background)
{
	if (say_logger_do_init(init_str, level, nonblock,
			       format, background) < 0) {
		diag_log();
		panic("failed to initialize logging subsystem");
	}
}

void
say_logger_free()
{
	if (logger_type == SAY_LOGGER_SYSLOG && log_fd != -1)
		close(log_fd);
	free(syslog_ident);
}

/** {{{ Formatters */

/**
 * Format the log message in compact form:
 * MESSAGE: ERROR
 *
 * Used during boot time, e.g. without box.cfg().
 */
static int
say_format_boot(char *buf, int len, const char *error,
		 const char *format, va_list ap)
{
	int total = 0;
	SNPRINT(total, vsnprintf, buf, len, format, ap);
	if (error != NULL)
		SNPRINT(total, snprintf, buf, len, ": %s", error);
	SNPRINT(total, snprintf, buf, len, "\n");
	return total;
}

/**
 * The common helper for say_format_plain() and say_format_syslog()
 */
static int
say_format_plain_tail(char *buf, int len, int level, const char *filename,
		      int line, const char *error, const char *format,
		      va_list ap)
{
	int total = 0;

	struct cord *cord = cord();
	if (cord) {
		SNPRINT(total, snprintf, buf, len, " %s", cord->name);
		if (fiber() && fiber()->fid != 1) {
			SNPRINT(total, snprintf, buf, len, "/%i/%s",
				fiber()->fid, fiber_name(fiber()));
		}
	}

	if (level == S_WARN || level == S_ERROR || level == S_SYSERROR) {
		/* Primitive basename(filename) */
		for (const char *f = filename; *f; f++)
			if (*f == '/' && *(f + 1) != '\0')
				filename = f + 1;
		SNPRINT(total, snprintf, buf, len, " %s:%i", filename, line);
	}

	SNPRINT(total, snprintf, buf, len, " %c> ", level_to_char(level));

	SNPRINT(total, vsnprintf, buf, len, format, ap);
	if (error != NULL)
		SNPRINT(total, snprintf, buf, len, ": %s", error);

	SNPRINT(total, snprintf, buf, len, "\n");
	return total;
}

/**
 * Format the log message in Tarantool format:
 * YYYY-MM-DD hh:mm:ss.ms [PID]: CORD/FID/FIBERNAME LEVEL> MSG
 */
static int
say_format_plain(char *buf, int len, int level, const char *filename, int line,
		 const char *error, const char *format, va_list ap)
{
	/* Don't use ev_now() since it requires a working event loop. */
	ev_tstamp now = ev_time();
	time_t now_seconds = (time_t) now;
	struct tm tm;
	localtime_r(&now_seconds, &tm);

	/* Print time in format 2012-08-07 18:30:00.634 */
	int total = strftime(buf, len, "%F %H:%M", &tm);
	buf += total, len -= total;
	SNPRINT(total, snprintf, buf, len, ":%06.3f",
		now - now_seconds + tm.tm_sec);

	/* Print pid */
	SNPRINT(total, snprintf, buf, len, " [%i]", getpid());

	/* Print remaining parts */
	SNPRINT(total, say_format_plain_tail, buf, len, level, filename, line,
		error, format, ap);

	return total;
}

/**
 * Format log message in json format:
 * {"time": 1507026445.23232, "level": "WARN", "message": <message>, "pid": <pid>,
 * "cord_name": <name>, "fiber_id": <id>, "fiber_name": <fiber_name>, filename": <filename>, "line": <fds>}
 */
static int
say_format_json(char *buf, int len, int level, const char *filename, int line,
		 const char *error, const char *format, va_list ap)
{
	int total = 0;

	SNPRINT(total, snprintf, buf, len, "{\"time\": \"");

	/* Don't use ev_now() since it requires a working event loop. */
	ev_tstamp now = ev_time();
	time_t now_seconds = (time_t) now;
	struct tm tm;
	localtime_r(&now_seconds, &tm);
	int written = strftime(buf, len, "%FT%H:%M", &tm);
	buf += written, len -= written, total += written;
	SNPRINT(total, snprintf, buf, len, ":%06.3f",
			now - now_seconds + tm.tm_sec);
	written = strftime(buf, len, "%z", &tm);
	buf += written, len -= written, total += written;
	SNPRINT(total, snprintf, buf, len, "\", ");

	SNPRINT(total, snprintf, buf, len, "\"level\": \"%s\", ",
			level_to_string(level));

	if (strncmp(format, "json", sizeof("json")) == 0) {
		/*
		 * Message is already JSON-formatted.
		 * Get rid of {} brackets and append to the output buffer.
		 */
		const char *str = va_arg(ap, const char *);
		assert(str != NULL);
		int str_len = strlen(str);
		assert(str_len > 2 && str[0] == '{' && str[str_len - 1] == '}');
		SNPRINT(total, snprintf, buf, len, "%.*s, ",
			str_len - 2, str + 1);
	} else {
		/* Format message */
		char *tmp = tt_static_buf();
		if (vsnprintf(tmp, TT_STATIC_BUF_LEN, format, ap) < 0)
			return -1;
		SNPRINT(total, snprintf, buf, len, "\"message\": \"");
		/* Escape and print message */
		SNPRINT(total, json_escape, buf, len, tmp);
		SNPRINT(total, snprintf, buf, len, "\", ");
	}

	/* in case of system errors */
	if (error) {
		SNPRINT(total, snprintf, buf, len, "\"error\": \"");
		SNPRINT(total, json_escape, buf, len, error);
		SNPRINT(total, snprintf, buf, len, "\", ");
	}

	SNPRINT(total, snprintf, buf, len, "\"pid\": %i, ", getpid());

	struct cord *cord = cord();
	if (cord) {
		SNPRINT(total, snprintf, buf, len, "\"cord_name\": \"");
		SNPRINT(total, json_escape, buf, len, cord->name);
		SNPRINT(total, snprintf, buf, len, "\", ");
		if (fiber() && fiber()->fid != 1) {
			SNPRINT(total, snprintf, buf, len,
				"\"fiber_id\": %i, ", fiber()->fid);
			SNPRINT(total, snprintf, buf, len,
				"\"fiber_name\": \"");
			SNPRINT(total, json_escape, buf, len,
				fiber()->name);
			SNPRINT(total, snprintf, buf, len, "\", ");
		}
	}

	SNPRINT(total, snprintf, buf, len, "\"file\": \"");
	SNPRINT(total, json_escape, buf, len, filename);
	SNPRINT(total, snprintf, buf, len, "\", ");
	SNPRINT(total, snprintf, buf, len, "\"line\": %i}", line);
	SNPRINT(total, snprintf, buf, len, "\n");
	return total;
}

/**
 * Format the log message in syslog format.
 *
 * See RFC 5424 and RFC 3164. RFC 3164 is compatible with RFC 5424,
 * so it is implemented.
 * Protocol:
 * <PRIORITY_VALUE>TIMESTAMP IDENTATION[PID]: CORD/FID/FIBERNAME LEVEL> MSG
 * - Priority value is encoded as message subject * 8 and bitwise
 *   OR with message level;
 * - Timestamp must be encoded in the format: Mmm dd hh:mm:ss;
 *   Mmm - moth abbreviation;
 * - Identation is application name. By default it is "tarantool";
 */
static int
say_format_syslog(char *buf, int len, int level, const char *filename,
		  int line, const char *error, const char *format, va_list ap)
{
	/* Don't use ev_now() since it requires a working event loop. */
	ev_tstamp now = ev_time();
	time_t now_seconds = (time_t) now;
	struct tm tm;
	localtime_r(&now_seconds, &tm);

	int total = 0;

	/* Format syslog header according to RFC */
	int prio = level_to_syslog_priority(level);
	SNPRINT(total, snprintf, buf, len, "<%d>", LOG_MAKEPRI(1, prio));
	SNPRINT(total, strftime, buf, len, "%h %e %T ", &tm);
	SNPRINT(total, snprintf, buf, len, "%s[%d]:", syslog_ident, getpid());

	/* Format message */
	SNPRINT(total, say_format_plain_tail, buf, len, level, filename, line,
		error, format, ap);

	return total;
}

/** Formatters }}} */

/** {{{ Loggers */

/*
 * From pipe(7):
 * POSIX.1 says that write(2)s of less than PIPE_BUF bytes must be atomic:
 * the output data is written to the pipe as a contiguous sequence. Writes
 * of more than PIPE_BUF bytes may be nonatomic: the kernel may interleave
 * the data with data written by other processes. PIPE_BUF is 4k on Linux.
 *
 * Nevertheless, let's ignore the fact that messages can be interleaved in
 * some situations and set SAY_BUF_LEN_MAX to 16k for now.
 */
enum { SAY_BUF_LEN_MAX = 16 * 1024 };
static __thread char buf[SAY_BUF_LEN_MAX];

/**
 * Boot-time logger.
 *
 * Used without box.cfg()
 */
static void
say_logger_boot(int level, const char *filename, int line, const char *error,
		const char *format, ...)
{
	assert(logger_type == SAY_LOGGER_BOOT);
	(void) filename;
	(void) line;
	if (!say_log_level_is_enabled(level))
		return;

	int errsv = errno; /* Preserve the errno. */
	va_list ap;
	va_start(ap, format);
	int total = say_format_boot(buf, sizeof(buf), error, format, ap);
	assert(total >= 0);
	(void) write(STDERR_FILENO, buf, total);
	va_end(ap);
	errno = errsv; /* Preserve the errno. */
}

/**
 * File and pipe logger
 */
static void
say_logger_file(int level, const char *filename, int line, const char *error,
		const char *format, ...)
{
	assert(logger_type == SAY_LOGGER_FILE ||
	       logger_type == SAY_LOGGER_PIPE ||
	       logger_type == SAY_LOGGER_STDERR);
	if (!say_log_level_is_enabled(level))
		return;

	int errsv = errno; /* Preserve the errno. */
	va_list ap;
	va_start(ap, format);
	int total = 0;
	switch (log_format) {
		case SF_PLAIN:
			total = say_format_plain(buf, sizeof(buf), level,
						 filename, line, error,
						 format, ap);
			break;
		case SF_JSON:
			total = say_format_json(buf, sizeof(buf), level,
						filename, line, error,
						format, ap);
			break;
		default:
			unreachable();
	}
	assert(total >= 0);
	(void) write(log_fd, buf, total);
	/* Log fatal errors to STDERR */
	if (level == S_FATAL && log_fd != STDERR_FILENO)
		(void) write(STDERR_FILENO, buf, total);

	va_end(ap);
	errno = errsv; /* Preserve the errno. */
}

/**
 * Syslog logger
 */
static void
say_logger_syslog(int level, const char *filename, int line, const char *error,
		  const char *format, ...)
{
	assert(logger_type == SAY_LOGGER_SYSLOG);

	if (!say_log_level_is_enabled(level))
		return;

	int errsv = errno; /* Preserve the errno. */
	va_list ap;
	va_start(ap, format);

	int total = say_format_syslog(buf, sizeof(buf), level, filename, line,
				       error, format, ap);
	assert(total >= 0);

	if (level == S_FATAL && log_fd != STDERR_FILENO)
		(void) write(STDERR_FILENO, buf, total);

	if (log_fd < 0 || write(log_fd, buf, total) <= 0) {
		/*
		 * Try to reconnect, if write to syslog has
		 * failed. Syslog write can fail, if, for example,
		 * syslogd is restarted. In such a case write to
		 * UNIX socket starts return -1 even for UDP.
		 */
		if (log_fd >= 0)
			close(log_fd);
		log_fd = say_syslog_connect();
		if (log_fd >= 0) {
			/*
			 * In a case or error the log message is
			 * lost. We can not wait for connection -
			 * it would block thread. Try to reconnect
			 * on next vsay().
			 */
			(void) write(log_fd, buf, total);
		}
	}

	va_end(ap);
	errno = errsv; /* Preserve the errno. */
}

/** Loggers }}} */

/*
 * Init string parser(s)
 */

int
say_check_init_str(const char *str)
{
	enum say_logger_type type;
	if (say_parse_logger_type(&str, &type)) {
		diag_set(IllegalParams, logger_syntax_reminder);
		return -1;
	}
	if (type == SAY_LOGGER_SYSLOG) {
		struct say_syslog_opts opts;

		if (say_parse_syslog_opts(str, &opts) < 0)
			return -1;
		say_free_syslog_opts(&opts);
	}
	return 0;
}

/**
 * @retval string after prefix if a prefix is found,
 *         *str also is advanced to the prefix
 *	   NULL a prefix is not found, str is left intact
 */
static const char *
say_parse_prefix(const char **str, const char *prefix)
{
	size_t len = strlen(prefix);
	if (strncmp(*str, prefix, len) == 0) {
		*str = *str + len;
		return *str;
	}
	return NULL;
}

int
say_parse_logger_type(const char **str, enum say_logger_type *type)
{
	if (say_parse_prefix(str, "|"))
		*type = SAY_LOGGER_PIPE;
	else if (say_parse_prefix(str, "file:"))
		*type = SAY_LOGGER_FILE;
	else if (say_parse_prefix(str, "pipe:"))
		*type = SAY_LOGGER_PIPE;
	else if (say_parse_prefix(str, "syslog:"))
		*type = SAY_LOGGER_SYSLOG;
	else if (strchr(*str, ':') == NULL)
		*type = SAY_LOGGER_FILE;
	else
		return -1;
	return 0;
}

int
say_parse_syslog_opts(const char *init_str, struct say_syslog_opts *opts)
{
	opts->identity = NULL;
	opts->facility = NULL;
	opts->copy = strdup(init_str);
	if (opts->copy == NULL) {
		diag_set(OutOfMemory, strlen(init_str), "malloc", "opts->copy");
		return -1;
	}
	char *ptr = opts->copy;
	const char *option, *value;

	/* strsep() overwrites the separator with '\0' */
	while ((option = strsep(&ptr, ","))) {
		if (*option == '\0')
			break;

		value = option;
		if (say_parse_prefix(&value, "identity=")) {
			if (opts->identity != NULL)
				goto duplicate;
			opts->identity = value;
		} else if (say_parse_prefix(&value, "facility=")) {
			if (opts->facility != NULL)
				goto duplicate;
			opts->facility = value;
		} else {
			diag_set(IllegalParams, "bad option '%s'", option);
			goto error;
		}
	}
	return 0;
duplicate:
	/* Terminate the "bad" option, by overwriting '=' sign */
	((char *)value)[-1] = '\0';
	diag_set(IllegalParams, "duplicate option '%s'", option);
error:
	free(opts->copy); opts->copy = NULL;
	return -1;
}

void
say_free_syslog_opts(struct say_syslog_opts *opts)
{
	free(opts->copy);
	opts->copy = NULL;
}
