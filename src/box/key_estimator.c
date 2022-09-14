/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "key_estimator.h"
#include "key_def.h"
#include "tuple_hash.h"
#include "tuple_compare.h"
#include "diag.h"
#include "error.h"
#include "trivia/util.h"

#include <PMurHash.h>

struct key_estimator *
key_estimator_new(const struct key_def *key_format,
		  int precision, enum HLL_REPRESENTATION representation)
{
	assert(key_format != NULL);
	if (!hll_is_supported_precision(precision)) {
		diag_set(IllegalParams, "Invalid precision %d "
					"(available values are from %d to %d)",
					precision,
					HLL_MIN_PRECISION, HLL_MAX_PRECISION);
		return NULL;
	}

	struct key_def *format_dup = key_def_dup(key_format);
	if (format_dup == NULL)
		return NULL;
	key_def_set_hash_func(format_dup);
	key_def_set_compare_func(format_dup);
	struct key_estimator *key_estimator = xmalloc(sizeof(*key_estimator));
	key_estimator->format = format_dup;
	/* hll_new can't fail */
	key_estimator->hll = hll_new(precision, representation);
	return key_estimator;
}

void
key_estimator_delete(struct key_estimator *estimator)
{
	hll_delete(estimator->hll);
	key_def_delete(estimator->format);
	free(estimator);
}

/*
 * Compute a hash value of the tuple key for the HyperLogLog algorithm.
 */
static uint64_t
key_estimator_hash(struct tuple *tuple, struct key_def *format)
{
	uint32_t hash = tuple_hash(tuple, format);
	hint_t hint = tuple_hint(tuple, format);
	 /*
	  * Implementation of the HyperLogLog algorithm works with 64-bit
	  * hashes so the result of tuple_hash must be expanded to 64-bit.
	  * The expanding can be performed by using an additional parameter as a
	  * seed for hash function. Compare hint can be used as a seed.
	  * In addition, using PMurHash32 helps to randomize hashes for integer
	  * values whose hash is equal to the input integer value.
	  */
	uint32_t seed = (uint32_t)hint ^ (hint >> 32);
	uint64_t h1 = PMurHash32(seed, &hash, sizeof(hash));
	uint64_t h2 = PMurHash32(seed, &h1, sizeof(h1));
	return h1 | (h2 << 32);
}

int
key_estimator_add(struct key_estimator *estimator, struct tuple *tuple)
{
	struct key_def *format = estimator->format;
	int ret = box_key_def_validate_tuple(format, tuple);
	if (ret != 0) {
		diag_set(IllegalParams, "Invalid tuple format.");
		return -1;
	}

	uint64_t hash = key_estimator_hash(tuple, format);
	hll_add(estimator->hll, hash);
	return 0;
}

/**
 * Check if the formats represented as key_def are equal in parts.
 */
static int
key_estimator_formats_are_equal(const struct key_estimator *est1,
				const struct key_estimator *est2)
{
	const struct key_def *fmt1 = est1->format;
	const struct key_part *p1 = fmt1->parts;
	uint32_t sz1 = fmt1->part_count;
	const struct key_def *fmt2 = est2->format;
	const struct key_part *p2 = fmt2->parts;
	uint32_t sz2 = fmt2->part_count;
	return key_part_cmp(p1, sz1, p2, sz2) == 0;
}

int
key_estimator_merge(struct key_estimator *dst, const struct key_estimator *src)
{
	if (!key_estimator_formats_are_equal(dst, src)) {
		diag_set(IllegalParams, "Different key formats.");
		return -1;
	}
	int rc = hll_merge(dst->hll, src->hll);
	if (rc != 0) {
		diag_set(IllegalParams, "Estimators cannot be merged.");
		return -1;
	}
	return 0;
}

uint64_t
key_estimator_estimate(struct key_estimator *estimator)
{
	return hll_estimate(estimator->hll);
}
