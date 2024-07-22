/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include <assert.h>
#include "field_def.h"
#include "cfg.h"
#include "sql.h"
#include "sql/sqlInt.h"
#include "sql/tarantoolInt.h"
#include "sql/mem.h"
#include "sql/vdbeInt.h"

#include "index.h"
#include "info/info.h"
#include "schema.h"
#include "box.h"
#include "txn.h"
#include "space.h"
#include "memtx_space.h"
#include "space_def.h"
#include "index_def.h"
#include "tuple.h"
#include "fiber.h"
#include "small/region.h"
#include "session.h"
#include "xrow.h"
#include "iproto_constants.h"
#include "mpstream/mpstream.h"
#include "sql_stmt_cache.h"
#include "box/tuple_constraint_def.h"
#include "mp_util.h"
#include "tweaks.h"
#include "coll_id_cache.h"

static sql *db = NULL;

static const char nil_key[] = { 0x90 }; /* Empty MsgPack array. */

static bool sql_seq_scan_default = false;
TWEAK_BOOL(sql_seq_scan_default);

uint32_t
sql_default_session_flags(void)
{
	if (sql_seq_scan_default)
		return SQL_DEFAULT_FLAGS | SQL_SeqScan;
	return SQL_DEFAULT_FLAGS & ~SQL_SeqScan;
}

void
sql_init(void)
{
	current_session()->sql_flags = sql_default_session_flags();

	if (sql_init_db(&db) != 0)
		panic("failed to initialize SQL subsystem");

	sql_stmt_cache_init();
	sql_built_in_functions_cache_init();

	assert(db != NULL);
}

sql *
sql_get(void)
{
	return db;
}

/*********************************************************************
 * sql cursor implementation on top of Tarantool storage API-s.
 *
 * NB: sql btree cursor emulation is less than perfect. The problem
 * is that btree cursors are more low-level compared to Tarantool
 * iterators. The 2 most drastic differences being:
 *
 * i. Positioning - sqlBtreeMovetoUnpacked(key) moves to a leaf
 *                  entry that is "reasonably close" to the requested
 *                  key. The result from the last comparator invocation
 *                  is returned to caller, so she can Prev/Next to
 *                  adjust the position if needed. Ex:
 *
 *                  SQL: "... WHERE v>42",
 *                  Data: [40,45]
 *                  The engine does M2U(42), ending up with the cursor
 *                  @40. The caller learns that the current item under
 *                  cursor is less than 42, and advances the cursor
 *                  ending up @45.
 *
 *                  Another complication is due to equal keys (sometimes
 *                  a lookup is done with a key prefix which may equal
 *                  multiple keys even in a unique index). Depending on
 *                  the configuration stored in UnpackedRecord either
 *                  the first or the last key in a run of equal keys is
 *                  selected.
 *
 * ii. Direction  - sql cursors are bidirectional while Tarantool
 *                  iterators are not.
 *
 * Fortunately, cursor semantics defined by VDBE matches Tarantool's one
 * well. Ex: a cursor positioned with Seek_GE can only move forward.
 *
 * We extended UnpackedRecord (UR) to include current running opcode
 * number. In M2U we request the matching Tarantool iterator type and
 * ignore detailed config in UR which we can't implement anyway. We are
 * lacking last comparator result so we make up one. The value is
 * innacurate: for instance for Seek_GE we return 0 (equal item) if
 * iterator will produce any items. If the first item is greater than
 * the key, +1 would be more apropriate. However, the value is only used
 * in VDBE interpretor to invoke Next when the current item is less than
 * the search key (-1), which is unnecessary since Tarantool iterators
 * are accurately positioned, hence both 0 and 1 are fine.
 */

int
key_alloc(BtCursor *c, size_t key_size);

static int
cursor_seek(BtCursor *pCur, int *pRes);

static int
cursor_advance(BtCursor *pCur, int *pRes);

const void *tarantoolsqlPayloadFetch(BtCursor *pCur, u32 *pAmt)
{
	assert(pCur->curFlags & BTCF_TaCursor ||
	       pCur->curFlags & BTCF_TEphemCursor);
	assert(pCur->last_tuple != NULL);

	*pAmt = box_tuple_bsize(pCur->last_tuple);
	return tuple_data(pCur->last_tuple);
}

/*
 * Set cursor to the first tuple in given space.
 * It is a simple wrapper around cursor_seek().
 */
int tarantoolsqlFirst(BtCursor *pCur, int *pRes)
{
	if (key_alloc(pCur, sizeof(nil_key)) != 0)
		return -1;
	memcpy(pCur->key, nil_key, sizeof(nil_key));
	pCur->iter_type = ITER_GE;
	return cursor_seek(pCur, pRes);
}

/* Set cursor to the last tuple in given space. */
int tarantoolsqlLast(BtCursor *pCur, int *pRes)
{
	if (key_alloc(pCur, sizeof(nil_key)) != 0)
		return -1;
	memcpy(pCur->key, nil_key, sizeof(nil_key));
	pCur->iter_type = ITER_LE;
	return cursor_seek(pCur, pRes);
}

/*
 * Set cursor to the next entry in given space.
 * If state of cursor is invalid (e.g. it is still under construction,
 * or already destroyed), it immediately returns.
 * Second argument is output parameter: success movement of cursor
 * results in 0 value of pRes, otherwise it is set to 1.
 */
int tarantoolsqlNext(BtCursor *pCur, int *pRes)
{
	if (pCur->eState == CURSOR_INVALID) {
		*pRes = 1;
		return 0;
	}
	assert(iterator_direction(pCur->iter_type) > 0);
	return cursor_advance(pCur, pRes);
}

/*
 * Set cursor to the previous entry in ephemeral space.
 * If state of cursor is invalid (e.g. it is still under construction,
 * or already destroyed), it immediately returns.
 */
int tarantoolsqlPrevious(BtCursor *pCur, int *pRes)
{
	if (pCur->eState == CURSOR_INVALID) {
		*pRes = 1;
		return 0;
	}
	assert(iterator_direction(pCur->iter_type) < 0);
	return cursor_advance(pCur, pRes);
}

int
sql_cursor_seek(struct BtCursor *cur, struct Mem *mems, uint32_t len, int *res)
{
	struct region *region = &fiber()->gc;
	size_t used = region_used(region);
	uint32_t size;
	const char *tuple = mem_encode_array(mems, len, &size, region);
	if (tuple == NULL)
		return -1;
	if (key_alloc(cur, size) != 0)
		return -1;
	memcpy(cur->key, tuple, size);
	region_truncate(region, used);
	return cursor_seek(cur, res);
}

/*
 * Count number of tuples in ephemeral space.
 *
 * @param pCur Cursor which will point to ephemeral space.
 *
 * @retval Number of tuples in ephemeral space.
 */
int64_t
tarantoolsqlEphemeralCount(struct BtCursor *pCur)
{
	assert(pCur->curFlags & BTCF_TEphemCursor);
	struct index *primary_index = space_index(pCur->space, 0 /* PK */);
	return index_count(primary_index, pCur->iter_type, NULL, 0);
}

int64_t
tarantoolsqlCount(struct BtCursor *pCur)
{
	assert(pCur->curFlags & BTCF_TaCursor);
	return index_count(pCur->index, pCur->iter_type, NULL, 0);
}

