#ifndef TARANTOOL_BOX_INDEX_BUILD_H_INCLUDED
#define TARANTOOL_BOX_INDEX_BUILD_H_INCLUDED

#include <stdbool.h>

#include "space.h"
#include "tuple_format.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

void
index_build_init(void);

void
index_build_free(void);

struct index_build_vtab {
	int (*replace_confirmed)(struct index *index, struct tuple *tuple);
	int (*replace_in_progress)(struct index *index, struct tuple *old_tuple,
				   struct tuple *new_tuple);
	int (*rollback)(struct index *index, struct tuple *old_tuple,
			struct tuple *new_tuple);
	int (*finalize)(struct index *index);
};

int
generic_index_build_replace_confirmed(struct index *index,
				      struct tuple *tuple);

int
generic_index_build_replace_in_progress(struct index *index,
					struct tuple *old_tuple,
					struct tuple *new_tuple);

int
generic_index_build_rollback(struct index *index,
			     struct tuple *old_tuple,
			     struct tuple *new_tuple);

int
generic_index_build_finalize(struct index *index);

int
index_build(struct space *src_space, struct index *new_index,
	    struct tuple_format *new_format,
	    bool check_unique_constraint,
	    const struct index_build_vtab *vtab,
	    bool can_yield, int yield_loops, bool need_wal_sync);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_INDEX_BUILD_H_INCLUDED */
