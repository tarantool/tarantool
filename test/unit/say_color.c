#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"
#include "say.h"
#include "fiber.h"
#include "memory.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static void
test_color(bool tty, const char *no_color, bool color)
{
	int fds[2];
	if (tty) {
		fds[0] = posix_openpt(O_RDWR | O_NOCTTY);
		fail_if(fds[0] < 0);
		fail_if(grantpt(fds[0]) != 0 || unlockpt(fds[0]) != 0);
		fds[1] = open(ptsname(fds[0]), O_RDWR | O_NOCTTY);
		fail_if(fds[1] < 0);
		struct termios term;
		fail_if(tcgetattr(fds[1], &term) != 0);
		term.c_oflag &= ~OPOST;
		fail_if(tcsetattr(fds[1], TCSANOW, &term) != 0);
	} else {
		fail_if(pipe(fds) != 0);
	}
	fflush(stdout);
	pid_t pid = fork();
	fail_if(pid < 0);
	if (pid == 0) {
		close(fds[0]);
		fail_if(dup2(fds[1], STDERR_FILENO) < 0);
		close(fds[1]);
		int rc = no_color == NULL ? unsetenv("NO_COLOR") :
			 setenv("NO_COLOR", no_color, 1);
		fail_if(rc != 0);
		memory_init();
		fiber_init(fiber_c_invoke);
		say_error("boot");
		say_logger_init(NULL, S_DEBUG, 0, "plain");
		for (int level = S_FATAL; level <= S_DEBUG; level++)
			_say(level, NULL, 0, NULL, "message");
		say_set_log_format(SF_JSON);
		say_error("%s", "json");
		_exit(0);
	}
	close(fds[1]);
	char buf[4096];
	size_t size = 0;
	ssize_t rc;
	while ((rc = read(fds[0], buf + size, sizeof(buf) - size - 1)) > 0)
		size += rc;
	fail_if(rc < 0 && errno != EIO);
	buf[size] = '\0';
	close(fds[0]);
	int status;
	fail_if(waitpid(pid, &status, 0) != pid);
	fail_unless(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	note("tty=%d, NO_COLOR=%s", tty, no_color == NULL ? "unset" : no_color);
	char *save;
	char *line = strtok_r(buf, "\n", &save);
	fail_if(line == NULL);
	is_str(line, color ? "\033[31mboot\033[0m" : "boot", "boot logger");
	for (int level = S_FATAL; level <= S_DEBUG; level++) {
		line = strtok_r(NULL, "\n", &save);
		fail_if(line == NULL);
		const char *prefix = level == S_WARN ? "\033[33m" : "\033[31m";
		bool colored = color && level <= S_WARN;
		ok((colored ? strncmp(line, prefix, strlen(prefix)) == 0 :
		    strchr(line, '\033') == NULL) &&
		   strstr(line, colored ? "message\033[0m" : "message") != NULL,
		   "plain logger, level %d", level);
	}
	line = strtok_r(NULL, "\n", &save);
	fail_if(line == NULL);
	ok(line[0] == '{' && strchr(line, '\033') == NULL, "JSON has no color");
}

int
main(void)
{
	plan(80);
	test_color(false, NULL, false);
	test_color(false, "", false);
	test_color(false, "0", false);
	test_color(false, "1", false);
	test_color(true, NULL, true);
	test_color(true, "", true);
	test_color(true, "0", true);
	test_color(true, "1", false);
	return check_plan();
}