struct sql_space_info *
sql_space_info_new(uint32_t field_count, uint32_t part_count)
{
	assert(field_count > 0);
	uint32_t info_size = sizeof(struct sql_space_info);
	uint32_t field_size = field_count * sizeof(enum field_type);
	uint32_t colls_size = field_count * sizeof(uint32_t);
	uint32_t parts_size = part_count * sizeof(uint32_t);
	uint32_t sort_orders_size = part_count * sizeof(enum sort_order);
	uint32_t size = info_size + field_size + colls_size + parts_size +
			sort_orders_size;

	struct sql_space_info *info = sql_xmalloc(size);
	info->types = (enum field_type *)((char *)info + info_size);
	info->coll_ids = (uint32_t *)((char *)info->types + field_size);
	info->parts = part_count == 0 ? NULL :
		      (uint32_t *)((char *)info->coll_ids + colls_size);
	info->sort_orders = part_count == 0 ? NULL :
			    (uint32_t *)((char *)info->parts + parts_size);
	info->field_count = field_count;
	info->part_count = part_count;

	for (uint32_t i = 0; i < field_count; ++i) {
		info->types[i] = FIELD_TYPE_SCALAR;
		info->coll_ids[i] = COLL_NONE;
	}
	for (uint32_t i = 0; i < part_count; ++i) {
		info->parts[i] = i;
		info->sort_orders[i] = SORT_ORDER_ASC;

	}
	return info;
}

struct sql_space_info *
sql_space_info_new_from_space_def(const struct space_def *def)
{
	uint32_t field_count = def->field_count + 1;
	struct sql_space_info *info = sql_space_info_new(field_count, 0);
	for (uint32_t i = 0; i < def->field_count; ++i) {
		info->types[i] = def->fields[i].type;
		info->coll_ids[i] = def->fields[i].coll_id;
	}
	/* Add one more field for rowid. */
	info->types[def->field_count] = FIELD_TYPE_INTEGER;
	return info;
}

struct sql_space_info *
sql_space_info_new_from_index_def(const struct index_def *def, bool has_rowid)
{
	uint32_t field_count = def->key_def->part_count;
	if (has_rowid)
		++field_count;
	struct sql_space_info *info = sql_space_info_new(field_count, 0);
	for (uint32_t i = 0; i < def->key_def->part_count; ++i) {
		info->types[i] = def->key_def->parts[i].type;
		info->coll_ids[i] = def->key_def->parts[i].coll_id;
	}
	if (has_rowid)
		info->types[def->key_def->part_count] = FIELD_TYPE_INTEGER;
	return info;
}

struct space *
sql_ephemeral_space_new(const struct sql_space_info *info)
{
	uint32_t field_count = info->field_count;
	uint32_t part_count = info->parts == NULL ? field_count :
			      info->part_count;
	uint32_t parts_indent = field_count * sizeof(struct field_def);
	uint32_t names_indent = part_count * sizeof(struct key_part_def) +
				parts_indent;
	/*
	 * Name of the fields will be "_COLUMN_1", "_COLUMN_2" and so on. Due to
	 * this, length of each name is no more than 19 == strlen("_COLUMN_")
	 * plus length of UINT32_MAX turned to string, which is 10 and plus 1
	 * for '\0'.
	 */
	uint32_t max_len = 19;
	uint32_t size = names_indent + field_count * max_len;

	struct region *region = &fiber()->gc;
	size_t svp = region_used(region);
	struct field_def *fields = xregion_aligned_alloc(region, size,
							 alignof(fields[0]));
	struct key_part_def *parts = (struct key_part_def *)((char *)fields +
							     parts_indent);
	static_assert(alignof(*fields) == alignof(*parts), "allocated in one "
		      "block, and should have the same alignment");
	char *names = (char *)fields + names_indent;

	for (uint32_t i = 0; i < info->field_count; ++i) {
		fields[i] = field_def_default;
		fields[i].name = names;
		snprintf(names, max_len, "_COLUMN_%d", i);
		names += strlen(fields[i].name) + 1;
		fields[i].is_nullable = true;
		fields[i].nullable_action = ON_CONFLICT_ACTION_NONE;
		fields[i].default_value = NULL;
		fields[i].default_value_size = 0;
		fields[i].default_func_id = 0;
		fields[i].type = info->types[i];
		fields[i].coll_id = info->coll_ids[i];
		fields[i].compression_type = COMPRESSION_TYPE_NONE;
	}
	for (uint32_t i = 0; i < part_count; ++i) {
		uint32_t j = info->parts == NULL ? i : info->parts[i];
		parts[i].fieldno = j;
		parts[i].nullable_action = ON_CONFLICT_ACTION_NONE;
		parts[i].is_nullable = true;
		parts[i].exclude_null = false;
		parts[i].sort_order = SORT_ORDER_ASC;
		parts[i].path = NULL;
		enum field_type type = info->types[j];
		parts[i].type = type;
		if (!field_type1_contains_type2(FIELD_TYPE_SCALAR, type)) {
			const char *err =
				tt_sprintf("field type '%s' is not comparable",
					   field_type_strs[type]);
			diag_set(ClientError, ER_SQL_EXECUTE, err);
			return NULL;
		}

		parts[i].coll_id = info->coll_ids[j];
	}

	struct key_def *key_def = key_def_new(parts, part_count, 0);
	if (key_def == NULL)
		return NULL;

	const char *name = "ephemer_idx";
	struct index_def *index_def = index_def_new(0, 0, name, strlen(name),
						    TREE, &index_opts_default,
						    key_def, NULL);
	key_def_delete(key_def);
	if (index_def == NULL)
		return NULL;

	struct rlist key_list;
	rlist_create(&key_list);
	rlist_add_entry(&key_list, index_def, link);

	struct space_def *space_def = space_def_new_ephemeral(field_count,
							      fields);
	if (space_def == NULL) {
		index_def_delete(index_def);
		return NULL;
	}

	struct space *space = space_new_ephemeral(space_def, &key_list);
	index_def_delete(index_def);
	space_def_delete(space_def);
	region_truncate(region, svp);

	return space;
}

int tarantoolsqlEphemeralInsert(struct space *space, const char *tuple,
				    const char *tuple_end)
{
	assert(space != NULL);
	mp_tuple_assert(tuple, tuple_end);
	return space_ephemeral_replace(space, tuple, tuple_end);
}

/* Simply delete ephemeral space by calling space_delete(). */
void
tarantoolsqlEphemeralDrop(BtCursor *pCur)
{
	assert(pCur);
	assert(pCur->curFlags & BTCF_TEphemCursor);
	space_delete(pCur->space);
	pCur->space = NULL;
}

static inline int
insertOrReplace(struct space *space, const char *tuple, const char *tuple_end,
		enum iproto_type type)
{
	assert(space != NULL);
	struct request request;
	memset(&request, 0, sizeof(request));
	request.tuple = tuple;
	request.tuple_end = tuple_end;
	request.space_id = space->def->id;
	request.type = type;
	mp_tuple_assert(request.tuple, request.tuple_end);
	return box_process1(&request, NULL);
}

int tarantoolsqlInsert(struct space *space, const char *tuple,
			   const char *tuple_end)
{
	return insertOrReplace(space, tuple, tuple_end, IPROTO_INSERT);
}

int tarantoolsqlReplace(struct space *space, const char *tuple,
			    const char *tuple_end)
{
	return insertOrReplace(space, tuple, tuple_end, IPROTO_REPLACE);
}

/*
 * Delete tuple from ephemeral space. It is contained in cursor
 * as a result of previous call to cursor_advance().
 *
 * @param pCur Cursor pointing to ephemeral space.
 *
 * @retval 0 on success, -1 otherwise.
 */
int tarantoolsqlEphemeralDelete(BtCursor *pCur)
{
	assert(pCur->curFlags & BTCF_TEphemCursor);
	assert(pCur->iter != NULL);
	assert(pCur->last_tuple != NULL);

	char *key;
	uint32_t key_size;
	size_t region_svp = region_used(&fiber()->gc);
	key = tuple_extract_key(pCur->last_tuple, pCur->index->def->key_def,
				MULTIKEY_NONE, &key_size);
	if (key == NULL)
		return -1;

	int rc = space_ephemeral_delete(pCur->space, key);
	region_truncate(&fiber()->gc, region_svp);
	if (rc != 0) {
		diag_log();
		return -1;
	}
	return 0;
}

