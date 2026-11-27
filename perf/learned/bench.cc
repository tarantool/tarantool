/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */

/**
 * Runner of the operation streams made by learned_gen.
 *
 *   learned_bench <index> <key file> <ops file>
 *   learned_bench header
 *
 * The index is bps, alex or alex:<max node size in bytes>. The runner bulk
 * loads the initial keys into it, executes the operations and prints a CSV
 * row with the throughput, latency and memory. The results are checked
 * against std::map.
 */
#include <time.h>

#include <algorithm>
#include <random>

#include "alex_index.h"
#include "bps_index.h"
#include "learned.h"
#include "map_index.h"

/** Everything read from the input files. */
struct workload {
	/** Keys of the key file. */
	std::vector<uint64_t> keys;
	/** The records in random order, like tuples in the memtx arena. */
	std::vector<struct rec> rec_mem;
	/** Record of each key. */
	std::vector<struct rec *> recs;
	/** Records to load before the operations, sorted by key. */
	std::vector<struct rec *> init;
	/** The operations. */
	std::vector<struct op> ops;
};

static void
workload_check_op(const struct workload *w, size_t i, const char *path)
{
	const struct op *op = &w->ops[i];
	if (op->type >= op_type_MAX)
		die("%s: op %zu has unknown type %u", path, i, op->type);
	if (op->idx >= w->keys.size())
		die("%s: op %zu has key position %u out of range", path, i,
		    op->idx);
	if (op->key != w->keys[op->idx])
		die("%s: op %zu has a key different from the key file", path,
		    i);
}

static void
workload_load(struct workload *w, const char *keys_path, const char *ops_path)
{
	w->keys = keys_load(keys_path);
	size_t key_count = w->keys.size();

	FILE *f = xfopen(ops_path, "rb");
	uint64_t file_size = xfsize(f, ops_path);
	struct ops_header h;
	xfread(&h, sizeof(h), f, ops_path);
	if (h.magic != OPS_MAGIC)
		die("%s: not an operation file", ops_path);
	if (h.key_count != key_count || h.key_hash != keys_hash(w->keys))
		die("%s was generated for another key file", ops_path);

	std::vector<uint64_t> bitmap(bitmap_words(key_count));
	uint64_t ops_start = sizeof(h) + bitmap.size() * sizeof(uint64_t);
	if (file_size < ops_start ||
	    (file_size - ops_start) % sizeof(struct op) != 0 ||
	    (file_size - ops_start) / sizeof(struct op) != h.op_count)
		die("%s: file size does not match the header", ops_path);
	xfread(bitmap.data(), bitmap.size() * sizeof(uint64_t), f, ops_path);
	w->ops.resize(h.op_count);
	xfread(w->ops.data(), h.op_count * sizeof(struct op), f, ops_path);
	fclose(f);

	w->rec_mem.resize(key_count);
	for (struct rec &rec : w->rec_mem)
		w->recs.push_back(&rec);
	std::shuffle(w->recs.begin(), w->recs.end(), std::mt19937_64(1));
	for (size_t i = 0; i < key_count; i++) {
		w->recs[i]->key = w->keys[i];
		if (bitmap_test(bitmap, i))
			w->init.push_back(w->recs[i]);
	}
	if (w->init.size() != h.init_count)
		die("%s: corrupted bitmap of the initial keys", ops_path);
	if (w->init.empty() || w->ops.empty())
		die("%s: no initial keys or operations", ops_path);
	for (size_t i = 0; i < w->ops.size(); i++)
		workload_check_op(w, i, ops_path);
}

/** Result of a lookup: reads the record like a user reads the tuple. */
static inline uint64_t
rec_result(const struct rec *rec)
{
	return rec == NULL ? UINT64_MAX : rec->key;
}

/** Executes one operation, the result is comparable between indexes. */
template<class Index>
static inline uint64_t
execute(Index *index, const struct op *op, struct workload *w)
{
	switch (op->type) {
	case OP_GET:
		return rec_result(index->get(op->key));
	case OP_LOWER_BOUND:
		return rec_result(index->lower_bound(op->key));
	case OP_SCAN:
		return index->scan(op->key, op->aux);
	case OP_COUNT:
		return index->count(op->key, op->aux);
	case OP_INSERT:
		return index->insert(w->recs[op->idx]);
	case OP_DELETE:
		return index->erase(w->recs[op->idx]);
	default:
		unreachable();
	}
}

