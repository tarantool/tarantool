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
struct space;

enum {
	BOX_INDEX_MAX = 10,
//	BOX_SPACE_MAX = 256,
};


/** Get space ordinal number. */
int space_n(struct space *sp);

void space_validate(struct space *sp, struct tuple *old_tuple,
		    struct tuple *new_tuple);
void space_replace(struct space *sp, struct tuple *old_tuple,
		   struct tuple *new_tuple);
void space_remove(struct space *sp, struct tuple *tuple);


/* Get index by index no */
Index * space_index(struct space *sp, int index_no);

/* Set index by index no */
Index * space_set_index(struct space *sp, int index_no, Index *idx);


/* look through all enabled spaces */
int space_foreach(int (*space_i)(struct space *sp, void *udata), void *udata);


struct space * space_by_n(i32 space_no);	/* NULL if space not found */
struct space * space_find(i32 space_no);	/* raise if space not found */

int space_max_fieldno(struct space *sp);

enum field_data_type space_field_type(struct space *sp, int no);

int key_def_n(struct space *sp, struct key_def *kp);


struct space * space_create(
	i32 space_no, struct key_def *key, int key_count, int arity
);

bool space_number_is_valid(i32 space_no);


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

int index_count(struct space *sp);

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
	if (!idx)
		tnt_raise(LoggedError, :ER_NO_SUCH_INDEX, index_no,
			  space_n(sp));
	return idx;
}

#endif /* TARANTOOL_BOX_SPACE_H_INCLUDED */
