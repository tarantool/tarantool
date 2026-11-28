/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "box/ann_memory.h"

#include <stdint.h>

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

static void
test_shared_quota(void)
{
	plan(12);
	struct quota quota;
	quota_init(&quota, 4096);
	struct ann_quota_memory first, second;
	ann_quota_memory_create(&first, &quota);
	ann_quota_memory_create(&second, &quota);
	struct ann_memory a = ann_quota_memory_bind(&first);
	struct ann_memory b = ann_quota_memory_bind(&second);
	void *p = a.alloc(a.ctx, 1);
	void *q = b.alloc(b.ctx, 1);
	ok(p != NULL && q != NULL, "two owners allocate from one quota");
	ok((uintptr_t)p % _Alignof(max_align_t) == 0,
	   "allocation has maximal C alignment");
	is(quota_used(&quota), 2048, "headers and quota rounding charged");
	is(first.blocks, 1, "first owner retains one block");
	is(second.blocks, 1, "second owner retains one block");
	void *large = a.alloc(a.ctx, 3000);
	is(large, NULL, "shared quota rejects an oversized allocation");
	is(quota_used(&quota), 2048, "failure has no quota side effect");
	a.free(a.ctx, p, 1);
	is(first.charged, 0, "first owner releases its charge");
	is(quota_used(&quota), 1024, "second owner retains its charge");
	b.free(b.ctx, q, 1);
	is(second.charged, 0, "second owner releases its charge");
	is(quota_used(&quota), 0, "shared quota returns to zero");
	ok(first.bytes + second.bytes == 0 &&
	   first.payload_bytes + second.payload_bytes == 0,
	   "all payload and allocator bytes returned");
	check_plan();
}

static void
test_backend_quota(void)
{
	plan(8);
	struct quota quota;
	quota_init(&quota, 8192);
	struct ann_quota_memory owner;
	ann_quota_memory_create(&owner, &quota);
	struct ann_memory memory = ann_quota_memory_bind(&owner);
	struct ann_config config = {2, ANN_L2, NULL};
	const struct ann_backend_ops *ops = ann_backend_find("flat");
	struct ann_backend *backend = NULL;
	is(ops->create(&config, &memory, &backend), ANN_OK,
	   "backend uses quota owner");
	float vector[] = {1, 2};
	struct ann_change *change = NULL;
	is(ops->prepare(backend, ANN_INSERT, 1, vector, 2, &change),
	   ANN_OK, "prepare charges retained vector and undo");
	is(ops->apply(change, NULL), ANN_OK, "apply prepared insert");
	ok(owner.charged != 0 && owner.blocks >= 2,
	   "backend allocations are charged");
	ops->rollback(change);
	ops->finish(change);
	struct ann_backend_stats stats;
	ops->stat(backend, &stats);
	is(stats.live, 0, "rollback clears searchable record");
	ops->destroy(backend);
	is(owner.blocks, 0, "backend destroy returns every block");
	is(owner.bytes, 0, "backend destroy returns retained bytes");
	is(quota_used(&quota), 0, "backend destroy returns engine quota");
	check_plan();
}

static void
test_two_generations(void)
{
	plan(8);
	struct quota quota;
	quota_init(&quota, 16384);
	struct ann_quota_memory older, newer;
	ann_quota_memory_create(&older, &quota);
	ann_quota_memory_create(&newer, &quota);
	struct ann_memory old_memory = ann_quota_memory_bind(&older);
	struct ann_memory new_memory = ann_quota_memory_bind(&newer);
	struct ann_config config = {2, ANN_L2, NULL};
	const struct ann_backend_ops *ops = ann_backend_find("flat");
	struct ann_backend *old_backend = NULL, *new_backend = NULL;
	is(ops->create(&config, &old_memory, &old_backend), ANN_OK,
	   "old generation creates");
	is(ops->create(&config, &new_memory, &new_backend), ANN_OK,
	   "new generation creates under the same quota");
	float vector[] = {1, 2};
	struct ann_change *change = NULL;
	is(ops->prepare(new_backend, ANN_INSERT, 2, vector, 2, &change),
	   ANN_OK, "new generation is writable");
	is(ops->apply(change, NULL), ANN_OK,
	   "new generation insertion applies");
	ops->finish(change);
	size_t before = quota_used(&quota);
	ops->destroy(old_backend);
	ok(quota_used(&quota) < before && quota_used(&quota) != 0,
	   "old generation releases only its own charge");
	struct ann_backend_stats stats;
	ops->stat(new_backend, &stats);
	is(stats.live, 1, "new generation remains readable");
	ops->destroy(new_backend);
	ok(older.blocks == 0 && newer.blocks == 0,
	   "both generation owners release all blocks");
	is(quota_used(&quota), 0, "shared quota is empty at final destroy");
	check_plan();
}

static void
test_maintenance_reserve(void)
{
	plan(10);
	struct quota quota;
	quota_init(&quota, 4096);
	struct ann_quota_memory owner, writer;
	ann_quota_memory_create(&owner, &quota);
	ann_quota_memory_create(&writer, &quota);
	struct ann_memory memory = ann_quota_memory_bind(&owner);
	struct ann_memory other = ann_quota_memory_bind(&writer);
	ok(ann_quota_memory_reserve(&owner, 3072), "reserve maintenance quota");
	is(quota_used(&quota), 3072, "reservation is charged immediately");
	void *p = other.alloc(other.ctx, 1);
	ok(p != NULL, "writer consumes only unreserved quota");
	is(other.alloc(other.ctx, 1), NULL, "writer cannot consume reserve");
	owner.use_reserve = true;
	void *q = memory.alloc(memory.ctx, 2000);
	ok(q != NULL, "maintenance proceeds with a full shared quota");
	is(owner.reserved, 1024, "allocation transfers reserved charge");
	is(quota_used(&quota), 4096, "transfer does not double charge");
	memory.free(memory.ctx, q, 2000);
	ok(ann_quota_memory_reserve(&owner, 0), "unused reserve releases");
	is(quota_used(&quota), 1024, "writer allocation survives cleanup");
	other.free(other.ctx, p, 1);
	is(quota_used(&quota), 0, "all allocations and reserve released");
	check_plan();
}

int
main(void)
{
	plan(4);
	test_shared_quota();
	test_backend_quota();
	test_two_generations();
	test_maintenance_reserve();
	return check_plan();
}
