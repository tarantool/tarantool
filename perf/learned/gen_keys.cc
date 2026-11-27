/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */

/**
 * Key files: synthetic distributions and import of real datasets.
 */
#include "gen.h"

#include <math.h>

#include <algorithm>

#include "learned.h"
#include "rng.h"

/**
 * Sorts and deduplicates the keys and drops UINT64_MAX, which ALEX uses as
 * its sentinel. If more than count keys remain, a uniform sample of count
 * keys is kept.
 */
static void
keys_finalize(std::vector<uint64_t> &keys, size_t count, struct rng *r)
{
	std::sort(keys.begin(), keys.end());
	keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
	if (!keys.empty() && keys.back() == UINT64_MAX)
		keys.pop_back();
	if (keys.size() <= count)
		return;
	for (size_t i = 0; i < count; i++)
		std::swap(keys[i], keys[i + rng_below(r, keys.size() - i)]);
	keys.resize(count);
	std::sort(keys.begin(), keys.end());
}

/**
 * Collects count unique keys made by the function. Random keys may collide,
 * so they are drawn with a margin, again and again.
 */
template<class F>
static std::vector<uint64_t>
keys_draw(size_t count, struct rng *r, F draw)
{
	std::vector<uint64_t> keys;
	for (int attempt = 0; keys.size() < count; attempt++) {
		if (attempt == 20)
			die("cannot draw %zu unique keys", count);
		size_t need = count - keys.size();
		need += need / 10 + 16;
		for (size_t i = 0; i < need; i++)
			keys.push_back(draw());
		keys_finalize(keys, count, r);
	}
	return keys;
}

/**
 * Uniform keys below 2^50: exact in double and below the saturation of
 * memtx hints, so this baseline is free of the precision effects.
 */
static std::vector<uint64_t>
gen_uniform(size_t count, struct rng *r)
{
	return keys_draw(count, r, [r] { return rng_next(r) >> 14; });
}

/** Dense sequence starting at an arbitrary offset, like auto-increment. */
static std::vector<uint64_t>
gen_seq(size_t count, struct rng *)
{
	std::vector<uint64_t> keys(count);
	for (size_t i = 0; i < count; i++)
		keys[i] = 1000000 + i;
	return keys;
}

/** The lognormal distribution of the ALEX paper: mu 0, sigma 2, times 1e9. */
static std::vector<uint64_t>
gen_lognormal(size_t count, struct rng *r)
{
	return keys_draw(count, r, [r] {
		return (uint64_t)(exp(2 * rng_normal(r)) * 1e9);
	});
}

/** Gaps between the keys follow a power law: most are tiny, a few are huge. */
static std::vector<uint64_t>
gen_zipf(size_t count, struct rng *r)
{
	std::vector<uint64_t> keys(count);
	uint64_t key = 0;
	for (size_t i = 0; i < count; i++) {
		double gap = 1 / pow(rng_unit(r), 1 / 1.2);
		key += 1 + (uint64_t)std::min(gap, 0x1p40);
		keys[i] = key;
	}
	return keys;
}

/**
 * Microsecond timestamps of events arriving in bursts: about 1000 events
 * a few microseconds apart, then an idle period of about 10 seconds.
 */
static std::vector<uint64_t>
gen_timestamps(size_t count, struct rng *r)
{
	std::vector<uint64_t> keys(count);
	uint64_t ts = 1600000000000000ULL;
	for (size_t i = 0; i < count; i++) {
		if (rng_below(r, 1000) == 0)
			ts += (uint64_t)rng_exp(r, 1e7);
		ts += 1 + (uint64_t)rng_exp(r, 5);
		keys[i] = ts;
	}
	return keys;
}

/** 64 dense normal clusters at random places below 2^50. */
static std::vector<uint64_t>
gen_clustered(size_t count, struct rng *r)
{
	double centers[64];
	for (double &center : centers)
		center = rng_next(r) >> 14;
	return keys_draw(count, r, [r, &centers] {
		double center = centers[rng_below(r, 64)];
		double key = center + rng_normal(r) * 0x1p28;
		return key < 0 ? 0 : (uint64_t)key;
	});
}

/**
 * Adversarial staircase: runs of 64 consecutive keys separated by jumps of
 * random magnitude from 2^10 to 2^40. No linear model fits more than a few
 * steps.
 */
static std::vector<uint64_t>
gen_steps(size_t count, struct rng *r)
{
	std::vector<uint64_t> keys(count);
	uint64_t key = 0;
	for (size_t i = 0; i < count; i++) {
		if (i % 64 == 0)
			key += 1ULL << (10 + rng_below(r, 31));
		keys[i] = key++;
	}
	return keys;
}

/**
 * Twitter Snowflake-like IDs: 41-bit millisecond timestamp, 10-bit worker,
 * 12-bit sequence. The values are about 2^60, above the precision of double
 * and above the exact range of memtx hints.
 */
static std::vector<uint64_t>
gen_snowflake(size_t count, struct rng *r)
{
	std::vector<uint64_t> keys;
	uint64_t ms = 1600000000000ULL - 1288834974657ULL;
	while (keys.size() < count) {
		ms += 1 + rng_below(r, 3);
		uint64_t n = 1 + rng_below(r, 16);
		for (uint64_t seq = 0; seq < n; seq++) {
			uint64_t worker = rng_below(r, 1024);
			keys.push_back(ms << 22 | worker << 12 | seq);
		}
	}
	keys_finalize(keys, count, r);
	return keys;
}

/**
 * Nanosecond timestamps of events about 100 ns apart. The values are about
 * 2^60.6, where neighbour doubles are 256 apart, so several keys in a row
 * look the same to a model.
 */
static std::vector<uint64_t>
gen_nanotime(size_t count, struct rng *r)
{
	std::vector<uint64_t> keys(count);
	uint64_t ts = 1760000000000000000ULL;
	for (size_t i = 0; i < count; i++) {
		ts += 1 + (uint64_t)rng_exp(r, 99);
		keys[i] = ts;
	}
	return keys;
}

/** A distribution and its generator. */
struct dist {
	/** Name in the command line. */
	const char *name;
	/** Generates count keys. */
	std::vector<uint64_t> (*gen)(size_t count, struct rng *r);
};

/** All the distributions. */
static const struct dist dists[] = {
	{"uniform", gen_uniform},
	{"seq", gen_seq},
	{"lognormal", gen_lognormal},
	{"zipf", gen_zipf},
	{"timestamps", gen_timestamps},
	{"clustered", gen_clustered},
	{"steps", gen_steps},
	{"snowflake", gen_snowflake},
	{"nanotime", gen_nanotime},
};

std::vector<uint64_t>
gen_keys(const std::string &name, size_t count, struct rng *r)
{
	std::string names;
	for (const struct dist &dist : dists) {
		if (name == dist.name)
			return dist.gen(count, r);
		names += std::string(" ") + dist.name;
	}
	die("unknown distribution '%s', expected one of:%s", name.c_str(),
	    names.c_str());
}

std::vector<uint64_t>
import_sosd(const char *path, size_t count, struct rng *r)
{
	FILE *f = xfopen(path, "rb");
	uint64_t total;
	xfread(&total, sizeof(total), f, path);
	std::vector<uint64_t> keys(total);
	xfread(keys.data(), total * sizeof(uint64_t), f, path);
	fclose(f);
	keys_finalize(keys, count == 0 ? keys.size() : count, r);
	return keys;
}
