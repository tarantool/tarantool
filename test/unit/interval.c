#include <stdio.h>

#include "unit.h"
#include "interval.h"
#include "mp_interval.h"

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

static void
test_interval_sizeof(void)
{
	struct interval itv;
	interval_create(&itv);
	uint32_t size = 3;
	is(mp_sizeof_interval(&itv), size, "Size of interval is %d", size);
	itv.year = 1;
	size = 6;
	is(mp_sizeof_interval(&itv), size, "Size of interval is %d", size);
	itv.month = 200;
	size = 9;
	is(mp_sizeof_interval(&itv), size, "Size of interval is %d", size);
	itv.day = -77;
	size = 12;
	is(mp_sizeof_interval(&itv), size, "Size of interval is %d", size);
	itv.hour = 2000000000;
	size = 18;
	is(mp_sizeof_interval(&itv), size, "Size of interval is %d", size);
	itv.sec = -2000000000;
	size = 24;
	is(mp_sizeof_interval(&itv), size, "Size of interval is %d", size);
}

static void
test_interval_encode_decode(void)
{
	struct interval itv;
	struct interval result;
	interval_create(&itv);
	char buf[SIZE];
	char *to_write = buf;
	mp_encode_interval(to_write, &itv);
	const char *to_read = buf;
	mp_decode_interval(&to_read, &result);
	is(memcmp(&itv, &result, sizeof(itv)), 0, "Intervals are equal.");

	itv.year = 1;
	to_write = buf;
	mp_encode_interval(to_write, &itv);
	to_read = buf;
	mp_decode_interval(&to_read, &result);
	is(memcmp(&itv, &result, sizeof(itv)), 0, "Intervals are equal.");

	itv.month = 200;
	to_write = buf;
	mp_encode_interval(to_write, &itv);
	to_read = buf;
	mp_decode_interval(&to_read, &result);
	is(memcmp(&itv, &result, sizeof(itv)), 0, "Intervals are equal.");

	itv.day = -77;
	to_write = buf;
	mp_encode_interval(to_write, &itv);
	to_read = buf;
	mp_decode_interval(&to_read, &result);
	is(memcmp(&itv, &result, sizeof(itv)), 0, "Intervals are equal.");

	itv.hour = 2000000000;
	to_write = buf;
	mp_encode_interval(to_write, &itv);
	to_read = buf;
	mp_decode_interval(&to_read, &result);
	is(memcmp(&itv, &result, sizeof(itv)), 0, "Intervals are equal.");

	itv.sec = -2000000000;
	to_write = buf;
	mp_encode_interval(to_write, &itv);
	to_read = buf;
	mp_decode_interval(&to_read, &result);
	is(memcmp(&itv, &result, sizeof(itv)), 0, "Intervals are equal.");

	is(result.year, 1, "Year value is right");
	is(result.month, 200, "Month value is right");
	is(result.week, 0, "Week value is right");
	is(result.day, -77, "Day value is right");
	is(result.hour, 2000000000, "Hour value is right");
	is(result.min, 0, "Minute value is right");
	is(result.sec, -2000000000, "Second value is right");
	is(result.nsec, 0, "Nanosecond value is right");
	is(result.adjust, INTERVAL_ADJUST_EXCESS, "Adjust value is right");
}

int
main(void)
{
	plan(33);
	test_interval_default_fields();
	test_interval_to_string();
	test_interval_sizeof();
	test_interval_encode_decode();
	return check_plan();
}
