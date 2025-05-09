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
#include "tt_strerror.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <coio_task.h>

pid_t log_pid = 0;
/**
 * The global log level. Used as an optimization by the say() macro to
 * avoid unnecessary calls to say_default().
 * Calculated as MAX(log_default->level, log_level_flightrec).
 */
int log_level = S_INFO;
/**
 * Log level of flight recorder. Log is passed to flight recorder only if
 * it's log level not less that this. -1 is used if we should not pass logs
 * to flight recorder (it is currently disabled). Used in Lua via FFI.
 */
int log_level_flightrec = -1;
/**
 * This function is called for every log which log level is not less than
 * log_level_flightrec.
 */
void
(*log_write_flightrec)(int level, const char *filename, int line,
		       const char *error, const char *format, va_list ap);

enum say_format log_format = SF_PLAIN;
enum { SAY_SYSLOG_DEFAULT_PORT = 512 };

NORETURN void
panic(const char *format, ...)
{
	char buf[1024];
	va_list args;

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	panic_status(EXIT_FAILURE, "%s", buf);
}

/** List of logs to rotate */
static RLIST_HEAD(log_rotate_list);

static const char logger_syntax_reminder[] =
	"expecting a file name or a prefix, such as '|', 'pipe:', 'syslog:'";
/**
 * True if Tarantool process runs in background mode, i.e. has no
 * controlling terminal.
 */
static bool log_background;

static void
say_default(int level, const char *filename, int line, const char *error,
	    const char *format, ...);

static int
say_format_boot(struct log *log, char *buf, int len, int level,
		const char *module, const char *filename, int line,
		const char *error, const char *format, va_list ap);

/*
 * Callbacks called before/after writing to stderr.
 */
static say_stderr_callback_t before_stderr_callback;
static say_stderr_callback_t after_stderr_callback;

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

const char *
_say_strerror(int errnum)
{
	return tt_strerror(errnum);
}

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

static_assert(lengthof(level_chars) == say_level_MAX,
	      "level_chars is not defined for one of log levels");

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

static_assert(lengthof(level_strs) == say_level_MAX,
	      "level_strs is not defined for one of log levels");

