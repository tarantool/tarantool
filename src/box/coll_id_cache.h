#ifndef TARANTOOL_BOX_COLL_ID_CACHE_H_INCLUDED
#define TARANTOOL_BOX_COLL_ID_CACHE_H_INCLUDED
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
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct coll_id;

/**
 * Create global hash tables.
 * @return - 0 on success, -1 on memory error.
 */
int
coll_id_cache_init();

/** Delete global hash tables. */
void
coll_id_cache_destroy();

/**
 * Insert or replace a collation into collation cache.
 * @param coll_id Collation to insert/replace.
 * @param Replaced_id Collation that was replaced.
 * @return - 0 on success, -1 on memory error.
 */
int
coll_id_cache_replace(struct coll_id *coll_id, struct coll_id **replaced_id);

/**
 * Delete a collation from collation cache.
 * @param coll_id Collation to delete.
 */
void
coll_id_cache_delete(const struct coll_id *coll_id);

/**
 * Find a collation object by its id.
 */
struct coll_id *
coll_by_id(uint32_t id);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_COLL_ID_CACHE_H_INCLUDED */
