#include <time.h>

#include "unit.h"
#include "ratelimit.h"

#define check(expected_emitted, expected_suppressed) do {		\
	is(emitted, expected_emitted, "emitted %d expected %d",		\
	   emitted, expected_emitted);					\
	is(suppressed, expected_suppressed, "suppressed %d expected %d",\
	   suppressed, expected_suppressed);				\
} while (0)

int
main()
{
	header();
	plan(10);

	srand(time(NULL));
	double now = rand();

	int burst = 10;
	double interval = 5;
	int count, emitted, suppressed;
	struct ratelimit rl = RATELIMIT_INITIALIZER(interval, burst);
	now += interval;

	count = burst;
	emitted = suppressed = 0;
	for (int i = 0; i < count; i++) {
		if (ratelimit_check(&rl, now, &suppressed))
			emitted++;
		now += interval / count / 2;
	}
	check(count, 0);

	emitted = suppressed = 0;
	for (int i = 0; i < count; i++) {
		if (ratelimit_check(&rl, now, &suppressed))
			emitted++;
		now += interval / count / 2;
	}
	check(0, 0);

	now += 1;
	emitted = suppressed = 0;
	if (ratelimit_check(&rl, now, &suppressed))
		emitted++;
	check(1, count);

	now += interval * 2;
	emitted = suppressed = 0;
	if (ratelimit_check(&rl, now, &suppressed))
		emitted++;
	check(1, 0);

	interval = 5;
	burst = 100;
	ratelimit_create(&rl, interval, burst);

	int interval_count = 10;
	count = burst * interval_count * 4;
	emitted = suppressed = 0;
	for (int i = 0; i < count; i++) {
		if (ratelimit_check(&rl, now, &suppressed))
			emitted++;
		now += interval_count * interval / count;
	}
	now += interval;
	ratelimit_check(&rl, now, &suppressed);
	check(interval_count * burst, count - interval_count * burst);

	check_plan();
	footer();

	return 0;
}
