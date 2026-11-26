/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <math.h>
#include <stdint.h>

#include <utility>
#include <vector>

/** Seeded pseudo-random generator, splitmix64. */
struct rng {
	/** Generator state. */
	uint64_t state;
};

static inline uint64_t
rng_next(struct rng *r)
{
	uint64_t z = (r->state += 0x9e3779b97f4a7c15ULL);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	return z ^ (z >> 31);
}

/** Uniform integer in [0, n). */
static inline uint64_t
rng_below(struct rng *r, uint64_t n)
{
	return (uint64_t)(((unsigned __int128)rng_next(r) * n) >> 64);
}

/** Uniform double in (0, 1]. */
static inline double
rng_unit(struct rng *r)
{
	return ((rng_next(r) >> 11) + 1) / (double)(1ULL << 53);
}

/** Standard normal distribution. */
static inline double
rng_normal(struct rng *r)
{
	double radius = sqrt(-2 * log(rng_unit(r)));
	return radius * cos(2 * M_PI * rng_unit(r));
}

/** Exponential distribution, like gaps between independent events. */
static inline double
rng_exp(struct rng *r, double mean)
{
	return -mean * log(rng_unit(r));
}

/** Shuffles the items starting from the given position. */
template<class T>
static void
shuffle(std::vector<T> &v, struct rng *r, size_t begin = 0)
{
	for (size_t i = v.size(); i > begin + 1; i--)
		std::swap(v[i - 1], v[begin + rng_below(r, i - begin)]);
}
