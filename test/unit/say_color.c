#define UNIT_TAP_COMPATIBLE 1

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "unit.h"
#include "say.h"
#include "fiber.h"
#include "memory.h"

#define COLOR_RED "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_RESET "\033[0m"
#define RED(str) COLOR_RED str COLOR_RESET
#define YELLOW(str) COLOR_YELLOW str COLOR_RESET

/** Use a fresh child for each case because NO_COLOR is cached per thread. */
static void
capture_log(bool tty, const char *no_color, void (*write_logs)(void),
	    char *buf, size_t capacity)
{
	int fds[2];
	int rc;
	if (tty) {
		fds[0] = posix_openpt(O_RDWR | O_NOCTTY);
		assert(fds[0] >= 0);
		rc = grantpt(fds[0]);
		assert(rc == 0);
		rc = unlockpt(fds[0]);
		assert(rc == 0);
		fds[1] = open(ptsname(fds[0]), O_RDWR | O_NOCTTY);
		assert(fds[1] >= 0);
		struct termios term;
		rc = tcgetattr(fds[1], &term);
		assert(rc == 0);
		term.c_oflag &= ~OPOST;
		rc = tcsetattr(fds[1], TCSANOW, &term);
		assert(rc == 0);
	} else {
		rc = pipe(fds);
		assert(rc == 0);
	}
	fflush(stdout);
	pid_t pid = fork();
	assert(pid >= 0);
	if (pid == 0) {
		close(fds[0]);
		rc = dup2(fds[1], STDERR_FILENO);
		assert(rc >= 0);
		close(fds[1]);
		if (no_color == NULL)
			rc = unsetenv("NO_COLOR");
		else
			rc = setenv("NO_COLOR", no_color, 1);
		assert(rc == 0);
		memory_init();
		fiber_init(fiber_c_invoke);
		write_logs();
		_exit(0);
	}
	close(fds[1]);
	size_t size = 0;
	ssize_t nread;
	while ((nread = read(fds[0], buf + size, capacity - size - 1)) > 0)
		size += nread;
	assert(nread == 0 || errno == EIO);
	assert(size < capacity - 1);
	buf[size] = '\0';
	close(fds[0]);
	int status;
	pid_t waited = waitpid(pid, &status, 0);
	assert(waited == pid);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void
write_plain_logs(void)
{
	say_error("boot");
	say_logger_init(NULL, S_DEBUG, 0, "plain");
	for (int level = S_FATAL; level <= S_DEBUG; level++)
		_say(level, NULL, 0, NULL, "message");
}

/** Check the severity colors on a TTY and plain output on a pipe. */
static void
test_color(bool tty, const char *no_color, bool color)
{
	char buf[4096];
	capture_log(tty, no_color, write_plain_logs, buf, sizeof(buf));
	note("tty=%d, NO_COLOR=%s", tty, no_color == NULL ? "unset" : no_color);
	char *save;
	char *line = strtok_r(buf, "\n", &save);
	assert(line != NULL);
	is_str(line, color ? RED("boot") : "boot", "boot logger");
	for (int level = S_FATAL; level <= S_DEBUG; level++) {
		line = strtok_r(NULL, "\n", &save);
		assert(line != NULL);
		const char *prefix = level == S_WARN ? COLOR_YELLOW : COLOR_RED;
		bool colored = color && level <= S_WARN;
		const char *suffix = colored ?
			"message" COLOR_RESET : "message";
		ok((colored ? strncmp(line, prefix, strlen(prefix)) == 0 :
		    strchr(line, '\033') == NULL) &&
		   strstr(line, suffix) != NULL,
		   "plain logger, level %d", level);
	}
}

static void
write_json_logs(void)
{
	say_logger_init(NULL, S_INFO, 0, "json");
	say_error("%s", "json");
	say_warn("%s", "json");
}

static void
test_json(void)
{
	char buf[4096];
	capture_log(true, NULL, write_json_logs, buf, sizeof(buf));
	char *save;
	char *line = strtok_r(buf, "\n", &save);
	for (int i = 0; i < 2; i++) {
		assert(line != NULL);
		ok(line[0] == '{' && line[strlen(line) - 1] == '}' &&
		   strstr(line, "\"message\": \"json\"") != NULL &&
		   strchr(line, '\033') == NULL, "JSON on a color-enabled TTY");
		line = strtok_r(NULL, "\n", &save);
	}
}

static void
write_syslog_fatal(void)
{
	char dir[] = "/tmp/say-color.XXXXXX";
	char *path = mkdtemp(dir);
	assert(path != NULL);
	struct sockaddr_un addr = {.sun_family = AF_UNIX};
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/log", dir);
	int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	assert(fd >= 0);
	int rc = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
	assert(rc == 0);
	char logger[sizeof(addr.sun_path) + 32];
	snprintf(logger, sizeof(logger), "syslog:server=unix:%s",
		 addr.sun_path);
	say_logger_init(logger, S_INFO, 0, "plain");
	_say(S_FATAL, NULL, 0, NULL, "fatal");
	close(fd);
	unlink(addr.sun_path);
	rmdir(dir);
}

static void
test_syslog_fatal(void)
{
	char buf[4096];
	capture_log(true, NULL, write_syslog_fatal, buf, sizeof(buf));
	const char *message = strstr(buf, "F> fatal");
	ok(message != NULL && strstr(message + 1, "F> fatal") == NULL &&
	   strstr(buf, COLOR_RED) != NULL &&
	   strstr(buf, "fatal" COLOR_RESET) != NULL,
	   "syslog copies FATAL to stderr exactly once");
}

static void
write_uncolored_log(void)
{
	bool suppressed = say_set_color_suppressed(true);
	say_crit("plain");
	say_set_color_suppressed(suppressed);
	say_warn("colored");
}

static void
test_color_suppression(void)
{
	char buf[4096];
	capture_log(true, NULL, write_uncolored_log, buf, sizeof(buf));
	is_str(buf, "plain\n" YELLOW("colored") "\n",
	       "color suppression applies only to the selected message");
}

static void
write_redirected_logs(void)
{
	FILE *file = tmpfile();
	assert(file != NULL);
	int tty = dup(STDERR_FILENO);
	assert(tty >= 0);
	say_error("before");
	int rc = dup2(fileno(file), STDERR_FILENO);
	assert(rc == STDERR_FILENO);
	say_error("file");
	rc = dup2(tty, STDERR_FILENO);
	assert(rc == STDERR_FILENO);
	close(tty);
	say_error("after");
	rc = fseek(file, 0, SEEK_SET);
	assert(rc == 0);
	char buf[64];
	char *line = fgets(buf, sizeof(buf), file);
	assert(line != NULL);
	/* Return the file bytes through the existing PTY capture. */
	rc = fputs(line, stderr);
	assert(rc >= 0);
	fclose(file);
}

static void
test_stderr_redirect(void)
{
	char buf[4096];
	capture_log(true, NULL, write_redirected_logs, buf, sizeof(buf));
	is_str(buf, RED("before") "\n" RED("after") "\nfile\n",
	       "stderr colors follow TTY -> file -> TTY redirection");
}

int
main(void)
{
	plan(113);
	test_color(false, NULL, false);
	test_color(false, "", false);
	test_color(false, "0", false);
	test_color(false, "1", false);
	test_color(false, "false", false);
	test_color(false, "yes", false);
	test_color(true, NULL, true);
	test_color(true, "", true);
	test_color(true, "0", false);
	test_color(true, "1", false);
	test_color(true, "false", false);
	test_color(true, "yes", false);
	test_json();
	test_syslog_fatal();
	test_color_suppression();
	test_stderr_redirect();
	return check_plan();
}
