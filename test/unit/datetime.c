#include "dt.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "datetime.h"
#include "mp_datetime.h"
#include "msgpuck.h"
#include "mp_extension_types.h"
#include "trivia/util.h"
#include "tzcode/tzcode.h"

#define UNIT_TAP_COMPATIBLE 1
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
		struct tnt_tm tm = { .tm_sec = 0 };
		datetime_to_tm(&dt, &tm);
		size_t len = tnt_strftime(buff, sizeof(buff), "%F %T%z", &tm);
		ok(len > 0, "strftime");
		int64_t parsed_secs;
		int32_t parsed_nsecs, parsed_ofs;
		rc = parse_datetime(buff, len, &parsed_secs, &parsed_nsecs,
				    &parsed_ofs);
		is(rc, 0, "correct parse_datetime return value for '%s'", buff);
		is(secs, parsed_secs,
		   "reversible seconds via strftime for '%s'", buff);
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
		datetime_to_string(&date, buf, sizeof(buf));
		is(strcmp(buf, tests[index].string), 0,
		   "string '%s' expected, received '%s'",
		   tests[index].string, buf);
	}
	check_plan();
}

static void
mp_datetime_test()
{
	static struct {
		int64_t     secs;
		uint32_t    nsec;
		uint32_t    offset;
		uint32_t    len;
	} tests[] = {
		{ /* '1970-01-01T02:00+02:00' */ 0, 0, 120, 18 },
		{ /* '1970-01-01T01:30+01:30' */ 0, 0, 90, 18 },
		{ /* '1970-01-01T01:00+01:00' */ 0, 0, 60, 18 },
		{ /* '1970-01-01T00:01+00:01' */ 0, 0, 1, 18 },
		{ /* '1970-01-01T00:00Z' */ 0, 0, 0, 10 },
		{ /* '1969-12-31T23:59-00:01' */ 0, 0, -1, 18 },
		{ /* '1969-12-31T23:00-01:00' */ 0, 0, -60, 18 },
		{ /* '1969-12-31T22:30-01:30' */ 0, 0, -90, 18 },
		{ /* '1969-12-31T22:00-02:00' */ 0, 0, -120, 18 },
		{ /* '1970-01-01T00:00:00.123456789Z' */ 0, 123456789, 0, 18 },
		{ /* '1970-01-01T00:00:00.123456Z' */ 0, 123456000, 0, 18 },
		{ /* '1970-01-01T00:00:00.123Z' */ 0, 123000000, 0, 18 },
		{ /* '1973-11-29T21:33:09Z' */ 123456789, 0, 0, 10 },
		{ /* '2013-10-28T17:51:56Z' */ 1382982716, 0, 0, 10 },
		{ /* '9999-12-31T23:59:59Z' */ 253402300799, 0, 0, 10 },
		{ /* '9999-12-31T23:59:59.123456789Z' */ 253402300799,
		  123456789, 0, 18 },
		{ /* '9999-12-31T23:59:59.123456789-02:00' */ 253402300799,
		  123456789, -120, 18 },
	};
	size_t index;

	plan(85);
	for (index = 0; index < lengthof(tests); index++) {
		struct datetime date = {
			tests[index].secs,
			tests[index].nsec,
			tests[index].offset,
			0
		};
		char buf[24], *data = buf;
		const char *data1 = buf;
		struct datetime ret;

		char *end = mp_encode_datetime(data, &date);
		uint32_t len = mp_sizeof_datetime(&date);
		is(len, tests[index].len, "len %u, expected len %u",
		   len, tests[index].len);
		is(end - data, len,
		   "tnt_mp_sizeof_datetime(%d) == encoded length %ld",
		   len, end - data);

		struct datetime *rc = mp_decode_datetime(&data1, &ret);
		is(rc, &ret, "mp_decode_datetime() return code");
		is(data1, end, "data1 == end (%lu)", data1 - end);
		is(datetime_compare(&date, &ret), 0, "datetime_compare(&date, &ret)");
	}
	check_plan();
}


static int
mp_fprint_ext_test(FILE *file, const char **data, int depth)
{
	(void)depth;
	int8_t type;
	uint32_t len = mp_decode_extl(data, &type);
	if (type != MP_DATETIME)
		return fprintf(file, "undefined");
	return mp_fprint_datetime(file, data, len);
}

static int
mp_snprint_ext_test(char *buf, int size, const char **data, int depth)
{
        (void)depth;
        int8_t type;
        uint32_t len = mp_decode_extl(data, &type);
        if (type != MP_DATETIME)
                return snprintf(buf, size, "undefined");
        return mp_snprint_datetime(buf, size, data, len);
}

static void
mp_print_test(void)
{
	plan(5);
	header();

	mp_snprint_ext = mp_snprint_ext_test;
	mp_fprint_ext = mp_fprint_ext_test;

	char sample[64];
	char buffer[64];
	char str[64];
	struct datetime date = {0, 0, 0, 0}; // 1970-01-01T00:00Z

	mp_encode_datetime(buffer, &date);
	int sz = datetime_to_string(&date, str, sizeof(str));
	int rc = mp_snprint(NULL, 0, buffer);
	is(rc, sz, "correct mp_snprint size %u with empty buffer", rc);
	rc = mp_snprint(str, sizeof(str), buffer);
	is(rc, sz, "correct mp_snprint size %u", rc);
	datetime_to_string(&date, sample, sizeof(sample));
	is(strcmp(str, sample), 0, "correct mp_snprint result");

	FILE *f = tmpfile();
	rc = mp_fprint(f, buffer);
	is(rc, sz, "correct mp_fprint size %u", sz);
	rewind(f);
	rc = fread(str, 1, sizeof(str), f);
	str[rc] = 0;
	is(strcmp(str, sample), 0, "correct mp_fprint result %u", rc);
	fclose(f);

	mp_snprint_ext = mp_snprint_ext_default;
	mp_fprint_ext = mp_fprint_ext_default;

	footer();
	check_plan();
}

int
main(void)
{
	plan(4);
	datetime_test();
	tostring_datetime_test();
	mp_datetime_test();
	mp_print_test();

	return check_plan();
}
