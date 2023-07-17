#include <stdio.h>
#include <assert.h>
#include <limits.h>

#include "unit.h"
#include "string.h"
#include "datetime.h"
#include "mp_interval.h"

enum {
	SIZE = 512,
};

static void
test_interval_sizeof(void)
{
	struct interval itv;
	memset(&itv, 0, sizeof(itv));
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

static bool
is_interval_equal(const struct interval *a, const struct interval *b)
{
	return a->year == b->year && a->week == b->week && a->day == b->day &&
	       a->month == b->month && a->hour == b->hour && a->min == b->min &&
	       a->sec == b->sec && a->nsec == b->nsec && a->adjust == b->adjust;
}

static void
interval_mp_recode(const struct interval *in, struct interval *out)
{
	char buf[SIZE];
	char *to_write = mp_encode_interval(buf, in);
	int size = to_write - buf;
	const char *to_read = buf;
	memset(out, 0, sizeof(*out));
	mp_decode_interval(&to_read, out);
	assert(to_read - buf == size);
}

static void
test_interval_encode_decode(void)
{
	struct interval itv;
	memset(&itv, 0, sizeof(itv));
	struct interval result;
	interval_mp_recode(&itv, &result);
	ok(is_interval_equal(&itv, &result), "Intervals are equal.");

	itv.year = 1;
	interval_mp_recode(&itv, &result);
	ok(is_interval_equal(&itv, &result), "Intervals are equal.");

	itv.month = 200;
	interval_mp_recode(&itv, &result);
	ok(is_interval_equal(&itv, &result), "Intervals are equal.");

	itv.day = -77;
	interval_mp_recode(&itv, &result);
	ok(is_interval_equal(&itv, &result), "Intervals are equal.");

	itv.hour = 2000000000;
	interval_mp_recode(&itv, &result);
	ok(is_interval_equal(&itv, &result), "Intervals are equal.");

	itv.sec = -2000000000;
	interval_mp_recode(&itv, &result);
	ok(is_interval_equal(&itv, &result), "Intervals are equal.");

	is(result.year, 1, "Year value is right");
	is(result.month, 200, "Month value is right");
	is(result.week, 0, "Week value is right");
	is(result.day, -77, "Day value is right");
	is(result.hour, 2000000000, "Hour value is right");
	is(result.min, 0, "Minute value is right");
	is(result.sec, -2000000000, "Second value is right");
	is(result.nsec, 0, "Nanosecond value is right");
	is(result.adjust, DT_EXCESS, "Adjust value is right");
}

static void
test_interval_encode_decode_values_outside_int32_limits(void)
{
	struct interval itv;
	memset(&itv, 0, sizeof(itv));
	struct interval result;
	interval_mp_recode(&itv, &result);
	ok(is_interval_equal(&itv, &result), "Intervals are equal.");

	itv.day = (double)INT32_MIN - 1;
	interval_mp_recode(&itv, &result);
	ok(is_interval_equal(&itv, &result), "Intervals are equal.");

	itv.day = (double)INT32_MAX + 1;
	interval_mp_recode(&itv, &result);
	ok(is_interval_equal(&itv, &result), "Intervals are equal.");

	itv.hour = (double)INT32_MIN - 1;
	interval_mp_recode(&itv, &result);
	ok(is_interval_equal(&itv, &result), "Intervals are equal.");

	itv.hour = (double)INT32_MAX + 1;
	interval_mp_recode(&itv, &result);
	ok(is_interval_equal(&itv, &result), "Intervals are equal.");

	itv.min = (double)INT32_MIN - 1;
	interval_mp_recode(&itv, &result);
	ok(is_interval_equal(&itv, &result), "Intervals are equal.");

	itv.min = (double)INT32_MAX + 1;
	interval_mp_recode(&itv, &result);
	ok(is_interval_equal(&itv, &result), "Intervals are equal.");

	itv.sec = (double)INT32_MIN - 1;
	interval_mp_recode(&itv, &result);
	ok(is_interval_equal(&itv, &result), "Intervals are equal.");

	itv.sec = (double)INT32_MAX + 1;
	interval_mp_recode(&itv, &result);
	ok(is_interval_equal(&itv, &result), "Intervals are equal.");
}

int
main(void)
{
	plan(30);
	test_interval_sizeof();
	test_interval_encode_decode();
	test_interval_encode_decode_values_outside_int32_limits();
	return check_plan();
}
