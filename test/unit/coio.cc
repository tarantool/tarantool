#include "memory.h"
#include "fiber.h"
#include "coio.h"
#include "coio_task.h"
#include "fio.h"
#include "unit.h"
#include "iostream.h"

#include <fcntl.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <sys/errno.h>

int
touch_f(va_list ap)
{
	FILE *f = va_arg(ap, FILE *);
	const char *c = "c";
	while (true) {
		int rc = fwrite(c, strlen(c), 1, f);
		fail_unless(rc == 1);
		fflush(f);
		fiber_sleep(0.01);
		if (fiber_is_cancelled())
			return -1;
	}
	return 0;
}

static void
stat_notify_test(FILE *f, const char *filename)
{
	header();

	struct fiber *touch = fiber_new_xc("touch", touch_f);
	fiber_start(touch, f);
	ev_stat stat;
	note("filename: %s", filename);
	coio_stat_init(&stat, filename);
	int rc = coio_stat_stat_timeout(&stat, TIMEOUT_INFINITY);
	fail_unless(rc == 0);
	fail_unless(stat.prev.st_size < stat.attr.st_size);
	fiber_cancel(touch);

	footer();
}

static void
stat_timeout_test(const char *filename)
{
	header();

	ev_stat stat;
	coio_stat_init(&stat, filename);
	int rc = coio_stat_stat_timeout(&stat, 0.01);
	fail_unless(rc == 0);

	footer();
}

static ssize_t
coio_test_wakeup(va_list ap)
{
	usleep(1000);
	return 0;
}

static int
test_call_f(va_list ap)
{
	header();
	int res = coio_call(coio_test_wakeup);
	note("call done with res %i", res);
	footer();
	return res;
}

static void
test_getaddrinfo(void)
{
	header();
	plan(3);
	const char *host = "127.0.0.1";
	const char *port = "3333";
	struct addrinfo *i;
	/* NULL hints should work. It is a standard. */
	int rc = coio_getaddrinfo(host, port, NULL, &i, 1);
	is(rc, 0, "getaddrinfo");
	freeaddrinfo(i);

	/*
	 * gh-4138: Check getaddrinfo() retval and diagnostics
	 * area.
	 */
	rc = coio_getaddrinfo("non_exists_hostname", port, NULL, &i,
			      15768000000);
	isnt(rc, 0, "getaddrinfo retval");
	const error *last_err = diag_get()->last;
	const char *errmsg = last_err == NULL ? "" : last_err->errmsg;
	bool is_match_with_exp = strstr(errmsg, "getaddrinfo") == errmsg;
	is(is_match_with_exp, true, "getaddrinfo error message");

	/*
	 * gh-4209: 0 timeout should not be a special value and
	 * detach a task. Before a fix it led to segfault
	 * sometimes. The cycle below runs getaddrinfo multiple
	 * times to increase segfault probability.
	 */
	for (int j = 0; j < 5; ++j) {
		if (coio_getaddrinfo(host, port, NULL, &i, 0) == 0 && i != NULL)
			freeaddrinfo(i);
		/*
		 * Skip one event loop to check, that the coio
		 * task destructor will not free the memory second
		 * time.
		 */
		fiber_sleep(0);
	}

	check_plan();
	footer();
}

static void
test_connect(void)
{
	header();
	plan(4);
	int rc;
	rc = coio_connect("~~~", "12345", 1, NULL, NULL);
	ok(rc < 0, "bad ipv4 host name - error");
	ok(strcmp(diag_get()->last->errmsg, "Invalid host name: ~~~") == 0,
	   "bad ipv4 host name - error message");
	rc = coio_connect("~~~", "12345", 2, NULL, NULL);
	ok(rc < 0, "bad ipv6 host name - error");
	ok(strcmp(diag_get()->last->errmsg, "Invalid host name: ~~~") == 0,
	   "bad ipv6 host name - error message");
	check_plan();
	footer();
}

static int
test_read_f(va_list ap)
{
	struct iostream *io = va_arg(ap, struct iostream *);
	char buf[1024];
	int rc = coio_read(io, buf, sizeof(buf));
	if (rc < (ssize_t)sizeof(buf))
		return -1;
	return 0;
}

static int
test_write_f(va_list ap)
{
	struct iostream *io = va_arg(ap, struct iostream *);
	char buf[1024] = "";
	int rc = coio_write_timeout(io, buf, sizeof(buf), TIMEOUT_INFINITY);
	if (rc < (ssize_t)sizeof(buf))
		return -1;
	return 0;
}

