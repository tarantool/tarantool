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
#undef Index
#undef likely
#undef unlikely

#include "index.h"
#include "tuple.h"

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

static int sql_copy_error(sqlite3 *);
static uint32_t get_space_id(Pgno page, uint32_t *index_id);

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

int tarantoolSqlite3Count(BtCursor *pCur, i64 *pnEntry)
{
	assert(pCur->curFlags & BTCF_TaCursor);

	uint32_t space_id, index_id;
	space_id = get_space_id(pCur->pgnoRoot, &index_id);
	*pnEntry = box_index_len(space_id, index_id);
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
		return sql_copy_error(pCur->pBtree->db);
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
		return sql_copy_error(pCur->pBtree->db);
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

/* Copy last error from Tarantool's diag */
static int sql_copy_error(sqlite3 *db)
{
	(void)db;
	return SQLITE_ERROR;
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
