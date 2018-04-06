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
#include "errinj.h"

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
#include <coio_task.h>

pid_t log_pid = 0;
int log_level = S_INFO;
enum say_format log_format = SF_PLAIN;

/** List of logs to rotate */
static RLIST_HEAD(log_rotate_list);

static const char logger_syntax_reminder[] =
	"expecting a file name or a prefix, such as '|', 'pipe:', 'syslog:'";
/**
 * True if Tarantool process runs in background mode, i.e. has no
 * controlling terminal.
 */
static bool log_background = true;

static void
say_default(int level, const char *filename, int line, const char *error,
	    const char *format, ...);

static int
say_format_boot(struct log *log, char *buf, int len, int level,
		const char *filename, int line, const char *error,
		const char *format, va_list ap);
static int
say_format_syslog(struct log *log, char *buf, int len, int level,
		  const char *filename, int line, const char *error,
		  const char *format, va_list ap);

/** A utility function to handle va_list from different varargs functions. */
static inline int
log_vsay(struct log *log, int level, const char *filename, int line,
	 const char *error, const char *format, va_list ap);

/** Default logger used before logging subsystem is initialized. */
static struct log log_boot = {
	.fd = STDERR_FILENO,
	.level = S_INFO,
	.type = SAY_LOGGER_BOOT,
	.path = NULL, /* iff type == SAY_LOGGER_FILE */
	.nonblock = false,
	.format_func = say_format_boot,
	.pid = 0,
	.syslog_ident = NULL,
};

/** Default logger used after bootstrap. */
static struct log log_std;

static struct log *log_default = &log_boot;

sayfunc_t _say = say_default;

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