static int
test_writev_f(va_list ap)
{
	struct iostream *io = va_arg(ap, struct iostream *);
	char buf[1024] = "";
	struct iovec iov = {(void *)buf, sizeof(buf)};
	int rc = coio_writev(io, &iov, 1, 0);
	if (rc < (ssize_t)sizeof(buf))
		return -1;
	return 0;
}

static int
test_waitpid_f(va_list ap)
{
	/* Flush buffers to avoid multiple output. */
	fflush(stdout);
	fflush(stderr);

	pid_t pid = fork();
	if (pid == 0) {
		/* Child process. */
		execlp("true", "true", NULL);
	}

	fail_if(pid == -1);
	int status, rc = coio_waitpid(pid, &status);
	fail_if(rc != 0);
	fail_if(WIFEXITED(status) == 0);

	return 0;
}

static void
fill_pipe(int fd)
{
	char buf[1024] = "";
	int rc = 0;
	while (rc >= 0 || errno == EINTR)
		rc = write(fd, buf, sizeof(buf));
	fail_unless(errno == EAGAIN || errno == EWOULDBLOCK);
}

static void
empty_pipe(int fd)
{
	char buf[1024];
	int rc = 0;
	while (rc >= 0 || errno == EINTR)
		rc = read(fd, buf, sizeof(buf));
	fail_unless(errno == EAGAIN || errno == EWOULDBLOCK);
}

static void
create_pipe(int fds[2])
{
	int rc = pipe(fds);
	fail_unless(rc >= 0);
	rc = fcntl(fds[0], F_SETFL, O_NONBLOCK);
	fail_unless(rc >= 0);
	rc = fcntl(fds[1], F_SETFL, O_NONBLOCK);
	fail_unless(rc >= 0);
}

static void
read_write_test(void)
{
	header();

	fiber_func test_funcs[] = {
		test_read_f,
		test_write_f,
		test_writev_f,
		test_waitpid_f
	};
	const char *descr[] = {
		"read",
		"write",
		"writev",
		"waitpid"
	};

	int num_tests = sizeof(test_funcs) / sizeof(test_funcs[0]);
	plan(2 * num_tests);

	int fds[2];
	create_pipe(fds);
	for (int i = 0; i < num_tests; i++) {
		struct iostream io;
		if (i == 0) {
			/* A non-readable fd, since the pipe is empty. */
			plain_iostream_create(&io, fds[0]);
		} else {
			plain_iostream_create(&io, fds[1]);
			/* Make the fd non-writable. */
			fill_pipe(fds[1]);
		}
		struct fiber *f = fiber_new_xc("rw_test", test_funcs[i]);
		fiber_set_joinable(f, true);
		fiber_start(f, &io);
		fiber_wakeup(f);
		fiber_sleep(0);
		ok(!fiber_is_dead(f), "coio_%s handle spurious wakeup",
		   descr[i]);
		if (i == 0)
			fill_pipe(fds[1]);
		else
			empty_pipe(fds[0]);
		int rc = fiber_join(f);
		ok(rc == 0, "coio_%s success after a spurious wakeup",
		   descr[i]);
		iostream_destroy(&io);
	}
	close(fds[0]);
	close(fds[1]);
	check_plan();
	footer();
}

static int
main_f(va_list ap)
{
	const char *filename = "1.out";
	FILE *f = fopen(filename, "w+");
	stat_timeout_test(filename);
	stat_notify_test(f, filename);
	fclose(f);
	(void) remove(filename);

	coio_init();
	coio_enable();
	struct fiber *call_fiber = fiber_new_xc("coio_call wakeup", test_call_f);
	fiber_set_joinable(call_fiber, true);
	fiber_start(call_fiber);
	fiber_wakeup(call_fiber);
	fiber_cancel(call_fiber);
	fiber_join(call_fiber);

	test_getaddrinfo();
	test_connect();

	read_write_test();

	ev_break(loop(), EVBREAK_ALL);
	return 0;
}

int main()
{
	memory_init();
	fiber_init(fiber_cxx_invoke);
	struct fiber *test = fiber_new_xc("coio_stat", main_f);
	fiber_wakeup(test);
	ev_run(loop(), 0);
	fiber_free();
	memory_free();
	return 0;
}
