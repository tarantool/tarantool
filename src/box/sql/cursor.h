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

#ifndef SQL_CURSOR_H
#define SQL_CURSOR_H

typedef struct BtCursor BtCursor;

/*
 * A cursor contains a particular entry either from Tarantrool or
 * Sorter. Tarantool cursor is able to point to ordinary table or
 * ephemeral one. To distinguish them curFlags is set to TaCursor
 * for ordinary space, or to TEphemCursor for ephemeral space.
 */
struct BtCursor {
	u8 curFlags;		/* zero or more BTCF_* flags defined below */
	u8 eState;		/* One of the CURSOR_XXX constants (see below) */
	u8 hints;		/* As configured by CursorSetHints() */
	struct space *space;
	struct index *index;
	struct iterator *iter;
	enum iterator_type iter_type;
	struct tuple *last_tuple;
	char *key;		/* Saved key that was cursor last known position */
};

void sqlCursorZero(BtCursor *);

/**
 * Close a cursor and invalidate its state. In case of
 * ephemeral cursor, corresponding space should be dropped.
 */
void
sql_cursor_close(struct BtCursor *cursor);
int sqlCursorMovetoUnpacked(BtCursor *, UnpackedRecord * pUnKey, int *pRes);

int sqlCursorNext(BtCursor *, int *pRes);
int sqlCursorPrevious(BtCursor *, int *pRes);
void
sqlCursorPayload(BtCursor *, u32 offset, u32 amt, void *);

/**
 * Release tuple, free iterator, invalidate cursor's state.
 * Note that this routine doesn't nullify space and index:
 * it is also used during OP_NullRow opcode to refresh given
 * cursor.
 */
void
sql_cursor_cleanup(struct BtCursor *cursor);

#ifndef NDEBUG
int sqlCursorIsValid(BtCursor *);
#endif
int sqlCursorIsValidNN(BtCursor *);

/*
 * Legal values for BtCursor.curFlags
 */
#define BTCF_TaCursor     0x80	/* Tarantool cursor, pTaCursor valid */
#define BTCF_TEphemCursor 0x40	/* Tarantool cursor to ephemeral table  */

/*
 * Potential values for BtCursor.eState.
 *
 * CURSOR_INVALID:
 *   Cursor does not point to a valid entry. This can happen (for example)
 *   because the table is empty or because cursorFirst() has not been
 *   called.
 *
 * CURSOR_VALID:
 *   Cursor points to a valid entry. getPayload() etc. may be called.
 *
 */
#define CURSOR_INVALID           0
#define CURSOR_VALID             1

#endif				/* SQL_CURSOR_H */