int
tarantoolsqlDelete(struct BtCursor *pCur)
{
	assert(pCur->curFlags & BTCF_TaCursor);
	assert(pCur->iter != NULL);
	assert(pCur->last_tuple != NULL);

	char *key;
	uint32_t key_size;

	size_t region_svp = region_used(&fiber()->gc);
	key = tuple_extract_key(pCur->last_tuple,
				pCur->index->def->key_def,
				MULTIKEY_NONE, &key_size);
	if (key == NULL)
		return -1;
	int rc = sql_delete_by_key(pCur->space, pCur->index->def->iid, key,
				   key_size);
	region_truncate(&fiber()->gc, region_svp);
	return rc;
}

int
sql_delete_by_key(struct space *space, uint32_t iid, char *key,
		  uint32_t key_size)
{
	struct request request;
	struct tuple *unused;
	memset(&request, 0, sizeof(request));
	request.type = IPROTO_DELETE;
	request.key = key;
	request.key_end = key + key_size;
	request.space_id = space->def->id;
	request.index_id = iid;
	assert(space_index(space, iid)->def->opts.is_unique);
	return box_process1(&request, &unused);
}

/*
 * Delete all tuples from space. It is worth noting, that truncate can't
 * be applied to ephemeral space, so this routine manually deletes
 * tuples one by one.
 *
 * @param pCur Cursor pointing to ephemeral space.
 *
 * @retval 0 on success, -1 otherwise.
 */
int tarantoolsqlEphemeralClearTable(BtCursor *pCur)
{
	assert(pCur);
	assert(pCur->curFlags & BTCF_TEphemCursor);

	struct iterator *it = index_create_iterator(*pCur->space->index,
						    ITER_ALL, nil_key,
						    0 /* part_count */);
	if (it == NULL) {
		pCur->eState = CURSOR_INVALID;
		return -1;
	}

	struct tuple *tuple;
	char *key;
	uint32_t  key_size;

	while (iterator_next(it, &tuple) == 0 && tuple != NULL) {
		size_t region_svp = region_used(&fiber()->gc);
		key = tuple_extract_key(tuple, pCur->index->def->key_def,
					MULTIKEY_NONE, &key_size);
		int rc = space_ephemeral_delete(pCur->space, key);
		region_truncate(&fiber()->gc, region_svp);

		if (rc != 0) {
			iterator_delete(it);
			return -1;
		}
	}
	iterator_delete(it);

	return 0;
}

/*
 * Removes all instances from table.
 * Iterate through the space and delete one by one all tuples.
 */
int tarantoolsqlClearTable(struct space *space, uint32_t *tuple_count)
{
	assert(tuple_count != NULL);
	*tuple_count = 0;
	uint32_t key_size;
	box_tuple_t *tuple;
	int rc;
	struct tuple *unused;
	struct request request;
	memset(&request, 0, sizeof(request));
	request.type = IPROTO_DELETE;
	request.space_id = space->def->id;
	struct index *pk = space_index(space, 0 /* PK */);
	struct iterator *iter = index_create_iterator(pk, ITER_ALL, nil_key, 0);
	if (iter == NULL)
		return -1;
	while (iterator_next(iter, &tuple) == 0 && tuple != NULL) {
		size_t region_svp = region_used(&fiber()->gc);
		request.key = tuple_extract_key(tuple, pk->def->key_def,
						MULTIKEY_NONE, &key_size);
		request.key_end = request.key + key_size;
		rc = box_process1(&request, &unused);
		region_truncate(&fiber()->gc, region_svp);
		if (rc != 0) {
			iterator_delete(iter);
			return -1;
		}
		(*tuple_count)++;
	}
	iterator_delete(iter);

	return 0;
}

/*
 * Change the statement of trigger in _trigger space.
 * This function is called after tarantoolsqlRenameTable,
 * in order to update name of table in create trigger statement.
 */
int tarantoolsqlRenameTrigger(const char *trig_name,
				  const char *old_table_name,
				  const char *new_table_name)
{
	assert(trig_name);
	assert(old_table_name);
	assert(new_table_name);

	box_tuple_t *tuple;
	uint32_t trig_name_len = strlen(trig_name);
	uint32_t old_table_name_len = strlen(old_table_name);
	uint32_t new_table_name_len = strlen(new_table_name);
	uint32_t key_len = mp_sizeof_str(trig_name_len) + mp_sizeof_array(1);
	char *key_begin = xregion_alloc(&fiber()->gc, key_len);
	char *key = mp_encode_array(key_begin, 1);
	key = mp_encode_str(key, trig_name, trig_name_len);
	if (box_index_get(BOX_TRIGGER_ID, 0, key_begin, key, &tuple) != 0)
		return -1;
	assert(tuple != NULL);
	assert(tuple_field_count(tuple) == 3);
	const char *field = tuple_field(tuple, BOX_TRIGGER_FIELD_SPACE_ID);
	assert(mp_typeof(*field) == MP_UINT);
	uint32_t space_id = mp_decode_uint(&field);
	field = tuple_field(tuple, BOX_TRIGGER_FIELD_OPTS);
	assert(mp_typeof(*field) == MP_MAP);
	mp_decode_map(&field);
	const char *sql_str = mp_decode_str(&field, &key_len);
	if (sqlStrNICmp(sql_str, "sql", 3) != 0) {
		diag_set(ClientError, ER_SQL_EXECUTE, "can't modify name of "\
			 "space created not via SQL facilities");
		return -1;
	}
	uint32_t trigger_stmt_len;
	const char *trigger_stmt_old = mp_decode_str(&field, &trigger_stmt_len);
	char *trigger_stmt = xregion_alloc(&fiber()->gc, trigger_stmt_len + 1);
	memcpy(trigger_stmt, trigger_stmt_old, trigger_stmt_len);
	trigger_stmt[trigger_stmt_len] = '\0';
	bool is_quoted = false;
	trigger_stmt = rename_trigger(trigger_stmt, new_table_name, &is_quoted);

	uint32_t trigger_stmt_new_len = trigger_stmt_len + new_table_name_len -
					old_table_name_len + 2 * (!is_quoted);
	assert(trigger_stmt_new_len > 0);
	key_len = mp_sizeof_array(3) + mp_sizeof_str(trig_name_len) +
		  mp_sizeof_map(1) + mp_sizeof_str(3) +
		  mp_sizeof_str(trigger_stmt_new_len) +
		  mp_sizeof_uint(space_id);
	char *new_tuple = xregion_alloc(&fiber()->gc, key_len);
	char *new_tuple_end = mp_encode_array(new_tuple, 3);
	new_tuple_end = mp_encode_str(new_tuple_end, trig_name, trig_name_len);
	new_tuple_end = mp_encode_uint(new_tuple_end, space_id);
	new_tuple_end = mp_encode_map(new_tuple_end, 1);
	new_tuple_end = mp_encode_str(new_tuple_end, "sql", 3);
	new_tuple_end = mp_encode_str(new_tuple_end, trigger_stmt,
				      trigger_stmt_new_len);

	return box_replace(BOX_TRIGGER_ID, new_tuple, new_tuple_end, NULL);
}

int
sql_rename_table(uint32_t space_id, const char *new_name)
{
	assert(space_id != 0);
	assert(new_name != NULL);
	int name_len = strlen(new_name);
	struct region *region = &fiber()->gc;
	/* 32 + name_len is enough to encode one update op. */
	size_t size = 32 + name_len;
	size_t region_svp = region_used(&fiber()->gc);
	char *raw = xregion_alloc(region, size);
	/* Encode key. */
	char *pos = mp_encode_array(raw, 1);
	pos = mp_encode_uint(pos, space_id);

	/* Encode op and new name. */
	char *ops = pos;
	pos = mp_encode_array(pos, 1);
	pos = mp_encode_array(pos, 3);
	pos = mp_encode_str(pos, "=", 1);
	pos = mp_encode_uint(pos, BOX_SPACE_FIELD_NAME);
	pos = mp_encode_str(pos, new_name, name_len);
	int rc = box_update(BOX_SPACE_ID, 0, raw, ops, ops, pos, 0, NULL);
	region_truncate(&fiber()->gc, region_svp);
	return rc;
}

