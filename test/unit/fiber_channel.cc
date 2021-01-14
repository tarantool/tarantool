#include "memory.h"
#include "fiber.h"
#include "fiber_channel.h"
#include "unit.h"

int status;

void
fiber_channel_basic()
{
	header();
	plan(10);

	struct fiber_channel *channel = fiber_channel_new(1);
	ok(channel != NULL, "fiber_channel_new()");

	ok(fiber_channel_size(channel) == 1, "fiber_channel_size()");

	ok(fiber_channel_count(channel) == 0, "fiber_channel_count()");

	ok(fiber_channel_is_full(channel) == false, "fiber_channel_is_full()");

	ok(fiber_channel_is_empty(channel) == true, "fiber_channel_is_empty()");

	char dummy;

	fiber_channel_put(channel, &dummy);

	ok(fiber_channel_size(channel) == 1, "fiber_channel_size(1)");

	ok(fiber_channel_count(channel) == 1, "fiber_channel_count(1)");

	ok(fiber_channel_is_full(channel) == true, "fiber_channel_is_full(1)");

	ok(fiber_channel_is_empty(channel) == false, "fiber_channel_is_empty(1)");

	void *ptr = NULL;

	fiber_channel_get(channel, &ptr);
	ok(ptr == &dummy, "fiber_channel_get()");

	fiber_channel_delete(channel);

	footer();
	status = check_plan();
}

void
fiber_channel_get()
{
	header();
	plan(7);

	struct fiber_channel *channel = fiber_channel_new(1);

	char dummy;
	ok(fiber_channel_put_timeout(channel, &dummy, 0) == 0,
	   "fiber_channel_put(0)");
	ok(fiber_channel_put_timeout(channel, &dummy, 0) == -1,
	   "fiber_channel_put_timeout(0)");
	void *ptr = NULL;
	fiber_channel_get(channel, &ptr);
	ok(ptr == &dummy, "fiber_channel_get(0)");
	ok(fiber_channel_put_timeout(channel, &dummy, 0.01) == 0,
	   "fiber_channel_put_timeout(1)");
	fiber_channel_get(channel, &ptr);
	ok(ptr == &dummy, "fiber_channel_get(1)");

	fiber_channel_close(channel);

	ok(fiber_channel_put(channel, &dummy) == -1, "fiber_channel_put(closed)");

	ok(fiber_channel_get(channel, &ptr) == -1, "fiber_channel_get(closed)");

	fiber_channel_delete(channel);

	footer();
	status = check_plan();
}

int
main_f(va_list ap)
{
	(void) ap;
	fiber_channel_basic();
	fiber_channel_get();
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
