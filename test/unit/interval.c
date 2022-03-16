#include <stdio.h>

#include "unit.h"
#include "interval.h"

enum {
	SIZE = 512,
};

static void
test_interval_default_fields(void)
{
	struct interval itv;
	interval_create(&itv);
	is(itv.year, 0, "Default year is 0");
	is(itv.month, 0, "Default month is 0");
	is(itv.week, 0, "Default week is 0");
	is(itv.day, 0, "Default day is 0");
	is(itv.hour, 0, "Default hour is 0");
	is(itv.min, 0, "Default min is 0");
	is(itv.sec, 0, "Default sec is 0");
	is(itv.nsec, 0, "Default nsec is 0");
	is(itv.adjust, INTERVAL_ADJUST_EXCESS,
	   "Default adjust is INTERVAL_ADJUST_EXCESS");
}

static void
test_interval_to_string(void)
{
	const char *res1 = "0 seconds";
	char buf[SIZE];
	struct interval itv;
	interval_create(&itv);
	interval_to_string(&itv, buf, SIZE);
	is(strcmp(buf, res1), 0, "%s", buf);

	const char *res2 = "3 weeks";
	itv.week = 3;
	interval_to_string(&itv, buf, SIZE);
	is(strcmp(buf, res2), 0, "%s", buf);

	const char *res3 = "-100 years, 100 months, 77 weeks, -77 days, "
			   "123 hours, -123 minutes, -1 seconds, "
			   "1 nanoseconds, LIMIT adjust";
	itv.year = -100;
	itv.month = 100;
	itv.week = 77;
	itv.day = -77;
	itv.hour = 123;
	itv.min = -123;
	itv.sec = -1;
	itv.nsec = 1;
	itv.adjust = INTERVAL_ADJUST_LIMIT;
	interval_to_string(&itv, buf, SIZE);
	is(strcmp(buf, res3), 0, "%s", buf);
}

int
main(void)
{
	plan(12);
	test_interval_default_fields();
	test_interval_to_string();
	return check_plan();
}
