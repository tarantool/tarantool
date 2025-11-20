#include "datetime.h"
#include "trivia/util.h"
#include "tzcode/tzcode.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

void
cord_on_yield(void) {}

/**
 * Tests for bug fix #11347.
 * 
 * Checks ambiguous case where day of year (yday, which is defines
 * calendar month and month day implicitly) and calendar month
 * without a day are both defined in the date text.
 */
const struct {
	const char *fmt;
	const char *text;
} tests[] = {
	/* Issue 11347 reported case:
	 * (month = 07, year = 00, day of year = 1)
	 * Month of day of year 1 is 01,
	 * but 07 is given.
	 */
	{ "%m%g%j",                  "07001" },
	/* 2025-321 is 2025-11-17. */
	{ "%G-%j %m",                "2025-321 01" },
	{ "%G-%j %m",                "2025-321 10" },
	{ "%G-%j %m",                "2025-321 12" },
};

/**
 * Expects tnt_strptime error for yday's month mismatch
 * calendar month case instead of "ok" with invalid tm_mday value.
 */
static void
tnt_strptime_fail_on_ambiguous_yday_mon_case_test()
{
        header();

	plan(lengthof(tests));
	for (uint32_t index = 0; index < lengthof(tests); index++) {
		const char *fmt = tests[index].fmt;
		const char *text = tests[index].text;
		struct tnt_tm tm = { 0 };
		char *res = tnt_strptime(text, fmt, &tm);
		is(res, NULL, "tnt_strptime fail to"
		   " parse string '%s' using '%s'",
		   text, fmt);
	}

	check_plan();
        footer();
}

/**
 * Expects datetime_strptime error for yday's month mismatch
 * calendar month case instead of assertion fail with invalid mday.
 */
static void
datetime_strptime_fail_on_ambiguous_yday_mon_case_test()
{
        header();

	plan(lengthof(tests));
	for (uint32_t index = 0; index < lengthof(tests); index++) {
		const char *fmt = tests[index].fmt;
		const char *text = tests[index].text;
		struct datetime date = { 0 };
		size_t res = datetime_strptime(&date, text, fmt);
		is(res, 0, "datetime_strptime fail to"
		   " parse string '%s' using '%s'",
		   text, fmt);
	}

	check_plan();
        footer();
}

int
main()
{
	plan(2);
	tnt_strptime_fail_on_ambiguous_yday_mon_case_test();
	datetime_strptime_fail_on_ambiguous_yday_mon_case_test();

	return check_plan();
}
