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
#include "sysview.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <small/mempool.h>

#include "diag.h"
#include "error.h"
#include "errcode.h"
#include "schema.h"
#include "sequence.h"
#include "space.h"
#include "index.h"
#include "engine.h"
#include "func.h"
#include "tuple.h"
#include "session.h"

typedef bool (*sysview_filter_f)(struct space *, struct tuple *);

struct sysview_engine {
	struct engine base;
	/** Memory pool for index iterator. */
	struct mempool iterator_pool;
};

struct sysview_index {
	struct index base;
	uint32_t source_space_id;
	uint32_t source_index_id;
	sysview_filter_f filter;
};

struct sysview_iterator {
	struct iterator base;
	struct iterator *source;
	struct space *space;
	/** Memory pool the iterator was allocated from. */
	struct mempool *pool;
};

static inline struct sysview_iterator *
sysview_iterator(struct iterator *ptr)
{
	return (struct sysview_iterator *) ptr;
}

static void
sysview_iterator_free(struct iterator *ptr)
{
	struct sysview_iterator *it = sysview_iterator(ptr);
	iterator_delete(it->source);
	mempool_free(it->pool, it);
}

static int
sysview_iterator_next(struct iterator *iterator, struct tuple **ret)
{
	assert(iterator->free == sysview_iterator_free);
	struct sysview_iterator *it = sysview_iterator(iterator);
	*ret = NULL;
	if (it->source->space_cache_version != space_cache_version)
		return 0; /* invalidate iterator */
	struct sysview_index *index = (struct sysview_index *)iterator->index;
	int rc;
	while ((rc = iterator_next(it->source, ret)) == 0 && *ret != NULL) {
		if (index->filter(it->space, *ret))
			break;
	}
	return rc;
}

static void
sysview_index_destroy(struct index *index)
{
	free(index);
}

static struct iterator *
sysview_index_create_iterator(struct index *base, enum iterator_type type,
			      const char *key, uint32_t part_count)
{
	struct sysview_index *index = (struct sysview_index *)base;
	struct sysview_engine *sysview = (struct sysview_engine *)base->engine;

	struct space *source = space_cache_find(index->source_space_id);
	if (source == NULL)
		return NULL;
	struct index *pk = index_find(source, index->source_index_id);
	if (pk == NULL)
		return NULL;
	/*
	 * Explicitly validate that key matches source's index_def.
	 * It is possible to change a source space without changing
	 * the view.
	 */
	if (key_validate(pk->def, type, key, part_count))
		return NULL;

	struct sysview_iterator *it = mempool_alloc(&sysview->iterator_pool);
	if (it == NULL) {
		diag_set(OutOfMemory, sizeof(struct sysview_iterator),
			 "mempool", "struct sysview_iterator");
		return NULL;
	}
	iterator_create(&it->base, base);
	it->pool = &sysview->iterator_pool;
	it->base.next = sysview_iterator_next;
	it->base.free = sysview_iterator_free;

	it->source = index_create_iterator(pk, type, key, part_count);
	if (it->source == NULL) {
		mempool_free(&sysview->iterator_pool, it);
		return NULL;
	}
	it->space = source;
	return (struct iterator *)it;
}

static int
sysview_index_get(struct index *base, const char *key,
		  uint32_t part_count, struct tuple **result)
{
	struct sysview_index *index = (struct sysview_index *)base;
	struct space *source = space_cache_find(index->source_space_id);
	if (source == NULL)
		return -1;
	struct index *pk = index_find(source, index->source_index_id);
	if (pk == NULL)
		return -1;
	if (!pk->def->opts.is_unique) {
		diag_set(ClientError, ER_MORE_THAN_ONE_TUPLE);
		return -1;
	}
	if (exact_key_validate(pk->def->key_def, key, part_count) != 0)
		return -1;
	struct tuple *tuple;
	if (index_get(pk, key, part_count, &tuple) != 0)
		return -1;
	if (tuple == NULL || !index->filter(source, tuple))
		*result = NULL;
	else
		*result = tuple;
	return 0;
}