int
tarantoolsqlIdxKeyCompare(struct BtCursor *cursor,
			      struct UnpackedRecord *unpacked)
{
	assert(cursor->curFlags & (BTCF_TaCursor | BTCF_TEphemCursor));
	assert(cursor->iter != NULL);
	assert(cursor->last_tuple != NULL);

	struct key_def *key_def;
	struct tuple *tuple;
	const char *base;
	struct tuple_format *format;
	const char *field_map;
	uint32_t field_count, next_fieldno = 0;
	const char *p, *field0;
	u32 i, n;
	int rc;
#ifndef NDEBUG
	size_t original_size;
	const char *key;
	uint32_t key_size;
#endif

	key_def = cursor->index->def->key_def;
	n = MIN(unpacked->nField, key_def->part_count);
	tuple = cursor->last_tuple;
	base = tuple_data(tuple);
	format = tuple_format(tuple);
	field_map = tuple_field_map(tuple);
	field_count = tuple_format_field_count(format);
	field0 = base;
	uint32_t base_len = mp_decode_array(&field0);
	p = field0;
	for (i = 0; i < n; i++) {
		/*
		 * Tuple contains offset map to make it possible to
		 * extract indexed fields without decoding all prior
		 * fields.  There's a caveat though:
		 *  (1) The very first field's offset is never stored;
		 *  (2) if an index samples consequetive fields,
		 *      ex: 3-4-5, only the very first field in a run
		 *      has its offset stored;
		 *  (3) field maps are rebuilt lazily when a new index
		 *      is added, i.e. it is possible to encounter a
		 *      tuple with an incomplete offset map.
		 *  (4) it is possible that the length of the tuple data will be
		 *      less than the given fieldno of the part, in which case
		 *      we should just compare the mem from unpacked with NULL.
		 */
		uint32_t fieldno = key_def->parts[i].fieldno;
		struct Mem *mem = &unpacked->aMem[i];
		struct key_part *part = &unpacked->key_def->parts[i];
		if (fieldno >= base_len) {
			if (mem_is_null(mem))
				continue;
			rc = part->sort_order == SORT_ORDER_ASC ? -1 : 1;
			goto out;
		}

		if (fieldno != next_fieldno) {
			struct tuple_field *field = NULL;
			if (fieldno < field_count)
				field = tuple_format_field(format, fieldno);

			if (field == NULL ||
			    field->offset_slot == TUPLE_OFFSET_SLOT_NIL) {
				/* Outdated field_map. */
				uint32_t j = 0;

				p = field0;
				while (j++ != fieldno)
					mp_next(&p);
			} else {
				uint32_t field_offset =
					field_map_get_offset(field_map,
							     field->offset_slot,
							     MULTIKEY_NONE);
				p = base + field_offset;
			}
		}
		next_fieldno = fieldno + 1;
		struct coll *coll = part->coll;
		if (mem_cmp_msgpack(mem, &p, &rc, coll) != 0)
			rc = 0;
		if (rc != 0) {
			if (part->sort_order == SORT_ORDER_ASC)
				rc = -rc;
			goto out;
		}
	}
	rc = unpacked->default_rc;
out:
#ifndef NDEBUG
	/* Sanity check. */
	original_size = region_used(&fiber()->gc);
	key = tuple_extract_key(tuple, key_def, MULTIKEY_NONE, &key_size);
	if (key != NULL) {
		int new_rc = sqlVdbeRecordCompareMsgpack(key, unpacked);
		region_truncate(&fiber()->gc, original_size);
		/*
		 * Here we compare two results from memcmp() alike
		 * calls. A particular implementation depends on
		 * a type of msgpack values to compare. For some
		 * of them we actually call memcmp().
		 *
		 * memcmp() only guarantees that a result will be
		 * less than zero, zero or more than zero. It
		 * DOES NOT guarantee that the result will be
		 * subtraction of the first non-equal bytes or
		 * something else about the result aside of its
		 * sign.
		 *
		 * So we don't compare `rc` and `new_rc` for
		 * equivalence.
		 */
		assert((rc == 0 && new_rc == 0) ||
		       (rc < 0 && new_rc < 0) ||
		       (rc > 0 && new_rc > 0));
	}
#endif
	return rc;
}

/*
 * Allocate or grow memory for cursor's key.
 * Result->type value is unspecified.
 */
int
key_alloc(BtCursor *cur, size_t key_size)
{
	if (cur->key == NULL) {
		cur->key = malloc(key_size);
		if (cur->key == NULL) {
			diag_set(OutOfMemory, key_size, "malloc", "cur->key");
			return -1;
		}
		/*
		 * Key can be NULL, only if it is a brand new
		 * cursor. In this case, iterator and tuple must
		 * also be NULLs, since memory for cursor is
		 * filled with 0.
		 */
		assert(cur->iter == NULL);
		assert(cur->last_tuple == NULL);
	} else {
		char *new_key = realloc(cur->key, key_size);
		if (new_key == NULL) {
			diag_set(OutOfMemory, key_size, "realloc", "new_key");
			return -1;
		}
		cur->key = new_key;
	}
	return 0;
}

/*
 * Create new Tarantool iterator and set it to the first entry found by
 * given key. If cursor already contains iterator, it will be freed.
 *
 * @param pCur Cursor which points to space.
 * @param pRes Flag which is == 0 if reached end of space, == 1 otherwise.
 * @param type Type of Tarantool iterator.
 * @param key Start of buffer containing key.
 * @param key_end End of buffer containing key.
 *
 * @retval 0 on success, -1 otherwise.
 */
static int
cursor_seek(BtCursor *pCur, int *pRes)
{
	/* Close existing iterator, if any */
	if (pCur->iter) {
		box_iterator_free(pCur->iter);
		pCur->iter = NULL;
	}
	const char *key = (const char *)pCur->key;
	uint32_t part_count = mp_decode_array(&key);
	if (key_validate(pCur->index->def, pCur->iter_type, key, part_count)) {
		diag_log();
		return -1;
	}

	struct space *space = pCur->space;
	struct txn *txn = NULL;
	struct txn_ro_savepoint svp;
	if (space->def->id != 0 && txn_begin_ro_stmt(space, &txn, &svp) != 0)
		return -1;
	struct iterator *it =
		index_create_iterator(pCur->index, pCur->iter_type, key,
				      part_count);
	if (txn != NULL)
		txn_end_ro_stmt(txn, &svp);
	if (it == NULL) {
		pCur->eState = CURSOR_INVALID;
		return -1;
	}
	pCur->iter = it;
	pCur->eState = CURSOR_VALID;

	return cursor_advance(pCur, pRes);
}

/*
 * Move cursor to the next entry in space.
 * New tuple is refed and saved in cursor.
 * Tuple from previous call is unrefed.
 *
 * @param pCur Cursor which contains space and tuple.
 * @param[out] pRes Flag which is 0 if reached end of space, 1 otherwise.
 *
 * @retval 0 on success, -1 otherwise.
 */
static int
cursor_advance(BtCursor *pCur, int *pRes)
{
	assert(pCur->iter != NULL);

	struct tuple *tuple;
	if (iterator_next(pCur->iter, &tuple) != 0)
		return -1;
	if (pCur->last_tuple)
		box_tuple_unref(pCur->last_tuple);
	if (tuple) {
		box_tuple_ref(tuple);
		*pRes = 0;
	} else {
		pCur->eState = CURSOR_INVALID;
		*pRes = 1;
	}
	pCur->last_tuple = tuple;
	return 0;
}

