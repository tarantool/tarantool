/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"
#include "fio.h"
#include "trivia/config.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <dlfcn.h>
#include <stdarg.h>
#endif

/** Number of calls to a synchronization primitive. */
static int sync_count;
/** File descriptor passed to the last synchronization call. */
static int sync_fd;
/** Primitive used by the last synchronization call. */
static int sync_op;

static void
record_sync(int fd, int op)
{
	++sync_count;
	sync_fd = fd;
	sync_op = op;
}

#if defined(__APPLE__)
/** System fcntl used to forward intercepted calls. */
static int
(*real_fcntl)(int fd, int cmd, ...);

int
fcntl(int fd, int cmd, ...)
{
	if (cmd == F_FULLFSYNC) {
		record_sync(fd, cmd);
		return real_fcntl(fd, cmd);
	}
	if (cmd == F_GETFD || cmd == F_GETFL)
		return real_fcntl(fd, cmd);
	fail_if(cmd != F_SETFD && cmd != F_SETFL);
	va_list ap;
	va_start(ap, cmd);
	int flags = va_arg(ap, int);
	va_end(ap);
	return real_fcntl(fd, cmd, flags);
}
#else
/** System fsync bypassing the linker wrapper. */
int
__real_fsync(int fd);

int
__wrap_fsync(int fd)
{
	record_sync(fd, 0);
	return __real_fsync(fd);
}

#if defined(HAVE_FDATASYNC)
/** System fdatasync bypassing the linker wrapper. */
int
__real_fdatasync(int fd);

int
__wrap_fdatasync(int fd)
{
	record_sync(fd, 1);
	return __real_fdatasync(fd);
}
#endif
#endif

static void
check_sync(int (*sync_fn)(int), int fd, int expected_op, const char *name)
{
	sync_count = 0;
	is(sync_fn(fd), 0, "%s succeeds", name);
	is(sync_count, 1, "%s calls the primitive once", name);
	is(sync_fd, fd, "%s passes the file descriptor", name);
	is(sync_op, expected_op, "%s uses the expected primitive", name);
	errno = 0;
	is(sync_fn(-1), -1, "%s returns the error", name);
	is(errno, EBADF, "%s preserves errno", name);
	is(sync_count, 2, "%s calls the primitive on failure", name);
	is(sync_fd, -1, "%s passes the invalid file descriptor", name);
	is(sync_op, expected_op, "%s uses the expected primitive on failure",
	   name);
}

static void
test_sync(void)
{
	header();
	plan(18);
#if defined(__APPLE__)
	real_fcntl = dlsym(RTLD_NEXT, "fcntl");
	fail_if(real_fcntl == NULL);
#endif
	char filename[] = "./fio.XXXXXX";
	int fd = mkstemp(filename);
	fail_if(fd < 0);
	fail_if(write(fd, "data", 4) != 4);
#if defined(__APPLE__)
	check_sync(fio_fsync, fd, F_FULLFSYNC, "fio_fsync");
	check_sync(fio_fdatasync, fd, F_FULLFSYNC, "fio_fdatasync");
#else
	check_sync(fio_fsync, fd, 0, "fio_fsync");
#if defined(HAVE_FDATASYNC)
	check_sync(fio_fdatasync, fd, 1, "fio_fdatasync");
#else
	check_sync(fio_fdatasync, fd, 0, "fio_fdatasync");
#endif
#endif
	fail_if(close(fd) != 0);
	fail_if(unlink(filename) != 0);
	check_plan();
	footer();
}

int
main(void)
{
	header();
	plan(1);
	test_sync();
	int rc = check_plan();
	footer();
	return rc;
}
