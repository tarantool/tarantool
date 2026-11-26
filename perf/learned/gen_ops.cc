/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */

/**
 * Operation files: which keys are loaded and what is done after that.
 */
#include "gen.h"

#include <math.h>

#include <algorithm>

#include "learned.h"
#include "rng.h"

/** State of an operation stream being generated. */
struct stream {
	/** Keys of the key file. */
	const std::vector<uint64_t> *keys;
	/** Bitmap of the keys loaded before the operations. */
	std::vector<uint64_t> init;
	/** Generated operations. */
	std::vector<struct op> ops;
	/** The random generator. */
	struct rng *r;
};

static void
stream_push(struct stream *s, enum op_type type, uint32_t idx, uint64_t aux)
{
	struct op op;
	op.key = (*s->keys)[idx];
	op.aux = aux;
	op.type = type;
	op.idx = idx;
	s->ops.push_back(op);
}

/** Marks the first count key positions of the order as loaded. */
static void
stream_load(struct stream *s, const std::vector<uint32_t> &order,
	    size_t count)
{
	s->init.assign(bitmap_words(s->keys->size()), 0);
	for (size_t i = 0; i < count; i++)
		bitmap_set(s->init, order[i]);
}

/** Length of OP_SCAN and of the key range of OP_COUNT. */
static const uint64_t RANGE_LEN = 100;

/** Zipf distribution of ranks: rank 0 is the most popular one. */
struct zipf {
	/** Sums of the weights of the ranks up to this one. */
	std::vector<double> sums;
};

/** The weights are the ones of YCSB: 1 / rank^0.99. */
static void
zipf_create(struct zipf *z, size_t count)
{
	double sum = 0;
	for (size_t i = 0; i < count; i++) {
		sum += pow(i + 1, -0.99);
		z->sums.push_back(sum);
	}
}

static size_t
zipf_next(const struct zipf *z, struct rng *r)
{
	double x = rng_unit(r) * z->sums.back();
	return std::lower_bound(z->sums.begin(), z->sums.end(), x) -
	       z->sums.begin();
}

/** Which of the present keys the reads of a mix take. */
enum read_choice {
	/** Any key, with the same probability. */
	READ_UNIFORM,
	/** The same few keys most of the time. */
	READ_HOT,
	/** Mostly the keys inserted last. */
	READ_LATEST,
};

/**
 * Loads the first half of the order, then mixes inserts of the second half,
 * one by one in the order, with reads of the present keys. The inserts stop
 * when the order ends.
 */
static void
stream_mix(struct stream *s, const std::vector<uint32_t> &order,
	   size_t op_count, uint64_t insert_percent, enum op_type read,
	   enum read_choice choice)
{
	size_t loaded = order.size() / 2, present = loaded;
	stream_load(s, order, loaded);
	struct zipf z;
	zipf_create(&z, loaded);
	for (size_t i = 0; i < op_count; i++) {
		if (present < order.size() &&
		    rng_below(s->r, 100) < insert_percent) {
			stream_push(s, OP_INSERT, order[present++], 0);
			continue;
		}
		size_t pos;
		if (choice == READ_UNIFORM)
			pos = rng_below(s->r, present);
		else if (choice == READ_HOT)
			pos = zipf_next(&z, s->r);
		else
			pos = present - 1 - zipf_next(&z, s->r);
		uint64_t aux = 0;
		if (read == OP_SCAN)
			aux = 1 + rng_below(s->r, RANGE_LEN);
		stream_push(s, read, order[pos], aux);
	}
}

/**
 * One workload per operation, then the mixes. The first four do not change
 * the index and run against all the keys or half of them. Inserts add the
 * keys which are not loaded yet, deletes remove up to a half of the loaded
 * ones. Both go in random order, each key at most once.
 */
