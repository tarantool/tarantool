/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * A libFuzzer-based target for the B+'-tree in lib/salad/bps_tree.h.
 *
 * The harness drives a bounded stream of operations (insert, replace,
 * delete, find, ordered iterator walks, views, build) decoded from the
 * fuzz input against a tree instantiated with the unit-test flavour
 * (small blocks, int64 elements) and verifies every result against a
 * shadow std::set, so any divergence from ordered-set semantics, any
 * structural invariant violation (debug_check) or any memory error
 * (ASan) is reported.
 *
 * Input format: a stream of operations, host endianness.
 *  - Each operation starts with one byte, its low nibble selects
 *    the operation, the high nibble is ignored.
 *  - 0: insert, int64 value.
 *  - 1: delete, int64 value.
 *  - 2: find, int64 value.
 *  - 3: lower/upper bound walk, int64 key, one byte with bit 0
 *    selecting lower_bound vs upper_bound and one byte of steps.
 *  - 4: first/last boundaries check, no operands.
 *  - 5: debug_check, no operands.
 *  - 6: random access, one byte index.
 *  - 7: pinned view check, int64 value taken only if the set is
 *    non-empty.
 *  - 8: build a tree from the sorted set, no operands.
 *  - 9: insert with iterator retrieval, int64 value.
 *  - 10: mem_used/approximate_count check, int64 value.
 *  - 11: identical-element replace, no operands.
 *  - 12: backward walk, one byte of steps.
 *  - 13-15: bulk insert, int64 base and one byte count, inserting
 *    base + i * 3 values, stops early on int64 overflow.
 *  - A read past the end of the input reuses the remaining bytes
 *    or yields zero.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <set>
#include <vector>

/* Select the small-block tree flavour, as in test/unit/bps_tree.cc. */
#define SMALL_BLOCK_SIZE 128

typedef int64_t type_t;

static int
compare(type_t a, type_t b)
{
	return a < b ? -1 : a > b ? 1 : 0;
}

#define test_TREE_EXTENT_SIZE 2048
#define BPS_TREE_NAME test
#define BPS_TREE_BLOCK_SIZE SMALL_BLOCK_SIZE
#define BPS_TREE_IS_IDENTICAL(a, b) ((a) == (b))
#define BPS_TREE_COMPARE(a, b, arg) compare(a, b)
#define BPS_TREE_COMPARE_KEY(a, b, arg) compare(a, b)
#define bps_tree_elem_t type_t
#define bps_tree_key_t type_t
#define bps_tree_arg_t int
/* As in the main flavour of test/unit/bps_tree.cc. */
#define BPS_TREE_DEBUG_BRANCH_VISIT
#include "salad/bps_tree.h"
#undef BPS_TREE_NAME
#undef BPS_TREE_BLOCK_SIZE
#undef BPS_TREE_IS_IDENTICAL
#undef BPS_TREE_COMPARE
#undef BPS_TREE_COMPARE_KEY
#undef bps_tree_elem_t
#undef bps_tree_key_t
#undef bps_tree_arg_t
#undef BPS_TREE_DEBUG_BRANCH_VISIT

static void *
fuzz_extent_alloc(struct matras_allocator *allocator)
{
	return xmalloc(allocator->extent_size);
}

static void
fuzz_extent_free(struct matras_allocator *allocator, void *extent)
{
	(void)allocator;
	free(extent);
}

static struct matras_allocator allocator;

__attribute__((constructor))
static void
setup(void)
{
	matras_allocator_create(&allocator, test_TREE_EXTENT_SIZE,
				fuzz_extent_alloc, fuzz_extent_free);
}

__attribute__((destructor))
static void
teardown(void)
{
	matras_allocator_destroy(&allocator);
}

/* Abort on an oracle violation: this is the bug signal of the target. */
static void
oracle_fail(void)
{
	abort();
}

