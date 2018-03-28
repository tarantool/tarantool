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
#include "sql.h"
/*
 * Both Tarantool and SQLite codebases declare Index, hence the
 * workaround below.
 */
#define Index SqliteIndex
#include "sql/sqliteInt.h"
#include "sql/tarantoolInt.h"
#include "sql/vdbeInt.h"
#undef Index

#include "index.h"
#include "info.h"
#include "schema.h"
#include "box.h"
#include "txn.h"
#include "space.h"
#include "space_def.h"
#include "index_def.h"
#include "tuple.h"
#include "fiber.h"
#include "small/region.h"
#include "session.h"
#include "xrow.h"
#include "iproto_constants.h"

static sqlite3 *db = NULL;

static const char nil_key[] = { 0x90 }; /* Empty MsgPack array. */

static const uint32_t default_sql_flags = SQLITE_ShortColNames
					  | SQLITE_EnableTrigger
					  | SQLITE_AutoIndex
					  | SQLITE_RecTriggers
					  | SQLITE_ForeignKeys;

void
sql_init()
{
	default_flags |= default_sql_flags;

	current_session()->sql_flags |= default_sql_flags;

	if (sql_init_db(&db) != SQLITE_OK)
		panic("failed to initialize SQL subsystem");

	assert(db != NULL);
}

void
sql_load_schema()
{
	int rc;
	struct session *user_session = current_session();
	int commit_internal = !(user_session->sql_flags
				& SQLITE_InternChanges);

	assert(db->init.busy == 0);
	db->init.busy = 1;
	db->pSchema = sqlite3SchemaCreate(db);
	rc = sqlite3InitDatabase(db);
	if (rc != SQLITE_OK) {
		sqlite3SchemaClear(db);
		panic("failed to initialize SQL subsystem");
	}
	db->init.busy = 0;
	if (rc == SQLITE_OK && commit_internal)
		sqlite3CommitInternalChanges();
}

void
sql_free()
{
	sqlite3_close(db); db = NULL;
}

sqlite3 *
sql_get()
{
	return db;
}

/*********************************************************************
 * SQLite cursor implementation on top of Tarantool storage API-s.
 *
 * NB: SQLite btree cursor emulation is less than perfect. The problem
 * is that btree cursors are more low-level compared to Tarantool
 * iterators. The 2 most drastic differences being:
 *
 * i. Positioning - sqlite3BtreeMovetoUnpacked(key) moves to a leaf
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
 * ii. Direction  - SQLite cursors are bidirectional while Tarantool
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

const char *tarantoolErrorMessage()
{
	if (diag_is_empty(&fiber()->diag))
		return NULL;
	return box_error_message(box_error_last());
}

int
is_tarantool_error(int rc)
{
	return (rc == SQL_TARANTOOL_ERROR ||
		rc == SQL_TARANTOOL_ITERATOR_FAIL ||
		rc == SQL_TARANTOOL_DELETE_FAIL ||
		rc == SQL_TARANTOOL_INSERT_FAIL);
}

const void *tarantoolSqlite3PayloadFetch(BtCursor *pCur, u32 *pAmt)
{
	assert(pCur->curFlags & BTCF_TaCursor ||
	       pCur->curFlags & BTCF_TEphemCursor);
	assert(pCur->last_tuple != NULL);

	*pAmt = box_tuple_bsize(pCur->last_tuple);
	return tuple_data(pCur->last_tuple);
}

const void *
tarantoolSqlite3TupleColumnFast(BtCursor *pCur, u32 fieldno, u32 *field_size)
{
	assert(pCur->curFlags & BTCF_TaCursor ||
	       pCur->curFlags & BTCF_TEphemCursor);
	assert(pCur->last_tuple != NULL);

	struct tuple_format *format = tuple_format(pCur->last_tuple);
	assert(format->exact_field_count == 0
	       || fieldno < format->exact_field_count);
	if (format->fields[fieldno].offset_slot == TUPLE_OFFSET_SLOT_NIL)
		return NULL;
	const char *field = tuple_field(pCur->last_tuple, fieldno);
	const char *end = field;
	mp_next(&end);
	*field_size = end - field;
	return field;
}

/*
 * Set cursor to the first tuple in given space.
 * It is a simple wrapper around cursor_seek().
 */
int tarantoolSqlite3First(BtCursor *pCur, int *pRes)
{
	if (key_alloc(pCur, sizeof(nil_key)) != 0)
		return SQL_TARANTOOL_ERROR;
	memcpy(pCur->key, nil_key, sizeof(nil_key));
	pCur->iter_type = ITER_GE;
	return cursor_seek(pCur, pRes);
}

