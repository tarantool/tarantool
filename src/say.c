/*
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

int sayfd = STDERR_FILENO;
pid_t logger_pid;
static bool booting = true;
static const char *binary_filename;
static int log_level_default = S_INFO;
static int *log_level = &log_level_default;

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
say_logger_init(char *logger, int *level, int nonblock)
{
	log_level = level;
	int pipefd[2];
	pid_t pid;
	char cmd[] = { "/bin/sh" };
	char args[] = { "-c" };
	char *argv[] = { cmd, args, logger, NULL };
	char *envp[] = { NULL };
	setvbuf(stderr, NULL, _IONBF, 0);

	if (logger != NULL) {
		sigset_t mask;
		sigemptyset(&mask);
		sigaddset(&mask, SIGCHLD);

		if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
			say_syserror("sigprocmask");

		if (pipe(pipefd) == -1) {
			say_syserror("pipe");
			goto error;
		}

		pid = fork();
		if (pid == -1) {
			say_syserror("pipe");
			goto error;
		}

		if (pid == 0) {
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
			execve(argv[0], argv, envp);
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
		dup2(pipefd[1], STDERR_FILENO);
		dup2(pipefd[1], STDOUT_FILENO);
		sayfd = pipefd[1];

		logger_pid = pid;
	} else {
		sayfd = STDERR_FILENO;
	}
	booting = false;
	if (nonblock) {
		int flags = fcntl(sayfd, F_GETFL, 0);
		fcntl(sayfd, F_SETFL, flags | O_NONBLOCK);
	}
	return;
error:
	say_syserror("Can't start logger: %s", logger);
	_exit(EXIT_FAILURE);
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

	if (level == S_WARN || level == S_ERROR)
		p += snprintf(buf + p, len - p, " %s:%i", filename, line);

	p += snprintf(buf + p, len - p, " %c> ", level_to_char(level));
	/* until here it is guaranteed that p < len */

	p += vsnprintf(buf + p, len - p, format, ap);
	if (error && p < len - 1)
		p += snprintf(buf + p, len - p, ": %s", error);
	if (p >= len - 1)
		p = len - 1;
	*(buf + p) = '\n';

	int r = write(sayfd, buf, p + 1);
	(void)r;

	if (S_FATAL && sayfd != STDERR_FILENO) {
		r = write(STDERR_FILENO, buf, p + 1);
		(void)r;
	}
}

static void
sayf(int level, const char *filename, int line, const char *error, const char *format, ...)
{
	int errsv = errno; /* Preserve the errno. */
	if (*log_level < level)
		return;
	va_list ap;
	va_start(ap, format);
	vsay(level, filename, line, error, format, ap);
	va_end(ap);
	errno = errsv; /* Preserve the errno. */
}
