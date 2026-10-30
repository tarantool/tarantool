#include <bit/int96.h>

#include "unit.h"

#define check(expr) if (!(expr)) printf("failed at %s:%d\n", __FILE__, __LINE__)

static void
test()
{
	header();

	const uint64_t a = 0xFFFFFFFFFFFFFFFFull / 2;
	int96_num num, num1, num2;
	int96_set_unsigned(&num, 0);
	int96_set_unsigned(&num1, a);
	int96_set_unsigned(&num2, a);
	int96_invert(&num2);
	check(int96_is_neg_int64(&num2));
	check(int96_extract_neg_int64(&num2) == int64_t(-a));
	check(int96_is_uint64(&num));
	check(int96_extract_uint64(&num) == 0);
	int96_add(&num, &num1);
	check(int96_is_uint64(&num));
	check(int96_extract_uint64(&num) == a);
	int96_add(&num, &num1);
	check(int96_is_uint64(&num));
	check(int96_extract_uint64(&num) == a * 2);
	for (int i = 1; i < 1000; i++) {
		for(int j = 0; j < i; j++) {
			int96_add(&num, &num1);
			check(!int96_is_uint64(&num) && !int96_is_neg_int64(&num));
		}
		for(int j = 0; j < i - 1; j++) {
			int96_add(&num, &num2);
			check(!int96_is_uint64(&num) && !int96_is_neg_int64(&num));
		}
		int96_add(&num, &num2);
		check(int96_is_uint64(&num));
		check(int96_extract_uint64(&num) == a * 2);
	}
	int96_add(&num, &num2);
	check(int96_is_uint64(&num));
	check(int96_extract_uint64(&num) == a);
	int96_add(&num, &num2);
	check(int96_is_uint64(&num));
	check(int96_extract_uint64(&num) == 0);
	int96_add(&num, &num2);
	check(int96_is_neg_int64(&num));
	check(int96_extract_neg_int64(&num) == int64_t(-a));
	for (int i = 1; i < 1000; i++) {
		for(int j = 0; j < i; j++) {
			int96_add(&num, &num2);
			check(!int96_is_uint64(&num) && !int96_is_neg_int64(&num));
		}
		for(int j = 0; j < i - 1; j++) {
			int96_add(&num, &num1);
			check(!int96_is_uint64(&num) && !int96_is_neg_int64(&num));
		}
		int96_add(&num, &num1);
		check(int96_is_neg_int64(&num));
		check(int96_extract_neg_int64(&num) == int64_t(-a));
	}

	footer();
}

int
main(int, const char **)
{
	test();
}
