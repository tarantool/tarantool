/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * HyperLogLog supports sparse and dense representations.
 * The representation determines the data storage scheme.
 */
enum HLL_REPRESENTATION {
	/**
	 * Instead of registers the sparse representation works with pairs
	 * consisting of the rank and the index of the added hashes. It requires
	 * less memory for small cardinalities and provides the best accuracy.
	 * Sparse representation switches to dense representation if it starts
	 * to require more amount of memory that is needed for the dense
	 * representation.
	 */
	HLL_SPARSE,
	/**
	 * Dense representation allocates 2^precision counters. For small
	 * cardinalities most counters are not used so this representation
	 * should be used to estimate large cardinalities.
	 */
	HLL_DENSE
};

enum {
	/** Precision of sparsely represented HyperLogLog. */
	HLL_SPARSE_PRECISION = 26,
	/** Minimum precision value for the HyperLogLog algorithm. */
	HLL_MIN_PRECISION = 6,
	/** Maximum precision value for the HyperLogLog algorithm. */
	HLL_MAX_PRECISION = 18,
	/** Number of available precisions. */
	HLL_N_PRECISIONS = HLL_MAX_PRECISION - HLL_MIN_PRECISION + 1,
};

struct hll;

/**
 * Create a HyperLogLog estimator.
 *
 * \param precision defines the estimation error and memory requirements.
 * Use 14 for an error of 0.81%. The algorithm needs no more than
 * 2^precision * 6 bits. The error formula is 1.04 / sqrt(2^precision).
 * The valid values are defined by constants HLL_MIN_PRECISION and
 * HLL_MAX_PRECISION.
 *
 * \return HyperLogLog estimator.
 *
 * \note By default the algorithm starts with the sparse representation
 * and then it automatically switches to the dense representation.
 * See HLL_REPRESENTATION enum for more details.
 */
struct hll *
hll_new(uint8_t precision);

/**
 * Create a HyperLogLog estimator with specified representation.
 *
 * \param precision the same semantics as in hll_new function.
 * \param representation representation to be setted.
 *
 * \return HyperLogLog estimator.
 */
struct hll *
hll_new_concrete(uint8_t precision, enum HLL_REPRESENTATION representation);

/**
 * Add a hash to the estimator.
 * The hash function for the algorithm must equally
 * likely to give values from 0 to 2^64 - 1.
 */
void
hll_add(struct hll *hll, uint64_t hash);

/**
 * Merge two HyperLogLog estimators. After merging all hashes from the src
 * object will be added to the dst object.
 *
 * \note As a side effect, the representation of the dst
 * estimator will be switched to dense.
 *
 * \return non-zero value if merging is not possible, otherwise 0 is returned.
 */
int
hll_merge(struct hll *dst, const struct hll *src);

/**
 * Estimate the number of distinct hashes added to the estimator.
 */
uint64_t
hll_count_distinct(struct hll *hll);

/**
 * Delete the hll estimator with its resources.
 */
void
hll_delete(struct hll *hll);

/** Check if the precision is supported. */
static inline int
hll_is_supported_precision(uint8_t prec)
{
	return (prec >= HLL_MIN_PRECISION && prec <= HLL_MAX_PRECISION);
}

/** Get the estimator precision parameter. */
uint8_t
hll_precision(struct hll *hll);

/** Get standard estimation error for this precision value. */
double
hll_error(uint8_t prec);

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */
