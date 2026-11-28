/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "ann_memory.h"
#include "errinj.h"
#include "clock.h"
#include "fiber.h"
#include "trivia/util.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/** malloc alignment also covers the header and the returned payload. */
union ann_quota_block {
	/** Preserve maximal C alignment for the following payload. */
	max_align_t alignment;
	/** Metadata retained until the block is released. */
	struct {
		/** Owner that charged the shared quota. */
		struct ann_quota_memory *owner;
		/** Requested payload size. */
		size_t size;
		/** Rounded charge returned by quota_use(). */
		size_t charge;
	} value;
};

/** Charge the quota first and roll it back on allocation failure. */
static void *
ann_quota_alloc(void *ctx, size_t size)
{
	struct ann_quota_memory *owner = ctx;
	if (owner == NULL || owner->quota == NULL || size == 0 ||
	    size > SIZE_MAX - sizeof(union ann_quota_block))
		return NULL;
	ERROR_INJECT_COUNTDOWN(ERRINJ_VECTOR_ALLOC, return NULL);
	size_t total = sizeof(union ann_quota_block) + size;
	if (total > QUOTA_MAX)
		return NULL;
	size_t rounded = (total + QUOTA_UNIT_SIZE - 1) /
			 QUOTA_UNIT_SIZE * QUOTA_UNIT_SIZE;
	ssize_t charged;
	if (owner->use_reserve) {
		if (rounded > owner->reserved)
			return NULL;
		owner->reserved -= rounded;
		charged = rounded;
	} else {
		charged = quota_use(owner->quota, total);
		if (charged < 0)
			return NULL;
	}
	union ann_quota_block *block = realloc_with_free(NULL, total);
	if (block == NULL) {
		if (owner->use_reserve)
			owner->reserved += charged;
		else
			quota_release(owner->quota, charged);
		return NULL;
	}
	block->value.owner = owner;
	block->value.size = size;
	block->value.charge = charged;
	owner->bytes += total;
	owner->payload_bytes += size;
	owner->charged += charged;
	owner->blocks++;
	return block + 1;
}

static void
ann_quota_free(void *ctx, void *ptr, size_t size)
{
	if (ptr == NULL)
		return;
	union ann_quota_block *block = (union ann_quota_block *)ptr - 1;
	struct ann_quota_memory *owner = ctx;
	assert(block->value.owner == owner);
	assert(block->value.size == size);
	size_t total = sizeof(*block) + size;
	size_t charged = block->value.charge;
	assert(owner->bytes >= total && owner->charged >= charged &&
	       owner->blocks > 0);
	owner->bytes -= total;
	owner->payload_bytes -= size;
	owner->charged -= charged;
	owner->blocks--;
	free(block);
	if (owner->use_reserve)
		owner->reserved += charged;
	else
		quota_release(owner->quota, charged);
	ann_quota_memory_gc_step(owner);
}

void
ann_quota_memory_start_gc(struct ann_quota_memory *owner)
{
	owner->gc_deadline = clock_monotonic64() + 1000000;
}

void
ann_quota_memory_gc_step(struct ann_quota_memory *owner)
{
	if (owner->gc_deadline == 0 ||
	    clock_monotonic64() < owner->gc_deadline)
		return;
	/* Cleanup must finish even if the requesting fiber was cancelled. */
	fiber_sleep(0);
	owner->gc_deadline = clock_monotonic64() + 1000000;
}

bool
ann_quota_memory_reserve(struct ann_quota_memory *owner, size_t bytes)
{
	if (bytes > QUOTA_MAX - QUOTA_UNIT_SIZE)
		return false;
	bytes = (bytes + QUOTA_UNIT_SIZE - 1) /
		QUOTA_UNIT_SIZE * QUOTA_UNIT_SIZE;
	if (bytes > owner->reserved) {
		ssize_t charge =
			quota_use(owner->quota, bytes - owner->reserved);
		if (charge < 0)
			return false;
	} else if (bytes < owner->reserved) {
		quota_release(owner->quota, owner->reserved - bytes);
	}
	owner->reserved = bytes;
	return true;
}

void
ann_quota_memory_create(struct ann_quota_memory *owner, struct quota *quota)
{
	memset(owner, 0, sizeof(*owner));
	owner->quota = quota;
}

/** Bind a memory owner to the allocator callbacks. */
struct ann_memory
ann_quota_memory_bind(struct ann_quota_memory *owner)
{
	struct ann_memory result = {
		.ctx = owner,
		.alloc = ann_quota_alloc,
		.free = ann_quota_free,
	};
	return result;
}
