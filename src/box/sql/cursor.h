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

#ifndef SQLITE_CURSOR_H
#define SQLITE_CURSOR_H

typedef u32 Pgno;
typedef struct BtCursor BtCursor;
typedef struct CursorPayload CursorPayload;

/*
 * Values that may be OR'd together to form the argument to the
 * BTREE_HINT_FLAGS hint for sqlite3BtreeCursorHint():
 *
 * The BTREE_BULKLOAD flag is set on index cursors when the index is going
 * to be filled with content that is already in sorted order.
 *
 * The BTREE_SEEK_EQ flag is set on cursors that will get OP_SeekGE or
 * OP_SeekLE opcodes for a range search, but where the range of entries
 * selected will all have the same key.  In other words, the cursor will
 * be used only for equality key searches.
 *
 */
#define BTREE_BULKLOAD 0x00000001	/* Used to full index in sorted order */
#define BTREE_SEEK_EQ  0x00000002	/* EQ seeks only - no range seeks */

/*
 * A cursor contains a particular entry either from Tarantrool or
 * Sorter. Tarantool cursor is able to point to ordinary table or
 * ephemeral one. To distinguish them curFlags is set to TaCursor
 * for ordinary space, or to TEphemCursor for ephemeral space.
 */
struct BtCursor {
	BtCursor *pNext;	/* Forms a linked list of all cursors */
	i64 nKey;		/* Size of pKey, or last integer key */
	void *pKey;		/* Saved key that was cursor last known position */
	Pgno pgnoRoot;		/* Contains both space_id and index_id */
	u8 curFlags;		/* zero or more BTCF_* flags defined below */
	u8 eState;		/* One of the CURSOR_XXX constants (see below) */
	u8 hints;		/* As configured by CursorSetHints() */
	/* All fields above are zeroed when the cursor is allocated.  See
	 * sqlite3CursorZero().  Fields that follow must be manually
	 * initialized.
	 */
	struct KeyInfo *pKeyInfo;	/* Argument passed to comparison function */
	void *pTaCursor;	/* Tarantool cursor */
};

void sqlite3CursorZero(BtCursor *);
void sqlite3CursorHintFlags(BtCursor *, unsigned);

int sqlite3CloseCursor(BtCursor *);
int sqlite3CursorMovetoUnpacked(BtCursor *, UnpackedRecord * pUnKey, int *pRes);

/* An instance of the CursorPayload object describes the content of a single
 * entry in index.
 *
 * This object is used to pass information into tarantoolSqlite3Insert().  The
 * same information used to be passed as five separate parameters.  But placing
 * the information into this object helps to keep the interface more
 * organized and understandable, and it also helps the resulting code to
 * run a little faster by using fewer registers for parameter passing.
 */
struct CursorPayload {
	const void *pKey;	/* Key content for indexes.  NULL for tables */
	sqlite3_int64 nKey;	/* Size of pKey for indexes.  PRIMARY KEY for tabs */
	const void *pData;	/* Data for tables.  NULL for indexes */
	struct Mem *aMem;	/* First of nMem value in the unpacked pKey */
	u16 nMem;		/* Number of aMem[] value.  Might be zero */
	int nData;		/* Size of pData.  0 if none. */
};

int sqlite3CursorNext(BtCursor *, int *pRes);
int sqlite3CursorPrevious(BtCursor *, int *pRes);
int sqlite3CursorPayload(BtCursor *, u32 offset, u32 amt, void *);

void sqlite3ClearCursor(BtCursor *);
int sqlite3CursorHasHint(BtCursor *, unsigned int mask);

#ifndef NDEBUG
int sqlite3CursorIsValid(BtCursor *);
#endif
int sqlite3CursorIsValidNN(BtCursor *);

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

/*
 * Routines to read or write a two- and four-byte big-endian integer values.
 */
#define get2byte(x)   ((x)[0]<<8 | (x)[1])
#define put2byte(p,v) ((p)[0] = (u8)((v)>>8), (p)[1] = (u8)(v))
#define get4byte sqlite3Get4byte
#define put4byte sqlite3Put4byte

/*
 * get2byteAligned(), unlike get2byte(), requires that its argument point to a
 * two-byte aligned address.  get2bytea() is only used for accessing the
 * cell addresses in a btree header.
 */
#if SQLITE_BYTEORDER==4321
#define get2byteAligned(x)  (*(u16*)(x))
#elif SQLITE_BYTEORDER==1234 && !defined(SQLITE_DISABLE_INTRINSIC) \
    && GCC_VERSION>=4008000
#define get2byteAligned(x)  __builtin_bswap16(*(u16*)(x))
#elif SQLITE_BYTEORDER==1234 && !defined(SQLITE_DISABLE_INTRINSIC) \
    && defined(_MSC_VER) && _MSC_VER>=1300
#define get2byteAligned(x)  _byteswap_ushort(*(u16*)(x))
#else
#define get2byteAligned(x)  ((x)[0]<<8 | (x)[1])
#endif

#endif				/* SQLITE_CURSOR_H */
