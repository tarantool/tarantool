#include "decimal.h"
#include "mp_decimal.h"
#include "mp_extension_types.h"
#include "msgpuck.h"
#include <limits.h>
#include <string.h>
#include <inttypes.h>
#include <float.h> /* DBL_DIG */

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

#define success(x) x
#define failure(x) NULL

#define dectest(a, b, type, cast) ({\
	decimal_t t, u, v, w;\
	is(decimal_from_##type(&u, (a)), &u, "decimal("#a")");\
	is(decimal_from_##type(&v, (b)), &v, "decimal("#b")");\
	\
	is(decimal_add(&t, &u, &v), &t, "decimal("#a") + decimal("#b")");\
	is(decimal_from_##type(&w, (cast)(a) + (cast)(b)), &w, "decimal(("#a") + ("#b"))");\
	is(decimal_compare(&t, &w), 0, "decimal("#a") + decimal("#b") == ("#a") + ("#b")");\
	\
	is(decimal_sub(&t, &u, &v), &t, "decimal("#a") - decimal("#b")");\
	is(decimal_from_##type(&w, (cast)(a) - (cast)(b)), &w, "decimal(("#a") - ("#b"))");\
	is(decimal_compare(&t, &w), 0, "decimal("#a") - decimal("#b") == ("#a") - ("#b")");\
	\
	is(decimal_mul(&t, &u, &v), &t, "decimal("#a") * decimal("#b")");\
	is(decimal_from_##type(&w, (cast)(a) * (cast)(b)), &w, "decimal(("#a") * ("#b"))");\
	is(decimal_round(&t, DBL_DIG), &t, "decimal_round(("#a") * ("#b"), %d)", DBL_DIG);\
	is(decimal_compare(&t, &w), 0, "decimal("#a") * decimal("#b") == ("#a") * ("#b")");\
	\
	is(decimal_div(&t, &u, &v), &t, "decimal("#a") / decimal("#b")");\
	is(decimal_from_double(&w, (double)((a)) / (b)), &w, "decimal(("#a") / ("#b"))");\
	is(decimal_round(&t, DBL_DIG - decimal_precision(&t) + decimal_scale(&t)), &t,\
	   "decimal_round(("#a")/("#b"), %d)", DBL_DIG);\
	is(decimal_compare(&t, &w), 0, "decimal("#a") / decimal("#b") == ("#a") / ("#b")");\
})

#define dectest_op(op, stra, strb, expected) ({\
	decimal_t a, b, c, d;\
	is(decimal_from_string(&a, #stra), &a, "decimal_from_string("#stra")");\
	is(decimal_from_string(&b, #strb), &b, "decimal_from_string("#strb")");\
	is(decimal_from_string(&d, #expected), &d, "decimal_from_string("#expected")");\
	is(decimal_##op(&c, &a, &b), &c, "decimal_"#op"("#stra", "#strb")");\
	is(decimal_compare(&c, &d), 0, "decimal_compare("#expected")");\
})

#define dectest_op1(op, stra, expected, scale) ({\
	decimal_t a, c, d;\
	is(decimal_from_string(&a, #stra), &a, "decimal_from_string("#stra")");\
	is(decimal_from_string(&d, #expected), &d, "decimal_from_string("#expected")");\
	is(decimal_##op(&c, &a), &c, "decimal_"#op"("#stra")");\
	if (scale > 0)\
		decimal_round(&c, scale);\
	is(decimal_compare(&c, &d), 0, "decimal_compare("#expected")");\
})

#define dectest_construct(type, a, expect) ({\
	decimal_t dec;\
	is(decimal_from_##type(&dec, a), expect(&dec), "decimal construction from "#a" "#expect);\
})

#define dectest_op1_fail(op, stra) ({\
	decimal_t a, b;\
	is(decimal_from_string(&a, #stra), &a, "decimal_from_string("#stra")");\
	is(decimal_##op(&b, &a), NULL, "decimal_"#op"("#stra") - error on wrong operands.");\
})

#define dectest_is(op, stra, expect) ({\
	decimal_t a;\
	is(decimal_from_string(&a, #stra), &a, "decimal_from_string("#stra")");\
	is(decimal_##op(&a), expect, "decimal_"#op"("#stra") - expected "\
				      #expect);\
})

#define test_strtodec(str, end, expect) ({\
	decimal_t dec;\
	const char *endptr;\
	is(strtodec(&dec, str, &endptr), expect(&dec), "strtodec("#str") "\
							#expect);\
	is(*endptr, end, "strtodec("#str") - expected end of valid string at "\
			  #end);\
})

char buf[64];

#define test_mpdec(str) ({\
	decimal_t dec;\
	decimal_from_string(&dec, str);\
	uint32_t l1 = mp_sizeof_decimal(&dec);\
	ok(l1 <= 25 && l1 >= 4, "mp_sizeof_decimal("str")");\
	char *b1 = mp_encode_decimal(buf, &dec);\
	is(b1, buf + l1, "mp_sizeof_decimal("str") == len(mp_encode_decimal("str"))");\
	const char *b2 = buf;\
	const char *b3 = buf;\
	decimal_t d2;\
	mp_next(&b3);\
	is(b3, b1, "mp_next(mp_encode("str"))");\
	mp_decode_decimal(&b2, &d2);\
	is(b1, b2, "mp_decode(mp_encode("str") len");\
	is(decimal_compare(&dec, &d2), 0, "mp_decode(mp_encode("str")) value");\
	is(decimal_scale(&dec), decimal_scale(&d2), "mp_decode(mp_encode("str")) scale");\
	is(strcmp(decimal_str(&d2), str), 0, "str(mp_decode(mp_encode("str"))) == "str);\
	char strbuf[DECIMAL_MAX_STR_LEN + 1];\
	decimal_to_string(&dec, strbuf);\
	is(strcmp(strbuf, decimal_str(&dec)), 0, "stack str == static str for "str);\
	b2 = buf;\
	int8_t type;\
	uint32_t l2 = mp_decode_extl(&b2, &type);\
	is(type, MP_DECIMAL, "mp_ext_type is MP_DECIMAL");\
	is(&d2, decimal_unpack(&b2, l2, &d2), "decimal_unpack() after mp_decode_extl()");\
	is(decimal_compare(&dec, &d2), 0, "decimal_unpack() after mp_decode_extl() value");\
	is(b2, buf + l1, "decimal_unpack() after mp_decode_extl() len");\
})

#define test_decpack(str) ({\
	decimal_t dec;\
	decimal_from_string(&dec, str);\
	uint32_t l1 = decimal_len(&dec);\
	ok(l1 <= 25 && l1 >= 2, "decimal_len("str")");\
	char *b1 = decimal_pack(buf, &dec);\
	is(b1, buf + l1, "decimal_len("str") == len(decimal_pack("str")");\
	const char *b2 = buf;\
	decimal_t d2;\
	is(decimal_unpack(&b2, l1, &d2), &d2, "decimal_unpack(decimal_pack("str"))");\
	is(b1, b2, "decimal_unpack(decimal_pack("str")) len");\
	is(decimal_compare(&dec, &d2), 0, "decimal_unpack(decimal_pack("str")) value");\
	is(decimal_scale(&dec), decimal_scale(&d2), "decimal_unpack(decimal_pack("str")) scale");\
	is(decimal_precision(&dec), decimal_precision(&d2), "decimal_unpack(decimal_pack("str")) precision");\
	is(strcmp(decimal_str(&d2), str), 0, "str(decimal_unpack(decimal_pack("str")) == "str);\
})

#define test_toint(type, num, out_fmt) ({\
	decimal_t dec;\
	type##_t val;\
	decimal_from_##type(&dec, num);\
	isnt(decimal_to_##type(&dec, &val), NULL, "Conversion of %"out_fmt\
						  " to decimal and back to "#type" successful", (type##_t) num);\
	is(val, num, "Conversion back to "#type" correct");\
})

struct canary {
	decimal_t dec;
	uint32_t val;
};

const uint32_t magic = 0xdecdecde;

#define test_unpack(str, len, expected, exp_val) ({\
	struct canary canary = {\
		.dec = {0},\
		.val = magic,\
	};\
	const char *bb = str;\
	is(decimal_unpack((const char **)&bb, len, &canary.dec),\
	   expected(&canary.dec), "Decode "#expected);\
	is(canary.val, magic, "Canary is intact");\
	if (expected(&canary.dec) != NULL) {\
		is(bb, str + len, "Whole string is processed");\
		decimal_t dec;\
		decimal_from_string(&dec, exp_val);\
		is(decimal_compare(&canary.dec, &dec), 0,\
		   "Decoding is correct");\
	} else {\
		is(bb, str, "Buffer position is restored");\
	} \
})

static int
test_pack_unpack(void)
{
	plan(235);

	test_decpack("0");
	test_decpack("-0");
	test_decpack("1");
	test_decpack("-1");
	test_decpack("0.1");
	test_decpack("-0.1");
	test_decpack("2.718281828459045");
	test_decpack("-2.718281828459045");
	test_decpack("3.141592653589793");
	test_decpack("-3.141592653589793");
	test_decpack("1234567891234567890.0987654321987654321");
	test_decpack("-1234567891234567890.0987654321987654321");
	test_decpack("1E-37");
	test_decpack("-1E-37");
	test_decpack("1E-38");
	test_decpack("-1E-38");
	test_decpack("99999999999999999999999999999999999999");
	test_decpack("-99999999999999999999999999999999999999");
	test_decpack("9.99E+1000");
	test_decpack("-9.99E-1000");
	/* Decimal with 38 significant digits and maximum exponent. */
	test_decpack("9.9999999999999999999999999999999999999E+999999");
	/* Normal decimal with 38 significant digits and minimum exponent. */
	test_decpack("9.9999999999999999999999999999999999999E-999999");
	/* Minimal subnormal decimal. */
	test_decpack("1E-1000036");
	/* Another subnormal decimal. */
	test_decpack("9.99E-1000034");

	/* Check correct encoding of positive exponent numbers. */
	decimal_t dec, d1;
	decimal_from_string(&dec, "1e10");
	uint32_t l1 = decimal_len(&dec);
	ok(l1 == 2, "decimal_len() is small for positive exponent decimal");
	char *b1 = decimal_pack(buf, &dec);
	is(b1, buf + l1, "positive exponent decimal length");
	const char *b2 = buf;
	is(decimal_unpack(&b2, l1, &d1), &d1, "decimal_unpack() of a positive exponent decimal");
	is(b1, b2, "decimal_unpack uses every byte packed by decimal_pack");
	is(decimal_compare(&dec, &d1), 0, "positive exponent number is packed/unpacked correctly");

	/* Pack an invalid decimal. */
	char *b = buf;
	*b++ = 1;
	*b++ = '\xab';
	*b++ = '\xcd';
	const char *bb = buf;
	is(decimal_unpack(&bb, 3, &dec), NULL, "unpack malformed decimal fails");
	is(bb, buf, "decode malformed decimal preserves buffer position");

	/* Test buffer overflows on unpack. */
	/* Only scale, no digits. */
	b = "\x00";
	test_unpack(b, 1, failure, "");
	b = "\x00\x9c";
	test_unpack(b, 2, success, "9");
	b = "\x26\x09\x99\x99\x99\x99\x99\x99"
	    "\x99\x99\x99\x99\x99\x99\x99\x99"
	    "\x99\x99\x99\x99\x9c";
	test_unpack(b, 21, success, "0.99999999999999999999999999999999999999");
	b = "\x00\x09\x99\x99\x99\x99\x99\x99"
	    "\x99\x99\x99\x99\x99\x99\x99\x99"
	    "\x99\x99\x99\x99\x9c";
	test_unpack(b, 21, success, "99999999999999999999999999999999999999");
	/* 999e+999998 with adjusted exponent of 1000000. */
	b = "\xd2\xff\xf0\xbd\xc2\x99\x9c";
	test_unpack(b, 7, failure, "");
	/* 9e-1000037 cannot be represented as subnormal. */
	b = "\xce\x00\x0f\x42\x65\x9c";
	test_unpack(b, 6, failure, "");
	/* 9999e-1000037 cannot be represented as subnormal. */
	b = "\xce\x00\x0f\x42\x65\x09\x99\x9c";
	test_unpack(b, 8, failure, "");
	/* Missing nibble. */
	b = "\x00\x09\x99\x99\x99\x99\x99\x99"
	    "\x99\x99\x99\x99\x99\x99\x99\x99"
	    "\x99\x99\x99\x99\x99";
	test_unpack(b, 21, failure, "");
	/*         V - 39th digit overflows the buffer. */
	b = "\x00\x99\x99\x99\x99\x99\x99\x99"
	    "\x99\x99\x99\x99\x99\x99\x99\x99"
	    "\x99\x99\x99\x99\x9c";
	test_unpack(b, 21, failure, "");
	/* Too long, non-empty. */
	b = "\x00\x99\x99\x99\x99\x99\x99\x99"
	    "\x99\x99\x99\x99\x99\x99\x99\x99"
	    "\x99\x99\x99\x99\x99\x9c";
	test_unpack(b, 22, failure, "");
	/* Too long, empty. Still fails. */
	b = "\x00\x00\x00\x00\x00\x00\x00\x00"
	    "\x00\x00\x00\x00\x00\x00\x00\x00"
	    "\x00\x00\x00\x00\x00\x0c";
	test_unpack(b, 22, failure, "");
	return check_plan();
}

static int
test_mp_decimal(void)
{
	plan(216);

	test_mpdec("0");
	test_mpdec("-0");
	test_mpdec("1");
	test_mpdec("-1");
	test_mpdec("0.1");
	test_mpdec("-0.1");
	test_mpdec("2.718281828459045");
	test_mpdec("-2.718281828459045");
	test_mpdec("3.141592653589793");
	test_mpdec("-3.141592653589793");
	test_mpdec("1234567891234567890.0987654321987654321");
	test_mpdec("-1234567891234567890.0987654321987654321");
	test_mpdec("1E-37");
	test_mpdec("-1E-37");
	test_mpdec("1E-38");
	test_mpdec("-1E-38");
	test_mpdec("99999999999999999999999999999999999999");
	test_mpdec("-99999999999999999999999999999999999999");

	return check_plan();
}

static int
test_to_int(void)
{
	plan(66);

	test_toint(uint64, ULLONG_MAX, PRIu64);
	test_toint(int64, LLONG_MAX, PRId64);
	test_toint(int64, LLONG_MIN, PRId64);
	test_toint(uint64, 0, PRIu64);
	test_toint(int64, 0, PRId64);
	test_toint(int64, -1, PRId64);

	/* test some arbitrary values. */
	test_toint(uint64, ULLONG_MAX / 157, PRIu64);
	test_toint(int64, LLONG_MAX / 157, PRId64);
	test_toint(int64, LLONG_MIN / 157, PRId64);

	test_toint(uint64, ULLONG_MAX / 157 / 151, PRIu64);
	test_toint(int64, LLONG_MAX / 157 / 151, PRId64);
	test_toint(int64, LLONG_MIN / 157 / 151, PRId64);

	test_toint(uint64, ULLONG_MAX / 157 / 151 / 149, PRIu64);
	test_toint(int64, LLONG_MAX / 157 / 151 / 149, PRId64);
	test_toint(int64, LLONG_MIN / 157 / 151 / 149, PRId64);

	test_toint(uint64, ULLONG_MAX / 157 / 151 / 149 / 139, PRIu64);
	test_toint(int64, LLONG_MAX / 157 / 151 / 149 / 139, PRId64);
	test_toint(int64, LLONG_MIN / 157 / 151 / 149 / 139, PRId64);

	test_toint(uint64, ULLONG_MAX / 157 / 151 / 149 / 139 / 137, PRIu64);
	test_toint(int64, LLONG_MAX / 156 / 151 / 149 / 139 / 137, PRId64);
	test_toint(int64, LLONG_MIN / 156 / 151 / 149 / 139 / 137, PRId64);

	test_toint(uint64, UINT_MAX, PRIu64);
	test_toint(int64, INT_MAX, PRId64);
	test_toint(int64, INT_MIN, PRId64);

	test_toint(uint64, UINT_MAX / 157, PRIu64); /* ~ 27356479 */
	test_toint(int64, INT_MAX / 157, PRId64);
	test_toint(int64, INT_MIN / 157, PRId64);

	test_toint(uint64, UINT_MAX / 157 / 151, PRIu64); /* ~ 181168 */
	test_toint(int64, INT_MAX / 157 / 151, PRId64);
	test_toint(int64, INT_MIN / 157 / 151, PRId64);

	test_toint(uint64, UINT_MAX / 157 / 151 / 149, PRIu64); /* ~ 1215 */
	test_toint(int64, INT_MAX / 157 / 151 / 149, PRId64);
	test_toint(int64, INT_MIN / 157 / 151 / 149, PRId64);

	return check_plan();
}

static int
mp_fprint_ext_test(FILE *file, const char **data, int depth)
{
	(void)depth;
	int8_t type;
	uint32_t len = mp_decode_extl(data, &type);
	if (type != MP_DECIMAL)
		return fprintf(file, "undefined");
	return mp_fprint_decimal(file, data, len);
}

static int
mp_snprint_ext_test(char *buf, int size, const char **data, int depth)
{
	(void)depth;
	int8_t type;
	uint32_t len = mp_decode_extl(data, &type);
	if (type != MP_DECIMAL)
		return snprintf(buf, size, "undefined");
	return mp_snprint_decimal(buf, size, data, len);
}

static void
test_mp_print(void)
{
	plan(5);
	header();

	mp_snprint_ext = mp_snprint_ext_test;
	mp_fprint_ext = mp_fprint_ext_test;

	char buffer[1024];
	char str[1024];
	const char *expected = "1.234";
	const int len = strlen(expected);
	decimal_t d;
	decimal_from_string(&d, expected);
	mp_encode_decimal(buffer, &d);
	int rc = mp_snprint(NULL, 0, buffer);
	is(rc, len, "correct mp_snprint size with empty buffer");
	rc = mp_snprint(str, sizeof(str), buffer);
	is(rc, len, "correct mp_snprint size");
	is(strcmp(str, expected), 0, "correct mp_snprint result");

	FILE *f = tmpfile();
	rc = mp_fprint(f, buffer);
	is(rc, len, "correct mp_fprint size");
	rewind(f);
	rc = fread(str, 1, sizeof(str), f);
	str[rc] = 0;
	is(strcmp(str, expected), 0, "correct mp_fprint result");
	fclose(f);

	mp_snprint_ext = mp_snprint_ext_default;
	mp_fprint_ext = mp_fprint_ext_default;

	footer();
	check_plan();
}

static void
test_print(void)
{
	plan(4);
	header();

	char buf[DECIMAL_MAX_STR_LEN + 1];
	decimal_t d;

	is(decimal_from_string(&d, "1e1000"), &d, "decimal(1e1000)");
	decimal_to_string(&d, buf);
	is(strcmp("1E+1000", buf), 0, "checking to_string(1e1000)");

	is(decimal_from_string(&d, "1e-1000"), &d, "decimal(1e-1000)");
	decimal_to_string(&d, buf);
	is(strcmp("1E-1000", buf), 0, "checking to_string(1e-1000)");

	footer();
	check_plan();
}

int
main(void)
{
	plan(325);

	dectest(314, 271, uint64, uint64_t);
	dectest(65535, 23456, uint64, uint64_t);

	dectest(0, 1, int64, int64_t);
	dectest(0, -1, int64, int64_t);
	dectest(-1, 1, int64, int64_t);
	dectest(INT_MIN, INT_MAX, int64, int64_t);
	dectest(-314, -271, int64, int64_t);
	dectest(-159615516, 172916921, int64, int64_t);

	dectest(1.1, 2.3, double, double);
	dectest(1e10, 1e10, double, double);
	dectest(1.23456789, 4.567890123, double, double);

	dectest_op(add, 1e-38, 1e-38, 2e-38);
	dectest_op(add, -1e-38, 1e-38, 0);
	dectest_op(mul, 1e-19, 1e-19, 1e-38);
	dectest_op(add, 1e37, 0, 1e37);
	dectest_op(mul, 1e18, 1e18, 1e36);

	dectest_op(pow, 10, 2, 100);
	dectest_op(pow, 2, 10, 1024);
	dectest_op(pow, 100, 0.5, 10);

	dectest_op(add, 1e1000, 1e1000, 2e1000);
	dectest_op(add, 1e-1000, 1e-1000, 2e-1000);
	dectest_op(mul, 1e1000, 1e1000, 1e2000);
	dectest_op(mul, 1e-1000, 1e-1000, 1e-2000);
	dectest_op(div, 1e1000, 1e-1000, 1e2000);
	dectest_op(div, 1e-1000, 1e1000, 1e-2000);

	dectest_op1(log10, 100, 2, 0);
	dectest_op1(ln, 10, 2.3, 2);
	dectest_op1(ln, 1.1, 0.1, 1);
	dectest_op1(ln, 1.0000000000000000000000000000000000001,
		    0.0000000000000000000000000000000000001, 0);
	dectest_op1(exp, 2, 7.39, 2);
	dectest_op1(sqrt, 100, 10, 0);

	/* Check large exponents. */
	dectest_construct(double, 1e300, success);
	dectest_construct(double, 1e-300, success);
	dectest_construct(string, "1e1000", success);
	dectest_construct(string, "1e-1000", success);
	/* Check that inf and NaN are not allowed. Check bad input. */
	dectest_construct(string, "inf", failure);
	dectest_construct(string, "NaN", failure);
	dectest_construct(string, "a random string", failure);

	dectest_construct(int64, LONG_MIN, success);
	dectest_construct(int64, LONG_MAX, success);
	dectest_construct(uint64, ULONG_MAX, success);

	dectest_op1_fail(ln, 0);
	dectest_op1_fail(ln, -1);
	dectest_op1_fail(log10, 0);
	dectest_op1_fail(log10, -1);
	dectest_op1_fail(sqrt, -10);

	test_to_int();

	test_pack_unpack();

	test_mp_decimal();
	test_mp_print();
	test_print();

	test_strtodec("15.e", 'e', success);
	test_strtodec("15.e+", 'e', success);
	test_strtodec(".0e-1", '\0', success);
	test_strtodec("1.1003 2.2", ' ', success);
	test_strtodec("cCC", 'c', failure);
	test_strtodec(".e--", '.', failure);
	test_strtodec("NaN", 'N', failure);
	test_strtodec("inf", 'i', failure);

	dectest_is(is_int, 1, true);
	dectest_is(is_int, 1.0000, true);
	dectest_is(is_int, 1.0000001, false);

	dectest_is(is_neg, 1, false);
	dectest_is(is_neg, -1, true);
	dectest_is(is_neg, 0, false);
	dectest_is(is_neg, -0, false);

	return check_plan();
}
