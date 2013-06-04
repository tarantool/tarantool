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
#ifndef PIPE_BUF
#include <sys/param.h>
#endif

#include <fiber.h>
extern "C" {
#include <cfg/warning.h>
#include <cfg/tarantool_box_cfg.h>
} /* extern "C" */
#include "tarantool.h"
#include "sio.h"

int sayfd = STDERR_FILENO;
pid_t logger_pid;

static char
level_to_char(int level)
{
	switch (level) {
	case S_FATAL:
		return 'F';
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
say_logger_init(int nonblock)
{
	int pipefd[2];
	pid_t pid;
	char cmd[] = { "/bin/sh" };
	char args[] = { "-c" };
	char *argv[] = { cmd, args, cfg.logger, NULL };
	char *envp[] = { NULL };

	if (cfg.logger != NULL) {
		if (pipe(pipefd) == -1) {
			say_syserror("pipe");
			goto out;
		}

		pid = fork();
		if (pid == -1) {
			say_syserror("pipe");
			goto out;
		}

		if (pid == 0) {
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
			say_syserror("Can't start logger: %s", cfg.logger);
			_exit(EXIT_FAILURE);
		}
		close(pipefd[0]);
		dup2(pipefd[1], STDERR_FILENO);
		dup2(pipefd[1], STDOUT_FILENO);
		sayfd = pipefd[1];

		logger_pid = pid;
	} else {
		sayfd = STDERR_FILENO;
	}
      out:
	if (nonblock)
		sio_setfl(sayfd, O_NONBLOCK, 1);

	setvbuf(stderr, NULL, _IONBF, 0);
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

	ev_now_update();

	for (f = filename; *f; f++)
		if (*f == '/' && *(f + 1) != '\0')
			filename = f + 1;

	time_t now = (time_t) ev_now();
	struct tm tm;
	localtime_r(&now, &tm);

	/* Print time in format 2012-08-07 18:30:00.634 */
	p += strftime(buf + p, len - p, "%F %H:%M", &tm);
	p += snprintf(buf + p, len - p, ":%06.3f",
		      ev_now() - now + tm.tm_sec);

	p += snprintf(buf + p, len - p, " [%i] %i/%s", getpid(),
		      fiber->fid, fiber_name(fiber));

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

void
_say(int level, const char *filename, int line, const char *error, const char *format, ...)
{
	int errsv = errno; /* Preserve the errno. */
	if (cfg.log_level < level)
		return;
	va_list ap;
	va_start(ap, format);
	vsay(level, filename, line, error, format, ap);
	va_end(ap);
	errno = errsv; /* Preserve the errno. */
}