/*********************************************************************
 * Metainformation about available spaces and indices is stored in
 * _space and _index system spaces respectively.
 *
 * sql inserts entries in system spaces.
 *
 * The routines below are called during SQL query processing in order to
 * format data for certain fields in _space and _index.
 */

/** Encode space constraints. */
static void
sql_mpstream_encode_constraints(struct mpstream *stream,
				const struct tuple_constraint_def *cdefs,
				uint32_t ck_count, uint32_t fk_count)
{
	if (fk_count > 0) {
		mpstream_encode_str(stream, "foreign_key");
		mpstream_encode_map(stream, fk_count);
		uint32_t count = ck_count + fk_count;
		for (uint32_t i = 0; i < count; ++i) {
			if (cdefs[i].type != CONSTR_FKEY)
				continue;
			mpstream_encode_str(stream, cdefs[i].name);
			const struct tuple_constraint_fkey_def *fkey =
				&cdefs[i].fkey;
			uint32_t space_id = fkey->space_id;
			if (space_id != 0) {
				mpstream_encode_map(stream, 2);
				mpstream_encode_str(stream, "space");
				mpstream_encode_uint(stream, space_id);
			} else {
				mpstream_encode_map(stream, 1);
			}
			mpstream_encode_str(stream, "field");
			assert(fkey->field.name_len != 0);
			mpstream_encode_str(stream, fkey->field.name);
		}
	}
	if (ck_count > 0) {
		mpstream_encode_str(stream, "constraint");
		mpstream_encode_map(stream, ck_count);
		uint32_t count = ck_count + fk_count;
		for (uint32_t i = 0; i < count; ++i) {
			if (cdefs[i].type != CONSTR_FUNC)
				continue;
			mpstream_encode_str(stream, cdefs[i].name);
			mpstream_encode_uint(stream, cdefs[i].func.id);
		}
	}
}

char *
sql_encode_table(struct region *region, struct space_def *def, uint32_t *size)
{
	size_t used = region_used(region);
	struct mpstream stream;
	bool is_error = false;
	mpstream_init(&stream, region, region_reserve_cb, region_alloc_cb,
		      set_encode_error, &is_error);

	assert(def != NULL);
	uint32_t field_count = def->field_count;
	mpstream_encode_array(&stream, field_count);
	for (uint32_t i = 0; i < field_count && !is_error; i++) {
		uint32_t cid = def->fields[i].coll_id;
		struct field_def *field = &def->fields[i];
		int base_len = 4;
		if (cid != COLL_NONE)
			base_len += 1;
		if (field->default_value != NULL)
			base_len += 1;
		if (field->default_func_id != 0)
			base_len += 1;
		uint32_t ck_count = 0;
		uint32_t fk_count = 0;
		struct tuple_constraint_def *cdefs = field->constraint_def;
		for (uint32_t i = 0; i < field->constraint_count; ++i) {
			assert(cdefs[i].type == CONSTR_FUNC ||
			       cdefs[i].type == CONSTR_FKEY);
			if (cdefs[i].type == CONSTR_FUNC)
				++ck_count;
			else
				++fk_count;
		}
		if (ck_count > 0)
			++base_len;
		if (fk_count > 0)
			++base_len;
		mpstream_encode_map(&stream, base_len);
		mpstream_encode_str(&stream, "name");
		mpstream_encode_str(&stream, field->name);
		mpstream_encode_str(&stream, "type");
		assert(def->fields[i].is_nullable ==
		       action_is_nullable(def->fields[i].nullable_action));
		mpstream_encode_str(&stream, field_type_strs[field->type]);
		mpstream_encode_str(&stream, "is_nullable");
		mpstream_encode_bool(&stream, def->fields[i].is_nullable);
		mpstream_encode_str(&stream, "nullable_action");

		assert(def->fields[i].nullable_action < on_conflict_action_MAX);
		const char *action =
			on_conflict_action_strs[def->fields[i].nullable_action];
		mpstream_encode_str(&stream, action);
		if (cid != COLL_NONE) {
			mpstream_encode_str(&stream, "collation");
			mpstream_encode_uint(&stream, cid);
		}
		if (field->default_value != NULL) {
			mpstream_encode_str(&stream, "default");
			mpstream_memcpy(&stream, field->default_value,
					field->default_value_size);
		}
		if (field->default_func_id != 0) {
			mpstream_encode_str(&stream, "default_func");
			mpstream_encode_uint(&stream, field->default_func_id);
		}
		sql_mpstream_encode_constraints(&stream, cdefs, ck_count,
						fk_count);
	}
	mpstream_flush(&stream);
	if (is_error) {
		diag_set(OutOfMemory, stream.pos - stream.buf,
			"mpstream_flush", "stream");
		return NULL;
	}
	*size = region_used(region) - used;
	return xregion_join(region, *size);
}

char *
sql_encode_table_opts(struct region *region, struct space_def *def,
		      uint32_t *size)
{
	size_t used = region_used(region);
	struct mpstream stream;
	bool is_error = false;
	mpstream_init(&stream, region, region_reserve_cb, region_alloc_cb,
		      set_encode_error, &is_error);
	bool is_view = def->opts.is_view;
	mpstream_encode_map(&stream, 2 * is_view);

	if (is_view) {
		assert(def->opts.sql != NULL);
		mpstream_encode_str(&stream, "sql");
		mpstream_encode_str(&stream, def->opts.sql);
		mpstream_encode_str(&stream, "view");
		mpstream_encode_bool(&stream, true);
	}
	mpstream_flush(&stream);
	if (is_error) {
		diag_set(OutOfMemory, stream.pos - stream.buf,
			 "mpstream_flush", "stream");
		return NULL;
	}
	*size = region_used(region) - used;
	return xregion_join(region, *size);
}

char *
fk_constraint_encode_links(const struct fk_constraint_def *fk, uint32_t *size)
{
	*size = mp_sizeof_map(fk->field_count);
	for (uint32_t i = 0; i < fk->field_count; ++i) {
		*size += mp_sizeof_uint(fk->links[i].child_field);
		*size += mp_sizeof_uint(fk->links[i].parent_field);
	}
	char *buf = sql_xmalloc(*size);
	char *buf_end = mp_encode_map(buf, fk->field_count);
	for (uint32_t i = 0; i < fk->field_count; ++i) {
		buf_end = mp_encode_uint(buf_end, fk->links[i].child_field);
		buf_end = mp_encode_uint(buf_end, fk->links[i].parent_field);
	}
	return buf;
}

char *
sql_encode_index_parts(struct region *region, const struct field_def *fields,
		       const struct index_def *idx_def, uint32_t *size)
{
	size_t used = region_used(region);
	struct mpstream stream;
	bool is_error = false;
	mpstream_init(&stream, region, region_reserve_cb, region_alloc_cb,
		      set_encode_error, &is_error);
	struct key_def *key_def = idx_def->key_def;
	struct key_part *part = key_def->parts;
	mpstream_encode_array(&stream, key_def->part_count);
	for (uint32_t i = 0; i < key_def->part_count; ++i, ++part) {
		uint32_t col = part->fieldno;
		assert(fields[col].is_nullable ==
		       action_is_nullable(fields[col].nullable_action));
		/* Do not decode default collation. */
		uint32_t cid = part->coll_id;
		mpstream_encode_map(&stream, 6 + (cid != COLL_NONE));
		mpstream_encode_str(&stream, "type");
		mpstream_encode_str(&stream, field_type_strs[fields[col].type]);
		mpstream_encode_str(&stream, "field");
		mpstream_encode_uint(&stream, col);

		if (cid != COLL_NONE) {
			mpstream_encode_str(&stream, "collation");
			mpstream_encode_uint(&stream, cid);
		}
		mpstream_encode_str(&stream, "is_nullable");
		mpstream_encode_bool(&stream, fields[col].is_nullable);
		mpstream_encode_str(&stream, "nullable_action");
		const char *action_str =
			on_conflict_action_strs[fields[col].nullable_action];
		mpstream_encode_str(&stream, action_str);

		mpstream_encode_str(&stream, "sort_order");
		enum sort_order sort_order = part->sort_order;
		assert(sort_order < sort_order_MAX);
		const char *sort_order_str = sort_order_strs[sort_order];
		mpstream_encode_str(&stream, sort_order_str);
		mpstream_encode_str(&stream, "exclude_null");
		mpstream_encode_bool(&stream, false);
	}
	mpstream_flush(&stream);
	if (is_error) {
		diag_set(OutOfMemory, stream.pos - stream.buf,
			 "mpstream_flush", "stream");
		return NULL;
	}
	*size = region_used(region) - used;
	return xregion_join(region, *size);
}

