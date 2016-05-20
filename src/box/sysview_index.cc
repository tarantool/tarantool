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

static struct tuple *
sysview_iterator_next(struct iterator *iterator)
{
	assert(iterator->free == sysview_iterator_free);
	struct sysview_iterator *it = sysview_iterator(iterator);
	if (it->source->sc_version != sc_version)
		return NULL; /* invalidate iterator */
	class SysviewIndex *index = (class SysviewIndex *) iterator->index;
	struct tuple *tuple;
	while ((tuple = it->source->next(it->source)) != NULL) {
		if (index->filter(it->space, tuple))
			return tuple;
	}
	return NULL;
}

SysviewIndex::SysviewIndex(struct key_def *key_def, uint32_t source_space_id,
		     uint32_t source_index_id, sysview_filter_f filter)
	: Index(key_def), source_space_id(source_space_id),
	  source_index_id(source_index_id), filter(filter)
{
}

SysviewIndex::~SysviewIndex()
{
}

struct iterator *
SysviewIndex::allocIterator() const
{
	struct sysview_iterator *it = (struct sysview_iterator *)
			calloc(1, sizeof(*it));
	if (it == NULL) {
		tnt_raise(OutOfMemory, sizeof(struct sysview_iterator),
			  "SysviewIndex", "iterator");
	}
	it->base.free = sysview_iterator_free;
	return (struct iterator *) it;
}

void
SysviewIndex::initIterator(struct iterator *iterator,
			   enum iterator_type type,
			   const char *key, uint32_t part_count) const
{
	assert(iterator->free == sysview_iterator_free);
	struct sysview_iterator *it = sysview_iterator(iterator);
	struct space *source = space_cache_find(source_space_id);
	class Index *pk = index_find(source, source_index_id);
	/*
	 * Explicitly validate that key matches source's key_def.
	 * It is possible to change a source space without changing
	 * the view.
	 */
	key_validate(pk->key_def, type, key, part_count);
	/* Re-allocate iterator if schema was changed */
	if (it->source != NULL && it->source->sc_version != ::sc_version) {
		it->source->free(it->source);
		it->source = NULL;
	}
	if (it->source == NULL) {
		it->source = pk->allocIterator();
		it->source->sc_version = ::sc_version;
	}
	pk->initIterator(it->source, type, key, part_count);
	iterator->index = (Index *) this;
	iterator->next = sysview_iterator_next;
	it->space = source;
}

struct tuple *
SysviewIndex::findByKey(const char *key, uint32_t part_count) const
{
	struct space *source = space_cache_find(source_space_id);
	class Index *pk = index_find(source, source_index_id);
	if (!pk->key_def->opts.is_unique)
		tnt_raise(ClientError, ER_MORE_THAN_ONE_TUPLE);
	primary_key_validate(pk->key_def, key, part_count);
	struct tuple *tuple = pk->findByKey(key, part_count);
	if (tuple == NULL || !filter(source, tuple))
		return NULL;
	return tuple;
}

static bool
vspace_filter(struct space *source, struct tuple *tuple)
{
	struct credentials *cr = current_user();
	if (PRIV_R & cr->universal_access)
		return true; /* read access to unverse */
	if (PRIV_R & source->access[cr->auth_token].effective)
		return true; /* read access to original space */

	uint32_t space_id = tuple_field_u32(tuple, 0);
	struct space *space = space_cache_find(space_id);
	uint8_t effective = space->access[cr->auth_token].effective;
	return ((PRIV_R | PRIV_W) & (cr->universal_access | effective) ||
		space->def.uid == cr->uid);
}

SysviewVspaceIndex::SysviewVspaceIndex(struct key_def *key_def)
	: SysviewIndex(key_def, BOX_SPACE_ID, key_def->iid, vspace_filter)
{
}

SysviewVindexIndex::SysviewVindexIndex(struct key_def *key_def)
	: SysviewIndex(key_def, BOX_INDEX_ID, key_def->iid, vspace_filter)
{
}

static bool
vuser_filter(struct space *source, struct tuple *tuple)
{
	struct credentials *cr = current_user();
	if (PRIV_R & cr->universal_access)
		return true; /* read access to unverse */
	if (PRIV_R & source->access[cr->auth_token].effective)
		return true; /* read access to original space */

	uint32_t uid = tuple_field_u32(tuple, 0);
	uint32_t owner_id = tuple_field_u32(tuple, 1);
	return uid == cr->uid || owner_id == cr->uid;
}

SysviewVuserIndex::SysviewVuserIndex(struct key_def *key_def)
	: SysviewIndex(key_def, BOX_USER_ID, key_def->iid, vuser_filter)
{
}

static bool
vpriv_filter(struct space *source, struct tuple *tuple)
{
	struct credentials *cr = current_user();
	if (PRIV_R & cr->universal_access)
		return true; /* read access to unverse */
	if (PRIV_R & source->access[cr->auth_token].effective)
		return true; /* read access to original space */

	uint32_t grantor_id = tuple_field_u32(tuple, 0);
	uint32_t grantee_id = tuple_field_u32(tuple, 1);
	return grantor_id == cr->uid || grantee_id == cr->uid;
}

SysviewVprivIndex::SysviewVprivIndex(struct key_def *key_def)
	: SysviewIndex(key_def, BOX_PRIV_ID, key_def->iid, vpriv_filter)
{
}

static bool
vfunc_filter(struct space *source, struct tuple *tuple)
{
	struct credentials *cr = current_user();
	if ((PRIV_R | PRIV_X) & cr->universal_access)
		return true; /* read or execute access to unverse */
	if (PRIV_R & source->access[cr->auth_token].effective)
		return true; /* read access to original space */

	const char *name = tuple_field_cstr(tuple, 2);
	uint32_t name_len = strlen(name);
	struct func *func = func_by_name(name, name_len);
	assert(func != NULL);
	uint8_t effective = func->access[cr->auth_token].effective;
	if (func->def.uid == cr->uid || (PRIV_X & effective))
		return true;
	return false;
}

SysviewVfuncIndex::SysviewVfuncIndex(struct key_def *key_def)
	: SysviewIndex(key_def, BOX_FUNC_ID, key_def->iid, vfunc_filter)
{
}