/* Never let the tree grow unboundedly: cap elements per input. */
static const size_t TREE_ELEM_CAP = 50000;
/* Cap of driven operations per input. */
static const size_t MAX_OPS = 8192;

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size < 2)
		return 0;

	const uint8_t *p = data;
	const uint8_t *end = data + size;
	std::set<type_t> shadow;
	bool ever_nonempty = false;
	test tree;
	test_create(&tree, 0, &allocator, NULL);

	auto take_i64 = [&]() -> type_t {
		type_t v = 0;
		if (end - p < (ptrdiff_t)sizeof(v)) {
			/* Reuse the remaining bytes instead of stopping. */
			if (p == end)
				return 0;
			memcpy(&v, p, (size_t)(end - p));
			p = end;
			return v;
		}
		memcpy(&v, p, sizeof(v));
		p += sizeof(v);
		return v;
	};
	auto take_byte = [&]() -> uint8_t {
		uint8_t b = p != end ? *p : 0;
		if (p != end)
			++p;
		return b;
	};

	for (size_t ops = 0; ops < MAX_OPS && p < end; ++ops) {
		uint8_t op = take_byte();
		switch (op & 0xF) {
		case 0: { /* insert */
			type_t v = take_i64();
			if (shadow.size() >= TREE_ELEM_CAP)
				break;
			type_t replaced = 0;
			int rc = test_insert(&tree, v, &replaced, NULL);
			if (rc != 0)
				oracle_fail();
			/*
			 * Only the replacement path must report the old
			 * element; a fresh insert may leave it untouched.
			 */
			if (shadow.count(v) != 0 && replaced != v)
				oracle_fail();
			shadow.insert(v);
			ever_nonempty = true;
			break;
		}
		case 1: { /* delete */
			type_t v = take_i64();
			type_t deleted = 0;
			bool was = shadow.count(v) != 0;
			int rc = test_delete(&tree, v, &deleted);
			if (rc != 0)
				oracle_fail();
			if (was && deleted != v)
				oracle_fail();
			if (!was && deleted != 0)
				oracle_fail();
			shadow.erase(v);
			break;
		}
		case 2: { /* find */
			type_t v = take_i64();
			type_t *found = test_find(&tree, v);
			if ((found == NULL) != (shadow.count(v) == 0))
				oracle_fail();
			if (found != NULL && *found != v)
				oracle_fail();
			break;
		}
		case 3: { /* ordered walk from lower/upper bound */
			type_t v = take_i64();
			uint8_t mode = take_byte();
			uint8_t steps = take_byte();
			bool exact = false;
			test_iterator it = (mode & 1)
				? test_lower_bound(&tree, v, &exact)
				: test_upper_bound(&tree, v, &exact);
			/*
			 * For both bounds the flag must report whether the
			 * key is present: for lower_bound it is the found
			 * element itself, for upper_bound its preceding
			 * element.
			 */
			if (exact != (shadow.count(v) != 0))
				oracle_fail();
			auto sit = shadow.lower_bound(v);
			if ((mode & 1) == 0 && sit != shadow.end() &&
			    *sit == v)
				++sit;
			for (uint8_t i = 0; i < steps; ++i) {
				if (test_iterator_is_invalid(&it)) {
					if (sit != shadow.end())
						oracle_fail();
					break;
				}
				type_t *elem =
					test_iterator_get_elem(&tree, &it);
				if (elem == NULL || sit == shadow.end()) {
					if (elem != NULL || sit != shadow.end())
						oracle_fail();
					break;
				}
				if (*elem != *sit)
					oracle_fail();
				test_iterator_next(&tree, &it);
				++sit;
			}
			break;
		}
		case 4: { /* first/last boundaries */
			if (shadow.empty())
				break;
			test_iterator first = test_first(&tree);
			type_t *fe = test_iterator_get_elem(&tree, &first);
			if (fe == NULL || *fe != *shadow.begin())
				oracle_fail();
			test_iterator last = test_last(&tree);
			type_t *le = test_iterator_get_elem(&tree, &last);
			if (le == NULL || *le != *shadow.rbegin())
				oracle_fail();
			/* next of last must be invalid; prev of last walks. */
			test_iterator it = last;
			test_iterator_next(&tree, &it);
			if (!test_iterator_is_invalid(&it))
				oracle_fail();
			break;
		}
		case 5: { /* structural invariant check */
			if (test_debug_check(&tree) != 0)
				oracle_fail();
			if (test_size(&tree) != shadow.size())
				oracle_fail();
			break;
		}
		case 6: { /* random access */
			type_t *elem = test_random(&tree, take_byte());
			if (elem != NULL && shadow.count(*elem) == 0)
				oracle_fail();
			break;
		}
		case 7: { /* pinned view semantics */
			test_view view;
			test_view_create(&view, &tree);
			if (test_view_size(&view) != shadow.size())
				oracle_fail();
			if (!shadow.empty()) {
				type_t v = take_i64();
				type_t *found = test_view_find(&view, v);
				if ((found == NULL) != (shadow.count(v) == 0))
					oracle_fail();
				if (found != NULL && *found != v)
					oracle_fail();
			}
			test_view_destroy(&view);
			break;
		}
		case 8: { /* build from a sorted array */
			if (shadow.empty())
				break;
			std::vector<type_t> arr(shadow.begin(), shadow.end());
			test tree2;
			test_create(&tree2, 0, &allocator, NULL);
			if (test_build(&tree2, arr.data(), arr.size()) != 0)
				oracle_fail();
			if (test_size(&tree2) != arr.size())
				oracle_fail();
			if (test_debug_check(&tree2) != 0)
				oracle_fail();
			/* Verify that the elements were actually copied. */
			type_t *first = test_find(&tree2, arr.front());
			if (first == NULL || *first != arr.front())
				oracle_fail();
			type_t *last = test_find(&tree2, arr.back());
			if (last == NULL || *last != arr.back())
				oracle_fail();
			test_destroy(&tree2);
			break;
		}
		case 9: { /* insert with iterator retrieval */
			type_t v = take_i64();
			if (shadow.size() >= TREE_ELEM_CAP)
				break;
			test_iterator it;
			int rc = test_insert_get_iterator(&tree, v, NULL, &it);
			if (rc != 0)
				oracle_fail();
			type_t *elem = test_iterator_get_elem(&tree, &it);
			if (elem == NULL || *elem != v)
				oracle_fail();
			shadow.insert(v);
			ever_nonempty = true;
			break;
		}
		case 10: { /* mem_used and approximate_count sanity */
			/*
			 * mem_used() reports retained matras extents: it is
			 * 0 only for a tree that never had elements and
			 * stays positive after deletions (garbage list), so
			 * the oracle tracks whether the tree was ever
			 * non-empty. approximate_count() is a heuristic
			 * estimate, so it is exercised without assertions.
			 */
			size_t used = test_mem_used(&tree);
			if (!ever_nonempty && used != 0)
				oracle_fail();
			if (ever_nonempty && used == 0)
				oracle_fail();
			type_t v = take_i64();
			(void)test_approximate_count(&tree, v);
			break;
		}
		case 11: { /* identical-element replace */
			if (shadow.empty())
				break;
			type_t v = *shadow.begin();
			type_t replaced = 0;
			int rc = test_insert(&tree, v, &replaced, NULL);
			if (rc != 0)
				oracle_fail();
			if (replaced != v)
				oracle_fail();
			if (test_size(&tree) != shadow.size())
				oracle_fail();
			break;
		}
		case 12: { /* prev-walk backwards, ordered and exact */
			uint8_t steps = take_byte();
			test_iterator it = test_last(&tree);
			auto sit = shadow.rbegin();
			for (uint8_t i = 0; i < steps; ++i) {
				if (test_iterator_is_invalid(&it)) {
					if (sit != shadow.rend())
						oracle_fail();
					break;
				}
				type_t *elem =
					test_iterator_get_elem(&tree, &it);
				if (elem == NULL || sit == shadow.rend()) {
					if (elem != NULL || sit != shadow.rend())
						oracle_fail();
					break;
				}
				if (*elem != *sit)
					oracle_fail();
				test_iterator_prev(&tree, &it);
				++sit;
			}
			break;
		}
		default: { /* 13-15: bulk insert then verify */
			type_t base = take_i64();
			uint8_t n = take_byte();
			for (uint8_t i = 0; i < n; ++i) {
				if (shadow.size() >= TREE_ELEM_CAP)
					break;
				type_t v;
				/*
				 * The base is arbitrary, so the sum may not
				 * fit into int64_t: skip the rest of the run
				 * to avoid undefined signed overflow.
				 */
				if (__builtin_add_overflow(base,
							   (type_t)i * 3, &v))
					break;
				if (test_insert(&tree, v, NULL, NULL) != 0)
					oracle_fail();
				shadow.insert(v);
				ever_nonempty = true;
			}
			break;
		}
		}
	}

	if (test_debug_check(&tree) != 0)
		oracle_fail();
	if (test_size(&tree) != shadow.size())
		oracle_fail();
	test_destroy(&tree);
	return 0;
}
