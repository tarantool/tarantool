/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "coio.h"
#include "diag.h"
#include "evio.h"
#include "fiber.h"
#include "memory.h"
#include "uri/uri.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

static void
test_unix_path(void)
{
	header();
	plan(3);

	char dir[] = "./coio-XXXXXX";
	fail_unless(mkdtemp(dir) != NULL);
	struct sockaddr_un addr = {0};
	addr.sun_family = AF_UNIX;
	size_t max = sizeof(addr.sun_path) - 1;
	size_t prefix_len = strlen(dir) + 1;
	fail_unless(prefix_len < max);
	memcpy(addr.sun_path, dir, prefix_len - 1);
	addr.sun_path[prefix_len - 1] = '/';
	memset(addr.sun_path + prefix_len, 's', max - prefix_len);

	int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	fail_unless(server_fd >= 0);
	fail_unless(bind(server_fd, (struct sockaddr *)&addr,
			 sizeof(addr)) == 0);
	fail_unless(listen(server_fd, 2) == 0);

	int fd = coio_connect(URI_HOST_UNIX, addr.sun_path, 0, NULL, NULL,
			      1.0, NULL);
	ok(fd >= 0, "connect at the native Unix socket path limit");
	if (fd >= 0)
		close(fd);

	char path[sizeof(addr.sun_path) + 1];
	memcpy(path, addr.sun_path, max);
	path[max] = 'x';
	path[max + 1] = '\0';
	diag_clear(diag_get());
	fd = coio_connect(URI_HOST_UNIX, path, 0, NULL, NULL, 1.0, NULL);
	ok(fd < 0, "reject an oversized path with a listening prefix");
	struct error *err = diag_last_error(diag_get());
	ok(err != NULL &&
	   strcmp(err->errmsg, "Unix socket path is too long") == 0,
	   "report an oversized Unix socket path");
	if (fd >= 0)
		close(fd);

	close(server_fd);
	fail_unless(unlink(addr.sun_path) == 0);
	fail_unless(rmdir(dir) == 0);
	check_plan();
	footer();
}

static void
test_tcp_listen_port(void)
{
	header();
	plan(2);

	int cwd = open(".", O_RDONLY);
	fail_unless(cwd >= 0);
	char dir[] = "./coio-XXXXXX";
	fail_unless(mkdtemp(dir) != NULL);
	fail_unless(chdir(dir) == 0);
	struct sockaddr_un addr = {.sun_family = AF_UNIX, .sun_path = "0"};
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	fail_unless(fd >= 0);
	fail_unless(bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
	fail_unless(close(fd) == 0);

	struct evio_service service;
	evio_service_create(loop(), &service, "test", NULL, NULL);
	struct uri_set uris;
	fail_unless(uri_set_create(&uris, addr.sun_path) == 0);
	is(evio_service_start(&service, &uris), 0,
	   "listen on a TCP port specified without a host");
	bool socket_exists = access(addr.sun_path, F_OK) == 0;
	ok(socket_exists,
	   "preserve the Unix socket with the same name as the TCP port");
	evio_service_stop(&service);
	uri_set_destroy(&uris);

	if (socket_exists)
		fail_unless(unlink(addr.sun_path) == 0);
	fail_unless(fchdir(cwd) == 0);
	fail_unless(close(cwd) == 0);
	fail_unless(rmdir(dir) == 0);
	check_plan();
	footer();
}

static int
main_f(va_list ap)
{
	(void)ap;
	test_unix_path();
	test_tcp_listen_port();
	ev_break(loop(), EVBREAK_ALL);
	return 0;
}

static void
test_coio_connect(void)
{
	memory_init();
	fiber_init(fiber_c_invoke);
	struct fiber *f = fiber_new("main", main_f);
	fiber_wakeup(f);
	ev_run(loop(), 0);
	fiber_free();
	memory_free();
}

int
main(void)
{
	header();
	plan(2);
	test_coio_connect();
	footer();
	return check_plan();
}
