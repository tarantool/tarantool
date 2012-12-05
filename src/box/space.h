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
#include <exception.h>

struct tarantool_cfg;


enum {
	BOX_INDEX_MAX = 10,
};

struct space {
	Index *index[BOX_INDEX_MAX];
	/** If not set (is 0), any tuple in the
	 * space can have any number of fields (but
	 * @sa max_fieldno). If set, Each tuple
	 * must have exactly this many fields.
	 */
	int arity;

	/**
	 * The number of indexes in the space.
	 *
	 * It is equal to the number of non-nil members of the index
	 * array and defines the key_defs array size as well.
	 */
	int key_count;

	/**
	 * The descriptors for all indexes that belong to the space.
	 */
	struct key_def *key_defs;

	/**
	 * Field types of indexed fields. This is an array of size
	 * field_count. If there are gaps, i.e. fields that do not
	 * participate in any index and thus we cannot infer their
	 * type, then respective array members have value UNKNOWN.
	 * XXX: right now UNKNOWN is also set for fields which types
	 * in two indexes contradict each other.
	 */
	enum field_data_type *field_types;

	/**
	 * Max field no which participates in any of the space indexes.
	 * Each tuple in this space must have, therefore, at least
	 * field_count fields.
	 */
	int max_fieldno;

	/** Space number. */
	i32 no;
};


/** Get space ordinal number. */
static inline i32 space_n(struct space *sp) { return sp->no; }

/**
 * @brief Replaces the @a old_tuple with the @a new_tuple in the @a sp.
 *
 * There is three major use cases for this method:
 *
 * 1. old_tuple = NULL, new_tuple != NULL
 * Inserts or replaces the @a new_tuple in the @a space. If no flags are set
 * and an old tuple in the @a sp has the same primary key as @a new_tuple,
 * the old tuple is removed before new tuple is inserted. If THROW_INSERT flag
 * is set and old tuple with same primary key is found, TupleFound exception
 * is thrown. If REPLACE_THROW flags is set and old tple is not found,
 * TupleNotFound exception is thrown. The return value is an tuple that was
 * actually removed. This case is usually used for box.replace.
 *
 * 2. old_tuple != NULL, new_tuple == NULL
 * Removes @a old_tuple from the @a space. Please note, that @a old_tuple
 * is removed only if it has same pointer address as an tuple in the @a sp.
 * The return value is an @a old_tuple if it was found or NULL otherwise.
 * This case is usually used for box.remove.
 *
 * 3. old_tuple != NULL, new_tuple != NULL
 * Perform atomically an operation that equivalent to
 * replace(sp, old_tuple, NULL, flags) + replace(sp, NULL, new_tuple, flags).
 * THROW_INSERT flag must be always set, because only one tuple can be removed
 * per one call. This case is usually used for box.update.
 *
 * The method is **atomic** in all cases. Changes are either applied to all
 * indexes, or nothing applied at all.
 *
 * All possible cases are described in the table:
 * +--------------------------------------------------------------------------+
 * | old  | new  | oldf | newf | A | R | Action               | Result        |
 * +--------------------------------------------------------------------------+
 * | NULL |  XX  |      |  XX  | 0 |0,1| r(newf), i(new)      | newf          |
 * | NULL |  XX  |      | NULL |0,1| 0 | i(new)               | NULL          |
 * | NULL |  XX  |      |  XX  | 1 | 0 |                      | TupleFound    |
 * | NULL |  XX  |      | NULL | 0 | 1 |                      | TupleNotFound |
 * +------+------+------+------+---+---+----------------------+---------------+
 * |  XX  | NULL |  XX  |      | * |0,1| r(oldf)              | oldf          |
 * |  XX  | NULL | NULL |      | * | 0 |                      | NULL          |
 * |  XX  | NULL | NULL |      | * | 1 |                      | TupleNotFound |
 * +------+------+------+------+---+---+----------------------+---------------+
 * |  XX  |  XX  |  XX  |!=oldf| 1 | 0 |                      | TupleFound    |
 * |  XX  |  XX  |  XX  |==oldf| 1 | 0 | r(oldf), i(new)      | oldf          |
 * |  XX  |  XX  |  XX  | NULL | 1 | 0 | r(oldf), i(new)      | oldf          |
 * |  XX  |  XX  | NULL |  XX  | 1 | 0 | i(new)               | NULL          |
 * |  XX  |  XX  | NULL | NULL | 1 | 0 | i(new)               | NULL          |
 * +------+------+------+------+---+---+----------------------+---------------+
 * oldf = findByTuple(old), newf = findByTuple(new), i = insert, r = remove,
 * A - THROW_INSERT, R = REPLACE_THROW (in @a flags parameter).
 *
 * @param sp space
 * @param old_tuple the tuple that should be removed (can be NULL)
 * @param new_tuple the tuple that should be inserted (can be NULL)
 * @param flags THROW_INSERT and REPLACE_THROW flags
 * @return tuple that actually has been removed from the space
 */