char *
sql_encode_index_opts(struct region *region, const struct index_opts *opts,
		      uint32_t *size)
{
	size_t used = region_used(region);
	struct mpstream stream;
	bool is_error = false;
	mpstream_init(&stream, region, region_reserve_cb, region_alloc_cb,
		      set_encode_error, &is_error);
	/*
	 * In case of vinyl engine we must inherit global
	 * (i.e. set via box.cfg) params such as bloom_fpr,
	 * page_size etc.
	 */
	uint8_t current_engine = current_session()->sql_default_engine;
	uint32_t map_sz = current_engine == SQL_STORAGE_ENGINE_VINYL ? 6 : 1;
	mpstream_encode_map(&stream, map_sz);
	mpstream_encode_str(&stream, "unique");
	mpstream_encode_bool(&stream, opts->is_unique);
	if (current_engine == SQL_STORAGE_ENGINE_VINYL) {
		mpstream_encode_str(&stream, "range_size");
		mpstream_encode_uint(&stream, cfg_geti64("vinyl_range_size"));
		mpstream_encode_str(&stream, "page_size");
		mpstream_encode_uint(&stream, cfg_geti64("vinyl_page_size"));
		mpstream_encode_str(&stream, "run_count_per_level");
		mpstream_encode_uint(&stream, cfg_geti("vinyl_run_count_per_level"));
		mpstream_encode_str(&stream, "run_size_ratio");
		mpstream_encode_double(&stream, cfg_getd("vinyl_run_size_ratio"));
		mpstream_encode_str(&stream, "bloom_fpr");
		mpstream_encode_double(&stream, cfg_getd("vinyl_bloom_fpr"));
	}
	mpstream_flush(&stream);
	if (is_error) {
		diag_set(OutOfMemory, stream.pos - stream.buf,
			 "mpstream_flush", "stream");
		return NULL;
	}
	*size = region_used(region) - used;
	return xregion_join(region, *size);
}

void
sql_debug_info(struct info_handler *h)
{
	extern int sql_search_count;
	extern int sql_sort_count;
	extern int sql_found_count;
	extern int sql_xfer_count;
	info_begin(h);
	info_append_int(h, "sql_search_count", sql_search_count);
	info_append_int(h, "sql_sort_count", sql_sort_count);
	info_append_int(h, "sql_found_count", sql_found_count);
	info_append_int(h, "sql_xfer_count", sql_xfer_count);
	info_end(h);
}

/**
 * Create and initialize a new template space_def object.
 * @param parser SQL Parser object.
 * @param name Name of space to be created.
 */
static struct space_def *
sql_template_space_def_new(struct Parse *parser, const char *name)
{
	struct space_def *def = NULL;
	size_t name_len = name != NULL ? strlen(name) : 0;
	size_t size = sizeof(*def) + name_len + 1;
	def = xregion_aligned_alloc(&parser->region, size, alignof(*def));
	memset(def, 0, size);
	memcpy(def->name, name, name_len);
	def->name[name_len] = '\0';
	def->opts.is_ephemeral = true;
	return def;
}

struct space *
sql_template_space_new(Parse *parser, const char *name)
{
	struct space *space = xregion_alloc_object(&parser->region,
						   typeof(*space));
	memset(space, 0, sizeof(*space));
	space->def = sql_template_space_def_new(parser, name);
	return space;
}

/**
 * Fill vdbe_field_ref instance with given tuple data.
 *
 * @param field_ref The vdbe_field_ref instance to initialize.
 * @param tuple The tuple object pointer or NULL when undefined.
 * @param data The tuple data (is always defined).
 * @param data_sz The size of tuple data (is always defined).
 */
static void
vdbe_field_ref_fill(struct vdbe_field_ref *field_ref, struct tuple *tuple,
		    uint32_t mp_count, const char *data, uint32_t data_sz)
{
	field_ref->tuple = tuple;
	field_ref->data = data;
	field_ref->data_sz = data_sz;

	field_ref->format = NULL;
	field_ref->field_count = MIN(field_ref->field_capacity, mp_count);
	field_ref->slots[0] = 0;
	memset(&field_ref->slots[1], 0,
	       field_ref->field_count * sizeof(field_ref->slots[0]));
	field_ref->slot_bitmask = 0;
	bitmask64_set_bit(&field_ref->slot_bitmask, 0);
}

void
vdbe_field_ref_prepare_data(struct vdbe_field_ref *field_ref, const char *data,
			    uint32_t data_sz)
{
	const char *field0 = data;
	uint32_t mp_count = mp_decode_array(&field0);
	vdbe_field_ref_fill(field_ref, NULL, mp_count, field0,
			    (uint32_t)(field0 - data) + data_sz);
}

void
vdbe_field_ref_prepare_tuple(struct vdbe_field_ref *field_ref,
			     struct tuple *tuple)
{
	const char *data = tuple_data(tuple);
	uint32_t data_sz = tuple_bsize(tuple);
	const char *field0 = data;
	uint32_t mp_count = mp_decode_array(&field0);
	vdbe_field_ref_fill(field_ref, NULL, mp_count, field0,
			    (uint32_t)(field0 - data) + data_sz);
}

void
vdbe_field_ref_prepare_array(struct vdbe_field_ref *ref, uint32_t field_count,
			     const char *data, uint32_t data_sz)
{
	return vdbe_field_ref_fill(ref, NULL, field_count, data, data_sz);
}

void
vdbe_field_ref_create(struct vdbe_field_ref *ref, uint32_t capacity)
{
	memset(ref, 0, sizeof(*ref) + capacity * sizeof(ref->slots[0]));
	ref->field_capacity = capacity;
}

ssize_t
sql_index_tuple_size(struct space *space, struct index *idx)
{
	assert(space != NULL);
	assert(idx != NULL);
	assert(idx->def->space_id == space->def->id);
	ssize_t tuple_count = index_size(idx);
	ssize_t space_size = space_bsize(space);
	ssize_t avg_tuple_size = tuple_count != 0 ?
				 (space_size / tuple_count) : 0;
	return avg_tuple_size;
}

/**
 * default_tuple_est[] array contains default information
 * which is used when we don't have real space, e.g. temporary
 * objects representing result set of nested SELECT or VIEW.
 *
 * First number is supposed to contain the number of elements
 * in the index. Since we do not know, guess 1 million.
 * Second one is an estimate of the number of rows in the
 * table that match any particular value of the first column of
 * the index. Third one is an estimate of the number of
 * rows that match any particular combination of the first 2
 * columns of the index. And so on. It must always be true:
 *
 *           default_tuple_est[N] <= default_tuple_est[N-1]
 *           default_tuple_est[N] >= 1
 *
 * Apart from that, we have little to go on besides intuition
 * as to how default values should be initialized. The numbers
 * generated here are based on typical values found in actual
 * indices.
 */
const int16_t default_tuple_est[] = {DEFAULT_TUPLE_LOG_COUNT, 33, 32, 30, 28,
				     26, 23};

