/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#ifndef HYPER_LOG_LOG_H_INCLUDED
#define HYPER_LOG_LOG_H_INCLUDED

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#include "lib/salad/hll_empirical.h"

/**
 * HyperLogLog supports sparse and dense represenations.
 * The rerpesentation determines the data storage scheme.
 */
enum HLL_REPRESENTATION {
	/**
	 * Instead of registers the sparse representation works with pairs
	 * consisting of the rank and the index of the added hashes. It requires
	 * less memory for small cardinalities and provides the best accuracy.
	 * Sparse representation swithces to dence representation if it starts
	 * to require more amount of memory that is needed for the dense
	 * representation.
	 */
	HLL_SPARSE,
	/**
	 * Dense representation allocates 2^precision counters. For small
	 * cardinalities most counters are not used so this representation
	 * should be used for esimating large cardinalities.
	 */
	HLL_DENSE
};

enum {
	/* Precision of sparsely represented HyperLogLog. */
	HLL_SPARSE_PRECISION = 26,
};

/**
 * Estimator that is used for the HyperLogLog algorithm.
 * The algorithm allows to estimate cardinality of a multiset using fixed amount
 * of memory or even less. Memory requirements and estimation accuracy are
 * determined by the algorithm precision parameter.
 * For the dense representation the relative error is 1.04/sqrt(m) and the
 * memory capasity is m*6 bits where m is number of counters wich
 * equals to 2^precision.
 * For the sparse representation the memory usage is proportional to the number
 * of distinct elements that has added until it reaches the memory usage of the
 * dense representation, and then it switches to the dense representation with
 * fixed memory requretments. The sparse representation has the best accuracy.
 */
struct hll {
	/** See the comment to HLL_REPRESENTATION enum. */
	enum HLL_REPRESENTATION representation;
	/**
	 * Interpretation of the data depends on the representation.
	 * For dense representation it's an array of registers of size
	 * 2^precision * 6 bits. Registers store the maximum added rank of set
	 * of hashes wich last precision bits are equal to register index.
	 * For the sparse representation it's a sparsely represented
	 * HyperLogLog. Instead of registers the sparse representation works
	 * with pairs consisting of the rank and the index of the added hashes.
	 */
	uint8_t *data;
	/**
	 * Precision is equal to the number of bits that are interpreted as
	 * a register index. Available values are from HLL_MIN_PRECISION to
	 * HLL_MAX_PRECISION (defined in hll_emprirical.h.)
	 * The larger value leads to less estimation error but larger
	 * memory requirement (2^precision * 6 bits).
	 * This value is only used by the dense representation and for switching
	 * from the sparse representation.
	 */
	uint8_t dprecision;
	/**
	 * Cached value of the last estimation.
	 */
	double cached_estimation;
};

/**
 * Create a HyperLogLog estimator.
 *
 * \param precision defines the estimation error and memory requirements of
 * the dense representation. The algorithm needs no more than
 * 2^precision * 6 bits. The error formula of the dense representation is
 * 1.04 / sqrt(2^precision). The valid values are defined by constants
 * HLL_MIN_PRECISION and HLL_MAX_PRECISION from hll_emprirical.h file.
 *
 * \param representation determines the data storage scheme for small
 * cardinalities. For big carinalities the dense representation is always used.
 * For more details see #HLL_REPRESENTATION.
 *
 * \return HyperLogLog object.
 */
struct hll *
hll_new(uint8_t precision, enum HLL_REPRESENTATION representation);

/**
 * Add a hash of a dataset element to the hll estimator.
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
 * estimator will be set to dense.
 *
 * \return non-zero value if merging is not possible, oherwise 0 is returned.
 */
int
hll_merge(struct hll *dst, const struct hll *src);

/**
 * Estimate the cardinality of the of the added elements.
 */
uint64_t
hll_estimate(struct hll *hll);

/**
 * Delete the hll estimator with its resourses.
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
static inline double
hll_precision(struct hll *hll)
{
	return hll->representation == HLL_DENSE ? hll->dprecision :
						  HLL_SPARSE_PRECISION;
}

/** Get standard estimation error for this precision value. */
double
hll_error(uint8_t prec);

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */

#endif /* HYPER_LOG_LOG_H_INCLUDED */
