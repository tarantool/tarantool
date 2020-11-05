/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
extern "C" {
#include "unit.h"
} /* extern "C" */

#include <stdarg.h>

#include "vclock/vclock.h"

#define str2(x) #x
#define str(x) str2(x)
#define arg(...) __VA_ARGS__

static inline int
test_compare_one(uint32_t a_count, const int64_t *lsns_a,
		 uint32_t b_count, const int64_t *lsns_b)
{
	struct vclock a;
	struct vclock b;
	vclock_create(&a);
	vclock_create(&b);
	for (uint32_t node_id = 0; node_id < a_count; node_id++) {
		if (lsns_a[node_id] > 0)
			vclock_follow(&a, node_id, lsns_a[node_id]);
	}
	for (uint32_t node_id = 0; node_id < b_count; node_id++) {
		if (lsns_b[node_id] > 0)
			vclock_follow(&b, node_id, lsns_b[node_id]);
	}

	return vclock_compare(&a, &b);
}

#define test2(xa, xb, res) ({\
	const int64_t a[] = {xa}, b[] = {xb};				\
	is(test_compare_one(sizeof(a) / sizeof(*a), a, sizeof(b) / sizeof(*b), b), res,	\
		"compare %s, %s => %d", str((xa)), str((xb)), res); })
#define test(a, b, res) ({ test2(arg(a), arg(b), res); \
	test2(arg(b), arg(a), res != VCLOCK_ORDER_UNDEFINED ? -res : res); })

int
test_compare()
{
	plan(40);
	header();

	test(arg(), arg(), 0);
	test(arg(), arg(10), -1);
	test(arg(0), arg(0), 0);
	test(arg(1), arg(1), 0);
	test(arg(1), arg(2), -1);
	test(arg(), arg(10, 1, 0), -1);
	test(arg(5), arg(10, 1, 0), -1);
	test(arg(10), arg(10, 1, 0), -1);
	test(arg(15), arg(10, 1, 0), VCLOCK_ORDER_UNDEFINED);
	test(arg(10, 1, 0), arg(10, 1, 1), -1);
	test(arg(10, 1, 0), arg(10, 2, 0), -1);
	test(arg(10, 1, 0), arg(10, 1, 0), 0);
	test(arg(10, 0, 1), arg(10, 1, 0), VCLOCK_ORDER_UNDEFINED);
	test(arg(10, 2, 1), arg(10, 1, 2), VCLOCK_ORDER_UNDEFINED);
	test(arg(10, 0, 1), arg(11, 0, 0), VCLOCK_ORDER_UNDEFINED);
	test(arg(10, 0, 5), arg(5, 0, 10), VCLOCK_ORDER_UNDEFINED);
	test(arg(10, 10, 10), arg(10, 10, 10), 0);
	test(arg(10, 10, 10), arg(10, 10, 10, 1), -1);
	test(arg(10, 10, 10), arg(10, 10, 10, 1, 2, 3), -1);
	test(arg(0, 0, 0), arg(10, 0, 0, 0, 0), -1);

	footer();
	return check_plan();
}

#undef test
#undef test2

static void
testset_create(vclockset_t *set, int64_t *files, int files_n, int node_n)
{
	vclockset_new(set);

	for (int f = 0; f < files_n; f++) {
		struct vclock *vclock = (struct vclock *) malloc(sizeof(*vclock));
		vclock_create(vclock);
		int64_t signature = 0;
		for (int32_t node_id = 0; node_id < node_n; node_id++) {
			int64_t lsn = *(files + f * node_n + node_id);
			if (lsn <= 0)
				continue;

			/* Calculate LSNSUM */
			signature += lsn;

			/* Update cluster hash */
			vclock_follow(vclock, node_id, lsn);
		}
		vclockset_insert(set, vclock);
	}
}

static void
testset_destroy(vclockset_t *set)
{
	struct vclock *cur = vclockset_first(set);
	while (cur != NULL) {
		struct vclock *next = vclockset_next(set, cur);
		vclockset_remove(set, cur);
		free(cur);
		cur = next;
	}
}

