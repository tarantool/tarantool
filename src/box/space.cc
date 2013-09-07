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
#include "space.h"
#include <stdlib.h>
#include <string.h>
#include <exception.h>
#include "tuple.h"

void
space_fill_index_map(struct space *space)
{
	space->index_count = 0;
	for (uint32_t j = 0; j <= space->index_id_max; j++) {
		Index *index = space->index_map[j];
		if (index)
			space->index[space->index_count++] = index;
	}
}

struct space *
space_new(struct space_def *space_def, struct rlist *key_list)
{
	uint32_t index_id_max = 0;
	uint32_t index_count = 0;
	struct key_def *key_def;
	rlist_foreach_entry(key_def, key_list, link) {
		index_count++;
		index_id_max = MAX(index_id_max, key_def->id);
	}
	size_t sz = sizeof(struct space) +
		(index_count + index_id_max + 1) * sizeof(Index *);
	struct space *space = (struct space *) calloc(1, sz);

	if (space == NULL)
		return NULL;

	space->index_map = (Index **)((char *) space + sizeof(*space) +
				      index_count * sizeof(Index *));
	space->def = *space_def;
	space->format = tuple_format_new(key_list);
	tuple_format_ref(space->format, 1);
	space->index_id_max = index_id_max;
	/* fill space indexes */
	rlist_foreach_entry(key_def, key_list, link) {
		struct key_def *dup = key_def_dup(key_def);
		if (dup == NULL)
			goto error;
		Index *index = Index::factory(dup);
		if (index == NULL) {
			key_def_delete(dup);
			goto error;
		}
		space->index_map[key_def->id] = index;
	}
	space_fill_index_map(space);
	space->engine = engine_no_keys;
	rlist_create(&space->on_replace);
	space->run_triggers = true;
	return space;
error:
	space_delete(space);
	return NULL;
}

void
space_delete(struct space *space)
{
	for (uint32_t j = 0; j < space->index_count; j++)
		delete space->index[j];
	tuple_format_ref(space->format, -1);
	free(space);
}

/**
 * A version of space_replace for a space which has
 * no indexes (is not yet fully built).
 */
struct tuple *
space_replace_no_keys(struct space *space, struct tuple * /* old_tuple */,
			 struct tuple * /* new_tuple */,
			 enum dup_replace_mode /* mode */)
{
	Index *index = index_find(space, 0);
	assert(index == NULL); /* not reached. */
	(void) index;
	return NULL; /* replace found no old tuple */
}

/** Do nothing if the space is already recovered. */
void
space_noop(struct space * /* space */)
{}

/**
 * A short-cut version of space_replace() used during bulk load
 * from snapshot.
 */
struct tuple *
space_replace_build_next(struct space *space, struct tuple *old_tuple,
			 struct tuple *new_tuple, enum dup_replace_mode mode)
{
	assert(old_tuple == NULL && mode == DUP_INSERT);
	(void) mode;
	if (old_tuple) {
		/*
		 * Called from txn_rollback() In practice
		 * is impossible: all possible checks for tuple
		 * validity are done before the space is changed,
		 * and WAL is off, so this part can't fail.
		 */
		panic("Failed to commit transaction when loading "
		      "from snapshot");
	}
	space->index[0]->buildNext(new_tuple);
	return NULL; /* replace found no old tuple */
}

/**
 * A short-cut version of space_replace() used when loading
 * data from XLOG files.
 */
struct tuple *
space_replace_primary_key(struct space *space, struct tuple *old_tuple,
			  struct tuple *new_tuple, enum dup_replace_mode mode)
{
	return space->index[0]->replace(old_tuple, new_tuple, mode);
}

static struct tuple *
space_replace_all_keys(struct space *space, struct tuple *old_tuple,
		       struct tuple *new_tuple, enum dup_replace_mode mode)
{
	uint32_t i = 0;
	try {
		/* Update the primary key */
		Index *pk = space->index[0];
		assert(pk->key_def->is_unique);
		/*
		 * If old_tuple is not NULL, the index
		 * has to find and delete it, or raise an
		 * error.
		 */
		old_tuple = pk->replace(old_tuple, new_tuple, mode);

		assert(old_tuple || new_tuple);
		/* Update secondary keys. */
		for (i++; i < space->index_count; i++) {
			Index *index = space->index[i];
			index->replace(old_tuple, new_tuple, DUP_INSERT);
		}
		return old_tuple;
	} catch (const Exception &e) {
		/* Rollback all changes */
		for (; i > 0; i--) {
			Index *index = space->index[i-1];
			index->replace(new_tuple, old_tuple, DUP_INSERT);
		}
		throw;
	}

