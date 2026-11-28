/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdint.h>

struct ann_candidate;

/** Immutable HNSW settings passed through ann_config.algorithm. */
struct ann_hnsw_config {
	/** Maximum outgoing graph connections per node. */
	uint32_t connectivity;
	/** Construction traversal width. */
	uint32_t expansion_add;
	/** Default search traversal width. */
	uint32_t expansion_search;
};

/** Per-query HNSW settings passed through ann_search_opts.algorithm. */
struct ann_hnsw_search_opts {
	/** Search traversal width overriding the index default. */
	uint32_t expansion;
	/** Order scored ties. Equal candidates represent the same result. */
	int (*compare_equal)(const struct ann_candidate *a,
			     const struct ann_candidate *b, void *ctx);
	/** Immutable state used by compare_equal. */
	void *compare_ctx;
};
