/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "memtx_tuple.h"

#include "small/small.h"
#include "small/region.h"
#include "small/quota.h"
#include "fiber.h"
#include "box.h"

struct memtx_tuple {
	/*
	 * sic: the header of the tuple is used
	 * to store a free list pointer in smfree_delayed.
	 * Please don't change it without understanding
	 * how smfree_delayed and snapshotting COW works.
	 */
	/** Snapshot generation version. */
	uint32_t version;
	struct tuple base;
};

/** Memtx slab arena */
extern struct slab_arena memtx_arena; /* defined in memtx_engine.cc */
/* Memtx slab_cache for tuples */
static struct slab_cache memtx_slab_cache;
/** Common quota for memtx tuples and indexes */
static struct quota memtx_quota;
/** Memtx tuple allocator */
struct small_alloc memtx_alloc; /* used box box.slab.info() */

uint32_t snapshot_version;

enum {
	/** Lowest allowed slab_alloc_minimal */
	OBJSIZE_MIN = 16,
	/** Lowest allowed slab_alloc_maximal */
	OBJSIZE_MAX_MIN = 16 * 1024,
	/** Lowest allowed slab size, for mmapped slabs */
	SLAB_SIZE_MIN = 1024 * 1024
};

void
memtx_tuple_init(float tuple_arena_max_size, uint32_t objsize_min,
		 uint32_t objsize_max, float alloc_factor)
{
	/* Apply lowest allowed objsize bounds */
	if (objsize_min < OBJSIZE_MIN)
		objsize_min = OBJSIZE_MIN;
	if (objsize_max < OBJSIZE_MAX_MIN)
		objsize_max = OBJSIZE_MAX_MIN;

	/* Calculate slab size for tuple arena */
	size_t slab_size = small_round(objsize_max * 4);
	if (slab_size < SLAB_SIZE_MIN)
		slab_size = SLAB_SIZE_MIN;

	/*
	 * Ensure that quota is a multiple of slab_size, to
	 * have accurate value of quota_used_ratio
	 */
	size_t prealloc = small_align(tuple_arena_max_size * 1024
				      * 1024 * 1024, slab_size);
	/** Preallocate entire quota. */
	quota_init(&memtx_quota, prealloc);

	say_info("mapping %zu bytes for tuple arena...", prealloc);

	if (slab_arena_create(&memtx_arena, &memtx_quota,
			      prealloc, slab_size, MAP_PRIVATE)) {
		if (ENOMEM == errno) {
			panic("failed to preallocate %zu bytes: "
			      "Cannot allocate memory, check option "
			      "'slab_alloc_arena' in box.cfg(..)",
			      prealloc);
		} else {
			panic_syserror("failed to preallocate %zu bytes",
				       prealloc);
		}
	}
	slab_cache_create(&memtx_slab_cache, &memtx_arena);
	small_alloc_create(&memtx_alloc, &memtx_slab_cache,
			   objsize_min, alloc_factor);
}

void
memtx_tuple_free(void)
{
}

struct tuple_format_vtab memtx_tuple_format_vtab = {
	memtx_tuple_delete,
};