static const char *say_logger_type_strs[] = {
	[SAY_LOGGER_BOOT] = "stdout",
	[SAY_LOGGER_STDERR] = "stderr",
	[SAY_LOGGER_FILE] = "file",
	[SAY_LOGGER_PIPE] = "pipe",
	[SAY_LOGGER_SYSLOG] = "syslog",
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
log_set_level(struct log *log, enum say_level level)
{
	log->level = level;
}

void
log_set_format(struct log *log, log_format_func_t format_func)
{
	assert(format_func == say_format_plain ||
	       log->type == SAY_LOGGER_STDERR ||
	       log->type == SAY_LOGGER_PIPE || log->type == SAY_LOGGER_FILE);

	log->format_func = format_func;
}

void
say_set_log_level(int new_level)
{
	log_level = new_level;
	log_set_level(log_default, (enum say_level) new_level);
}

void
say_set_log_format(enum say_format format)
{
	/*
	 * For syslog, default or boot log type the log format can
	 * not be changed.
	 */
	bool allowed_to_change = log_default->type == SAY_LOGGER_STDERR ||
				 log_default->type == SAY_LOGGER_PIPE ||
				 log_default->type == SAY_LOGGER_FILE;
	switch (format) {
	case SF_JSON:

		if (!allowed_to_change) {
			say_error("json log format is not supported when output is '%s'",
				  say_logger_type_strs[log_default->type]);
			return;
		}
		log_set_format(log_default, say_format_json);
		break;
	case SF_PLAIN:
		if (!allowed_to_change) {
			return;
		}
		log_set_format(log_default, say_format_plain);
		break;
	default:
		unreachable();
	}
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

static void
write_to_file(struct log *log, int total);
static void
write_to_syslog(struct log *log, int total);

/**
 * Rotate logs on SIGHUP
 */
static int
log_rotate(struct log *log)
{
	if (pm_atomic_load(&log->type) != SAY_LOGGER_FILE)
		return 0;

	ERROR_INJECT(ERRINJ_LOG_ROTATE, { usleep(10); });

	int fd = open(log->path, O_WRONLY | O_APPEND | O_CREAT,
		      S_IRUSR | S_IWUSR | S_IRGRP);
	if (fd < 0) {
		diag_set(SystemError, "logrotate can't open %s", log->path);
		return -1;
	}
	/*
	 * The whole charade's purpose is to avoid log->fd changing.
	 * Remember, we are a signal handler.
	 */
	dup2(fd, log->fd);
	close(fd);

	if (log->nonblock) {
		int flags;
		if ( (flags = fcntl(log->fd, F_GETFL, 0)) < 0 ||
		     fcntl(log->fd, F_SETFL, flags | O_NONBLOCK) < 0) {
			say_syserror("fcntl, fd=%i", log->fd);
		}
	}
	/* We are in ev signal handler
	 * so we don't have to be worry about async signal safety
	 */
	log_say(log, S_INFO, __FILE__, __LINE__, NULL,
		"log file has been reopened");
	/*
	 * log_background applies only to log_default logger
	 */
	if (log == log_default && log_background &&
		log->type == SAY_LOGGER_FILE) {
		dup2(log_default->fd, STDOUT_FILENO);
		dup2(log_default->fd, STDERR_FILENO);
	}

	return 0;
}

struct rotate_task {
	struct coio_task base;
	struct log *log;
	struct ev_loop *loop;
};

static int
logrotate_cb(struct coio_task *ptr)
{
	struct rotate_task *task = (struct rotate_task *) ptr;
	if (log_rotate(task->log) < 0) {
		diag_log();
	}
	ev_async_send(task->loop, &task->log->log_async);
	return 0;
}

static int
logrotate_cleanup_cb(struct coio_task *ptr)
{
	struct rotate_task *task = (struct rotate_task *) ptr;
	coio_task_destroy(&task->base);
	free(task);
	return 0;
}

static void
log_rotate_async_cb(struct ev_loop *loop, struct ev_async *watcher, int events)
{
	(void)loop;
	(void)events;
	struct log *log = container_of(watcher, struct log, log_async);
	log->rotating_threads--;
	fiber_cond_signal(&log->rotate_cond);
}

void
say_logrotate(struct ev_loop *loop, struct ev_signal *w, int revents)
{
	(void) loop;
	(void) w;
	(void) revents;
	int saved_errno = errno;
	struct log *log;
	rlist_foreach_entry(log, &log_rotate_list, in_log_list) {
		struct rotate_task *task =
			(struct rotate_task *) calloc(1, sizeof(*task));
		if (task == NULL) {
			diag_set(OutOfMemory, sizeof(*task), "malloc",
				 "say_logrotate");
			diag_log();
			continue;
		}
		ev_async_start(loop(), &log->log_async);
		log->rotating_threads++;
		coio_task_create(&task->base, logrotate_cb, logrotate_cleanup_cb);
		task->log = log;
		task->loop = loop();
		coio_task_post(&task->base, 0);
	}
	errno = saved_errno;
}

/**
 * Initialize the logger pipe: a standalone
 * process which is fed all log messages.
 */
static int
log_pipe_init(struct log *log, const char *init_str)
{
	int pipefd[2];
	char cmd[] = { "/bin/sh" };
	char args[] = { "-c" };
	char *argv[] = { cmd, args, (char *) init_str, NULL };
	log->type = SAY_LOGGER_PIPE;
	log->format_func = say_format_plain;
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

	log->pid = fork();
	if (log->pid == -1) {
		diag_set(SystemError, "failed to create process");
		return -1;
	}

	if (log->pid == 0) {
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
		execv(argv[0], argv); /* does not return */
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
	log->fd = pipefd[1];
	return 0;
}

/**
 * Initialize logging to a file and set up a log
 * rotation signal.
 */
static int
log_file_init(struct log *log, const char *init_str)
{
	int fd;
	log->path = abspath(init_str);
	log->type = SAY_LOGGER_FILE;
	log->format_func = say_format_plain;
	if (log->path == NULL) {
		diag_set(OutOfMemory, strlen(init_str), "malloc", "abspath");
		return -1;
	}
	fd = open(log->path, O_WRONLY | O_APPEND | O_CREAT,
	          S_IRUSR | S_IWUSR | S_IRGRP);
	if (fd < 0) {
		diag_set(SystemError, "can't open log file: %s", log->path);
		return -1;
	}
	log->fd = fd;
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
log_syslog_connect(struct log *log)
{
	/*
	 * Try two locations: '/dev/log' for Linux and
	 * '/var/run/syslog' for Mac.
	 */
	log->fd = syslog_connect_unix("/dev/log");
	if (log->fd < 0)
		log->fd = syslog_connect_unix("/var/run/syslog");
	return log->fd;
}

/** Initialize logging to syslog */
static int
log_syslog_init(struct log *log, const char *init_str)
{
	struct say_syslog_opts opts;
	log->type = SAY_LOGGER_SYSLOG;
	/* syslog supports only one formatting function */
	log->format_func = say_format_syslog;

	if (say_parse_syslog_opts(init_str, &opts) < 0)
		return -1;

	if (opts.identity == NULL)
		log->syslog_ident = strdup("tarantool");
	else
		log->syslog_ident = strdup(opts.identity);

	if (opts.facility == syslog_facility_MAX)
		log->syslog_facility = SYSLOG_LOCAL7;
	else
		log->syslog_facility = opts.facility;
	say_free_syslog_opts(&opts);
	log->fd = log_syslog_connect(log);
	if (log->fd < 0) {
		/* syslog indent is freed in atexit(). */
		diag_set(SystemError, "syslog logger: %s", strerror(errno));
		return -1;
	}
	return 0;
}

/**
 * Initialize logging subsystem to use in daemon mode.
 */
int
log_create(struct log *log, const char *init_str, int nonblock)
{
	log->pid = 0;
	log->syslog_ident = NULL;
	log->path = NULL;
	log->format_func = NULL;
	log->level = S_INFO;
	log->rotating_threads = 0;
	fiber_cond_create(&log->rotate_cond);
	ev_async_init(&log->log_async, log_rotate_async_cb);
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
			log->nonblock = (nonblock >= 0)? nonblock: true;
			rc = log_pipe_init(log, init_str);
			break;
		case SAY_LOGGER_SYSLOG:
			log->nonblock = (nonblock >= 0)? nonblock: true;
			rc = log_syslog_init(log, init_str);
			break;
		case SAY_LOGGER_FILE:
		default:
			log->nonblock = (nonblock >= 0)? nonblock: false;
			rc = log_file_init(log, init_str);
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
		if (log->nonblock) {
			int flags;
			if ( (flags = fcntl(log->fd, F_GETFL, 0)) < 0 ||
			     fcntl(log->fd, F_SETFL, flags | O_NONBLOCK) < 0)
				say_syserror("fcntl, fd=%i", log->fd);
		}
	} else {
		log->type = SAY_LOGGER_STDERR;
		log->fd = STDERR_FILENO;
	}
	if (log->type == SAY_LOGGER_FILE)
		rlist_add_entry(&log_rotate_list, log, in_log_list);
	else
		rlist_create(&log->in_log_list);
	return 0;
}

void
say_logger_init(const char *init_str, int level, int nonblock,
		const char *format, int background)
{
	if (log_create(&log_std, init_str, nonblock) < 0)
		goto fail;

	log_default = &log_std;

	switch (log_default->type) {
	case SAY_LOGGER_PIPE:
		fprintf(stderr, "started logging into a pipe,"
			" SIGHUP log rotation disabled\n");
		break;
	case SAY_LOGGER_SYSLOG:
		fprintf(stderr, "started logging into a syslog,"
			" SIGHUP log rotation disabled\n");
	default:
		break;
	}
	_say = say_default;
	say_set_log_level(level);
	log_background = background;
	log_pid = log_default->pid;
	say_set_log_format(say_format_by_name(format));

	if (background) {
		fflush(stderr);
		fflush(stdout);
		if (log_default->fd == STDERR_FILENO) {
			int fd = open("/dev/null", O_WRONLY);
			if (fd < 0) {
				diag_set(SystemError, "open /dev/null");
				goto fail;
			}
			dup2(fd, STDERR_FILENO);
			dup2(fd, STDOUT_FILENO);
			close(fd);
		} else {
			dup2(log_default->fd, STDERR_FILENO);
			dup2(log_default->fd, STDOUT_FILENO);
		}
	}
	return;
fail:
	diag_log();
	panic("failed to initialize logging subsystem");
}

void
say_logger_free()
{
	if (log_default == &log_std)
		log_destroy(&log_std);
}

/** {{{ Formatters */

/**
 * Format the log message in compact form:
 * MESSAGE: ERROR
 *
 * Used during boot time, e.g. without box.cfg().
 */
static int
say_format_boot(struct log *log, char *buf, int len, int level, const char *filename, int line,
		const char *error, const char *format, va_list ap)
{
	(void) log;
	(void) filename;
	(void) line;
	(void) level;
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
		if (filename) {
			SNPRINT(total, snprintf, buf, len, " %s:%i", filename,
				line);
		}
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
int
say_format_plain(struct log *log, char *buf, int len, int level, const char *filename, int line,
		 const char *error, const char *format, va_list ap)
{
	(void) log;
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
 * {"time": 1507026445.23232, "level": "WARN", "message": <message>,
 * "pid": <pid>, "cord_name": <name>, "fiber_id": <id>,
 * "fiber_name": <fiber_name>, filename": <filename>, "line": <fds>}
 */
int
say_format_json(struct log *log, char *buf, int len, int level, const char *filename, int line,
		 const char *error, const char *format, va_list ap)
{
	(void) log;
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

	SNPRINT(total, snprintf, buf, len, "\"pid\": %i ", getpid());

	struct cord *cord = cord();
	if (cord) {
		SNPRINT(total, snprintf, buf, len, ", \"cord_name\": \"");
		SNPRINT(total, json_escape, buf, len, cord->name);
		SNPRINT(total, snprintf, buf, len, "\"");
		if (fiber() && fiber()->fid != 1) {
			SNPRINT(total, snprintf, buf, len,
				", \"fiber_id\": %i, ", fiber()->fid);
			SNPRINT(total, snprintf, buf, len,
				"\"fiber_name\": \"");
			SNPRINT(total, json_escape, buf, len,
				fiber()->name);
			SNPRINT(total, snprintf, buf, len, "\"");
		}
	}

	if (filename) {
		SNPRINT(total, snprintf, buf, len, ", \"file\": \"");
		SNPRINT(total, json_escape, buf, len, filename);
		SNPRINT(total, snprintf, buf, len, "\", \"line\": %i", line);
	}
	SNPRINT(total, snprintf, buf, len, "}\n");
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
say_format_syslog(struct log *log, char *buf, int len, int level, const char *filename,
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
	SNPRINT(total, snprintf, buf, len, "<%d>",
		LOG_MAKEPRI(8 * log->syslog_facility, prio));
	SNPRINT(total, strftime, buf, len, "%h %e %T ", &tm);
	SNPRINT(total, snprintf, buf, len, "%s[%d]:", log->syslog_ident, getpid());

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
 * Wrapper over write which ensures, that writes not more than buffer size.
 */
static ssize_t
safe_write(int fd, const char *buf, int size)
{
	/* Writes at most SAY_BUF_LEN_MAX - 1
	 * (1 byte was taken for 0 byte in vsnprintf).
	 */
	return write(fd, buf, MIN(size, SAY_BUF_LEN_MAX - 1));
}

static void
say_default(int level, const char *filename, int line, const char *error,
	    const char *format, ...)
{
	int errsv = errno;
	va_list ap;
	va_start(ap, format);
	int total = log_vsay(log_default, level, filename,
			     line, error, format, ap);
	if (level == S_FATAL && log_default->fd != STDERR_FILENO) {
		ssize_t r = safe_write(STDERR_FILENO, buf, total);
		(void) r;                       /* silence gcc warning */
	}

	va_end(ap);
	errno = errsv; /* Preserve the errno. */
}

/**
 * File and pipe logger
 */
static void
write_to_file(struct log *log, int total)
{
	assert(log->type == SAY_LOGGER_FILE ||
	       log->type == SAY_LOGGER_PIPE ||
	       log->type == SAY_LOGGER_STDERR);
	assert(total >= 0);
	ssize_t r = safe_write(log->fd, buf, total);
	(void) r;                               /* silence gcc warning */
}

/**
 * Syslog logger
 */
static void
write_to_syslog(struct log *log, int total)
{
	assert(log->type == SAY_LOGGER_SYSLOG);
	assert(total >= 0);
	if (log->fd < 0 || safe_write(log->fd, buf, total) <= 0) {
		/*
		 * Try to reconnect, if write to syslog has
		 * failed. Syslog write can fail, if, for example,
		 * syslogd is restarted. In such a case write to
		 * UNIX socket starts return -1 even for UDP.
		 */
		if (log->fd >= 0)
			close(log->fd);
		log->fd = log_syslog_connect(log);
		if (log->fd >= 0) {
			/*
			 * In a case or error the log message is
			 * lost. We can not wait for connection -
			 * it would block thread. Try to reconnect
			 * on next vsay().
			 */
			ssize_t r = safe_write(log->fd, buf, total);
			(void) r;               /* silence gcc warning */
		}
	}
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

static const char *syslog_facility_strs[] = {
	[SYSLOG_KERN] = "kern",
	[SYSLOG_USER] = "user",
	[SYSLOG_MAIL] = "mail",
	[SYSLOG_DAEMON] = "daemon",
	[SYSLOG_AUTH] = "auth",
	[SYSLOG_INTERN] = "intern",
	[SYSLOG_LPR] = "lpr",
	[SYSLOG_NEWS] = "news",
	[SYSLOG_UUCP] = "uucp",
	[SYSLOG_CLOCK] = "clock",
	[SYSLOG_AUTHPRIV] = "authpriv",
	[SYSLOG_FTP] = "ftp",
	[SYSLOG_NTP] = "ntp",
	[SYSLOG_AUDIT] = "audit",
	[SYSLOG_ALERT] = "alert",
	[SYSLOG_CRON] = "cron",
	[SYSLOG_LOCAL0] = "local0",
	[SYSLOG_LOCAL1] = "local1",
	[SYSLOG_LOCAL2] = "local2",
	[SYSLOG_LOCAL3] = "local3",
	[SYSLOG_LOCAL4] = "local4",
	[SYSLOG_LOCAL5] = "local5",
	[SYSLOG_LOCAL6] = "local6",
	[SYSLOG_LOCAL7] = "local7",
	[syslog_facility_MAX] = "unknown",
};

enum syslog_facility
say_syslog_facility_by_name(const char *facility)
{
	return STR2ENUM(syslog_facility, facility);
}

int
say_parse_syslog_opts(const char *init_str, struct say_syslog_opts *opts)
{
	opts->identity = NULL;
	opts->facility = syslog_facility_MAX;
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
			if (opts->facility != syslog_facility_MAX)
				goto duplicate;
			opts->facility = say_syslog_facility_by_name(value);
			if (opts->facility == syslog_facility_MAX) {
				diag_set(IllegalParams, "bad syslog facility option '%s'",
					 value);
				goto error;
			}
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

void
log_destroy(struct log *log)
{
	assert(log != NULL);
	while(log->rotating_threads > 0)
		fiber_cond_wait(&log->rotate_cond);
	pm_atomic_store(&log->type, SAY_LOGGER_BOOT);

	if (log->fd != -1)
		close(log->fd);
	free(log->syslog_ident);
	free(log->path);
	rlist_del_entry(log, in_log_list);
	ev_async_stop(loop(), &log->log_async);
	fiber_cond_destroy(&log->rotate_cond);
}

static inline int
log_vsay(struct log *log, int level, const char *filename, int line,
	 const char *error, const char *format, va_list ap)
{
	int errsv = errno;
	if (level > log->level) {
		return 0;
	}
	int total = log->format_func(log, buf, sizeof(buf), level,
				     filename, line, error, format, ap);
	switch (log->type) {
	case SAY_LOGGER_FILE:
	case SAY_LOGGER_PIPE:
	case SAY_LOGGER_STDERR:
		write_to_file(log, total);
		break;
	case SAY_LOGGER_SYSLOG:
		write_to_syslog(log, total);
		if (level == S_FATAL && log->fd != STDERR_FILENO)
			(void) safe_write(STDERR_FILENO, buf, total);
		break;
	case SAY_LOGGER_BOOT:
	{
		ssize_t r = safe_write(STDERR_FILENO, buf, total);
		(void) r;                       /* silence gcc warning */
		break;
	}
	default:
		unreachable();
	}
	errno = errsv; /* Preserve the errno. */
	return total;
}

int
log_say(struct log *log, int level, const char *filename, int line,
	const char *error, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	int total = log_vsay(log, level, filename, line, error, format, ap);
	va_end(ap);
	return total;
}