static const struct index_vtab sysview_index_vtab = {
	/* .destroy = */ sysview_index_destroy,
	/* .commit_create = */ generic_index_commit_create,
	/* .abort_create = */ generic_index_abort_create,
	/* .commit_modify = */ generic_index_commit_modify,
	/* .commit_drop = */ generic_index_commit_drop,
	/* .update_def = */ generic_index_update_def,
	/* .depends_on_pk = */ generic_index_depends_on_pk,
	/* .def_change_requires_rebuild = */
		generic_index_def_change_requires_rebuild,
	/* .size = */ generic_index_size,
	/* .bsize = */ generic_index_bsize,
	/* .min = */ generic_index_min,
	/* .max = */ generic_index_max,
	/* .random = */ generic_index_random,
	/* .count = */ generic_index_count,
	/* .get = */ sysview_index_get,
	/* .replace = */ generic_index_replace,
	/* .create_iterator = */ sysview_index_create_iterator,
	/* .create_snapshot_iterator = */
		generic_index_create_snapshot_iterator,
	/* .stat = */ generic_index_stat,
	/* .compact = */ generic_index_compact,
	/* .reset_stat = */ generic_index_reset_stat,
	/* .begin_build = */ generic_index_begin_build,
	/* .reserve = */ generic_index_reserve,
	/* .build_next = */ generic_index_build_next,
	/* .end_build = */ generic_index_end_build,
};

static void
sysview_space_destroy(struct space *space)
{
	free(space);
}

static int
sysview_space_execute_replace(struct space *space, struct txn *txn,
			      struct request *request, struct tuple **result)
{
	(void)txn;
	(void)request;
	(void)result;
	diag_set(ClientError, ER_VIEW_IS_RO, space->def->name);
	return -1;
}

static int
sysview_space_execute_delete(struct space *space, struct txn *txn,
			     struct request *request, struct tuple **result)
{
	(void)txn;
	(void)request;
	(void)result;
	diag_set(ClientError, ER_VIEW_IS_RO, space->def->name);
	return -1;
}

static int
sysview_space_execute_update(struct space *space, struct txn *txn,
			     struct request *request, struct tuple **result)
{
	(void)txn;
	(void)request;
	(void)result;
	diag_set(ClientError, ER_VIEW_IS_RO, space->def->name);
	return -1;
}

static int
sysview_space_execute_upsert(struct space *space, struct txn *txn,
			     struct request *request)
{
	(void)txn;
	(void)request;
	diag_set(ClientError, ER_VIEW_IS_RO, space->def->name);
	return -1;
}

/*
 * System view filters.
 * Filter gives access to an object, if one of the following conditions is true:
 * 1. User has read, write, drop or alter access to universe.
 * 2. User has read access to according system space.
 * 3. User has read, write, drop or alter access to the object.
 * 4. User is a owner of the object.
 * 5. User is grantor or grantee for the privilege.
 * 6. User has execute for the function or the sequence.
 * 7. User is parent for the user/role.
 */

const uint32_t PRIV_WRDA = PRIV_W | PRIV_D | PRIV_A | PRIV_R;

static bool
vspace_filter(struct space *source, struct tuple *tuple)
{
	struct credentials *cr = effective_user();
	/*
	 * Allow access for a user with read, write,
	 * drop or alter privileges for universe.
	 */
	if (PRIV_WRDA & cr->universal_access)
		return true;
	/* Allow access for a user with space privileges. */
	if (PRIV_WRDA & entity_access_get(SC_SPACE)[cr->auth_token].effective)
		return true;
	if (PRIV_R & source->access[cr->auth_token].effective)
		return true; /* read access to _space space */
	uint32_t space_id;
	if (tuple_field_u32(tuple, BOX_SPACE_FIELD_ID, &space_id) != 0)
		return false;
	struct space *space = space_cache_find(space_id);
	if (space == NULL)
		return false;
	user_access_t effective = space->access[cr->auth_token].effective;
	/*
	 * Allow access for space owners and users with any
	 * privilege for the space.
	 */
	return (PRIV_WRDA & effective ||
	       space->def->uid == cr->uid);
}

