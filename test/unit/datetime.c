#include "dt.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "unit.h"

const char sample[] = "2012-12-24T15:30Z";

#define S(s) {s, sizeof(s) - 1}
struct {
	const char * sz;
	size_t len;
} tests[] = {
	S("2012-12-24 15:30Z"),
	S("2012-12-24 15:30z"),
	S("2012-12-24 15:30"),
	S("2012-12-24 16:30+01:00"),
	S("2012-12-24 16:30+0100"),
	S("2012-12-24 16:30+01"),
	S("2012-12-24 14:30-01:00"),
	S("2012-12-24 14:30-0100"),
	S("2012-12-24 14:30-01"),
	S("2012-12-24 15:30:00Z"),
	S("2012-12-24 15:30:00z"),
	S("2012-12-24 15:30:00"),
	S("2012-12-24 16:30:00+01:00"),
	S("2012-12-24 16:30:00+0100"),
	S("2012-12-24 14:30:00-01:00"),
	S("2012-12-24 14:30:00-0100"),
	S("2012-12-24 15:30:00.123456Z"),
	S("2012-12-24 15:30:00.123456z"),
	S("2012-12-24 15:30:00.123456"),
	S("2012-12-24 16:30:00.123456+01:00"),
	S("2012-12-24 16:30:00.123456+01"),
	S("2012-12-24 14:30:00.123456-01:00"),
	S("2012-12-24 14:30:00.123456-01"),
	S("2012-12-24t15:30Z"),
	S("2012-12-24t15:30z"),
	S("2012-12-24t15:30"),
	S("2012-12-24t16:30+01:00"),
	S("2012-12-24t16:30+0100"),
	S("2012-12-24t14:30-01:00"),
	S("2012-12-24t14:30-0100"),
	S("2012-12-24t15:30:00Z"),
	S("2012-12-24t15:30:00z"),
	S("2012-12-24t15:30:00"),
	S("2012-12-24t16:30:00+01:00"),
	S("2012-12-24t16:30:00+0100"),
	S("2012-12-24t14:30:00-01:00"),
	S("2012-12-24t14:30:00-0100"),
	S("2012-12-24t15:30:00.123456Z"),
	S("2012-12-24t15:30:00.123456z"),
	S("2012-12-24t16:30:00.123456+01:00"),
	S("2012-12-24t14:30:00.123456-01:00"),
	S("2012-12-24 16:30 +01:00"),
	S("2012-12-24 14:30 -01:00"),
	S("2012-12-24 15:30 UTC"),
	S("2012-12-24 16:30 UTC+1"),
	S("2012-12-24 16:30 UTC+01"),
	S("2012-12-24 16:30 UTC+0100"),
	S("2012-12-24 16:30 UTC+01:00"),
	S("2012-12-24 14:30 UTC-1"),
	S("2012-12-24 14:30 UTC-01"),
	S("2012-12-24 14:30 UTC-01:00"),
	S("2012-12-24 14:30 UTC-0100"),
	S("2012-12-24 15:30 GMT"),
	S("2012-12-24 16:30 GMT+1"),
	S("2012-12-24 16:30 GMT+01"),
	S("2012-12-24 16:30 GMT+0100"),
	S("2012-12-24 16:30 GMT+01:00"),
	S("2012-12-24 14:30 GMT-1"),
	S("2012-12-24 14:30 GMT-01"),
	S("2012-12-24 14:30 GMT-01:00"),
	S("2012-12-24 14:30 GMT-0100"),
	S("2012-12-24 14:30 -01:00"),
	S("2012-12-24 16:30:00 +01:00"),
	S("2012-12-24 14:30:00 -01:00"),
	S("2012-12-24 16:30:00.123456 +01:00"),
	S("2012-12-24 14:30:00.123456 -01:00"),
	S("2012-12-24 15:30:00.123456 -00:00"),
	S("20121224T1630+01:00"),
	S("2012-12-24T1630+01:00"),
	S("20121224T16:30+01"),
	S("20121224T16:30 +01"),
};
#undef S

#define DIM(a) (sizeof(a) / sizeof(a[0]))

// p5-time-moment/src/moment_parse.c: parse_string_lenient()
static int
parse_datetime(const char *str, size_t len, int64_t *sp, int64_t *np,
	       int64_t *op)
{
	size_t n;
	dt_t dt;
	char c;
	int sod = 0, nanosecond = 0, offset = 0;

	n = dt_parse_iso_date(str, len, &dt);
	if (!n)
		return 1;
	if (n == len)
		goto exit;

	c = str[n++];
	if (!(c == 'T' || c == 't' || c == ' '))
		return 1;

	str += n;
	len -= n;

	n = dt_parse_iso_time(str, len, &sod, &nanosecond);
	if (!n)
		return 1;
	if (n == len)
		goto exit;

	if (str[n] == ' ')
	n++;

	str += n;
	len -= n;

	n = dt_parse_iso_zone_lenient(str, len, &offset);
	if (!n || n != len)
		return 1;

exit:
	*sp = ((int64_t)dt_rdn(dt) - 719163) * 86400 + sod - offset * 60;
	*np = nanosecond;
	*op = offset;

	return 0;
}

// avoid introducing external datetime.h dependency
// - just copy paste it for today
#define SECS_PER_DAY      86400
#define DT_EPOCH_1970_OFFSET 719163


struct datetime_t {
	int64_t secs;
	int64_t nsec;
	int64_t offset;
};

static int
local_rd(const struct datetime_t * dt) {
	return (int)(dt->secs / SECS_PER_DAY) + DT_EPOCH_1970_OFFSET;
}

static int
local_dt(const struct datetime_t * dt) {
	return dt_from_rdn(local_rd(dt));
}


struct tm*
datetime_to_tm(struct datetime_t * dt)
{
	static struct tm tm;

	memset(&tm, 0, sizeof(tm));
	dt_to_struct_tm(local_dt(dt), &tm);

	int seconds_of_day = dt->secs % 86400;
	tm.tm_hour = (seconds_of_day / 3600) % 24;
	tm.tm_min = (seconds_of_day / 60) % 60;
	tm.tm_sec = seconds_of_day % 60;

	return &tm;
}

static void datetime_test(void)
{
	size_t index;
	int64_t secs_expected;
	int64_t nanosecs;
	int64_t ofs;

	plan(355);
	parse_datetime(sample, sizeof(sample) - 1,
		       &secs_expected, &nanosecs, &ofs);

	for (index = 0; index < DIM(tests); index++) {
		int64_t secs;
		int rc = parse_datetime(tests[index].sz, tests[index].len,
						&secs, &nanosecs, &ofs);
		is(rc, 0, "correct parse_datetime return value for '%s'",
		   tests[index].sz);
		is(secs, secs_expected, "correct parse_datetime output "
		   "seconds for '%s", tests[index].sz);

		// check that stringized literal produces the same date
		// time fields
		static char buff[40];
		struct datetime_t dt = {secs, nanosecs, ofs};
		// datetime_to_tm returns time in GMT zone
		struct tm * p_tm = datetime_to_tm(&dt);
		size_t len = strftime(buff, sizeof buff, "%F %T%z", p_tm);
		ok(len > 0, "strftime");
		rc = parse_datetime(buff, len, &dt.secs, &dt.nsec, &dt.offset);
		is(rc, 0, "correct parse_datetime return value for '%s'", buff);
		is(secs, dt.secs,
		   "reversible seconds via strftime for '%s", buff);
	}
}

int
main(void)
{
	plan(1);
	datetime_test();

	return check_plan();
}
