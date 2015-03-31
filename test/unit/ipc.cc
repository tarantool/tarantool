#include "memory.h"
#include "fiber.h"
#include "ipc.h"
#include "unit.h"

enum {
	ITERATIONS = 100000,
};

void
push_f(va_list ap)
{
	struct ipc_channel *channel = va_arg(ap, struct ipc_channel *);

	for (int i = 0; i < ITERATIONS; i++)
		ipc_channel_put(channel, NULL);
}

void
pop_f(va_list ap)
{
	struct ipc_channel *channel = va_arg(ap, struct ipc_channel *);

	for (int i = 0; i < ITERATIONS; i++)
		(void) ipc_channel_get(channel);
}

void main_f(va_list ap)
{
	header();
	struct fiber *push = fiber_new("push_f", push_f);
	fiber_set_joinable(push, true);
	struct fiber *pop = fiber_new("pop_f", pop_f);
	fiber_set_joinable(pop, true);
	struct ipc_channel *channel = ipc_channel_new(1);
	fiber_start(push, channel);
	fiber_start(pop, channel);
	fiber_join(push);
	fiber_join(pop);
	ipc_channel_delete(channel);
	ev_break(loop(), EVBREAK_ALL);
	footer();
}

int main()
{
	memory_init();
	fiber_init();
	struct fiber *main= fiber_new("main", main_f);
	fiber_wakeup(main);
	ev_run(loop(), 0);
	fiber_free();
	memory_free();
	return 0;
}