static int
test_isearch()
{
	plan(36);
	header();

	enum { NODE_N = 4};
	int64_t files[][NODE_N] = {
		{ 10, 0, 0, 0}, /* =10.xlog */
		{ 12, 2, 0, 0}, /* =14.xlog */
		{ 14, 2, 0, 0}, /* =16.xlog */
		{ 14, 2, 2, 0}, /* =18.xlog */
		{ 14, 4, 2, 3}, /* =23.xlog */
		{ 14, 4, 2, 5}, /* =25.xlog */
	};
	enum { FILE_N = sizeof(files) / (sizeof(files[0])) };

	int64_t queries[][NODE_N + 1] = {
		/* not found (lsns are too old) */
		{  0,  0, 0, 0, /* => */ 10},
		{  1,  0, 0, 0, /* => */ 10},
		{  5,  0, 0, 0, /* => */ 10},

		/* =10.xlog (left bound) */
		{  10, 0, 0, 0, /* => */ 10},
		{  10, 1, 0, 0, /* => */ 10},
		{  10, 2, 0, 0, /* => */ 10},
		{  10, 3, 0, 0, /* => */ 10},
		{  10, 4, 0, 0, /* => */ 10},

		/* =10.xlog (middle) */
		{  11, 0, 0, 0, /* => */ 10},
		{  11, 1, 0, 0, /* => */ 10},
		{  11, 2, 0, 0, /* => */ 10},
		{  11, 3, 0, 0, /* => */ 10},
		{  11, 4, 0, 0, /* => */ 10},
		{  11, 5, 3, 6, /* => */ 10},

		/* =10.xlog (right bound) */
		{  12, 0, 0, 0, /* => */ 10},
		{  12, 1, 0, 0, /* => */ 10},
		{  12, 1, 1, 1, /* => */ 10},
		{  12, 1, 2, 5, /* => */ 10},

		/* =14.xlog */
		{  12, 2, 0, 0, /* => */ 14},
		{  12, 3, 0, 0, /* => */ 14},
		{  12, 4, 0, 0, /* => */ 14},
		{  12, 5, 3, 6, /* => */ 14},

		/* =16.xlog */
		{  14, 2, 0, 0, /* => */ 16},
		{  14, 2, 1, 0, /* => */ 16},
		{  14, 2, 0, 1, /* => */ 16},

		/* =18.xlog */
		{  14, 2, 2, 0, /* => */ 18},
		{  14, 2, 4, 0, /* => */ 18},
		{  14, 2, 4, 3, /* => */ 18},
		{  14, 2, 4, 5, /* => */ 18},
		{  14, 4, 2, 0, /* => */ 18},
		{  14, 5, 2, 0, /* => */ 18},

		/* =23.xlog */
		{  14, 4, 2, 3, /* => */ 23},
		{  14, 5, 2, 3, /* => */ 23},

		/* =25.xlog */
		{  14, 4, 2, 5, /* => */ 25},
		{  14, 5, 2, 6, /* => */ 25},
		{ 100, 9, 9, 9, /* => */ 25},
	};
	enum { QUERY_N = sizeof(queries) / (sizeof(queries[0])) };

	vclockset_t set;
	testset_create(&set, (int64_t *) files, FILE_N, NODE_N);

	for (int q = 0; q < QUERY_N; q++) {
		struct vclock vclock;
		vclock_create(&vclock);
		int64_t *query = (int64_t *) queries + q * (NODE_N + 1);

		/* Update cluster hash */
		for (uint32_t node_id = 0; node_id < NODE_N; node_id++) {
			int64_t lsn = *(query + node_id);
			if (lsn <= 0)
				continue;

			vclock_follow(&vclock, node_id, lsn);
		}

		int64_t check = *(query + NODE_N);
		struct vclock *res = vclockset_match(&set, &vclock);
		int64_t value = res != NULL ? vclock_sum(res) : INT64_MAX;
		is(value, check, "query #%d", q + 1);
	}

	testset_destroy(&set);

	footer();
	return check_plan();
}

static inline int
test_tostring_one(uint32_t count, const int64_t *lsns, const char *res)
{
	struct vclock vclock;
	vclock_create(&vclock);
	for (uint32_t node_id = 0; node_id < count; node_id++) {
		if (lsns[node_id] > 0)
			vclock_follow(&vclock, node_id, lsns[node_id]);
	}
	const char *str = vclock_to_string(&vclock);
	int result = strcmp(str, res);
	if (result)
		diag("\n!!!new result!!! %s\n", str);
	return !result;
}

#define test(xa, res) ({\
	const int64_t a[] = {xa};				\
	ok(test_tostring_one(sizeof(a) / sizeof(*a), a, res),	\
		"tostring %s => %s", str((xa)), res); })
