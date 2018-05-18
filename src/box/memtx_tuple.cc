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
/* The maximal allowed tuple size, box.cfg.memtx_max_tuple_size */
size_t memtx_max_tuple_size = 1 * 1024 * 1024; /* set dynamically */
uint32_t snapshot_version;

enum {
	/** Lowest allowed slab_alloc_minimal */
	OBJSIZE_MIN = 16,
	SLAB_SIZE = 16 * 1024 * 1024,
};

void
memtx_tuple_init(uint64_t tuple_arena_max_size, uint32_t objsize_min,
		 float alloc_factor)
{
	/* Apply lowest allowed objsize bounds */
	if (objsize_min < OBJSIZE_MIN)
		objsize_min = OBJSIZE_MIN;
	/** Preallocate entire quota. */
	quota_init(&memtx_quota, tuple_arena_max_size);
	tuple_arena_create(&memtx_arena, &memtx_quota, tuple_arena_max_size,
			   SLAB_SIZE, "memtx");
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
	memtx_tuple_new,
};

struct tuple *
memtx_tuple_new(struct tuple_format *format, const char *data, const char *end)
{
	assert(mp_typeof(*data) == MP_ARRAY);
	size_t tuple_len = end - data;
	size_t meta_size = tuple_format_meta_size(format);
	size_t total = sizeof(struct memtx_tuple) + meta_size + tuple_len;

	ERROR_INJECT(ERRINJ_TUPLE_ALLOC,
		     do { diag_set(OutOfMemory, (unsigned) total,
				   "slab allocator", "memtx_tuple"); return NULL; }
		     while(false); );
	if (unlikely(total > memtx_max_tuple_size)) {
		diag_set(ClientError, ER_MEMTX_MAX_TUPLE_SIZE,
			 (unsigned) total);
		error_log(diag_last_error(diag_get()));
		return NULL;
	}

	struct memtx_tuple *memtx_tuple =
		(struct memtx_tuple *) smalloc(&memtx_alloc, total);
	/**
	 * Use a nothrow version and throw an exception here,
	 * to throw an instance of ClientError. Apart from being
	 * more nice to the user, ClientErrors are ignored in
	 * force_recovery=true mode, allowing us to start
	 * with lower arena than necessary in the circumstances
	 * of disaster recovery.
	 */
	if (memtx_tuple == NULL) {
		diag_set(OutOfMemory, (unsigned) total,
				 "slab allocator", "memtx_tuple");
		return NULL;
	}
	struct tuple *tuple = &memtx_tuple->base;
	tuple->refs = 0;
	memtx_tuple->version = snapshot_version;
	assert(tuple_len <= UINT32_MAX); /* bsize is UINT32_MAX */
	tuple->bsize = tuple_len;
	tuple->format_id = tuple_format_id(format);
	tuple_format_ref(format);
	/*
	 * Data offset is calculated from the begin of the struct
	 * tuple base, not from memtx_tuple, because the struct
	 * tuple is not the first field of the memtx_tuple.
	 */
	tuple->data_offset = sizeof(struct tuple) + meta_size;
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
	size_t total = sizeof(struct memtx_tuple) +
		       tuple_format_meta_size(format) + tuple->bsize;
	tuple_format_unref(format);
	struct memtx_tuple *memtx_tuple =
		container_of(tuple, struct memtx_tuple, base);
	if (memtx_alloc.free_mode != SMALL_DELAYED_FREE ||
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
