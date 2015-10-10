/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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

#include "fiber.h"

char log_path[PATH_MAX + 1];
int log_fd = STDERR_FILENO;
static int logger_nonblock;
pid_t logger_pid;
static bool booting = true;
static bool logger_background = true;
static const char *binary_filename;
int log_level = S_INFO;

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
	case S_DEBUG:
		return 'D';
	default:
		return '_';
	}
}

void
say_init(const char *argv0)
{
	binary_filename = strdup(argv0);
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
void
say_init_pipe()
{
	int pipefd[2];
	char cmd[] = { "/bin/sh" };
	char args[] = { "-c" };
	char *argv[] = { cmd, args, (char *) log_path, NULL };
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
	logger_pid = fork();
	if (logger_pid == -1) {
		say_syserror("pipe");
		goto error;
	}

	if (logger_pid == 0) {
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
	return;
error:
	say_syserror("Can't start logger: %s", log_path);
	_exit(EXIT_FAILURE);
}

/**
 * Rotate logs on SIGHUP
 */
void
say_logrotate(int signo)
{
	(void) signo;
	/* For cases when used from a Lua FFI binding */
	if (logger_pid || strlen(log_path) == 0)
		return;
	close(log_fd);
	log_fd = open(log_path, O_WRONLY | O_APPEND | O_CREAT,
		      S_IRUSR | S_IWUSR | S_IRGRP);
	if (log_fd < 0)
		return;

	if (logger_background) {
		dup2(log_fd, STDOUT_FILENO);
		dup2(log_fd, STDERR_FILENO);
	}
	if (logger_nonblock) {
		int flags = fcntl(log_fd, F_GETFL, 0);
		fcntl(log_fd, F_SETFL, flags | O_NONBLOCK);
	}
	say_info("log file has been reopened");
}

/**
 * Initialize logging to a file and set up a log
 * rotation signal.
 */
void
say_init_file()
{
	int fd = open(log_path, O_WRONLY|O_APPEND|O_CREAT,
		      S_IRUSR | S_IWUSR | S_IRGRP);
	if (fd < 0) {
		say_syserror("Can't open log file: %s", log_path);
		_exit(EXIT_FAILURE);
	}
	signal(SIGHUP, say_logrotate);
	log_fd = fd;
}

/**
 * Initialize logging subsystem to use in daemon mode.
 */
void
say_logger_init(const char *path, int level, int nonblock, int background)
{
	log_level = level;
	logger_nonblock = nonblock;
	logger_background = background;
	setvbuf(stderr, NULL, _IONBF, 0);

	if (path != NULL) {
		if (path[0] == '|') {
			snprintf(log_path, sizeof(log_path), "%s", path + 1);
			say_init_pipe();
		} else {
			snprintf(log_path, sizeof(log_path), "%s", path);
			say_init_file();
		}
		if (background) {
			dup2(log_fd, STDERR_FILENO);
			dup2(log_fd, STDOUT_FILENO);
		}
	}
	if (nonblock) {
		int flags = fcntl(log_fd, F_GETFL, 0);
		fcntl(log_fd, F_SETFL, flags | O_NONBLOCK);
	}
	booting = false;
}

void
vsay(int level, const char *filename, int line, const char *error, const char *format, va_list ap)
{
	size_t p = 0, len = PIPE_BUF;
	const char *f;
	static __thread char buf[PIPE_BUF];

	if (booting) {
		fprintf(stderr, "%s: ", binary_filename);
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

	/* Print time in format 2012-08-07 18:30:00.634 */
	p += strftime(buf + p, len - p, "%F %H:%M", &tm);
	p += snprintf(buf + p, len - p, ":%06.3f",
		      now - now_seconds + tm.tm_sec);
	struct cord *cord = cord();
	p += snprintf(buf + p, len - p, " [%i]", getpid());
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

	int r = write(log_fd, buf, p + 1);
	(void)r;

	if (level == S_FATAL && log_fd != STDERR_FILENO) {
		r = write(STDERR_FILENO, buf, p + 1);
		(void)r;
	}
}

static void
sayf(int level, const char *filename, int line, const char *error, const char *format, ...)
{
	int errsv = errno; /* Preserve the errno. */
	if (log_level < level)
		return;
	va_list ap;
	va_start(ap, format);
	vsay(level, filename, line, error, format, ap);
	va_end(ap);
	errno = errsv; /* Preserve the errno. */
}
