#include "tt_ulid.h"
#include "errinj.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

#include <string.h>

/**
 * Helper for comparison tests:
 * - parses two ULID strings;
 * - round-trips them through to_string();
 * - compares parsed ULIDs and checks the expected ordering.
 */
static void
ulid_cmp_case(const char *a_str, const char *b_str, int expected)
{
	struct tt_ulid a, b;
	char a_buf[ULID_STR_LEN + 1];
	char b_buf[ULID_STR_LEN + 1];
	int rc;

	rc = tt_ulid_from_string(a_str, &a);
	is(rc, 0, "parse a");

	rc = tt_ulid_from_string(b_str, &b);
	is(rc, 0, "parse b");

	tt_ulid_to_string(&a, a_buf);
	tt_ulid_to_string(&b, b_buf);

	is(strcmp(a_buf, a_str), 0,
	   "a round-trip preserves string");
	is(strcmp(b_buf, b_str), 0,
	   "b round-trip preserves string");

	is(tt_ulid_compare(&a, &b), expected,
	   "compare result matches");
}

/**
 * Check round-trip and alias decoding (lowercase, O/I/L).
 */
static void
ulid_round_trip_test(void)
{
	plan(10);
	header();

	const char *canonical = "06DDM1BBF9RSFPG2HV23VXVSJM";
	const char *lower = "06ddm1bbf9rsfpg2hv23vxvsjm";

	/* Crockford aliases:
	 *  - 'O' decodes as 0
	 *  - 'I' and 'L' decode as 1
	 *  (case-insensitive)
	 */
	const char *alias_O = "O6DDM1BBF9RSFPG2HV23VXVSJM"; /* '0' -> 'O' */
	const char *alias_I = "06DDMIBBF9RSFPG2HV23VXVSJM"; /* '1' -> 'I' */
	const char *alias_L = "06DDMLBBF9RSFPG2HV23VXVSJM"; /* '1' -> 'L' */

	struct tt_ulid u_canon, u_var;
	char buf[ULID_STR_LEN + 1];
	int rc;

	/* canonical string -> ulid -> string */
	rc = tt_ulid_from_string(canonical, &u_canon);
	is(rc, 0, "parse canonical ULID string");

	tt_ulid_to_string(&u_canon, buf);
	is(strcmp(buf, canonical), 0,
	   "round-trip preserves canonical representation");

	/* lowercase variant decodes identically */
	rc = tt_ulid_from_string(lower, &u_var);
	is(rc, 0, "parse lowercase ULID string");
	ok(tt_ulid_is_equal(&u_canon, &u_var),
	   "lowercase variant decodes identically");

	/* 'O' as alias for 0 */
	rc = tt_ulid_from_string(alias_O, &u_var);
	is(rc, 0, "parse ULID string with 'O' alias");
	ok(tt_ulid_is_equal(&u_canon, &u_var),
	   "'O' alias decodes identically to canonical");

	/* 'I' as alias for 1 */
	rc = tt_ulid_from_string(alias_I, &u_var);
	is(rc, 0, "parse ULID string with 'I' alias");
	ok(tt_ulid_is_equal(&u_canon, &u_var),
	   "'I' alias decodes identically to canonical");

	/* 'L' as alias for 1 */
	rc = tt_ulid_from_string(alias_L, &u_var);
	is(rc, 0, "parse ULID string with 'L' alias");
	ok(tt_ulid_is_equal(&u_canon, &u_var),
	   "'L' alias decodes identically to canonical");

	footer();
	check_plan();
}

/**
 * Check nil ULID: constant nil and all-zero string.
 */
static void
ulid_nil_test(void)
{
	plan(3);
	header();

	struct tt_ulid u;

	/* Constant nil value must be detected as nil. */
	ok(tt_ulid_is_nil(&ulid_nil),
	   "ulid_nil is nil");

	/* All-zero string decodes to nil. */
	const char *zeros = "00000000000000000000000000";
	ok(tt_ulid_from_string(zeros, &u) == 0,
	   "parse all-zero ULID string");
	ok(tt_ulid_is_equal(&u, &ulid_nil),
	   "all-zero string corresponds to ulid_nil");

	footer();
	check_plan();
}

/**
 * Check ordering of parsed ULIDs.
 */
static void
ulid_compare_test(void)
{
	plan(15);
	header();

	/* equal */
	ulid_cmp_case("06DDK1Z9CSJMTB8ASPQ47JWZP0",
		      "06DDK1Z9CSJMTB8ASPQ47JWZP0", 0);

	/* differ only in random part */
	ulid_cmp_case("06DDK2K5NHB4W8VBE67MNR3VQ4",
		      "06DDK2K5NHB4W8VBE67MNR3VQ8", -1);

	/* and vice versa */
	ulid_cmp_case("06DDK2K5NHB4W8VBE67MNR3VQ8",
		      "06DDK2K5NHB4W8VBE67MNR3VQ4", 1);

	footer();
	check_plan();
}

int
main(void)
{
	plan(3);
	ulid_round_trip_test();
	ulid_nil_test();
	ulid_compare_test();
	return check_plan();
}
