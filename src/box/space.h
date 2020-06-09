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
#include "bit/bit.h"
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
struct ck_constraint;
struct constraint_id;

struct space_vtab {
	/** Free a space instance. */
	void (*destroy)(struct space *);
	/** Return binary size of a space. */
	size_t (*bsize)(struct space *);

	int (*execute_replace)(struct space *, struct txn *,
			       struct request *, struct tuple **result);
	int (*execute_delete)(struct space *, struct txn *,
			      struct request *, struct tuple **result);
	int (*execute_update)(struct space *, struct txn *,
			      struct request *, struct tuple **result);
	int (*execute_upsert)(struct space *, struct txn *, struct request *);

	int (*ephemeral_replace)(struct space *, const char *, const char *);

	int (*ephemeral_delete)(struct space *, const char *);

	int (*ephemeral_rowid_next)(struct space *, uint64_t *);

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
	 * @param check_unique_constraint
	 *                    if this flag is set the build procedure
	 *                    must check the uniqueness constraint of
	 *                    the new index, otherwise the check may
	 *                    be optimized out even if the index is
	 *                    marked as unique
	 *
	 * @retval  0           success
	 * @retval -1           build failed
	 */
	int (*build_index)(struct space *src_space, struct index *new_index,
			   struct tuple_format *new_format,
			   bool check_unique_constraint);
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
	/**
	 * Called right after removing a space from the cache.
	 * The engine should abort all transactions involving
	 * the space, because the space will be destroyed soon.
	 *
	 * This function isn't allowed to yield or fail.
	 */
	void (*invalidate)(struct space *space);
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
	/** SQL Trigger list. */
	struct sql_trigger *sql_triggers;
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
	/** Auto increment field number. */
	uint32_t sequence_fieldno;
	/** Path to data in the auto-increment field. */
	char *sequence_path;
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
	/**
	 * If bit i is set, the unique constraint of index i must
	 * be checked before inserting a tuple into this space.
	 * Note, it isn't quite the same as index_opts::is_unique,
	 * as we don't need to check the unique constraint of
	 * a unique index in case the uniqueness of the indexed
	 * fields is guaranteed by another unique index.
	 */
	void *check_unique_constraint_map;
	/**
	 * List of check constraints linked with
	 * ck_constraint::link.
	 */
	struct rlist ck_constraint;
	/** Trigger that performs ck constraint validation. */
	struct trigger *ck_constraint_trigger;
	/**
	 * Lists of foreign key constraints. In SQL terms child
	 * space is the "from" table i.e. the table that contains
	 * the REFERENCES clause. Parent space is "to" table, in
	 * other words the table that is named in the REFERENCES
	 * clause.
	 */
	struct rlist parent_fk_constraint;
	struct rlist child_fk_constraint;
	/**
	 * Mask indicates which fields are involved in foreign
	 * key constraint checking routine. Includes fields
	 * of parent constraints as well as child ones.
	 */
	uint64_t fk_constraint_mask;
	/**
	 * Hash table with constraint identifiers hashed by name.
	 */
	struct mh_strnptr_t *constraint_ids;
	/**
	 * List of all tx stories in the space.
	 */
	struct rlist memtx_stories;
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
space_is_temporary(struct space *space)
{
	return space->def->opts.is_temporary;
}

/** Return replication group id of a space. */
static inline uint32_t
space_group_id(struct space *space)
{
	return space->def->opts.group_id;
}

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
 * Get index by index name.
 *
 * @param space Space index belongs to.
 * @param index_name Name of index to be found.
 *
 * @retval NULL if the index is not found.
 */
static inline struct index *
space_index_by_name(struct space *space, const char *index_name)
{
	for(uint32_t i = 0; i < space->index_count; i++) {
		struct index *index = space->index[i];
		if (strcmp(index_name, index->def->name) == 0)
			return index;
	}
	return NULL;
}

/**
 * Return true if the unique constraint must be checked for
 * the index with the given id before inserting a tuple into
 * the space.
 */
static inline bool
space_needs_check_unique_constraint(struct space *space, uint32_t index_id)
{
	return bit_test(space->check_unique_constraint_map, index_id);
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
		diag_set(ClientError, ER_NO_SUCH_INDEX_ID, index_id,
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
space_build_index(struct space *src_space, struct space *new_space,
		  struct index *new_index)
{
	bool check = space_needs_check_unique_constraint(new_space,
							 new_index->def->iid);
	return src_space->vtab->build_index(src_space, new_index,
					    new_space->format, check);
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

static inline void
space_invalidate(struct space *space)
{
	return space->vtab->invalidate(space);
}

static inline bool
space_is_memtx(struct space *space) { return space->engine->id == 0; }

/** Return true if space is run under vinyl engine. */
static inline bool
space_is_vinyl(struct space *space) { return strcmp(space->engine->name, "vinyl") == 0; }

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
 */
struct space *
space_new_ephemeral(struct space_def *space_def, struct rlist *key_list);

/** Destroy and free a space. */
void
space_delete(struct space *space);

/**
 * Dump space definition (key definitions, key count)
 * for ALTER.
 */
void
space_dump_def(const struct space *space, struct rlist *key_list);

/** Rebuild index map in a space after a series of swap index. */
void
space_fill_index_map(struct space *space);

/**
 * Add a new ck constraint to the space. A ck constraint check
 * trigger is created, if this is a first ck in this space. The
 * space takes ownership of this object.
 */
int
space_add_ck_constraint(struct space *space, struct ck_constraint *ck);

/**
 * Remove a ck constraint from the space. A ck constraint check
 * trigger is deleted, if this is a last ck in this space. This
 * object may be deleted manually after the call.
 */
void
space_remove_ck_constraint(struct space *space, struct ck_constraint *ck);

/** Find a constraint identifier by name. */
struct constraint_id *
space_find_constraint_id(struct space *space, const char *name);

/**
 * Add a new constraint id to the space's hash table of all
 * constraints. That is used to prevent existence of constraints
 * with equal names.
 */
int
space_add_constraint_id(struct space *space, struct constraint_id *id);

/**
 * Remove a given name from the hash of all constraint
 * identifiers of the given space.
 */
struct constraint_id *
space_pop_constraint_id(struct space *space, const char *name);

/*
 * Virtual method stubs.
 */
size_t generic_space_bsize(struct space *);
int generic_space_ephemeral_replace(struct space *, const char *, const char *);
int generic_space_ephemeral_delete(struct space *, const char *);
int generic_space_ephemeral_rowid_next(struct space *, uint64_t *);
void generic_init_system_space(struct space *);
void generic_init_ephemeral_space(struct space *);
int generic_space_check_index_def(struct space *, struct index_def *);
int generic_space_add_primary_key(struct space *space);
void generic_space_drop_primary_key(struct space *space);
int generic_space_check_format(struct space *, struct tuple_format *);
int generic_space_build_index(struct space *, struct index *,
			      struct tuple_format *, bool);
int generic_space_prepare_alter(struct space *, struct space *);
void generic_space_invalidate(struct space *);

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
space_build_index_xc(struct space *src_space, struct space *new_space,
		     struct index *new_index)
{
	if (space_build_index(src_space, new_space, new_index) != 0)
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