/* Set cursor to the last tuple in given space. */
int tarantoolSqlite3Last(BtCursor *pCur, int *pRes)
{
	if (key_alloc(pCur, sizeof(nil_key)) != 0)
		return SQL_TARANTOOL_ERROR;
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
int tarantoolSqlite3Next(BtCursor *pCur, int *pRes)
{
	if (pCur->eState == CURSOR_INVALID) {
		*pRes = 1;
		return SQLITE_OK;
	}
	assert(iterator_direction(pCur->iter_type) > 0);
	return cursor_advance(pCur, pRes);
}

/*
 * Set cursor to the previous entry in ephemeral space.
 * If state of cursor is invalid (e.g. it is still under construction,
 * or already destroyed), it immediately returns.
 */
int tarantoolSqlite3Previous(BtCursor *pCur, int *pRes)
{
	if (pCur->eState == CURSOR_INVALID) {
		*pRes = 1;
		return SQLITE_OK;
	}
	assert(iterator_direction(pCur->iter_type) < 0);
	return cursor_advance(pCur, pRes);
}

int tarantoolSqlite3MovetoUnpacked(BtCursor *pCur, UnpackedRecord *pIdxKey,
				   int *pRes)
{
	int rc, res_success;
	size_t ks;

	ks = sqlite3VdbeMsgpackRecordLen(pIdxKey->aMem, pIdxKey->nField);
	if (key_alloc(pCur, ks) != 0)
		return SQL_TARANTOOL_ERROR;
	sqlite3VdbeMsgpackRecordPut((u8 *)pCur->key, pIdxKey->aMem,
				    pIdxKey->nField);

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
		pCur->iter_type = (pCur->hints & BTREE_SEEK_EQ) ?
				  ITER_REQ : ITER_LE;
		res_success = 0; /* item==key */
		break;
	case OP_SeekGE:
		pCur->iter_type = (pCur->hints & BTREE_SEEK_EQ) ?
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
		 * (last item), SQLite comparator is configured to
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
 * Count number of tuples in ephemeral space and write it to pnEntry.
 *
 * @param pCur Cursor which will point to ephemeral space.
 * @param[out] pnEntry Number of tuples in ephemeral space.
 *
 * @retval SQLITE_OK
 */
int tarantoolSqlite3EphemeralCount(struct BtCursor *pCur, i64 *pnEntry)
{
	assert(pCur->curFlags & BTCF_TEphemCursor);

	struct index *primary_index = space_index(pCur->space, 0 /* PK */);
	*pnEntry = index_size(primary_index);
	return SQLITE_OK;
}

int tarantoolSqlite3Count(BtCursor *pCur, i64 *pnEntry)
{
	assert(pCur->curFlags & BTCF_TaCursor);

	*pnEntry = index_size(pCur->index);
	return SQLITE_OK;
}

/*
 * Create ephemeral space and set cursor to the first entry. Features of
 * ephemeral spaces: id == 0, name == "ephemeral", memtx engine (in future it
 * can be changed, but now only memtx engine is supported), primary index
 * which covers all fields and no secondary indexes, given field number and
 * collation sequence. All fields are scalar and nullable.
 *
 * @param pCur Cursor which will point to the new ephemeral space.
 * @param field_count Number of fields in ephemeral space.
 * @param aColl Collation sequence of ephemeral space.
 *
 * @retval SQLITE_OK on success, SQLITE_TARANTOOL_ERROR otherwise.
 */
int tarantoolSqlite3EphemeralCreate(BtCursor *pCur, uint32_t field_count,
				    struct coll *aColl)
{
	assert(pCur);
	assert(pCur->curFlags & BTCF_TEphemCursor);

	struct key_def *ephemer_key_def = key_def_new(field_count);
	if (ephemer_key_def == NULL)
		return SQL_TARANTOOL_ERROR;
	for (uint32_t part = 0; part < field_count; ++part) {
		key_def_set_part(ephemer_key_def, part /* part no */,
				 part /* filed no */,
				 FIELD_TYPE_SCALAR,
				 ON_CONFLICT_ACTION_NONE /* nullable_action */,
				 aColl /* coll */);
	}

	struct index_def *ephemer_index_def =
		index_def_new(0 /*space id */, 0 /* index id */, "ephemer_idx",
			      strlen("ephemer_idx"), TREE, &index_opts_default,
			      ephemer_key_def, NULL /* pk def */);
	key_def_delete(ephemer_key_def);
	if (ephemer_index_def == NULL)
		return SQL_TARANTOOL_ERROR;

	struct rlist key_list;
	rlist_create(&key_list);
	rlist_add_entry(&key_list, ephemer_index_def, link);

	struct space_def *ephemer_space_def =
		space_def_new(0 /* space id */, 0 /* user id */, field_count,
			      "ephemeral", strlen("ephemeral"),
			      "memtx", strlen("memtx"),
			      &space_opts_default, &field_def_default,
			      0 /* length of field_def */);
	if (ephemer_space_def == NULL) {
		index_def_delete(ephemer_index_def);
		return SQL_TARANTOOL_ERROR;
	}

	struct space *ephemer_new_space = space_new_ephemeral(ephemer_space_def,
							      &key_list);
	index_def_delete(ephemer_index_def);
	space_def_delete(ephemer_space_def);
	if (ephemer_new_space == NULL)
		return SQL_TARANTOOL_ERROR;
	if (key_alloc(pCur, field_count) != 0) {
		space_delete(ephemer_new_space);
		return SQL_TARANTOOL_ERROR;
	}
	pCur->space = ephemer_new_space;
	pCur->index = *ephemer_new_space->index;

	int unused;
	return tarantoolSqlite3First(pCur, &unused);
}

int tarantoolSqlite3EphemeralInsert(struct space *space, const char *tuple,
				    const char *tuple_end)
{
	assert(space != NULL);
	mp_tuple_assert(tuple, tuple_end);
	if (space_ephemeral_replace(space, tuple, tuple_end) != 0)
		return SQL_TARANTOOL_INSERT_FAIL;
	return SQLITE_OK;
}

/* Simply delete ephemeral space calling space_delete_ephemeral(). */
int tarantoolSqlite3EphemeralDrop(BtCursor *pCur)
{
	assert(pCur);
	assert(pCur->curFlags & BTCF_TEphemCursor);
	space_delete_ephemeral(pCur->space);
	pCur->space = NULL;
	return SQLITE_OK;
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
	int rc = box_process_rw(&request, space, NULL);
	return rc == 0 ? SQLITE_OK : SQL_TARANTOOL_INSERT_FAIL;
}

int tarantoolSqlite3Insert(struct space *space, const char *tuple,
			   const char *tuple_end)
{
	return insertOrReplace(space, tuple, tuple_end, IPROTO_INSERT);
}

int tarantoolSqlite3Replace(struct space *space, const char *tuple,
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
 * @retval SQLITE_OK on success, SQLITE_TARANTOOL_ERROR otherwise.
 */
int tarantoolSqlite3EphemeralDelete(BtCursor *pCur)
{
	assert(pCur->curFlags & BTCF_TEphemCursor);
	assert(pCur->iter != NULL);
	assert(pCur->last_tuple != NULL);

	char *key;
	uint32_t key_size;
	key = tuple_extract_key(pCur->last_tuple,
				box_iterator_key_def(pCur->iter),
				&key_size);
	if (key == NULL)
		return SQL_TARANTOOL_DELETE_FAIL;

	int rc = space_ephemeral_delete(pCur->space, key);
	if (rc != 0) {
		diag_log();
		return SQL_TARANTOOL_DELETE_FAIL;
	}
	return SQLITE_OK;
}

int tarantoolSqlite3Delete(BtCursor *pCur, u8 flags)
{
	(void)flags;

	assert(pCur->curFlags & BTCF_TaCursor);
	assert(pCur->iter != NULL);
	assert(pCur->last_tuple != NULL);

	char *key;
	uint32_t key_size;
	int rc;

	key = tuple_extract_key(pCur->last_tuple,
				box_iterator_key_def(pCur->iter),
				&key_size);
	if (key == NULL)
		return SQL_TARANTOOL_DELETE_FAIL;

	rc = sql_delete_by_key(pCur->space, key, key_size);

	return rc == 0 ? SQLITE_OK : SQL_TARANTOOL_DELETE_FAIL;
}

/**
 * Delete entry from space by its key.
 *
 * @param space Space which contains record to be deleted.
 * @param key Key of record to be deleted.
 * @param key_size Size of key.
 *
 * @retval SQLITE_OK on success, SQL_TARANTOOL_DELETE_FAIL otherwise.
 */
int
sql_delete_by_key(struct space *space, char *key, uint32_t key_size)
{
	struct request request;
	struct tuple *unused;
	memset(&request, 0, sizeof(request));
	request.type = IPROTO_DELETE;
	request.key = key;
	request.key_end = key + key_size;
	request.space_id = space->def->id;
	int rc = box_process_rw(&request, space, &unused);

	return rc == 0 ? SQLITE_OK : SQL_TARANTOOL_DELETE_FAIL;
}

/*
 * Delete all tuples from space. It is worth noting, that truncate can't
 * be applied to ephemeral space, so this routine manually deletes
 * tuples one by one.
 *
 * @param pCur Cursor pointing to ephemeral space.
 *
 * @retval SQLITE_OK on success, SQLITE_TARANTOOL_ERROR otherwise.
 */
int tarantoolSqlite3EphemeralClearTable(BtCursor *pCur)
{
	assert(pCur);
	assert(pCur->curFlags & BTCF_TEphemCursor);

	struct iterator *it = index_create_iterator(*pCur->space->index,
						    ITER_ALL, nil_key,
						    0 /* part_count */);
	if (it == NULL) {
		pCur->eState = CURSOR_INVALID;
		return SQL_TARANTOOL_ITERATOR_FAIL;
	}

	struct tuple *tuple;
	char *key;
	uint32_t  key_size;

	while (iterator_next(it, &tuple) == 0 && tuple != NULL) {
		key = tuple_extract_key(tuple, box_iterator_key_def(it),
					&key_size);
		if (space_ephemeral_delete(pCur->space, key) != 0) {
			iterator_delete(it);
			return SQL_TARANTOOL_DELETE_FAIL;
		}
	}
	iterator_delete(it);

	return SQLITE_OK;
}

/*
 * Removes all instances from table.
 * Iterate through the space and delete one by one all tuples.
 */
int tarantoolSqlite3ClearTable(struct space *space)
{
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
		return SQL_TARANTOOL_ITERATOR_FAIL;
	while (iterator_next(iter, &tuple) == 0 && tuple != NULL) {
		request.key = tuple_extract_key(tuple, pk->def->key_def,
						&key_size);
		request.key_end = request.key + key_size;
		rc = box_process_rw(&request, space, &unused);
		if (rc != 0) {
			iterator_delete(iter);
			return SQL_TARANTOOL_DELETE_FAIL;
		}
	}
	iterator_delete(iter);

	return SQLITE_OK;
}

/*
 * Change the statement of trigger in _trigger space.
 * This function is called after tarantoolSqlite3RenameTable,
 * in order to update name of table in create trigger statement.
 */
int tarantoolSqlite3RenameTrigger(const char *trig_name,
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
		return SQL_TARANTOOL_ERROR;
	}
	char *key = mp_encode_array(key_begin, 1);
	key = mp_encode_str(key, trig_name, trig_name_len);
	if (box_index_get(BOX_TRIGGER_ID, 0, key_begin, key, &tuple) != 0)
		return SQL_TARANTOOL_ERROR;
	assert(tuple != NULL);
	assert(tuple_field_count(tuple) == 2);
	const char *field = box_tuple_field(tuple, 1);
	assert(mp_typeof(*field) == MP_MAP);
	mp_decode_map(&field);
	const char *sql_str = mp_decode_str(&field, &key_len);
	if (sqlite3StrNICmp(sql_str, "sql", 3) != 0)
		goto rename_fail;
	uint32_t trigger_stmt_len;
	const char *trigger_stmt_old = mp_decode_str(&field, &trigger_stmt_len);
	char *trigger_stmt = (char*)region_alloc(&fiber()->gc,
						 trigger_stmt_len + 1);
	if (trigger_stmt == NULL) {
		diag_set(OutOfMemory, trigger_stmt_len + 1, "region_alloc",
			 "trigger_stmt");
		return SQL_TARANTOOL_ERROR;
	}
	memcpy(trigger_stmt, trigger_stmt_old, trigger_stmt_len);
	trigger_stmt[trigger_stmt_len] = '\0';
	bool is_quoted = false;
	trigger_stmt = rename_trigger(db, trigger_stmt, new_table_name, &is_quoted);

	uint32_t trigger_stmt_new_len = trigger_stmt_len + old_table_name_len -
					new_table_name_len + 2 * (!is_quoted);
	assert(trigger_stmt_new_len > 0);
	key_len = mp_sizeof_array(2) + mp_sizeof_str(trig_name_len) +
		  mp_sizeof_map(1) + mp_sizeof_str(3) +
		  mp_sizeof_str(trigger_stmt_new_len);
	char *new_tuple = (char*)region_alloc(&fiber()->gc, key_len);
	if (new_tuple == NULL) {
		diag_set(OutOfMemory, key_len, "region_alloc", "new_tuple");
		return SQL_TARANTOOL_ERROR;
	}
	char *new_tuple_end = mp_encode_array(new_tuple, 2);
	new_tuple_end = mp_encode_str(new_tuple_end, trig_name, trig_name_len);
	new_tuple_end = mp_encode_map(new_tuple_end, 1);
	new_tuple_end = mp_encode_str(new_tuple_end, "sql", 3);
	new_tuple_end = mp_encode_str(new_tuple_end, trigger_stmt,
				      trigger_stmt_new_len);

	if (box_replace(BOX_TRIGGER_ID, new_tuple, new_tuple_end, NULL) != 0)
		return SQL_TARANTOOL_ERROR;
	else
		return SQLITE_OK;

rename_fail:
	diag_set(ClientError, ER_SQL_EXECUTE, "can't modify name of space "
		"created not via SQL facilities");
	return SQL_TARANTOOL_ERROR;
}

/*
 * Rename the table in _space. Update tuple with corresponding id with
 * new name and statement fields and insert back. If sql_stmt is NULL,
 * then return from function after getting length of new statement:
 * it is the way how to dynamically allocate memory for new statement in VDBE.
 * So basically this function should be called twice: firstly to get length of
 * CREATE TABLE statement, and secondly to make routine of replacing tuple and
 * filling out param sql_stmt with new CREATE TABLE statement.
 *
 * @param iTab pageno of table to be renamed
 * @param new_name new name of table
 * @param[out] sql_stmt CREATE TABLE statement for new name table, can be NULL.
 *
 * @retval SQLITE_OK on success, SQLITE_TARANTOOL_ERROR otherwise.
 */
int tarantoolSqlite3RenameTable(int iTab, const char *new_name, char **sql_stmt)
{
	assert(iTab > 0);
	assert(new_name);
	assert(sql_stmt);

	int space_id = SQLITE_PAGENO_TO_SPACEID(iTab);
	box_tuple_t *tuple;
	uint32_t key_len = mp_sizeof_uint(space_id) + mp_sizeof_array(1);
	char *key_begin = (char*) region_alloc(&fiber()->gc, key_len);
	if (key_begin == NULL) {
		diag_set(OutOfMemory, key_len, "region_alloc", "key_begin");
		return SQL_TARANTOOL_ERROR;
	}
	char *key = mp_encode_array(key_begin, 1);
	key = mp_encode_uint(key, space_id);
	if (box_index_get(BOX_SPACE_ID, 0, key_begin, key, &tuple) != 0)
		return SQL_TARANTOOL_ERROR;
	assert(tuple != NULL);

	/* Code below relies on format of _space. If number of fields or their
	 * order will ever change, this code should be changed too.
	 */
	assert(tuple_field_count(tuple) == 7);
	const char *sql_stmt_map = box_tuple_field(tuple, 5);

	if (sql_stmt_map == NULL || mp_typeof(*sql_stmt_map) != MP_MAP)
		goto rename_fail;
	uint32_t map_size = mp_decode_map(&sql_stmt_map);
	if (map_size != 1)
		goto rename_fail;
	const char *sql_str = mp_decode_str(&sql_stmt_map, &key_len);

	/* If this table hasn't been created via SQL facilities,
	 * we can't do anything yet.
	 */
	if (sqlite3StrNICmp(sql_str, "sql", 3) != 0)
		goto rename_fail;
	uint32_t sql_stmt_decoded_len;
	const char *sql_stmt_old = mp_decode_str(&sql_stmt_map,
						 &sql_stmt_decoded_len);
	uint32_t old_name_len;
	const char *old_name = box_tuple_field(tuple, 2);
	old_name = mp_decode_str(&old_name, &old_name_len);
	uint32_t new_name_len = strlen(new_name);

	*sql_stmt = (char*)region_alloc(&fiber()->gc, sql_stmt_decoded_len + 1);
	if (*sql_stmt == NULL) {
		diag_set(OutOfMemory, sql_stmt_decoded_len + 1, "region_alloc",
			 "sql_stmt");
		return SQL_TARANTOOL_ERROR;
	}
	memcpy(*sql_stmt, sql_stmt_old, sql_stmt_decoded_len);
	*(*sql_stmt + sql_stmt_decoded_len) = '\0';
	bool is_quoted = false;
	*sql_stmt = rename_table(db, *sql_stmt, new_name, &is_quoted);
	if (*sql_stmt == NULL)
		goto rename_fail;

	/* If old table name isn't quoted, then need to reserve space for quotes. */
	uint32_t  sql_stmt_len = sql_stmt_decoded_len +
				 new_name_len - old_name_len +
				 2 * (!is_quoted);

	assert(sql_stmt_len > 0);
	/* Construct new msgpack to insert to _space.
	 * Since we have changed only name of table and create statement,
	 * there is no need to decode/encode other fields of tuple,
	 * just memcpy constant parts.
	 */
	char *new_tuple = (char*)region_alloc(&fiber()->gc, tuple->bsize +
					      mp_sizeof_str(sql_stmt_len));
	if (new_tuple == NULL) {
		free(*sql_stmt);
		*sql_stmt = NULL;
		diag_set(OutOfMemory,
			 tuple->bsize + mp_sizeof_str(sql_stmt_len),
			 "region_alloc", "new_tuple");
		return SQL_TARANTOOL_ERROR;
	}

	char *new_tuple_end = new_tuple;
	const char *data_begin = tuple_data(tuple);
	const char *data_end = tuple_field(tuple, 2);
	uint32_t data_size = data_end - data_begin;
	memcpy(new_tuple, data_begin, data_size);
	new_tuple_end += data_size;
	new_tuple_end = mp_encode_str(new_tuple_end, new_name, new_name_len);
	data_begin = tuple_field(tuple, 3);
	data_end = tuple_field(tuple, 5);
	data_size = data_end - data_begin;
	memcpy(new_tuple_end, data_begin, data_size);
	new_tuple_end += data_size;
	new_tuple_end = mp_encode_map(new_tuple_end, 1);
	new_tuple_end = mp_encode_str(new_tuple_end, "sql", 3);
	new_tuple_end = mp_encode_str(new_tuple_end, *sql_stmt, sql_stmt_len);
	data_begin = tuple_field(tuple, 6);
	data_end = (char*) tuple + tuple_size(tuple);
	data_size = data_end - data_begin;
	memcpy(new_tuple_end, data_begin, data_size);
	new_tuple_end += data_size;

	if (box_replace(BOX_SPACE_ID, new_tuple, new_tuple_end, NULL) != 0)
		return SQL_TARANTOOL_ERROR;
	else
		return SQLITE_OK;

rename_fail:
	diag_set(ClientError, ER_SQL_EXECUTE, "can't modify name of space "
		"created not via SQL facilities");
	return SQL_TARANTOOL_ERROR;
}

/*
 * Acts almost as tarantoolSqlite3RenameTable, but doesn't change
 * name of table, only statement.
 */
int tarantoolSqlite3RenameParentTable(int iTab, const char *old_parent_name,
				      const char *new_parent_name)
{
	assert(iTab > 0);
	assert(old_parent_name);
	assert(new_parent_name);

	int space_id = SQLITE_PAGENO_TO_SPACEID(iTab);
	box_tuple_t *tuple;
	uint32_t key_len = mp_sizeof_uint(space_id) + mp_sizeof_array(1);

	char *key_begin = (char*) region_alloc(&fiber()->gc, key_len);
	if (key_begin == NULL) {
		diag_set(OutOfMemory, key_len, "region_alloc", "key_begin");
		return SQL_TARANTOOL_ERROR;
	}
	char *key = mp_encode_array(key_begin, 1);
	key = mp_encode_uint(key, space_id);
	if (box_index_get(BOX_SPACE_ID, 0, key_begin, key, &tuple) != 0)
		return SQL_TARANTOOL_ERROR;
	assert(tuple != NULL);

	assert(tuple_field_count(tuple) == 7);
	const char *sql_stmt_map = box_tuple_field(tuple, 5);

	if (sql_stmt_map == NULL || mp_typeof(*sql_stmt_map) != MP_MAP)
		goto rename_fail;
	uint32_t map_size = mp_decode_map(&sql_stmt_map);
	if (map_size != 1)
		goto rename_fail;
	const char *sql_str = mp_decode_str(&sql_stmt_map, &key_len);
	if (sqlite3StrNICmp(sql_str, "sql", 3) != 0)
		goto rename_fail;
	uint32_t create_stmt_decoded_len;
	const char *create_stmt_old = mp_decode_str(&sql_stmt_map,
						    &create_stmt_decoded_len);
	uint32_t old_name_len = strlen(old_parent_name);
	uint32_t new_name_len = strlen(new_parent_name);
	char *create_stmt_new = (char*) region_alloc(&fiber()->gc,
						     create_stmt_decoded_len + 1);
	if (create_stmt_new == NULL) {
		diag_set(OutOfMemory, create_stmt_decoded_len + 1,
			 "region_alloc", "create_stmt_new");
		return SQL_TARANTOOL_ERROR;
	}
	memcpy(create_stmt_new, create_stmt_old, create_stmt_decoded_len);
	create_stmt_new[create_stmt_decoded_len] = '\0';
	uint32_t numb_of_quotes = 0;
	uint32_t numb_of_occurrences = 0;
	create_stmt_new = rename_parent_table(db, create_stmt_new, old_parent_name,
					      new_parent_name, &numb_of_occurrences,
					      &numb_of_quotes);
	uint32_t create_stmt_new_len = create_stmt_decoded_len -
				       numb_of_occurrences *
				       (old_name_len - new_name_len) +
				       2 * numb_of_quotes;
	assert(create_stmt_new_len > 0);

	key_len = tuple->bsize + mp_sizeof_str(create_stmt_new_len);
	char *new_tuple = (char*)region_alloc(&fiber()->gc, key_len);
	if (new_tuple == NULL) {
		sqlite3DbFree(db, create_stmt_new);
		diag_set(OutOfMemory, key_len, "region_alloc", "new_tuple");
		return SQL_TARANTOOL_ERROR;
	}

	char *new_tuple_end = new_tuple;
	const char *data_begin = tuple_data(tuple);
	const char *data_end = tuple_field(tuple, 5);
	uint32_t data_size = data_end - data_begin;
	memcpy(new_tuple, data_begin, data_size);
	new_tuple_end += data_size;
	new_tuple_end = mp_encode_map(new_tuple_end, 1);
	new_tuple_end = mp_encode_str(new_tuple_end, "sql", 3);
	new_tuple_end = mp_encode_str(new_tuple_end, create_stmt_new,
				      create_stmt_new_len);
	sqlite3DbFree(db, create_stmt_new);
	data_begin = tuple_field(tuple, 6);
	data_end = (char*) tuple + tuple_size(tuple);
	data_size = data_end - data_begin;
	memcpy(new_tuple_end, data_begin, data_size);
	new_tuple_end += data_size;

	if (box_replace(BOX_SPACE_ID, new_tuple, new_tuple_end, NULL) != 0)
		return SQL_TARANTOOL_ERROR;
	else
		return SQLITE_OK;

rename_fail:
	diag_set(ClientError, ER_SQL_EXECUTE, "can't modify name of space "
		"created not via SQL facilities");
	return SQL_TARANTOOL_ERROR;
}

/*
 * Performs exactly as extract_key + sqlite3VdbeCompareMsgpack,
 * only faster.
 */
int tarantoolSqlite3IdxKeyCompare(BtCursor *pCur, UnpackedRecord *pUnpacked,
				  int *res)
{
	assert(pCur->curFlags & BTCF_TaCursor);
	assert(pCur->iter != NULL);
	assert(pCur->last_tuple != NULL);

	const box_key_def_t *key_def;
	const struct tuple *tuple;
	const char *base;
	const struct tuple_format *format;
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

	key_def = box_iterator_key_def(pCur->iter);
	n = MIN(pUnpacked->nField, key_def->part_count);
	tuple = pCur->last_tuple;
	base = tuple_data(tuple);
	format = tuple_format(tuple);
	field_map = tuple_field_map(tuple);
	field_count = format->field_count;
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
			if (fieldno >= field_count ||
			    format->fields[fieldno].offset_slot ==
			    TUPLE_OFFSET_SLOT_NIL) {
				/* Outdated field_map. */
				uint32_t j = 0;

				p = field0;
				while (j++ != fieldno)
					mp_next(&p);
			} else {
				p = base + field_map[
					format->fields[fieldno].offset_slot
];
			}
		}
		next_fieldno = fieldno + 1;
		rc = sqlite3VdbeCompareMsgpack(&p, pUnpacked, i);
		if (rc != 0) {
			if (pUnpacked->pKeyInfo->aSortOrder[i]) {
				rc = -rc;
			}
			*res = rc;
			goto out;
		}
	}
	*res = pUnpacked->default_rc;
out:
#ifndef NDEBUG
	/* Sanity check. */
	original_size = region_used(&fiber()->gc);
	key = tuple_extract_key(tuple, key_def, &key_size);
	if (key != NULL) {
		rc = sqlite3VdbeRecordCompareMsgpack((int)key_size, key,
						     pUnpacked);
		region_truncate(&fiber()->gc, original_size);
		assert(rc == *res);
	}
#endif
	return SQLITE_OK;
}