struct tuple *
space_replace(struct space *space, struct tuple *old_tuple,
	      struct tuple *new_tuple, enum replace_flags flags);
void
space_validate_tuple(struct space *sp, struct tuple *new_tuple);

/**
 * Get index by index number.
 * @return NULL if index not found.
 */
static inline Index *
space_index(struct space *sp, int index_no)
{
	if (index_no >= 0 && index_no < BOX_INDEX_MAX)
		return sp->index[index_no];
	return NULL;
}

/** Set index by index no. */
void
space_set_index(struct space *sp, int index_no, Index *idx);

/**
 * Call a visitor function on every enabled space.
 */
void
space_foreach(void (*func)(struct space *sp, void *udata), void *udata);

/**
 * Try to look up a space by space number.
 *
 * @return NULL if space not found, otherwise space object.
 */
struct space *space_by_n(i32 space_no);

static inline struct space *
space_find(i32 space_no)
{
	struct space *s = space_by_n(space_no);
	if (s)
		return s;

	tnt_raise(ClientError, :ER_NO_SUCH_SPACE, space_no);
}


/** Get key_def ordinal number. */
static inline int
key_def_n(struct space *sp, struct key_def *kp)
{
	assert(kp >= sp->key_defs && kp < (sp->key_defs + sp->key_count));
	return kp - sp->key_defs;
}

static inline int
space_max_fieldno(struct space *sp)
{
	return sp->max_fieldno;
}

static inline enum field_data_type
space_field_type(struct space *sp, int no)
{
	return sp->field_types[no];
}


struct space *
space_create(i32 space_no, struct key_def *key_defs, int key_count, int arity);


/** Get index ordinal number in space. */
static inline int
index_n(Index *index)
{
	return key_def_n(index->space, index->key_def);
}

/** Check whether or not an index is primary in space.  */
static inline bool
index_is_primary(Index *index)
{
	return index_n(index) == 0;
}

/**
 * Secondary indexes are built in bulk after all data is
 * recovered. This flag indicates that the indexes are
 * already built and ready for use.
 */
extern bool secondary_indexes_enabled;
/**
 * Primary indexes are enabled only after reading the snapshot.
 */
extern bool primary_indexes_enabled;

void space_init(void);
void space_free(void);
i32 check_spaces(struct tarantool_cfg *conf);
/* Build secondary keys. */
void begin_build_primary_indexes(void);
void end_build_primary_indexes(void);
void build_secondary_indexes(void);


static inline Index *
index_find(struct space *sp, int index_no)
{
	Index *idx = space_index(sp, index_no);
	if (idx == NULL)
		tnt_raise(LoggedError, :ER_NO_SUCH_INDEX, index_no,
			  space_n(sp));
	return idx;
}

#endif /* TARANTOOL_BOX_SPACE_H_INCLUDED */
