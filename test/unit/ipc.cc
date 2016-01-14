#include "memory.h"
#include "fiber.h"
#include "ipc.h"
#include "unit.h"

int status;

void
ipc_basic()
{
	header();
	plan(10);

	struct ipc_channel *channel = ipc_channel_new(1);
	ok(channel != NULL, "ipc_channel_new()");

	ok(ipc_channel_size(channel) == 1, "ipc_channel_size()");

	ok(ipc_channel_count(channel) == 0, "ipc_channel_count()");

	ok(ipc_channel_is_full(channel) == false, "ipc_channel_is_full()");

	ok(ipc_channel_is_empty(channel) == true, "ipc_channel_is_empty()");

	char dummy;

	ipc_channel_put(channel, &dummy);

	ok(ipc_channel_size(channel) == 1, "ipc_channel_size(1)");

	ok(ipc_channel_count(channel) == 1, "ipc_channel_count(1)");

	ok(ipc_channel_is_full(channel) == true, "ipc_channel_is_full(1)");

	ok(ipc_channel_is_empty(channel) == false, "ipc_channel_is_empty(1)");

	void *ptr;

	ipc_channel_get(channel, &ptr);
	ok(ptr == &dummy, "ipc_channel_get()");

	ipc_channel_delete(channel);

	footer();
	status = check_plan();
}

void
ipc_get()
{
	header();
	plan(7);

	struct ipc_channel *channel = ipc_channel_new(1);

	char dummy;
	ok(ipc_channel_put_timeout(channel, &dummy, 0) == 0,
	   "ipc_channel_put(0)");
	ok(ipc_channel_put_timeout(channel, &dummy, 0) == -1,
	   "ipc_channel_put_timeout(0)");
	void *ptr;
	ipc_channel_get(channel, &ptr);
	ok(ptr == &dummy, "ipc_channel_get(0)");
	ok(ipc_channel_put_timeout(channel, &dummy, 0.01) == 0,
	   "ipc_channel_put_timeout(1)");
	ipc_channel_get(channel, &ptr);
	ok(ptr == &dummy, "ipc_channel_get(1)");

	ipc_channel_close(channel);

	ok(ipc_channel_put(channel, &dummy) == -1, "ipc_channel_put(closed)");

	ok(ipc_channel_get(channel, &ptr) == -1, "ipc_channel_get(closed)");

	ipc_channel_delete(channel);

	footer();
	status = check_plan();
}

int
main_f(va_list ap)
{
	(void) ap;
	ipc_basic();
	ipc_get();
	ev_break(loop(), EVBREAK_ALL);
	return 0;
}

int main()
{
	memory_init();
	fiber_init(fiber_c_invoke);
	struct fiber *main= fiber_new_xc("main", main_f);
	fiber_wakeup(main);
	ev_run(loop(), 0);
	fiber_free();
	memory_free();
	return status;
}