struct tuple *
memtx_tuple_new(struct tuple_format *format, const char *data, const char *end)
{
	assert(mp_typeof(*data) == MP_ARRAY);
	size_t tuple_len = end - data;
	size_t total = sizeof(struct memtx_tuple) + tuple_len +
		       format->tuple_meta_size;
	ERROR_INJECT(ERRINJ_TUPLE_ALLOC,
		     do { diag_set(OutOfMemory, (unsigned) total,
				   "slab allocator", "memtx_tuple"); return NULL; }
		     while(false); );
	struct memtx_tuple *memtx_tuple =
		(struct memtx_tuple *) smalloc(&memtx_alloc, total);
	/**
	 * Use a nothrow version and throw an exception here,
	 * to throw an instance of ClientError. Apart from being
	 * more nice to the user, ClientErrors are ignored in
	 * panic_on_wal_error=false mode, allowing us to start
	 * with lower arena than necessary in the circumstances
	 * of disaster recovery.
	 */
	if (memtx_tuple == NULL) {
		if (total > memtx_alloc.objsize_max) {
			diag_set(ClientError, ER_SLAB_ALLOC_MAX,
				 (unsigned) total);
			error_log(diag_last_error(diag_get()));
		} else {
			diag_set(OutOfMemory, (unsigned) total,
				 "slab allocator", "memtx_tuple");
		}
		return NULL;
	}
	struct tuple *tuple = &memtx_tuple->base;
	tuple->refs = 0;
	memtx_tuple->version = snapshot_version;
	tuple->bsize = tuple_len;
	tuple->format_id = tuple_format_id(format);
	tuple_format_ref(format, 1);
	/*
	 * Data offset is calculated from the begin of the struct
	 * tuple base, not from memtx_tuple, because the struct
	 * tuple is not the first field of the memtx_tuple.
	 */
	tuple->data_offset = sizeof(struct tuple) + format->tuple_meta_size;
	char *raw = (char *) tuple + tuple->data_offset;
	uint32_t *field_map = (uint32_t *) raw;
	memcpy(raw, data, tuple_len);
	if (tuple_init_field_map(format, field_map, raw)) {
		memtx_tuple_delete(format, tuple);
		return NULL;
	}
	say_debug("%s(%zu) = %p", __func__, tuple_len, memtx_tuple);
	return tuple;
}

void
memtx_tuple_delete(struct tuple_format *format, struct tuple *tuple)
{
	say_debug("%s(%p)", __func__, tuple);
	assert(tuple->refs == 0);
	size_t total = sizeof(struct memtx_tuple) + tuple->bsize +
		       format->tuple_meta_size;
	tuple_format_ref(format, -1);
	struct memtx_tuple *memtx_tuple =
		container_of(tuple, struct memtx_tuple, base);
	if (!memtx_alloc.is_delayed_free_mode ||
	    memtx_tuple->version == snapshot_version)
		smfree(&memtx_alloc, memtx_tuple, total);
	else
		smfree_delayed(&memtx_alloc, memtx_tuple, total);
}

void
memtx_tuple_begin_snapshot()
{
	snapshot_version++;
	small_alloc_setopt(&memtx_alloc, SMALL_DELAYED_FREE_MODE, true);
}

void
memtx_tuple_end_snapshot()
{
	small_alloc_setopt(&memtx_alloc, SMALL_DELAYED_FREE_MODE, false);
}

box_tuple_t *
box_tuple_update(const box_tuple_t *tuple, const char *expr,
		 const char *expr_end)
{
	uint32_t new_size = 0, bsize;
	const char *old_data = tuple_data_range(tuple, &bsize);
	struct region *region = &fiber()->gc;
	size_t used = region_used(region);
	const char *new_data =
		tuple_update_execute(region_aligned_alloc_cb, region, expr,
				     expr_end, old_data, old_data + bsize,
				     &new_size, 1, NULL);
	if (new_data == NULL) {
		region_truncate(region, used);
		return NULL;
	}

	struct tuple *ret = memtx_tuple_new(tuple_format_default, new_data,
					    new_data + new_size);
	region_truncate(region, used);
	if (ret != NULL)
		return tuple_bless_xc(ret);
	return NULL;
}

box_tuple_t *
box_tuple_upsert(const box_tuple_t *tuple, const char *expr,
		 const char *expr_end)
{
	uint32_t new_size = 0, bsize;
	const char *old_data = tuple_data_range(tuple, &bsize);
	struct region *region = &fiber()->gc;
	size_t used = region_used(region);
	const char *new_data =
		tuple_upsert_execute(region_aligned_alloc_cb, region, expr,
				     expr_end, old_data, old_data + bsize,
				     &new_size, 1, false, NULL);
	if (new_data == NULL) {
		region_truncate(region, used);
		return NULL;
	}

	struct tuple *ret = memtx_tuple_new(tuple_format_default, new_data,
					    new_data + new_size);
	region_truncate(region, used);
	if (ret != NULL)
		return tuple_bless_xc(ret);
	return NULL;
}

box_tuple_t *
box_tuple_new(box_tuple_format_t *format, const char *data, const char *end)
{
	struct tuple *ret = memtx_tuple_new(format, data, end);
	if (ret == NULL)
		return NULL;
	/* Can't throw on zero refs. */
	return tuple_bless_xc(ret);
}
