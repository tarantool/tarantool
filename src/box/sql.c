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
#include "fk_constraint.h"
#include "mpstream/mpstream.h"
#include "sql_stmt_cache.h"

static sql *db = NULL;

static const char nil_key[] = { 0x90 }; /* Empty MsgPack array. */

static const uint32_t default_sql_flags = SQL_EnableTrigger
					  | SQL_AutoIndex
					  | SQL_RecTriggers;

void
sql_init(void)
{
	default_flags |= default_sql_flags;

	current_session()->sql_flags |= default_sql_flags;

	if (sql_init_db(&db) != 0)
		panic("failed to initialize SQL subsystem");

	sql_stmt_cache_init();

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

int tarantoolsqlMovetoUnpacked(BtCursor *pCur, UnpackedRecord *pIdxKey,
				   int *pRes)
{
	struct region *region = &fiber()->gc;
	size_t used = region_used(region);
	uint32_t tuple_size;
	const char *tuple =
		sql_vdbe_mem_encode_tuple(pIdxKey->aMem, pIdxKey->nField,
					  &tuple_size, region);
	if (tuple == NULL)
		return -1;
	if (key_alloc(pCur, tuple_size) != 0)
		return -1;
	memcpy(pCur->key, tuple, tuple_size);
	region_truncate(region, used);

	int rc, res_success;
	switch (pIdxKey->opcode) {
	default:
	  /*  "Unexpected opcode" */
		assert(0);
	case 255:
	/* Restore saved state. Just re-seek cursor.
	   TODO: replace w/ named constant.  */
		res_success = 0;
		break;
	case OP_SeekLT:
		pCur->iter_type = ITER_LT;
		res_success = -1; /* item<key */
		break;
	case OP_SeekLE:
		pCur->iter_type = (pCur->hints & OPFLAG_SEEKEQ) != 0 ?
				  ITER_REQ : ITER_LE;
		res_success = 0; /* item==key */
		break;
	case OP_SeekGE:
		pCur->iter_type = (pCur->hints & OPFLAG_SEEKEQ) != 0 ?
				  ITER_EQ : ITER_GE;
		res_success = 0; /* item==key */
		break;
	case OP_SeekGT:
		pCur->iter_type = ITER_GT;
		res_success = 1; /* item>key */
		break;
	case OP_NoConflict:
	case OP_NotFound:
	case OP_Found:
	case OP_IdxDelete:
		pCur->iter_type = ITER_EQ;
		res_success = 0;
		break;
	}
	rc = cursor_seek(pCur, pRes);
	if (*pRes == 0) {
		*pRes = res_success;
		/*
		 * To select the first item in a row of equal items
		 * (last item), sql comparator is configured to
		 * return +1 (-1) if an item equals the key making it
		 * impossible to distinguish from an item>key (item<key)
		 * from comparator output alone.
		 * To make it possible to learn if the current item
		 * equals the key, the comparator sets eqSeen.
		 */
		pIdxKey->eqSeen = 1;
	} else {
		*pRes = -1; /* -1 also means EOF */
	}
	return rc;
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

struct space *
sql_ephemeral_space_create(uint32_t field_count, struct sql_key_info *key_info)
{
	struct key_def *def = NULL;
	uint32_t part_count = field_count;
	if (key_info != NULL) {
		def = sql_key_info_to_key_def(key_info);
		if (def == NULL)
			return NULL;
		/*
		 * In case is_pk_rowid is true we can use rowid
		 * as the only part of the key.
		 */
		if (key_info->is_pk_rowid)
			part_count = 1;
	}

	struct region *region = &fiber()->gc;
	/*
	 * Name of the fields will be "_COLUMN_1", "_COLUMN_2"
	 * and so on. Due to this, length of each name is no more
	 * than strlen("_COLUMN_") plus length of UINT32_MAX
	 * turned to string, which is 10 and plus 1 for \0.
	 */
	uint32_t name_len = strlen("_COLUMN_") + 11;
	uint32_t size = field_count * (sizeof(struct field_def) + name_len) +
			part_count * sizeof(struct key_part_def);
	struct field_def *fields = region_aligned_alloc(region, size,
							alignof(fields[0]));
	if (fields == NULL) {
		diag_set(OutOfMemory, size, "region_aligned_alloc", "fields");
		return NULL;
	}
	struct key_part_def *ephemer_key_parts =
		(void *)fields + field_count * sizeof(struct field_def);
	static_assert(alignof(*fields) == alignof(*ephemer_key_parts),
		      "allocated in one block, and should have the same "
		      "alignment");
	char *names = (char *)ephemer_key_parts +
		       part_count * sizeof(struct key_part_def);
	for (uint32_t i = 0; i < field_count; ++i) {
		struct field_def *field = &fields[i];
		field->name = names;
		names += name_len;
		sprintf(field->name, "_COLUMN_%d", i);
		field->is_nullable = true;
		field->nullable_action = ON_CONFLICT_ACTION_NONE;
		field->default_value = NULL;
		field->default_value_expr = NULL;
		if (def != NULL && i < def->part_count) {
			assert(def->parts[i].type < field_type_MAX);
			field->type = def->parts[i].type;
			field->coll_id = def->parts[i].coll_id;
		} else {
			field->coll_id = COLL_NONE;
			field->type = FIELD_TYPE_SCALAR;
		}
	}

	for (uint32_t i = 0; i < part_count; ++i) {
		struct key_part_def *part = &ephemer_key_parts[i];
		/*
		 * In case we need to save initial order of
		 * inserted into ephemeral space rows we use rowid
		 * as the only part of PK. If ephemeral space has
		 * a rowid, it is always the last column.
		 */
		uint32_t j = i;
		if (key_info != NULL && key_info->is_pk_rowid)
			j = field_count - 1;
		part->fieldno = j;
		part->nullable_action = ON_CONFLICT_ACTION_NONE;
		part->is_nullable = true;
		part->exclude_null = false;
		part->sort_order = SORT_ORDER_ASC;
		part->path = NULL;
		part->type = fields[j].type;
		part->coll_id = fields[j].coll_id;
	}
	struct key_def *ephemer_key_def = key_def_new(ephemer_key_parts,
						      part_count, false);
	if (ephemer_key_def == NULL)
		return NULL;

	struct index_def *ephemer_index_def =
		index_def_new(0, 0, "ephemer_idx", strlen("ephemer_idx"), TREE,
			      &index_opts_default, ephemer_key_def, NULL);
	key_def_delete(ephemer_key_def);
	if (ephemer_index_def == NULL)
		return NULL;

	struct rlist key_list;
	rlist_create(&key_list);
	rlist_add_entry(&key_list, ephemer_index_def, link);

	struct space_def *ephemer_space_def =
		space_def_new_ephemeral(field_count, fields);
	if (ephemer_space_def == NULL) {
		index_def_delete(ephemer_index_def);
		return NULL;
	}

	struct space *ephemer_new_space = space_new_ephemeral(ephemer_space_def,
							      &key_list);
	index_def_delete(ephemer_index_def);
	space_def_delete(ephemer_space_def);

	return ephemer_new_space;
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
	key = tuple_extract_key(pCur->last_tuple,
				pCur->iter->index->def->key_def,
				MULTIKEY_NONE, &key_size);
	if (key == NULL)
		return -1;

	int rc = space_ephemeral_delete(pCur->space, key);
	if (rc != 0) {
		diag_log();
		return -1;
	}
	return 0;
}

int tarantoolsqlDelete(BtCursor *pCur, u8 flags)
{
	(void)flags;

	assert(pCur->curFlags & BTCF_TaCursor);
	assert(pCur->iter != NULL);
	assert(pCur->last_tuple != NULL);

	char *key;
	uint32_t key_size;

	key = tuple_extract_key(pCur->last_tuple,
				pCur->iter->index->def->key_def,
				MULTIKEY_NONE, &key_size);
	if (key == NULL)
		return -1;
	return sql_delete_by_key(pCur->space, pCur->index->def->iid, key,
				 key_size);
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
		key = tuple_extract_key(tuple, it->index->def->key_def,
					MULTIKEY_NONE, &key_size);
		if (space_ephemeral_delete(pCur->space, key) != 0) {
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
		request.key = tuple_extract_key(tuple, pk->def->key_def,
						MULTIKEY_NONE, &key_size);
		request.key_end = request.key + key_size;
		rc = box_process1(&request, &unused);
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
	char *key_begin = (char*) region_alloc(&fiber()->gc, key_len);
	if (key_begin == NULL) {
		diag_set(OutOfMemory, key_len, "region_alloc", "key_begin");
		return -1;
	}
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
	char *trigger_stmt = (char*)region_alloc(&fiber()->gc,
						 trigger_stmt_len + 1);
	if (trigger_stmt == NULL) {
		diag_set(OutOfMemory, trigger_stmt_len + 1, "region_alloc",
			 "trigger_stmt");
		return -1;
	}
	memcpy(trigger_stmt, trigger_stmt_old, trigger_stmt_len);
	trigger_stmt[trigger_stmt_len] = '\0';
	bool is_quoted = false;
	trigger_stmt = rename_trigger(db, trigger_stmt, new_table_name, &is_quoted);

	uint32_t trigger_stmt_new_len = trigger_stmt_len + new_table_name_len -
					old_table_name_len + 2 * (!is_quoted);
	assert(trigger_stmt_new_len > 0);
	key_len = mp_sizeof_array(3) + mp_sizeof_str(trig_name_len) +
		  mp_sizeof_map(1) + mp_sizeof_str(3) +
		  mp_sizeof_str(trigger_stmt_new_len) +
		  mp_sizeof_uint(space_id);
	char *new_tuple = (char*)region_alloc(&fiber()->gc, key_len);
	if (new_tuple == NULL) {
		diag_set(OutOfMemory, key_len, "region_alloc", "new_tuple");
		return -1;
	}
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
	char *raw = (char *) region_alloc(region, size);
	if (raw == NULL) {
		diag_set(OutOfMemory, size, "region_alloc", "raw");
		return -1;
	}
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
	return box_update(BOX_SPACE_ID, 0, raw, ops, ops, pos, 0, NULL);
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
	const uint32_t *field_map;
	uint32_t field_count, next_fieldno = 0;
	const char *p, *field0;
	u32 i, n;
	int rc;
#ifndef NDEBUG
	size_t original_size;
	const char *key;
	uint32_t key_size;
#endif

	key_def = cursor->iter->index->def->key_def;
	n = MIN(unpacked->nField, key_def->part_count);
	tuple = cursor->last_tuple;
	base = tuple_data(tuple);
	format = tuple_format(tuple);
	field_map = tuple_field_map(tuple);
	field_count = tuple_format_field_count(format);
	field0 = base; mp_decode_array(&field0); p = field0;
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
		 */
		uint32_t fieldno = key_def->parts[i].fieldno;

		if (fieldno != next_fieldno) {
			struct tuple_field *field =
				tuple_format_field(format, fieldno);
			if (fieldno >= field_count ||
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
		rc = sqlVdbeCompareMsgpack(&p, unpacked, i);
		if (rc != 0) {
			if (unpacked->key_def->parts[i].sort_order !=
			    SORT_ORDER_ASC)
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

int
tarantoolsqlIncrementMaxid(uint64_t *space_max_id)
{
	/* ["max_id"] */
	static const char key[] = {
		(char)0x91, /* MsgPack array(1) */
		(char)0xa6, /* MsgPack string(6) */
		'm', 'a', 'x', '_', 'i', 'd'
	};
	/* [["+", 1, 1]]*/
	static const char ops[] = {
		(char)0x91, /* MsgPack array(1) */
		(char)0x93, /* MsgPack array(3) */
		(char)0xa1, /* MsgPack string(1) */
		'+',
		1,          /* MsgPack int(1) */
		1           /* MsgPack int(1) */
	};

	struct tuple *res = NULL;
	struct space *space_schema = space_by_id(BOX_SCHEMA_ID);
	assert(space_schema != NULL);
	struct request request;
	memset(&request, 0, sizeof(request));
	request.tuple = ops;
	request.tuple_end = ops + sizeof(ops);
	request.key = key;
	request.key_end = key + sizeof(key);
	request.type = IPROTO_UPDATE;
	request.space_id = space_schema->def->id;
	if (box_process1(&request, &res) != 0 || res == NULL ||
	    tuple_field_u64(res, 1, space_max_id) != 0)
		return -1;
	return 0;
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
	if (it == NULL) {
		if (txn != NULL)
			txn_rollback_stmt(txn);
		pCur->eState = CURSOR_INVALID;
		return -1;
	}
	if (txn != NULL)
		txn_commit_ro_stmt(txn, &svp);
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
		const char *default_str = field->default_value;
		int base_len = 4;
		if (cid != COLL_NONE)
			base_len += 1;
		if (default_str != NULL)
			base_len += 1;
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
		if (default_str != NULL) {
			mpstream_encode_str(&stream, "default");
			mpstream_encode_str(&stream, default_str);
		}
	}
	mpstream_flush(&stream);
	if (is_error) {
		diag_set(OutOfMemory, stream.pos - stream.buf,
			"mpstream_flush", "stream");
		return NULL;
	}
	*size = region_used(region) - used;
	char *raw = region_join(region, *size);
	if (raw == NULL)
		diag_set(OutOfMemory, *size, "region_join", "raw");
	return raw;
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
	char *raw = region_join(region, *size);
	if (raw == NULL)
		diag_set(OutOfMemory, *size, "region_join", "raw");
	return raw;
}

char *
fk_constraint_encode_links(struct region *region, const struct fk_constraint_def *def,
			   int type, uint32_t *size)
{
	size_t used = region_used(region);
	struct mpstream stream;
	bool is_error = false;
	mpstream_init(&stream, region, region_reserve_cb, region_alloc_cb,
		      set_encode_error, &is_error);
	uint32_t field_count = def->field_count;
	mpstream_encode_array(&stream, field_count);
	for (uint32_t i = 0; i < field_count && !is_error; ++i)
		mpstream_encode_uint(&stream, def->links[i].fields[type]);
	mpstream_flush(&stream);
	if (is_error) {
		diag_set(OutOfMemory, stream.pos - stream.buf,
			 "mpstream_flush", "stream");
		return NULL;
	}
	*size = region_used(region) - used;
	char *raw = region_join(region, *size);
	if (raw == NULL)
		diag_set(OutOfMemory, *size, "region_join", "raw");
	return raw;
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
	char *raw = region_join(region, *size);
	if (raw == NULL)
		diag_set(OutOfMemory, *size, "region_join", "raw");
	return raw;
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
	char *raw = region_join(region, *size);
	if (raw == NULL)
		diag_set(OutOfMemory, *size, "region_join", "raw");
	return raw;
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

int
tarantoolSqlNextSeqId(uint64_t *max_id)
{
	char key[1];
	struct tuple *tuple;
	char *key_end = mp_encode_array(key, 0);
	assert(key_end - key == 1);
	if (box_index_max(BOX_SEQUENCE_ID, 0 /* PK */, key,
			  key_end, &tuple) != 0)
		return -1;

	/* Index is empty  */
	if (tuple == NULL) {
		*max_id = 0;
		return 0;
	}

	return tuple_field_u64(tuple, BOX_SEQUENCE_FIELD_ID, max_id);
}

struct Expr*
space_column_default_expr(uint32_t space_id, uint32_t fieldno)
{
	struct space *space;
	space = space_cache_find(space_id);
	assert(space != NULL);
	assert(space->def != NULL);
	if (space->def->opts.is_view)
		return NULL;
	assert(space->def->field_count > fieldno);

	return space->def->fields[fieldno].default_value_expr;
}

/**
 * Create and initialize a new ephemeral space_def object.
 * @param parser SQL Parser object.
 * @param name Name of space to be created.
 * @retval NULL on memory allocation error, Parser state changed.
 * @retval not NULL on success.
 */
static struct space_def *
sql_ephemeral_space_def_new(struct Parse *parser, const char *name)
{
	struct space_def *def = NULL;
	size_t name_len = name != NULL ? strlen(name) : 0;
	uint32_t dummy;
	size_t size = space_def_sizeof(name_len, NULL, 0, &dummy, &dummy,
				       &dummy);
	def = (struct space_def *)region_aligned_alloc(&parser->region, size,
						       alignof(*def));
	if (def == NULL) {
		diag_set(OutOfMemory, size, "region_aligned_alloc",
			 "sql_ephemeral_space_def_new");
		parser->is_aborted = true;
		return NULL;
	}

	memset(def, 0, size);
	memcpy(def->name, name, name_len);
	def->name[name_len] = '\0';
	def->opts.is_ephemeral = true;
	return def;
}

struct space *
sql_ephemeral_space_new(Parse *parser, const char *name)
{
	size_t sz;
	struct space *space = region_alloc_object(&parser->region,
						  typeof(*space), &sz);
	if (space == NULL) {
		diag_set(OutOfMemory, sz, "region_alloc_object", "space");
		parser->is_aborted = true;
		return NULL;
	}

	memset(space, 0, sz);
	space->def = sql_ephemeral_space_def_new(parser, name);
	if (space->def == NULL)
		return NULL;

	return space;
}

/**
 * Initialize a new vdbe_field_ref instance with given tuple
 * data.
 * @param field_ref The vdbe_field_ref instance to initialize.
 * @param tuple The tuple object pointer or NULL when undefined.
 * @param data The tuple data (is always defined).
 * @param data_sz The size of tuple data (is always defined).
 */
static void
vdbe_field_ref_create(struct vdbe_field_ref *field_ref, struct tuple *tuple,
		      const char *data, uint32_t data_sz)
{
	field_ref->tuple = tuple;
	field_ref->data = data;
	field_ref->data_sz = data_sz;

	const char *field0 = data;
	field_ref->field_count = mp_decode_array((const char **) &field0);
	field_ref->slots[0] = (uint32_t)(field0 - data);
	memset(&field_ref->slots[1], 0,
	       field_ref->field_count * sizeof(field_ref->slots[0]));
	field_ref->slot_bitmask = 0;
	bitmask64_set_bit(&field_ref->slot_bitmask, 0);
}

void
vdbe_field_ref_prepare_data(struct vdbe_field_ref *field_ref, const char *data,
			    uint32_t data_sz)
{
	vdbe_field_ref_create(field_ref, NULL, data, data_sz);
}

void
vdbe_field_ref_prepare_tuple(struct vdbe_field_ref *field_ref,
			     struct tuple *tuple)
{
	vdbe_field_ref_create(field_ref, tuple, tuple_data(tuple),
			      tuple->bsize);
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
const log_est_t default_tuple_est[] = {DEFAULT_TUPLE_LOG_COUNT,
/**                  [10*log_{2}(x)]:  10, 9,  8,  7,  6,  5 */
				       33, 32, 30, 28, 26, 23};

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

log_est_t
index_field_tuple_est(const struct index_def *idx_def, uint32_t field)
{
	assert(idx_def != NULL);
	struct space *space = space_by_id(idx_def->space_id);
	if (space == NULL || strcmp(idx_def->name, "fake_autoindex") == 0)
		return idx_def->opts.stat->tuple_log_est[field];
	assert(field <= idx_def->key_def->part_count);
	/* Statistics is held only in real indexes. */
	struct index *tnt_idx = space_index(space, idx_def->iid);
	assert(tnt_idx != NULL);
	if (tnt_idx->def->opts.stat == NULL) {
		/*
		 * Last number for unique index is always 0:
		 * only one tuple exists with given full key
		 * in unique index and log(1) == 0.
		 */
		if (field == idx_def->key_def->part_count &&
		    idx_def->opts.is_unique)
			return 0;
		return default_tuple_est[field + 1 >= 6 ? 6 : field];
	}
	return tnt_idx->def->opts.stat->tuple_log_est[field];
}