static bool
vuser_filter(struct space *source, struct tuple *tuple)
{
	struct credentials *cr = effective_user();
	/*
	 * Allow access for a user with read, write,
	 * drop or alter privileges for universe.
	 */
	if (PRIV_WRDA & cr->universal_access)
		return true;
	if (PRIV_R & source->access[cr->auth_token].effective)
		return true; /* read access to _user space */

	uint32_t uid;
	if (tuple_field_u32(tuple, BOX_USER_FIELD_ID, &uid) != 0)
		return false;
	uint32_t owner_id;
	if (tuple_field_u32(tuple, BOX_USER_FIELD_UID, &owner_id) != 0)
		return false;
	/* Allow access for self, childs or public user. */
	return uid == cr->uid || owner_id == cr->uid || uid == PUBLIC;
}

static bool
vpriv_filter(struct space *source, struct tuple *tuple)
{
	struct credentials *cr = effective_user();
	/*
	 * Allow access for a user with read, write,
	 * drop or alter privileges for universe.
	 */
	if (PRIV_WRDA & cr->universal_access)
		return true;
	if (PRIV_R & source->access[cr->auth_token].effective)
		return true; /* read access to _priv space */

	uint32_t grantor_id;
	if (tuple_field_u32(tuple, BOX_PRIV_FIELD_ID, &grantor_id) != 0)
		return false;
	uint32_t grantee_id;
	if (tuple_field_u32(tuple, BOX_PRIV_FIELD_UID, &grantee_id) != 0)
		return false;
	/* Allow access for privilege grantor or grantee. */
	return grantor_id == cr->uid || grantee_id == cr->uid;
}

static bool
vfunc_filter(struct space *source, struct tuple *tuple)
{
	struct credentials *cr = effective_user();
	/*
	 * Allow access for a user with read, write,
	 * drop, alter or execute privileges for universe.
	 */
	if ((PRIV_WRDA | PRIV_X) & cr->universal_access)
		return true;
	/* Allow access for a user with function privileges. */
	if ((PRIV_WRDA | PRIV_X) &
	    entity_access_get(SC_FUNCTION)[cr->auth_token].effective)
		return true;
	if (PRIV_R & source->access[cr->auth_token].effective)
		return true; /* read access to _func space */

	uint32_t name_len;
	const char *name = tuple_field_str(tuple, BOX_FUNC_FIELD_NAME,
					   &name_len);
	if (name == NULL)
		return false;
	struct func *func = func_by_name(name, name_len);
	assert(func != NULL);
	user_access_t effective = func->access[cr->auth_token].effective;
	return func->def->uid == cr->uid ||
	       ((PRIV_WRDA | PRIV_X) & effective);
}

static bool
vsequence_filter(struct space *source, struct tuple *tuple)
{
	struct credentials *cr = effective_user();
	/*
	 * Allow access for a user with read, write,
	 * drop, alter or execute privileges for universe.
	 */
	if ((PRIV_WRDA | PRIV_X) & cr->universal_access)
		return true;
	/* Allow access for a user with sequence privileges. */
	if ((PRIV_WRDA | PRIV_X) &
	    entity_access_get(SC_SEQUENCE)[cr->auth_token].effective)
		return true;
	if (PRIV_R & source->access[cr->auth_token].effective)
		return true; /* read access to _sequence space */

	uint32_t id;
	if (tuple_field_u32(tuple, BOX_SEQUENCE_FIELD_ID, &id) != 0)
		return false;
	struct sequence *sequence = sequence_by_id(id);
	if (sequence == NULL)
		return false;
	user_access_t effective = sequence->access[cr->auth_token].effective;
	return sequence->def->uid == cr->uid ||
	       ((PRIV_WRDA | PRIV_X) & effective);
}

