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
#ifndef PIPE_BUF
#include <sys/param.h>
#endif
#include <syslog.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>

pid_t log_pid = 0;
int log_level = S_INFO;

static const char logger_syntax_reminder[] =
	"expecting a file name or a prefix, such as '|', 'pipe:', 'syslog:'";

static bool booting = true;
static enum say_logger_type logger_type = SAY_LOGGER_STDERR;
static bool logger_background = true;
static int logger_nonblock;

static int log_fd = STDERR_FILENO;
static char *log_path; /* iff logger_type == SAY_LOGGER_FILE */
/* Application identifier used to group syslog messages. */
static char *syslog_ident = NULL;

static void
sayf(int level, const char *filename, int line, const char *error,
     const char *format, ...);

sayfunc_t _say = sayf;

static char
level_to_char(int level)
{
	switch (level) {
	case S_FATAL:
		return 'F';
	case S_SYSERROR:
		return '!';
	case S_ERROR:
		return 'E';
	case S_CRIT:
		return 'C';
	case S_WARN:
		return 'W';
	case S_INFO:
		return 'I';
	case S_VERBOSE:
		return 'V';
	case S_DEBUG:
		return 'D';
	default:
		return '_';
	}
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

/**
 * Initialize the logger pipe: a standalone
 * process which is fed all log messages.
 */
static void
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
		say_syserror("pipe");
		goto error;
	}

	/* flush buffers to avoid multiple output */
	/* https://github.com/tarantool/tarantool/issues/366 */
	fflush(stdout);
	fflush(stderr);
	log_pid = fork();
	if (log_pid == -1) {
		say_syserror("pipe");
		goto error;
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
		goto error;
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
	if (sigtimedwait(&mask, NULL, &timeout) == SIGCHLD)
		goto error;
#endif
	/* OK, let's hope for the best. */
	sigprocmask(SIG_UNBLOCK, &mask, NULL);
	close(pipefd[0]);
	log_fd = pipefd[1];
	say_info("started logging into a pipe, SIGHUP log rotation disabled");
	logger_type = SAY_LOGGER_PIPE;
	return;
error:
	say_syserror("can't start logger: %s", init_str);
	exit(EXIT_FAILURE);
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
static void
say_file_init(const char *init_str)
{
	int fd;
	log_path = abspath(init_str);
	if (log_path == NULL)
		panic("out of memory");
	fd = open(log_path, O_WRONLY | O_APPEND | O_CREAT,
	          S_IRUSR | S_IWUSR | S_IRGRP);
	if (fd < 0) {
		say_syserror("can't open log file: %s", log_path);
		exit(EXIT_FAILURE);
	}
	log_fd = fd;
	signal(SIGHUP, say_logrotate); /* will access log_fd */
	logger_type = SAY_LOGGER_FILE;
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
static void
say_syslog_init(const char *init_str)
{
	char *error;
	struct say_syslog_opts opts;

	if (say_parse_syslog_opts(init_str, &opts, &error)) {
		say_syserror("syslog logger: %s",
			     error ? error : "out of memory");
		free(error);
		exit(EXIT_FAILURE);
	}

	if (opts.identity == NULL)
		syslog_ident = strdup("tarantool");
	else
		syslog_ident = strdup(opts.identity);
	say_free_syslog_opts(&opts);
	log_fd = say_syslog_connect();
	if (log_fd < 0) {
		/* syslog indent is freed in atexit(). */
		say_syserror("syslog logger: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
	say_info("started logging to syslog, SIGHUP log rotation disabled");
	logger_type = SAY_LOGGER_SYSLOG;
	return;
}

/**
 * Initialize logging subsystem to use in daemon mode.
 */
void
say_logger_init(const char *init_str, int level, int nonblock, int background)
{
	log_level = level;
	logger_nonblock = nonblock;
	logger_background = background;
	setvbuf(stderr, NULL, _IONBF, 0);

	if (init_str != NULL) {
		enum say_logger_type type;
		if (say_parse_logger_type(&init_str, &type)) {
			say_syserror("logger: bad initialization string: %s, %s",
				     init_str, logger_syntax_reminder);
			exit(EXIT_FAILURE);
		}
		switch (type) {
		case SAY_LOGGER_PIPE:
			say_pipe_init(init_str);
			break;
		case SAY_LOGGER_SYSLOG:
			say_syslog_init(init_str);
			break;
		case SAY_LOGGER_FILE:
		default:
			say_file_init(init_str);
			break;
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
	}

	if (background) {
		fflush(stderr);
		fflush(stdout);
		if (log_fd == STDERR_FILENO) {
			int fd = open("/dev/null", O_WRONLY);
			if (fd < 0)
				exit(EXIT_FAILURE);
			dup2(fd, STDERR_FILENO);
			dup2(fd, STDOUT_FILENO);
			close(fd);
		} else {
			dup2(log_fd, STDERR_FILENO);
			dup2(log_fd, STDOUT_FILENO);
		}
	}
	booting = false;
}

void
say_logger_free()
{
	if (logger_type == SAY_LOGGER_SYSLOG && log_fd != -1)
		close(log_fd);
	free(syslog_ident);
}

/**
 * Encode log message header, using syslog protocol. See RFC 5424
 * and RFC 3164. The 3164 is compatible with 5424, so it is
 * implemented.
 * Protocol:
 * <PRIORITY_VALUE>TIMESTAMP IDENTATION[PID]: MSG
 * - Priority value is encoded as message subject * 8 and bitwise
 *   OR with message level;
 * - Timestamp must be encoded in the format: Mmm dd hh:mm:ss;
 *   Mmm - moth abbreviation;
 * - Identation is application name. By default it is "tarantool";
 * - Pid - process identifier;
 * - Msg - log message in any format. Encoded in vsay().
 *
 * @param level Tarantool log level.
 * @param buf Buffer to encode to.
 * @param len Length of @a buf.
 * @param tm Timestamp.
 * @param pid Process identifier.
 *
 * @retval Number of encoded bytes.
 */
static int
say_encode_syslog_header(int level, char *buf, int len, struct tm *tm, int pid)
{
	int prio = level_to_syslog_priority(level);
	int pos = 0;
	pos += snprintf(buf + pos, len - pos, "<%d>", LOG_MAKEPRI(1, prio));
	pos += strftime(buf + pos, len - pos, "%h %e %T ", tm);
	pos += snprintf(buf + pos, len - pos, "%s[%d]:", syslog_ident, pid);
	return pos;
}

/**
 * Encode log message header using tarantool protocol:
 * TIMESTAMP [PID]: MSG
 * - Timestamp must be encoded in the format:
 *   yyyy-mm-dd hh:mm:ss.ms;
 * - Pid - process identifier;
 * - Msg - log message in any format. Encoded in vsay().
 *
 * @param buf Buffer to encode to.
 * @param len Length of @a buf.
 * @param tm Timestamp.
 * @param now Libev timestamp.
 * @param now_seconds Seconds part of @a now.
 * @param pid Process identifier.
 *
 * @retval Number of encoded bytes.
 */
static int
say_encode_tarantool_header(char *buf, int len, struct tm *tm, ev_tstamp now,
			    time_t now_seconds, int pid)
{
	int pos = 0;
	/* Print time in format 2012-08-07 18:30:00.634 */
	pos += strftime(buf + pos, len - pos, "%F %H:%M", tm);
	pos += snprintf(buf + pos, len - pos, ":%06.3f",
			now - now_seconds + tm->tm_sec);
	pos += snprintf(buf + pos, len - pos, " [%i]", pid);
	return pos;
}

void
vsay(int level, const char *filename, int line, const char *error,
     const char *format, va_list ap)
{
	size_t p = 0, len = PIPE_BUF;
	const char *f;
	static __thread char buf[PIPE_BUF];
	int pid = getpid();

	if (booting) {
		vfprintf(stderr, format, ap);
		if (error)
			fprintf(stderr, ": %s", error);
		fprintf(stderr, "\n");
		return;
	}

	for (f = filename; *f; f++)
		if (*f == '/' && *(f + 1) != '\0')
			filename = f + 1;

	/* Don't use ev_now() since it requires a working event loop. */
	ev_tstamp now = ev_time();
	time_t now_seconds = (time_t) now;
	struct tm tm;
	localtime_r(&now_seconds, &tm);
	if (logger_type != SAY_LOGGER_SYSLOG) {
		p += say_encode_tarantool_header(buf + p, len - p, &tm, now,
						 now_seconds, pid);
	} else {
		p += say_encode_syslog_header(level, buf + p, len - p, &tm,
					      pid);
	}

	struct cord *cord = cord();
	if (cord) {
		p += snprintf(buf + p, len - p, " %s", cord->name);
		if (fiber() && fiber()->fid != 1) {
			p += snprintf(buf + p, len - p, "/%i/%s",
				      fiber()->fid, fiber_name(fiber()));
		}
	}

	if (level == S_WARN || level == S_ERROR || level == S_SYSERROR)
		p += snprintf(buf + p, len - p, " %s:%i", filename, line);

	p += snprintf(buf + p, len - p, " %c> ", level_to_char(level));
	/* until here it is guaranteed that p < len */

	p += vsnprintf(buf + p, len - p, format, ap);
	if (error && p < len - 1)
		p += snprintf(buf + p, len - p, ": %s", error);

	if (p >= len - 1)
		p = len - 1;
	*(buf + p) = '\n';
	if (logger_type != SAY_LOGGER_SYSLOG) {
		(void) write(log_fd, buf, p + 1);
	} else if (log_fd < 0 || write(log_fd, buf, p + 1) <= 0) {
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
			(void) write(log_fd, buf, p + 1);
		}
	}

	if (level == S_FATAL && log_fd != STDERR_FILENO)
		(void) write(STDERR_FILENO, buf, p + 1);
}

static void
sayf(int level, const char *filename, int line, const char *error,
     const char *format, ...)
{
	int errsv = errno; /* Preserve the errno. */
	if (!say_log_level_is_enabled(level))
		return;
	va_list ap;
	va_start(ap, format);
	vsay(level, filename, line, error, format, ap);
	va_end(ap);
	errno = errsv; /* Preserve the errno. */
}

/*
 * Init string parser(s)
 */

int
say_check_init_str(const char *str, char **error)
{
	enum say_logger_type type;
	if (say_parse_logger_type(&str, &type)) {
		*error = strdup(logger_syntax_reminder);
		return -1;
	}
	if (type == SAY_LOGGER_SYSLOG) {
		struct say_syslog_opts opts;

		if (say_parse_syslog_opts(str, &opts, error))
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
say_parse_syslog_opts(const char *init_str,
		      struct say_syslog_opts *opts,
		      char **err)
{
	opts->identity = NULL;
	opts->facility = NULL;
	opts->copy = strdup(init_str);
	if (opts->copy == NULL) {
		*err = NULL;
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
			if (asprintf(err, "bad option '%s'", option) == -1)
				*err = NULL;
			goto error;
		}
	}
	return 0;
duplicate:
	/* Terminate the "bad" option, by overwriting '=' sign */
	((char *)value)[-1] = '\0';
	if (asprintf(err, "duplicate option '%s'", option) == -1)
		*err = NULL;
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
