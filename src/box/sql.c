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
#undef likely
#undef unlikely

#include "index.h"
#include "box.h"
#include "key_def.h"
#include "tuple.h"
#include "fiber.h"
#include "small/region.h"

static sqlite3 *db = NULL;

void
sql_init()
{
	int rc;
	rc = sqlite3_open("", &db);
	if (rc == SQLITE_OK) {
		assert(db);
	} else {
        /* XXX */
	}
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

static const char nil_key[] = { 0x90 }; /* Empty MsgPack array. */

/*
 * Tarantool iterator API was apparently designed by space aliens.
 * This wrapper is necessary for interfacing with the SQLite btree code.
 */
struct ta_cursor
{
	box_iterator_t   *iter;
	struct tuple     *tuple_last;
	enum iterator_type type;
};

static int
cursor_seek(BtCursor *pCur, int *pRes, enum iterator_type type,
	    const char *k, const char *ke);

static int
cursor_advance(BtCursor *pCur, int *pRes);

#ifndef NDEBUG
static enum iterator_type normalize_iter_type(BtCursor *pCur)
{
	struct ta_cursor *c = pCur->pTaCursor;
	enum iterator_type t;
	assert(pCur->curFlags & BTCF_TaCursor);
	assert(c);
	t = c->type;
	if (t == ITER_GE || t == ITER_GT || t == ITER_EQ) {
		return ITER_GE;
	} else if (t == ITER_LE || t == ITER_LT || t == ITER_REQ) {
		return ITER_LE;
	} else {
		assert(("Unexpected iterator type", 0));
	}
}
#endif

static uint32_t get_space_id(Pgno page, uint32_t *index_id);

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
	assert(normalize_iter_type(pCur) == ITER_GE);
	return cursor_advance(pCur, pRes);
}

int tarantoolSqlite3Previous(BtCursor *pCur, int *pRes)
{
	assert(normalize_iter_type(pCur) == ITER_LE);
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
		assert(("Unexpected opcode", 0));
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

	uint32_t space_id, index_id;
	space_id = get_space_id(pCur->pgnoRoot, &index_id);
	*pnEntry = box_index_len(space_id, index_id);
	return SQLITE_OK;
}

int tarantoolSqlite3Insert(BtCursor *pCur, const BtreePayload *pX)
{
	assert(pCur->curFlags & BTCF_TaCursor);

	if (box_replace(get_space_id(pCur->pgnoRoot, NULL),
			pX->pKey, (const char *)pX->pKey + pX->nKey,
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
	size_t original_size;
	const char *key;
	uint32_t key_size;
	int rc;

	assert(c);
	assert(c->iter);
	assert(c->tuple_last);

	space_id = get_space_id(pCur->pgnoRoot, &index_id);
	original_size = region_used(&fiber()->gc);
	key = tuple_extract_key(c->tuple_last,
				box_iterator_key_def(c->iter),
				&key_size);
	if (key == NULL)
		return SQLITE_TARANTOOL_ERROR;
	rc = box_delete(space_id, index_id, key, key+key_size, NULL);
	region_truncate(&fiber()->gc, original_size);
	return rc == 0 ? SQLITE_OK : SQLITE_TARANTOOL_ERROR;
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
	const struct key_def *key_def;
	const struct tuple *tuple;
	const char *base;
	const struct tuple_format *format;
	const uint32_t *field_map;
	const char *p;
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
	p = base; mp_decode_array(&p);
	for (i=0; i<n; i++) {
		int32_t offset_slot = format->fields[
			key_def->parts[i].fieldno
		].offset_slot;
		if (offset_slot != TUPLE_OFFSET_SLOT_NIL) {
			p = base + field_map[offset_slot];
		}
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
	key = tuple_extract_key(tuple,
				key_def,
				&key_size);
	if (key != NULL) {
		rc = sqlite3VdbeRecordCompareMsgpack((int)key_size, key,
						     pUnpacked);
		region_truncate(&fiber()->gc, original_size);
		assert(rc == *res);
	}
#endif
	return SQLITE_OK;
}

/* Cursor positioning. */
static int
cursor_seek(BtCursor *pCur, int *pRes, enum iterator_type type,
	    const char *k, const char *ke)
{
	assert(pCur->curFlags & BTCF_TaCursor);

	struct ta_cursor *c;
	uint32_t space_id, index_id;

	space_id = get_space_id(pCur->pgnoRoot, &index_id);

	c = pCur->pTaCursor;
	if (c) {
		/* close existing iterator, if any */
		if (c->iter) {
			box_iterator_free(c->iter);
			c->iter = NULL;
		}
	} else {
		c = malloc(sizeof(*c));
		if (!c) {
			*pRes = 1;
			return SQLITE_NOMEM;
		}
		pCur->pTaCursor = c;
		c->tuple_last = NULL;
	}
	c->iter = box_index_iterator(space_id, index_id, type, k, ke);
	if (c->iter == NULL) {
		pCur->eState = CURSOR_INVALID;
		return SQLITE_TARANTOOL_ERROR;
        }
	c->type = type;
	pCur->eState = CURSOR_VALID;
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

/*
 * Manually feed in a row in sqlite_master format; creates schema
 * objects. Called from Lua (ffi).
 */
int sql_schema_put(int idb, int argc, char **argv)
{
	InitData init;
	char *err_msg = NULL;

	if (!db) return SQLITE_ERROR;

	init.db = db;
	init.iDb = idb;
	init.rc = SQLITE_OK;
	init.pzErrMsg = &err_msg;

	sqlite3_mutex_enter(db->mutex);
	sqlite3BtreeEnterAll(db);
	db->init.busy = 1;
	sqlite3InitCallback(&init, argc, argv, NULL);
	db->init.busy = 0;
	sqlite3BtreeLeaveAll(db);

	/*
	 * Overwrite argv[0] with the error message (if any), caller
	 * should free it.
	 */
	if (err_msg) {
		argv[0] = strdup(err_msg);
		sqlite3DbFree(db, err_msg);
	} else
		argv[0] = NULL;

	sqlite3_mutex_leave(db->mutex);
	return init.rc;
}

/* Space_id and index_id are encoded in SQLite page number. */
static uint32_t get_space_id(Pgno page, uint32_t *index_id)
{
	if (index_id) *index_id = page & 31;
	return page >> 5;
}
