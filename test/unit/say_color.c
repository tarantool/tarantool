#define UNIT_TAP_COMPATIBLE 1

#include <sys/uio.h>

#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
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

/** Alternate EINTR and short writes when testing colored output. */
static int writev_calls;

ssize_t
writev(int fd, const struct iovec *iov, int count)
{
	if (writev_calls > 0) {
		if (writev_calls++ % 2 != 0) {
			errno = EINTR;
			return -1;
		}
		return write(fd, iov[0].iov_base, MIN(iov[0].iov_len, 2));
	}
	ssize_t (*real_writev)(int, const struct iovec *, int) =
		dlsym(RTLD_NEXT, "writev");
	assert(real_writev != NULL);
	return real_writev(fd, iov, count);
}

static void
capture_log(void (*write_logs)(void), char *buf, size_t capacity)
{
	int tty = posix_openpt(O_RDWR | O_NOCTTY);
	assert(tty >= 0);
	int flags = fcntl(tty, F_GETFL);
	assert(flags >= 0);
	int rc = fcntl(tty, F_SETFL, flags | O_NONBLOCK);
	assert(rc == 0);
	rc = grantpt(tty);
	assert(rc == 0);
	rc = unlockpt(tty);
	assert(rc == 0);
	int fd = open(ptsname(tty), O_RDWR | O_NOCTTY);
	assert(fd >= 0);
	struct termios term;
	rc = tcgetattr(fd, &term);
	assert(rc == 0);
	term.c_oflag &= ~OPOST;
	rc = tcsetattr(fd, TCSANOW, &term);
	assert(rc == 0);
	rc = unsetenv("NO_COLOR");
	assert(rc == 0);
	rc = setenv("TERM", "xterm", 1);
	assert(rc == 0);
	memory_init();
	fiber_init(fiber_c_invoke);
	int saved_stderr = dup(STDERR_FILENO);
	assert(saved_stderr >= 0);
	rc = dup2(fd, STDERR_FILENO);
	assert(rc == STDERR_FILENO);
	write_logs();
	rc = dup2(saved_stderr, STDERR_FILENO);
	assert(rc == STDERR_FILENO);
	close(saved_stderr);
	size_t size = 0;
	ssize_t nread;
	do {
		nread = read(tty, buf + size, capacity - size - 1);
		if (nread > 0)
			size += nread;
	} while (nread > 0 || (nread < 0 && errno == EINTR));
	assert(nread < 0 && errno == EAGAIN);
	assert(size < capacity - 1);
	buf[size] = '\0';
	close(fd);
	close(tty);
	fiber_free();
	memory_free();
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
	capture_log(write_uncolored_log, buf, sizeof(buf));
	is_str(buf, "plain\n" YELLOW("colored") "\n",
	       "color suppression applies only to the selected message");
}

static void
write_short_log(void)
{
	writev_calls = 1;
	say_error("short");
	writev_calls = 0;
}

static void
test_short_write(void)
{
	char buf[4096];
	capture_log(write_short_log, buf, sizeof(buf));
	is_str(buf, RED("short") "\n", "retry short writes and EINTR");
}

int
main(void)
{
	plan(2);
	test_color_suppression();
	test_short_write();
	return check_plan();
}
