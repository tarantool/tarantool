#include <stdio.h>
#include <time.h>

#include "unit.h"
#include "salad/guava.h"

static void
check_guava_correctness(uint64_t code)
{
	int32_t last = 0;
	for (int32_t shards = 1; shards <= 100000; shards++) {
		int32_t b = guava(code, shards);
		if (b != last) {
			fail_if(shards - 1 != b);
			last = b;
		}
	}
}

static void
correctness_check()
{
	header();
	int64_t i_vals[] = {0, 1, 2};
	for (size_t i = 0; i < sizeof(i_vals) / sizeof(int64_t); ++i)
		check_guava_correctness(i_vals[i]);
	srand(time(NULL));
	for (size_t i = 0; i < 20; ++i)
		check_guava_correctness(rand() % 7);
	footer();
}

static void
sameresult_check()
{
	header();
	fail_if(guava(100, 20) != guava(100, 20));
	footer();
}

static void
lcg_compat_check()
{
	header();
	int32_t golden100[] = {
		0, 55, 62, 8, 45, 59, 86, 97, 82, 59,
		73, 37, 17, 56, 86, 21, 90, 37, 38, 83
	};
	size_t nr_elems = sizeof(golden100) / sizeof(golden100[0]);
	for (size_t i = 0; i < nr_elems; ++i)
		check_guava_correctness(golden100[i]);

	fail_if(6     != guava(10863919174838991ULL, 11));
	fail_if(3     != guava(2016238256797177309ULL, 11));
	fail_if(5     != guava(1673758223894951030ULL, 11));
	fail_if(80343 != guava(2, 100001));
	fail_if(22152 != guava(2201, 100001));
	fail_if(15018 != guava(2202, 100001));
	footer();
}

int
main(void)
{
	correctness_check();
	lcg_compat_check();
	sameresult_check();
}
