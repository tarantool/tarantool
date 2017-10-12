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
#include "schema.h"
#include "space.h"
#include "func.h"
#include "tuple.h"
#include "session.h"

struct sysview_iterator {
	struct iterator base;
	struct iterator *source;
	struct space *space;
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
	if (it->source != NULL) {
		it->source->free(it->source);
	}
	free(it);
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
	while ((rc = it->source->next(it->source, ret)) == 0 && *ret != NULL) {
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
sysview_index_bsize(struct index *)
{
	return 0;
}

static struct iterator *
sysview_index_alloc_iterator(void)
{
	struct sysview_iterator *it = (struct sysview_iterator *)
			calloc(1, sizeof(*it));
	if (it == NULL) {
		diag_set(OutOfMemory, sizeof(struct sysview_iterator),
			 "malloc", "struct sysview_iterator");
		return NULL;
	}
	it->base.free = sysview_iterator_free;
	return (struct iterator *) it;
}

static int
sysview_index_init_iterator(struct index *base, struct iterator *iterator,
			    enum iterator_type type,
			    const char *key, uint32_t part_count)
{
	assert(iterator->free == sysview_iterator_free);
	struct sysview_index *index = (struct sysview_index *)base;
	struct sysview_iterator *it = sysview_iterator(iterator);
	struct space *source = space_cache_find(index->source_space_id);
	if (source == NULL)
		return -1;
	struct index *pk = index_find(source, index->source_index_id);
	if (pk == NULL)
		return -1;
	/*
	 * Explicitly validate that key matches source's index_def.
	 * It is possible to change a source space without changing
	 * the view.
	 */
	if (key_validate(pk->def, type, key, part_count))
		return -1;
	/* Re-allocate iterator if schema was changed */
	if (it->source != NULL &&
	    it->source->schema_version != schema_version) {
		it->source->free(it->source);
		it->source = NULL;
	}
	if (it->source == NULL) {
		it->source = index_alloc_iterator(pk);
		if (it->source == NULL)
			return -1;
		it->source->schema_version = schema_version;
	}
	if (index_init_iterator(pk, it->source, type, key, part_count) != 0)
		return -1;
	iterator->index = (struct index *) index;
	iterator->next = sysview_iterator_next;
	it->space = source;
	return 0;
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
	/* .commit_drop = */ generic_index_commit_drop,
	/* .size = */ generic_index_size,
	/* .bsize = */ sysview_index_bsize,
	/* .min = */ generic_index_min,
	/* .max = */ generic_index_max,
	/* .random = */ generic_index_random,
	/* .count = */ generic_index_count,
	/* .get = */ sysview_index_get,
	/* .replace = */ generic_index_replace,
	/* .alloc_iterator = */ sysview_index_alloc_iterator,
	/* .init_iterator = */ sysview_index_init_iterator,
	/* .create_snapshot_iterator = */
		generic_index_create_snapshot_iterator,
	/* .info = */ generic_index_info,
	/* .begin_build = */ generic_index_begin_build,
	/* .reserve = */ generic_index_reserve,
	/* .build_next = */ generic_index_build_next,
	/* .end_build = */ generic_index_end_build,
};

static bool
vspace_filter(struct space *source, struct tuple *tuple)
{
	struct credentials *cr = current_user();
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
	struct credentials *cr = current_user();
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
	struct credentials *cr = current_user();
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
	struct credentials *cr = current_user();
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
sysview_index_new(struct index_def *def, const char *space_name)
{
	assert(def->type == TREE);

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
	if (index_create(&index->base, &sysview_index_vtab, def) != 0) {
		free(index);
		return NULL;
	}

	index->source_space_id = source_space_id;
	index->source_index_id = source_index_id;
	index->filter = filter;
	return index;
}
