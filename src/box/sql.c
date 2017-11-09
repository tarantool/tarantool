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
#include "sql.h"
#include "sql/sqlite3.h"

/*
 * Both Tarantool and SQLite codebases declare Index, hence the
 * workaround below.
 */
#define Index SqliteIndex
#include "sql/sqliteInt.h"
#include "sql/btreeInt.h"
#include "sql/tarantoolInt.h"
#include "sql/vdbeInt.h"
#undef Index

#include "index.h"
#include "info.h"
#include "schema.h"
#include "box.h"
#include "txn.h"
#include "space_def.h"
#include "index_def.h"
#include "tuple.h"
#include "fiber.h"
#include "small/region.h"
#include "session.h"

static sqlite3 *db;

static const char nil_key[] = { 0x90 }; /* Empty MsgPack array. */

static const uint32_t default_sql_flags = SQLITE_ShortColNames
					  | SQLITE_EnableTrigger
					  | SQLITE_AutoIndex
					  | SQLITE_RecTriggers
					  | SQLITE_ForeignKeys;

void
sql_init()
{
	int rc;
	char *zErrMsg = 0;

	default_flags |= default_sql_flags;

	rc = sqlite3_open("", &db);
	if (rc != SQLITE_OK) {
		panic("failed to initialize SQL subsystem");
	} else {
		/* XXX */
	}

	current_session()->sql_flags |= default_sql_flags;

	rc = sqlite3Init(db, &zErrMsg);
	if (rc != SQLITE_OK) {
		panic(zErrMsg);
	} else {
		/* XXX */
	}

	assert(db != NULL);
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
 * See the corresponding SQLite function in btree.c for documentation.
 * Ex: sqlite3BtreeCloseCursor -> tarantoolSqlite3CloseCursor
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

/*
 * Tarantool iterator API was apparently designed by space aliens.
 * This wrapper is necessary for interfacing with the SQLite btree code.
 */
struct ta_cursor {
	size_t             size;
	box_iterator_t    *iter;
	struct tuple      *tuple_last;
	enum iterator_type type;
	char               key[1];
};

static struct ta_cursor *
cursor_create(struct ta_cursor *c, size_t key_size);

static int
cursor_seek(BtCursor *pCur, int *pRes, enum iterator_type type,
	    const char *k, const char *ke);

static int
cursor_advance(BtCursor *pCur, int *pRes);

const char *tarantoolErrorMessage()
{
	return box_error_message(box_error_last());
}

int tarantoolSqlite3CloseCursor(BtCursor *pCur)
{
	assert(pCur->curFlags & BTCF_TaCursor);

	struct ta_cursor *c = pCur->pTaCursor;

	pCur->pTaCursor = NULL;

	if (c) {
	if (c->iter) box_iterator_free(c->iter);
	if (c->tuple_last) box_tuple_unref(c->tuple_last);
	    free(c);
	}
	return SQLITE_OK;
}

const void *tarantoolSqlite3PayloadFetch(BtCursor *pCur, u32 *pAmt)
{
	assert(pCur->curFlags & BTCF_TaCursor);

	struct ta_cursor *c = pCur->pTaCursor;

	assert(c);
	assert(c->tuple_last);

	*pAmt = box_tuple_bsize(c->tuple_last);
	return tuple_data(c->tuple_last);
}

const void *
tarantoolSqlite3TupleColumnFast(BtCursor *pCur, u32 fieldno, u32 *field_size)
{
	assert((pCur->curFlags & BTCF_TaCursor) != 0);
	struct ta_cursor *c = pCur->pTaCursor;
	assert(c != NULL);
	assert(c->tuple_last != NULL);
	struct tuple_format *format = tuple_format(c->tuple_last);
	assert(fieldno < format->field_count);
	if (format->fields[fieldno].offset_slot == TUPLE_OFFSET_SLOT_NIL)
		return NULL;
	const char *field = tuple_field(c->tuple_last, fieldno);
	const char *end = field;
	mp_next(&end);
	*field_size = end - field;
	return field;
}

int tarantoolSqlite3First(BtCursor *pCur, int *pRes)
{
	return cursor_seek(pCur, pRes, ITER_GE,
			   nil_key, nil_key + sizeof(nil_key));
}

int tarantoolSqlite3Last(BtCursor *pCur, int *pRes)
{
	return cursor_seek(pCur, pRes, ITER_LE,
			   nil_key, nil_key + sizeof(nil_key));
}

int tarantoolSqlite3Next(BtCursor *pCur, int *pRes)
{
	assert(pCur->curFlags & BTCF_TaCursor);
	if (pCur->eState == CURSOR_INVALID) {
	  *pRes = 1;
	return SQLITE_OK;
	}
	assert(pCur->pTaCursor);
	assert(iterator_direction(
		((struct ta_cursor *)pCur->pTaCursor)->type
	) > 0);
	return cursor_advance(pCur, pRes);
}

int tarantoolSqlite3Previous(BtCursor *pCur, int *pRes)
{
	assert(pCur->curFlags & BTCF_TaCursor);
	if (pCur->eState == CURSOR_INVALID) {
	  *pRes = 1;
	return SQLITE_OK;
	}
	assert(pCur->pTaCursor);
	assert(iterator_direction(
		((struct ta_cursor *)pCur->pTaCursor)->type
	) < 0);
	return cursor_advance(pCur, pRes);
}

int tarantoolSqlite3MovetoUnpacked(BtCursor *pCur, UnpackedRecord *pIdxKey,
				   int *pRes)
{
	int rc, res_success;
	size_t ks;
	const char *k, *ke;
	enum iterator_type iter_type;

	ks = sqlite3VdbeMsgpackRecordLen(pIdxKey->aMem,
					 pIdxKey->nField);
	k = region_reserve(&fiber()->gc, ks);
	if (k == NULL) return SQLITE_NOMEM;
	ke = k + sqlite3VdbeMsgpackRecordPut((u8 *)k, pIdxKey->aMem,
					     pIdxKey->nField);

	switch (pIdxKey->opcode) {
	default:
	  /*  "Unexpected opcode" */
		assert(0);
	case 255:
	/* Restore saved state. Just re-seek cursor.
	   TODO: replace w/ named constant.  */
		iter_type = ((struct ta_cursor *)pCur->pTaCursor)->type;
		res_success = 0;
		break;
	case OP_SeekLT:
		iter_type = ITER_LT;
		res_success = -1; /* item<key */
		break;
	case OP_SeekLE:
		iter_type = (pCur->hints & BTREE_SEEK_EQ) ?
			    ITER_REQ : ITER_LE;
		res_success = 0; /* item==key */
		break;
	case OP_SeekGE:
		iter_type = (pCur->hints & BTREE_SEEK_EQ) ?
			    ITER_EQ : ITER_GE;
		res_success = 0; /* item==key */
		break;
	case OP_SeekGT:
		iter_type = ITER_GT;
		res_success = 1; /* item>key */
		break;
	case OP_NoConflict:
	case OP_NotFound:
	case OP_Found:
	case OP_IdxDelete:
		iter_type = ITER_EQ;
		res_success = 0;
		break;
	}
	rc = cursor_seek(pCur, pRes, iter_type, k, ke);
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

int tarantoolSqlite3Count(BtCursor *pCur, i64 *pnEntry)
{
	assert(pCur->curFlags & BTCF_TaCursor);

	uint32_t space_id = SQLITE_PAGENO_TO_SPACEID(pCur->pgnoRoot);
	uint32_t index_id = SQLITE_PAGENO_TO_INDEXID(pCur->pgnoRoot);
	*pnEntry = box_index_len(space_id, index_id);
	return SQLITE_OK;
}

int tarantoolSqlite3Insert(BtCursor *pCur, const BtreePayload *pX)
{
	assert(pCur->curFlags & BTCF_TaCursor);

	char *buf = (char*)region_alloc(&fiber()->gc, pX->nKey);
	if (buf == NULL) {
		diag_set(OutOfMemory, pX->nKey, "malloc", "buf");
		return SQLITE_TARANTOOL_ERROR;
	}

	memcpy(buf, pX->pKey, pX->nKey);
	if (box_replace(SQLITE_PAGENO_TO_SPACEID(pCur->pgnoRoot),
			buf, (const char *)buf + pX->nKey,
			NULL)
	    != 0) {
		return SQLITE_TARANTOOL_ERROR;
	}
	return SQLITE_OK;
}

int tarantoolSqlite3Delete(BtCursor *pCur, u8 flags)
{
	(void)flags;

	assert(pCur->curFlags & BTCF_TaCursor);

	struct ta_cursor *c = pCur->pTaCursor;
	uint32_t space_id, index_id;
	char *key;
	uint32_t key_size;
	int rc;

	assert(c);
	assert(c->iter);
	assert(c->tuple_last);

	space_id = SQLITE_PAGENO_TO_SPACEID(pCur->pgnoRoot);
	index_id = SQLITE_PAGENO_TO_INDEXID(pCur->pgnoRoot);
	key = tuple_extract_key(c->tuple_last,
				box_iterator_key_def(c->iter),
				&key_size);
	if (key == NULL)
		return SQLITE_TARANTOOL_ERROR;

	rc = box_delete(space_id, index_id, key, key + key_size, NULL);

	return rc == 0 ? SQLITE_OK : SQLITE_TARANTOOL_ERROR;
}

/*
 * Removes all instances from table. If there is no active transaction,
 * then truncate is used. Otherwise, manually deletes one-by-one all tuples.
 */
int tarantoolSqlite3ClearTable(int iTable)
{
	int space_id = SQLITE_PAGENO_TO_SPACEID(iTable);

	if (box_txn()) {
		int primary_index_id = 0;
		char *key;
		uint32_t key_size;
		box_tuple_t *tuple;
		int rc;
		box_iterator_t *iter;
		iter = box_index_iterator(space_id, primary_index_id, ITER_ALL,
					  nil_key, nil_key + sizeof(nil_key));
		if (iter == NULL)
			return SQLITE_TARANTOOL_ERROR;
		while (box_iterator_next(iter, &tuple) == 0 && tuple != NULL) {
			key = tuple_extract_key(tuple,
						box_iterator_key_def(iter),
						&key_size);
			rc = box_delete(space_id, primary_index_id, key,
					key + key_size, NULL);
			if (rc != 0)
				return SQLITE_TARANTOOL_ERROR;
		}
		box_iterator_free(iter);
	} else if (box_truncate(space_id) != 0) {
		return SQLITE_TARANTOOL_ERROR;
	}

	return SQLITE_OK;
}

/*
 * Function is used for modifying SQL statement, which creates table or trigger.
 * This routine finds old table name in statement and replaces it
 * with the new one. Lengths of stings are necessary for the reason that
 * strings of SQL statement and name, which come from _space
 * are not null-terminated. If original statement contains table name without
 * quotes, then add them. It is requited due to further use of this statement
 * in callback function, which creates table.
 *
 * It is passed six arguments:
 *
 *   1) The complete text of the CREATE TABLE statement being modified, and its length.
 *   2) The old name of the table being renamed, and its length.
 *   3) The new name of the table being renamed, ant its length.
 *
 * Example of usage (lengths are omitted):
 * replace_table_name('CREATE TABLE abc(a, b, c)', 'abc', 'def')
 * 	-> 'CREATE TABLE "def"(a, b, c)'
 *
 * replace_table_name('CREATE TABLE abc(id, b, FOREIGN KEY(b) REFERENCES t2(id))',
 * 		      't2', 'def')
 * 	-> 'CREATE TABLE abc(id, b, FOREIGN KEY(b) REFERENCES "def"(id)'
 */
static uint32_t
replace_table_name(char* create_stmt, uint32_t create_stmt_len,
		   const char* old_name, uint32_t old_name_len,
		   const char* new_name, uint32_t new_name_len)
{
	assert(create_stmt);
	assert(old_name);
	assert(new_name);
	assert(create_stmt_len > old_name_len);
	char *ptr = create_stmt;
	char *sub_ptr = (char*) old_name;
	uint8_t is_quoted;
	uint32_t i, j;
	for (i = 0; i <= (create_stmt_len - old_name_len); i++) {
		while (i < (create_stmt_len - old_name_len)
		       && *(ptr+i) != ' ') i++;
		i++;
		is_quoted = 0;
		if (*(ptr+i) == '\"') {
			is_quoted = 1;
			i++;
		}
		for (j = 0; j < old_name_len; j++) {
			/*
			 * SQL statement which creates table, is held in _space
			 * table as it was entered by user, without
			 * any modification. In contrast, table name to be
			 * changed will come in upper-case, if it isn't quoted.
			 */
			if(is_quoted) {
				if (*(ptr+i+j) != *(sub_ptr+j)) break;
			} else {
				if (sqlite3Toupper(*(ptr+i+j)) != *(sub_ptr+j)) break;
			}
		}
		/*
		 * Make sure that it is not a substring:
		 * the next symbol has to be '(', '"' or ' ',
		 * which are the only tokens allowed to be after table name.
		 * Then construct new statement in temporary buffer,
		 * add quotes, if necessary and copy it back.
		 */
		if (j == old_name_len && (*(ptr+i+j) == '(' ||
		    *(ptr+i+j) == '\"'|| *(ptr+i+j) == ' ')) {

			char* temp_buf = (char*)region_alloc(&fiber()->gc,
							     create_stmt_len +
							     new_name_len + 2);
			uint8_t quotes = 0;
			memcpy(temp_buf, create_stmt, i);
			if (!is_quoted) {
				temp_buf[i] =  '\"';
				quotes++;
			}
			memcpy(temp_buf + i + quotes, new_name, new_name_len);
			if (!is_quoted) {
				temp_buf[i+new_name_len+quotes] = '\"';
				quotes++;
			}
			uint32_t full_len = create_stmt_len + new_name_len -
					    old_name_len + quotes;
			memcpy(temp_buf + i + new_name_len + quotes,
			       create_stmt + i + old_name_len,
			       create_stmt_len - i - old_name_len);
			memcpy(create_stmt, temp_buf, full_len);
			create_stmt[full_len] = '\0';
			return full_len;
		}
	}
	create_stmt[create_stmt_len] = '\0';
	return 0;
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
	int rc;
	uint32_t trig_name_len = strlen(trig_name);
	uint32_t old_table_name_len = strlen(old_table_name);
	uint32_t new_table_name_len = strlen(new_table_name);
	char *key_begin = (char*) region_alloc(&fiber()->gc,
					       mp_sizeof_str(trig_name_len) +
					       mp_sizeof_array(1));
	char *key = mp_encode_array(key_begin, 1);
	key = mp_encode_str(key, trig_name, trig_name_len);

	box_iterator_t *iter = box_index_iterator(BOX_TRIGGER_ID, 0, ITER_EQ,
						  key_begin, key);
	if (box_iterator_next(iter, &tuple) != 0 || tuple == 0) {
		return SQLITE_TARANTOOL_ERROR;
	}

	assert(tuple_field_count(tuple) == 2);
	const char *field = box_tuple_field(tuple, 1);
	assert(mp_typeof(*field) == MP_MAP);
	mp_decode_map(&field);
	uint32_t key_len;
	const char *sql_str = mp_decode_str(&field, &key_len);
	assert(sqlite3StrNICmp(sql_str, "sql", 3));
	(void)sql_str;
	uint32_t trigger_stmt_len;
	const char *trigger_stmt_old = mp_decode_str(&field, &trigger_stmt_len);
	char *trigger_stmt = (char*)region_alloc(&fiber()->gc,
						 trigger_stmt_len +
						 strlen(new_table_name) + 2);

	memcpy(trigger_stmt, trigger_stmt_old, trigger_stmt_len);

	uint32_t trigger_stmt_new_len = replace_table_name(trigger_stmt,
							   trigger_stmt_len,
							   old_table_name,
							   old_table_name_len,
							   new_table_name,
							   new_table_name_len);
	assert(trigger_stmt_new_len > 0);
	char *new_tuple = (char*)region_alloc(&fiber()->gc, mp_sizeof_array(2) +
					      mp_sizeof_str(trig_name_len) +
					      mp_sizeof_map(1) +
					      mp_sizeof_str(3) +
					      mp_sizeof_str(trigger_stmt_new_len));
	char *new_tuple_end = mp_encode_array(new_tuple, 2);
	new_tuple_end = mp_encode_str(new_tuple_end, trig_name, trig_name_len);
	new_tuple_end = mp_encode_map(new_tuple_end, 1);
	new_tuple_end = mp_encode_str(new_tuple_end, "sql", 3);
	new_tuple_end = mp_encode_str(new_tuple_end, trigger_stmt,
				      trigger_stmt_new_len);

	rc = box_replace(BOX_TRIGGER_ID, new_tuple, new_tuple_end, &tuple);

	if (rc != 0 || tuple == NULL) {
		return SQLITE_TARANTOOL_ERROR;
	}
	box_iterator_free(iter);
	return SQLITE_OK;
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
 * @param[out] sql_stmt_len length of new CREATE TABLE statement
 *
 * @retval SQLITE_OK on success, SQLITE_TARANTOOL_ERROR otherwise.
 */
int tarantoolSqlite3RenameTable(int iTab, const char *new_name,
				char *sql_stmt, uint32_t *sql_stmt_len)
{
	assert(iTab > 0);
	assert(new_name);
	assert(sql_stmt_len);

	int space_id = SQLITE_PAGENO_TO_SPACEID(iTab);
	box_tuple_t *tuple;
	int rc;

	char *key_begin = (char*) region_alloc(&fiber()->gc,
					       mp_sizeof_uint(space_id) +
					       mp_sizeof_array(1));
	char *key = mp_encode_array(key_begin, 1);
	key = mp_encode_uint(key, space_id);

	box_iterator_t *iter = box_index_iterator(BOX_SPACE_ID, 0, ITER_EQ,
						  key_begin, key);

	if (box_iterator_next(iter, &tuple) != 0 || tuple == 0) {
		return SQLITE_TARANTOOL_ERROR;
	}
	/* Code below relies on format of _space. If number of fields or their
	 * order will ever change, this code should be changed too.
	 */
	assert(tuple_field_count(tuple) == 7);
	const char *sql_stmt_map = box_tuple_field(tuple, 5);

	if (sql_stmt_map == NULL || mp_typeof(*sql_stmt_map) != MP_MAP) {
		return SQLITE_TARANTOOL_ERROR;
	}
	uint32_t map_size = mp_decode_map(&sql_stmt_map);
	assert(map_size == 1);
	(void)map_size;
	uint32_t key_len;
	const char *sql_str = mp_decode_str(&sql_stmt_map, &key_len);
	assert(sqlite3StrNICmp(sql_str, "sql", 3));
	(void)sql_str;
	uint32_t sql_stmt_decoded_len;
	const char *sql_stmt_old = mp_decode_str(&sql_stmt_map,
						 &sql_stmt_decoded_len);
	uint32_t old_name_len;
	const char *old_name = box_tuple_field(tuple, 2);
	old_name = mp_decode_str(&old_name, &old_name_len);
	uint32_t new_name_len = strlen(new_name);

	if (sql_stmt == NULL) {
		/* sql_stmt_decoded includes old_name, so the difference of
		 * their lengths can't be negative.
		 */
		uint32_t new_sql_stmt_len = sql_stmt_decoded_len +
					    new_name_len - old_name_len + 2;
		/* Since firstly sql_stmt_old should be copied to sql_stmt,
		 * the length has to be max(stmt_old, stmt_new).
		 */
		*sql_stmt_len = new_sql_stmt_len > sql_stmt_decoded_len ?
				new_sql_stmt_len : sql_stmt_decoded_len;
		return SQLITE_OK;
	}

	memcpy(sql_stmt, sql_stmt_old, sql_stmt_decoded_len);
	*sql_stmt_len = replace_table_name(sql_stmt, sql_stmt_decoded_len,
					   old_name, old_name_len,
					   new_name, new_name_len);
	assert(*sql_stmt_len > 0);
	/* Construct new msgpack to insert to _space.
	 * Since we have changed only name of table and create statement,
	 * there is no need to decode/encode other fields of tuple,
	 * just memcpy constant parts.
	 */
	char *new_tuple = (char*)region_alloc(&fiber()->gc, tuple->bsize +
					      mp_sizeof_str(new_name_len) +
					      mp_sizeof_str(*sql_stmt_len));

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
	new_tuple_end = mp_encode_str(new_tuple_end, sql_stmt,
				      *sql_stmt_len);
	data_begin = tuple_field(tuple, 6);
	data_end = (char*) tuple + tuple_size(tuple);
	data_size = data_end - data_begin;
	memcpy(new_tuple_end, data_begin, data_size);
	new_tuple_end += data_size;

	rc = box_replace(BOX_SPACE_ID, new_tuple, new_tuple_end, &tuple);

	if (rc != 0 || tuple == NULL) {
		return SQLITE_TARANTOOL_ERROR;
	}
	box_iterator_free(iter);

	return SQLITE_OK;
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
	int rc;

	char *key_begin = (char*) region_alloc(&fiber()->gc,
					       mp_sizeof_uint(space_id) +
					       mp_sizeof_array(1));
	char *key = mp_encode_array(key_begin, 1);
	key = mp_encode_uint(key, space_id);

	box_iterator_t *iter = box_index_iterator(BOX_SPACE_ID, 0, ITER_EQ,
						  key_begin, key);

	if (box_iterator_next(iter, &tuple) != 0 || tuple == 0) {
		return SQLITE_TARANTOOL_ERROR;
	}

	assert(tuple_field_count(tuple) == 7);
	const char *sql_stmt_map = box_tuple_field(tuple, 5);

	if (sql_stmt_map == NULL || mp_typeof(*sql_stmt_map) != MP_MAP) {
		return SQLITE_TARANTOOL_ERROR;
	}
	uint32_t map_size = mp_decode_map(&sql_stmt_map);
	assert(map_size == 1);
	(void)map_size;
	uint32_t key_len;
	const char *sql_str = mp_decode_str(&sql_stmt_map, &key_len);
	assert(sqlite3StrNICmp(sql_str, "sql", 3));
	(void)sql_str;
	uint32_t create_stmt_decoded_len;
	const char *create_stmt_old = mp_decode_str(&sql_stmt_map,
						    &create_stmt_decoded_len);
	uint32_t old_name_len = strlen(old_parent_name);
	uint32_t new_name_len = strlen(new_parent_name);
	char *create_stmt_new = (char*) region_alloc(&fiber()->gc,
						  create_stmt_decoded_len +
						  new_name_len + 2);
	memcpy(create_stmt_new, create_stmt_old, create_stmt_decoded_len);

	uint32_t create_stmt_new_len =
		replace_table_name(create_stmt_new, create_stmt_decoded_len,
				   old_parent_name, old_name_len,
				   new_parent_name, new_name_len);
	assert(create_stmt_new_len > 0);

	char *new_tuple = (char*)region_alloc(&fiber()->gc, tuple->bsize +
					      mp_sizeof_str(create_stmt_new_len));

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
	data_begin = tuple_field(tuple, 6);
	data_end = (char*) tuple + tuple_size(tuple);
	data_size = data_end - data_begin;
	memcpy(new_tuple_end, data_begin, data_size);
	new_tuple_end += data_size;

	rc = box_replace(BOX_SPACE_ID, new_tuple, new_tuple_end, &tuple);

	if (rc != 0 || tuple == NULL) {
		return SQLITE_TARANTOOL_ERROR;
	}
	box_iterator_free(iter);

	return SQLITE_OK;
}


/*
 * Performs exactly as extract_key + sqlite3VdbeCompareMsgpack,
 * only faster.
 */
int tarantoolSqlite3IdxKeyCompare(BtCursor *pCur, UnpackedRecord *pUnpacked,
				  int *res)
{
	assert(pCur->curFlags & BTCF_TaCursor);

	struct ta_cursor *c = pCur->pTaCursor;
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

	assert(c);
	assert(c->iter);
	assert(c->tuple_last);

	key_def = box_iterator_key_def(c->iter);
	n = MIN(pUnpacked->nField, key_def->part_count);
	tuple = c->tuple_last;
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

/*
 * The function assumes the cursor is open on _schema.
 * Increment max_id and store updated tuple in the cursor
 * object.
 */
int tarantoolSqlite3IncrementMaxid(BtCursor *pCur)
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

	assert(pCur->curFlags & BTCF_TaCursor);

	struct ta_cursor *c = pCur->pTaCursor;
	uint32_t space_id = SQLITE_PAGENO_TO_SPACEID(pCur->pgnoRoot);
	uint32_t index_id = SQLITE_PAGENO_TO_INDEXID(pCur->pgnoRoot);
	box_tuple_t *res;
	int rc;

	rc = box_update(space_id, index_id,
		key, key + sizeof(key),
		ops, ops + sizeof(ops),
		0,
		&res);
	if (rc != 0 || res == NULL) {
		return SQLITE_TARANTOOL_ERROR;
	}
	if (!c) {
		c = cursor_create(NULL, 0);
		if (!c) return SQLITE_NOMEM;
		pCur->pTaCursor = c;
		c->type = ITER_EQ; /* store some meaningfull value */
	} else if (c->tuple_last) {
		box_tuple_unref(c->tuple_last);
	}
	box_tuple_ref(res);
	c->tuple_last = res;
	pCur->eState = CURSOR_VALID;
	pCur->curIntKey = 0;
	return SQLITE_OK;
}

/*
 * Allocate or grow cursor.
 * Result->type value is unspecified.
 */
static struct ta_cursor *
cursor_create(struct ta_cursor *c, size_t key_size)
{
	size_t             size;
	struct ta_cursor  *res;

	if (c) {
		size = c->size;
		if (size - offsetof(struct ta_cursor, key) >= key_size)
			return c;
	} else {
		size = sizeof(*c);
	}

	while (size - offsetof(struct ta_cursor, key) < key_size)
		size *= 2;

	res = realloc(c, size);
	if (res) {
		res->size = size;
		if (!c) {
			res->iter = NULL;
			res->tuple_last = NULL;
		}
	}
	return res;
}

/* Cursor positioning. */
static int
cursor_seek(BtCursor *pCur, int *pRes, enum iterator_type type,
	    const char *k, const char *ke)
{
	assert(pCur->curFlags & BTCF_TaCursor);

	struct ta_cursor *c = pCur->pTaCursor;
	uint32_t space_id = SQLITE_PAGENO_TO_SPACEID(pCur->pgnoRoot);
	uint32_t index_id = SQLITE_PAGENO_TO_INDEXID(pCur->pgnoRoot);
	size_t key_size = 0;

	/* Close existing iterator, if any */
	if (c && c->iter) {
		box_iterator_free(c->iter);
		c->iter = NULL;
	}

	/* Allocate or grow cursor if needed. */
	if (type == ITER_EQ || type == ITER_REQ) {
		key_size = (size_t)(ke - k);
	}
	c = cursor_create(c, key_size);
	if (!c) {
		*pRes = 1;
		return SQLITE_NOMEM;
	}
	pCur->pTaCursor = c;

	/* Copy key if necessary. */
	if (key_size != 0) {
		memcpy(c->key, k, ke-k);
		ke = c->key + (ke-k);
		k = c->key;
	}

	c->iter = box_index_iterator(space_id, index_id, type, k, ke);
	if (c->iter == NULL) {
		pCur->eState = CURSOR_INVALID;
		return SQLITE_TARANTOOL_ERROR;
	}
	c->type = type;
	pCur->eState = CURSOR_VALID;
	pCur->curIntKey = 0;
	return cursor_advance(pCur, pRes);
}

static int
cursor_advance(BtCursor *pCur, int *pRes)
{
	assert(pCur->curFlags & BTCF_TaCursor);

	struct ta_cursor *c;
	struct tuple *tuple;
	int rc;

	c = pCur->pTaCursor;
	assert(c);
	assert(c->iter);

	rc = box_iterator_next(c->iter, &tuple);
	if (rc)
		return SQLITE_TARANTOOL_ERROR;
	if (c->tuple_last) box_tuple_unref(c->tuple_last);
	if (tuple) {
		box_tuple_ref(tuple);
		*pRes = 0;
	} else {
		pCur->eState = CURSOR_INVALID;
		*pRes = 1;
	}
	c->tuple_last = tuple;
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

struct space;

struct space *
space_by_id(uint32_t id);

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
		"CREATE TABLE "TARANTOOL_SYS_SCHEMA_NAME
		" (key TEXT PRIMARY KEY, value)"
	);

	sql_schema_put(
		init, TARANTOOL_SYS_SPACE_NAME,
		BOX_SPACE_ID, 0,
		"CREATE TABLE "TARANTOOL_SYS_SPACE_NAME
		" (id INT PRIMARY KEY, owner INT, name TEXT, "
		"engine TEXT, field_count INT, opts, format)"
	);

	sql_schema_put(
		init, TARANTOOL_SYS_INDEX_NAME,
		BOX_INDEX_ID, 0,
		"CREATE TABLE "TARANTOOL_SYS_INDEX_NAME" (id INT, iid INT, "
		"name TEXT, type TEXT, opts, parts, "
		"PRIMARY KEY (id, iid))"
	);

	sql_schema_put(
		init, TARANTOOL_SYS_TRIGGER_NAME,
		BOX_TRIGGER_ID, 0,
		"CREATE TABLE "TARANTOOL_SYS_TRIGGER_NAME" ("
		"name TEXT, opts, PRIMARY KEY(name))"
	);

	sql_schema_put(
		init, TARANTOOL_SYS_TRUNCATE_NAME,
		BOX_TRUNCATE_ID, 0,
		"CREATE TABLE "TARANTOOL_SYS_TRUNCATE_NAME
		" (id INT PRIMARY KEY, count INT NOT NULL)"
	);

	sql_schema_put(init, TARANTOOL_SYS_SEQUENCE_NAME, BOX_SEQUENCE_ID, 0,
		       "CREATE TABLE "TARANTOOL_SYS_SEQUENCE_NAME
		       " (id INT PRIMARY KEY, uid INT, name TEXT, step INT, "
		       "max INT, min INT, \"start\" INT, cache INT, cycle INT)");

	sql_schema_put(init, TARANTOOL_SYS_SPACE_SEQUENCE_NAME, BOX_SPACE_SEQUENCE_ID, 0,
		       "CREATE TABLE "TARANTOOL_SYS_SPACE_SEQUENCE_NAME
		       " (space_id INT PRIMARY KEY, sequence_id INT, flag INT)");

	/* Read _space */
	if (space_foreach(space_foreach_put_cb, init) != 0) {
		init->rc = SQLITE_TARANTOOL_ERROR;
		return;
	}

	/* Read _trigger */
	it = box_index_iterator(BOX_TRIGGER_ID, 0, ITER_GE,
				nil_key, nil_key + sizeof(nil_key));

	if (it == NULL) {
		init->rc = SQLITE_TARANTOOL_ERROR;
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
	if (pk_idx && pk_idx->nKeyCol == 1) {
		int pk = pk_idx->aiColumn[0];
		if (pTable->aCol[pk].affinity == 'D')
			pk_forced_int = pk;
	}

	for (i = 0; i < n; i++) {
		const char *t;

		p = enc->encode_map(p, 2);
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

	p = enc->encode_map(base, 1);
	p = enc->encode_str(p, "sql", 3);
	p = enc->encode_str(p, zSql, strlen(zSql));
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
	if (primary_index->nKeyCol == 1) {
		int pk = primary_index->aiColumn[0];
		if (aCol[pk].affinity == 'D')
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
		struct coll * collation = NULL;
		if (pk_forced_int == col)
			t = "integer";
		else
			t = convertSqliteAffinity(aCol[col].affinity, aCol[col].notNull == 0);
		/* do not decode default collation */
		if (sqlite3StrICmp(pIndex->azColl[i], "binary") != 0){
			collation = sqlite3FindCollSeq(NULL, pIndex->azColl[i], 0);
			/* 
			 * At this point, the collation has already been found 
			 * once and the assert should not fire.
			 */
			assert(collation);
		}
		p = enc->encode_map(p, collation == NULL ? 2 : 3);
		p = enc->encode_str(p, "type", sizeof("type")-1);
		p = enc->encode_str(p, t, strlen(t));
		p = enc->encode_str(p, "field", sizeof("field")-1);
		p = enc->encode_uint(p, col);
		if (collation != NULL){
			p = enc->encode_str(p, "collation", sizeof("collation")-1);
			p = enc->encode_uint(p, collation->id);
		}
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
	/* gh-2187
	 *
	 * Include all index columns, i.e. "key" columns followed by the
	 * primary key columns, in secondary indices. It means that all
	 * indices created via SQL engine are unique.
	 */
	p = enc->encode_str(p, "unique", 6);
	/* By now uniqueness is checked by sqlite vdbe engine by extra
	 * secondary index lookups because we did not implement
	 * on conflict Replase, Ignore... features
	 **/
	p = enc->encode_bool(p, IsPrimaryKeyIndex(index));
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

/**
 * Extract maximum integer value from:
 * @param index space_id
 * @param index_id
 * @param field number fieldno
 * @param[out] fetched value in max_id
 *
 * @retval 0 on success, -1 otherwise.
 *
 * If index is empty - return 0 in max_id and success status
 */
int
tarantoolSqlGetMaxId(uint32_t space_id, uint32_t index_id, uint32_t fieldno,
		     uint64_t *max_id)
{
	char key[16];
	struct tuple *tuple;
	char *key_end = mp_encode_array(key, 0);
	if (box_index_max(space_id, index_id, key, key_end, &tuple) != 0)
		return -1;

	/* Index is empty  */
	if (tuple == NULL) {
		*max_id = 0;
		return 0;
	}

	return tuple_field_u64(tuple, fieldno, max_id);
}
