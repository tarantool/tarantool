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
#include "sysview_index.h"
#include "sysview_engine.h"
#include <small/mempool.h>
#include "fiber.h"
#include "schema.h"
#include "space.h"
#include "func.h"
#include "tuple.h"
#include "session.h"

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
	if (it->source->schema_version != schema_version)
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

static ssize_t
sysview_index_bsize(struct index *index)
{
	(void)index;
	return 0;
}

static bool
sysview_index_def_change_requires_rebuild(struct index *index,
					  const struct index_def *new_def)
{
	(void)index;
	(void)new_def;
	return true;
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
		sysview_index_def_change_requires_rebuild,
	/* .size = */ generic_index_size,
	/* .bsize = */ sysview_index_bsize,
	/* .min = */ generic_index_min,
	/* .max = */ generic_index_max,
	/* .random = */ generic_index_random,
	/* .count = */ generic_index_count,
	/* .get = */ sysview_index_get,
	/* .replace = */ generic_index_replace,
	/* .create_iterator = */ sysview_index_create_iterator,
	/* .create_snapshot_iterator = */
		generic_index_create_snapshot_iterator,
	/* .info = */ generic_index_info,
	/* .reset_stat = */ generic_index_reset_stat,
	/* .begin_build = */ generic_index_begin_build,
	/* .reserve = */ generic_index_reserve,
	/* .build_next = */ generic_index_build_next,
	/* .end_build = */ generic_index_end_build,
};

static bool
vspace_filter(struct space *source, struct tuple *tuple)
{
	struct credentials *cr = effective_user();
	if (PRIV_R & cr->universal_access)
		return true; /* read access to unverse */
	if (PRIV_R & source->access[cr->auth_token].effective)
		return true; /* read access to original space */

	uint32_t space_id;
	if (tuple_field_u32(tuple, BOX_SPACE_FIELD_ID, &space_id) != 0)
		return false;
	struct space *space = space_cache_find(space_id);
	if (space == NULL)
		return false;
	uint8_t effective = space->access[cr->auth_token].effective;
	return ((PRIV_R | PRIV_W) & (cr->universal_access | effective) ||
		space->def->uid == cr->uid);
}

static bool
vuser_filter(struct space *source, struct tuple *tuple)
{
	struct credentials *cr = effective_user();
	if (PRIV_R & cr->universal_access)
		return true; /* read access to unverse */
	if (PRIV_R & source->access[cr->auth_token].effective)
		return true; /* read access to original space */

	uint32_t uid;
	if (tuple_field_u32(tuple, BOX_USER_FIELD_ID, &uid) != 0)
		return false;
	uint32_t owner_id;
	if (tuple_field_u32(tuple, BOX_USER_FIELD_UID, &owner_id) != 0)
		return false;
	return uid == cr->uid || owner_id == cr->uid;
}

static bool
vpriv_filter(struct space *source, struct tuple *tuple)
{
	struct credentials *cr = effective_user();
	if (PRIV_R & cr->universal_access)
		return true; /* read access to unverse */
	if (PRIV_R & source->access[cr->auth_token].effective)
		return true; /* read access to original space */

	uint32_t grantor_id;
	if (tuple_field_u32(tuple, BOX_PRIV_FIELD_ID, &grantor_id) != 0)
		return false;
	uint32_t grantee_id;
	if (tuple_field_u32(tuple, BOX_PRIV_FIELD_UID, &grantee_id) != 0)
		return false;
	return grantor_id == cr->uid || grantee_id == cr->uid;
}

static bool
vfunc_filter(struct space *source, struct tuple *tuple)
{
	struct credentials *cr = effective_user();
	if ((PRIV_R | PRIV_X) & cr->universal_access)
		return true; /* read or execute access to unverse */
	if (PRIV_R & source->access[cr->auth_token].effective)
		return true; /* read access to original space */

	const char *name = tuple_field_cstr(tuple, BOX_FUNC_FIELD_NAME);
	if (name == NULL)
		return false;
	uint32_t name_len = strlen(name);
	struct func *func = func_by_name(name, name_len);
	assert(func != NULL);
	uint8_t effective = func->access[cr->auth_token].effective;
	if (func->def->uid == cr->uid || (PRIV_X & effective))
		return true;
	return false;
}

struct sysview_index *
sysview_index_new(struct sysview_engine *sysview,
		  struct index_def *def, const char *space_name)
{
	assert(def->type == TREE);

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
	default:
		diag_set(ClientError, ER_MODIFY_INDEX,
			 def->name, space_name,
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
	return index;
}
