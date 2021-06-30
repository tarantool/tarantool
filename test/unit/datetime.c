#include "dt.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "datetime.h"
#include "trivia/util.h"
#include "unit.h"

static const char sample[] = "2012-12-24T15:30Z";

#define S(s) {s, sizeof(s) - 1}
struct {
	const char *str;
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

static int
parse_datetime(const char *str, size_t len, int64_t *secs_p,
	       int32_t *nanosecs_p, int32_t *offset_p)
{
	size_t n;
	dt_t dt;
	char c;
	int sec_of_day = 0, nanosecond = 0, offset = 0;

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

	n = dt_parse_iso_time(str, len, &sec_of_day, &nanosecond);
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
	*secs_p = ((int64_t)dt_rdn(dt) - DT_EPOCH_1970_OFFSET) * SECS_PER_DAY +
		  sec_of_day - offset * 60;
	*nanosecs_p = nanosecond;
	*offset_p = offset;

	return 0;
}

static int
local_rd(const struct datetime *dt)
{
	return (int)((int64_t)dt->epoch / SECS_PER_DAY) + DT_EPOCH_1970_OFFSET;
}

static int
local_dt(const struct datetime *dt)
{
	return dt_from_rdn(local_rd(dt));
}


static void
datetime_to_tm(struct datetime *dt, struct tm *tm)
{
	memset(tm, 0, sizeof(*tm));
	dt_to_struct_tm(local_dt(dt), tm);

	int seconds_of_day = (int64_t)dt->epoch % 86400;
	tm->tm_hour = (seconds_of_day / 3600) % 24;
	tm->tm_min = (seconds_of_day / 60) % 60;
	tm->tm_sec = seconds_of_day % 60;
}

static void
datetime_test(void)
{
	size_t index;
	int64_t secs_expected;
	int32_t nanosecs;
	int32_t offset;

	plan(355);
	parse_datetime(sample, sizeof(sample) - 1, &secs_expected, &nanosecs,
		       &offset);

	for (index = 0; index < lengthof(tests); index++) {
		int64_t secs;
		int rc = parse_datetime(tests[index].str, tests[index].len,
					&secs, &nanosecs, &offset);
		is(rc, 0, "correct parse_datetime return value for '%s'",
		   tests[index].str);
		is(secs, secs_expected,
		   "correct parse_datetime output "
		   "seconds for '%s",
		   tests[index].str);

		/*
		 * check that stringized literal produces the same date
		 * time fields
		 */
		static char buff[40];
		struct datetime dt = { secs, nanosecs, offset, 0 };
		/* datetime_to_tm returns time in GMT zone */
		struct tm tm = { .tm_sec = 0 };
		datetime_to_tm(&dt, &tm);
		size_t len = strftime(buff, sizeof(buff), "%F %T", &tm);
		ok(len > 0, "strftime");
		int64_t parsed_secs;
		int32_t parsed_nsecs, parsed_ofs;
		rc = parse_datetime(buff, len, &parsed_secs, &parsed_nsecs,
				    &parsed_ofs);
		is(rc, 0, "correct parse_datetime return value for '%s'", buff);
		is(secs, parsed_secs, "reversible seconds via strftime for '%s",
		   buff);
	}
	check_plan();
}

static void
tostring_datetime_test(void)
{
	static struct {
		const char *string;
		int64_t     secs;
		uint32_t    nsec;
		uint32_t    offset;
	} tests[] = {
		{"1970-01-01T02:00:00+0200",        0,         0,  120},
		{"1970-01-01T01:30:00+0130",        0,         0,   90},
		{"1970-01-01T01:00:00+0100",        0,         0,   60},
		{"1970-01-01T00:01:00+0001",        0,         0,    1},
		{"1970-01-01T00:00:00Z",            0,         0,    0},
		{"1969-12-31T23:59:00-0001",        0,         0,   -1},
		{"1969-12-31T23:00:00-0100",        0,         0,  -60},
		{"1969-12-31T22:30:00-0130",        0,         0,  -90},
		{"1969-12-31T22:00:00-0200",        0,         0, -120},
		{"1970-01-01T00:00:00.123456789Z",  0, 123456789,    0},
		{"1970-01-01T00:00:00.123456Z",     0, 123456000,    0},
		{"1970-01-01T00:00:00.123Z",        0, 123000000,    0},
		{"1973-11-29T21:33:09Z",    123456789,         0,    0},
		{"2013-10-28T17:51:56Z",   1382982716,         0,    0},
		{"9999-12-31T23:59:59Z", 253402300799,         0,    0},
	};
	size_t index;

	plan(15);
	for (index = 0; index < lengthof(tests); index++) {
		struct datetime date = {
			tests[index].secs,
			tests[index].nsec,
			tests[index].offset,
			0
		};
		char buf[48];
		tnt_datetime_to_string(&date, buf, sizeof(buf));
		is(strcmp(buf, tests[index].string), 0,
		   "string '%s' expected, received '%s'",
		   tests[index].string, buf);
	}
	check_plan();
}

int
main(void)
{
	plan(2);
	datetime_test();
	tostring_datetime_test();

	return check_plan();
}
