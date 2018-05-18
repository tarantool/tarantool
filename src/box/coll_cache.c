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
#include "coll_cache.h"
#include "diag.h"
#include "assoc.h"

/** mhash table (id -> collation) */
static struct mh_i32ptr_t *coll_cache_id = NULL;
/** mhash table (name, len -> collation) */
static struct mh_strnptr_t *coll_cache_name = NULL;

/** Create global hash tables if necessary. */
int
coll_cache_init()
{
	coll_cache_id = mh_i32ptr_new();
	if (coll_cache_id == NULL) {
		diag_set(OutOfMemory, sizeof(*coll_cache_id), "malloc",
			 "coll_cache_id");
		return -1;
	}
	coll_cache_name = mh_strnptr_new();
	if (coll_cache_name == NULL) {
		diag_set(OutOfMemory, sizeof(*coll_cache_name), "malloc",
			 "coll_cache_name");
		mh_i32ptr_delete(coll_cache_id);
		return -1;
	}
	return 0;
}

/** Delete global hash tables. */
void
coll_cache_destroy()
{
	mh_i32ptr_delete(coll_cache_id);
	mh_strnptr_delete(coll_cache_name);
}

/**
 * Insert or replace a collation into collation cache.
 * @param coll - collation to insert/replace.
 * @return - NULL if inserted, replaced collation if replaced.
 */
int
coll_cache_replace(struct coll *coll, struct coll **replaced)
{
	const struct mh_i32ptr_node_t id_node = {coll->id, coll};
	struct mh_i32ptr_node_t repl_id_node = {0, NULL};
	struct mh_i32ptr_node_t *prepl_id_node = &repl_id_node;
	mh_int_t i =
		mh_i32ptr_put(coll_cache_id, &id_node, &prepl_id_node, NULL);
	if (i == mh_end(coll_cache_id)) {
		diag_set(OutOfMemory, sizeof(id_node), "malloc",
			 "coll_cache_id");
		return -1;
	}

	uint32_t hash = mh_strn_hash(coll->name, coll->name_len);
	const struct mh_strnptr_node_t name_node =
		{ coll->name, coll->name_len, hash, coll };
	struct mh_strnptr_node_t repl_name_node = { NULL, 0, 0, NULL };
	struct mh_strnptr_node_t *prepl_node_name = &repl_name_node;
	if (mh_strnptr_put(coll_cache_name, &name_node, &prepl_node_name,
			   NULL) == mh_end(coll_cache_id)) {
		diag_set(OutOfMemory, sizeof(name_node), "malloc",
			 "coll_cache_name");
		mh_i32ptr_del(coll_cache_id, i, NULL);
		return -1;
	}
	assert(repl_id_node.val == repl_name_node.val);
	assert(repl_id_node.val == NULL);
	*replaced = repl_id_node.val;
	return 0;
}

/**
 * Delete a collation from collation cache.
 * @param coll - collation to delete.
 */
void
coll_cache_delete(const struct coll *coll)
{
	mh_int_t id_i = mh_i32ptr_find(coll_cache_id, coll->id, NULL);
	mh_i32ptr_del(coll_cache_id, id_i, NULL);
	mh_int_t name_i = mh_strnptr_find_inp(coll_cache_name, coll->name,
					      coll->name_len);
	mh_strnptr_del(coll_cache_name, name_i, NULL);
}

/**
 * Find a collation object by its id.
 */
struct coll *
coll_by_id(uint32_t id)
{
	mh_int_t pos = mh_i32ptr_find(coll_cache_id, id, NULL);
	if (pos == mh_end(coll_cache_id))
		return NULL;
	return mh_i32ptr_node(coll_cache_id, pos)->val;
}

/**
 * Find a collation object by its name.
 */
struct coll *
coll_by_name(const char *name, uint32_t len)
{
	mh_int_t pos = mh_strnptr_find_inp(coll_cache_name, name, len);
	if (pos == mh_end(coll_cache_name))
		return NULL;
	return mh_strnptr_node(coll_cache_name, pos)->val;
}
