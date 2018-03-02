/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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

#include "sqliteInt.h"
#include "tarantoolInt.h"

/*
 * Clear the current cursor position.
 */
void
sqlite3ClearCursor(BtCursor * pCur)
{
	free(pCur->key);
	pCur->key = 0;
	pCur->iter = NULL;
	pCur->last_tuple = NULL;
	pCur->eState = CURSOR_INVALID;
}

/*
 * Provide flag hints to the cursor.
 */
void
sqlite3CursorHintFlags(BtCursor * pCur, unsigned x)
{
	assert(x == BTREE_SEEK_EQ || x == BTREE_BULKLOAD || x == 0);
	pCur->hints = x;
}

/*
 * Initialize memory that will be converted into a BtCursor object.
 */
void
sqlite3CursorZero(BtCursor * p)
{
	memset(p, 0, sizeof(*p));
}

/*
 * Close a cursor and invalidate its state. In case of
 * ephemeral cursor, corresponding space should be dropped.
 */
int
sqlite3CloseCursor(BtCursor * pCur)
{
	assert((pCur->curFlags & BTCF_TaCursor) ||
	       (pCur->curFlags & BTCF_TEphemCursor));

	if (pCur->curFlags & BTCF_TEphemCursor) {
		tarantoolSqlite3EphemeralDrop(pCur);
	}
	tarantoolSqlite3CloseCursor(pCur);
	sqlite3ClearCursor(pCur);

	return SQLITE_OK;
}

#ifndef NDEBUG			/* The next routine used only within assert() statements */
/*
 * Return true if the given BtCursor is valid.  A valid cursor is one
 * that is currently pointing to a row in a (non-empty) table.
 * This is a verification routine is used only within assert() statements.
 */
int
sqlite3CursorIsValid(BtCursor *pCur)
{
	return pCur && pCur->eState == CURSOR_VALID;
}
#endif				/* NDEBUG */
int
sqlite3CursorIsValidNN(BtCursor *pCur)
{
	assert(pCur != 0);
	return pCur->eState == CURSOR_VALID;
}

/*
 * Read part of the payload for the row at which that cursor pCur is currently
 * pointing.  "amt" bytes will be transferred into pBuf[].  The transfer
 * begins at "offset".
 *
 * For sqlite3CursorPayload(), the caller must ensure that pCur is pointing
 * to a valid row in the table.
 *
 * Return SQLITE_OK on success or an error code if anything goes
 * wrong.  An error is returned if "offset+amt" is larger than
 * the available payload.
 */
int
sqlite3CursorPayload(BtCursor *pCur, u32 offset, u32 amt, void *pBuf)
{
	assert(pCur->eState == CURSOR_VALID);
	assert((pCur->curFlags & BTCF_TaCursor) ||
	       (pCur->curFlags & BTCF_TEphemCursor));

	const void *pPayload;
	u32 sz;
	pPayload = tarantoolSqlite3PayloadFetch(pCur, &sz);
	if ((uptr) (offset + amt) > sz)
		return SQLITE_CORRUPT_BKPT;
	memcpy(pBuf, pPayload + offset, amt);
	return SQLITE_OK;
}

/* Move the cursor so that it points to an entry near the key
 * specified by pIdxKey. Return a success code.
 *
 * If an exact match is not found, then the cursor is always
 * left pointing at a leaf page which would hold the entry if it
 * were present.  The cursor might point to an entry that comes
 * before or after the key.
 *
 * An integer is written into *pRes which is the result of
 * comparing the key with the entry to which the cursor is
 * pointing.  The meaning of the integer written into
 * *pRes is as follows:
 *
 *     *pRes<0      The cursor is left pointing at an entry that
 *                  is smaller than pIdxKey or if the table is empty
 *                  and the cursor is therefore left point to nothing.
 *
 *     *pRes==0     The cursor is left pointing at an entry that
 *                  exactly matches pIdxKey.
 *
 *     *pRes>0      The cursor is left pointing at an entry that
 *                  is larger than pIdxKey.
 */

int
sqlite3CursorMovetoUnpacked(BtCursor * pCur,	/* The cursor to be moved */
			   UnpackedRecord * pIdxKey,	/* Unpacked index key */
			   int *pRes	/* Write search results here */
    )
{
	assert(pRes);
	assert(pIdxKey);
	assert((pCur->curFlags & BTCF_TaCursor) ||
	       (pCur->curFlags & BTCF_TEphemCursor));

	return tarantoolSqlite3MovetoUnpacked(pCur, pIdxKey, pRes);
}

int
sqlite3CursorNext(BtCursor *pCur, int *pRes)
{
	assert(pRes != 0);
	assert(*pRes == 0 || *pRes == 1);
	assert((pCur->curFlags & BTCF_TaCursor) ||
	       (pCur->curFlags & BTCF_TEphemCursor));

	*pRes = 0;
	return tarantoolSqlite3Next(pCur, pRes);
}

int
sqlite3CursorPrevious(BtCursor *pCur, int *pRes)
{
	assert(pRes != 0);
	assert(*pRes == 0 || *pRes == 1);
	assert((pCur->curFlags & BTCF_TaCursor) ||
	       (pCur->curFlags & BTCF_TEphemCursor));

	*pRes = 0;
	return tarantoolSqlite3Previous(pCur, pRes);
}

/*
 * Return true if the cursor has a hint specified.  This routine is
 * only used from within assert() statements
 */
int
sqlite3CursorHasHint(BtCursor *pCsr, unsigned int mask)
{
	return (pCsr->hints & mask) != 0;
}