int
tarantoolSqlite3IncrementMaxid(uint64_t *space_max_id)
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
	if (box_process_rw(&request, space_schema, &res) != 0 || res == NULL ||
	    tuple_field_u64(res, 1, space_max_id) != 0)
		return SQL_TARANTOOL_ERROR;
	return SQLITE_OK;
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
	cur->nKey = key_size;
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
 * @retval SQLITE_OK on success, SQLITE_TARANTOOL_ERROR otherwise.
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
		return SQL_TARANTOOL_ITERATOR_FAIL;
	}

	struct iterator *it = index_create_iterator(pCur->index, pCur->iter_type,
						    key, part_count);
	if (it == NULL) {
		pCur->eState = CURSOR_INVALID;
		return SQL_TARANTOOL_ITERATOR_FAIL;
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
 * @retval SQLITE_OK on success, SQLITE_TARANTOOL_ERROR otherwise.
 */
static int
cursor_advance(BtCursor *pCur, int *pRes)
{
	assert(pCur->iter != NULL);

	struct tuple *tuple;
	if (iterator_next(pCur->iter, &tuple) != 0)
		return SQL_TARANTOOL_ITERATOR_FAIL;
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
	return SQLITE_OK;
}

/*********************************************************************
 * Schema support.
 */

/*
 * Manully add objects to SQLite in-memory schema.
 * This is loosely based on sqlite_master row format.
 * @Params
 *   name - object name
 *   id   - SQLITE_PAGENO_FROM_SPACEID_INDEXID(...)
 *          for tables and indices
 *   sql  - SQL statement that created this object
 */
static void
sql_schema_put(InitData *init,
	       const char *name,
	       uint32_t spaceid, uint32_t indexid,
	       const char *sql)
{
	int pageno = SQLITE_PAGENO_FROM_SPACEID_AND_INDEXID(spaceid, indexid);

	char *argv[] = {
		(char *)name,
		(char *)&pageno,
		(char *)sql,
		NULL
	};

	if (init->rc != SQLITE_OK) return;

	sqlite3InitCallback(init, 3, argv, NULL);
}

static int
space_foreach_put_cb(struct space *space, void *udata)
{
	if (space->def->opts.sql == NULL)
		return 0; /* Not SQL space. */
	sql_schema_put((InitData *) udata, space->def->name, space->def->id, 0,
		       space->def->opts.sql);
	for (uint32_t i = 0; i < space->index_count; ++i) {
		struct index_def *def = space_index_def(space, i);
		if (def->opts.sql != NULL) {
			sql_schema_put((InitData *) udata, def->name,
				       def->space_id, def->iid, def->opts.sql);
		}
	}
	return 0;
}

/* Load database schema from Tarantool. */
void tarantoolSqlite3LoadSchema(InitData *init)
{
	box_iterator_t *it;
	box_tuple_t *tuple;

	sql_schema_put(
		init, TARANTOOL_SYS_SCHEMA_NAME,
		BOX_SCHEMA_ID, 0,
		"CREATE TABLE \""TARANTOOL_SYS_SCHEMA_NAME
		"\" (\"key\" TEXT PRIMARY KEY, \"value\")"
	);

	sql_schema_put(
		init, TARANTOOL_SYS_SPACE_NAME,
		BOX_SPACE_ID, 0,
		"CREATE TABLE \""TARANTOOL_SYS_SPACE_NAME
		"\" (\"id\" INT PRIMARY KEY, \"owner\" INT, \"name\" TEXT, "
		"\"engine\" TEXT, \"field_count\" INT, \"opts\", \"format\")"
	);

	sql_schema_put(
		init, TARANTOOL_SYS_INDEX_NAME,
		BOX_INDEX_ID, 0,
		"CREATE TABLE \""TARANTOOL_SYS_INDEX_NAME"\" "
		"(\"id\" INT, \"iid\" INT, \"name\" TEXT, \"type\" TEXT,"
		"\"opts\", \"parts\", PRIMARY KEY (\"id\", \"iid\"))"
	);

	sql_schema_put(
		init, TARANTOOL_SYS_TRIGGER_NAME,
		BOX_TRIGGER_ID, 0,
		"CREATE TABLE \""TARANTOOL_SYS_TRIGGER_NAME"\" ("
		"\"name\" TEXT PRIMARY KEY, \"opts\")"
	);

	sql_schema_put(
		init, TARANTOOL_SYS_TRUNCATE_NAME,
		BOX_TRUNCATE_ID, 0,
		"CREATE TABLE \""TARANTOOL_SYS_TRUNCATE_NAME
		"\" (\"id\" INT PRIMARY KEY, \"count\" INT NOT NULL)"
	);

	sql_schema_put(init, TARANTOOL_SYS_SEQUENCE_NAME, BOX_SEQUENCE_ID, 0,
		       "CREATE TABLE \""TARANTOOL_SYS_SEQUENCE_NAME
		       "\" (\"id\" INT PRIMARY KEY, \"uid\" INT, \"name\" TEXT, \"step\" INT, "
		       "\"max\" INT, \"min\" INT, \"start\" INT, \"cache\" INT, \"cycle\" INT)");

	sql_schema_put(init, TARANTOOL_SYS_SPACE_SEQUENCE_NAME, BOX_SPACE_SEQUENCE_ID, 0,
		       "CREATE TABLE \""TARANTOOL_SYS_SPACE_SEQUENCE_NAME
		       "\" (\"space_id\" INT PRIMARY KEY, \"sequence_id\" INT, \"flag\" INT)");

	sql_schema_put(init, TARANTOOL_SYS_SQL_STAT1_NAME, BOX_SQL_STAT1_ID, 0,
		       "CREATE TABLE \""TARANTOOL_SYS_SQL_STAT1_NAME
			       "\"(\"tbl\" text,"
			       "\"idx\" text,"
			       "\"stat\" not null,"
			       "PRIMARY KEY(\"tbl\", \"idx\"))");

	sql_schema_put(init, TARANTOOL_SYS_SQL_STAT4_NAME, BOX_SQL_STAT4_ID, 0,
		       "CREATE TABLE \""TARANTOOL_SYS_SQL_STAT4_NAME
			       "\"(\"tbl\" text,"
			       "\"idx\" text,"
			       "\"neq\" text,"
			       "\"nlt\" text,"
			       "\"ndlt\" text,"
			       "\"sample\","
			       "PRIMARY KEY(\"tbl\", \"idx\", \"sample\"))");

	/* Read _space */
	if (space_foreach(space_foreach_put_cb, init) != 0) {
		init->rc = SQL_TARANTOOL_ERROR;
		return;
	}

	/* Read _trigger */
	it = box_index_iterator(BOX_TRIGGER_ID, 0, ITER_GE,
				nil_key, nil_key + sizeof(nil_key));

	if (it == NULL) {
		init->rc = SQL_TARANTOOL_ITERATOR_FAIL;
		return;
	}

	while (box_iterator_next(it, &tuple) == 0 && tuple != NULL) {
		const char *field, *ptr;
		char *name, *sql;
		unsigned len;
		assert(tuple_field_count(tuple) == 2);

		field = tuple_field(tuple, 0);
		assert (field != NULL);
		ptr = mp_decode_str(&field, &len);
		name = strndup(ptr, len);

		field = tuple_field(tuple, 1);
		assert (field != NULL);
		mp_decode_array(&field);
		ptr = mp_decode_str(&field, &len);
		assert (strncmp(ptr, "sql", 3) == 0);

		ptr = mp_decode_str(&field, &len);
		sql = strndup(ptr, len);

		sql_schema_put(init, name, 0, 0, sql);

		free(name);
		free(sql);
	}
	box_iterator_free(it);
}

/*********************************************************************
 * Metainformation about available spaces and indices is stored in
 * _space and _index system spaces respectively.
 *
 * SQLite inserts entries in system spaces.
 *
 * The routines below are called during SQL query processing in order to
 * format data for certain fields in _space and _index.
 */

/*
 * Resulting data is of the variable length. Routines are called twice:
 *  1. with a NULL buffer, yielding result size estimation;
 *  2. with a buffer of the estimated size, rendering the result.
 *
 * For convenience, formatting routines use Enc structure to call
 * Enc is either configured to perform size estimation
 * or to render the result.
 */
struct Enc {
	char *(*encode_uint)(char *data, uint64_t num);
	char *(*encode_str)(char *data, const char *str, uint32_t len);
	char *(*encode_bool)(char *data, bool v);
	char *(*encode_array)(char *data, uint32_t len);
	char *(*encode_map)(char *data, uint32_t len);
};

/* no_encode_XXX functions estimate result size */

static char *no_encode_uint(char *data, uint64_t num)
{
	/* MsgPack UINT is encoded in 9 bytes or less */
	(void)num; return data + 9;
}

static char *no_encode_str(char *data, const char *str, uint32_t len)
{
	/* MsgPack STR header is encoded in 5 bytes or less, followed by
	 * the string data. */
	(void)str; return data + 5 + len;
}

static char *no_encode_bool(char *data, bool v)
{
	/* MsgPack BOOL is encoded in 1 byte. */
	(void)v; return data + 1;
}

static char *no_encode_array_or_map(char *data, uint32_t len)
{
	/* MsgPack ARRAY or MAP header is encoded in 5 bytes or less. */
	(void)len; return data + 5;
}

/*
 * If buf==NULL, return Enc that will perform size estimation;
 * otherwize, return Enc that renders results in the provided buf.
 */
static const struct Enc *get_enc(void *buf)
{
	static const struct Enc mp_enc = {
		mp_encode_uint, mp_encode_str, mp_encode_bool,
		mp_encode_array, mp_encode_map
	}, no_enc = {
		no_encode_uint, no_encode_str, no_encode_bool,
		no_encode_array_or_map, no_encode_array_or_map
	};
	return buf ? &mp_enc : &no_enc;
}

/*
 * Convert SQLite affinity value to the corresponding Tarantool type
 * string which is suitable for _index.parts field.
 */
static const char *convertSqliteAffinity(int affinity, bool allow_nulls)
{
	if (allow_nulls || 1) {
		return "scalar";
	}
	switch (affinity) {
	default:
		assert(false);
	case SQLITE_AFF_BLOB:
		return "scalar";
	case SQLITE_AFF_TEXT:
		return "string";
	case SQLITE_AFF_NUMERIC:
	case SQLITE_AFF_REAL:
	  /* Tarantool workaround: to make comparators able to compare, e.g.
	     double and int use generic type. This might be a performance issue.  */
	  /* return "number"; */
		return "scalar";
	case SQLITE_AFF_INTEGER:
	  /* See comment above.  */
	  /* return "integer"; */
		return "scalar";
	}
}

/*
 * Render "format" array for _space entry.
 * Returns result size.
 * If buf==NULL estimate result size.
 *
 * Ex: [{"name": "col1", "type": "integer"}, ... ]
 */
int tarantoolSqlite3MakeTableFormat(Table *pTable, void *buf)
{
	struct Column *aCol = pTable->aCol;
	const struct Enc *enc = get_enc(buf);
	struct SqliteIndex *pk_idx = sqlite3PrimaryKeyIndex(pTable);
	int pk_forced_int = -1;
	char *base = buf, *p;
	int i, n = pTable->nCol;

	p = enc->encode_array(base, n);

	/* If table's PK is single column which is INTEGER, then
	 * treat it as strict type, not affinity.  */
	if (pk_idx && pk_idx->nColumn == 1) {
		int pk = pk_idx->aiColumn[0];
		if (pTable->aCol[pk].type == FIELD_TYPE_INTEGER)
			pk_forced_int = pk;
	}

	for (i = 0; i < n; i++) {
		const char *t;
		struct coll *coll = aCol[i].coll;
		struct field_def *field = &pTable->def->fields[i];
		struct Expr *def = field->default_value_expr;
		int base_len = 4;
		if (coll != NULL)
			base_len += 1;
		if (def != NULL)
			base_len += 1;
		p = enc->encode_map(p, base_len);
		p = enc->encode_str(p, "name", 4);
		p = enc->encode_str(p, aCol[i].zName, strlen(aCol[i].zName));
		p = enc->encode_str(p, "type", 4);
		if (i == pk_forced_int) {
			t = "integer";
		} else {
			t = aCol[i].affinity == SQLITE_AFF_BLOB ? "scalar" :
				convertSqliteAffinity(aCol[i].affinity, aCol[i].notNull == 0);
		}
		p = enc->encode_str(p, t, strlen(t));
		p = enc->encode_str(p, "is_nullable", 11);
		p = enc->encode_bool(p, aCol[i].notNull ==
				     ON_CONFLICT_ACTION_NONE);
		p = enc->encode_str(p, "nullable_action", 15);
		assert(aCol[i].notNull < on_conflict_action_MAX);
		const char *action = on_conflict_action_strs[aCol[i].notNull];
		p = enc->encode_str(p, action, strlen(action));
		if (coll != NULL) {
			p = enc->encode_str(p, "collation", strlen("collation"));
			p = enc->encode_uint(p, coll->id);
		}
		if (def != NULL) {
		        assert((def->flags & EP_IntValue) == 0);
			assert(def->u.zToken != NULL);
			p = enc->encode_str(p, "default", strlen("default"));
			p = enc->encode_str(p, def->u.zToken, strlen(def->u.zToken));
		}
	}
	return (int)(p - base);
}

/*
 * Format "opts" dictionary for _space entry.
 * Returns result size.
 * If buf==NULL estimate result size.
 *
 * Ex: {"temporary": true, "sql": "CREATE TABLE student (name, grade)"}
 */
int tarantoolSqlite3MakeTableOpts(Table *pTable, const char *zSql, void *buf)
{
	(void)pTable;
	const struct Enc *enc = get_enc(buf);
	char *base = buf, *p;

	bool is_view = false;
	if (pTable != NULL)
		is_view = pTable->pSelect != NULL;
	p = enc->encode_map(base, is_view ? 2 : 1);
	p = enc->encode_str(p, "sql", 3);
	p = enc->encode_str(p, zSql, strlen(zSql));
	if (is_view) {
		p = enc->encode_str(p, "view", 4);
		p = enc->encode_bool(p, true);
	}
	return (int)(p - base);
}

/*
 * Format "parts" array for _index entry.
 * Returns result size.
 * If buf==NULL estimate result size.
 *
 * Ex: [[0, "integer"]]
 */
int tarantoolSqlite3MakeIdxParts(SqliteIndex *pIndex, void *buf)
{
	struct Column *aCol = pIndex->pTable->aCol;
	const struct Enc *enc = get_enc(buf);
	struct SqliteIndex *primary_index;
	char *base = buf, *p;
	int pk_forced_int = -1;

	primary_index = sqlite3PrimaryKeyIndex(pIndex->pTable);

	/* If table's PK is single column which is INTEGER, then
	 * treat it as strict type, not affinity.  */
	if (primary_index->nColumn == 1) {
		int pk = primary_index->aiColumn[0];
		if (aCol[pk].type == FIELD_TYPE_INTEGER)
			pk_forced_int = pk;
	}

	/* gh-2187
	 *
	 * Include all index columns, i.e. "key" columns followed by the
	 * primary key columns. Query planner depends on this particular
	 * data layout.
	 */
	int i, n = pIndex->nColumn;

	p = enc->encode_array(base, n);
	for (i = 0; i < n; i++) {
		int col = pIndex->aiColumn[i];
		const char *t;
		if (pk_forced_int == col)
			t = "integer";
		else
			t = convertSqliteAffinity(aCol[col].affinity, aCol[col].notNull == 0);
		/* do not decode default collation */
		p = enc->encode_map(p, pIndex->coll_array[i] == NULL ? 4 : 5);
		p = enc->encode_str(p, "type", sizeof("type")-1);
		p = enc->encode_str(p, t, strlen(t));
		p = enc->encode_str(p, "field", sizeof("field")-1);
		p = enc->encode_uint(p, col);
		if (pIndex->coll_array[i] != NULL) {
			p = enc->encode_str(p, "collation", sizeof("collation")-1);
			p = enc->encode_uint(p, pIndex->coll_array[i]->id);
		}
		p = enc->encode_str(p, "is_nullable", 11);
		p = enc->encode_bool(p, aCol[col].notNull == ON_CONFLICT_ACTION_NONE);
		p = enc->encode_str(p, "nullable_action", 15);
		const char *action_str = on_conflict_action_strs[aCol[col].notNull];
		p = enc->encode_str(p, action_str, strlen(action_str));
	}
	return (int)(p - base);
}

/*
 * Format "opts" dictionary for _index entry.
 * Returns result size.
 * If buf==NULL estimate result size.
 *
 * Ex: {
 *   "unique": "true",
 *   "sql": "CREATE INDEX student_by_name ON students(name)"
 * }
 */
int tarantoolSqlite3MakeIdxOpts(SqliteIndex *index, const char *zSql, void *buf)
{
	const struct Enc *enc = get_enc(buf);
	char *base = buf, *p;

	(void)index;

	p = enc->encode_map(base, 2);
	/* Mark as unique pk and unique indexes */
	p = enc->encode_str(p, "unique", 6);
	/* If user didn't defined ON CONFLICT OPTIONS, all uniqueness checks
	 * will be made by Tarantool. However, Tarantool doesn't have ON
	 * CONFLIT option, so in that case (except ON CONFLICT ABORT, which is
	 * default behavior) uniqueness will be checked by SQL.
	 * INSERT OR REPLACE/IGNORE uniqueness checks will be also done by
	 * Tarantool.
	 */
	p = enc->encode_bool(p, IsUniqueIndex(index));
	p = enc->encode_str(p, "sql", 3);
	p = enc->encode_str(p, zSql, zSql ? strlen(zSql) : 0);
	return (int)(p - base);
}

void
sql_debug_info(struct info_handler *h)
{
	extern int sql_search_count;
	extern int sql_sort_count;
	extern int sql_found_count;
	info_begin(h);
	info_append_int(h, "sql_search_count", sql_search_count);
	info_append_int(h, "sql_sort_count", sql_sort_count);
	info_append_int(h, "sql_found_count", sql_found_count);
	info_end(h);
}

/*
 * Extract maximum integer value from ephemeral space.
 * If index is empty - return 0 in max_id and success status.
 *
 * @param pCur Cursor pointing to ephemeral space.
 * @param fieldno Number of field from fetching tuple.
 * @param[out] max_id Fetched max value.
 *
 * @retval 0 on success, -1 otherwise.
 */
int tarantoolSqlite3EphemeralGetMaxId(BtCursor *pCur, uint32_t fieldno,
				       uint64_t *max_id)
{
	struct space *ephem_space = pCur->space;
	assert(ephem_space);
	struct index *primary_index = *ephem_space->index;

	struct tuple *tuple;
	if (index_max(primary_index, NULL, 0, &tuple) != 0) {
		return SQL_TARANTOOL_ERROR;
	}
	if (tuple == NULL) {
		*max_id = 0;
		return SQLITE_OK;
	}
	if (tuple_field_u64(tuple, fieldno, max_id) == -1)
		return SQL_TARANTOOL_ERROR;

	return SQLITE_OK;
}

int
tarantoolSqlNextSeqId(uint64_t *max_id)
{
	char key[16];
	struct tuple *tuple;
	char *key_end = mp_encode_array(key, 0);
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
