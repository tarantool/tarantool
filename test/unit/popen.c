#include <stdio.h>
#include <stdlib.h>

#include "trivia/util.h"
#include "unit.h"

#include "coio.h"
#include "coio_task.h"
#include "memory.h"
#include "fiber.h"
#include "popen.h"
#include "say.h"

static char popen_child_path[PATH_MAX];

#define TEST_POPEN_COMMON_FLAGS			\
	(POPEN_FLAG_SETSID		|	\
	POPEN_FLAG_RESTORE_SIGNALS)

/**
 * A real return value of main_f(), see a comment in swim.c.
 */
static int test_result = 1;

static int
wait_exit(struct popen_handle *handle, int *state, int *exit_code)
{
	for (;;) {
		popen_state(handle, state, exit_code);
		if (*state == POPEN_STATE_EXITED ||
		    *state == POPEN_STATE_SIGNALED)
			break;
		fiber_sleep(0.1);
	}
	return 0;
}

static void
popen_write_exit(void)
{
	struct popen_handle *handle;
	char *child_argv[] = {
		popen_child_path,
		"read", "-n", "5",
		NULL,
	};

	const char data[] = "12345";
	int state, exit_code;

	struct popen_opts opts = {
		.argv		= child_argv,
		.nr_argv	= lengthof(child_argv),
		.env		= NULL,
		.flags		=
			POPEN_FLAG_FD_STDIN		|
			POPEN_FLAG_FD_STDOUT		|
			POPEN_FLAG_FD_STDERR		|
			TEST_POPEN_COMMON_FLAGS,
	};
	int rc;

	plan(6);
	header();

	handle = popen_new(&opts);
	ok(handle != NULL, "popen_new");
	if (handle == NULL)
		goto out;

	popen_state(handle, &state, &exit_code);

	ok(state == POPEN_STATE_ALIVE, "state %s",
	   popen_state_str(state));

	rc = popen_write_timeout(handle, (void *)data,
				 (int)strlen(data),
				 POPEN_FLAG_FD_STDOUT, 180);
	ok(rc == -1, "write flag check");

	rc = popen_write_timeout(handle, (void *)data,
				 (int)strlen(data),
				 POPEN_FLAG_FD_STDIN, 180);
	ok(rc == (int)strlen(data), "write to pipe");
	if (rc != (int)strlen(data))
		goto out_kill;

	rc = wait_exit(handle, &state, &exit_code);
	if (rc) {
		ok(false, "child wait");
		goto out_kill;
	}

	ok(state == POPEN_STATE_EXITED, "child exited");

out_kill:
	rc = popen_delete(handle);
	ok(rc == 0, "popen_delete");

out:
	footer();
	check_plan();
}

static void
popen_read_exit(void)
{
	struct popen_handle *handle;
	char *child_argv[] = {
		popen_child_path,
		"echo", "1 2 3 4 5",
		NULL,
	};

	int state, exit_code;
	char data[32] = { };

	struct popen_opts opts = {
		.argv		= child_argv,
		.nr_argv	= lengthof(child_argv),
		.env		= NULL,
		.flags		=
			POPEN_FLAG_FD_STDIN		|
			POPEN_FLAG_FD_STDOUT		|
			POPEN_FLAG_FD_STDERR		|
			TEST_POPEN_COMMON_FLAGS,
	};
	int rc;

	plan(5);
	header();

	handle = popen_new(&opts);
	ok(handle != NULL, "popen_new");
	if (handle == NULL)
		goto out;

	rc = wait_exit(handle, &state, &exit_code);
	if (rc) {
		ok(false, "child wait");
		goto out_kill;
	}
	ok(state == POPEN_STATE_EXITED, "child exited");

	rc = popen_read_timeout(handle, data, sizeof(data),
				POPEN_FLAG_FD_STDIN, 180);
	ok(rc == -1, "read flag check");

	rc = popen_read_timeout(handle, data, sizeof(data),
				POPEN_FLAG_FD_STDOUT, 180);
	ok(rc >= 9 && !strncmp(data, "1 2 3 4 5", 9), "read from pipe");

out_kill:
	rc = popen_delete(handle);
	ok(rc == 0, "popen_delete");

out:
	footer();
	check_plan();
}

static void
popen_kill(void)
{
	struct popen_handle *handle;
	char *child_argv[] = {
		popen_child_path,
		"loop",
		NULL,
	};

	int state, exit_code;

	struct popen_opts opts = {
		.argv		= child_argv,
		.nr_argv	= lengthof(child_argv),
		.env		= NULL,
		.flags		=
			POPEN_FLAG_FD_STDIN		|
			POPEN_FLAG_FD_STDOUT		|
			POPEN_FLAG_FD_STDERR		|
			TEST_POPEN_COMMON_FLAGS,
	};
	int rc;

	plan(4);
	header();

	handle = popen_new(&opts);
	ok(handle != NULL, "popen_new");
	if (handle == NULL)
		goto out;

	rc = popen_send_signal(handle, SIGTERM);
	ok(rc == 0, "popen_send_signal");
	if (rc != 0)
		goto out_kill;

	rc = wait_exit(handle, &state, &exit_code);
	if (rc) {
		ok(false, "child wait");
		goto out_kill;
	}
	ok(state == POPEN_STATE_SIGNALED, "child terminated");

out_kill:
	rc = popen_delete(handle);
	ok(rc == 0, "popen_delete");

out:
	footer();
	check_plan();
}

static int
main_f(va_list ap)
{
	plan(3);
	header();

	popen_write_exit();
	popen_read_exit();
	popen_kill();

	ev_break(loop(), EVBREAK_ALL);

	footer();
	test_result = check_plan();

	return 0;
}

int
main(int argc, char *argv[])
{
#if 0
	say_logger_init(NULL, S_DEBUG, 0, "plain", 0);
#endif
	memory_init();

	if (getenv("BUILDDIR") == NULL) {
		size_t size = sizeof(popen_child_path);
		strncpy(popen_child_path, "./test/unit/popen-child", size);
		popen_child_path[size-1] = '\0';
	} else {
		snprintf(popen_child_path, sizeof(popen_child_path),
			 "%s/test/unit/popen-child", getenv("BUILDDIR"));
	}

	fiber_init(fiber_c_invoke);
	popen_init();
	coio_init();
	coio_enable();

	if (!loop())
		panic("%s", "can't init event loop");

	struct fiber *test = fiber_new("main", main_f);
	fiber_wakeup(test);

	ev_now_update(loop());
	ev_run(loop(), 0);
	popen_free();
	fiber_free();
	memory_free();

	return test_result;
}
