#include "dt.h"
#include <assert.h>
#include <inttypes.h>
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

void
cord_on_yield(void) {}

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

static void
datetime_test(void)
{
	size_t index;
	struct datetime date_expected;

	plan(497);
	datetime_parse_full(&date_expected, sample, sizeof(sample) - 1);

	for (index = 0; index < lengthof(tests); index++) {
		struct datetime date;
		size_t len = datetime_parse_full(&date, tests[index].str,
						 tests[index].len);
		is(len > 0, true, "correct parse_datetime return value "
		   "for '%s'", tests[index].str);
		is(date.epoch, date_expected.epoch,
		   "correct parse_datetime output "
		   "seconds for '%s",
		   tests[index].str);

		/*
		 * check that stringized literal produces the same date
		 * time fields
		 */
		static char buff[DT_TO_STRING_BUFSIZE];
		len = datetime_strftime(&date, buff, sizeof(buff), "%F %T%z");
		ok(len > 0, "strftime");
		struct datetime date_strp;
		len = datetime_strptime(&date_strp, buff, "%F %T%z");
		is(len > 0, true, "correct parse_strptime return value "
		   "for '%s'", buff);
		is(date.epoch, date_strp.epoch,
		   "reversible seconds via datetime_strptime for '%s'", buff);
		struct datetime date_parsed;
		len = datetime_parse_full(&date_parsed, buff, len);
		is(len > 0, true, "correct datetime_parse_full return value "
		   "for '%s'", buff);
		is(date.epoch, date_parsed.epoch,
		   "reversible seconds via datetime_parse_full for '%s'", buff);
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
		{"10000-01-01T00:00:00Z", 253402300800,        0,    0},
		{"5879611-07-11T00:00:00Z", 185480451417600,   0,    0},
	};
	size_t index;

	plan(17);
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

static int64_t
_dt_to_epoch(dt_t dt)
{
	return ((int64_t)dt_rdn(dt) - DT_EPOCH_1970_OFFSET) * SECS_PER_DAY;
}

static void
parse_date_test(void)
{
	plan(154);

	static struct {
		int64_t epoch;
		const char *string;
		size_t len; /* expected parsed length, may be not full */
	} const valid_tests[] = {
		{ 1356307200, "20121224", 8 },
		{ 1356307200, "20121224  Foo bar", 8 },
		{ 1356307200, "2012-12-24", 10 },
		{ 1356307200, "2012-12-24 23:59:59", 10 },
		{ 1356307200, "2012-12-24T00:00:00+00:00", 10 },
		{ 1356307200, "2012359", 7 },
		{ 1356307200, "2012359T235959+0130", 7 },
		{ 1356307200, "2012-359", 8 },
		{ 1356307200, "2012W521", 8 },
		{ 1356307200, "2012-W52-1", 10 },
		{ 1356307200, "2012Q485", 8 },
		{ 1356307200, "2012-Q4-85", 10 },
		{ -62135596800, "0001-Q1-01", 10 },
		{ -62135596800, "0001-W01-1", 10 },
		{ -62135596800, "0001-01-01", 10 },
		{ -62135596800, "0001-001", 8 },

		/* Tarantool extra ranges */
		{ -62167219200, "0000-01-01", 10 },
		{ -62167046400, "0000-W01-1", 10 },
		{ -62167219200, "0000-Q1-01", 10 },
		{ -68447116800, "-200-12-31", 10 },
		{ -377705203200, "-10000-12-31", 12 },
		{ -185604722870400, "-5879610-06-22", 14 },
		{ -185604706627200, "-5879610W521", 12 },
		{ 253402214400, "9999-12-31", 10 },
		{ 253402300800, "10000-01-01", 11 },
		{ 185480451417600, "5879611-07-11", 13 },
		{ 185480434915200, "5879611Q101", 11 },
	};
	size_t index;

	for (index = 0; index < lengthof(valid_tests); index++) {
		dt_t dt = 0;
		const char *str = valid_tests[index].string;
		size_t expected_len = valid_tests[index].len;
		int64_t expected_epoch = valid_tests[index].epoch;
		size_t len = tnt_dt_parse_iso_date(str, expected_len, &dt);
		int64_t epoch = _dt_to_epoch(dt);
		is(len, expected_len, "string '%s' parse, len %lu", str,
		   len);
		is(epoch, expected_epoch, "string '%s' parse, epoch %" PRId64,
		   str, epoch);
	}

	static const char *const invalid_tests[] = {
		"20121232",    /* Invalid day of month */
		"2012-12-310", /* Invalid day of month */
		"2012-13-24",  /* Invalid month */
		"2012367",     /* Invalid day of year */
		"2012-000",    /* Invalid day of year */
		"2012W533",    /* Invalid week of year */
		"2012-W52-8",  /* Invalid day of week */
		"2012Q495",    /* Invalid day of quarter */
		"2012-Q5-85",  /* Invalid quarter */
		"20123670",    /* Trailing digit */
		"201212320",   /* Trailing digit */
		"2012-12",     /* Reduced accuracy */
		"2012-Q4",     /* Reduced accuracy */
		"2012-Q42",    /* Invalid */
		"2012-Q1-1",   /* Invalid day of quarter */
		"2012Q/* 420", /* Invalid */
		"2012-Q-420",  /* Invalid */
		"2012Q11",     /* Incomplete */
		"2012Q1234",   /* Trailing digit */
		"2012W12",     /* Incomplete */
		"2012W1234",   /* Trailing digit */
		"2012W-123",   /* Invalid */
		"2012-W12",    /* Incomplete */
		"2012-W12-12", /* Trailing digit */
		"2012U1234",   /* Invalid */
		"2012-1234",   /* Invalid */
		"2012-X1234",  /* Invalid */
	};
	for (index = 0; index < lengthof(invalid_tests); index++) {
		dt_t dt = 0;
		const char *str = invalid_tests[index];
		size_t len = tnt_dt_parse_iso_date(str, strlen(str), &dt);
		is(len, 0, "expected failure of string '%s' parse, len %lu",
		   str, len);
	}

	/* check strptime formats */
	const struct {
		const char *fmt;
		const char *text;
	} format_tests[] = {
		{ "%A",                      "Thursday" },
		{ "%a",                      "Thu" },
		{ "%B",                      "January" },
		{ "%b",                      "Jan" },
		{ "%h",                      "Jan" },
		{ "%c",                      "Thu Jan  1 03:00:00 1970" },
		{ "%D",                      "01/01/70" },
		{ "%m/%d/%y",                "01/01/70" },
		{ "%d",                      "01" },
		{ "%Ec",                     "Thu Jan  1 03:00:00 1970" },
		{ "%Ex",                     "01/01/70" },
		{ "%EX",                     "03:00:00" },
		{ "%Ey",                     "70" },
		{ "%EY",                     "1970" },
		{ "%Od",                     "01" },
		{ "%OH",                     "03" },
		{ "%OI",                     "03" },
		{ "%Om",                     "01" },
		{ "%OM",                     "00" },
		{ "%OS",                     "00" },
		{ "%Ou",                     "4" },
		{ "%OU",                     "00" },
		{ "%Ow",                     "4" },
		{ "%OW",                     "00" },
		{ "%Oy",                     "70" },
		{ "%e",                      " 1" },
		{ "%F",                      "1970-01-01" },
		{ "%Y-%m-%d",                "1970-01-01" },
		{ "%H",                      "03" },
		{ "%I",                      "03" },
		{ "%j",                      "001" },
		{ "%k",                      " 3" },
		{ "%l",                      " 3" },
		{ "%M",                      "00" },
		{ "%m",                      "01" },
		{ "%n",                      "\n" },
		{ "%p",                      "AM" },
		{ "%R",                      "03:00" },
		{ "%H:%M",                   "03:00" },
		{ "%r",                      "03:00:00 AM" },
		{ "%I:%M:%S %p",             "03:00:00 AM" },
		{ "%S",                      "00" },
		{ "%s",                      "10800" },
		{ "%f",                      "125" },
		{ "%T",                      "03:00:00" },
		{ "%H:%M:%S",                "03:00:00" },
		{ "%t",                      "\t" },
		{ "%U",                      "00" },
		{ "%u",                      "4" },
		{ "%G",                      "1970" },
		{ "%g",                      "70" },
		{ "%v",                      " 1-Jan-1970" },
		{ "%e-%b-%Y",                " 1-Jan-1970" },
		{ "%W",                      "00" },
		{ "%w",                      "4" },
		{ "%X",                      "03:00:00" },
		{ "%x",                      "01/01/70" },
		{ "%y",                      "70" },
		{ "%Y",                      "1970" },
		{ "%z",                      "+0300" },
		{ "%%",                      "%" },
		{ "%Y-%m-%dT%H:%M:%S.%9f%z", "1970-01-01T03:00:00.125000000+0300" },
		{ "%Y-%m-%dT%H:%M:%S.%f%z",  "1970-01-01T03:00:00.125+0300" },
		{ "%Y-%m-%dT%H:%M:%S.%f",    "1970-01-01T03:00:00.125" },
		{ "%FT%T.%f",                "1970-01-01T03:00:00.125" },
		{ "%FT%T.%f%z",              "1970-01-01T03:00:00.125+0300" },
		{ "%FT%T.%9f%z",             "1970-01-01T03:00:00.125000000+0300" },
		{ "%Y-%m-%d",                "0000-01-01" },
		{ "%Y-%m-%d",                "0001-01-01" },
		{ "%Y-%m-%d",                "9999-01-01" },
		{ "%Y-%m-%d",                "10000-01-01" },
		{ "%Y-%m-%d",                "10000-01-01" },
		{ "%Y-%m-%d",                "5879611-07-11" },
	};

	for (index = 0; index < lengthof(format_tests); index++) {
		const char *fmt = format_tests[index].fmt;
		const char *text = format_tests[index].text;
		struct tnt_tm date = { .tm_epoch = 0};
		char *ptr = tnt_strptime(text, fmt, &date);
		static char buff[DT_TO_STRING_BUFSIZE];
		tnt_strftime(buff, sizeof(buff), "%FT%T%z", &date);
		isnt(ptr, NULL, "parse string '%s' using '%s' (result '%s')",
		     text, fmt, buff);
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

static void
mp_datetime_unpack_valid_checks(void)
{
	/* Binary, message-pack representation of datetime
	 * payload in MP value
	 */
	struct binary_datetime {
		/** Seconds since Epoch. */
		int64_t epoch;
		/** Nanoseconds, if any. */
		int32_t nsec;
		/** Offset in minutes from UTC. */
		int16_t tzoffset;
		/** Olson timezone id */
		int16_t tzindex;
	};

	static struct binary_datetime invalid_values[] = {
		{.epoch = MAX_EPOCH_SECS_VALUE + 1},
		{.epoch = MIN_EPOCH_SECS_VALUE - 1},
		{.nsec = MAX_NANOS_PER_SEC},
		{.nsec = -1},
		{.tzoffset = MIN_TZOFFSET - 1},
		{.tzoffset = MAX_TZOFFSET + 1},
		{.tzindex = MAX_TZINDEX + 1},
		{.tzindex = -1},
	};

	static struct binary_datetime valid_values[] = {
		{.epoch = MAX_EPOCH_SECS_VALUE},
		{.epoch = MIN_EPOCH_SECS_VALUE},
		{.nsec = MAX_NANOS_PER_SEC - 1},
		{.nsec = 0},
		{.tzoffset = MIN_TZOFFSET},
		{.tzoffset = MAX_TZOFFSET},
		{.tzindex = MAX_TZINDEX},
		{.tzindex = 0},
	};
	size_t index;
	const char *p;
	struct datetime date;

	plan(24);
	for (index = 0; index < lengthof(valid_values); index++) {
		struct binary_datetime value = valid_values[index];
		p = (char *)&value;
		memset(&date, 0, sizeof(date));
		struct datetime *dt = datetime_unpack(&p, sizeof(value), &date);
		isnt(dt, NULL, "datetime_unpack() is not NULL");
		is((int64_t)dt->epoch, value.epoch, "epoch value expected");
	}

	for (index = 0; index < lengthof(valid_values); index++) {
		struct binary_datetime value = invalid_values[index];
		p = (char *)&value;
		memset(&date, 0, sizeof(date));
		struct datetime *dt = datetime_unpack(&p, sizeof(value), &date);
		is(dt, NULL, "datetime_unpack() is NULL");
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

static void
interval_from_map_test(void)
{
	plan(2);
	header();

	struct interval iv;
	char buffer[128];
	int rc = mp_format(buffer, sizeof(buffer), "{%s%d}", "year", 100);
	fail_if(rc >= (int)sizeof(buffer));
	rc = interval_from_map(&iv, buffer);
	is(rc, 0, "normal year");
	const char *suboptimal_data =
		"\x81\xa4year\xd3\x00\xff\xff\xff\xff\xff\xff\xff";
	rc = interval_from_map(&iv, suboptimal_data);
	is(rc, -1, "too big year inside mp_int");

	footer();
	check_plan();
}

int
main(void)
{
	plan(7);
	datetime_test();
	tostring_datetime_test();
	parse_date_test();
	mp_datetime_unpack_valid_checks();
	mp_datetime_test();
	mp_print_test();
	interval_from_map_test();

	return check_plan();
}
