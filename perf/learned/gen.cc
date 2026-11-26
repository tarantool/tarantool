/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */

/**
 * Generator of key files and operation streams for learned_bench.
 *
 *   learned_gen keys <dist> <count> <seed> <out>
 *   learned_gen ops <key file> <workload> <op count> <seed> <out>
 *   learned_gen import <sosd file> <count> <seed> <out>
 *   learned_gen info <key file>
 *
 * All the randomness comes from a seeded splitmix64, so the output only
 * depends on the arguments.
 */
#include "gen.h"

#include "learned.h"
#include "rng.h"

static void
print_info(const char *path)
{
	std::vector<uint64_t> keys = keys_load(path);
	size_t above_double = 0, above_hint = 0;
	for (uint64_t k : keys) {
		above_double += k > (1ULL << 53);
		above_hint += k >= (1ULL << 59) - 1;
	}
	printf("keys: %zu\n", keys.size());
	if (keys.empty())
		return;
	printf("min: %llu\nmax: %llu\n", (unsigned long long)keys.front(),
	       (unsigned long long)keys.back());
	printf("above 2^53 (inexact in double): %zu\n", above_double);
	printf("above 2^59 (saturated memtx hint): %zu\n", above_hint);
}

static void
print_usage(void)
{
	die("usage:\n"
	    "  learned_gen keys <dist> <count> <seed> <out>\n"
	    "  learned_gen ops <key file> <workload> <op count> <seed> <out>\n"
	    "  learned_gen import <sosd file> <count|0> <seed> <out>\n"
	    "  learned_gen info <key file>");
}

int
main(int argc, char **argv)
{
	if (argc < 2)
		print_usage();
	std::string cmd = argv[1];
	if (cmd == "info" && argc == 3) {
		print_info(argv[2]);
		return 0;
	}
	if (cmd == "ops" && argc == 7) {
		struct rng r = {strtoull(argv[5], NULL, 10)};
		size_t op_count = strtoull(argv[4], NULL, 10);
		gen_ops(keys_load(argv[2]), argv[3], op_count, &r, argv[6]);
		return 0;
	}
	if (argc != 6)
		print_usage();
	struct rng r = {strtoull(argv[4], NULL, 10)};
	size_t count = strtoull(argv[3], NULL, 10);
	if (cmd == "keys")
		keys_save(argv[5], gen_keys(argv[2], count, &r));
	else if (cmd == "import")
		keys_save(argv[5], import_sosd(argv[2], count, &r));
	else
		print_usage();
	return 0;
}