int
test_tostring()
{
	plan(8);
	header();

	test(arg(), "{}");
	test(arg(-1, -1, -1), "{}");
	test(arg(1), "{0: 1}");
	test(arg(1, 2), "{0: 1, 1: 2}");
	test(arg(10, 15, 20), "{0: 10, 1: 15, 2: 20}");
	test(arg(10, -1, 15, -1, 20), "{0: 10, 2: 15, 4: 20}");
	test(arg(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15),
	     "{1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 6: 6, 7: 7, 8: 8, "
	      "9: 9, 10: 10, 11: 11, 12: 12, 13: 13, 14: 14, 15: 15}");
	test(arg(9223372054775000, 9223372054775001, 9223372054775002,
		 9223372054775003, 9223372054775004, 9223372054775005,
		 9223372054775006, 9223372054775007, 9223372054775008,
		 9223372054775009, 9223372054775010, 9223372054775011,
		 9223372054775012, 9223372054775013, 9223372054775014,
		 9223372054775015),
	     "{0: 9223372054775000, 1: 9223372054775001, "
	      "2: 9223372054775002, 3: 9223372054775003, "
	      "4: 9223372054775004, 5: 9223372054775005, "
	      "6: 9223372054775006, 7: 9223372054775007, "
	      "8: 9223372054775008, 9: 9223372054775009, "
	     "10: 9223372054775010, 11: 9223372054775011, "
	     "12: 9223372054775012, 13: 9223372054775013, "
	     "14: 9223372054775014, 15: 9223372054775015}");

	footer();
	return check_plan();
}

#undef test

static inline int
test_fromstring_one(const char *str, uint32_t count, const int64_t *lsns)
{
	struct vclock vclock;
	vclock_create(&vclock);
	size_t rc = vclock_from_string(&vclock, str);

	struct vclock check;
	vclock_create(&check);
	for (uint32_t node_id = 0; node_id < count; node_id++) {
		if (lsns[node_id] >= 0)
			vclock_follow(&check, node_id, lsns[node_id]);
	}

	return (rc != 0 || vclock_compare(&vclock, &check) != 0);
}

#define test(s, xa) ({\
	const int64_t a[] = {xa};				\
	ok(!test_fromstring_one(s, sizeof(a) / sizeof(*a), a),	\
		"fromstring %s => %s", s, str((xa))); })
int
test_fromstring()
{
	plan(12);
	header();

	test("{}", arg());
	test(" \t \t { \t \t } \t \t ", arg());
	test("{0: 10}", arg(10));
	test("{0: 10,}", arg(10));
	test("{\t 0\t :\t  10\t ,\t }", arg(10));
	test("{0: 10, 1: 15, 3: 20}", arg(10, 15, -1, 20));
	test("{2: 20, 0: 10, 4: 30}", arg(10, -1, 20, -1, 30));
	test("{4: 30, 2: 20}", arg(-1, -1, 20, -1, 30));
	test("{4: 30, 2: 20,}", arg(-1, -1, 20, -1, 30));
	test("{0: 4294967295}", arg(4294967295));
	test("{0: 4294967296}", arg(4294967296));
	test("{0: 9223372036854775807}", arg(9223372036854775807));

	footer();
	return check_plan();
}
#undef test


#define test(str, offset) ({						\
	struct vclock tmp;						\
	vclock_create(&tmp);						\
	is(vclock_from_string(&tmp, str), offset,			\
		"fromstring \"%s\" => %u", str, offset)})

int
test_fromstring_invalid()
{
	plan(32);
	header();

	/* invalid symbols */
	test("", 1);
	test(" ", 2);
	test("\t \t \t ", 7);
	test("}", 1);
	test("1: 10", 1);
	test("abcde", 1);
	test("12345", 1);
	test("\1\2\3\4\5\6", 1);

	/* truncated */
	test("{", 2);
	test("{1\t ", 5);
	test("{1:\t ", 6);
	test("{1:10", 6);
	test("{1:10\t ", 8);
	test("{1:10,", 7);
	test("{1:10,\t \t ", 11);

	/* comma */
	test("{1:10 2:20", 7);
	test("{1:10,,", 7);
	test("{1:10, 10,}", 10);

	/* invalid values */
	test("{1:-1}", 4);
	test("{-1:1}", 2);
	test("{128:1}", 5); /* node_id > VCLOCK_MAX */
	test("{1:abcde}", 4);
	test("{abcde:1}", 2);
	test("{1:1.1}", 5);
	test("{1.1:1}", 3);
	test("{4294967296:1}", 12);
	test("{1:9223372036854775808}", 23);
	test("{1:18446744073709551616}", 24);
	test("{1:18446744073709551616}", 24);
	test("{1:340282366920938463463374607431768211456}", 43);

	/* duplicate */
	test("{1:10, 1:20}", 12);
	test("{1:20, 1:10}", 12);

	footer();
	return check_plan();
}

#undef test

int
main(void)
{
	plan(5);

	test_compare();
	test_isearch();
	test_tostring();
	test_fromstring();
	test_fromstring_invalid();

	return check_plan();
}