LogEst
sql_space_tuple_log_count(struct space *space)
{
	if (space == NULL || space->index_map == NULL)
		return 0;

	struct index *pk = space_index(space, 0);
	assert(sqlLogEst(DEFAULT_TUPLE_COUNT) == DEFAULT_TUPLE_LOG_COUNT);
	/* If space represents VIEW, return default number. */
	if (pk == NULL)
		return DEFAULT_TUPLE_LOG_COUNT;
	return sqlLogEst(pk->vtab->size(pk));
}

int16_t
index_field_tuple_est(const struct index_def *idx_def, uint32_t field)
{
	assert(idx_def != NULL);
	struct space *space = space_by_id(idx_def->space_id);
	if (space == NULL)
		return 0;
	if (strcmp(idx_def->name, "fake_autoindex") == 0)
		return DEFAULT_TUPLE_LOG_COUNT;
	assert(field <= idx_def->key_def->part_count);
	/*
	 * Last number for unique index is always 0: only one tuple exists with
	 * given full key in unique index and log(1) == 0.
	 */
	if (field == idx_def->key_def->part_count &&
	    idx_def->opts.is_unique)
		return 0;
	return default_tuple_est[field + 1 >= 6 ? 6 : field];
}

/** Drop tuple or field constraint. */
static int
sql_constraint_drop(uint32_t space_id, const char *name, const char *prefix)
{
	size_t name_len = strlen(name);
	size_t prefix_len = strlen(prefix);

	struct region *region = &fiber()->gc;
	uint32_t used = region_used(region);
	char *path = xregion_alloc(region, name_len + prefix_len + 1);
	memcpy(path, prefix, prefix_len);
	memcpy(path + prefix_len, name, name_len);
	path[name_len + prefix_len] = '\0';

	char key[16];
	char *key_end = key + mp_format(key, 16, "[%u]", space_id);
	size_t size;
	const char *ops = mp_format_on_region(region, &size, "[[%s%s%u]]", "#",
					      path, 1);
	const char *end = ops + size;
	int rc = box_update(BOX_SPACE_ID, 0, key, key_end, ops, end, 0, NULL);
	region_truncate(region, used);
	return rc;
}

int
sql_tuple_foreign_key_drop(uint32_t space_id, const char *name)
{
	return sql_constraint_drop(space_id, name, "flags.foreign_key.");
}

int
sql_tuple_check_drop(uint32_t space_id, const char *name)
{
	return sql_constraint_drop(space_id, name, "flags.constraint.");
}

int
sql_field_foreign_key_drop(uint32_t space_id, uint32_t fieldno,
			   const char *name)
{
	const char *prefix = tt_sprintf("format[%u].foreign_key.", fieldno + 1);
	return sql_constraint_drop(space_id, name, prefix);
}

int
sql_field_check_drop(uint32_t space_id, uint32_t fieldno, const char *name)
{
	const char *prefix = tt_sprintf("format[%u].constraint.", fieldno + 1);
	return sql_constraint_drop(space_id, name, prefix);
}

/**
 * Create new constraint in space.
 *
 * @param name Name of the constraint.
 * @param space_id ID of the space.
 * @param path JSON-path of the new constraint.
 * @param value Encoded value of the new constraint.
 */
static int
sql_constraint_create(const char *name, uint32_t space_id, const char *path,
		      const char *value)
{
	struct region *region = &fiber()->gc;
	uint32_t used = region_used(region);
	const int key_size = 16;
	char key[key_size];
	char *key_end = key + mp_format(key, key_size, "[%u]", space_id);
	/*
	 * Even if there were no constraints of this type, it is possible that
	 * _space contains a non-empty field of this type with an empty map as
	 * its value, which affects the update operation.
	 */
	struct tuple *tuple;
	if (box_index_get(BOX_SPACE_ID, 0, key, key_end, &tuple) != 0)
		goto error;
	assert(tuple != NULL);
	uint32_t path_len = strlen(path);
	uint32_t path_hash = field_name_hash(path, path_len);
	const char *field = tuple_field_raw_by_full_path(
		tuple_format(tuple), tuple_data(tuple), tuple_field_map(tuple),
		path, path_len, path_hash, TUPLE_INDEX_BASE);
	bool is_empty = field == NULL;

	const char *ops;
	size_t ops_size;
	if (is_empty) {
		ops = mp_format_on_region(region, &ops_size, "[[%s%s{%s%p}]]",
					  "!", path, name, value);
	} else {
		uint32_t size = strlen(path) + strlen(name) + 2;
		char *buf = xregion_alloc(region, size);
		int len = snprintf(buf, size, "%s.%s", path, name);
		assert(len >= 0 && (uint32_t)len < size);
		(void)len;
		ops = mp_format_on_region(region, &ops_size, "[[%s%s%p]]", "!",
					  buf, value);
	}
	const char *end = ops + ops_size;
	if (box_update(BOX_SPACE_ID, 0, key, key_end, ops, end, 0, NULL) != 0)
		goto error;
	region_truncate(region, used);
	return 0;
error:
	region_truncate(region, used);
	return -1;
}

int
sql_foreign_key_create(const char *name, uint32_t child_id, uint32_t parent_id,
		       uint32_t child_fieldno, uint32_t parent_fieldno,
		       const char *mapping)
{
	const struct space *child = space_by_id(child_id);
	if (child == NULL) {
		diag_set(ClientError, ER_NO_SUCH_SPACE, space_name);
		return -1;
	}
	struct tuple_constraint_def *cdefs;
	uint32_t count;
	const int buf_size = 64;
	char buf[buf_size];
	const char *path;
	const char *value = NULL;

	struct region *region = &fiber()->gc;
	uint32_t used = region_used(region);
	if (mapping == NULL) {
		count = child->def->fields[child_fieldno].constraint_count;
		cdefs = child->def->fields[child_fieldno].constraint_def;
		int len = snprintf(buf, buf_size, "format[%u].foreign_key",
				   child_fieldno + 1);
		assert(len > 0 && len < buf_size);
		(void)len;
		path = buf;
		size_t unused;
		value = mp_format_on_region(region, &unused, "{%s%u%s%u}",
					    "space", parent_id, "field",
					    parent_fieldno);
	} else {
		count = child->def->opts.constraint_count;
		cdefs = child->def->opts.constraint_def;
		path = "flags.foreign_key";
		size_t unused;
		value = mp_format_on_region(region, &unused, "{%s%u%s%p}",
					    "space", parent_id, "field",
					    mapping);
	}
	assert(mp_typeof(*value) == MP_MAP);
	for (uint32_t i = 0; i < count; ++i) {
		if (cdefs[i].type != CONSTR_FKEY)
			continue;
		if (strcmp(name, cdefs[i].name) == 0) {
			region_truncate(region, used);
			diag_set(ClientError, ER_CONSTRAINT_EXISTS,
				 "FOREIGN KEY", name, space_name(child));
			return -1;
		}
	}
	int rc = sql_constraint_create(name, child_id, path, value);
	region_truncate(region, used);
	return rc;
}

