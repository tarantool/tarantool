#include "memory.h"
#include "fiber.h"
#include "fiber_channel.h"
#include "unit.h"

enum {
	ITERATIONS = 100000,
};

static int
push_f(va_list ap)
{
	struct fiber_channel *channel = va_arg(ap, struct fiber_channel *);

	for (int i = 0; i < ITERATIONS; i++)
		fiber_channel_put(channel, NULL);
	return 0;
}

static int
pop_f(va_list ap)
{
	struct fiber_channel *channel = va_arg(ap, struct fiber_channel *);

	for (int i = 0; i < ITERATIONS; i++) {
		void *ptr;
		fiber_channel_get(channel, &ptr);
	}
	return 0;
}

static int
main_f(va_list ap)
{
	header();
	struct fiber *push = fiber_new_xc("push_f", push_f);
	fiber_set_joinable(push, true);
	struct fiber *pop = fiber_new_xc("pop_f", pop_f);
	fiber_set_joinable(pop, true);
	struct fiber_channel *channel = fiber_channel_new(1);
	fiber_start(push, channel);
	fiber_start(pop, channel);
	fiber_join(push);
	fiber_join(pop);
	fiber_channel_delete(channel);
	ev_break(loop(), EVBREAK_ALL);
	footer();
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
	return 0;
}
