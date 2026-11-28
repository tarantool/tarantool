/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "../lib/ann/ann_backend.h"

#include <small/quota.h>

#if defined(__cplusplus)
extern "C" {
#endif

/** Quota owner shared by all allocations of one ANN generation. */
struct ann_quota_memory {
	/** Shared memtx quota. */
	struct quota *quota;
	/** Retained payload and per-block header bytes. */
	size_t bytes;
	/** Requested payload bytes, excluding allocator headers. */
	size_t payload_bytes;
	/** Quota units charged after rounding. */
	size_t charged;
	/** Number of outstanding blocks. */
	size_t blocks;
	/** Quota held for future maintenance allocations. */
	size_t reserved;
	/** Maintenance allocations consume reserved quota only. */
	bool use_reserve;
	/** Nonzero only while reclaiming an unreachable generation. */
	int64_t gc_deadline;
};

/** Initialize a memory owner against the engine's shared quota. */
void
ann_quota_memory_create(struct ann_quota_memory *owner,
			struct quota *quota);

/** Change the amount of unused quota reserved for maintenance. */
bool
ann_quota_memory_reserve(struct ann_quota_memory *owner, size_t bytes);

/** Return backend allocation callbacks bound to this owner. */
struct ann_memory
ann_quota_memory_bind(struct ann_quota_memory *owner);

/** Enable yielding frees after a generation becomes unreachable. */
void
ann_quota_memory_start_gc(struct ann_quota_memory *owner);

/** Yield between bounded portions of private generation cleanup. */
void
ann_quota_memory_gc_step(struct ann_quota_memory *owner);

#if defined(__cplusplus)
} /* extern "C" */
#endif
