#ifndef TARANTOOL_BOX_SPACE_H_INCLUDED
#define TARANTOOL_BOX_SPACE_H_INCLUDED
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
#include "user_def.h"
#include "space_def.h"
#include "small/rlist.h"
#include "engine.h"
#include "index.h"
#include "error.h"
#include "diag.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct space;
struct engine;
struct sequence;
struct txn;
struct request;
struct port;
struct tuple;
struct tuple_format;

struct space_vtab {
	/** Free a space instance. */
	void (*destroy)(struct space *);
	/** Return binary size of a space. */
	size_t (*bsize)(struct space *);

	int (*apply_initial_join_row)(struct space *, struct request *);

	int (*execute_replace)(struct space *, struct txn *,
			       struct request *, struct tuple **result);
	int (*execute_delete)(struct space *, struct txn *,
			      struct request *, struct tuple **result);
	int (*execute_update)(struct space *, struct txn *,
			      struct request *, struct tuple **result);
	int (*execute_upsert)(struct space *, struct txn *, struct request *);

	int (*ephemeral_replace)(struct space *, const char *, const char *);

	int (*ephemeral_delete)(struct space *, const char *);

	void (*ephemeral_cleanup)(struct space *);

	void (*init_system_space)(struct space *);
	/**
	 * Initialize an ephemeral space instance.
	 */
	void (*init_ephemeral_space)(struct space *);
	/**
	 * Check an index definition for violation of
	 * various limits.
	 */
	int (*check_index_def)(struct space *, struct index_def *);
	/**
	 * Create an instance of space index. Used in alter
	 * space before commit to WAL. The created index is
	 * deleted with delete operator.
	 */
	struct index *(*create_index)(struct space *, struct index_def *);
	/**
	 * Called by alter when a primary key is added,
	 * after create_index is invoked for the new
	 * key and before the write to WAL.
	 */
	int (*add_primary_key)(struct space *);
	/**
	 * Called by alter when the primary key is dropped.
	 * Do whatever is necessary with the space object,
	 * to not crash in DML.
	 */
	void (*drop_primary_key)(struct space *);
	/**
	 * Check that all tuples stored in a space are compatible
	 * with the new format.
	 */
	int (*check_format)(struct space *space, struct tuple_format *format);
	/**
	 * Build a new index, primary or secondary, and fill it
	 * with tuples stored in the given space. The function is
	 * supposed to assure that all tuples conform to the new
	 * format.
	 *
	 * @param src_space   space to use as build source
	 * @param new_index   index to build
	 * @param new_format  format for validating tuples
	 * @retval  0         success
	 * @retval -1         build failed
	 */
	int (*build_index)(struct space *src_space, struct index *new_index,
			   struct tuple_format *new_format);
	/**
	 * Exchange two index objects in two spaces. Used
	 * to update a space with a newly built index, while
	 * making sure the old index doesn't leak.
	 */
	void (*swap_index)(struct space *old_space, struct space *new_space,
			   uint32_t old_index_id, uint32_t new_index_id);
	/**
	 * Notify the engine about the changed space,
	 * before it's done, to prepare 'new_space' object.
	 */
	int (*prepare_alter)(struct space *old_space,
			     struct space *new_space);
};

struct space {
	/** Virtual function table. */
	const struct space_vtab *vtab;
	/** Cached runtime access information. */
	struct access access[BOX_USER_MAX];
	/** Engine used by this space. */
	struct engine *engine;
	/** Triggers fired before executing a request. */
	struct rlist before_replace;
	/** Triggers fired after space_replace() -- see txn_commit_stmt(). */
	struct rlist on_replace;
	/** Triggers fired before space statement */
	struct rlist on_stmt_begin;
	/**
	 * The number of *enabled* indexes in the space.
	 *
	 * After all indexes are built, it is equal to the number
	 * of non-nil members of the index[] array.
	 */
	uint32_t index_count;
	/**
	 * There may be gaps index ids, i.e. index 0 and 2 may exist,
	 * while index 1 is not defined. This member stores the
	 * max id of a defined index in the space. It defines the
	 * size of index_map array.
	 */
	uint32_t index_id_max;
	/** Space meta. */
	struct space_def *def;
	/** Sequence attached to this space or NULL. */
	struct sequence *sequence;
	/** Enable/disable triggers. */
	bool run_triggers;
	/**
	 * Space format or NULL if space does not have format
	 * (sysview engine, for example).
	 */
	struct tuple_format *format;
	/**
	 * Sparse array of indexes defined on the space, indexed
	 * by id. Used to quickly find index by id (for SELECTs).
	 */
	struct index **index_map;
	/**
	 * Dense array of indexes defined on the space, in order
	 * of index id.
	 */
	struct index **index;
};

