/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
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
#include "session_settings.h"
#include "xrow_update.h"
#include "service_engine.h"
#include "column_mask.h"
#include "session.h"
#include "schema.h"
#include "tuple.h"
#include "xrow.h"
#include "sql.h"

struct session_setting session_settings[SESSION_SETTING_COUNT] = {};

/** Corresponding names of session settings. */
const char *session_setting_strs[SESSION_SETTING_COUNT] = {
	"error_marshaling_enabled",
	"sql_default_engine",
	"sql_defer_foreign_keys",
	"sql_full_column_names",
	"sql_full_metadata",
	"sql_parser_debug",
	"sql_recursive_triggers",
	"sql_reverse_unordered_selects",
	"sql_select_debug",
	"sql_vdbe_debug",
};

struct session_settings_index {
	/** Base index. Must be the first member. */
	struct index base;
	/**
	 * Format of the tuples iterators of this index return. It
	 * is stored here so as not to lookup space each time to
	 * get a format and create an iterator.
	 */
	struct tuple_format *format;
};

struct session_settings_iterator {
	/** Base iterator. Must be the first member. */
	struct iterator base;
	/**
	 * Format of the tuples this iterator returns. It is
	 * stored here so as not to lookup space each time to get
	 * a format for selected tuples.
	 */
	struct tuple_format *format;
	/** ID of the setting. */
	int setting_id;
	/** Decoded key. */
	char *key;
	/** True if the iterator returns only equal keys. */
	bool is_eq;
	/** True if the iterator should include equal keys. */
	bool is_including;
};

static void
session_settings_iterator_free(struct iterator *ptr)
{
	struct session_settings_iterator *it =
		(struct session_settings_iterator *)ptr;
	free(it->key);
	free(it);
}

static int
session_settings_next(int *sid, const char *key, bool is_eq, bool is_including)
{
	int i = *sid;
	if (i >= SESSION_SETTING_COUNT)
		return -1;
	if (key == NULL)
		return 0;
	assert(i >= 0);
	for (; i < SESSION_SETTING_COUNT; ++i) {
		const char *name = session_setting_strs[i];
		int cmp = strcmp(name, key);
		if ((cmp == 0 && is_including) ||
		    (cmp > 0 && !is_eq)) {
			*sid = i;
			return 0;
		}
	}
	*sid = SESSION_SETTING_COUNT;
	return -1;
}

static int
session_settings_prev(int *sid, const char *key, bool is_eq, bool is_including)
{
	int i = *sid;
	if (i < 0)
		return -1;
	if (key == NULL)
		return 0;
	if (i >= SESSION_SETTING_COUNT)
		i = SESSION_SETTING_COUNT - 1;
	for (; i >= 0; --i) {
		const char *name = session_setting_strs[i];
		int cmp = strcmp(name, key);
		if ((cmp == 0 && is_including) ||
		    (cmp < 0 && !is_eq)) {
			*sid = i;
			return 0;
		}
	}
	*sid = -1;
	return -1;
}

static int
session_settings_iterator_next(struct iterator *iterator, struct tuple **result)
{
	struct session_settings_iterator *it =
		(struct session_settings_iterator *)iterator;
	int sid = it->setting_id;
	const char *key = it->key;
	bool is_including = it->is_including, is_eq = it->is_eq;
	bool is_found = false;
	if (session_settings_next(&sid, key, is_eq, is_including) == 0)
		is_found = true;
	it->setting_id = sid + 1;
	if (!is_found) {
		*result = NULL;
		return 0;
	}
	const char *mp_pair, *mp_pair_end;
	session_settings[sid].get(sid, &mp_pair, &mp_pair_end);
	*result = box_tuple_new(it->format, mp_pair, mp_pair_end);
	return *result != NULL ? 0 : -1;
}

static int
session_settings_iterator_prev(struct iterator *iterator, struct tuple **result)
{
	struct session_settings_iterator *it =
		(struct session_settings_iterator *)iterator;
	int sid = it->setting_id;
	const char *key = it->key;
	bool is_including = it->is_including, is_eq = it->is_eq;
	bool is_found = false;
	if (session_settings_prev(&sid, key, is_eq, is_including) == 0)
		is_found = true;
	it->setting_id = sid - 1;
	if (!is_found) {
		*result = NULL;
		return 0;
	}
	const char *mp_pair, *mp_pair_end;
	session_settings[sid].get(sid, &mp_pair, &mp_pair_end);
	*result = box_tuple_new(it->format, mp_pair, mp_pair_end);
	return *result != NULL ? 0 : -1;
}

static void
session_settings_index_destroy(struct index *index)
{
	free(index);
}