/** Runs the workload on the index and on std::map, compares the results. */
template<class Index>
static bool
verify(Index *index, struct workload *w)
{
	MapIndex map;
	map.build(w->init);
	index->build(w->init);
	for (size_t i = 0; i < w->ops.size(); i++) {
		const struct op *op = &w->ops[i];
		if (execute(index, op, w) != execute(&map, op, w)) {
			fprintf(stderr, "wrong result of op %zu: %s\n", i,
				op_type_strs[op->type]);
			return false;
		}
	}
	for (uint64_t key : w->keys) {
		if (index->get(key) != map.get(key)) {
			fprintf(stderr, "wrong final lookup of key %llu\n",
				(unsigned long long)key);
			return false;
		}
	}
	if (index->size() != map.size() ||
	    index->scan(0, SIZE_MAX) != map.scan(0, SIZE_MAX)) {
		fprintf(stderr, "wrong final content\n");
		return false;
	}
	return true;
}

/** Keeps the results of the operations from being optimized out. */
static volatile uint64_t sink;

static inline uint64_t
now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

/** Columns of the rows printed by run(). */
static const char CSV_HEADER[] =
	"index,init_keys,ops,build_ns_per_key,mops,p50_ns,p99_ns,p999_ns,"
	"max_ns,timer_ns,bytes_per_key_loaded,bytes_per_key_final,verified";

/**
 * Number of passes. The work of every pass is the same, only the noise of the
 * OS differs, and it can only slow down, so the minimal times are reported.
 */
static const int PASS_COUNT = 5;

/**
 * Measures the workload on the index and prints a CSV row. Each pass builds a
 * new index, constructed from the args. Returns false on a wrong result.
 */
template<class Index, class... Args>
static bool
run(const char *name, struct workload *w, Args... args)
{
	size_t op_count = w->ops.size();
	uint64_t build_ns = UINT64_MAX, total_ns = UINT64_MAX;
	double loaded_bytes_per_key = 0, final_bytes_per_key = 0;
	for (int pass = 0; pass < PASS_COUNT; pass++) {
		Index index(args...);
		uint64_t start = now_ns();
		index.build(w->init);
		build_ns = std::min(build_ns, now_ns() - start);
		loaded_bytes_per_key = (double)index.mem_used() / index.size();

		uint64_t sum = 0;
		start = now_ns();
		for (const struct op &op : w->ops)
			sum += execute(&index, &op, w);
		total_ns = std::min(total_ns, now_ns() - start);
		sink = sum;
		final_bytes_per_key = (double)index.mem_used() / index.size();
	}

	std::vector<uint64_t> op_ns(op_count, UINT64_MAX);
	uint64_t timer_ns = UINT64_MAX;
	for (int pass = 0; pass < PASS_COUNT; pass++) {
		Index index(args...);
		index.build(w->init);
		for (size_t i = 0; i < op_count; i++) {
			uint64_t start = now_ns();
			sink = execute(&index, &w->ops[i], w);
			op_ns[i] = std::min(op_ns[i], now_ns() - start);
		}
		uint64_t start = now_ns();
		timer_ns = std::min(timer_ns, now_ns() - start);
	}
	std::sort(op_ns.begin(), op_ns.end());

	Index verified_index(args...);
	bool is_verified = verify(&verified_index, w);

	printf("%s,%zu,%zu,%.1f,%.3f,", name, w->init.size(), op_count,
	       (double)build_ns / w->init.size(), op_count * 1e3 / total_ns);
	for (double part : {0.5, 0.99, 0.999})
		printf("%llu,", (unsigned long long)op_ns[op_count * part]);
	printf("%llu,%llu,%.2f,%.2f,%s\n", (unsigned long long)op_ns.back(),
	       (unsigned long long)timer_ns, loaded_bytes_per_key,
	       final_bytes_per_key, is_verified ? "yes" : "no");
	return is_verified;
}

int
main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "header") == 0) {
		puts(CSV_HEADER);
		return 0;
	}
	if (argc != 4)
		die("usage:\n"
		    "  learned_bench <index> <key file> <ops file>\n"
		    "  learned_bench header\n"
		    "index: bps, alex or alex:<max node size in bytes>");
	struct workload w;
	workload_load(&w, argv[2], argv[3]);
	const char *name = argv[1];
	int node_size = ALEX_DEFAULT_NODE_SIZE;
	bool is_ok;
	if (strcmp(name, "bps") == 0)
		is_ok = run<BpsIndex>(name, &w);
	else if (strcmp(name, "alex") != 0 &&
		 sscanf(name, "alex:%d", &node_size) != 1)
		die("unknown index '%s'", name);
	else if (w.keys.back() == UINT64_MAX)
		die("ALEX uses key UINT64_MAX as a sentinel");
	else
		is_ok = run<AlexIndex>(name, &w, node_size);
	return is_ok ? 0 : 2;
}