	assert(false);
	return NULL;
}

uint32_t
space_size(struct space *space)
{
	return space_index(space, 0)->size();
}

/**
 * Secondary indexes are built in bulk after all data is
 * recovered. This function enables secondary keys on a space.
 * Data dictionary spaces are an exception, they are fully
 * built right from the start.
 */
void
space_build_secondary_keys(struct space *space)
{
	if (space->index_id_max > 0) {
		Index *pk = space->index[0];
		uint32_t n_tuples = pk->size();

		if (n_tuples > 0) {
			say_info("Building secondary indexes in space %d...",
				 space_id(space));
		}

		for (uint32_t j = 1; j < space->index_count; j++)
			index_build(space->index[j], pk);

		if (n_tuples > 0) {
			say_info("Space %d: done", space_id(space));
		}
	}
	space->engine.state = READY_ALL_KEYS;
	space->engine.recover = space_noop; /* mark the end of recover */
	space->engine.replace = space_replace_all_keys;
}

/** Build the primary key after loading data from a snapshot. */
void
space_end_build_primary_key(struct space *space)
{
	space->index[0]->endBuild();
	space->engine.state = READY_PRIMARY_KEY;
	space->engine.replace = space_replace_primary_key;
	space->engine.recover = space_build_secondary_keys;
}

/** Prepare the primary key for bulk load (loading from
 * a snapshot).
 */
void
space_begin_build_primary_key(struct space *space)
{
	space->index[0]->beginBuild();
	space->engine.replace = space_replace_build_next;
	space->engine.recover = space_end_build_primary_key;
}

/**
 * Bring a space up to speed if its primary key is added during
 * XLOG recovery. This is a recovery function called on
 * spaces which had no primary key at the end of snapshot
 * recovery, and got one only when reading an XLOG.
 */
void
space_build_primary_key(struct space *space)
{
	space_begin_build_primary_key(space);
	space_end_build_primary_key(space);
}

/** Bring a space up to speed once it's got a primary key.
 *
 * This is a recovery function used for all spaces added after the
 * end of SNAP/XLOG recovery.
 */
void
space_build_all_keys(struct space *space)
{
	space_build_primary_key(space);
	space_build_secondary_keys(space);
}

/**
 * This is a vtab with which a newly created space which has no
 * keys is primed.
 * At first it is set to correctly work for spaces created during
 * recovery from snapshot. In process of recovery it is updated as
 * below:
 *
 * 1) after SNAP is loaded:
 *    recover = space_build_primary_key
 * 2) when all XLOGs are loaded:
 *    recover = space_build_all_keys
 */
struct engine engine_no_keys = {
	/* .state = */   READY_NO_KEYS,
	/* .recover = */ space_begin_build_primary_key,
	/* .replace = */ space_replace_no_keys
};

void
space_validate_tuple(struct space *sp, struct tuple *new_tuple)
{
	if (sp->def.arity > 0 && sp->def.arity != new_tuple->field_count)
		tnt_raise(ClientError, ER_SPACE_ARITY,
			  new_tuple->field_count, sp->def.id, sp->def.arity);
}

void
space_dump_def(const struct space *space, struct rlist *key_list)
{
	rlist_create(key_list);

	for (int j = 0; j < space->index_count; j++)
		rlist_add_tail_entry(key_list, space->index[j]->key_def,
				     link);
}

void
space_swap_index(struct space *lhs, struct space *rhs, uint32_t lhs_id,
		 uint32_t rhs_id)
{
	Index *tmp = lhs->index_map[lhs_id];
	lhs->index_map[lhs_id] = rhs->index_map[rhs_id];
	rhs->index_map[rhs_id] = tmp;
}

extern "C" void
space_run_triggers(struct space *space, bool yesno)
{
	space->run_triggers = yesno;
}

/* vim: set fm=marker */