static void
stream_fill(struct stream *s, const std::string &workload, size_t op_count)
{
	size_t n = s->keys->size();
	std::vector<uint32_t> order(n);
	for (size_t i = 0; i < n; i++)
		order[i] = i;
	if (workload == "lookup") {
		stream_load(s, order, n);
		for (size_t i = 0; i < op_count; i++)
			stream_push(s, OP_GET, rng_below(s->r, n), 0);
	} else if (workload == "lower_bound") {
		/* Half of the probes miss and land between the keys. */
		shuffle(order, s->r);
		stream_load(s, order, n / 2);
		for (size_t i = 0; i < op_count; i++)
			stream_push(s, OP_LOWER_BOUND, rng_below(s->r, n), 0);
	} else if (workload == "scan") {
		stream_load(s, order, n);
		for (size_t i = 0; i < op_count; i++)
			stream_push(s, OP_SCAN, rng_below(s->r, n), RANGE_LEN);
	} else if (workload == "count") {
		/* Every range [key, aux) holds exactly RANGE_LEN keys. */
		stream_load(s, order, n);
		for (size_t i = 0; i < op_count; i++) {
			uint64_t lo = rng_below(s->r, n - RANGE_LEN);
			uint64_t hi = (*s->keys)[lo + RANGE_LEN];
			stream_push(s, OP_COUNT, lo, hi);
		}
	} else if (workload == "insert") {
		shuffle(order, s->r);
		stream_load(s, order, n / 2);
		op_count = std::min(op_count, n - n / 2);
		for (size_t i = 0; i < op_count; i++)
			stream_push(s, OP_INSERT, order[n / 2 + i], 0);
	} else if (workload == "delete") {
		shuffle(order, s->r);
		stream_load(s, order, n);
		op_count = std::min(op_count, n / 2);
		for (size_t i = 0; i < op_count; i++)
			stream_push(s, OP_DELETE, order[i], 0);
	} else if (workload == "ycsb_a") {
		shuffle(order, s->r);
		stream_mix(s, order, op_count, 50, OP_GET, READ_HOT);
	} else if (workload == "ycsb_b") {
		shuffle(order, s->r);
		stream_mix(s, order, op_count, 5, OP_GET, READ_HOT);
	} else if (workload == "ycsb_d") {
		shuffle(order, s->r);
		stream_mix(s, order, op_count, 5, OP_GET, READ_LATEST);
	} else if (workload == "ycsb_e") {
		shuffle(order, s->r);
		stream_mix(s, order, op_count, 5, OP_SCAN, READ_HOT);
	} else if (workload == "drift") {
		/* The low keys are loaded, all the inserts are above them. */
		shuffle(order, s->r, n / 2);
		stream_mix(s, order, op_count, 50, OP_GET, READ_UNIFORM);
	} else {
		die("unknown workload '%s', expected one of: lookup, "
		    "lower_bound, scan, count, insert, delete, ycsb_a, "
		    "ycsb_b, ycsb_d, ycsb_e, drift", workload.c_str());
	}
}

static void
stream_save(const struct stream *s, const char *path)
{
	struct ops_header h;
	h.magic = OPS_MAGIC;
	h.key_count = s->keys->size();
	h.key_hash = keys_hash(*s->keys);
	h.init_count = 0;
	for (uint64_t word : s->init)
		h.init_count += __builtin_popcountll(word);
	h.op_count = s->ops.size();
	FILE *f = xfopen(path, "wb");
	xfwrite(&h, sizeof(h), f, path);
	xfwrite(s->init.data(), s->init.size() * sizeof(uint64_t), f, path);
	xfwrite(s->ops.data(), s->ops.size() * sizeof(struct op), f, path);
	fclose(f);
}

void
gen_ops(const std::vector<uint64_t> &keys, const std::string &workload,
	size_t op_count, struct rng *r, const char *path)
{
	if (keys.size() <= RANGE_LEN || keys.size() >= UINT32_MAX)
		die("key count must be in (%llu, 2^32)",
		    (unsigned long long)RANGE_LEN);
	struct stream s;
	s.keys = &keys;
	s.r = r;
	stream_fill(&s, workload, op_count);
	stream_save(&s, path);
}
