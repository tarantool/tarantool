/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#ifndef TARANTOOL_BOX_KEY_ESTIMATOR_H_INCLUDED
#define TARANTOOL_BOX_KEY_ESTIMATOR_H_INCLUDED

#include "lib/salad/hll.h"

struct tuple;
struct key_def;

/**
 * Key estimator object that is used to estimate cadinality of the set of added
 * keys. The estimator uses the HyperLogLog algorithm.
 */
struct key_estimator {
	/** HyperLogLog algorithm. */
	struct hll *hll;
	/** Format of keys of added tuples. */
	struct key_def *format;
};

/**
 * @brief Create a new key estimator that uses the HyperLogLog algorithm.
 *
 * @param key_format format of tuples to be added. Format is copied.
 * @param precision precision of the HyperLogLog algorithm.
 * @param representation representation of the HyperLogLog algorithm.
 * @return a new key_estimator object or NULL in case of error.
 * @note The error can be caused by invalid precision value.
 */
struct key_estimator *
key_estimator_new(const struct key_def *key_format, int precision,
		  enum HLL_REPRESENTATION representation);

/**
 * @brief Add a new tuple to the estimator.
 *
 * @param estimator key_estimator object.
 * @param tuple tuple to be added.
 * @return none-zero value in case of format error, otherwise 0 is returned.
 */
int
key_estimator_add(struct key_estimator *estimator, struct tuple *tuple);

/**
 * @brief Add all elements from the src object to the dst. The objects that
 * are being merged must have the same precision values and formats.
 *
 * @param dst destination object.
 * @param src object with elements to be added.
 * @return none-zero value if mering can't be performed,
 * otherwise 0 is returned.
 */
int
key_estimator_merge(struct key_estimator *dst, const struct key_estimator *src);

/**
 * @brief Estimate the cardinality of the set of added tuples.
 *
 * @param estimator key_estimator object.
 * @return caridnality of the set of added tuples.
 */
uint64_t
key_estimator_estimate(struct key_estimator *estimator);

/**
 * @brief Realize the key_estimator object and its resources.
 *
 * @param estimator key_estimator object to be deleted.
 */
void
key_estimator_delete(struct key_estimator *estimator);

#endif /* TARANTOOL_BOX_KEY_ESTIMATOR_H_INCLUDED */