static struct iterator *
session_settings_index_create_iterator(struct index *base,
				       enum iterator_type type, const char *key,
				       uint32_t part_count)
{
	struct session_settings_index *index =
		(struct session_settings_index *)base;
	char *decoded_key = NULL;
	if (part_count > 0) {
		assert(part_count == 1);
		assert(mp_typeof(*key) == MP_STR);
		uint32_t len;
		const char *name = mp_decode_str(&key, &len);
		decoded_key = (char *)malloc(len + 1);
		if (decoded_key == NULL) {
			diag_set(OutOfMemory, len + 1, "malloc", "decoded_key");
			return NULL;
		}
		memcpy(decoded_key, name, len);
		decoded_key[len] = '\0';
	}
	struct session_settings_iterator *it =
		(struct session_settings_iterator *)malloc(sizeof(*it));
	if (it == NULL) {
		diag_set(OutOfMemory, sizeof(*it), "malloc", "it");
		free(decoded_key);
		return NULL;
	}
	iterator_create(&it->base, base);
	it->base.free = session_settings_iterator_free;
	it->key = decoded_key;
	it->is_eq = type == ITER_EQ || type == ITER_REQ;
	it->is_including = it->is_eq || type == ITER_GE || type == ITER_ALL ||
			   type == ITER_LE;
	it->format = index->format;
	if (!iterator_type_is_reverse(type)) {
		it->base.next = session_settings_iterator_next;
		it->setting_id = 0;
	} else {
		it->base.next = session_settings_iterator_prev;
		it->setting_id = SESSION_SETTING_COUNT - 1;
	}
	return (struct iterator *)it;
}

static int
session_settings_index_get(struct index *base, const char *key,
			   uint32_t part_count, struct tuple **result)
{
	struct session_settings_index *index =
		(struct session_settings_index *) base;
	assert(part_count == 1);
	(void) part_count;
	uint32_t len;
	key = mp_decode_str(&key, &len);
	key = tt_cstr(key, len);
	int sid = session_setting_find(key);
	if (sid < 0) {
		*result = NULL;
		return 0;
	}
	const char *mp_pair;
	const char *mp_pair_end;
	session_settings[sid].get(sid, &mp_pair, &mp_pair_end);
	*result = box_tuple_new(index->format, mp_pair, mp_pair_end);
	return *result != NULL ? 0 : -1;
}

