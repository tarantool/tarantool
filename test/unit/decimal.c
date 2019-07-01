#include "unit.h"
#include "decimal.h"
#include <limits.h>
#include <string.h>
#include <float.h> /* DBL_DIG */

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

#define dectest_op_fail(op, stra, strb) ({\
	decimal_t a, b, c;\
	is(decimal_from_string(&a, #stra), &a, "decimal_from_string("#stra")");\
	is(decimal_from_string(&b, #strb), &b, "decimal_from_string("#strb")");\
	is(decimal_##op(&c, &a, &b), NULL, "decimal_"#op"("#stra", "#strb") - overflow");\
})

#define dectest_op1_fail(op, stra) ({\
	decimal_t a, b;\
	is(decimal_from_string(&a, #stra), &a, "decimal_from_string("#stra")");\
	is(decimal_##op(&b, &a), NULL, "decimal_"#op"("#stra") - error on wrong operands.");\
})

char buf[32];

#define test_decpack(str) ({\
	decimal_t dec;\
	decimal_from_string(&dec, str);\
	uint32_t l1 = decimal_len(&dec);\
	ok(l1 <= 21 && l1 >= 2, "decimal_len("str")");\
	char *b1 = decimal_pack(buf, &dec);\
	is(b1, buf + l1, "decimal_len("str") == len(decimal_pack("str")");\
	const char *b2 = buf;\
	decimal_t d2;\
	is(decimal_unpack(&b2, l1, &d2), &d2, "decimal_unpack(decimal_pack("str"))");\
	is(b1, b2, "decimal_unpack(decimal_pack("str")) len");\
	is(decimal_compare(&dec, &d2), 0, "decimal_unpack(decimal_pack("str")) value");\
	is(decimal_scale(&dec), decimal_scale(&d2), "decimal_unpack(decimal_pack("str")) scale");\
	is(decimal_precision(&dec), decimal_precision(&d2), "decimal_unpack(decimal_pack("str")) precision");\
	is(strcmp(decimal_to_string(&d2), str), 0, "str(decimal_unpack(decimal_pack("str")) == "str);\
})

static int
test_pack_unpack(void)
{
	plan(146);

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
	test_decpack("0.0000000000000000000000000000000000001");
	test_decpack("-0.0000000000000000000000000000000000001");
	test_decpack("0.00000000000000000000000000000000000001");
	test_decpack("-0.00000000000000000000000000000000000001");
	test_decpack("99999999999999999999999999999999999999");
	test_decpack("-99999999999999999999999999999999999999");

	/* Pack an invalid decimal. */
	char *b = buf;
	*b++ = 1;
	*b++ = '\xab';
	*b++ = '\xcd';
	const char *bb = buf;
	decimal_t dec;
	is(decimal_unpack(&bb, 3, &dec), NULL, "unpack malformed decimal fails");
	is(bb, buf, "decode malformed decimal preserves buffer position");

	return check_plan();
}

int
main(void)
{
	plan(279);

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
	/* Check that maximum scale == 38. Otherwise rounding occurs. */
	dectest_op(add, 1e-39, 0, 0);
	dectest_op(add, 1e-39, 1e-38, 1e-38);
	dectest_op(mul, 1e-19, 1e-19, 1e-38);
	dectest_op(add, 1e37, 0, 1e37);
	dectest_op(mul, 1e18, 1e18, 1e36);

	dectest_op(pow, 10, 2, 100);
	dectest_op(pow, 2, 10, 1024);
	dectest_op(pow, 100, 0.5, 10);

	dectest_op1(log10, 100, 2, 0);
	dectest_op1(ln, 10, 2.3, 2);
	dectest_op1(ln, 1.1, 0.1, 1);
	dectest_op1(ln, 1.0000000000000000000000000000000000001,
		    0.0000000000000000000000000000000000001, 0);
	dectest_op1(exp, 2, 7.39, 2);
	dectest_op1(sqrt, 100, 10, 0);

	/* 39 digits > DECIMAL_MAX_DIGITS (== 38) */
	dectest_construct(double, 2e38, failure);
	dectest_construct(string, "1e38", failure);
	dectest_construct(string, "100000000000000000000000000000000000000", failure);
	/* Check that inf and NaN are not allowed. Check bad input. */
	dectest_construct(string, "inf", failure);
	dectest_construct(string, "NaN", failure);
	dectest_construct(string, "a random string", failure);

	dectest_construct(int64, LONG_MIN, success);
	dectest_construct(int64, LONG_MAX, success);
	dectest_construct(uint64, ULONG_MAX, success);

	dectest_op_fail(add, 9e37, 1e37);
	dectest_op_fail(mul, 1e19, 1e19);
	dectest_op_fail(div, 1e19, 1e-19);

	dectest_op1_fail(ln, 0);
	dectest_op1_fail(ln, -1);
	dectest_op1_fail(log10, 0);
	dectest_op1_fail(log10, -1);
	dectest_op1_fail(sqrt, -10);

	test_pack_unpack();

	return check_plan();
}