/** Initialize a base space instance. */
int
space_create(struct space *space, struct engine *engine,
	     const struct space_vtab *vtab, struct space_def *def,
	     struct rlist *key_list, struct tuple_format *format);

/** Get space ordinal number. */
static inline uint32_t
space_id(struct space *space) { return space->def->id; }

/** Get space name. */
static inline const char *
space_name(const struct space *space)
{
	return space->def->name;
}

/** Return true if space is temporary. */
static inline bool
space_is_temporary(struct space *space) { return space->def->opts.temporary; }

void
space_run_triggers(struct space *space, bool yesno);

/**
 * Get index by index id.
 * @return NULL if the index is not found.
 */
static inline struct index *
space_index(struct space *space, uint32_t id)
{
	if (id <= space->index_id_max)
		return space->index_map[id];
	return NULL;
}

/**
 * Return key_def of the index identified by id or NULL
 * if there is no such index.
 */
struct key_def *
space_index_key_def(struct space *space, uint32_t id);

/**
 * Look up the index by id.
 */
static inline struct index *
index_find(struct space *space, uint32_t index_id)
{
	struct index *index = space_index(space, index_id);
	if (index == NULL) {
		diag_set(ClientError, ER_NO_SUCH_INDEX, index_id,
			 space_name(space));
		diag_log();
	}
	return index;
}

/**
 * Wrapper around index_find() which checks that
 * the found index is unique.
 */
static inline struct index *
index_find_unique(struct space *space, uint32_t index_id)
{
	struct index *index = index_find(space, index_id);
	if (index != NULL && !index->def->opts.is_unique) {
		diag_set(ClientError, ER_MORE_THAN_ONE_TUPLE);
		return NULL;
	}
	return index;
}

/**
 * Returns number of bytes used in memory by tuples in the space.
 */
size_t
space_bsize(struct space *space);

/** Get definition of the n-th index of the space. */
struct index_def *
space_index_def(struct space *space, int n);

/**
 * Get name of the index by its identifier and parent space.
 *
 * @param space Parent space.
 * @param id    Index identifier.
 *
 * @retval not NULL Index name.
 * @retval     NULL No index with the specified identifier.
 */
const char *
index_name_by_id(struct space *space, uint32_t id);

/**
 * Check whether or not the current user can be granted
 * the requested access to the space.
 */
int
access_check_space(struct space *space, user_access_t access);

static inline int
space_apply_initial_join_row(struct space *space, struct request *request)
{
	return space->vtab->apply_initial_join_row(space, request);
}

/**
 * Execute a DML request on the given space.
 */
int
space_execute_dml(struct space *space, struct txn *txn,
		  struct request *request, struct tuple **result);

static inline int
space_ephemeral_replace(struct space *space, const char *tuple,
			const char *tuple_end)
{
	return space->vtab->ephemeral_replace(space, tuple, tuple_end);
}

static inline int
space_ephemeral_delete(struct space *space, const char *key)
{
	return space->vtab->ephemeral_delete(space, key);
}

/**
 * Generic implementation of space_vtab::swap_index
 * that simply swaps the two indexes in index maps.
 */
void
generic_space_swap_index(struct space *old_space, struct space *new_space,
			 uint32_t old_index_id, uint32_t new_index_id);

static inline void
init_system_space(struct space *space)
{
	space->vtab->init_system_space(space);
}

static inline int
space_check_index_def(struct space *space, struct index_def *index_def)
{
	return space->vtab->check_index_def(space, index_def);
}

static inline struct index *
space_create_index(struct space *space, struct index_def *index_def)
{
	return space->vtab->create_index(space, index_def);
}

static inline int
space_add_primary_key(struct space *space)
{
	return space->vtab->add_primary_key(space);
}

static inline int
space_check_format(struct space *space, struct tuple_format *format)
{
	return space->vtab->check_format(space, format);
}

static inline void
space_drop_primary_key(struct space *space)
{
	space->vtab->drop_primary_key(space);
}

static inline int
space_build_index(struct space *src_space, struct index *new_index,
		  struct tuple_format *new_format)
{
	return src_space->vtab->build_index(src_space, new_index, new_format);
}

static inline void
space_swap_index(struct space *old_space, struct space *new_space,
		 uint32_t old_index_id, uint32_t new_index_id)
{
	assert(old_space->vtab == new_space->vtab);
	return new_space->vtab->swap_index(old_space, new_space,
					   old_index_id, new_index_id);
}

