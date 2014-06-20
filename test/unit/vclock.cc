/*
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
#include "test.h"
} /* extern "C" */

#include <stdarg.h>

#include "box/vclock.h"

#define header() note("*** %s ***", __func__)
#define footer() note("*** %s: done ***", __func__)
#define str2(x) #x
#define str(x) str2(x)

static inline int
test_compare_one(uint32_t a_count, const int64_t *lsns_a,
		 uint32_t b_count, const int64_t *lsns_b)
{
	struct vclock a;
	struct vclock b;
	vclock_create(&a);
	vclock_create(&b);
	for (uint32_t node_id = 0; node_id < a_count; node_id++) {
		vclock_add_server(&a, node_id);
		if (lsns_a[node_id] > 0)
			vclock_follow(&a, node_id, lsns_a[node_id]);
	}
	for (uint32_t node_id = 0; node_id < b_count; node_id++) {
		vclock_add_server(&b, node_id);
		if (lsns_b[node_id] > 0)
			vclock_follow(&b, node_id, lsns_b[node_id]);
	}

	int result = vclock_compare(&a, &b);
	vclock_destroy(&a);
	vclock_destroy(&b);
	return result;
}

#define arg(...) __VA_ARGS__
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
#undef arg

static void
testset_create(vclockset_t *set, int64_t *files, int files_n, int node_n)
{
	vclockset_new(set);

	for (int f = 0; f < files_n; f++) {
		struct vclock *vclock = (struct vclock *) malloc(sizeof(*vclock));
		vclock_create(vclock);
		int64_t signature = 0;
		for (uint32_t node_id = 0; node_id < node_n; node_id++) {
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
		vclock_destroy(cur);
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
		{  0,  0, 0, 0, /* => */ INT64_MAX},
		{  1,  0, 0, 0, /* => */ INT64_MAX},
		{  5,  0, 0, 0, /* => */ INT64_MAX},

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

			vclock_add_server(&vclock, node_id);
			vclock_follow(&vclock, node_id, lsn);
		}

		int64_t check = *(query + NODE_N);
		struct vclock *res = vclockset_isearch(&set, &vclock);
		int64_t value = res != NULL ? vclock_signature(res) : INT64_MAX;
		is(value, check, "query #%d", q + 1);

		vclock_destroy(&vclock);
	}

	testset_destroy(&set);

	footer();
	return check_plan();
}

int
main(void)
{
	plan(2);

	test_compare();
	test_isearch();

	return check_plan();
}