static bool
vcollation_filter(struct space *source, struct tuple *tuple)
{
	(void) source;
	(void) tuple;
	return true;
}

static struct index *
sysview_space_create_index(struct space *space, struct index_def *def)
{
	assert(def->type == TREE);

	struct sysview_engine *sysview = (struct sysview_engine *)space->engine;
	if (!mempool_is_initialized(&sysview->iterator_pool)) {
		mempool_create(&sysview->iterator_pool, cord_slab_cache(),
			       sizeof(struct sysview_iterator));
	}

	uint32_t source_space_id;
	uint32_t source_index_id;
	sysview_filter_f filter;

	switch (def->space_id) {
	case BOX_VSPACE_ID:
		source_space_id = BOX_SPACE_ID;
		source_index_id = def->iid;
		filter = vspace_filter;
		break;
	case BOX_VINDEX_ID:
		source_space_id = BOX_INDEX_ID;
		source_index_id = def->iid;
		filter = vspace_filter;
		break;
	case BOX_VUSER_ID:
		source_space_id = BOX_USER_ID;
		source_index_id = def->iid;
		filter = vuser_filter;
		break;
	case BOX_VFUNC_ID:
		source_space_id = BOX_FUNC_ID;
		source_index_id = def->iid;
		filter = vfunc_filter;
		break;
	case BOX_VPRIV_ID:
		source_space_id = BOX_PRIV_ID;
		source_index_id = def->iid;
		filter = vpriv_filter;
		break;
	case BOX_VSEQUENCE_ID:
		source_space_id = BOX_SEQUENCE_ID;
		source_index_id = def->iid;
		filter = vsequence_filter;
		break;
	case BOX_VCOLLATION_ID:
		source_space_id = BOX_COLLATION_ID;
		source_index_id = def->iid;
		filter = vcollation_filter;
		break;
	default:
		diag_set(ClientError, ER_MODIFY_INDEX,
			 def->name, space_name(space),
			 "unknown space for system view");
		return NULL;
	}

	struct sysview_index *index =
		(struct sysview_index *)calloc(1, sizeof(*index));
	if (index == NULL) {
		diag_set(OutOfMemory, sizeof(*index),
			 "malloc", "struct sysview_index");
		return NULL;
	}
	if (index_create(&index->base, (struct engine *)sysview,
			 &sysview_index_vtab, def) != 0) {
		free(index);
		return NULL;
	}

	index->source_space_id = source_space_id;
	index->source_index_id = source_index_id;
	index->filter = filter;
	return &index->base;
}

static const struct space_vtab sysview_space_vtab = {
	/* .destroy = */ sysview_space_destroy,
	/* .bsize = */ generic_space_bsize,
	/* .execute_replace = */ sysview_space_execute_replace,
	/* .execute_delete = */ sysview_space_execute_delete,
	/* .execute_update = */ sysview_space_execute_update,
	/* .execute_upsert = */ sysview_space_execute_upsert,
	/* .ephemeral_replace = */ generic_space_ephemeral_replace,
	/* .ephemeral_delete = */ generic_space_ephemeral_delete,
	/* .ephemeral_rowid_next = */ generic_space_ephemeral_rowid_next,
	/* .init_system_space = */ generic_init_system_space,
	/* .init_ephemeral_space = */ generic_init_ephemeral_space,
	/* .check_index_def = */ generic_space_check_index_def,
	/* .create_index = */ sysview_space_create_index,
	/* .add_primary_key = */ generic_space_add_primary_key,
	/* .drop_primary_key = */ generic_space_drop_primary_key,
	/* .check_format = */ generic_space_check_format,
	/* .build_index = */ generic_space_build_index,
	/* .swap_index = */ generic_space_swap_index,
	/* .prepare_alter = */ generic_space_prepare_alter,
	/* .invalidate = */ generic_space_invalidate,
};

