/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

struct rng;

/** Generates count sorted unique keys of the distribution, gen_keys.cc. */
std::vector<uint64_t>
gen_keys(const std::string &dist, size_t count, struct rng *r);

/**
 * Reads a raw SOSD dataset (uint64 count, then keys in any order, possibly
 * with duplicates), sorts and deduplicates it and samples count keys
 * uniformly. Zero count keeps every key. See gen_keys.cc.
 */
std::vector<uint64_t>
import_sosd(const char *path, size_t count, struct rng *r);

/** Generates an operation stream and saves it to the path, gen_ops.cc. */
void
gen_ops(const std::vector<uint64_t> &keys, const std::string &workload,
	size_t op_count, struct rng *r, const char *path);