const char*
say_log_level_str(int level)
{
	if (level < 0 || level >= (int)lengthof(level_strs))
		return NULL;
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

/**
 * Helper function that fills the given tm struct with the current local time.
 * tm_sec is set to the number of seconds that passed since the last minute
 * (0-60). Note, in contrast to tm.tm_sec, it has a fractional part.
 */
static void
get_current_time(struct tm *tm, double *tm_sec)
{
	/* Don't use ev_now() since it requires a working event loop. */
	ev_tstamp now = ev_time();
	time_t now_seconds = (time_t)now;
	localtime_r(&now_seconds, tm);
	*tm_sec = now - now_seconds + tm->tm_sec;
}

enum say_logger_type
log_type(void)
{
	return log_default->type;
}

int
log_get_fd(void)
{
	return log_default->fd;
}

void
log_set_fd(int new_fd)
{
	log_default->fd = new_fd;
}

void
log_set_level(struct log *log, enum say_level level)
{
	log->level = level;
}

void
log_set_format(struct log *log, log_format_func_t format_func)
{
	log->format_func = format_func;
}

void
say_set_log_level(int new_level)
{
	log_level = MAX(new_level, log_level_flightrec);
	log_set_level(log_default, (enum say_level) new_level);
}

int
say_get_log_level(void)
{
	return log_default->level;
}

void
say_set_log_format(enum say_format format)
{
	log_format_func_t format_func;

	switch (format) {
	case SF_JSON:
		format_func = say_format_json;
		break;
	case SF_PLAIN:
		format_func = say_format_plain;
		break;
	default:
		unreachable();
	}

	log_set_format(log_default, format_func);
	log_format = format;
}

void
say_set_flightrec_log_level(int new_level)
{
	log_level = MAX(new_level, log_default->level);
	log_level_flightrec = new_level;
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
 * Sets O_NONBLOCK flag in case if lognonblock is set.
 */
static void
log_set_nonblock(struct log *log)
{
	if (!log->nonblock)
		return;
	int flags;
	if ((flags = fcntl(log->fd, F_GETFL, 0)) < 0 ||
	    fcntl(log->fd, F_SETFL, flags | O_NONBLOCK) < 0) {
		say_syserror("fcntl, fd=%i", log->fd);
	}
}

/**
 * Rotate logs on SIGHUP
 */
static int
log_rotate(struct log *log)
{
	if (log->type != SAY_LOGGER_FILE)
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

	log_set_nonblock(log);

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
	struct log *log = task->log;
	if (log_rotate(log) < 0)
		diag_log();
	tt_pthread_mutex_lock(&log->rotate_mutex);
	assert(log->rotating_threads > 0);
	log->rotating_threads--;
	if (log->rotating_threads == 0)
		tt_pthread_cond_signal(&log->rotate_cond);
	tt_pthread_mutex_unlock(&log->rotate_mutex);
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
		tt_pthread_mutex_lock(&log->rotate_mutex);
		log->rotating_threads++;
		tt_pthread_mutex_unlock(&log->rotate_mutex);
		coio_task_create(&task->base, logrotate_cb, logrotate_cleanup_cb);
		task->log = log;
		task->loop = loop();
		coio_task_post(&task->base);
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
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);

	if (pthread_sigmask(SIG_BLOCK, &mask, NULL) == -1)
		say_syserror("pthread_sigmask");

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
		pthread_sigmask(SIG_UNBLOCK, &mask, NULL);

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
#if !TARGET_OS_DARWIN
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
#if defined(__OpenBSD__)
	int sig = 0;
	sigwait(&mask, &sig);
	if (sig ==SIGCHLD) {
		diag_set(IllegalParams, "logger process died");
		return -1;
	}
#else
	if (sigtimedwait(&mask, NULL, &timeout) == SIGCHLD) {
		diag_set(IllegalParams, "logger process died");
		return -1;
	}
#endif
#endif
	/* OK, let's hope for the best. */
	pthread_sigmask(SIG_UNBLOCK, &mask, NULL);
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
	if (fd < 0) {
		diag_set(SystemError, "socket");
		return -1;
	}
	struct sockaddr_un un;
	memset(&un, 0, sizeof(un));
	snprintf(un.sun_path, sizeof(un.sun_path), "%s", path);
	un.sun_family = AF_UNIX;
	if (connect(fd, (struct sockaddr *) &un, sizeof(un)) != 0) {
		diag_set(SystemError, "connect");
		close(fd);
		return -1;
	}
	return fd;
}

/**
 * Connect to remote syslogd using server:port.
 * @param server_address address of remote host.
 * @retval not 0 Socket descriptor.
 * @retval    -1 Socket error.
 */
static int
syslog_connect_remote(const char *server_address)
{
	struct addrinfo *inf, hints, *ptr;
	char *remote;
	char buf[10];
	char *portnum, *copy;
	int fd = -1;
	int ret;

	copy = strdup(server_address);
	if (copy == NULL) {
		diag_set(OutOfMemory, strlen(server_address), "malloc",
			 "stslog server address");
		return fd;
	}
	portnum = copy;
	remote = strsep(&portnum, ":");
	if (portnum == NULL) {
		snprintf(buf, sizeof(buf), "%d", SAY_SYSLOG_DEFAULT_PORT);
		portnum = buf;
	}
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;

	ret = getaddrinfo(remote, portnum, &hints, &inf);
	if (ret != 0) {
		errno = EIO;
		diag_set(SystemError, "getaddrinfo: %s",
			 gai_strerror(ret));
		goto out;
	}
	for (ptr = inf; ptr; ptr = ptr->ai_next) {
		fd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
		if (fd < 0) {
			diag_set(SystemError, "socket");
			continue;
		}
		if (connect(fd, inf->ai_addr, inf->ai_addrlen) != 0) {
			close(fd);
			fd = -1;
			diag_set(SystemError, "connect");
			continue;
		}
		break;
	}
	freeaddrinfo(inf);
out:
	free(copy);
	return fd;
}

static inline int
log_syslog_connect(struct log *log)
{
	/*
	 * If server option is not set use '/dev/log' for Linux and
	 * '/var/run/syslog' for Mac.
	 */
	switch (log->syslog_server_type) {
	case SAY_SYSLOG_UNIX:
		log->fd = syslog_connect_unix(log->path);
		break;
	case SAY_SYSLOG_REMOTE:
		log->fd = syslog_connect_remote(log->path);
		break;
	default:
		log->fd = syslog_connect_unix("/dev/log");
		if (log->fd < 0)
			log->fd = syslog_connect_unix("/var/run/syslog");

	}
	return log->fd;
}

/** Initialize logging to syslog */
static int
log_syslog_init(struct log *log, const char *init_str)
{
	struct say_syslog_opts opts;
	log->type = SAY_LOGGER_SYSLOG;

	if (say_parse_syslog_opts(init_str, &opts) < 0)
		return -1;

	log->syslog_server_type = opts.server_type;
	if (log->syslog_server_type != SAY_SYSLOG_DEFAULT)
		log->path = xstrdup(opts.server_path);
	if (opts.identity == NULL)
		log->syslog_ident = xstrdup("tarantool");
	else
		log->syslog_ident = xstrdup(opts.identity);

	if (opts.facility == syslog_facility_MAX)
		log->syslog_facility = SYSLOG_LOCAL7;
	else
		log->syslog_facility = opts.facility;
	say_free_syslog_opts(&opts);
	log->fd = log_syslog_connect(log);
	if (log->fd < 0) {
		diag_log();
		/* syslog indent is freed in atexit(). */
		diag_set(SystemError, "syslog logger");
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
	log->format_func = say_format_plain;
	log->level = S_INFO;
	log->rotating_threads = 0;
	tt_pthread_mutex_init(&log->rotate_mutex, NULL);
	tt_pthread_cond_init(&log->rotate_cond, NULL);
	setvbuf(stderr, NULL, _IONBF, 0);

	if (init_str != NULL) {
		enum say_logger_type type;
		if (say_parse_logger_type(&init_str, &type))
			return -1;

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
		log_set_nonblock(log);
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

bool
say_logger_initialized(void)
{
	return log_default == &log_std;
}

void
say_logger_init(const char *init_str, int level, int nonblock,
		const char *format)
{
	/*
	 * The logger may be early configured
	 * by hands without configuing the whole box.
	 */
	if (say_logger_initialized()) {
		say_set_log_level(level);
		say_set_log_format(say_format_by_name(format));
		return;
	}

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
	log_pid = log_default->pid;
	say_set_log_format(say_format_by_name(format));

	return;
fail:
	diag_log();
	panic("failed to initialize logging subsystem");
}

int
say_set_background(void)
{
	assert(say_logger_initialized());

	if (log_background)
		return 0;

	log_background = true;

	fflush(stderr);
	fflush(stdout);

	int fd;
	int fd_null = -1;
	if (log_default->fd == STDERR_FILENO) {
		fd_null = open("/dev/null", O_WRONLY);
		if (fd_null < 0) {
			diag_set(SystemError, "open(/dev/null)");
			return -1;
		}
		fd = fd_null;
	} else {
		fd = log_default->fd;
	}

	dup2(fd, STDERR_FILENO);
	dup2(fd, STDOUT_FILENO);
	if (fd_null != -1)
		close(fd_null);

	return 0;
}

void
say_logger_free(void)
{
	if (say_logger_initialized())
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
say_format_boot(struct log *log, char *buf, int len, int level,
		const char *module, const char *filename, int line,
		const char *error, const char *format, va_list ap)
{
	(void)log;
	(void)module;
	(void)filename;
	(void)line;
	(void)level;
	int total = 0;
	SNPRINT(total, vsnprintf, buf, len, format, ap);
	if (error != NULL)
		SNPRINT(total, snprintf, buf, len, ": %s", error);
	SNPRINT(total, snprintf, buf, len, "\n");
	return total;
}

/**
 * Format the log message in Tarantool format:
 * YYYY-MM-DD hh:mm:ss.ms [PID]: CORD/FID/FIBERNAME/MODULENAME LEVEL> MSG
 */
int
say_format_plain(struct log *log, char *buf, int len, int level,
		 const char *module, const char *filename, int line,
		 const char *error, const char *format, va_list ap)
{
	int total = 0;

	/*
	 * Every message written to syslog has a header that contains
	 * the current time and pid (see format_syslog_header) so we
	 * exclude them from the message body.
	 */
	if (log->type != SAY_LOGGER_SYSLOG) {
		struct tm tm;
		double tm_sec;
		get_current_time(&tm, &tm_sec);
		/* Print time in format 2012-08-07 18:30:00.634 */
		SNPRINT(total, strftime, buf, len, "%F %H:%M", &tm);
		SNPRINT(total, snprintf, buf, len, ":%06.3f ", tm_sec);
		/* Print pid */
		SNPRINT(total, snprintf, buf, len, "[%i] ", getpid());
	}

	SNPRINT(total, snprintf, buf, len, "%s", cord()->name);
	if (fiber() && fiber()->fid != FIBER_ID_SCHED) {
		SNPRINT(total, snprintf, buf, len, "/%llu/%s",
			(long long)fiber()->fid, fiber_name(fiber()));
	}

	if (module != NULL)
		SNPRINT(total, snprintf, buf, len, "/%s", module);

	/* Primitive basename(filename) */
	if (filename != NULL) {
		for (const char *f = filename; *f; f++)
			if (*f == '/' && *(f + 1) != '\0')
				filename = f + 1;
		SNPRINT(total, snprintf, buf, len, " %s:%i", filename, line);
	}

	SNPRINT(total, snprintf, buf, len, " %c> ", level_chars[level]);

	SNPRINT(total, vsnprintf, buf, len, format, ap);
	if (error != NULL)
		SNPRINT(total, snprintf, buf, len, ": %s", error);

	SNPRINT(total, snprintf, buf, len, "\n");
	return total;
}

/**
 * Format log message in json format:
 * {"time": "2024-12-23T20:04:26.491+0300", "level": "WARN",
 * "message": <message>, "pid": <pid>, "cord_name": <name>, "fiber_id": <id>,
 * "fiber_name": <fiber_name>, file": <filename>, "line": <fds>,
 * "module": <module_name>}
 */
int
say_format_json(struct log *log, char *buf, int len, int level,
		const char *module, const char *filename, int line,
		const char *error, const char *format, va_list ap)
{
	(void)log;

	int total = 0;
	char *buf_end = buf + len;

	/*
	 * Print time in format YYYY-MM-DDThh:mm:ss.ms+tz_offset.
	 * For example 2024-12-23T20:04:26.491+0300.
	 */
	SNPRINT(total, snprintf, buf, len, "{\"time\": \"");
	struct tm tm;
	double tm_sec;
	get_current_time(&tm, &tm_sec);
	int written = strftime(buf, len, "%FT%H:%M", &tm);
	buf += written, len -= written, total += written;
	SNPRINT(total, snprintf, buf, len, ":%06.3f", tm_sec);
	written = strftime(buf, len, "%z", &tm);
	buf += written, len -= written, total += written;
	SNPRINT(total, snprintf, buf, len, "\", ");

	/* Print level. */
	SNPRINT(total, snprintf, buf, len, "\"level\": \"%s\", ",
		level_strs[level]);

	/*
	 * Remember where the message starts for now, will write the message,
	 * after writing the rest of the context and moving it to the end of
	 * the buffer.
	 * Currently our buffer looks like this:
	 * | head \0| garbage |
	 *         ^ buf       ^ buf_end
	 */
	char *msg_ptr = buf;
	const int head_len = total;

	/* Print error, if any. */
	if (error) {
		SNPRINT(total, snprintf, buf, len, "\"error\": \"");
		SNPRINT(total, json_escape, buf, len, error);
		SNPRINT(total, snprintf, buf, len, "\", ");
	}

	/* Print PID, cord name, fiber id and fiber name. */
	SNPRINT(total, snprintf, buf, len, "\"pid\": %i ", getpid());
	SNPRINT(total, snprintf, buf, len, ", \"cord_name\": \"");
	SNPRINT(total, json_escape, buf, len, cord()->name);
	SNPRINT(total, snprintf, buf, len, "\"");
	if (fiber() && fiber()->fid != FIBER_ID_SCHED) {
		SNPRINT(total, snprintf, buf, len, ", \"fiber_id\": %llu, ",
			(long long)fiber()->fid);
		SNPRINT(total, snprintf, buf, len, "\"fiber_name\": \"");
		SNPRINT(total, json_escape, buf, len, fiber()->name);
		SNPRINT(total, snprintf, buf, len, "\"");
	}

	/* Print filename, line and module name if any. */
	if (filename) {
		SNPRINT(total, snprintf, buf, len, ", \"file\": \"");
		SNPRINT(total, json_escape, buf, len, filename);
		SNPRINT(total, snprintf, buf, len, "\", \"line\": %i", line);
	}
	if (module != NULL) {
		SNPRINT(total, snprintf, buf, len, ", \"module\": \"");
		SNPRINT(total, json_escape, buf, len, module);
		SNPRINT(total, snprintf, buf, len, "\"");
	}

	SNPRINT(total, snprintf, buf, len, "}\n");

	/*
	 * Finished printing context for the log entry. Will move tail to the
	 * end of the buffer.
	 * The buffer looks like this:
	 * | head |   tail  \0| garbage |
	 *         ^ msg_ptr ^ buf       ^ buf_end
	 */

	const int tail_len = total - head_len;
	char *tail_ptr = buf_end - tail_len - 1; /* Reserve space for '\0'. */

	/* Move the tail of the context to the end of the buffer. */
	memmove(tail_ptr, msg_ptr, tail_len);
	tail_ptr[tail_len] = '\0';
	int msg_cap = tail_ptr - msg_ptr;

	/* After moving the tail, the buffer looks like this:
	 * | head |   garbage   |   tail   \0|
	 *         ^ msg_ptr     ^ tail_ptr   ^ buf_end
	 */

	/* Write the message. */
	if (strncmp(format, "json", sizeof("json")) == 0) {
		/*
		 * Message is already JSON-formatted.
		 * Get rid of {} brackets and print it as-is.
		 */
		const char *str = va_arg(ap, const char *);
		assert(str != NULL);
		int str_len = strlen(str);
		assert(str_len > 2 && str[0] == '{' && str[str_len - 1] == '}');

		/*
		 * Can't use SNPRINT macro here, because it sets the pointer to
		 * null, if the message didn't fit into the provided buffer,
		 * but we still need to use the pointer after.
		 */
		int msg_len = snprintf(msg_ptr, msg_cap,
				       "%.*s, ", str_len - 2, str + 1);

		if (msg_len < 0) {
			return -1;
		}

		msg_len = MIN(msg_len, msg_cap);
		len -= msg_len;
		total += msg_len;
		msg_ptr += msg_len;
	} else {
		/* Print message header. */
		SNPRINT(total, snprintf, msg_ptr, msg_cap, "\"message\": \"");

		static const char msg_tail[] = "\", ";
		msg_cap -= strlen(msg_tail);

		/*
		 * Print the message.
		 * Need to cast msg_cap to unsigned, because otherwise
		 * during LTO build stringop-overread warning is triggered.
		 * Can't use SNPRINT macro here, because it sets the pointer to
		 * null, if the message didn't fit into the provided buffer,
		 * but we still need to use the pointer after.
		 */
		vsnprintf(msg_ptr, (unsigned)msg_cap, format, ap);

		/* Escape the message. */
		int msg_len = json_escape_inplace(msg_ptr, msg_cap);
		len -= msg_len;
		total += msg_len;
		msg_ptr += msg_len;

		/* Print message tail. */
		SNPRINT(total, snprintf, msg_ptr, len, msg_tail);
	}

	/*
	 * We now will move the tail with '\0' to the end of the message.
	 * After escaping the message, the buffer looks like this:
	 * | head | message \0| garbage |   tail   \0|
	 *                   ^ msg_ptr   ^ tail_ptr   ^ buf_end
	 */
	memmove(msg_ptr, tail_ptr, tail_len + 1);

	return total;
}

/** Wrapper around log->format_func to be used with SNPRINT. */
static int
format_func_adapter(char *buf, int len, struct log *log, int level,
		    const char *module, const char *filename, int line,
		    const char *error, const char *format, va_list ap)
{
	return log->format_func(log, buf, len, level, module, filename, line,
				error, format, ap);
}

/**
 * Format the header of a log message in syslog format.
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
format_syslog_header(char *buf, int len, int level,
		     enum syslog_facility facility, const char *ident)
{
	struct tm tm;
	double tm_sec;
	get_current_time(&tm, &tm_sec);

	int total = 0;

	/* Format syslog header according to RFC */
	int prio = (facility << 3) | level_to_syslog_priority(level);
	SNPRINT(total, snprintf, buf, len, "<%d>", prio);
	SNPRINT(total, strftime, buf, len, "%h %e %T ", &tm);
	SNPRINT(total, snprintf, buf, len, "%s[%d]: ", ident,
		getpid());
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
static __thread char say_buf[SAY_BUF_LEN_MAX];

/**
 * Wrapper over write which ensures, that writes not more than buffer size.
 */
static ssize_t
safe_write(int fd, const char *buf, int size)
{
	assert(size >= 0);
	/* Writes at most SAY_BUF_LEN_MAX - 1
	 * (1 byte was taken for 0 byte in vsnprintf).
	 */
	return write(fd, buf, MIN(size, SAY_BUF_LEN_MAX - 1));
}

/**
 * Common part of say_default() and say_from_lua().
 */
static void
say_internal(int level, bool check_level, const char *module,
	     const char *filename, int line, const char *error,
	     const char *format, va_list ap)
{
	int errsv = errno;
	int total = log_vsay(log_default, level, check_level, module, filename,
			     line, error, format, ap);
	if (total > 0 &&
	    level == S_FATAL && log_default->fd != STDERR_FILENO) {
		ssize_t r = safe_write(STDERR_FILENO, say_buf, total);
		(void)r;
	}
	errno = errsv; /* Preserve the errno. */
}

/**
 * Default say function.
 */
static void
say_default(int level, const char *filename, int line, const char *error,
	    const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	if (level <= log_level_flightrec) {
		assert(log_write_flightrec != NULL);
		int errsv = errno;
		va_list ap_copy;
		va_copy(ap_copy, ap);
		log_write_flightrec(level, filename, line, error, format,
				    ap_copy);
		va_end(ap_copy);
		errno = errsv; /* Preserve the errno. */
	}
	say_internal(level, true, NULL, filename, line, error, format, ap);
	va_end(ap);
}

/**
 * Format and print a message to the default Tarantool log.
 * This function is used by Lua say().
 * Unlike say_default(), it doesn't compare level against log->level, the check
 * is performed in Lua, because each module can have its own log level.
 * Also it takes module name, and doesn't take an error argument.
 */
void
say_from_lua(int level, const char *module, const char *filename, int line,
	     const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	say_internal(level, false, module, filename, line, NULL, format, ap);
	va_end(ap);
}

/**
 * A variadic wrapper over log_write_flightrec(), used by Lua say().
 */
void
log_write_flightrec_from_lua(int level, const char *filename, int line, ...)
{
	assert(log_write_flightrec != NULL);
	int errsv = errno;
	va_list ap;
	va_start(ap, line);
	/* ap contains a single string, which is already formatted. */
	log_write_flightrec(level, filename, line, NULL, "%s", ap);
	errno = errsv; /* Preserve the errno. */
	va_end(ap);
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
	ssize_t r = safe_write(log->fd, say_buf, total);
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
	if (log->fd < 0 || safe_write(log->fd, say_buf, total) <= 0) {
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
			log_set_nonblock(log);
			/*
			 * In a case or error the log message is
			 * lost. We can not wait for connection -
			 * it would block thread. Try to reconnect
			 * on next vsay().
			 */
			ssize_t r = safe_write(log->fd, say_buf, total);
			(void) r;               /* silence gcc warning */
		}
	}
}

/** Loggers }}} */

/*
 * Init string parser(s)
 */

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
	else {
		diag_set(IllegalParams, logger_syntax_reminder);
		return -1;
	}
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
	opts->server_path = NULL;
	opts->server_type = SAY_SYSLOG_DEFAULT;
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
		if (say_parse_prefix(&value, "server=")) {
			if (opts->server_path != NULL ||
			    opts->server_type != SAY_SYSLOG_DEFAULT)
				goto duplicate;
			if (say_parse_prefix(&value, "unix:")) {
				opts->server_type = SAY_SYSLOG_UNIX;
				opts->server_path = value;
			} else {
				opts->server_type = SAY_SYSLOG_REMOTE;
				opts->server_path = value;
			}
		} else if (say_parse_prefix(&value, "identity=")) {
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
	tt_pthread_mutex_lock(&log->rotate_mutex);
	while(log->rotating_threads > 0)
		tt_pthread_cond_wait(&log->rotate_cond, &log->rotate_mutex);
	tt_pthread_mutex_unlock(&log->rotate_mutex);
	/*
	 * Do not close stderr because it's used for reporting crashes
	 * and memory leaks.
	 */
	if (log->type != SAY_LOGGER_STDERR)
		close(log->fd);
	free(log->syslog_ident);
	free(log->path);
	rlist_del_entry(log, in_log_list);
	tt_pthread_mutex_destroy(&log->rotate_mutex);
	tt_pthread_cond_destroy(&log->rotate_cond);
}

/**
 * Format a line of log.
 */
static int
format_log_entry(struct log *log, int level, const char *module,
		 const char *filename, int line, const char *error,
		 const char *format, va_list ap)
{
	int total = 0;
	char *buf = say_buf;
	int len = SAY_BUF_LEN_MAX;

	if (log->type == SAY_LOGGER_SYSLOG) {
		SNPRINT(total, format_syslog_header, buf, len,
			level, log->syslog_facility, log->syslog_ident);
	}
	SNPRINT(total, format_func_adapter, buf, len, log, level, module,
		filename, line, error, format, ap);

	return total;
}

int
log_vsay(struct log *log, int level, bool check_level, const char *module,
	 const char *filename, int line, const char *error, const char *format,
	 va_list ap)
{
	int errsv = errno;
	int total = 0;

	assert(level >= 0 && level < say_level_MAX);

	if (check_level && level > log->level)
		goto out;

	total = format_log_entry(log, level, module, filename, line, error,
				 format, ap);
	if (total <= 0)
		goto out;

	switch (log->type) {
	case SAY_LOGGER_FILE:
	case SAY_LOGGER_PIPE:
		write_to_file(log, total);
		break;
	case SAY_LOGGER_STDERR:
		if (before_stderr_callback != NULL)
			before_stderr_callback();
		write_to_file(log, total);
		if (after_stderr_callback != NULL)
			after_stderr_callback();
		break;
	case SAY_LOGGER_SYSLOG:
		write_to_syslog(log, total);
		if (level == S_FATAL && log->fd != STDERR_FILENO)
			(void)safe_write(STDERR_FILENO, say_buf, total);
		break;
	case SAY_LOGGER_BOOT:
	{
		if (before_stderr_callback != NULL)
			before_stderr_callback();
		ssize_t r = safe_write(STDERR_FILENO, say_buf, total);
		(void) r;                       /* silence gcc warning */
		if (after_stderr_callback != NULL)
			after_stderr_callback();
		break;
	}
	default:
		unreachable();
	}
out:
	errno = errsv; /* Preserve the errno. */
	return total;
}

void
say_set_stderr_callback(say_stderr_callback_t before,
			say_stderr_callback_t after)
{
	before_stderr_callback = before;
	after_stderr_callback = after;
}
