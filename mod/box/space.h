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
	BOX_SPACE_MAX = 256,
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

	bool enabled;
};

extern struct space *spaces;

/** Get space ordinal number. */
static inline int
space_n(struct space *sp)
{
	assert(sp >= spaces && sp < (spaces + BOX_SPACE_MAX));
	return sp - spaces;
}

void space_validate(struct space *sp, struct tuple *old_tuple,
		    struct tuple *new_tuple);
void space_replace(struct space *sp, struct tuple *old_tuple,
		   struct tuple *new_tuple);
void space_remove(struct space *sp, struct tuple *tuple);

/** Get key_def ordinal number. */
static inline int
key_def_n(struct space *sp, struct key_def *kp)
{
	assert(kp >= sp->key_defs && kp < (sp->key_defs + sp->key_count));
	return kp - sp->key_defs;
}

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

static inline int
index_count(struct space *sp)
{
	if (!secondary_indexes_enabled) {
		/* If secondary indexes are not enabled yet,
		   we can use only the primary index. So return
		   1 if there is at least one index (which
		   must be primary) and return 0 otherwise. */
		return sp->key_count > 0;
	} else {
		/* Return the actual number of indexes. */
		return sp->key_count;
	}
}

void space_init(void);
void space_free(void);
i32 check_spaces(struct tarantool_cfg *conf);
/* Build secondary keys. */
void build_indexes(void);

static inline struct space *
space_find(u32 space_no)
{
	if (space_no >= BOX_SPACE_MAX)
		tnt_raise(ClientError, :ER_NO_SUCH_SPACE, space_no);

	struct space *sp = &spaces[space_no];

	if (!sp->enabled)
		tnt_raise(ClientError, :ER_SPACE_DISABLED, space_no);
	return sp;
}

static inline Index *
index_find(struct space *sp, u32 index_no)
{
	if (index_no >= sp->key_count)
		tnt_raise(LoggedError, :ER_NO_SUCH_INDEX, index_no,
			  space_n(sp));
	return sp->index[index_no];
}

#endif /* TARANTOOL_BOX_SPACE_H_INCLUDED */