static void
sysview_engine_shutdown(struct engine *engine)
{
	struct sysview_engine *sysview = (struct sysview_engine *)engine;
	if (mempool_is_initialized(&sysview->iterator_pool))
		mempool_destroy(&sysview->iterator_pool);
	free(engine);
}

static struct space *
sysview_engine_create_space(struct engine *engine, struct space_def *def,
			    struct rlist *key_list)
{
	struct space *space = (struct space *)calloc(1, sizeof(*space));
	if (space == NULL) {
		diag_set(OutOfMemory, sizeof(*space),
			 "malloc", "struct space");
		return NULL;
	}
	int key_count = 0;
	/*
	 * Despite the fact that space with sysview engine
	 * actually doesn't own tuples, setup of format will be
	 * useful in order to unify it with SQL views and to use
	 * same machinery to do selects from such views from Lua
	 * land.
	 */
	struct key_def **keys = index_def_to_key_def(key_list, &key_count);
	if (keys == NULL) {
		free(space);
		return NULL;
	}
	struct tuple_format *format =
		tuple_format_new(NULL, NULL, keys, key_count, def->fields,
				 def->field_count, def->exact_field_count,
				 def->dict, def->opts.is_temporary,
				 def->opts.is_ephemeral);
	if (format == NULL) {
		free(space);
		return NULL;
	}
	tuple_format_ref(format);
	if (space_create(space, engine, &sysview_space_vtab,
			 def, key_list, format) != 0) {
		free(space);
		return NULL;
	}
	/* Format is now referenced by the space. */
	tuple_format_unref(format);
	return space;
}

static const struct engine_vtab sysview_engine_vtab = {
	/* .shutdown = */ sysview_engine_shutdown,
	/* .create_space = */ sysview_engine_create_space,
	/* .prepare_join = */ generic_engine_prepare_join,
	/* .join = */ generic_engine_join,
	/* .complete_join = */ generic_engine_complete_join,
	/* .begin = */ generic_engine_begin,
	/* .begin_statement = */ generic_engine_begin_statement,
	/* .prepare = */ generic_engine_prepare,
	/* .commit = */ generic_engine_commit,
	/* .rollback_statement = */ generic_engine_rollback_statement,
	/* .rollback = */ generic_engine_rollback,
	/* .switch_to_ro = */ generic_engine_switch_to_ro,
	/* .bootstrap = */ generic_engine_bootstrap,
	/* .begin_initial_recovery = */ generic_engine_begin_initial_recovery,
	/* .begin_final_recovery = */ generic_engine_begin_final_recovery,
	/* .begin_hot_standby = */ generic_engine_begin_hot_standby,
	/* .end_recovery = */ generic_engine_end_recovery,
	/* .begin_checkpoint = */ generic_engine_begin_checkpoint,
	/* .wait_checkpoint = */ generic_engine_wait_checkpoint,
	/* .commit_checkpoint = */ generic_engine_commit_checkpoint,
	/* .abort_checkpoint = */ generic_engine_abort_checkpoint,
	/* .collect_garbage = */ generic_engine_collect_garbage,
	/* .backup = */ generic_engine_backup,
	/* .memory_stat = */ generic_engine_memory_stat,
	/* .reset_stat = */ generic_engine_reset_stat,
	/* .check_space_def = */ generic_engine_check_space_def,
};

struct sysview_engine *
sysview_engine_new(void)
{
	struct sysview_engine *sysview = calloc(1, sizeof(*sysview));
	if (sysview == NULL) {
		diag_set(OutOfMemory, sizeof(*sysview),
			 "malloc", "struct sysview_engine");
		return NULL;
	}

	sysview->base.vtab = &sysview_engine_vtab;
	sysview->base.name = "sysview";
	sysview->base.flags = ENGINE_BYPASS_TX;
	return sysview;
}
