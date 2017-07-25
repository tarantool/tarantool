#include <stdio.h>
#include <assert.h>

#include "box/sql.h"
#include "box/sql/sqliteInt.h"

#ifdef SWAP
    #undef SWAP
#endif
#ifdef likely
    #undef likely
#endif
#ifdef unlikely
    #undef unlikely
#endif

#include "trivia/util.h"
#include "unit.h"

#define bitvec_test(a, b) sqlite3BitvecBuiltinTest((a), (b))

void
test_errors()
{
	plan(2);

	int test_1[] = { 5, 1, 1, 1, 0 };
	is(1, bitvec_test(400, test_1), "error test");
	int test_2[] = { 5, 1, 234, 1, 0 };
	is(234, bitvec_test(400, test_2), "error test");

	check_plan();
}

void
test_various_sizes()
{
	plan(4);

	int sz_args[] = { 400, 4000, 40000, 400000 };
	int test_args[][5] = {
		{ 1, 400, 1, 1, 0 },
		{ 1, 4000, 1, 1, 0 },
		{ 1, 40000, 1, 1, 0 },
		{ 1, 400000, 1, 1, 0 }
	};
	assert(lengthof(sz_args) == lengthof(test_args));

	for (size_t i = 0; i < lengthof(sz_args); i++) {
	       is(0, bitvec_test(sz_args[i], test_args[i]), "various sizes");
	}

	check_plan();
}


void
test_larger_increments()
{
	plan(4);

	int sz_args[] = { 400, 4000, 40000, 400000 };
	int test_args[][5] = {
		{ 1, 400, 1, 7, 0},
		{ 1, 4000, 1, 7, 0},
		{ 1, 40000, 1, 7, 0},
		{ 1, 400000, 1, 7, 0}
	};
	assert(lengthof(sz_args) == lengthof(test_args));

	for (size_t i = 0; i < lengthof(sz_args); i++) {
		is(0, bitvec_test(sz_args[i], test_args[i]),
		   "larger increments");
	}

	check_plan();
}

void
test_clearing_mechanism()
{
	plan(9);

	int sz_args[] = { 400, 4000, 40000, 400000, 400, 4000, 40000, 400000,
			  5000 };
	int test_args[][9] = {
		{1, 400, 1, 1, 2, 400, 1, 1, 0},
		{1, 4000, 1, 1, 2, 4000, 1, 1, 0},
		{1, 40000, 1, 1, 2, 40000, 1, 1, 0},
		{1, 400000, 1, 1, 2, 400000, 1, 1, 0},
		{1, 400, 1, 1, 2, 400, 1, 7, 0},
		{1, 4000, 1, 1, 2, 4000, 1, 7, 0},
		{1, 40000, 1, 1, 2, 40000, 1, 7, 0},
		{1, 400000, 1, 1, 2, 400000, 1, 7, 0},
		{1, 5000, 100000, 1, 2, 400000, 1, 37, 0}
	};
	assert(lengthof(sz_args) == lengthof(test_args));

	for (size_t i = 0; i < lengthof(sz_args); i++) {
		is(0, bitvec_test(sz_args[i], test_args[i]),
		   "clearing mechanism");
	}

	check_plan();
}

void
test_hashing_collisions()
{
	plan(10);

	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			int test_args[] = { 1, 60, i, j, 2, 5000, 1, 1, 0 };
			is(0, bitvec_test(5000, test_args),
			   "hashing collisions");
		}
	}

	int test_args[] = {1, 17000000, 1, 1, 2, 17000000, 1, 1, 0};
	is(0, bitvec_test(17000000, test_args), "hashing collisions");

	check_plan();
}

void
test_random_subsets()
{
	plan(7);

	int sz_args[] = { 4000, 4000, 400000, 4000, 5000, 50000, 5000 };
	/* Use 30 to avoid compile error and fit all subarrays. */
	int test_args[][30] = {
		{ 3, 2000, 4, 2000, 0 },
		{ 3, 1000, 4, 1000, 3, 1000, 4, 1000, 3, 1000, 4,
		  1000, 3, 1000, 4, 1000, 3, 1000, 4, 1000, 3, 1000, 4, 1000,
		  0 },
		{ 3, 10, 0 },
		{3, 10, 2, 4000, 1, 1, 0},
		{3, 20, 2, 5000, 1, 1, 0},
		{3, 60, 2, 50000, 1, 1, 0},
		{1, 25, 121, 125, 1, 50, 121, 125, 2, 25, 121, 125, 0},
	};
	assert(lengthof(sz_args) == lengthof(test_args));
	for (size_t i = 0; i < lengthof(sz_args); ++i)
		is(0, bitvec_test(sz_args[i], test_args[i]), "random subsets");

	check_plan();
}

int
main(void)
{
	plan(5);
	header();
	sqlite3MutexInit();
	sqlite3MallocInit();

	test_errors();
	test_various_sizes();
	test_larger_increments();
	test_clearing_mechanism();
	test_random_subsets();

	sqlite3MallocEnd();
	sqlite3MutexEnd();
	footer();
	return check_plan();
}
