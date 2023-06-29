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
#include <stdbool.h>
#include "small/rlist.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct coll_id;

/**
 * Type of a holder that can pin coll_id. See `struct coll_id_cache_holder`.
 */
enum coll_id_holder_type {
	COLL_ID_HOLDER_SPACE_FORMAT,
	COLL_ID_HOLDER_INDEX,
	COLL_ID_HOLDER_MAX,
};

/**
 * Lowercase name of each type.
 */
extern const char *coll_id_holder_type_strs[COLL_ID_HOLDER_MAX];

/**
 * Definition of a holder that pinned some coll_id. Pinning of a coll_id is
 * a mechanism that is designed for preventing of deletion of some coll_id from
 * coll_id cache by storing links to holders that prevented that.
 */
struct coll_id_cache_holder {
	/** Link in `space::coll_id_holders`. */
	struct rlist in_space;
	/** Link in `coll_id::cache_pin_list`. */
	struct rlist in_coll_id;
	/** Actual pointer to coll_id. */
	struct coll_id *coll_id;
	/**
	 * Type of holder, mostly for better error generation, but also can be
	 * used for proper container_of application.
	 */
	enum coll_id_holder_type type;
};

/**
 * Create global hash tables.
 */
void
coll_id_cache_init(void);

/** Delete global hash tables. */
void
coll_id_cache_destroy(void);

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
coll_id_cache_delete(struct coll_id *coll_id);

/**
 * Find a collation object by its id.
 */
struct coll_id *
coll_by_id(uint32_t id);

/**
 * Find a collation object by its name.
 */
struct coll_id *
coll_by_name(const char *name, uint32_t len);

/**
 * Register that there is a `holder` of type `type` that is dependent on
 * coll_id. coll_id must be in cache (asserted).
 * If coll_id has holders, it must not be deleted (asserted).
 */
void
coll_id_pin(struct coll_id *coll_id, struct coll_id_cache_holder *holder,
	    enum coll_id_holder_type type);

/**
 * Notify that `holder` does not depend anymore on coll_id.
 * coll_id must be in cache (asserted).
 * If coll_id has no holders, it can be deleted.
 */
void
coll_id_unpin(struct coll_id_cache_holder *holder);

/**
 * Check whether coll_id has holders or not.
 * If it has, `type` argument is set to the first holder's type.
 * coll_id must be in cache (asserted).
 * If coll_id has holders, it must not be deleted (asserted).
 */
bool
coll_id_is_pinned(struct coll_id *coll_id, enum coll_id_holder_type *type);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_COLL_ID_CACHE_H_INCLUDED */
