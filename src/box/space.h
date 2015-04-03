#ifndef TARANTOOL_BOX_SPACE_H_INCLUDED
#define TARANTOOL_BOX_SPACE_H_INCLUDED
/*
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
#include "key_def.h"
#include "engine.h"
#include "salad/rlist.h"

struct space {
	struct access access[BOX_USER_MAX];
	/**
	 * Reflects the current space state and is also a vtab
	 * with methods. Unlike a C++ vtab, changes during space
	 * life cycle, throughout phases of recovery or with
	 * deletion and addition of indexes.
	 */
	Handler *handler;

	/** Triggers fired after space_replace() -- see txn_replace(). */
	struct rlist on_replace;
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
	struct space_def def;
	/** Enable/disable triggers. */
	bool run_triggers;

	/** Default tuple format used by this space */
	struct tuple_format *format;
	/**
	 * Sparse array of indexes defined on the space, indexed
	 * by id. Used to quickly find index by id (for SELECTs).
	 */
	Index **index_map;
	/**
	 * Dense array of indexes defined on the space, in order
	 * of index id. Initially stores only the primary key at
	 * position 0, and is fully built by
	 * space_build_secondary_keys().
	 */
	Index *index[];
};

/** Check whether or not the current user can be granted
 * the requested access to the space.
 */
void
access_check_space(struct space *space, uint8_t access);

/** Get space ordinal number. */
static inline uint32_t
space_id(struct space *space) { return space->def.id; }

/** Get space name. */
static inline const char *
space_name(struct space *space) { return space->def.name; }


/** Return true if space is temporary. */
static inline bool
space_is_temporary(struct space *space) { return space->def.temporary; }

/** Return true if space is run under sophia engine. */
static inline bool
space_is_sophia(struct space *space) { return strcmp(space->handler->engine->name, "sophia") == 0; }

/**
 * @brief A single method to handle REPLACE, DELETE and UPDATE.
 *
 * @param sp space
 * @param old_tuple the tuple that should be removed (can be NULL)
 * @param new_tuple the tuple that should be inserted (can be NULL)
 * @param mode      dup_replace_mode, used only if new_tuple is not
 *                  NULL and old_tuple is NULL, and only for the
 *                  primary key.
 *
 * For DELETE, new_tuple must be NULL. old_tuple must be
 * previously found in the primary key.
 *
 * For REPLACE, old_tuple must be NULL. The additional
 * argument dup_replace_mode further defines how REPLACE
 * should proceed.
 *
 * For UPDATE, both old_tuple and new_tuple must be given,
 * where old_tuple must be previously found in the primary key.
 *
 * Let's consider these three cases in detail:
 *
 * 1. DELETE, old_tuple is not NULL, new_tuple is NULL
 *    The effect is that old_tuple is removed from all
 *    indexes. dup_replace_mode is ignored.
 *
 * 2. REPLACE, old_tuple is NULL, new_tuple is not NULL,
 *    has one simple sub-case and two with further
 *    ramifications:
 *
 *	A. dup_replace_mode is DUP_INSERT. Attempts to insert the
 *	new tuple into all indexes. If *any* of the unique indexes
 *	has a duplicate key, deletion is aborted, all of its
 *	effects are removed, and an error is thrown.
 *
 *	B. dup_replace_mode is DUP_REPLACE. It means an existing
 *	tuple has to be replaced with the new one. To do it, tries
 *	to find a tuple with a duplicate key in the primary index.
 *	If the tuple is not found, throws an error. Otherwise,
 *	replaces the old tuple with a new one in the primary key.
 *	Continues on to secondary keys, but if there is any
 *	secondary key, which has a duplicate tuple, but one which
 *	is different from the duplicate found in the primary key,
 *	aborts, puts everything back, throws an exception.
 *
 *	For example, if there is a space with 3 unique keys and
 *	two tuples { 1, 2, 3 } and { 3, 1, 2 }:
 *
 *	This REPLACE/DUP_REPLACE is OK: { 1, 5, 5 }
 *	This REPLACE/DUP_REPLACE is not OK: { 2, 2, 2 } (there
 *	is no tuple with key '2' in the primary key)
 *	This REPLACE/DUP_REPLACE is not OK: { 1, 1, 1 } (there
 *	is a conflicting tuple in the secondary unique key).
 *
 *	C. dup_replace_mode is DUP_REPLACE_OR_INSERT. If
 *	there is a duplicate tuple in the primary key, behaves the
 *	same way as DUP_REPLACE, otherwise behaves the same way as
 *	DUP_INSERT.
 *
 * 3. UPDATE has to delete the old tuple and insert a new one.
 *    dup_replace_mode is ignored.
 *    Note that old_tuple primary key doesn't have to match
 *    new_tuple primary key, thus a duplicate can be found.
 *    For this reason, and since there can be duplicates in
 *    other indexes, UPDATE is the same as DELETE +
 *    REPLACE/DUP_INSERT.
 *
 * @return old_tuple. DELETE, UPDATE and REPLACE/DUP_REPLACE
 * always produce an old tuple. REPLACE/DUP_INSERT always returns
 * NULL. REPLACE/DUP_REPLACE_OR_INSERT may or may not find
 * a duplicate.
 *
 * The method is all-or-nothing in all cases. Changes are either
 * applied to all indexes, or nothing applied at all.
 *
 * Note, that even in case of REPLACE, dup_replace_mode only
 * affects the primary key, for secondary keys it's always
 * DUP_INSERT.
 *
 * @return tuple that was removed from the space.
 *         The call never removes more than one tuple: if
 *         old_tuple is given, dup_replace_mode is ignored.
 *         Otherwise, it's taken into account only for the
 *         primary key.
 */
