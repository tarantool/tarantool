#include <stdio.h>
#include <assert.h>
#include <limits.h>

#include "string.h"
#include "datetime.h"
#include "mp_interval.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

enum {
	SIZE = 512,
};

static void
test_interval_sizeof(void)
{
	header();
	plan(6);

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

	check_plan();
	footer();
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
	header();
	plan(15);

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

	check_plan();
	footer();
}

static void
test_interval_encode_decode_values_outside_int32_limits(void)
{
	header();
	plan(9);

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

	check_plan();
	footer();
}

static void
test_interval_validate(void)
{
	header();
	plan(24);

	/* Check reading field key is checked. */
	ok(mp_validate_interval("\x02", 1) != 0,
	   "reading interval field 1 key is checked");
	ok(mp_validate_interval("\x02\x00\x00", 3) != 0,
	   "reading interval field 2 key is checked");

	/* Check reading field value is checked. */
	ok(mp_validate_interval("\x01\x00\xce", 2) != 0,
	   "reading interval field positive value type is checked");
	ok(mp_validate_interval("\x01\x00\xce", 3) != 0,
	   "reading interval field positive value is checked");
	ok(mp_validate_interval("\x01\x00\xd3", 2) != 0,
	   "reading interval field negative value type is checked");
	ok(mp_validate_interval("\x01\x00\xd3", 3) != 0,
	   "reading interval field negative value is checked");

	/* Check adjust decoding. */
	ok(mp_validate_interval("\x01\x08\x03", 3) != 0,
	   "check adjust value is not greater than DT_SNAP");
	ok(mp_validate_interval("\x01\x08\xff", 3) != 0,
	   "check adjust value is not less that DT_EXCESS (0)");

	/* Check year decoding. */
	ok(mp_validate_interval("\x01\x00\xd2\x80\x00\x00\x00", 7) == 0,
	   "check year equal to INT32_MIN");
	ok(mp_validate_interval(
		"\x01\x00\xd3\xff\xff\xff\xff\x7f\xff\xff\xff", 11) != 0,
		"check year less than INT32_MIN");
	ok(mp_validate_interval("\x01\x00\xce\x7f\xff\xff\xff", 7) == 0,
	   "check year equal to INT32_MAX");
	ok(mp_validate_interval("\x01\x00\xce\x80\x00\x00\x00", 7) != 0,
	   "check year larger than INT32_MAX");

	/* Check month decoding. */
	ok(mp_validate_interval("\x01\x01\xd2\x80\x00\x00\x00", 7) == 0,
	   "check month equal to INT32_MIN");
	ok(mp_validate_interval(
		"\x01\x01\xd3\xff\xff\xff\xff\x7f\xff\xff\xff", 11) != 0,
		"check month less than INT32_MIN");
	ok(mp_validate_interval("\x01\x01\xce\x7f\xff\xff\xff", 7) == 0,
	   "check month equal to INT32_MAX");
	ok(mp_validate_interval("\x01\x01\xce\x80\x00\x00\x00", 7) != 0,
	   "check month larger than INT32_MAX");

	/* Check week decoding. */
	ok(mp_validate_interval("\x01\x02\xd2\x80\x00\x00\x00", 7) == 0,
	   "check week equal to INT32_MIN");
	ok(mp_validate_interval(
		"\x01\x02\xd3\xff\xff\xff\xff\x7f\xff\xff\xff", 11) != 0,
		"check week less than INT32_MIN");
	ok(mp_validate_interval("\x01\x02\xce\x7f\xff\xff\xff", 7) == 0,
	   "check week equal to INT32_MAX");
	ok(mp_validate_interval("\x01\x02\xce\x80\x00\x00\x00", 7) != 0,
	   "check week larger than INT32_MAX");

	/* Check nanosecond decoding. */
	ok(mp_validate_interval("\x01\x07\xd2\x80\x00\x00\x00", 7) == 0,
	   "check nanosecond equal to INT32_MIN");
	ok(mp_validate_interval(
		"\x01\x07\xd3\xff\xff\xff\xff\x7f\xff\xff\xff", 11) != 0,
		"check nanosecond less than INT32_MIN");
	ok(mp_validate_interval("\x01\x07\xce\x7f\xff\xff\xff", 7) == 0,
	   "check nanosecond equal to INT32_MAX");
	ok(mp_validate_interval("\x01\x07\xce\x80\x00\x00\x00", 7) != 0,
	   "check nanosecond larger than INT32_MAX");

	check_plan();
	footer();
}

int
main(void)
{
	header();
	plan(4);

	test_interval_sizeof();
	test_interval_encode_decode();
	test_interval_encode_decode_values_outside_int32_limits();
	test_interval_validate();

	footer();
	return check_plan();
}
