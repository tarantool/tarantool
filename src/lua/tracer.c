/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include <stdint.h>
#include <string.h>

#include <pmatomic.h>

/**
 * Probability of recording a new trace. It is written and read by
 * different threads, so it is accessed only atomically.
 */
static double tracer_probability = 0;

/**
 * This function sets tracer_probability.
 * This function static because it is exported via FFI.
 */
void
tracer_set_probability(double p)
{
	__atomic_store(&tracer_probability, &p, __ATOMIC_RELAXED);
}

/**
 * This function returns tracer_probability.
 * This function static because it is exported via FFI.
 */
double
tracer_get_probability(void)
{
	double p;
	__atomic_load(&tracer_probability, &p, __ATOMIC_RELAXED);
	return p;
}
