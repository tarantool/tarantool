/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#ifndef HLL_EMPIRICAL_H_INCLUDED
#define HLL_EMPIRICAL_H_INCLUDED

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * HyperLogLog needs a bias correction for small cardianlities.
 * The bias corrections was found empirically for precsion range
 * defined by following constants.
 */
enum HLL_EMPERICAL_LIMITS {
	/** Minimum precision value for the HyperLogLog algorithm. */
	HLL_MIN_PRECISION = 6,
	/** Maximum precision value for the HyperLogLog algorithm. */
	HLL_MAX_PRECISION = 18,
	/** Number of available precisions. */
	HLL_N_PRECISIONS = HLL_MAX_PRECISION - HLL_MIN_PRECISION + 1,
};

/**
 * Return the bias correction for the raw_estimation.
 */
double
hll_empirical_bias_correction(uint8_t precision, double raw_estimation);

/**
 * Return the threshold below which linear counting algorithm has a smaller
 * error than the HyperLogLog algorithm.
 */
uint64_t
hll_empirical_estimation_threshold(uint8_t precision);

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */

#endif /* HLL_EMPIRICAL_H_INCLUDED */
