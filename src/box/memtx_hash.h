#ifndef TARANTOOL_BOX_MEMTX_HASH_H_INCLUDED
#define TARANTOOL_BOX_MEMTX_HASH_H_INCLUDED
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
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "index.h"
#include "memtx_engine.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

static inline bool
memtx_hash_equal(struct tuple *tuple_a, struct tuple *tuple_b,
		 struct key_def *key_def)
{
	return tuple_compare(tuple_a, tuple_b, key_def) == 0;
}

static inline bool
memtx_hash_equal_key(struct tuple *tuple, const char *key,
		     struct key_def *key_def)
{
	return tuple_compare_with_key(tuple, key, key_def->part_count,
				      key_def) == 0;
}

#define LIGHT_NAME _index
#define LIGHT_DATA_TYPE struct tuple *
#define LIGHT_KEY_TYPE const char *
#define LIGHT_CMP_ARG_TYPE struct key_def *
#define LIGHT_EQUAL(a, b, c) memtx_hash_equal(a, b, c)
#define LIGHT_EQUAL_KEY(a, b, c) memtx_hash_equal_key(a, b, c)

#include "salad/light.h"

#undef LIGHT_NAME
#undef LIGHT_DATA_TYPE
#undef LIGHT_KEY_TYPE
#undef LIGHT_CMP_ARG_TYPE
#undef LIGHT_EQUAL
#undef LIGHT_EQUAL_KEY

struct memtx_hash_index {
	struct index base;
	struct light_index_core hash_table;
	struct memtx_gc_task gc_task;
	struct light_index_iterator gc_iterator;
};

struct memtx_hash_index *
memtx_hash_index_new(struct memtx_engine *memtx, struct index_def *def);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_MEMTX_HASH_H_INCLUDED */
