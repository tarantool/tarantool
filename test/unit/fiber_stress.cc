#include "memory.h"
#include "fiber.h"

enum {
	ITERATIONS = 50000,
	FIBERS = 100
};

void yield_f(va_list ap)
{
	for (int i = 0; i < ITERATIONS; i++) {
		fiber_wakeup(fiber());
		fiber_yield();
	}
}

void benchmark_f(va_list ap)
{
	struct fiber *fibers[FIBERS];
	for (int i = 0; i < FIBERS; i++) {
		fibers[i] = fiber_new("yield-wielder", yield_f);
		fiber_wakeup(fibers[i]);
	}
	/** Wait for fibers to die. */
	for (int i = 0; i < FIBERS; i++) {
		while (fibers[i]->fid > 0)
			fiber_sleep(0.001);
	}
	ev_break(loop(), EVBREAK_ALL);
}

int main()
{
	memory_init();
	fiber_init();
	struct fiber *benchmark = fiber_new("benchmark", benchmark_f);
	fiber_wakeup(benchmark);
	ev_run(loop(), 0);
	fiber_free();
	memory_free();
	return 0;
}