static inline int
space_prepare_alter(struct space *old_space, struct space *new_space)
{
	assert(old_space->vtab == new_space->vtab);
	return new_space->vtab->prepare_alter(old_space, new_space);
}

static inline bool
space_is_memtx(struct space *space) { return space->engine->id == 0; }

/** Return true if space is run under vinyl engine. */
static inline bool
space_is_vinyl(struct space *space) { return strcmp(space->engine->name, "vinyl") == 0; }

void space_noop(struct space *space);

struct field_def;
/**
 * Allocate and initialize a space.
 * @param space_def Space definition.
 * @param key_list List of index_defs.
 * @retval Space object.
 */
struct space *
space_new(struct space_def *space_def, struct rlist *key_list);

/**
 * Create an ephemeral space.
 * @param space_def Space definition.
 * @param key_list List of index_defs.
 * @retval Space object.
 *
 * Ephemeral spaces are invisible via public API and they
 * are not persistent. They are needed solely to do some
 * transient calculations.
 *
 * Ephemeral spaces created with this function must be
 * deleted with space_delete_ephemeral().
 */
struct space *
space_new_ephemeral(struct space_def *space_def, struct rlist *key_list);

/** Destroy and free a space. */
void
space_delete(struct space *space);

/**
 * Ephemeral spaces must be destroyed with
 * space_delete_ephemeral(). It calls ephemeral_cleanup() in
 * addition to space_delete(). Otherwise, it can lead to
 * memory leaks. For instance, calling space_delete() instead
 * of space_delete_ephemeral() for memtx engine doesn't unref
 * tuples, so they are appear to be incompletely destroyed.
 */
void
space_delete_ephemeral(struct space *space);

/**
 * Dump space definition (key definitions, key count)
 * for ALTER.
 */
void
space_dump_def(const struct space *space, struct rlist *key_list);

/** Rebuild index map in a space after a series of swap index. */
void
space_fill_index_map(struct space *space);

#if defined(__cplusplus)
} /* extern "C" */

static inline struct space *
space_new_xc(struct space_def *space_def, struct rlist *key_list)
{
	struct space *space = space_new(space_def, key_list);
	if (space == NULL)
		diag_raise();
	return space;
}

static inline void
access_check_space_xc(struct space *space, user_access_t access)
{
	if (access_check_space(space, access) != 0)
		diag_raise();
}

/**
 * Look up the index by id, and throw an exception if not found.
 */
static inline struct index *
index_find_xc(struct space *space, uint32_t index_id)
{
	struct index *index = index_find(space, index_id);
	if (index == NULL)
		diag_raise();
	return index;
}

static inline struct index *
index_find_unique_xc(struct space *space, uint32_t index_id)
{
	struct index *index = index_find_unique(space, index_id);
	if (index == NULL)
		diag_raise();
	return index;
}

/**
 * Find an index in a system space. Throw an error
 * if we somehow deal with a non-memtx space (it can't
 * be used for system spaces.
 */
static inline struct index *
index_find_system_xc(struct space *space, uint32_t index_id)
{
	if (! space_is_memtx(space)) {
		tnt_raise(ClientError, ER_UNSUPPORTED,
			  space->engine->name, "system data");
	}
	return index_find_xc(space, index_id);
}

static inline void
space_apply_initial_join_row_xc(struct space *space, struct request *request)
{
	if (space_apply_initial_join_row(space, request) != 0)
		diag_raise();
}

static inline void
space_check_index_def_xc(struct space *space, struct index_def *index_def)
{
	if (space_check_index_def(space, index_def) != 0)
		diag_raise();
}

static inline struct index *
space_create_index_xc(struct space *space, struct index_def *index_def)
{
	struct index *index = space_create_index(space, index_def);
	if (index == NULL)
		diag_raise();
	return index;
}

static inline void
space_add_primary_key_xc(struct space *space)
{
	if (space_add_primary_key(space) != 0)
		diag_raise();
}

static inline void
space_check_format_xc(struct space *space, struct tuple_format *format)
{
	if (space_check_format(space, format) != 0)
		diag_raise();
}

static inline void
space_build_index_xc(struct space *src_space, struct index *new_index,
		     struct tuple_format *new_format)
{
	if (space_build_index(src_space, new_index, new_format) != 0)
		diag_raise();
}

static inline void
space_prepare_alter_xc(struct space *old_space, struct space *new_space)
{
	if (space_prepare_alter(old_space, new_space) != 0)
		diag_raise();
}

#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_SPACE_H_INCLUDED */