int
sql_check_create(const char *name, uint32_t space_id, uint32_t func_id,
		 uint32_t fieldno, bool is_field_ck)
{
	const struct space *space = space_by_id(space_id);
	assert(space != NULL);
	struct tuple_constraint_def *cdefs;
	uint32_t count;
	const char *path;
	const int buf_size = 64;
	char buf[buf_size];
	const uint32_t value_size = 16;
	char value[value_size];
	assert(mp_sizeof_uint(func_id) < value_size);
	mp_encode_uint(value, func_id);

	if (is_field_ck) {
		struct func *func = func_by_id(func_id);
		assert(func != NULL);
		const char *field_name = space->def->fields[fieldno].name;
		if (!func_sql_expr_has_single_arg(func, field_name)) {
			diag_set(ClientError, ER_CREATE_CK_CONSTRAINT, name,
				 "wrong field name specified in the field "
				 "check constraint");
			return -1;
		}
		count = space->def->fields[fieldno].constraint_count;
		cdefs = space->def->fields[fieldno].constraint_def;
		int len = snprintf(buf, buf_size, "format[%u].constraint",
				   fieldno + 1);
		assert(len > 0 && len < buf_size);
		(void)len;
		path = buf;
	} else {
		count = space->def->opts.constraint_count;
		cdefs = space->def->opts.constraint_def;
		path = "flags.constraint";
	}
	for (uint32_t i = 0; i < count; ++i) {
		if (cdefs[i].type != CONSTR_FUNC)
			continue;
		if (strcmp(name, cdefs[i].name) == 0) {
			diag_set(ClientError, ER_CONSTRAINT_EXISTS, "CHECK",
				 name, space_name(space));
			return -1;
		}
	}
	return sql_constraint_create(name, space_id, path, value);
}

int
sql_add_default(uint32_t space_id, uint32_t fieldno, uint32_t func_id)
{
	const char *path = tt_sprintf("format[%u].default_func", fieldno + 1);
	const int ops_size = 128;
	char ops[ops_size];
	const char *ops_end = ops + mp_format(ops, ops_size, "[[%s%s%u]]", "!",
					      path, func_id);
	const int key_size = 16;
	char key[key_size];
	const char *key_end = key + mp_format(key, key_size, "[%u]", space_id);
	return box_update(BOX_SPACE_ID, 0, key, key_end, ops, ops_end, 0, NULL);
}

const struct space *
sql_space_by_token(const struct Token *name)
{
	char *name_str = sql_name_from_token(name);
	struct space *res = space_by_name0(name_str);
	sql_xfree(name_str);
	if (res != NULL || name->z[0] == '"')
		return res;
	char *old_name_str = sql_legacy_name_new(name->z, name->n);
	res = space_by_name0(old_name_str);
	sql_xfree(old_name_str);
	return res;
}

const struct space *
sql_space_by_src(const struct SrcList_item *src)
{
	struct space *res = space_by_name0(src->zName);
	if (res != NULL || src->legacy_name == NULL)
		return res;
	return space_by_name0(src->legacy_name);
}

/**
 * Return id of index with the given name. Return UINT32_MAX if the index was
 * not found.
 */
static uint32_t
sql_space_index_id(const struct space *space, const char *name)
{
	for (uint32_t i = 0; i < space->index_count; ++i) {
		if (strcmp(space->index[i]->def->name, name) == 0)
			return space->index[i]->def->iid;
	}
	return UINT32_MAX;
}

uint32_t
sql_index_id_by_token(const struct space *space, const struct Token *name)
{
	char *name_str = sql_name_from_token(name);
	uint32_t res = sql_space_index_id(space, name_str);
	sql_xfree(name_str);
	if (res != UINT32_MAX || name->z[0] == '"')
		return res;
	char *old_name_str = sql_legacy_name_new(name->z, name->n);
	res = sql_space_index_id(space, old_name_str);
	sql_xfree(old_name_str);
	return res;
}

uint32_t
sql_index_id_by_src(const struct SrcList_item *src)
{
	assert(src->space != NULL && src->fg.isIndexedBy != 0);
	uint32_t res = sql_space_index_id(src->space, src->u1.zIndexedBy);
	if (res != UINT32_MAX || src->legacy_index_name == NULL)
		return res;
	return sql_space_index_id(src->space, src->legacy_index_name);
}

uint32_t
sql_space_fieldno(const struct space *space, const char *name)
{
	for (uint32_t i = 0; i < space->def->field_count; ++i) {
		if (strcmp(space->def->fields[i].name, name) == 0)
			return i;
	}
	return UINT32_MAX;
}

uint32_t
sql_fieldno_by_token(const struct space *space, const struct Token *name)
{
	char *name_str = sql_name_from_token(name);
	uint32_t res = sql_space_fieldno(space, name_str);
	sql_xfree(name_str);
	return res;
}

uint32_t
sql_fieldno_by_id(const struct space *space, const struct IdList_item *id)
{
	uint32_t res = sql_space_fieldno(space, id->zName);
	if (res != UINT32_MAX || id->legacy_name == NULL)
		return res;
	return sql_space_fieldno(space, id->legacy_name);
}

uint32_t
sql_coll_id_by_token(const struct Token *name)
{
	char *name_str = sql_name_from_token(name);
	struct coll_id *coll_id = coll_by_name(name_str, strlen(name_str));
	sql_xfree(name_str);
	if (coll_id != NULL)
		return coll_id->id;
	if (name->z[0] == '"')
		return UINT32_MAX;

	char *old_name_str = sql_legacy_name_new(name->z, name->n);
	coll_id = coll_by_name(old_name_str, strlen(old_name_str));
	sql_xfree(old_name_str);
	if (coll_id != NULL)
		return coll_id->id;
	return UINT32_MAX;
}

/**
 * Return a constraint with the name specified by the token and the
 * specified type. A second lookup will be performed if the constraint is not
 * found on the first try and token is not start with double quote. Return NULL
 * if the constraint was not found.
 */
static const struct tuple_constraint_def *
sql_constraint_by_token(const struct tuple_constraint_def *cdefs,
			uint32_t count, enum tuple_constraint_type type,
			const struct Token *name)
{
	char *name_str = sql_name_from_token(name);
	for (uint32_t i = 0; i < count; ++i) {
		if (strcmp(cdefs[i].name, name_str) == 0 &&
		    cdefs[i].type == type) {
			sql_xfree(name_str);
			return &cdefs[i];
		}
	}
	sql_xfree(name_str);
	if (name->z[0] == '"')
		return NULL;
	char *old_name_str = sql_legacy_name_new(name->z, name->n);
	for (uint32_t i = 0; i < count; ++i) {
		if (strcmp(cdefs[i].name, old_name_str) == 0 &&
		    cdefs[i].type == type) {
			sql_xfree(old_name_str);
			return &cdefs[i];
		}
	}
	sql_xfree(old_name_str);
	return NULL;
}

const struct tuple_constraint_def *
sql_tuple_fk_by_token(const struct space *space, const struct Token *name)
{
	struct tuple_constraint_def *cdefs = space->def->opts.constraint_def;
	uint32_t count = space->def->opts.constraint_count;
	return sql_constraint_by_token(cdefs, count, CONSTR_FKEY, name);
}

const struct tuple_constraint_def *
sql_tuple_ck_by_token(const struct space *space, const struct Token *name)
{
	struct tuple_constraint_def *cdefs = space->def->opts.constraint_def;
	uint32_t count = space->def->opts.constraint_count;
	return sql_constraint_by_token(cdefs, count, CONSTR_FUNC, name);
}

const struct tuple_constraint_def *
sql_field_fk_by_token(const struct space *space, uint32_t fieldno,
		      const struct Token *name)
{
	struct field_def *field = &space->def->fields[fieldno];
	struct tuple_constraint_def *cdefs = field->constraint_def;
	uint32_t count = field->constraint_count;
	return sql_constraint_by_token(cdefs, count, CONSTR_FKEY, name);
}

const struct tuple_constraint_def *
sql_field_ck_by_token(const struct space *space, uint32_t fieldno,
		      const struct Token *name)
{
	struct field_def *field = &space->def->fields[fieldno];
	struct tuple_constraint_def *cdefs = field->constraint_def;
	uint32_t count = field->constraint_count;
	return sql_constraint_by_token(cdefs, count, CONSTR_FUNC, name);
}

#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
int
sql_fuzz(const char *sql, int bytes_count)
{
	struct Vdbe *stmt;
	if (sql_stmt_compile(sql, bytes_count, NULL, &stmt, NULL) != 0)
		return -1;
	return sqlVdbeFinalize(stmt);
}
#endif /* FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION */
