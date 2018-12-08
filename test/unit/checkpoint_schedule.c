#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <time.h>

#include "unit.h"
#include "checkpoint_schedule.h"

static inline bool
feq(double a, double b)
{
	return fabs(a - b) <= 1;
}

int
main()
{
	header();
	plan(38);

	srand(time(NULL));
	double now = rand();

	struct checkpoint_schedule sched;
	checkpoint_schedule_cfg(&sched, now, 0);

	is(checkpoint_schedule_timeout(&sched, now), 0,
	   "checkpointing disabled - timeout after configuration");

	now += rand();
	is(checkpoint_schedule_timeout(&sched, now), 0,
	   "checkpointing disabled - timeout after sleep");

	checkpoint_schedule_reset(&sched, now);
	is(checkpoint_schedule_timeout(&sched, now), 0,
	   "checkpointing disabled - timeout after reset");

	double intervals[] = { 100, 600, 1200, 1800, 3600, };
	int intervals_len = sizeof(intervals) / sizeof(intervals[0]);
	for (int i = 0; i < intervals_len; i++) {
		double interval = intervals[i];

		checkpoint_schedule_cfg(&sched, now, interval);
		double t = checkpoint_schedule_timeout(&sched, now);
		ok(t >= interval && t <= interval * 2,
		   "checkpoint interval %.0lf - timeout after configuration",
		   interval);

		double t0;
		for (int j = 0; j < 100; j++) {
			checkpoint_schedule_cfg(&sched, now, interval);
			t0 = checkpoint_schedule_timeout(&sched, now);
			if (fabs(t - t0) > interval / 4)
				break;
		}
		ok(fabs(t - t0) > interval / 4,
		   "checkpoint interval %.0lf - initial timeout randomization",
		   interval);

		now += t0 / 2;
		t = checkpoint_schedule_timeout(&sched, now);
		ok(feq(t, t0 / 2),
		   "checkpoint interval %.0lf - timeout after sleep 1",
		   interval);

		now += t0 / 2;
		t = checkpoint_schedule_timeout(&sched, now);
		ok(feq(t, interval),
		   "checkpoint interval %.0lf - timeout after sleep 2",
		   interval);

		now += interval / 2;
		t = checkpoint_schedule_timeout(&sched, now);
		ok(feq(t, interval / 2),
		   "checkpoint interval %.0lf - timeout after sleep 3",
		   interval);

		now += interval;
		t = checkpoint_schedule_timeout(&sched, now);
		ok(feq(t, interval / 2),
		   "checkpoint interval %.0lf - timeout after sleep 4",
		   interval);

		checkpoint_schedule_reset(&sched, now);
		t = checkpoint_schedule_timeout(&sched, now);
		ok(feq(t, interval),
		   "checkpoint interval %.0lf - timeout after reset",
		   interval);
	}

	check_plan();
	footer();

	return 0;
}