static inline struct tuple *
space_replace(struct space *space, struct tuple *old_tuple,
	      struct tuple *new_tuple, enum dup_replace_mode mode)
{
	return space->handler->replace(space, old_tuple, new_tuple, mode);
}

void space_noop(struct space *space);

uint32_t
space_size(struct space *space);

/**
 * Check that the tuple has correct field count and correct field
 * types (a pre-requisite for an INSERT).
 */
void
space_validate_tuple(struct space *sp, struct tuple *new_tuple);

/**
 * Allocate and initialize a space. The space
 * needs to be loaded before it can be used
 * (see space->handler->recover()).
 */
struct space *
space_new(struct space_def *space_def, struct rlist *key_list);

/** Destroy and free a space. */
void
space_delete(struct space *space);

/**
 * Dump space definition (key definitions, key count)
 * for ALTER.
 */
void
space_dump_def(const struct space *space, struct rlist *key_list);

/**
 * Exchange two index objects in two spaces. Used
 * to update a space with a newly built index, while
 * making sure the old index doesn't leak.
 */
void
space_swap_index(struct space *lhs, struct space *rhs,
		 uint32_t lhs_id, uint32_t rhs_id);

/** Rebuild index map in a space after a series of swap index. */
void
space_fill_index_map(struct space *space);

/**
 * Get index by index id.
 * @return NULL if the index is not found.
 */
static inline Index *
space_index(struct space *space, uint32_t id)
{
	if (id <= space->index_id_max)
		return space->index_map[id];
	return NULL;
}

/**
 * Look up index by id.
 * Raise an error if the index is not found.
 */
static inline Index *
index_find(struct space *space, uint32_t index_id)
{
	Index *index = space_index(space, index_id);
	if (index == NULL)
		tnt_raise(LoggedError, ER_NO_SUCH_INDEX, index_id,
			  space_name(space));
	return index;
}

extern "C" void
space_run_triggers(struct space *space, bool yesno);

struct index_stat {
	int32_t id;
	int64_t keys;
	int64_t memsize;
};

struct space_stat {
	int32_t id;
	struct index_stat index[BOX_INDEX_MAX];
};

struct space_stat *
space_stat(struct space *space);

/**
 * Checks that primary key of a tuple did not change during update,
 * otherwise throws ClientError.
 * You should not call this method, if an engine can control it by
 * itself.
 */
void
space_check_update(struct space *space,
		   struct tuple *old_tuple,
		   struct tuple *new_tuple);

#endif /* TARANTOOL_BOX_SPACE_H_INCLUDED */