static const struct index_vtab session_settings_index_vtab = {
	/* .destroy = */ session_settings_index_destroy,
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
	/* .get = */ session_settings_index_get,
	/* .replace = */ generic_index_replace,
	/* .create_iterator = */ session_settings_index_create_iterator,
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
session_settings_space_destroy(struct space *space)
{
	free(space);
}

static int
session_settings_space_execute_replace(struct space *space, struct txn *txn,
				       struct request *request,
				       struct tuple **result)
{
	(void)space;
	(void)txn;
	(void)request;
	(void)result;
	diag_set(ClientError, ER_UNSUPPORTED, "_session_settings space",
		 "replace()");
	return -1;
}

static int
session_settings_space_execute_delete(struct space *space, struct txn *txn,
				      struct request *request,
				      struct tuple **result)
{
	(void)space;
	(void)txn;
	(void)request;
	(void)result;
	diag_set(ClientError, ER_UNSUPPORTED, "_session_settings space",
		 "delete()");
	return -1;
}

static int
session_settings_space_execute_update(struct space *space, struct txn *txn,
				      struct request *request,
				      struct tuple **result)
{
	(void)txn;
	struct tuple_format *format = space->format;
	const char *old_data, *old_data_end, *new_data;
	struct region *region = &fiber()->gc;
	size_t used = region_used(region);
	int rc = -1, sid = 0;
	struct index_def *pk_def = space->index[0]->def;
	uint64_t column_mask;

	const char *new_key, *key = request->key;
	uint32_t new_size, new_key_len, key_len = mp_decode_array(&key);
	if (key_len == 0) {
		diag_set(ClientError, ER_EXACT_MATCH, 1, 0);
		return -1;
	}
	if (key_len > 1 || mp_typeof(*key) != MP_STR) {
		diag_set(ClientError, ER_KEY_PART_TYPE, 0, "string");
		return -1;
	}
	key = mp_decode_str(&key, &key_len);
	key = tt_cstr(key, key_len);
	sid = session_setting_find(key);
	if (sid < 0) {
		*result = NULL;
		return 0;
	}
	session_settings[sid].get(sid, &old_data, &old_data_end);
	new_data = xrow_update_execute(request->tuple, request->tuple_end,
				       old_data, old_data_end, format,
				       &new_size, request->index_base,
				       &column_mask);
	if (new_data == NULL)
		goto finish;
	*result = box_tuple_new(format, new_data, new_data + new_size);
	if (*result == NULL)
		goto finish;

	mp_decode_array(&new_data);
	new_key = mp_decode_str(&new_data, &new_key_len);
	if (!key_update_can_be_skipped(pk_def->key_def->column_mask,
				       column_mask)) {
		if (key_len != new_key_len ||
		    memcmp(key, new_key, key_len) != 0) {
			diag_set(ClientError, ER_CANT_UPDATE_PRIMARY_KEY,
				 pk_def->name, space_name(space));
			goto finish;
		}
	}
	if (session_settings[sid].set(sid, new_data) != 0)
		goto finish;
	rc = 0;
finish:
	region_truncate(region, used);
	return rc;
}

static int
session_settings_space_execute_upsert(struct space *space, struct txn *txn,
				      struct request *request)
{
	(void)space;
	(void)txn;
	(void)request;
	diag_set(ClientError, ER_UNSUPPORTED, "_session_settings space",
		 "upsert()");
	return -1;
}

static struct index *
session_settings_space_create_index(struct space *space, struct index_def *def)
{
	assert(space->def->id == BOX_SESSION_SETTINGS_ID);
	if (def->iid != 0) {
		diag_set(ClientError, ER_UNSUPPORTED, "_session_settings space",
			 "create_index()");
		return NULL;
	}

	struct session_settings_index *index =
		(struct session_settings_index *)calloc(1, sizeof(*index));
	if (index == NULL) {
		diag_set(OutOfMemory, sizeof(*index), "calloc", "index");
		return NULL;
	}
	if (index_create(&index->base, space->engine,
			 &session_settings_index_vtab, def) != 0) {
		free(index);
		return NULL;
	}

	index->format = space->format;
	return &index->base;
}

const struct space_vtab session_settings_space_vtab = {
	/* .destroy = */ session_settings_space_destroy,
	/* .bsize = */ generic_space_bsize,
	/* .execute_replace = */ session_settings_space_execute_replace,
	/* .execute_delete = */ session_settings_space_execute_delete,
	/* .execute_update = */ session_settings_space_execute_update,
	/* .execute_upsert = */ session_settings_space_execute_upsert,
	/* .ephemeral_replace = */ generic_space_ephemeral_replace,
	/* .ephemeral_delete = */ generic_space_ephemeral_delete,
	/* .ephemeral_rowid_next = */ generic_space_ephemeral_rowid_next,
	/* .init_system_space = */ generic_init_system_space,
	/* .init_ephemeral_space = */ generic_init_ephemeral_space,
	/* .check_index_def = */ generic_space_check_index_def,
	/* .create_index = */ session_settings_space_create_index,
	/* .add_primary_key = */ generic_space_add_primary_key,
	/* .drop_primary_key = */ generic_space_drop_primary_key,
	/* .check_format = */ generic_space_check_format,
	/* .build_index = */ generic_space_build_index,
	/* .swap_index = */ generic_space_swap_index,
	/* .prepare_alter = */ generic_space_prepare_alter,
	/* .invalidate = */ generic_space_invalidate,
};

int
session_setting_find(const char *name) {
	int sid = 0;
	if (session_settings_next(&sid, name, true, true) == 0)
		return sid;
	else
		return -1;
}

/* Module independent session settings. */

static void
session_setting_error_marshaling_enabled_get(int id, const char **mp_pair,
					     const char **mp_pair_end)
{
	assert(id == SESSION_SETTING_ERROR_MARSHALING_ENABLED);
	struct session *session = current_session();
	const char *name = session_setting_strs[id];
	size_t name_len = strlen(name);
	bool value = session->meta.serializer_opts.error_marshaling_enabled;
	size_t size = mp_sizeof_array(2) + mp_sizeof_str(name_len) +
		      mp_sizeof_bool(value);

	char *pos = (char*)static_alloc(size);
	assert(pos != NULL);
	char *pos_end = mp_encode_array(pos, 2);
	pos_end = mp_encode_str(pos_end, name, name_len);
	pos_end = mp_encode_bool(pos_end, value);
	*mp_pair = pos;
	*mp_pair_end = pos_end;
}

static int
session_setting_error_marshaling_enabled_set(int id, const char *mp_value)
{
	assert(id == SESSION_SETTING_ERROR_MARSHALING_ENABLED);
	enum mp_type mtype = mp_typeof(*mp_value);
	enum field_type stype = session_settings[id].field_type;
	if (mtype != MP_BOOL) {
		diag_set(ClientError, ER_SESSION_SETTING_INVALID_VALUE,
			 session_setting_strs[id], field_type_strs[stype]);
		return -1;
	}
	struct session *session = current_session();
	session->meta.serializer_opts.error_marshaling_enabled =
		mp_decode_bool(&mp_value);
	return 0;
}

extern void
sql_session_settings_init();

void
session_settings_init(void)
{
	struct session_setting *s =
		&session_settings[SESSION_SETTING_ERROR_MARSHALING_ENABLED];
	s->field_type = FIELD_TYPE_BOOLEAN;
	s->get = session_setting_error_marshaling_enabled_get;
	s->set = session_setting_error_marshaling_enabled_set;

	sql_session_settings_init();
}
