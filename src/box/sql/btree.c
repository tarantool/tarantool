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

/*
 * This file implements an external (disk-based) database using BTrees.
 * See the header comment on "btreeInt.h" for additional information.
 * Including a description of file format and an overview of operation.
 */
#include "btreeInt.h"
#include "tarantoolInt.h"
#include "box/session.h"

/*
 * The header string that appears at the beginning of every
 * SQLite database.
 */
static const char zMagicHeader[] = SQLITE_FILE_HEADER;

/*
 * Set this global variable to 1 to enable tracing using the TRACE
 * macro.
 */
#if 0
int sqlite3BtreeTrace = 1;	/* True to enable tracing */
#define TRACE(X)  if(sqlite3BtreeTrace){printf X;fflush(stdout);}
#else
#define TRACE(X)
#endif

/*
 * Extract a 2-byte big-endian integer from an array of unsigned bytes.
 * But if the value is zero, make it 65536.
 *
 * This routine is used to extract the "offset to cell content area" value
 * from the header of a btree page.  If the page size is 65536 and the page
 * is empty, the offset should be 65536, but the 2-byte value stores zero.
 * This routine makes the necessary adjustment to 65536.
 */
#define get2byteNotZero(X)  (((((int)get2byte(X))-1)&0xffff)+1)

/*
 * Values passed as the 5th argument to allocateBtreePage()
 */
#define BTALLOC_ANY   0		/* Allocate any page */
#define BTALLOC_EXACT 1		/* Allocate exact page if possible */
#define BTALLOC_LE    2		/* Allocate any page <= the parameter */

#ifndef SQLITE_OMIT_SHARED_CACHE
/*
 * A list of BtShared objects that are eligible for participation
 * in shared cache.  This variable has file scope during normal builds,
 * but the test harness needs to access it so we make it global for
 * test builds.
 *
 * Access to this variable is protected by SQLITE_MUTEX_STATIC_MASTER.
 */
#ifdef SQLITE_TEST
BtShared *SQLITE_WSD sqlite3SharedCacheList = 0;
#else
static BtShared *SQLITE_WSD sqlite3SharedCacheList = 0;
#endif
#endif				/* SQLITE_OMIT_SHARED_CACHE */

#ifndef SQLITE_OMIT_SHARED_CACHE
/*
 * Enable or disable the shared pager and schema features.
 *
 * This routine has no effect on existing database connections.
 * The shared cache setting effects only future calls to
 * sqlite3_open(), sqlite3_open16(), or sqlite3_open_v2().
 */
int
sqlite3_enable_shared_cache(int enable)
{
	sqlite3GlobalConfig.sharedCacheEnabled = enable;
	return SQLITE_OK;
}
#endif

#ifdef SQLITE_OMIT_SHARED_CACHE
  /*
   * The functions querySharedCacheTableLock(), setSharedCacheTableLock(),
   * and clearAllSharedCacheTableLocks()
   * manipulate entries in the BtShared.pLock linked list used to store
   * shared-cache table level locks. If the library is compiled with the
   * shared-cache feature disabled, then there is only ever one user
   * of each BtShared structure and so this locking is not necessary.
   * So define the lock related functions as no-ops.
   */
#define querySharedCacheTableLock(a,b,c) SQLITE_OK
#define setSharedCacheTableLock(a,b,c) SQLITE_OK
#define clearAllSharedCacheTableLocks(a)
#define downgradeAllSharedCacheTableLocks(a)
#define hasSharedCacheTableLock(a,b,c,d) 1
#define hasReadConflicts(a, b) 0
#endif

#ifndef SQLITE_OMIT_SHARED_CACHE

#ifdef SQLITE_DEBUG
/*
 *** This function is only used as part of an assert() statement. ***
 *
 * Check to see if pBtree holds the required locks to read or write to the
 * table with root page iRoot.   Return 1 if it does and 0 if not.
 *
 * For example, when writing to a table with root-page iRoot via
 * Btree connection pBtree:
 *
 *    assert( hasSharedCacheTableLock(pBtree, iRoot, 0, WRITE_LOCK) );
 *
 * When writing to an index that resides in a sharable database, the
 * caller should have first obtained a lock specifying the root page of
 * the corresponding table. This makes things a bit more complicated,
 * as this module treats each table as a separate structure. To determine
 * the table corresponding to the index being written, this
 * function has to search through the database schema.
 *
 * Instead of a lock on the table/index rooted at page iRoot, the caller may
 * hold a write-lock on the schema table (root page 1). This is also
 * acceptable.
 */
static int
hasSharedCacheTableLock(Btree * pBtree,	/* Handle that must hold lock */
			Pgno iRoot,	/* Root page of b-tree */
			int isIndex,	/* True if iRoot is the root of an index b-tree */
			int eLockType	/* Required lock type (READ_LOCK or WRITE_LOCK) */
    )
{
	Schema *pSchema = (Schema *) pBtree->pBt->pSchema;
	Pgno iTab = 0;
	BtLock *pLock;
	struct session *user_session = current_session();

	/* If this database is not shareable, or if the client is reading
	 * and has the read-uncommitted flag set, then no lock is required.
	 * Return true immediately.
	 */
	if ((pBtree->sharable == 0)
	    || (eLockType == READ_LOCK &&
		(user_session->sql_flags & SQLITE_ReadUncommitted))
	    ) {
		return 1;
	}

	/* If the client is reading  or writing an index and the schema is
	 * not loaded, then it is too difficult to actually check to see if
	 * the correct locks are held.  So do not bother - just return true.
	 * This case does not come up very often anyhow.
	 */
	if (isIndex
	    && (!pSchema || (pSchema->schemaFlags & DB_SchemaLoaded) == 0)) {
		return 1;
	}

	/* Figure out the root-page that the lock should be held on. For table
	 * b-trees, this is just the root page of the b-tree being read or
	 * written. For index b-trees, it is the root page of the associated
	 * table.
	 */
	if (isIndex) {
		HashElem *p, *j;
		for (p = sqliteHashFirst(&pSchema->tblHash); p;
		     p = sqliteHashNext(p)) {
			Table *pTab = (Table *) sqliteHashData(p);
			for (j = sqliteHashFirst(&pTab->idxHash); j;
			     j = sqliteHashNext(j)) {
				Index *pIdx = (Index *) sqliteHashData(j);
				if (pIdx->tnum == (int)iRoot) {
					if (iTab) {
						/* Two or more indexes share the same root page.  There must
						 * be imposter tables.  So just return true.  The assert is not
						 * useful in that case.
						 */
						return 1;
					}
					iTab = pIdx->pTable->tnum;
				}
			}
		}
	} else {
		iTab = iRoot;
	}

	/* Search for the required lock. Either a write-lock on root-page iTab, a
	 * write-lock on the schema table, or (if the client is reading) a
	 * read-lock on iTab will suffice. Return 1 if any of these are found.
	 */
	for (pLock = pBtree->pBt->pLock; pLock; pLock = pLock->pNext) {
		if (pLock->pBtree == pBtree
		    && (pLock->iTable == iTab
			|| (pLock->eLock == WRITE_LOCK && pLock->iTable == 1))
		    && pLock->eLock >= eLockType) {
			return 1;
		}
	}

	/* Failed to find the required lock. */
	return 0;
}
#endif				/* SQLITE_DEBUG */

#ifdef SQLITE_DEBUG
/*
 *** This function may be used as part of assert() statements only. ****
 *
 * Return true if it would be illegal for pBtree to write into the
 * table or index rooted at iRoot because other shared connections are
 * simultaneously reading that same table or index.
 *
 * It is illegal for pBtree to write if some other Btree object that
 * shares the same BtShared object is currently reading or writing
 * the iRoot table.  Except, if the other Btree object has the
 * read-uncommitted flag set, then it is OK for the other object to
 * have a read cursor.
 *
 * For example, before writing to any part of the table or index
 * rooted at page iRoot, one should call:
 *
 *    assert( !hasReadConflicts(pBtree, iRoot) );
 */
static int
hasReadConflicts(Btree * pBtree, Pgno iRoot)
{
	BtCursor *p;
	struct session *user_session = current_session();
	for (p = pBtree->pBt->pCursor; p; p = p->pNext) {
		if (p->pgnoRoot == iRoot
		    && p->pBtree != pBtree
		    && 0 == (user_session->sql_flags & SQLITE_ReadUncommitted)
		    ) {
			return 1;
		}
	}
	return 0;
}
#endif				/* #ifdef SQLITE_DEBUG */

/*
 * Query to see if Btree handle p may obtain a lock of type eLock
 * (READ_LOCK or WRITE_LOCK) on the table with root-page iTab. Return
 * SQLITE_OK if the lock may be obtained (by calling
 * setSharedCacheTableLock()), or SQLITE_LOCKED if not.
 */
static int
querySharedCacheTableLock(Btree * p, Pgno iTab, u8 eLock)
{
	BtShared *pBt = p->pBt;
	BtLock *pIter;
	struct session *user_session = current_session();

	assert(sqlite3BtreeHoldsMutex(p));
	assert(eLock == READ_LOCK || eLock == WRITE_LOCK);
	assert(p->db != 0);
	assert(!(user_session->sql_flags &
		 SQLITE_ReadUncommitted) || eLock == WRITE_LOCK || iTab == 1);

	/* If requesting a write-lock, then the Btree must have an open write
	 * transaction on this file. And, obviously, for this to be so there
	 * must be an open write transaction on the file itself.
	 */
	assert(eLock == READ_LOCK
	       || (p == pBt->pWriter && p->inTrans == TRANS_WRITE));
	assert(eLock == READ_LOCK || pBt->inTransaction == TRANS_WRITE);

	/* This routine is a no-op if the shared-cache is not enabled */
	if (!p->sharable) {
		return SQLITE_OK;
	}

	/* If some other connection is holding an exclusive lock, the
	 * requested lock may not be obtained.
	 */
	if (pBt->pWriter != p && (pBt->btsFlags & BTS_EXCLUSIVE) != 0) {
		sqlite3ConnectionBlocked(p->db, pBt->pWriter->db);
		return SQLITE_LOCKED_SHAREDCACHE;
	}

	for (pIter = pBt->pLock; pIter; pIter = pIter->pNext) {
		/* The condition (pIter->eLock!=eLock) in the following if(...)
		 * statement is a simplification of:
		 *
		 *   (eLock==WRITE_LOCK || pIter->eLock==WRITE_LOCK)
		 *
		 * since we know that if eLock==WRITE_LOCK, then no other connection
		 * may hold a WRITE_LOCK on any table in this file (since there can
		 * only be a single writer).
		 */
		assert(pIter->eLock == READ_LOCK || pIter->eLock == WRITE_LOCK);
		assert(eLock == READ_LOCK || pIter->pBtree == p
		       || pIter->eLock == READ_LOCK);
		if (pIter->pBtree != p && pIter->iTable == iTab
		    && pIter->eLock != eLock) {
			sqlite3ConnectionBlocked(p->db, pIter->pBtree->db);
			if (eLock == WRITE_LOCK) {
				assert(p == pBt->pWriter);
				pBt->btsFlags |= BTS_PENDING;
			}
			return SQLITE_LOCKED_SHAREDCACHE;
		}
	}
	return SQLITE_OK;
}
#endif				/* !SQLITE_OMIT_SHARED_CACHE */

#ifndef SQLITE_OMIT_SHARED_CACHE
/*
 * Add a lock on the table with root-page iTable to the shared-btree used
 * by Btree handle p. Parameter eLock must be either READ_LOCK or
 * WRITE_LOCK.
 *
 * This function assumes the following:
 *
 *   (a) The specified Btree object p is connected to a sharable
 *       database (one with the BtShared.sharable flag set), and
 *
 *   (b) No other Btree objects hold a lock that conflicts
 *       with the requested lock (i.e. querySharedCacheTableLock() has
 *       already been called and returned SQLITE_OK).
 *
 * SQLITE_OK is returned if the lock is added successfully. SQLITE_NOMEM
 * is returned if a malloc attempt fails.
 */
static int
setSharedCacheTableLock(Btree * p, Pgno iTable, u8 eLock)
{
	BtShared *pBt = p->pBt;
	BtLock *pLock = 0;
	BtLock *pIter;
	struct session *user_session = current_session();

	assert(sqlite3BtreeHoldsMutex(p));
	assert(eLock == READ_LOCK || eLock == WRITE_LOCK);
	assert(p->db != 0);

	/* A connection with the read-uncommitted flag set will never try to
	 * obtain a read-lock using this function. The only read-lock obtained
	 * by a connection in read-uncommitted mode is on the sqlite_master
	 * table, and that lock is obtained in BtreeBeginTrans().
	 */
	assert(0 == (user_session->sql_flags & SQLITE_ReadUncommitted)
	       || eLock == WRITE_LOCK);

	/* This function should only be called on a sharable b-tree after it
	 * has been determined that no other b-tree holds a conflicting lock.
	 */
	assert(p->sharable);
	assert(SQLITE_OK == querySharedCacheTableLock(p, iTable, eLock));

	/* First search the list for an existing lock on this table. */
	for (pIter = pBt->pLock; pIter; pIter = pIter->pNext) {
		if (pIter->iTable == iTable && pIter->pBtree == p) {
			pLock = pIter;
			break;
		}
	}

	/* If the above search did not find a BtLock struct associating Btree p
	 * with table iTable, allocate one and link it into the list.
	 */
	if (!pLock) {
		pLock = (BtLock *) sqlite3MallocZero(sizeof(BtLock));
		if (!pLock) {
			return SQLITE_NOMEM_BKPT;
		}
		pLock->iTable = iTable;
		pLock->pBtree = p;
		pLock->pNext = pBt->pLock;
		pBt->pLock = pLock;
	}

	/* Set the BtLock.eLock variable to the maximum of the current lock
	 * and the requested lock. This means if a write-lock was already held
	 * and a read-lock requested, we don't incorrectly downgrade the lock.
	 */
	assert(WRITE_LOCK > READ_LOCK);
	if (eLock > pLock->eLock) {
		pLock->eLock = eLock;
	}

	return SQLITE_OK;
}
#endif				/* !SQLITE_OMIT_SHARED_CACHE */

#ifndef SQLITE_OMIT_SHARED_CACHE
/*
 * Release all the table locks (locks obtained via calls to
 * the setSharedCacheTableLock() procedure) held by Btree object p.
 *
 * This function assumes that Btree p has an open read or write
 * transaction. If it does not, then the BTS_PENDING flag
 * may be incorrectly cleared.
 */
static void
clearAllSharedCacheTableLocks(Btree * p)
{
	BtShared *pBt = p->pBt;
	BtLock **ppIter = &pBt->pLock;

	assert(sqlite3BtreeHoldsMutex(p));
	assert(p->sharable || 0 == *ppIter);
	assert(p->inTrans > 0);

	while (*ppIter) {
		BtLock *pLock = *ppIter;
		assert((pBt->btsFlags & BTS_EXCLUSIVE) == 0
		       || pBt->pWriter == pLock->pBtree);
		assert(pLock->pBtree->inTrans >= pLock->eLock);
		if (pLock->pBtree == p) {
			*ppIter = pLock->pNext;
			assert(pLock->iTable != 1 || pLock == &p->lock);
			if (pLock->iTable != 1) {
				sqlite3_free(pLock);
			}
		} else {
			ppIter = &pLock->pNext;
		}
	}

	assert((pBt->btsFlags & BTS_PENDING) == 0 || pBt->pWriter);
	if (pBt->pWriter == p) {
		pBt->pWriter = 0;
		pBt->btsFlags &= ~(BTS_EXCLUSIVE | BTS_PENDING);
	} else if (pBt->nTransaction == 2) {
		/* This function is called when Btree p is concluding its
		 * transaction. If there currently exists a writer, and p is not
		 * that writer, then the number of locks held by connections other
		 * than the writer must be about to drop to zero. In this case
		 * set the BTS_PENDING flag to 0.
		 *
		 * If there is not currently a writer, then BTS_PENDING must
		 * be zero already. So this next line is harmless in that case.
		 */
		pBt->btsFlags &= ~BTS_PENDING;
	}
}

/*
 * This function changes all write-locks held by Btree p into read-locks.
 */
static void
downgradeAllSharedCacheTableLocks(Btree * p)
{
	BtShared *pBt = p->pBt;
	if (pBt->pWriter == p) {
		BtLock *pLock;
		pBt->pWriter = 0;
		pBt->btsFlags &= ~(BTS_EXCLUSIVE | BTS_PENDING);
		for (pLock = pBt->pLock; pLock; pLock = pLock->pNext) {
			assert(pLock->eLock == READ_LOCK || pLock->pBtree == p);
			pLock->eLock = READ_LOCK;
		}
	}
}

#endif				/* SQLITE_OMIT_SHARED_CACHE */

static void releasePage(MemPage * pPage);	/* Forward reference */

/*
 **** This routine is used inside of assert() only ****
 *
 * Verify that the cursor holds the mutex on its BtShared
 */
#ifdef SQLITE_DEBUG
static int
cursorHoldsMutex(BtCursor * p)
{
	return sqlite3_mutex_held(p->pBt->mutex);
}

/* Verify that the cursor and the BtShared agree about what is the current
 * database connetion. This is important in shared-cache mode. If the database
 * connection pointers get out-of-sync, it is possible for routines like
 * btreeInitPage() to reference an stale connection pointer that references a
 * a connection that has already closed.  This routine is used inside assert()
 * statements only and for the purpose of double-checking that the btree code
 * does keep the database connection pointers up-to-date.
 */
static int
cursorOwnsBtShared(BtCursor * p)
{
	assert(cursorHoldsMutex(p));
	return (p->pBtree->db == p->pBt->db);
}
#endif

#ifndef SQLITE_OMIT_INCRBLOB
/*
 * This function is called before modifying the contents of a table
 * to invalidate any incrblob cursors that are open on the
 * row or one of the rows being modified.
 *
 * If argument isClearTable is true, then the entire contents of the
 * table is about to be deleted. In this case invalidate all incrblob
 * cursors open on any row within the table with root-page pgnoRoot.
 *
 * Otherwise, if argument isClearTable is false, then the row with
 * rowid iRow is being replaced or deleted. In this case invalidate
 * only those incrblob cursors open on that specific row.
 */
static void
invalidateIncrblobCursors(Btree * pBtree,	/* The database file to check */
			  i64 iRow,	/* The rowid that might be changing */
			  int isClearTable	/* True if all rows are being deleted */
    )
{
	BtCursor *p;
	if (pBtree->hasIncrblobCur == 0)
		return;
	assert(sqlite3BtreeHoldsMutex(pBtree));
	pBtree->hasIncrblobCur = 0;
	for (p = pBtree->pBt->pCursor; p; p = p->pNext) {
		if ((p->curFlags & BTCF_Incrblob) != 0) {
			pBtree->hasIncrblobCur = 1;
			if (isClearTable || p->info.nKey == iRow) {
				p->eState = CURSOR_INVALID;
			}
		}
	}
}

#else
  /* Stub function when INCRBLOB is omitted */
#define invalidateIncrblobCursors(x,y,z)
#endif				/* SQLITE_OMIT_INCRBLOB */

/*
 * Set bit pgno of the BtShared.pHasContent bitvec. This is called
 * when a page that previously contained data becomes a free-list leaf
 * page.
 *
 * The BtShared.pHasContent bitvec exists to work around an obscure
 * bug caused by the interaction of two useful IO optimizations surrounding
 * free-list leaf pages:
 *
 *   1) When all data is deleted from a page and the page becomes
 *      a free-list leaf page, the page is not written to the database
 *      (as free-list leaf pages contain no meaningful data). Sometimes
 *      such a page is not even journalled (as it will not be modified,
 *      why bother journalling it?).
 *
 *   2) When a free-list leaf page is reused, its content is not read
 *      from the database or written to the journal file (why should it
 *      be, if it is not at all meaningful?).
 *
 * By themselves, these optimizations work fine and provide a handy
 * performance boost to bulk delete or insert operations. However, if
 * a page is moved to the free-list and then reused within the same
 * transaction, a problem comes up. If the page is not journalled when
 * it is moved to the free-list and it is also not journalled when it
 * is extracted from the free-list and reused, then the original data
 * may be lost. In the event of a rollback, it may not be possible
 * to restore the database to its original configuration.
 *
 * The solution is the BtShared.pHasContent bitvec. Whenever a page is
 * moved to become a free-list leaf page, the corresponding bit is
 * set in the bitvec. Whenever a leaf page is extracted from the free-list,
 * optimization 2 above is omitted if the corresponding bit is already
 * set in BtShared.pHasContent. The contents of the bitvec are cleared
 * at the end of every transaction.
 */
static int
btreeSetHasContent(BtShared * pBt, Pgno pgno)
{
	int rc = SQLITE_OK;
	if (!pBt->pHasContent) {
		assert(pgno <= pBt->nPage);
		pBt->pHasContent = sqlite3BitvecCreate(pBt->nPage);
		if (!pBt->pHasContent) {
			rc = SQLITE_NOMEM_BKPT;
		}
	}
	if (rc == SQLITE_OK && pgno <= sqlite3BitvecSize(pBt->pHasContent)) {
		rc = sqlite3BitvecSet(pBt->pHasContent, pgno);
	}
	return rc;
}

/*
 * Query the BtShared.pHasContent vector.
 *
 * This function is called when a free-list leaf page is removed from the
 * free-list for reuse. It returns false if it is safe to retrieve the
 * page from the pager layer with the 'no-content' flag set. True otherwise.
 */
static int
btreeGetHasContent(BtShared * pBt, Pgno pgno)
{
	Bitvec *p = pBt->pHasContent;
	return (p
		&& (pgno > sqlite3BitvecSize(p) || sqlite3BitvecTest(p, pgno)));
}

/*
 * Clear (destroy) the BtShared.pHasContent bitvec. This should be
 * invoked at the conclusion of each write-transaction.
 */
static void
btreeClearHasContent(BtShared * pBt)
{
	sqlite3BitvecDestroy(pBt->pHasContent);
	pBt->pHasContent = 0;
}

/*
 * Release all of the apPage[] pages for a cursor.
 */
static void
btreeReleaseAllCursorPages(BtCursor * pCur)
{
	int i;
	for (i = 0; i <= pCur->iPage; i++) {
		releasePage(pCur->apPage[i]);
		pCur->apPage[i] = 0;
	}
	pCur->iPage = -1;
}

/*
 * The cursor passed as the only argument must point to a valid entry
 * when this function is called (i.e. have eState==CURSOR_VALID). This
 * function saves the current cursor key in variables pCur->nKey and
 * pCur->pKey. SQLITE_OK is returned if successful or an SQLite error
 * code otherwise.
 *
 * If the cursor is open on an intkey table, then the integer key
 * (the rowid) is stored in pCur->nKey and pCur->pKey is left set to
 * NULL. If the cursor is open on a non-intkey table, then pCur->pKey is
 * set to point to a malloced buffer pCur->nKey bytes in size containing
 * the key.
 */
static int
saveCursorKey(BtCursor * pCur)
{
	int rc = SQLITE_OK;
	assert(CURSOR_VALID == pCur->eState);
	assert(0 == pCur->pKey);
	assert(cursorHoldsMutex(pCur));

	if (pCur->curIntKey) {
		/* Only the rowid is required for a table btree */
		pCur->nKey = sqlite3BtreeIntegerKey(pCur);
	} else {
		/* For an index btree, save the complete key content */
		void *pKey;
		pCur->nKey = sqlite3BtreePayloadSize(pCur);
		pKey = sqlite3Malloc(pCur->nKey);
		if (pKey) {
			rc = sqlite3BtreePayload(pCur, 0, (int)pCur->nKey,
						 pKey);
			if (rc == SQLITE_OK) {
				pCur->pKey = pKey;
			} else {
				sqlite3_free(pKey);
			}
		} else {
			rc = SQLITE_NOMEM_BKPT;
		}
	}
	assert(!pCur->curIntKey || !pCur->pKey);
	return rc;
}

/*
 * Save the current cursor position in the variables BtCursor.nKey
 * and BtCursor.pKey. The cursor's state is set to CURSOR_REQUIRESEEK.
 *
 * The caller must ensure that the cursor is valid (has eState==CURSOR_VALID)
 * prior to calling this routine.
 */
static int
saveCursorPosition(BtCursor * pCur)
{
	int rc;

	assert(CURSOR_VALID == pCur->eState || CURSOR_SKIPNEXT == pCur->eState);
	assert(0 == pCur->pKey);
	assert(cursorHoldsMutex(pCur));

	if (pCur->eState == CURSOR_SKIPNEXT) {
		pCur->eState = CURSOR_VALID;
	} else {
		pCur->skipNext = 0;
	}

	rc = saveCursorKey(pCur);
	if (rc == SQLITE_OK) {
		btreeReleaseAllCursorPages(pCur);
		pCur->eState = CURSOR_REQUIRESEEK;
	}

	pCur->curFlags &= ~(BTCF_ValidNKey | BTCF_ValidOvfl | BTCF_AtLast);
	return rc;
}

/* Forward reference */
static int SQLITE_NOINLINE saveCursorsOnList(BtCursor *, Pgno, BtCursor *);

/*
 * Save the positions of all cursors (except pExcept) that are open on
 * the table with root-page iRoot.  "Saving the cursor position" means that
 * the location in the btree is remembered in such a way that it can be
 * moved back to the same spot after the btree has been modified.  This
 * routine is called just before cursor pExcept is used to modify the
 * table, for example in BtreeDelete() or BtreeInsert().
 *
 * If there are two or more cursors on the same btree, then all such
 * cursors should have their BTCF_Multiple flag set.  The btreeCursor()
 * routine enforces that rule.  This routine only needs to be called in
 * the uncommon case when pExpect has the BTCF_Multiple flag set.
 *
 * If pExpect!=NULL and if no other cursors are found on the same root-page,
 * then the BTCF_Multiple flag on pExpect is cleared, to avoid another
 * pointless call to this routine.
 *
 * Implementation note:  This routine merely checks to see if any cursors
 * need to be saved.  It calls out to saveCursorsOnList() in the (unusual)
 * event that cursors are in need to being saved.
 */
static int
saveAllCursors(BtShared * pBt, Pgno iRoot, BtCursor * pExcept)
{
	BtCursor *p;
	assert(sqlite3_mutex_held(pBt->mutex));
	assert(pExcept == 0 || pExcept->pBt == pBt);
	for (p = pBt->pCursor; p; p = p->pNext) {
		if (p != pExcept && (0 == iRoot || p->pgnoRoot == iRoot))
			break;
	}
	if (p)
		return saveCursorsOnList(p, iRoot, pExcept);
	if (pExcept)
		pExcept->curFlags &= ~BTCF_Multiple;
	return SQLITE_OK;
}

/* This helper routine to saveAllCursors does the actual work of saving
 * the cursors if and when a cursor is found that actually requires saving.
 * The common case is that no cursors need to be saved, so this routine is
 * broken out from its caller to avoid unnecessary stack pointer movement.
 */
static int SQLITE_NOINLINE
saveCursorsOnList(BtCursor * p,	/* The first cursor that needs saving */
		  Pgno iRoot,	/* Only save cursor with this iRoot. Save all if zero */
		  BtCursor * pExcept	/* Do not save this cursor */
    )
{
	do {
		if (p != pExcept && (0 == iRoot || p->pgnoRoot == iRoot)) {
			if (p->eState == CURSOR_VALID
			    || p->eState == CURSOR_SKIPNEXT) {
				int rc = saveCursorPosition(p);
				if (SQLITE_OK != rc) {
					return rc;
				}
			} else {
				testcase(p->iPage > 0);
				btreeReleaseAllCursorPages(p);
			}
		}
		p = p->pNext;
	} while (p);
	return SQLITE_OK;
}

/*
 * Clear the current cursor position.
 */
void
sqlite3BtreeClearCursor(BtCursor * pCur)
{
	assert(cursorHoldsMutex(pCur));
	sqlite3_free(pCur->pKey);
	pCur->pKey = 0;
	pCur->eState = CURSOR_INVALID;
}

/*
 * In this version of BtreeMoveto, pKey is a packed index record
 * such as is generated by the OP_MakeRecord opcode.  Unpack the
 * record and then call BtreeMovetoUnpacked() to do the work.
 */
static int
btreeMoveto(BtCursor * pCur,	/* Cursor open on the btree to be searched */
	    const void *pKey,	/* Packed key if the btree is an index */
	    i64 nKey,		/* Integer key for tables.  Size of pKey for indices */
	    int bias,		/* Bias search to the high end */
	    int *pRes		/* Write search results here */
    )
{
	int rc;			/* Status code */
	UnpackedRecord *pIdxKey;	/* Unpacked index key */

	if (pKey) {
		assert(nKey == (i64) (int)nKey);
		pIdxKey = sqlite3VdbeAllocUnpackedRecord(pCur->pKeyInfo);
		if (pIdxKey == 0)
			return SQLITE_NOMEM_BKPT;
		sqlite3VdbeRecordUnpackMsgpack(pCur->pKeyInfo, (int)nKey, pKey,
					       pIdxKey);
		if (pIdxKey->nField == 0) {
			rc = SQLITE_CORRUPT_BKPT;
			goto moveto_done;
		}
	} else {
		pIdxKey = 0;
	}
	/* Pass non-existing OP code to signal Tarantool to re-seek cursor.  */
	pIdxKey->opcode = 255;
	rc = sqlite3BtreeMovetoUnpacked(pCur, pIdxKey, nKey, bias, pRes);
 moveto_done:
	if (pIdxKey) {
		sqlite3DbFree(pCur->pKeyInfo->db, pIdxKey);
	}
	return rc;
}

/*
 * Restore the cursor to the position it was in (or as close to as possible)
 * when saveCursorPosition() was called. Note that this call deletes the
 * saved position info stored by saveCursorPosition(), so there can be
 * at most one effective restoreCursorPosition() call after each
 * saveCursorPosition().
 */
static int
btreeRestoreCursorPosition(BtCursor * pCur)
{
	int rc;
	int skipNext;
	assert(cursorOwnsBtShared(pCur));
	assert(pCur->eState >= CURSOR_REQUIRESEEK);
	if (pCur->eState == CURSOR_FAULT) {
		return pCur->skipNext;
	}
	pCur->eState = CURSOR_INVALID;
	rc = btreeMoveto(pCur, pCur->pKey, pCur->nKey, 0, &skipNext);
	if (rc == SQLITE_OK) {
		sqlite3_free(pCur->pKey);
		pCur->pKey = 0;
		assert(pCur->eState == CURSOR_VALID
		       || pCur->eState == CURSOR_INVALID);
		pCur->skipNext |= skipNext;
		if (pCur->skipNext && pCur->eState == CURSOR_VALID) {
			pCur->eState = CURSOR_SKIPNEXT;
		}
	}
	return rc;
}

#define restoreCursorPosition(p) \
  (p->eState>=CURSOR_REQUIRESEEK ? \
         btreeRestoreCursorPosition(p) : \
         SQLITE_OK)

/*
 * Determine whether or not a cursor has moved from the position where
 * it was last placed, or has been invalidated for any other reason.
 * Cursors can move when the row they are pointing at is deleted out
 * from under them, for example.  Cursor might also move if a btree
 * is rebalanced.
 *
 * Calling this routine with a NULL cursor pointer returns false.
 *
 * Use the separate sqlite3BtreeCursorRestore() routine to restore a cursor
 * back to where it ought to be if this routine returns true.
 */
int
sqlite3BtreeCursorHasMoved(BtCursor * pCur)
{
	return pCur->eState != CURSOR_VALID;
}

/*
 * This routine restores a cursor back to its original position after it
 * has been moved by some outside activity (such as a btree rebalance or
 * a row having been deleted out from under the cursor).
 *
 * On success, the *pDifferentRow parameter is false if the cursor is left
 * pointing at exactly the same row.  *pDifferntRow is the row the cursor
 * was pointing to has been deleted, forcing the cursor to point to some
 * nearby row.
 *
 * This routine should only be called for a cursor that just returned
 * TRUE from sqlite3BtreeCursorHasMoved().
 */
int
sqlite3BtreeCursorRestore(BtCursor * pCur, int *pDifferentRow)
{
	int rc;

	assert(pCur != 0);
	assert(pCur->eState != CURSOR_VALID);
	rc = restoreCursorPosition(pCur);
	if (rc) {
		*pDifferentRow = 1;
		return rc;
	}
	if (pCur->eState != CURSOR_VALID) {
		*pDifferentRow = 1;
	} else {
		assert(pCur->skipNext == 0);
		*pDifferentRow = 0;
	}
	return SQLITE_OK;
}

#ifdef SQLITE_ENABLE_CURSOR_HINTS
/*
 * Provide hints to the cursor.  The particular hint given (and the type
 * and number of the varargs parameters) is determined by the eHintType
 * parameter.  See the definitions of the BTREE_HINT_* macros for details.
 */
void
sqlite3BtreeCursorHint(BtCursor * pCur, int eHintType, ...)
{
	/* Used only by system that substitute their own storage engine */
}
#endif

/*
 * Provide flag hints to the cursor.
 */
void
sqlite3BtreeCursorHintFlags(BtCursor * pCur, unsigned x)
{
	assert(x == BTREE_SEEK_EQ || x == BTREE_BULKLOAD || x == 0);
	pCur->hints = x;
}

/*
 * Given a btree page and a cell index (0 means the first cell on
 * the page, 1 means the second cell, and so forth) return a pointer
 * to the cell content.
 *
 * findCellPastPtr() does the same except it skips past the initial
 * 4-byte child pointer found on interior pages, if there is one.
 *
 * This routine works only for pages that do not contain overflow cells.
 */
#define findCell(P,I) \
  ((P)->aData + ((P)->maskPage & get2byteAligned(&(P)->aCellIdx[2*(I)])))
#define findCellPastPtr(P,I) \
  ((P)->aDataOfst + ((P)->maskPage & get2byteAligned(&(P)->aCellIdx[2*(I)])))

/*
 * This is common tail processing for btreeParseCellPtr() and
 * btreeParseCellPtrIndex() for the case when the cell does not fit entirely
 * on a single B-tree page.  Make necessary adjustments to the CellInfo
 * structure.
 */
static SQLITE_NOINLINE void
btreeParseCellAdjustSizeForOverflow(MemPage * pPage,	/* Page containing the cell */
				    u8 * pCell,	/* Pointer to the cell text. */
				    CellInfo * pInfo	/* Fill in this structure */
    )
{
	/* If the payload will not fit completely on the local page, we have
	 * to decide how much to store locally and how much to spill onto
	 * overflow pages.  The strategy is to minimize the amount of unused
	 * space on overflow pages while keeping the amount of local storage
	 * in between minLocal and maxLocal.
	 *
	 * Warning:  changing the way overflow payload is distributed in any
	 * way will result in an incompatible file format.
	 */
	int minLocal;		/* Minimum amount of payload held locally */
	int maxLocal;		/* Maximum amount of payload held locally */
	int surplus;		/* Overflow payload available for local storage */

	minLocal = pPage->minLocal;
	maxLocal = pPage->maxLocal;
	surplus =
	    minLocal + (pInfo->nPayload - minLocal) % (pPage->pBt->usableSize -
						       4);
	testcase(surplus == maxLocal);
	testcase(surplus == maxLocal + 1);
	if (surplus <= maxLocal) {
		pInfo->nLocal = (u16) surplus;
	} else {
		pInfo->nLocal = (u16) minLocal;
	}
	pInfo->nSize = (u16) (&pInfo->pPayload[pInfo->nLocal] - pCell) + 4;
}

/*
 * The following routines are implementations of the MemPage.xParseCell()
 * method.
 *
 * Parse a cell content block and fill in the CellInfo structure.
 *
 * btreeParseCellPtr()        =>   table btree leaf nodes
 * btreeParseCellNoPayload()  =>   table btree internal nodes
 * btreeParseCellPtrIndex()   =>   index btree nodes
 *
 * There is also a wrapper function btreeParseCell() that works for
 * all MemPage types and that references the cell by index rather than
 * by pointer.
 */
static void
btreeParseCellPtrNoPayload(MemPage * pPage,	/* Page containing the cell */
			   u8 * pCell,	/* Pointer to the cell text. */
			   CellInfo * pInfo	/* Fill in this structure */
    )
{
	assert(sqlite3_mutex_held(pPage->pBt->mutex));
	assert(pPage->leaf == 0);
	assert(pPage->childPtrSize == 4);
#ifndef SQLITE_DEBUG
	UNUSED_PARAMETER(pPage);
#endif
	pInfo->nSize = 4 + getVarint(&pCell[4], (u64 *) & pInfo->nKey);
	pInfo->nPayload = 0;
	pInfo->nLocal = 0;
	pInfo->pPayload = 0;
	return;
}

static void
btreeParseCellPtr(MemPage * pPage,	/* Page containing the cell */
		  u8 * pCell,	/* Pointer to the cell text. */
		  CellInfo * pInfo	/* Fill in this structure */
    )
{
	u8 *pIter;		/* For scanning through pCell */
	u32 nPayload;		/* Number of bytes of cell payload */
	u64 iKey;		/* Extracted Key value */

	assert(sqlite3_mutex_held(pPage->pBt->mutex));
	assert(pPage->leaf == 0 || pPage->leaf == 1);
	assert(pPage->intKeyLeaf);
	assert(pPage->childPtrSize == 0);
	pIter = pCell;

	/* The next block of code is equivalent to:
	 *
	 *     pIter += getVarint32(pIter, nPayload);
	 *
	 * The code is inlined to avoid a function call.
	 */
	nPayload = *pIter;
	if (nPayload >= 0x80) {
		u8 *pEnd = &pIter[8];
		nPayload &= 0x7f;
		do {
			nPayload = (nPayload << 7) | (*++pIter & 0x7f);
		} while ((*pIter) >= 0x80 && pIter < pEnd);
	}
	pIter++;

	/* The next block of code is equivalent to:
	 *
	 *     pIter += getVarint(pIter, (u64*)&pInfo->nKey);
	 *
	 * The code is inlined to avoid a function call.
	 */
	iKey = *pIter;
	if (iKey >= 0x80) {
		u8 *pEnd = &pIter[7];
		iKey &= 0x7f;
		while (1) {
			iKey = (iKey << 7) | (*++pIter & 0x7f);
			if ((*pIter) < 0x80)
				break;
			if (pIter >= pEnd) {
				iKey = (iKey << 8) | *++pIter;
				break;
			}
		}
	}
	pIter++;

	pInfo->nKey = *(i64 *) & iKey;
	pInfo->nPayload = nPayload;
	pInfo->pPayload = pIter;
	testcase(nPayload == pPage->maxLocal);
	testcase(nPayload == pPage->maxLocal + 1);
	if (nPayload <= pPage->maxLocal) {
		/* This is the (easy) common case where the entire payload fits
		 * on the local page.  No overflow is required.
		 */
		pInfo->nSize = nPayload + (u16) (pIter - pCell);
		if (pInfo->nSize < 4)
			pInfo->nSize = 4;
		pInfo->nLocal = (u16) nPayload;
	} else {
		btreeParseCellAdjustSizeForOverflow(pPage, pCell, pInfo);
	}
}

static void
btreeParseCellPtrIndex(MemPage * pPage,	/* Page containing the cell */
		       u8 * pCell,	/* Pointer to the cell text. */
		       CellInfo * pInfo	/* Fill in this structure */
    )
{
	u8 *pIter;		/* For scanning through pCell */
	u32 nPayload;		/* Number of bytes of cell payload */

	assert(sqlite3_mutex_held(pPage->pBt->mutex));
	assert(pPage->leaf == 0 || pPage->leaf == 1);
	assert(pPage->intKeyLeaf == 0);
	pIter = pCell + pPage->childPtrSize;
	nPayload = *pIter;
	if (nPayload >= 0x80) {
		u8 *pEnd = &pIter[8];
		nPayload &= 0x7f;
		do {
			nPayload = (nPayload << 7) | (*++pIter & 0x7f);
		} while (*(pIter) >= 0x80 && pIter < pEnd);
	}
	pIter++;
	pInfo->nKey = nPayload;
	pInfo->nPayload = nPayload;
	pInfo->pPayload = pIter;
	testcase(nPayload == pPage->maxLocal);
	testcase(nPayload == pPage->maxLocal + 1);
	if (nPayload <= pPage->maxLocal) {
		/* This is the (easy) common case where the entire payload fits
		 * on the local page.  No overflow is required.
		 */
		pInfo->nSize = nPayload + (u16) (pIter - pCell);
		if (pInfo->nSize < 4)
			pInfo->nSize = 4;
		pInfo->nLocal = (u16) nPayload;
	} else {
		btreeParseCellAdjustSizeForOverflow(pPage, pCell, pInfo);
	}
}

static void
btreeParseCell(MemPage * pPage,	/* Page containing the cell */
	       int iCell,	/* The cell index.  First cell is 0 */
	       CellInfo * pInfo	/* Fill in this structure */
    )
{
	pPage->xParseCell(pPage, findCell(pPage, iCell), pInfo);
}

/*
 * The following routines are implementations of the MemPage.xCellSize
 * method.
 *
 * Compute the total number of bytes that a Cell needs in the cell
 * data area of the btree-page.  The return number includes the cell
 * data header and the local payload, but not any overflow page or
 * the space used by the cell pointer.
 *
 * cellSizePtrNoPayload()    =>   table internal nodes
 * cellSizePtr()             =>   all index nodes & table leaf nodes
 */
static u16
cellSizePtr(MemPage * pPage, u8 * pCell)
{
	u8 *pIter = pCell + pPage->childPtrSize;	/* For looping over bytes of pCell */
	u8 *pEnd;		/* End mark for a varint */
	u32 nSize;		/* Size value to return */

#ifdef SQLITE_DEBUG
	/* The value returned by this function should always be the same as
	 * the (CellInfo.nSize) value found by doing a full parse of the
	 * cell. If SQLITE_DEBUG is defined, an assert() at the bottom of
	 * this function verifies that this invariant is not violated.
	 */
	CellInfo debuginfo;
	pPage->xParseCell(pPage, pCell, &debuginfo);
#endif

	nSize = *pIter;
	if (nSize >= 0x80) {
		pEnd = &pIter[8];
		nSize &= 0x7f;
		do {
			nSize = (nSize << 7) | (*++pIter & 0x7f);
		} while (*(pIter) >= 0x80 && pIter < pEnd);
	}
	pIter++;
	if (pPage->intKey) {
		/* pIter now points at the 64-bit integer key value, a variable length
		 * integer. The following block moves pIter to point at the first byte
		 * past the end of the key value.
		 */
		pEnd = &pIter[9];
		while ((*pIter++) & 0x80 && pIter < pEnd) ;
	}
	testcase(nSize == pPage->maxLocal);
	testcase(nSize == pPage->maxLocal + 1);
	if (nSize <= pPage->maxLocal) {
		nSize += (u32) (pIter - pCell);
		if (nSize < 4)
			nSize = 4;
	} else {
		int minLocal = pPage->minLocal;
		nSize =
		    minLocal + (nSize - minLocal) % (pPage->pBt->usableSize -
						     4);
		testcase(nSize == pPage->maxLocal);
		testcase(nSize == pPage->maxLocal + 1);
		if (nSize > pPage->maxLocal) {
			nSize = minLocal;
		}
		nSize += 4 + (u16) (pIter - pCell);
	}
	assert(nSize == debuginfo.nSize || CORRUPT_DB);
	return (u16) nSize;
}

static u16
cellSizePtrNoPayload(MemPage * pPage, u8 * pCell)
{
	u8 *pIter = pCell + 4;	/* For looping over bytes of pCell */
	u8 *pEnd;		/* End mark for a varint */

#ifdef SQLITE_DEBUG
	/* The value returned by this function should always be the same as
	 * the (CellInfo.nSize) value found by doing a full parse of the
	 * cell. If SQLITE_DEBUG is defined, an assert() at the bottom of
	 * this function verifies that this invariant is not violated.
	 */
	CellInfo debuginfo;
	pPage->xParseCell(pPage, pCell, &debuginfo);
#else
	UNUSED_PARAMETER(pPage);
#endif

	assert(pPage->childPtrSize == 4);
	pEnd = pIter + 9;
	while ((*pIter++) & 0x80 && pIter < pEnd) ;
	assert(debuginfo.nSize == (u16) (pIter - pCell) || CORRUPT_DB);
	return (u16) (pIter - pCell);
}

#ifdef SQLITE_DEBUG
/* This variation on cellSizePtr() is used inside of assert() statements
 * only.
 */
static u16
cellSize(MemPage * pPage, int iCell)
{
	return pPage->xCellSize(pPage, findCell(pPage, iCell));
}
#endif

/*
 * Defragment the page given.  All Cells are moved to the
 * end of the page and all free space is collected into one
 * big FreeBlk that occurs in between the header and cell
 * pointer array and the cell content area.
 *
 * EVIDENCE-OF: R-44582-60138 SQLite may from time to time reorganize a
 * b-tree page so that there are no freeblocks or fragment bytes, all
 * unused bytes are contained in the unallocated space region, and all
 * cells are packed tightly at the end of the page.
 */
static int
defragmentPage(MemPage * pPage)
{
	int i;			/* Loop counter */
	int pc;			/* Address of the i-th cell */
	int hdr;		/* Offset to the page header */
	int size;		/* Size of a cell */
	int usableSize;		/* Number of usable bytes on a page */
	int cellOffset;		/* Offset to the cell pointer array */
	int cbrk;		/* Offset to the cell content area */
	int nCell;		/* Number of cells on the page */
	unsigned char *data;	/* The page data */
	unsigned char *temp;	/* Temp area for cell content */
	unsigned char *src;	/* Source of content */
	int iCellFirst;		/* First allowable cell index */
	int iCellLast;		/* Last possible cell index */

	assert(sqlite3PagerIswriteable(pPage->pDbPage));
	assert(pPage->pBt != 0);
	assert(pPage->pBt->usableSize <= SQLITE_MAX_PAGE_SIZE);
	assert(pPage->nOverflow == 0);
	assert(sqlite3_mutex_held(pPage->pBt->mutex));
	temp = 0;
	src = data = pPage->aData;
	hdr = pPage->hdrOffset;
	cellOffset = pPage->cellOffset;
	nCell = pPage->nCell;
	assert(nCell == get2byte(&data[hdr + 3]));
	usableSize = pPage->pBt->usableSize;
	cbrk = usableSize;
	iCellFirst = cellOffset + 2 * nCell;
	iCellLast = usableSize - 4;
	for (i = 0; i < nCell; i++) {
		u8 *pAddr;	/* The i-th cell pointer */
		pAddr = &data[cellOffset + i * 2];
		pc = get2byte(pAddr);
		testcase(pc == iCellFirst);
		testcase(pc == iCellLast);
		/* These conditions have already been verified in btreeInitPage()
		 */
		if (pc < iCellFirst || pc > iCellLast) {
			return SQLITE_CORRUPT_BKPT;
		}
		assert(pc >= iCellFirst && pc <= iCellLast);
		size = pPage->xCellSize(pPage, &src[pc]);
		cbrk -= size;
		if (cbrk < iCellFirst || pc + size > usableSize) {
			return SQLITE_CORRUPT_BKPT;
		}
		assert(cbrk + size <= usableSize && cbrk >= iCellFirst);
		testcase(cbrk + size == usableSize);
		testcase(pc + size == usableSize);
		put2byte(pAddr, cbrk);
		if (temp == 0) {
			int x;
			if (cbrk == pc)
				continue;
			temp = sqlite3PagerTempSpace(pPage->pBt->pPager);
			x = get2byte(&data[hdr + 5]);
			memcpy(&temp[x], &data[x], (cbrk + size) - x);
			src = temp;
		}
		memcpy(&data[cbrk], &src[pc], size);
	}
	assert(cbrk >= iCellFirst);
	put2byte(&data[hdr + 5], cbrk);
	data[hdr + 1] = 0;
	data[hdr + 2] = 0;
	data[hdr + 7] = 0;
	memset(&data[iCellFirst], 0, cbrk - iCellFirst);
	assert(sqlite3PagerIswriteable(pPage->pDbPage));
	if (cbrk - iCellFirst != pPage->nFree) {
		return SQLITE_CORRUPT_BKPT;
	}
	return SQLITE_OK;
}

/*
 * Search the free-list on page pPg for space to store a cell nByte bytes in
 * size. If one can be found, return a pointer to the space and remove it
 * from the free-list.
 *
 * If no suitable space can be found on the free-list, return NULL.
 *
 * This function may detect corruption within pPg.  If corruption is
 * detected then *pRc is set to SQLITE_CORRUPT and NULL is returned.
 *
 * Slots on the free list that are between 1 and 3 bytes larger than nByte
 * will be ignored if adding the extra space to the fragmentation count
 * causes the fragmentation count to exceed 60.
 */
static u8 *
pageFindSlot(MemPage * pPg, int nByte, int *pRc)
{
	const int hdr = pPg->hdrOffset;
	u8 *const aData = pPg->aData;
	int iAddr = hdr + 1;
	int pc = get2byte(&aData[iAddr]);
	int x;
	int usableSize = pPg->pBt->usableSize;

	assert(pc > 0);
	do {
		int size;	/* Size of the free slot */
		/* EVIDENCE-OF: R-06866-39125 Freeblocks are always connected in order of
		 * increasing offset.
		 */
		if (pc > usableSize - 4 || pc < iAddr + 4) {
			*pRc = SQLITE_CORRUPT_BKPT;
			return 0;
		}
		/* EVIDENCE-OF: R-22710-53328 The third and fourth bytes of each
		 * freeblock form a big-endian integer which is the size of the freeblock
		 * in bytes, including the 4-byte header.
		 */
		size = get2byte(&aData[pc + 2]);
		if ((x = size - nByte) >= 0) {
			testcase(x == 4);
			testcase(x == 3);
			if (pc < pPg->cellOffset + 2 * pPg->nCell
			    || size + pc > usableSize) {
				*pRc = SQLITE_CORRUPT_BKPT;
				return 0;
			} else if (x < 4) {
				/* EVIDENCE-OF: R-11498-58022 In a well-formed b-tree page, the total
				 * number of bytes in fragments may not exceed 60.
				 */
				if (aData[hdr + 7] > 57)
					return 0;

				/* Remove the slot from the free-list. Update the number of
				 * fragmented bytes within the page.
				 */
				memcpy(&aData[iAddr], &aData[pc], 2);
				aData[hdr + 7] += (u8) x;
			} else {
				/* The slot remains on the free-list. Reduce its size to account
				 * for the portion used by the new allocation.
				 */
				put2byte(&aData[pc + 2], x);
			}
			return &aData[pc + x];
		}
		iAddr = pc;
		pc = get2byte(&aData[pc]);
	} while (pc);

	return 0;
}

/*
 * Allocate nByte bytes of space from within the B-Tree page passed
 * as the first argument. Write into *pIdx the index into pPage->aData[]
 * of the first byte of allocated space. Return either SQLITE_OK or
 * an error code (usually SQLITE_CORRUPT).
 *
 * The caller guarantees that there is sufficient space to make the
 * allocation.  This routine might need to defragment in order to bring
 * all the space together, however.  This routine will avoid using
 * the first two bytes past the cell pointer area since presumably this
 * allocation is being made in order to insert a new cell, so we will
 * also end up needing a new cell pointer.
 */
static int
allocateSpace(MemPage * pPage, int nByte, int *pIdx)
{
	const int hdr = pPage->hdrOffset;	/* Local cache of pPage->hdrOffset */
	u8 *const data = pPage->aData;	/* Local cache of pPage->aData */
	int top;		/* First byte of cell content area */
	int rc = SQLITE_OK;	/* Integer return code */
	int gap;		/* First byte of gap between cell pointers and cell content */

	assert(sqlite3PagerIswriteable(pPage->pDbPage));
	assert(pPage->pBt);
	assert(sqlite3_mutex_held(pPage->pBt->mutex));
	assert(nByte >= 0);	/* Minimum cell size is 4 */
	assert(pPage->nFree >= nByte);
	assert(pPage->nOverflow == 0);
	assert(nByte < (int)(pPage->pBt->usableSize - 8));

	assert(pPage->cellOffset == hdr + 12 - 4 * pPage->leaf);
	gap = pPage->cellOffset + 2 * pPage->nCell;
	assert(gap <= 65536);
	/* EVIDENCE-OF: R-29356-02391 If the database uses a 65536-byte page size
	 * and the reserved space is zero (the usual value for reserved space)
	 * then the cell content offset of an empty page wants to be 65536.
	 * However, that integer is too large to be stored in a 2-byte unsigned
	 * integer, so a value of 0 is used in its place.
	 */
	top = get2byte(&data[hdr + 5]);
	assert(top <= (int)pPage->pBt->usableSize);	/* Prevent by getAndInitPage() */
	if (gap > top) {
		if (top == 0 && pPage->pBt->usableSize == 65536) {
			top = 65536;
		} else {
			return SQLITE_CORRUPT_BKPT;
		}
	}

	/* If there is enough space between gap and top for one more cell pointer
	 * array entry offset, and if the freelist is not empty, then search the
	 * freelist looking for a free slot big enough to satisfy the request.
	 */
	testcase(gap + 2 == top);
	testcase(gap + 1 == top);
	testcase(gap == top);
	if ((data[hdr + 2] || data[hdr + 1]) && gap + 2 <= top) {
		u8 *pSpace = pageFindSlot(pPage, nByte, &rc);
		if (pSpace) {
			assert(pSpace >= data && (pSpace - data) < 65536);
			*pIdx = (int)(pSpace - data);
			return SQLITE_OK;
		} else if (rc) {
			return rc;
		}
	}

	/* The request could not be fulfilled using a freelist slot.  Check
	 * to see if defragmentation is necessary.
	 */
	testcase(gap + 2 + nByte == top);
	if (gap + 2 + nByte > top) {
		assert(pPage->nCell > 0 || CORRUPT_DB);
		rc = defragmentPage(pPage);
		if (rc)
			return rc;
		top = get2byteNotZero(&data[hdr + 5]);
		assert(gap + nByte <= top);
	}

	/* Allocate memory from the gap in between the cell pointer array
	 * and the cell content area.  The btreeInitPage() call has already
	 * validated the freelist.  Given that the freelist is valid, there
	 * is no way that the allocation can extend off the end of the page.
	 * The assert() below verifies the previous sentence.
	 */
	top -= nByte;
	put2byte(&data[hdr + 5], top);
	assert(top + nByte <= (int)pPage->pBt->usableSize);
	*pIdx = top;
	return SQLITE_OK;
}

/*
 * Return a section of the pPage->aData to the freelist.
 * The first byte of the new free block is pPage->aData[iStart]
 * and the size of the block is iSize bytes.
 *
 * Adjacent freeblocks are coalesced.
 *
 * Note that even though the freeblock list was checked by btreeInitPage(),
 * that routine will not detect overlap between cells or freeblocks.  Nor
 * does it detect cells or freeblocks that encrouch into the reserved bytes
 * at the end of the page.  So do additional corruption checks inside this
 * routine and return SQLITE_CORRUPT if any problems are found.
 */
static int
freeSpace(MemPage * pPage, u16 iStart, u16 iSize)
{
	u16 iPtr;		/* Address of ptr to next freeblock */
	u16 iFreeBlk;		/* Address of the next freeblock */
	u8 hdr;			/* Page header size.  0 or 100 */
	u8 nFrag = 0;		/* Reduction in fragmentation */
	u16 iOrigSize = iSize;	/* Original value of iSize */
	u32 iLast = pPage->pBt->usableSize - 4;	/* Largest possible freeblock offset */
	u32 iEnd = iStart + iSize;	/* First byte past the iStart buffer */
	unsigned char *data = pPage->aData;	/* Page content */

	assert(pPage->pBt != 0);
	assert(sqlite3PagerIswriteable(pPage->pDbPage));
	assert(CORRUPT_DB
	       || iStart >= pPage->hdrOffset + 6 + pPage->childPtrSize);
	assert(CORRUPT_DB || iEnd <= pPage->pBt->usableSize);
	assert(sqlite3_mutex_held(pPage->pBt->mutex));
	assert(iSize >= 4);	/* Minimum cell size is 4 */
	assert(iStart <= iLast);

	/* Overwrite deleted information with zeros when the secure_delete
	 * option is enabled
	 */
	if (pPage->pBt->btsFlags & BTS_SECURE_DELETE) {
		memset(&data[iStart], 0, iSize);
	}

	/* The list of freeblocks must be in ascending order.  Find the
	 * spot on the list where iStart should be inserted.
	 */
	hdr = pPage->hdrOffset;
	iPtr = hdr + 1;
	if (data[iPtr + 1] == 0 && data[iPtr] == 0) {
		iFreeBlk = 0;	/* Shortcut for the case when the freelist is empty */
	} else {
		while ((iFreeBlk = get2byte(&data[iPtr])) < iStart) {
			if (iFreeBlk < iPtr + 4) {
				if (iFreeBlk == 0)
					break;
				return SQLITE_CORRUPT_BKPT;
			}
			iPtr = iFreeBlk;
		}
		if (iFreeBlk > iLast)
			return SQLITE_CORRUPT_BKPT;
		assert(iFreeBlk > iPtr || iFreeBlk == 0);

		/* At this point:
		 *    iFreeBlk:   First freeblock after iStart, or zero if none
		 *    iPtr:       The address of a pointer to iFreeBlk
		 *
		 * Check to see if iFreeBlk should be coalesced onto the end of iStart.
		 */
		if (iFreeBlk && iEnd + 3 >= iFreeBlk) {
			nFrag = iFreeBlk - iEnd;
			if (iEnd > iFreeBlk)
				return SQLITE_CORRUPT_BKPT;
			iEnd = iFreeBlk + get2byte(&data[iFreeBlk + 2]);
			if (iEnd > pPage->pBt->usableSize)
				return SQLITE_CORRUPT_BKPT;
			iSize = iEnd - iStart;
			iFreeBlk = get2byte(&data[iFreeBlk]);
		}

		/* If iPtr is another freeblock (that is, if iPtr is not the freelist
		 * pointer in the page header) then check to see if iStart should be
		 * coalesced onto the end of iPtr.
		 */
		if (iPtr > hdr + 1) {
			int iPtrEnd = iPtr + get2byte(&data[iPtr + 2]);
			if (iPtrEnd + 3 >= iStart) {
				if (iPtrEnd > iStart)
					return SQLITE_CORRUPT_BKPT;
				nFrag += iStart - iPtrEnd;
				iSize = iEnd - iPtr;
				iStart = iPtr;
			}
		}
		if (nFrag > data[hdr + 7])
			return SQLITE_CORRUPT_BKPT;
		data[hdr + 7] -= nFrag;
	}
	if (iStart == get2byte(&data[hdr + 5])) {
		/* The new freeblock is at the beginning of the cell content area,
		 * so just extend the cell content area rather than create another
		 * freelist entry
		 */
		if (iPtr != hdr + 1)
			return SQLITE_CORRUPT_BKPT;
		put2byte(&data[hdr + 1], iFreeBlk);
		put2byte(&data[hdr + 5], iEnd);
	} else {
		/* Insert the new freeblock into the freelist */
		put2byte(&data[iPtr], iStart);
		put2byte(&data[iStart], iFreeBlk);
		put2byte(&data[iStart + 2], iSize);
	}
	pPage->nFree += iOrigSize;
	return SQLITE_OK;
}

/*
 * Decode the flags byte (the first byte of the header) for a page
 * and initialize fields of the MemPage structure accordingly.
 *
 * Only the following combinations are supported.  Anything different
 * indicates a corrupt database files:
 *
 *         PTF_ZERODATA
 *         PTF_ZERODATA | PTF_LEAF
 *         PTF_LEAFDATA | PTF_INTKEY
 *         PTF_LEAFDATA | PTF_INTKEY | PTF_LEAF
 */
static int
decodeFlags(MemPage * pPage, int flagByte)
{
	BtShared *pBt;		/* A copy of pPage->pBt */

	assert(pPage->hdrOffset == (pPage->pgno == 1 ? 100 : 0));
	assert(sqlite3_mutex_held(pPage->pBt->mutex));
	pPage->leaf = (u8) (flagByte >> 3);
	assert(PTF_LEAF == 1 << 3);
	flagByte &= ~PTF_LEAF;
	pPage->childPtrSize = 4 - 4 * pPage->leaf;
	pPage->xCellSize = cellSizePtr;
	pBt = pPage->pBt;
	if (flagByte == (PTF_LEAFDATA | PTF_INTKEY)) {
		/* EVIDENCE-OF: R-07291-35328 A value of 5 (0x05) means the page is an
		 * interior table b-tree page.
		 */
		assert((PTF_LEAFDATA | PTF_INTKEY) == 5);
		/* EVIDENCE-OF: R-26900-09176 A value of 13 (0x0d) means the page is a
		 * leaf table b-tree page.
		 */
		assert((PTF_LEAFDATA | PTF_INTKEY | PTF_LEAF) == 13);
		pPage->intKey = 1;
		if (pPage->leaf) {
			pPage->intKeyLeaf = 1;
			pPage->xParseCell = btreeParseCellPtr;
		} else {
			pPage->intKeyLeaf = 0;
			pPage->xCellSize = cellSizePtrNoPayload;
			pPage->xParseCell = btreeParseCellPtrNoPayload;
		}
		pPage->maxLocal = pBt->maxLeaf;
		pPage->minLocal = pBt->minLeaf;
	} else if (flagByte == PTF_ZERODATA) {
		/* EVIDENCE-OF: R-43316-37308 A value of 2 (0x02) means the page is an
		 * interior index b-tree page.
		 */
		assert((PTF_ZERODATA) == 2);
		/* EVIDENCE-OF: R-59615-42828 A value of 10 (0x0a) means the page is a
		 * leaf index b-tree page.
		 */
		assert((PTF_ZERODATA | PTF_LEAF) == 10);
		pPage->intKey = 0;
		pPage->intKeyLeaf = 0;
		pPage->xParseCell = btreeParseCellPtrIndex;
		pPage->maxLocal = pBt->maxLocal;
		pPage->minLocal = pBt->minLocal;
	} else {
		/* EVIDENCE-OF: R-47608-56469 Any other value for the b-tree page type is
		 * an error.
		 */
		return SQLITE_CORRUPT_BKPT;
	}
	pPage->max1bytePayload = pBt->max1bytePayload;
	return SQLITE_OK;
}

/*
 * Initialize the auxiliary information for a disk block.
 *
 * Return SQLITE_OK on success.  If we see that the page does
 * not contain a well-formed database page, then return
 * SQLITE_CORRUPT.  Note that a return of SQLITE_OK does not
 * guarantee that the page is well-formed.  It only shows that
 * we failed to detect any corruption.
 */
static int
btreeInitPage(MemPage * pPage)
{

	assert(pPage->pBt != 0);
	assert(pPage->pBt->db != 0);
	assert(sqlite3_mutex_held(pPage->pBt->mutex));
	assert(pPage->pgno == sqlite3PagerPagenumber(pPage->pDbPage));
	assert(pPage == sqlite3PagerGetExtra(pPage->pDbPage));
	assert(pPage->aData == sqlite3PagerGetData(pPage->pDbPage));

	if (!pPage->isInit) {
		int pc;		/* Address of a freeblock within pPage->aData[] */
		u8 hdr;		/* Offset to beginning of page header */
		u8 *data;	/* Equal to pPage->aData */
		BtShared *pBt;	/* The main btree structure */
		int usableSize;	/* Amount of usable space on each page */
		u16 cellOffset;	/* Offset from start of page to first cell pointer */
		int nFree;	/* Number of unused bytes on the page */
		int top;	/* First byte of the cell content area */
		int iCellFirst;	/* First allowable cell or freeblock offset */
		int iCellLast;	/* Last possible cell or freeblock offset */

		pBt = pPage->pBt;

		hdr = pPage->hdrOffset;
		data = pPage->aData;
		/* EVIDENCE-OF: R-28594-02890 The one-byte flag at offset 0 indicating
		 * the b-tree page type.
		 */
		if (decodeFlags(pPage, data[hdr]))
			return SQLITE_CORRUPT_BKPT;
		assert(pBt->pageSize >= 512 && pBt->pageSize <= 65536);
		pPage->maskPage = (u16) (pBt->pageSize - 1);
		pPage->nOverflow = 0;
		usableSize = pBt->usableSize;
		pPage->cellOffset = cellOffset = hdr + 8 + pPage->childPtrSize;
		pPage->aDataEnd = &data[usableSize];
		pPage->aCellIdx = &data[cellOffset];
		pPage->aDataOfst = &data[pPage->childPtrSize];
		/* EVIDENCE-OF: R-58015-48175 The two-byte integer at offset 5 designates
		 * the start of the cell content area. A zero value for this integer is
		 * interpreted as 65536.
		 */
		top = get2byteNotZero(&data[hdr + 5]);
		/* EVIDENCE-OF: R-37002-32774 The two-byte integer at offset 3 gives the
		 * number of cells on the page.
		 */
		pPage->nCell = get2byte(&data[hdr + 3]);
		if (pPage->nCell > MX_CELL(pBt)) {
			/* To many cells for a single page.  The page must be corrupt */
			return SQLITE_CORRUPT_BKPT;
		}
		testcase(pPage->nCell == MX_CELL(pBt));
		/* EVIDENCE-OF: R-24089-57979 If a page contains no cells (which is only
		 * possible for a root page of a table that contains no rows) then the
		 * offset to the cell content area will equal the page size minus the
		 * bytes of reserved space.
		 */
		assert(pPage->nCell > 0 || top == usableSize || CORRUPT_DB);

		/* A malformed database page might cause us to read past the end
		 * of page when parsing a cell.
		 *
		 * The following block of code checks early to see if a cell extends
		 * past the end of a page boundary and causes SQLITE_CORRUPT to be
		 * returned if it does.
		 */
		iCellFirst = cellOffset + 2 * pPage->nCell;
		iCellLast = usableSize - 4;

		/* Compute the total free space on the page
		 * EVIDENCE-OF: R-23588-34450 The two-byte integer at offset 1 gives the
		 * start of the first freeblock on the page, or is zero if there are no
		 * freeblocks.
		 */
		pc = get2byte(&data[hdr + 1]);
		nFree = data[hdr + 7] + top;	/* Init nFree to non-freeblock free space */
		if (pc > 0) {
			u32 next, size;
			if (pc < iCellFirst) {
				/* EVIDENCE-OF: R-55530-52930 In a well-formed b-tree page, there will
				 * always be at least one cell before the first freeblock.
				 */
				return SQLITE_CORRUPT_BKPT;
			}
			while (1) {
				if (pc > iCellLast) {
					return SQLITE_CORRUPT_BKPT;	/* Freeblock off the end of the page */
				}
				next = get2byte(&data[pc]);
				size = get2byte(&data[pc + 2]);
				nFree = nFree + size;
				if (next <= pc + size + 3)
					break;
				pc = next;
			}
			if (next > 0) {
				return SQLITE_CORRUPT_BKPT;	/* Freeblock not in ascending order */
			}
			if (pc + size > (unsigned int)usableSize) {
				return SQLITE_CORRUPT_BKPT;	/* Last freeblock extends past page end */
			}
		}

		/* At this point, nFree contains the sum of the offset to the start
		 * of the cell-content area plus the number of free bytes within
		 * the cell-content area. If this is greater than the usable-size
		 * of the page, then the page must be corrupted. This check also
		 * serves to verify that the offset to the start of the cell-content
		 * area, according to the page header, lies within the page.
		 */
		if (nFree > usableSize) {
			return SQLITE_CORRUPT_BKPT;
		}
		pPage->nFree = (u16) (nFree - iCellFirst);
		pPage->isInit = 1;
	}
	return SQLITE_OK;
}

/*
 * Set up a raw page so that it looks like a database page holding
 * no entries.
 */
static void
zeroPage(MemPage * pPage, int flags)
{
	unsigned char *data = pPage->aData;
	BtShared *pBt = pPage->pBt;
	u8 hdr = pPage->hdrOffset;
	u16 first;

	assert(sqlite3PagerPagenumber(pPage->pDbPage) == pPage->pgno);
	assert(sqlite3PagerGetExtra(pPage->pDbPage) == (void *)pPage);
	assert(sqlite3PagerGetData(pPage->pDbPage) == data);
	assert(sqlite3PagerIswriteable(pPage->pDbPage));
	assert(sqlite3_mutex_held(pBt->mutex));
	if (pBt->btsFlags & BTS_SECURE_DELETE) {
		memset(&data[hdr], 0, pBt->usableSize - hdr);
	}
	data[hdr] = (char)flags;
	first = hdr + ((flags & PTF_LEAF) == 0 ? 12 : 8);
	memset(&data[hdr + 1], 0, 4);
	data[hdr + 7] = 0;
	put2byte(&data[hdr + 5], pBt->usableSize);
	pPage->nFree = (u16) (pBt->usableSize - first);
	decodeFlags(pPage, flags);
	pPage->cellOffset = first;
	pPage->aDataEnd = &data[pBt->usableSize];
	pPage->aCellIdx = &data[first];
	pPage->aDataOfst = &data[pPage->childPtrSize];
	pPage->nOverflow = 0;
	assert(pBt->pageSize >= 512 && pBt->pageSize <= 65536);
	pPage->maskPage = (u16) (pBt->pageSize - 1);
	pPage->nCell = 0;
	pPage->isInit = 1;
}

/*
 * Convert a DbPage obtained from the pager into a MemPage used by
 * the btree layer.
 */
static MemPage *
btreePageFromDbPage(DbPage * pDbPage, Pgno pgno, BtShared * pBt)
{
	MemPage *pPage = (MemPage *) sqlite3PagerGetExtra(pDbPage);
	if (pgno != pPage->pgno) {
		pPage->aData = sqlite3PagerGetData(pDbPage);
		pPage->pDbPage = pDbPage;
		pPage->pBt = pBt;
		pPage->pgno = pgno;
		pPage->hdrOffset = pgno == 1 ? 100 : 0;
	}
	assert(pPage->aData == sqlite3PagerGetData(pDbPage));
	return pPage;
}

/*
 * Get a page from the pager.  Initialize the MemPage.pBt and
 * MemPage.aData elements if needed.  See also: btreeGetUnusedPage().
 *
 * If the PAGER_GET_NOCONTENT flag is set, it means that we do not care
 * about the content of the page at this time.  So do not go to the disk
 * to fetch the content.  Just fill in the content with zeros for now.
 * If in the future we call sqlite3PagerWrite() on this page, that
 * means we have started to be concerned about content and the disk
 * read should occur at that point.
 */
static int
btreeGetPage(BtShared * pBt,	/* The btree */
	     Pgno pgno,		/* Number of the page to fetch */
	     MemPage ** ppPage,	/* Return the page in this parameter */
	     int flags		/* PAGER_GET_NOCONTENT or PAGER_GET_READONLY */
    )
{
	int rc;
	DbPage *pDbPage;

	assert(flags == 0 || flags == PAGER_GET_NOCONTENT
	       || flags == PAGER_GET_READONLY);
	assert(sqlite3_mutex_held(pBt->mutex));
	rc = sqlite3PagerGet(pBt->pPager, pgno, (DbPage **) & pDbPage, flags);
	if (rc)
		return rc;
	*ppPage = btreePageFromDbPage(pDbPage, pgno, pBt);
	return SQLITE_OK;
}

/*
 * Retrieve a page from the pager cache. If the requested page is not
 * already in the pager cache return NULL. Initialize the MemPage.pBt and
 * MemPage.aData elements if needed.
 */
static MemPage *
btreePageLookup(BtShared * pBt, Pgno pgno)
{
	DbPage *pDbPage;
	assert(sqlite3_mutex_held(pBt->mutex));
	pDbPage = sqlite3PagerLookup(pBt->pPager, pgno);
	if (pDbPage) {
		return btreePageFromDbPage(pDbPage, pgno, pBt);
	}
	return 0;
}

/*
 * Return the size of the database file in pages. If there is any kind of
 * error, return ((unsigned int)-1).
 */
static Pgno
btreePagecount(BtShared * pBt)
{
	return pBt->nPage;
}

u32
sqlite3BtreeLastPage(Btree * p)
{
	assert(sqlite3BtreeHoldsMutex(p));
	assert(((p->pBt->nPage) & 0x8000000) == 0);
	return btreePagecount(p->pBt);
}

/*
 * Get a page from the pager and initialize it.
 *
 * If pCur!=0 then the page is being fetched as part of a moveToChild()
 * call.  Do additional sanity checking on the page in this case.
 * And if the fetch fails, this routine must decrement pCur->iPage.
 *
 * The page is fetched as read-write unless pCur is not NULL and is
 * a read-only cursor.
 *
 * If an error occurs, then *ppPage is undefined. It
 * may remain unchanged, or it may be set to an invalid value.
 */
static int
getAndInitPage(BtShared * pBt,	/* The database file */
	       Pgno pgno,	/* Number of the page to get */
	       MemPage ** ppPage,	/* Write the page pointer here */
	       BtCursor * pCur,	/* Cursor to receive the page, or NULL */
	       int bReadOnly	/* True for a read-only page */
    )
{
	int rc;
	DbPage *pDbPage;
	assert(sqlite3_mutex_held(pBt->mutex));
	assert(pCur == 0 || ppPage == &pCur->apPage[pCur->iPage]);
	assert(pCur == 0 || bReadOnly == pCur->curPagerFlags);
	assert(pCur == 0 || pCur->iPage > 0);

	if (pgno > btreePagecount(pBt)) {
		rc = SQLITE_CORRUPT_BKPT;
		goto getAndInitPage_error;
	}
	rc = sqlite3PagerGet(pBt->pPager, pgno, (DbPage **) & pDbPage,
			     bReadOnly);
	if (rc) {
		goto getAndInitPage_error;
	}
	*ppPage = (MemPage *) sqlite3PagerGetExtra(pDbPage);
	if ((*ppPage)->isInit == 0) {
		btreePageFromDbPage(pDbPage, pgno, pBt);
		rc = btreeInitPage(*ppPage);
		if (rc != SQLITE_OK) {
			releasePage(*ppPage);
			goto getAndInitPage_error;
		}
	}
	assert((*ppPage)->pgno == pgno);
	assert((*ppPage)->aData == sqlite3PagerGetData(pDbPage));

	/* If obtaining a child page for a cursor, we must verify that the page is
	 * compatible with the root page.
	 */
	if (pCur
	    && ((*ppPage)->nCell < 1 || (*ppPage)->intKey != pCur->curIntKey)) {
		rc = SQLITE_CORRUPT_BKPT;
		releasePage(*ppPage);
		goto getAndInitPage_error;
	}
	return SQLITE_OK;

 getAndInitPage_error:
	if (pCur)
		pCur->iPage--;
	testcase(pgno == 0);
	assert(pgno != 0 || rc == SQLITE_CORRUPT);
	return rc;
}

/*
 * Release a MemPage.  This should be called once for each prior
 * call to btreeGetPage.
 */
static void
releasePageNotNull(MemPage * pPage)
{
	assert(pPage->aData);
	assert(pPage->pBt);
	assert(pPage->pDbPage != 0);
	assert(sqlite3PagerGetExtra(pPage->pDbPage) == (void *)pPage);
	assert(sqlite3PagerGetData(pPage->pDbPage) == pPage->aData);
	assert(sqlite3_mutex_held(pPage->pBt->mutex));
	sqlite3PagerUnrefNotNull(pPage->pDbPage);
}

static void
releasePage(MemPage * pPage)
{
	if (pPage)
		releasePageNotNull(pPage);
}

/*
 * Get an unused page.
 *
 * This works just like btreeGetPage() with the addition:
 *
 *   *  If the page is already in use for some other purpose, immediately
 *      release it and return an SQLITE_CURRUPT error.
 *   *  Make sure the isInit flag is clear
 */
static int
btreeGetUnusedPage(BtShared * pBt,	/* The btree */
		   Pgno pgno,	/* Number of the page to fetch */
		   MemPage ** ppPage,	/* Return the page in this parameter */
		   int flags	/* PAGER_GET_NOCONTENT or PAGER_GET_READONLY */
    )
{
	int rc = btreeGetPage(pBt, pgno, ppPage, flags);
	if (rc == SQLITE_OK) {
		if (sqlite3PagerPageRefcount((*ppPage)->pDbPage) > 1) {
			releasePage(*ppPage);
			*ppPage = 0;
			return SQLITE_CORRUPT_BKPT;
		}
		(*ppPage)->isInit = 0;
	} else {
		*ppPage = 0;
	}
	return rc;
}

/*
 * During a rollback, when the pager reloads information into the cache
 * so that the cache is restored to its original state at the start of
 * the transaction, for each page restored this routine is called.
 *
 * This routine needs to reset the extra data section at the end of the
 * page to agree with the restored data.
 */
static void
pageReinit(DbPage * pData)
{
	MemPage *pPage;
	pPage = (MemPage *) sqlite3PagerGetExtra(pData);
	assert(sqlite3PagerPageRefcount(pData) > 0);
	if (pPage->isInit) {
		assert(sqlite3_mutex_held(pPage->pBt->mutex));
		pPage->isInit = 0;
		if (sqlite3PagerPageRefcount(pData) > 1) {
			/* pPage might not be a btree page;  it might be an overflow page
			 * or ptrmap page or a free page.  In those cases, the following
			 * call to btreeInitPage() will likely return SQLITE_CORRUPT.
			 * But no harm is done by this.  And it is very important that
			 * btreeInitPage() be called on every btree page so we make
			 * the call for every page that comes in for re-initing.
			 */
			btreeInitPage(pPage);
		}
	}
}

/*
 * Invoke the busy handler for a btree.
 */
static int
btreeInvokeBusyHandler(void *pArg)
{
	BtShared *pBt = (BtShared *) pArg;
	assert(pBt->db);
	assert(sqlite3_mutex_held(pBt->db->mutex));
	return sqlite3InvokeBusyHandler(&pBt->db->busyHandler);
}

/*
 * Open a database file.
 *
 * zFilename is the name of the database file.  If zFilename is NULL
 * then an ephemeral database is created.  The ephemeral database might
 * be exclusively in memory, or it might use a disk-based memory cache.
 * Either way, the ephemeral database will be automatically deleted
 * when sqlite3BtreeClose() is called.
 *
 * If zFilename is ":memory:" then an in-memory database is created
 * that is automatically destroyed when it is closed.
 *
 * The "flags" parameter is a bitmask that might contain bits like
 * BTREE_OMIT_JOURNAL and/or BTREE_MEMORY.
 *
 * If the database is already opened in the same database connection
 * and we are in shared cache mode, then the open will fail with an
 * SQLITE_CONSTRAINT error.  We cannot allow two or more BtShared
 * objects in the same database connection since doing so will lead
 * to problems with locking.
 */
int
sqlite3BtreeOpen(sqlite3_vfs * pVfs,	/* VFS to use for this b-tree */
		 const char *zFilename,	/* Name of the file containing the BTree database */
		 sqlite3 * db,	/* Associated database handle */
		 Btree ** ppBtree,	/* Pointer to new Btree object written here */
		 int flags,	/* Options */
		 int vfsFlags	/* Flags passed through to sqlite3_vfs.xOpen() */
    )
{
	BtShared *pBt = 0;	/* Shared part of btree structure */
	Btree *p;		/* Handle to return */
	sqlite3_mutex *mutexOpen = 0;	/* Prevents a race condition. Ticket #3537 */
	int rc = SQLITE_OK;	/* Result code from this function */
	u8 nReserve;		/* Byte of unused space on each page */
	unsigned char zDbHeader[100];	/* Database header content */

	/* True if opening an ephemeral, temporary database */
	const int isTempDb = zFilename == 0 || zFilename[0] == 0;

	/* Set the variable isMemdb to true for an in-memory database, or
	 * false for a file-based database.
	 */
#ifdef SQLITE_OMIT_MEMORYDB
	const int isMemdb = 0;
#else
	const int isMemdb = (zFilename && strcmp(zFilename, ":memory:") == 0)
	    || (isTempDb && sqlite3TempInMemory(db))
	    || (vfsFlags & SQLITE_OPEN_MEMORY) != 0;
#endif

	assert(db != 0);
	assert(pVfs != 0);
	assert(sqlite3_mutex_held(db->mutex));
	assert((flags & 0xff) == flags);	/* flags fit in 8 bits */

	/* Only a BTREE_SINGLE database can be BTREE_UNORDERED */
	assert((flags & BTREE_UNORDERED) == 0 || (flags & BTREE_SINGLE) != 0);

	/* A BTREE_SINGLE database is always a temporary and/or ephemeral */
	assert((flags & BTREE_SINGLE) == 0 || isTempDb);

	if (isMemdb) {
		flags |= BTREE_MEMORY;
	}
	if ((vfsFlags & SQLITE_OPEN_MAIN_DB) != 0 && (isMemdb || isTempDb)) {
		vfsFlags =
		    (vfsFlags & ~SQLITE_OPEN_MAIN_DB) | SQLITE_OPEN_TEMP_DB;
	}
	p = sqlite3MallocZero(sizeof(Btree));
	if (!p) {
		return SQLITE_NOMEM_BKPT;
	}
	p->inTrans = TRANS_NONE;
	p->db = db;
#ifndef SQLITE_OMIT_SHARED_CACHE
	p->lock.pBtree = p;
	p->lock.iTable = 1;
#endif

#if !defined(SQLITE_OMIT_SHARED_CACHE) && !defined(SQLITE_OMIT_DISKIO)
	/*
	 * If this Btree is a candidate for shared cache, try to find an
	 * existing BtShared object that we can share with
	 */
#endif
	if (pBt == 0) {
		/*
		 * The following asserts make sure that structures used by the btree are
		 * the right size.  This is to guard against size changes that result
		 * when compiling on a different architecture.
		 */
		assert(sizeof(i64) == 8);
		assert(sizeof(u64) == 8);
		assert(sizeof(u32) == 4);
		assert(sizeof(u16) == 2);
		assert(sizeof(Pgno) == 4);

		pBt = sqlite3MallocZero(sizeof(*pBt));
		if (pBt == 0) {
			rc = SQLITE_NOMEM_BKPT;
			goto btree_open_out;
		}
		rc = sqlite3PagerOpen(pVfs, &pBt->pPager, zFilename,
				      sizeof(MemPage), flags, vfsFlags,
				      pageReinit);
		if (rc == SQLITE_OK) {
			rc = sqlite3PagerReadFileheader(pBt->pPager,
							sizeof(zDbHeader),
							zDbHeader);
		}
		if (rc != SQLITE_OK) {
			goto btree_open_out;
		}
		pBt->openFlags = (u8) flags;
		pBt->db = db;
		p->pBt = pBt;

		pBt->pCursor = 0;
		pBt->pPage1 = 0;
		if (sqlite3PagerIsreadonly(pBt->pPager))
			pBt->btsFlags |= BTS_READ_ONLY;
#ifdef SQLITE_SECURE_DELETE
		pBt->btsFlags |= BTS_SECURE_DELETE;
#endif
		/* EVIDENCE-OF: R-51873-39618 The page size for a database file is
		 * determined by the 2-byte integer located at an offset of 16 bytes from
		 * the beginning of the database file.
		 */
		pBt->pageSize = (zDbHeader[16] << 8) | (zDbHeader[17] << 16);
		if (pBt->pageSize < 512 || pBt->pageSize > SQLITE_MAX_PAGE_SIZE
		    || ((pBt->pageSize - 1) & pBt->pageSize) != 0) {
			pBt->pageSize = 0;
			nReserve = 0;
		} else {
			/* EVIDENCE-OF: R-37497-42412 The size of the reserved region is
			 * determined by the one-byte unsigned integer found at an offset of 20
			 * into the database file header.
			 */
			nReserve = zDbHeader[20];
			pBt->btsFlags |= BTS_PAGESIZE_FIXED;
		}
		rc = sqlite3PagerSetPagesize(pBt->pPager, &pBt->pageSize,
					     nReserve);
		if (rc)
			goto btree_open_out;
		pBt->usableSize = pBt->pageSize - nReserve;
		assert((pBt->pageSize & 7) == 0);	/* 8-byte alignment of pageSize */

#if !defined(SQLITE_OMIT_SHARED_CACHE) && !defined(SQLITE_OMIT_DISKIO)
		/* Add the new BtShared object to the linked list sharable BtShareds.
		 */
		pBt->nRef = 1;
		if (p->sharable) {
			MUTEX_LOGIC(sqlite3_mutex * mutexShared;
			    )
			    MUTEX_LOGIC(mutexShared =
					sqlite3MutexAlloc
					(SQLITE_MUTEX_STATIC_MASTER);
			    )
			    if (SQLITE_THREADSAFE
				&& sqlite3GlobalConfig.bCoreMutex) {
				pBt->mutex =
				    sqlite3MutexAlloc(SQLITE_MUTEX_FAST);
				if (pBt->mutex == 0) {
					rc = SQLITE_NOMEM_BKPT;
					goto btree_open_out;
				}
			}
			sqlite3_mutex_enter(mutexShared);
			pBt->pNext = GLOBAL(BtShared *, sqlite3SharedCacheList);
			GLOBAL(BtShared *, sqlite3SharedCacheList) = pBt;
			sqlite3_mutex_leave(mutexShared);
		}
#endif
	}
#if !defined(SQLITE_OMIT_SHARED_CACHE) && !defined(SQLITE_OMIT_DISKIO)
	/* If the new Btree uses a sharable pBtShared, then link the new
	 * Btree into the list of all sharable Btrees for the same connection.
	 * The list is kept in ascending order by pBt address.
	 */
	if (p->sharable) {
		Btree *pSib;
		if ((pSib = db->mdb.pBt) != 0 && pSib->sharable) {
			while (pSib->pPrev) {
				pSib = pSib->pPrev;
			}
			if ((uptr) p->pBt < (uptr) pSib->pBt) {
				p->pNext = pSib;
				p->pPrev = 0;
				pSib->pPrev = p;
			} else {
				while (pSib->pNext
				       && (uptr) pSib->pNext->pBt <
				       (uptr) p->pBt) {
					pSib = pSib->pNext;
				}
				p->pNext = pSib->pNext;
				p->pPrev = pSib;
				if (p->pNext) {
					p->pNext->pPrev = p;
				}
				pSib->pNext = p;
			}
		}
	}
#endif
	*ppBtree = p;

 btree_open_out:
	if (rc != SQLITE_OK) {
		if (pBt && pBt->pPager) {
			sqlite3PagerClose(pBt->pPager, 0);
		}
		sqlite3_free(pBt);
		sqlite3_free(p);
		*ppBtree = 0;
	} else {
		sqlite3_file *pFile;

		/* If the B-Tree was successfully opened, set the pager-cache size to the
		 * default value. Except, when opening on an existing shared pager-cache,
		 * do not change the pager-cache size.
		 */
		if (sqlite3BtreeSchema(p, 0, 0) == 0) {
			sqlite3PagerSetCachesize(p->pBt->pPager,
						 SQLITE_DEFAULT_CACHE_SIZE);
		}

		pFile = sqlite3PagerFile(pBt->pPager);
		if (pFile->pMethods) {
			sqlite3OsFileControlHint(pFile, SQLITE_FCNTL_PDB,
						 (void *)&pBt->db);
		}
	}
	if (mutexOpen) {
		assert(sqlite3_mutex_held(mutexOpen));
		sqlite3_mutex_leave(mutexOpen);
	}
	assert(rc != SQLITE_OK || sqlite3BtreeConnectionCount(*ppBtree) > 0);
	return rc;
}

/*
 * Decrement the BtShared.nRef counter.  When it reaches zero,
 * remove the BtShared structure from the sharing list.  Return
 * true if the BtShared.nRef counter reaches zero and return
 * false if it is still positive.
 */
static int
removeFromSharingList(BtShared * pBt)
{
#ifndef SQLITE_OMIT_SHARED_CACHE
	MUTEX_LOGIC(sqlite3_mutex * pMaster;
	    )
	BtShared *pList;
	int removed = 0;

	assert(sqlite3_mutex_notheld(pBt->mutex));
	MUTEX_LOGIC(pMaster = sqlite3MutexAlloc(SQLITE_MUTEX_STATIC_MASTER);
	    )
	    sqlite3_mutex_enter(pMaster);
	pBt->nRef--;
	if (pBt->nRef <= 0) {
		if (GLOBAL(BtShared *, sqlite3SharedCacheList) == pBt) {
			GLOBAL(BtShared *, sqlite3SharedCacheList) = pBt->pNext;
		} else {
			pList = GLOBAL(BtShared *, sqlite3SharedCacheList);
			while (ALWAYS(pList) && pList->pNext != pBt) {
				pList = pList->pNext;
			}
			if (ALWAYS(pList)) {
				pList->pNext = pBt->pNext;
			}
		}
		if (SQLITE_THREADSAFE) {
			sqlite3_mutex_free(pBt->mutex);
		}
		removed = 1;
	}
	sqlite3_mutex_leave(pMaster);
	return removed;
#else
	return 1;
#endif
}

/*
 * Make sure pBt->pTmpSpace points to an allocation of
 * MX_CELL_SIZE(pBt) bytes with a 4-byte prefix for a left-child
 * pointer.
 */
static void
allocateTempSpace(BtShared * pBt)
{
	if (!pBt->pTmpSpace) {
		pBt->pTmpSpace = sqlite3PageMalloc(pBt->pageSize);

		/* One of the uses of pBt->pTmpSpace is to format cells before
		 * inserting them into a leaf page (function fillInCell()). If
		 * a cell is less than 4 bytes in size, it is rounded up to 4 bytes
		 * by the various routines that manipulate binary cells. Which
		 * can mean that fillInCell() only initializes the first 2 or 3
		 * bytes of pTmpSpace, but that the first 4 bytes are copied from
		 * it into a database page. This is not actually a problem, but it
		 * does cause a valgrind error when the 1 or 2 bytes of unitialized
		 * data is passed to system call write(). So to avoid this error,
		 * zero the first 4 bytes of temp space here.
		 *
		 * Also:  Provide four bytes of initialized space before the
		 * beginning of pTmpSpace as an area available to prepend the
		 * left-child pointer to the beginning of a cell.
		 */
		if (pBt->pTmpSpace) {
			memset(pBt->pTmpSpace, 0, 8);
			pBt->pTmpSpace += 4;
		}
	}
}

/*
 * Free the pBt->pTmpSpace allocation
 */
static void
freeTempSpace(BtShared * pBt)
{
	if (pBt->pTmpSpace) {
		pBt->pTmpSpace -= 4;
		sqlite3PageFree(pBt->pTmpSpace);
		pBt->pTmpSpace = 0;
	}
}

/*
 * Close an open database and invalidate all cursors.
 */
int
sqlite3BtreeClose(Btree * p)
{
	BtShared *pBt = p->pBt;
	BtCursor *pCur;

	/* Close all cursors opened via this handle.  */
	assert(sqlite3_mutex_held(p->db->mutex));
	sqlite3BtreeEnter(p);
	pCur = pBt->pCursor;
	while (pCur) {
		BtCursor *pTmp = pCur;
		pCur = pCur->pNext;
		if (pTmp->pBtree == p) {
			sqlite3BtreeCloseCursor(pTmp);
		}
	}

	/* Rollback any active transaction and free the handle structure.
	 * The call to sqlite3BtreeRollback() drops any table-locks held by
	 * this handle.
	 */
	sqlite3BtreeRollback(p, SQLITE_OK, 0);
	sqlite3BtreeLeave(p);

	/* If there are still other outstanding references to the shared-btree
	 * structure, return now. The remainder of this procedure cleans
	 * up the shared-btree.
	 */
	assert(p->wantToLock == 0 && p->locked == 0);
	if (!p->sharable || removeFromSharingList(pBt)) {
		/* The pBt is no longer on the sharing list, so we can access
		 * it without having to hold the mutex.
		 *
		 * Clean out and delete the BtShared object.
		 */
		assert(!pBt->pCursor);
		sqlite3PagerClose(pBt->pPager, p->db);
		if (pBt->xFreeSchema && pBt->pSchema) {
			pBt->xFreeSchema(pBt->pSchema);
		}
		sqlite3DbFree(0, pBt->pSchema);
		freeTempSpace(pBt);
		sqlite3_free(pBt);
	}
#ifndef SQLITE_OMIT_SHARED_CACHE
	assert(p->wantToLock == 0);
	assert(p->locked == 0);
	if (p->pPrev)
		p->pPrev->pNext = p->pNext;
	if (p->pNext)
		p->pNext->pPrev = p->pPrev;
#endif

	sqlite3_free(p);
	return SQLITE_OK;
}

/*
 * Change the "soft" limit on the number of pages in the cache.
 * Unused and unmodified pages will be recycled when the number of
 * pages in the cache exceeds this soft limit.  But the size of the
 * cache is allowed to grow larger than this limit if it contains
 * dirty pages or pages still in active use.
 */
int
sqlite3BtreeSetCacheSize(Btree * p, int mxPage)
{
	BtShared *pBt = p->pBt;
	assert(sqlite3_mutex_held(p->db->mutex));
	sqlite3BtreeEnter(p);
	sqlite3PagerSetCachesize(pBt->pPager, mxPage);
	sqlite3BtreeLeave(p);
	return SQLITE_OK;
}

/*
 * Change the default pages size and the number of reserved bytes per page.
 * Or, if the page size has already been fixed, return SQLITE_READONLY
 * without changing anything.
 *
 * The page size must be a power of 2 between 512 and 65536.  If the page
 * size supplied does not meet this constraint then the page size is not
 * changed.
 *
 * Page sizes are constrained to be a power of two so that the region
 * of the database file used for locking (beginning at PENDING_BYTE,
 * the first byte past the 1GB boundary, 0x40000000) needs to occur
 * at the beginning of a page.
 *
 * If parameter nReserve is less than zero, then the number of reserved
 * bytes per page is left unchanged.
 */
int
sqlite3BtreeSetPageSize(Btree * p, int pageSize, int nReserve)
{
	int rc = SQLITE_OK;
	BtShared *pBt = p->pBt;
	assert(nReserve >= -1 && nReserve <= 255);
	sqlite3BtreeEnter(p);
#if SQLITE_HAS_CODEC
	if (nReserve > pBt->optimalReserve)
		pBt->optimalReserve = (u8) nReserve;
#endif
	if (pBt->btsFlags & BTS_PAGESIZE_FIXED) {
		sqlite3BtreeLeave(p);
		return SQLITE_READONLY;
	}
	if (nReserve < 0) {
		nReserve = pBt->pageSize - pBt->usableSize;
	}
	assert(nReserve >= 0 && nReserve <= 255);
	if (pageSize >= 512 && pageSize <= SQLITE_MAX_PAGE_SIZE &&
	    ((pageSize - 1) & pageSize) == 0) {
		assert((pageSize & 7) == 0);
		assert(!pBt->pCursor);
		pBt->pageSize = (u32) pageSize;
		freeTempSpace(pBt);
	}
	rc = sqlite3PagerSetPagesize(pBt->pPager, &pBt->pageSize, nReserve);
	pBt->usableSize = pBt->pageSize - (u16) nReserve;
	sqlite3BtreeLeave(p);
	return rc;
}

/*
 * Return the currently defined page size
 */
int
sqlite3BtreeGetPageSize(Btree * p)
{
	return p->pBt->pageSize;
}

/*
 * This function is similar to sqlite3BtreeGetReserve(), except that it
 * may only be called if it is guaranteed that the b-tree mutex is already
 * held.
 */
int
sqlite3BtreeGetReserveNoMutex(Btree * p)
{
	int n;
	assert(sqlite3_mutex_held(p->pBt->mutex));
	n = p->pBt->pageSize - p->pBt->usableSize;
	return n;
}

/*
 * Return the number of bytes of space at the end of every page that
 * are intentually left unused.  This is the "reserved" space that is
 * sometimes used by extensions.
 *
 * If SQLITE_HAS_MUTEX is defined then the number returned is the
 * greater of the current reserved space and the maximum requested
 * reserve space.
 */
int
sqlite3BtreeGetOptimalReserve(Btree * p)
{
	int n;
	sqlite3BtreeEnter(p);
	n = sqlite3BtreeGetReserveNoMutex(p);
#ifdef SQLITE_HAS_CODEC
	if (n < p->pBt->optimalReserve)
		n = p->pBt->optimalReserve;
#endif
	sqlite3BtreeLeave(p);
	return n;
}

/*
 * Set the BTS_SECURE_DELETE flag if newFlag is 0 or 1.  If newFlag is -1,
 * then make no changes.  Always return the value of the BTS_SECURE_DELETE
 * setting after the change.
 */
int
sqlite3BtreeSecureDelete(Btree * p, int newFlag)
{
	int b;
	if (p == 0)
		return 0;
	sqlite3BtreeEnter(p);
	if (newFlag >= 0) {
		p->pBt->btsFlags &= ~BTS_SECURE_DELETE;
		if (newFlag)
			p->pBt->btsFlags |= BTS_SECURE_DELETE;
	}
	b = (p->pBt->btsFlags & BTS_SECURE_DELETE) != 0;
	sqlite3BtreeLeave(p);
	return b;
}

/*
 * Get a reference to pPage1 of the database file.  This will
 * also acquire a readlock on that file.
 *
 * SQLITE_OK is returned on success.  If the file is not a
 * well-formed database file, then SQLITE_CORRUPT is returned.
 * SQLITE_BUSY is returned if the database is locked.  SQLITE_NOMEM
 * is returned if we run out of memory.
 */
static int
lockBtree(BtShared * pBt)
{
	int rc;			/* Result code from subfunctions */
	MemPage *pPage1;	/* Page 1 of the database file */
	int nPage;		/* Number of pages in the database */
	int nPageFile = 0;	/* Number of pages in the database file */

	assert(sqlite3_mutex_held(pBt->mutex));
	assert(pBt->pPage1 == 0);
	rc = sqlite3PagerSharedLock(pBt->pPager);
	if (rc != SQLITE_OK)
		return rc;
	rc = btreeGetPage(pBt, 1, &pPage1, 0);
	if (rc != SQLITE_OK)
		return rc;

	/* Do some checking to help insure the file we opened really is
	 * a valid database file.
	 */
	nPage = get4byte(28 + (u8 *) pPage1->aData);
	sqlite3PagerPagecount(pBt->pPager, &nPageFile);
	if (nPage == 0
	    || memcmp(24 + (u8 *) pPage1->aData, 92 + (u8 *) pPage1->aData,
		      4) != 0) {
		nPage = nPageFile;
	}
	if (nPage > 0) {
		u32 pageSize;
		u32 usableSize;
		u8 *page1 = pPage1->aData;
		rc = SQLITE_NOTADB;
		/* EVIDENCE-OF: R-43737-39999 Every valid SQLite database file begins
		 * with the following 16 bytes (in hex): 53 51 4c 69 74 65 20 66 6f 72 6d
		 * 61 74 20 33 00.
		 */
		if (memcmp(page1, zMagicHeader, 16) != 0) {
			goto page1_init_failed;
		}

		if (page1[18] > 1) {
			pBt->btsFlags |= BTS_READ_ONLY;
		}
		if (page1[19] > 1) {
			goto page1_init_failed;
		}

		/* EVIDENCE-OF: R-15465-20813 The maximum and minimum embedded payload
		 * fractions and the leaf payload fraction values must be 64, 32, and 32.
		 *
		 * The original design allowed these amounts to vary, but as of
		 * version 3.6.0, we require them to be fixed.
		 */
		if (memcmp(&page1[21], "\100\040\040", 3) != 0) {
			goto page1_init_failed;
		}
		/* EVIDENCE-OF: R-51873-39618 The page size for a database file is
		 * determined by the 2-byte integer located at an offset of 16 bytes from
		 * the beginning of the database file.
		 */
		pageSize = (page1[16] << 8) | (page1[17] << 16);
		/* EVIDENCE-OF: R-25008-21688 The size of a page is a power of two
		 * between 512 and 65536 inclusive.
		 */
		if (((pageSize - 1) & pageSize) != 0
		    || pageSize > SQLITE_MAX_PAGE_SIZE || pageSize <= 256) {
			goto page1_init_failed;
		}
		assert((pageSize & 7) == 0);
		/* EVIDENCE-OF: R-59310-51205 The "reserved space" size in the 1-byte
		 * integer at offset 20 is the number of bytes of space at the end of
		 * each page to reserve for extensions.
		 *
		 * EVIDENCE-OF: R-37497-42412 The size of the reserved region is
		 * determined by the one-byte unsigned integer found at an offset of 20
		 * into the database file header.
		 */
		usableSize = pageSize - page1[20];
		if ((u32) pageSize != pBt->pageSize) {
			/* After reading the first page of the database assuming a page size
			 * of BtShared.pageSize, we have discovered that the page-size is
			 * actually pageSize. Unlock the database, leave pBt->pPage1 at
			 * zero and return SQLITE_OK. The caller will call this function
			 * again with the correct page-size.
			 */
			releasePage(pPage1);
			pBt->usableSize = usableSize;
			pBt->pageSize = pageSize;
			freeTempSpace(pBt);
			rc = sqlite3PagerSetPagesize(pBt->pPager,
						     &pBt->pageSize,
						     pageSize - usableSize);
			return rc;
		}
		if (nPage > nPageFile) {
			rc = SQLITE_CORRUPT_BKPT;
			goto page1_init_failed;
		}
		/* EVIDENCE-OF: R-28312-64704 However, the usable size is not allowed to
		 * be less than 480. In other words, if the page size is 512, then the
		 * reserved space size cannot exceed 32.
		 */
		if (usableSize < 480) {
			goto page1_init_failed;
		}
		pBt->pageSize = pageSize;
		pBt->usableSize = usableSize;
	}

	/* maxLocal is the maximum amount of payload to store locally for
	 * a cell.  Make sure it is small enough so that at least minFanout
	 * cells can will fit on one page.  We assume a 10-byte page header.
	 * Besides the payload, the cell must store:
	 *     2-byte pointer to the cell
	 *     4-byte child pointer
	 *     9-byte nKey value
	 *     4-byte nData value
	 *     4-byte overflow page pointer
	 * So a cell consists of a 2-byte pointer, a header which is as much as
	 * 17 bytes long, 0 to N bytes of payload, and an optional 4 byte overflow
	 * page pointer.
	 */
	pBt->maxLocal = (u16) ((pBt->usableSize - 12) * 64 / 255 - 23);
	pBt->minLocal = (u16) ((pBt->usableSize - 12) * 32 / 255 - 23);
	pBt->maxLeaf = (u16) (pBt->usableSize - 35);
	pBt->minLeaf = (u16) ((pBt->usableSize - 12) * 32 / 255 - 23);
	if (pBt->maxLocal > 127) {
		pBt->max1bytePayload = 127;
	} else {
		pBt->max1bytePayload = (u8) pBt->maxLocal;
	}
	assert(pBt->maxLeaf + 23 <= MX_CELL_SIZE(pBt));
	pBt->pPage1 = pPage1;
	pBt->nPage = nPage;
	return SQLITE_OK;

 page1_init_failed:
	releasePage(pPage1);
	pBt->pPage1 = 0;
	return rc;
}

#ifndef NDEBUG
/*
 * Return the number of cursors open on pBt. This is for use
 * in assert() expressions, so it is only compiled if NDEBUG is not
 * defined.
 *
 * Only write cursors are counted if wrOnly is true.  If wrOnly is
 * false then all cursors are counted.
 *
 * For the purposes of this routine, a cursor is any cursor that
 * is capable of reading or writing to the database.  Cursors that
 * have been tripped into the CURSOR_FAULT state are not counted.
 */
static int
countValidCursors(BtShared * pBt, int wrOnly)
{
	BtCursor *pCur;
	int r = 0;
	for (pCur = pBt->pCursor; pCur; pCur = pCur->pNext) {
		if ((wrOnly == 0 || (pCur->curFlags & BTCF_WriteFlag) != 0)
		    && pCur->eState != CURSOR_FAULT)
			r++;
	}
	return r;
}
#endif

/*
 * If there are no outstanding cursors and we are not in the middle
 * of a transaction but there is a read lock on the database, then
 * this routine unrefs the first page of the database file which
 * has the effect of releasing the read lock.
 *
 * If there is a transaction in progress, this routine is a no-op.
 */
static void
unlockBtreeIfUnused(BtShared * pBt)
{
	assert(sqlite3_mutex_held(pBt->mutex));
	assert(countValidCursors(pBt, 0) == 0
	       || pBt->inTransaction > TRANS_NONE);
	if (pBt->inTransaction == TRANS_NONE && pBt->pPage1 != 0) {
		MemPage *pPage1 = pBt->pPage1;
		assert(pPage1->aData);
		assert(sqlite3PagerRefcount(pBt->pPager) == 1);
		pBt->pPage1 = 0;
		releasePageNotNull(pPage1);
	}
}

/*
 * If pBt points to an empty file then convert that empty file
 * into a new empty database by initializing the first page of
 * the database.
 */
static int
newDatabase(BtShared * pBt)
{
	MemPage *pP1;
	unsigned char *data;
	int rc;

	assert(sqlite3_mutex_held(pBt->mutex));
	if (pBt->nPage > 0) {
		return SQLITE_OK;
	}
	pP1 = pBt->pPage1;
	assert(pP1 != 0);
	data = pP1->aData;
	rc = sqlite3PagerWrite(pP1->pDbPage);
	if (rc)
		return rc;
	memcpy(data, zMagicHeader, sizeof(zMagicHeader));
	assert(sizeof(zMagicHeader) == 16);
	data[16] = (u8) ((pBt->pageSize >> 8) & 0xff);
	data[17] = (u8) ((pBt->pageSize >> 16) & 0xff);
	data[18] = 1;
	data[19] = 1;
	assert(pBt->usableSize <= pBt->pageSize
	       && pBt->usableSize + 255 >= pBt->pageSize);
	data[20] = (u8) (pBt->pageSize - pBt->usableSize);
	data[21] = 64;
	data[22] = 32;
	data[23] = 32;
	memset(&data[24], 0, 100 - 24);
	zeroPage(pP1, PTF_INTKEY | PTF_LEAF | PTF_LEAFDATA);
	pBt->btsFlags |= BTS_PAGESIZE_FIXED;
	pBt->nPage = 1;
	data[31] = 1;
	return SQLITE_OK;
}

/*
 * Initialize the first page of the database file (creating a database
 * consisting of a single page and no schema objects). Return SQLITE_OK
 * if successful, or an SQLite error code otherwise.
 */
int
sqlite3BtreeNewDb(Btree * p)
{
	int rc;
	sqlite3BtreeEnter(p);
	p->pBt->nPage = 0;
	rc = newDatabase(p->pBt);
	sqlite3BtreeLeave(p);
	return rc;
}

/*
 * Attempt to start a new transaction. A write-transaction
 * is started if the second argument is nonzero, otherwise a read-
 * transaction.  If the second argument is 2 or more and exclusive
 * transaction is started, meaning that no other process is allowed
 * to access the database.  A preexisting transaction may not be
 * upgraded to exclusive by calling this routine a second time - the
 * exclusivity flag only works for a new transaction.
 *
 * A write-transaction must be started before attempting any
 * changes to the database.  None of the following routines
 * will work unless a transaction is started first:
 *
 *      sqlite3BtreeCreateTable()
 *      sqlite3BtreeCreateIndex()
 *      sqlite3BtreeClearTable()
 *      sqlite3BtreeDropTable()
 *      sqlite3BtreeInsert()
 *      sqlite3BtreeDelete()
 *      sqlite3BtreeUpdateMeta()
 *
 * If an initial attempt to acquire the lock fails because of lock contention
 * and the database was previously unlocked, then invoke the busy handler
 * if there is one.  But if there was previously a read-lock, do not
 * invoke the busy handler - just return SQLITE_BUSY.  SQLITE_BUSY is
 * returned when there is already a read-lock in order to avoid a deadlock.
 *
 * Suppose there are two processes A and B.  A has a read lock and B has
 * a reserved lock.  B tries to promote to exclusive but is blocked because
 * of A's read lock.  A tries to promote to reserved but is blocked by B.
 * One or the other of the two processes must give way or there can be
 * no progress.  By returning SQLITE_BUSY and not invoking the busy callback
 * when A already has a read lock, we encourage A to give up and let B
 * proceed.
 */
int
sqlite3BtreeBeginTrans(Btree * p, int nSavepoint, int wrflag)
{
	BtShared *pBt = p->pBt;
	int rc = SQLITE_OK;
	(void)nSavepoint;
	sqlite3BtreeEnter(p);
	btreeIntegrity(p);

	/* If the btree is already in a write-transaction, or it
	 * is already in a read-transaction and a read-transaction
	 * is requested, this is a no-op.
	 */
	if (p->inTrans == TRANS_WRITE || (p->inTrans == TRANS_READ && !wrflag)) {
		goto trans_begun;
	}

	/* Write transactions are not possible on a read-only database */
	if ((pBt->btsFlags & BTS_READ_ONLY) != 0 && wrflag) {
		rc = SQLITE_READONLY;
		goto trans_begun;
	}
#ifndef SQLITE_OMIT_SHARED_CACHE
	{
		sqlite3 *pBlock = 0;
		/* If another database handle has already opened a write transaction
		 * on this shared-btree structure and a second write transaction is
		 * requested, return SQLITE_LOCKED.
		 */
		if ((wrflag && pBt->inTransaction == TRANS_WRITE)
		    || (pBt->btsFlags & BTS_PENDING) != 0) {
			pBlock = pBt->pWriter->db;
		} else if (wrflag > 1) {
			BtLock *pIter;
			for (pIter = pBt->pLock; pIter; pIter = pIter->pNext) {
				if (pIter->pBtree != p) {
					pBlock = pIter->pBtree->db;
					break;
				}
			}
		}
		if (pBlock) {
			sqlite3ConnectionBlocked(p->db, pBlock);
			rc = SQLITE_LOCKED_SHAREDCACHE;
			goto trans_begun;
		}
	}
#endif

	/* Any read-only or read-write transaction implies a read-lock on
	 * page 1. So if some other shared-cache client already has a write-lock
	 * on page 1, the transaction cannot be opened.
	 */
	rc = querySharedCacheTableLock(p, MASTER_ROOT, READ_LOCK);
	if (SQLITE_OK != rc)
		goto trans_begun;

	pBt->btsFlags &= ~BTS_INITIALLY_EMPTY;
	if (pBt->nPage == 0)
		pBt->btsFlags |= BTS_INITIALLY_EMPTY;
	do {
		/* Call lockBtree() until either pBt->pPage1 is populated or
		 * lockBtree() returns something other than SQLITE_OK. lockBtree()
		 * may return SQLITE_OK but leave pBt->pPage1 set to 0 if after
		 * reading page 1 it discovers that the page-size of the database
		 * file is not pBt->pageSize. In this case lockBtree() will update
		 * pBt->pageSize to the page-size of the file on disk.
		 */
		while (pBt->pPage1 == 0 && SQLITE_OK == (rc = lockBtree(pBt))) ;

		if (rc == SQLITE_OK && wrflag) {
			if ((pBt->btsFlags & BTS_READ_ONLY) != 0) {
				rc = SQLITE_READONLY;
			} else {
				// we have no transactions on ephem tables (other tables work over Tarantool)
				//rc = sqlite3PagerBegin(pBt->pPager,wrflag>1,sqlite3TempInMemory(p->db));
				//if( rc==SQLITE_OK ){
				rc = newDatabase(pBt);
				//}
			}
		}

		if (rc != SQLITE_OK) {
			unlockBtreeIfUnused(pBt);
		}
	} while ((rc & 0xFF) == SQLITE_BUSY && pBt->inTransaction == TRANS_NONE
		 && btreeInvokeBusyHandler(pBt));

	if (rc == SQLITE_OK) {
		if (p->inTrans == TRANS_NONE) {
			pBt->nTransaction++;
#ifndef SQLITE_OMIT_SHARED_CACHE
			if (p->sharable) {
				assert(p->lock.pBtree == p
				       && p->lock.iTable == 1);
				p->lock.eLock = READ_LOCK;
				p->lock.pNext = pBt->pLock;
				pBt->pLock = &p->lock;
			}
#endif
		}
		p->inTrans = (wrflag ? TRANS_WRITE : TRANS_READ);
		if (p->inTrans > pBt->inTransaction) {
			pBt->inTransaction = p->inTrans;
		}
		if (wrflag) {
			MemPage *pPage1 = pBt->pPage1;
#ifndef SQLITE_OMIT_SHARED_CACHE
			assert(!pBt->pWriter);
			pBt->pWriter = p;
			pBt->btsFlags &= ~BTS_EXCLUSIVE;
			if (wrflag > 1)
				pBt->btsFlags |= BTS_EXCLUSIVE;
#endif

			/* If the db-size header field is incorrect (as it may be if an old
			 * client has been writing the database file), update it now. Doing
			 * this sooner rather than later means the database size can safely
			 * re-read the database size from page 1 if a savepoint or transaction
			 * rollback occurs within the transaction.
			 */
			if (pBt->nPage != get4byte(&pPage1->aData[28])) {
				rc = sqlite3PagerWrite(pPage1->pDbPage);
				if (rc == SQLITE_OK) {
					put4byte(&pPage1->aData[28],
						 pBt->nPage);
				}
			}
		}
	}

 trans_begun:
	if (rc == SQLITE_OK && wrflag) {
		/* This call makes sure that the pager has the correct number of
		 * open savepoints. If the second parameter is greater than 0 and
		 * the sub-journal is not already open, then it will be opened here.
		 */
		/* disabled as btree used only for ephemeral tables
		   rc = sqlite3PagerOpenSavepoint(pBt->pPager, nSavepoint);
		 */
	}

	btreeIntegrity(p);
	sqlite3BtreeLeave(p);
	return rc;
}

/*
 * This routine does the first phase of a two-phase commit.  This routine
 * causes a rollback journal to be created (if it does not already exist)
 * and populated with enough information so that if a power loss occurs
 * the database can be restored to its original state by playing back
 * the journal.  Then the contents of the journal are flushed out to
 * the disk.  After the journal is safely on oxide, the changes to the
 * database are written into the database file and flushed to oxide.
 * At the end of this call, the rollback journal still exists on the
 * disk and we are still holding all locks, so the transaction has not
 * committed.  See sqlite3BtreeCommitPhaseTwo() for the second phase of the
 * commit process.
 *
 * This call is a no-op if no write-transaction is currently active on pBt.
 *
 * Otherwise, sync the database file for the btree pBt. zMaster points to
 * the name of a master journal file that should be written into the
 * individual journal file, or is NULL, indicating no master journal file
 * (single database transaction).
 *
 * When this is called, the master journal should already have been
 * created, populated with this journal pointer and synced to disk.
 *
 * Once this is routine has returned, the only thing required to commit
 * the write-transaction for this database file is to delete the journal.
 */
int
sqlite3BtreeCommitPhaseOne(Btree * p)
{
	int rc = SQLITE_OK;
	if (p->inTrans == TRANS_WRITE) {
		BtShared *pBt = p->pBt;
		sqlite3BtreeEnter(p);
		rc = sqlite3PagerCommitPhaseOne(pBt->pPager);
		sqlite3BtreeLeave(p);
	}
	return rc;
}

/*
 * This function is called from both BtreeCommitPhaseTwo() and BtreeRollback()
 * at the conclusion of a transaction.
 */
static void
btreeEndTransaction(Btree * p)
{
	BtShared *pBt = p->pBt;
	sqlite3 *db = p->db;
	assert(sqlite3BtreeHoldsMutex(p));

	if (p->inTrans > TRANS_NONE && db->nVdbeRead > 1) {
		/* If there are other active statements that belong to this database
		 * handle, downgrade to a read-only transaction. The other statements
		 * may still be reading from the database.
		 */
		downgradeAllSharedCacheTableLocks(p);
		p->inTrans = TRANS_READ;
	} else {
		/* If the handle had any kind of transaction open, decrement the
		 * transaction count of the shared btree. If the transaction count
		 * reaches 0, set the shared state to TRANS_NONE. The unlockBtreeIfUnused()
		 * call below will unlock the pager.
		 */
		if (p->inTrans != TRANS_NONE) {
			clearAllSharedCacheTableLocks(p);
			pBt->nTransaction--;
			if (0 == pBt->nTransaction) {
				pBt->inTransaction = TRANS_NONE;
			}
		}

		/* Set the current transaction state to TRANS_NONE and unlock the
		 * pager if this call closed the only read or write transaction.
		 */
		p->inTrans = TRANS_NONE;
		unlockBtreeIfUnused(pBt);
	}

	btreeIntegrity(p);
}

/*
 * Commit the transaction currently in progress.
 *
 * This routine implements the second phase of a 2-phase commit.  The
 * sqlite3BtreeCommitPhaseOne() routine does the first phase and should
 * be invoked prior to calling this routine.  The sqlite3BtreeCommitPhaseOne()
 * routine did all the work of writing information out to disk and flushing the
 * contents so that they are written onto the disk platter.  All this
 * routine has to do is delete or truncate or zero the header in the
 * the rollback journal (which causes the transaction to commit) and
 * drop locks.
 *
 * Normally, if an error occurs while the pager layer is attempting to
 * finalize the underlying journal file, this function returns an error and
 * the upper layer will attempt a rollback. However, if the second argument
 * is non-zero then this b-tree transaction is part of a multi-file
 * transaction. In this case, the transaction has already been committed
 * (by deleting a master journal file) and the caller will ignore this
 * functions return code. So, even if an error occurs in the pager layer,
 * reset the b-tree objects internal state to indicate that the write
 * transaction has been closed. This is quite safe, as the pager will have
 * transitioned to the error state.
 *
 * This will release the write lock on the database file.  If there
 * are no active cursors, it also releases the read lock.
 */
int
sqlite3BtreeCommitPhaseTwo(Btree * p, int bCleanup)
{

	if (p->inTrans == TRANS_NONE)
		return SQLITE_OK;
	sqlite3BtreeEnter(p);
	btreeIntegrity(p);

	/* If the handle has a write-transaction open, commit the shared-btrees
	 * transaction and set the shared state to TRANS_READ.
	 */
	if (p->inTrans == TRANS_WRITE) {
		int rc;
		BtShared *pBt = p->pBt;
		assert(pBt->inTransaction == TRANS_WRITE);
		assert(pBt->nTransaction > 0);
		/* disabled as pager used only by ephemeral tables
		   rc = sqlite3PagerCommitPhaseTwo(pBt->pPager);
		 */
		rc = SQLITE_OK;
		if (rc != SQLITE_OK && bCleanup == 0) {
			sqlite3BtreeLeave(p);
			return rc;
		}
		p->iDataVersion--;	/* Compensate for pPager->iDataVersion++; */
		pBt->inTransaction = TRANS_READ;
		btreeClearHasContent(pBt);
	}

	btreeEndTransaction(p);
	sqlite3BtreeLeave(p);
	return SQLITE_OK;
}

/*
 * Do both phases of a commit.
 */
int
sqlite3BtreeCommit(Btree * p)
{
	int rc;
	sqlite3BtreeEnter(p);
	rc = sqlite3BtreeCommitPhaseOne(p);
	if (rc == SQLITE_OK) {
		rc = sqlite3BtreeCommitPhaseTwo(p, 0);
	}
	sqlite3BtreeLeave(p);
	return rc;
}

/*
 * This routine sets the state to CURSOR_FAULT and the error
 * code to errCode for every cursor on any BtShared that pBtree
 * references.  Or if the writeOnly flag is set to 1, then only
 * trip write cursors and leave read cursors unchanged.
 *
 * Every cursor is a candidate to be tripped, including cursors
 * that belong to other database connections that happen to be
 * sharing the cache with pBtree.
 *
 * This routine gets called when a rollback occurs. If the writeOnly
 * flag is true, then only write-cursors need be tripped - read-only
 * cursors save their current positions so that they may continue
 * following the rollback. Or, if writeOnly is false, all cursors are
 * tripped. In general, writeOnly is false if the transaction being
 * rolled back modified the database schema. In this case b-tree root
 * pages may be moved or deleted from the database altogether, making
 * it unsafe for read cursors to continue.
 *
 * If the writeOnly flag is true and an error is encountered while
 * saving the current position of a read-only cursor, all cursors,
 * including all read-cursors are tripped.
 *
 * SQLITE_OK is returned if successful, or if an error occurs while
 * saving a cursor position, an SQLite error code.
 */
int
sqlite3BtreeTripAllCursors(Btree * pBtree, int errCode, int writeOnly)
{
	BtCursor *p;
	int rc = SQLITE_OK;

	assert((writeOnly == 0 || writeOnly == 1) && BTCF_WriteFlag == 1);
	if (pBtree) {
		sqlite3BtreeEnter(pBtree);
		for (p = pBtree->pBt->pCursor; p; p = p->pNext) {
			int i;
			if (writeOnly && (p->curFlags & BTCF_WriteFlag) == 0) {
				if (p->eState == CURSOR_VALID
				    || p->eState == CURSOR_SKIPNEXT) {
					rc = saveCursorPosition(p);
					if (rc != SQLITE_OK) {
						(void)
						    sqlite3BtreeTripAllCursors
						    (pBtree, rc, 0);
						break;
					}
				}
			} else {
				sqlite3BtreeClearCursor(p);
				p->eState = CURSOR_FAULT;
				p->skipNext = errCode;
			}
			for (i = 0; i <= p->iPage; i++) {
				releasePage(p->apPage[i]);
				p->apPage[i] = 0;
			}
		}
		sqlite3BtreeLeave(pBtree);
	}
	return rc;
}

/*
 * Rollback the transaction in progress.
 *
 * If tripCode is not SQLITE_OK then cursors will be invalidated (tripped).
 * Only write cursors are tripped if writeOnly is true but all cursors are
 * tripped if writeOnly is false.  Any attempt to use
 * a tripped cursor will result in an error.
 *
 * This will release the write lock on the database file.  If there
 * are no active cursors, it also releases the read lock.
 */
int
sqlite3BtreeRollback(Btree * p, int tripCode, int writeOnly)
{
	int rc;
	BtShared *pBt = p->pBt;
	MemPage *pPage1;

	assert(writeOnly == 1 || writeOnly == 0);
	assert(tripCode == SQLITE_ABORT_ROLLBACK || tripCode == SQLITE_OK);
	sqlite3BtreeEnter(p);
	if (tripCode == SQLITE_OK) {
		rc = tripCode = saveAllCursors(pBt, 0, 0);
		if (rc)
			writeOnly = 0;
	} else {
		rc = SQLITE_OK;
	}
	if (tripCode) {
		int rc2 = sqlite3BtreeTripAllCursors(p, tripCode, writeOnly);
		assert(rc == SQLITE_OK || (writeOnly == 0 && rc2 == SQLITE_OK));
		if (rc2 != SQLITE_OK)
			rc = rc2;
	}
	btreeIntegrity(p);

	if (p->inTrans == TRANS_WRITE) {

		assert(TRANS_WRITE == pBt->inTransaction);

		/* The rollback may have destroyed the pPage1->aData value.  So
		 * call btreeGetPage() on page 1 again to make
		 * sure pPage1->aData is set correctly.
		 */
		if (btreeGetPage(pBt, 1, &pPage1, 0) == SQLITE_OK) {
			int nPage = get4byte(28 + (u8 *) pPage1->aData);
			testcase(nPage == 0);
			if (nPage == 0)
				sqlite3PagerPagecount(pBt->pPager, &nPage);
			testcase(pBt->nPage != nPage);
			pBt->nPage = nPage;
			releasePage(pPage1);
		}
		/* This asserts are disabled as we are not sure if they are necessary or not
		   assert( countValidCursors(pBt, 1)==0 );
		 */
		pBt->inTransaction = TRANS_READ;
		btreeClearHasContent(pBt);
	}

	btreeEndTransaction(p);
	sqlite3BtreeLeave(p);
	return rc;
}

/*
 * Start a statement subtransaction. The subtransaction can be rolled
 * back independently of the main transaction. You must start a transaction
 * before starting a subtransaction. The subtransaction is ended automatically
 * if the main transaction commits or rolls back.
 *
 * Statement subtransactions are used around individual SQL statements
 * that are contained within a BEGIN...COMMIT block.  If a constraint
 * error occurs within the statement, the effect of that one statement
 * can be rolled back without having to rollback the entire transaction.
 *
 * A statement sub-transaction is implemented as an anonymous savepoint. The
 * value passed as the second parameter is the total number of savepoints,
 * including the new anonymous savepoint, open on the B-Tree. i.e. if there
 * are no active savepoints and no other statement-transactions open,
 * iStatement is 1. This anonymous savepoint can be released or rolled back
 * using the sqlite3BtreeSavepoint() function.
 */
int
sqlite3BtreeBeginStmt(Btree * p, int iStatement, int nSavepoint)
{
	int rc = SQLITE_OK;
	BtShared *pBt = p->pBt;
	sqlite3BtreeEnter(p);
	assert(p->inTrans == TRANS_WRITE);
	assert((pBt->btsFlags & BTS_READ_ONLY) == 0);
	assert(iStatement > 0);
	assert(iStatement > nSavepoint);
	assert(pBt->inTransaction == TRANS_WRITE);
	/* At the pager level, a statement transaction is a savepoint with
	 * an index greater than all savepoints created explicitly using
	 * SQL statements. It is illegal to open, release or rollback any
	 * such savepoints while the statement transaction savepoint is active.
	 */
	/* disabled as pager used only by epemeral tables
	 * rc = sqlite3PagerOpenSavepoint(pBt->pPager, iStatement);
	 */
	sqlite3BtreeLeave(p);
	return rc;
}

/*
 * The second argument to this function, op, is always SAVEPOINT_ROLLBACK
 * or SAVEPOINT_RELEASE. This function either releases or rolls back the
 * savepoint identified by parameter iSavepoint, depending on the value
 * of op.
 *
 * Normally, iSavepoint is greater than or equal to zero. However, if op is
 * SAVEPOINT_ROLLBACK, then iSavepoint may also be -1. In this case the
 * contents of the entire transaction are rolled back. This is different
 * from a normal transaction rollback, as no locks are released and the
 * transaction remains open.
 */
int
sqlite3BtreeSavepoint(Btree * p, int op, int iSavepoint)
{
	int rc = SQLITE_OK;
	if (p && p->inTrans == TRANS_WRITE) {
		BtShared *pBt = p->pBt;
		assert(op == SAVEPOINT_RELEASE || op == SAVEPOINT_ROLLBACK);
		assert(iSavepoint >= 0
		       || (iSavepoint == -1 && op == SAVEPOINT_ROLLBACK));
		sqlite3BtreeEnter(p);
		rc = sqlite3PagerSavepoint(pBt->pPager, op, iSavepoint);
		if (rc == SQLITE_OK) {
			if (iSavepoint < 0
			    && (pBt->btsFlags & BTS_INITIALLY_EMPTY) != 0) {
				pBt->nPage = 0;
			}
			rc = newDatabase(pBt);
			pBt->nPage = get4byte(28 + pBt->pPage1->aData);

			/* The database size was written into the offset 28 of the header
			 * when the transaction started, so we know that the value at offset
			 * 28 is nonzero.
			 */
			assert(pBt->nPage > 0);
		}
		sqlite3BtreeLeave(p);
	}
	return rc;
}

/*
 * Create a new cursor for the BTree whose root is on the page
 * iTable. If a read-only cursor is requested, it is assumed that
 * the caller already has at least a read-only transaction open
 * on the database already. If a write-cursor is requested, then
 * the caller is assumed to have an open write transaction.
 *
 * If the BTREE_WRCSR bit of wrFlag is clear, then the cursor can only
 * be used for reading.  If the BTREE_WRCSR bit is set, then the cursor
 * can be used for reading or for writing if other conditions for writing
 * are also met.  These are the conditions that must be met in order
 * for writing to be allowed:
 *
 * 1:  The cursor must have been opened with wrFlag containing BTREE_WRCSR
 *
 * 2:  Other database connections that share the same pager cache
 *     but which are not in the READ_UNCOMMITTED state may not have
 *     cursors open with wrFlag==0 on the same table.  Otherwise
 *     the changes made by this write cursor would be visible to
 *     the read cursors in the other database connection.
 *
 * 3:  The database must be writable (not on read-only media)
 *
 * 4:  There must be an active transaction.
 *
 * The BTREE_FORDELETE bit of wrFlag may optionally be set if BTREE_WRCSR
 * is set.  If FORDELETE is set, that is a hint to the implementation that
 * this cursor will only be used to seek to and delete entries of an index
 * as part of a larger DELETE statement.  The FORDELETE hint is not used by
 * this implementation.  But in a hypothetical alternative storage engine
 * in which index entries are automatically deleted when corresponding table
 * rows are deleted, the FORDELETE flag is a hint that all SEEK and DELETE
 * operations on this cursor can be no-ops and all READ operations can
 * return a null row (2-bytes: 0x01 0x00).
 *
 * No checking is done to make sure that page iTable really is the
 * root page of a b-tree.  If it is not, then the cursor acquired
 * will not work correctly.
 *
 * It is assumed that the sqlite3BtreeCursorZero() has been called
 * on pCur to initialize the memory space prior to invoking this routine.
 */
static int
btreeCursor(Btree * p,		/* The btree */
	    int iTable,		/* Root page of table to open */
	    int wrFlag,		/* 1 to write. 0 read-only */
	    struct KeyInfo *pKeyInfo,	/* First arg to comparison function */
	    BtCursor * pCur	/* Space for new cursor */
    )
{
	BtShared *pBt = p->pBt;	/* Shared b-tree handle */
	BtCursor *pX;		/* Looping over other all cursors */

	assert(sqlite3BtreeHoldsMutex(p));
	assert(wrFlag == 0
	       || wrFlag == BTREE_WRCSR
	       || wrFlag == (BTREE_WRCSR | BTREE_FORDELETE)
	    );

	/* The following assert statements verify that if this is a sharable
	 * b-tree database, the connection is holding the required table locks,
	 * and that no other connection has any open cursor that conflicts with
	 * this lock.
	 */
	assert(hasSharedCacheTableLock
	       (p, iTable, pKeyInfo != 0, (wrFlag ? 2 : 1)));
	assert(wrFlag == 0 || !hasReadConflicts(p, iTable));

	/* Assert that the caller has opened the required transaction. */
	assert(p->inTrans > TRANS_NONE);
	/* This asserts are disabled as we are not sure if they are necessary or not
	   assert( wrFlag==0 || p->inTrans==TRANS_WRITE );
	 */
	assert(pBt->pPage1 && pBt->pPage1->aData);
	assert(wrFlag == 0 || (pBt->btsFlags & BTS_READ_ONLY) == 0);

	if (wrFlag) {
		allocateTempSpace(pBt);
		if (pBt->pTmpSpace == 0)
			return SQLITE_NOMEM_BKPT;
	}
	if (iTable == 1 && btreePagecount(pBt) == 0) {
		assert(wrFlag == 0);
		iTable = 0;
	}

	/* Now that no other errors can occur, finish filling in the BtCursor
	 * variables and link the cursor into the BtShared list.
	 */
	pCur->pgnoRoot = (Pgno) iTable;
	pCur->iPage = -1;
	pCur->pKeyInfo = pKeyInfo;
	pCur->pBtree = p;
	pCur->pBt = pBt;
	pCur->curFlags = wrFlag ? BTCF_WriteFlag : 0;
	pCur->curPagerFlags = wrFlag ? 0 : PAGER_GET_READONLY;
	/* If there are two or more cursors on the same btree, then all such
	 * cursors *must* have the BTCF_Multiple flag set.
	 */
	for (pX = pBt->pCursor; pX; pX = pX->pNext) {
		if (pX->pgnoRoot == (Pgno) iTable) {
			pX->curFlags |= BTCF_Multiple;
			pCur->curFlags |= BTCF_Multiple;
		}
	}
	pCur->pNext = pBt->pCursor;
	pBt->pCursor = pCur;
	pCur->eState = CURSOR_INVALID;
	return SQLITE_OK;
}

int
sqlite3BtreeCursor(Btree * p,	/* The btree */
		   int iTable,	/* Root page of table to open */
		   int wrFlag,	/* 1 to write. 0 read-only */
		   struct KeyInfo *pKeyInfo,	/* First arg to xCompare() */
		   BtCursor * pCur	/* Write new cursor here */
    )
{
	int rc;
	if (iTable < 1) {
		rc = SQLITE_CORRUPT_BKPT;
	} else {
		sqlite3BtreeEnter(p);
		rc = btreeCursor(p, iTable, wrFlag, pKeyInfo, pCur);
		if (iTable != 1 && (p->db->mdb.pBt == p)) {
			/* Main database and temporary database "files" are backed by
			 * Tarantool, except for the sqlite_master table(s)
			 * (assuming it fits in 1 page).
			 */
			pCur->curFlags |= BTCF_TaCursor;
			pCur->pTaCursor = 0;	/* sqlite3BtreeCursorZero didn't touch it */
		}
		sqlite3BtreeLeave(p);
	}
	return rc;
}

/*
 * Return the size of a BtCursor object in bytes.
 *
 * This interfaces is needed so that users of cursors can preallocate
 * sufficient storage to hold a cursor.  The BtCursor object is opaque
 * to users so they cannot do the sizeof() themselves - they must call
 * this routine.
 */
int
sqlite3BtreeCursorSize(void)
{
	return ROUND8(sizeof(BtCursor));
}

/*
 * Initialize memory that will be converted into a BtCursor object.
 *
 * The simple approach here would be to memset() the entire object
 * to zero.  But it turns out that the apPage[] and aiIdx[] arrays
 * do not need to be zeroed and they are large, so we can save a lot
 * of run-time by skipping the initialization of those elements.
 */
void
sqlite3BtreeCursorZero(BtCursor * p)
{
	memset(p, 0, offsetof(BtCursor, iPage));
}

/*
 * Close a cursor.  The read lock on the database file is released
 * when the last cursor is closed.
 */
int
sqlite3BtreeCloseCursor(BtCursor * pCur)
{
	Btree *pBtree = pCur->pBtree;
	if (pBtree) {
		int i;
		BtShared *pBt = pCur->pBt;
		sqlite3BtreeEnter(pBtree);
		sqlite3BtreeClearCursor(pCur);
		assert(pBt->pCursor != 0);
		if (pBt->pCursor == pCur) {
			pBt->pCursor = pCur->pNext;
		} else {
			BtCursor *pPrev = pBt->pCursor;
			do {
				if (pPrev->pNext == pCur) {
					pPrev->pNext = pCur->pNext;
					break;
				}
				pPrev = pPrev->pNext;
			} while (ALWAYS(pPrev));
		}
		for (i = 0; i <= pCur->iPage; i++) {
			releasePage(pCur->apPage[i]);
		}
		unlockBtreeIfUnused(pBt);
		sqlite3_free(pCur->aOverflow);
		if (pCur->curFlags & BTCF_TaCursor) {
			tarantoolSqlite3CloseCursor(pCur);
		}
		/* sqlite3_free(pCur); */
		sqlite3BtreeLeave(pBtree);
	}
	return SQLITE_OK;
}

/*
 * Make sure the BtCursor* given in the argument has a valid
 * BtCursor.info structure.  If it is not already valid, call
 * btreeParseCell() to fill it in.
 *
 * BtCursor.info is a cache of the information in the current cell.
 * Using this cache reduces the number of calls to btreeParseCell().
 */
#ifndef NDEBUG
static void
assertCellInfo(BtCursor * pCur)
{
	CellInfo info;
	int iPage = pCur->iPage;
	memset(&info, 0, sizeof(info));
	btreeParseCell(pCur->apPage[iPage], pCur->aiIdx[iPage], &info);
	assert(CORRUPT_DB || memcmp(&info, &pCur->info, sizeof(info)) == 0);
}
#else
#define assertCellInfo(x)
#endif
static SQLITE_NOINLINE void
getCellInfo(BtCursor * pCur)
{
	if (pCur->info.nSize == 0) {
		int iPage = pCur->iPage;
		pCur->curFlags |= BTCF_ValidNKey;
		btreeParseCell(pCur->apPage[iPage], pCur->aiIdx[iPage],
			       &pCur->info);
	} else {
		assertCellInfo(pCur);
	}
}

#ifndef NDEBUG			/* The next routine used only within assert() statements */
/*
 * Return true if the given BtCursor is valid.  A valid cursor is one
 * that is currently pointing to a row in a (non-empty) table.
 * This is a verification routine is used only within assert() statements.
 */
int
sqlite3BtreeCursorIsValid(BtCursor * pCur)
{
	return pCur && pCur->eState == CURSOR_VALID;
}
#endif				/* NDEBUG */
int
sqlite3BtreeCursorIsValidNN(BtCursor * pCur)
{
	assert(pCur != 0);
	return pCur->eState == CURSOR_VALID;
}

/*
 * Return the value of the integer key or "rowid" for a table btree.
 * This routine is only valid for a cursor that is pointing into a
 * ordinary table btree.  If the cursor points to an index btree or
 * is invalid, the result of this routine is undefined.
 */
i64
sqlite3BtreeIntegerKey(BtCursor * pCur)
{
	assert(cursorHoldsMutex(pCur));
	assert(pCur->eState == CURSOR_VALID);
	assert(pCur->curIntKey);
	/* tables backed by Tarantool are all "WITHOUT ROWID" */
	assert(!(pCur->curFlags & BTCF_TaCursor));
	getCellInfo(pCur);
	return pCur->info.nKey;
}

/*
 * Return the number of bytes of payload for the entry that pCur is
 * currently pointing to.  For table btrees, this will be the amount
 * of data.  For index btrees, this will be the size of the key.
 *
 * The caller must guarantee that the cursor is pointing to a non-NULL
 * valid entry.  In other words, the calling procedure must guarantee
 * that the cursor has Cursor.eState==CURSOR_VALID.
 */
u32
sqlite3BtreePayloadSize(BtCursor * pCur)
{
	assert(cursorHoldsMutex(pCur));
	assert(pCur->eState == CURSOR_VALID);
	if (pCur->curFlags & BTCF_TaCursor) {
		u32 sz;
		tarantoolSqlite3PayloadFetch(pCur, &sz);
		return sz;
	}
	getCellInfo(pCur);
	return pCur->info.nPayload;
}

/*
 * Given the page number of an overflow page in the database (parameter
 * ovfl), this function finds the page number of the next page in the
 * linked list of overflow pages.
 *
 * If an error occurs an SQLite error code is returned. Otherwise:
 *
 * The page number of the next overflow page in the linked list is
 * written to *pPgnoNext. If page ovfl is the last page in its linked
 * list, *pPgnoNext is set to zero.
 *
 * If ppPage is not NULL, and a reference to the MemPage object corresponding
 * to page number pOvfl was obtained, then *ppPage is set to point to that
 * reference. It is the responsibility of the caller to call releasePage()
 * on *ppPage to free the reference. In no reference was obtained (because
 * the pointer-map was used to obtain the value for *pPgnoNext), then
 * *ppPage is set to zero.
 */
static int
getOverflowPage(BtShared * pBt,	/* The database file */
		Pgno ovfl,	/* Current overflow page number */
		MemPage ** ppPage,	/* OUT: MemPage handle (may be NULL) */
		Pgno * pPgnoNext	/* OUT: Next overflow page number */
    )
{
	Pgno next = 0;
	MemPage *pPage = 0;
	int rc = SQLITE_OK;

	assert(sqlite3_mutex_held(pBt->mutex));
	assert(pPgnoNext);

	assert(next == 0 || rc == SQLITE_DONE);
	if (rc == SQLITE_OK) {
		rc = btreeGetPage(pBt, ovfl, &pPage,
				  (ppPage == 0) ? PAGER_GET_READONLY : 0);
		assert(rc == SQLITE_OK || pPage == 0);
		if (rc == SQLITE_OK) {
			next = get4byte(pPage->aData);
		}
	}

	*pPgnoNext = next;
	if (ppPage) {
		*ppPage = pPage;
	} else {
		releasePage(pPage);
	}
	return (rc == SQLITE_DONE ? SQLITE_OK : rc);
}

/*
 * Copy data from a buffer to a page, or from a page to a buffer.
 *
 * pPayload is a pointer to data stored on database page pDbPage.
 * If argument eOp is false, then nByte bytes of data are copied
 * from pPayload to the buffer pointed at by pBuf. If eOp is true,
 * then sqlite3PagerWrite() is called on pDbPage and nByte bytes
 * of data are copied from the buffer pBuf to pPayload.
 *
 * SQLITE_OK is returned on success, otherwise an error code.
 */
static int
copyPayload(void *pPayload,	/* Pointer to page data */
	    void *pBuf,		/* Pointer to buffer */
	    int nByte,		/* Number of bytes to copy */
	    int eOp,		/* 0 -> copy from page, 1 -> copy to page */
	    DbPage * pDbPage	/* Page containing pPayload */
    )
{
	if (eOp) {
		/* Copy data from buffer to page (a write operation) */
		int rc = sqlite3PagerWrite(pDbPage);
		if (rc != SQLITE_OK) {
			return rc;
		}
		memcpy(pPayload, pBuf, nByte);
	} else {
		/* Copy data from page to buffer (a read operation) */
		memcpy(pBuf, pPayload, nByte);
	}
	return SQLITE_OK;
}

/*
 * This function is used to read or overwrite payload information
 * for the entry that the pCur cursor is pointing to. The eOp
 * argument is interpreted as follows:
 *
 *   0: The operation is a read. Populate the overflow cache.
 *   1: The operation is a write. Populate the overflow cache.
 *   2: The operation is a read. Do not populate the overflow cache.
 *
 * A total of "amt" bytes are read or written beginning at "offset".
 * Data is read to or from the buffer pBuf.
 *
 * The content being read or written might appear on the main page
 * or be scattered out on multiple overflow pages.
 *
 * If the current cursor entry uses one or more overflow pages and the
 * eOp argument is not 2, this function may allocate space for and lazily
 * populates the overflow page-list cache array (BtCursor.aOverflow).
 * Subsequent calls use this cache to make seeking to the supplied offset
 * more efficient.
 *
 * Once an overflow page-list cache has been allocated, it may be
 * invalidated if some other cursor writes to the same table, or if
 * the cursor is moved to a different row.
 *
 * Creating a table (may require moving an overflow page).
 */
static int
accessPayload(BtCursor * pCur,	/* Cursor pointing to entry to read from */
	      u32 offset,	/* Begin reading this far into payload */
	      u32 amt,		/* Read this many bytes */
	      unsigned char *pBuf,	/* Write the bytes into this buffer */
	      int eOp		/* zero to read. non-zero to write. */
    )
{
	if (pCur->curFlags & BTCF_TaCursor) {
		const void *pPayload;
		u32 sz;
		pPayload = tarantoolSqlite3PayloadFetch(pCur, &sz);
		if ((uptr) (offset + amt) > sz)
			return SQLITE_CORRUPT_BKPT;
		memcpy(pBuf, pPayload + offset, amt);
		return SQLITE_OK;
	}

	unsigned char *aPayload;
	int rc = SQLITE_OK;
	int iIdx = 0;
	MemPage *pPage = pCur->apPage[pCur->iPage];	/* Btree page of current entry */
	BtShared *pBt = pCur->pBt;	/* Btree this cursor belongs to */
#ifdef SQLITE_DIRECT_OVERFLOW_READ
	unsigned char *const pBufStart = pBuf;
	int bEnd;		/* True if reading to end of data */
#endif

	assert(pPage);
	assert(pCur->eState == CURSOR_VALID);
	assert(pCur->aiIdx[pCur->iPage] < pPage->nCell);
	assert(cursorHoldsMutex(pCur));
	assert(eOp != 2 || offset == 0);	/* Always start from beginning for eOp==2 */

	getCellInfo(pCur);
	aPayload = pCur->info.pPayload;
#ifdef SQLITE_DIRECT_OVERFLOW_READ
	bEnd = offset + amt == pCur->info.nPayload;
#endif
	assert(offset + amt <= pCur->info.nPayload);

	assert(aPayload > pPage->aData);
	if ((uptr) (aPayload - pPage->aData) >
	    (pBt->usableSize - pCur->info.nLocal)) {
		/* Trying to read or write past the end of the data is an error.  The
		 * conditional above is really:
		 *    &aPayload[pCur->info.nLocal] > &pPage->aData[pBt->usableSize]
		 * but is recast into its current form to avoid integer overflow problems
		 */
		return SQLITE_CORRUPT_BKPT;
	}

	/* Check if data must be read/written to/from the btree page itself. */
	if (offset < pCur->info.nLocal) {
		int a = amt;
		if (a + offset > pCur->info.nLocal) {
			a = pCur->info.nLocal - offset;
		}
		rc = copyPayload(&aPayload[offset], pBuf, a, (eOp & 0x01),
				 pPage->pDbPage);
		offset = 0;
		pBuf += a;
		amt -= a;
	} else {
		offset -= pCur->info.nLocal;
	}

	if (rc == SQLITE_OK && amt > 0) {
		const u32 ovflSize = pBt->usableSize - 4;	/* Bytes content per ovfl page */
		Pgno nextPage;

		nextPage = get4byte(&aPayload[pCur->info.nLocal]);

		/* If the BtCursor.aOverflow[] has not been allocated, allocate it now.
		 * Except, do not allocate aOverflow[] for eOp==2.
		 *
		 * The aOverflow[] array is sized at one entry for each overflow page
		 * in the overflow chain. The page number of the first overflow page is
		 * stored in aOverflow[0], etc. A value of 0 in the aOverflow[] array
		 * means "not yet known" (the cache is lazily populated).
		 */
		if (eOp != 2 && (pCur->curFlags & BTCF_ValidOvfl) == 0) {
			int nOvfl =
			    (pCur->info.nPayload - pCur->info.nLocal +
			     ovflSize - 1) / ovflSize;
			if (nOvfl > pCur->nOvflAlloc) {
				Pgno *aNew =
				    (Pgno *) sqlite3Realloc(pCur->aOverflow,
							    nOvfl * 2 *
							    sizeof(Pgno)
				    );
				if (aNew == 0) {
					rc = SQLITE_NOMEM_BKPT;
				} else {
					pCur->nOvflAlloc = nOvfl * 2;
					pCur->aOverflow = aNew;
				}
			}
			if (rc == SQLITE_OK) {
				memset(pCur->aOverflow, 0,
				       nOvfl * sizeof(Pgno));
				pCur->curFlags |= BTCF_ValidOvfl;
			}
		}

		/* If the overflow page-list cache has been allocated and the
		 * entry for the first required overflow page is valid, skip
		 * directly to it.
		 */
		if ((pCur->curFlags & BTCF_ValidOvfl) != 0
		    && pCur->aOverflow[offset / ovflSize]
		    ) {
			iIdx = (offset / ovflSize);
			nextPage = pCur->aOverflow[iIdx];
			offset = (offset % ovflSize);
		}

		for (; rc == SQLITE_OK && amt > 0 && nextPage; iIdx++) {

			/* If required, populate the overflow page-list cache. */
			if ((pCur->curFlags & BTCF_ValidOvfl) != 0) {
				assert(pCur->aOverflow[iIdx] == 0
				       || pCur->aOverflow[iIdx] == nextPage
				       || CORRUPT_DB);
				pCur->aOverflow[iIdx] = nextPage;
			}

			if (offset >= ovflSize) {
				/* The only reason to read this page is to obtain the page
				 * number for the next page in the overflow chain. The page
				 * data is not required. So first try to lookup the overflow
				 * page-list cache, if any, then fall back to the getOverflowPage()
				 * function.
				 *
				 * Note that the aOverflow[] array must be allocated because eOp!=2
				 * here.  If eOp==2, then offset==0 and this branch is never taken.
				 */
				assert(eOp != 2);
				assert(pCur->curFlags & BTCF_ValidOvfl);
				assert(pCur->pBtree->db == pBt->db);
				if (pCur->aOverflow[iIdx + 1]) {
					nextPage = pCur->aOverflow[iIdx + 1];
				} else {
					rc = getOverflowPage(pBt, nextPage, 0,
							     &nextPage);
				}
				offset -= ovflSize;
			} else {
				/* Need to read this page properly. It contains some of the
				 * range of data that is being read (eOp==0) or written (eOp!=0).
				 */
#ifdef SQLITE_DIRECT_OVERFLOW_READ
				sqlite3_file *fd;
#endif
				int a = amt;
				if (a + offset > ovflSize) {
					a = ovflSize - offset;
				}
#ifdef SQLITE_DIRECT_OVERFLOW_READ
				/* If all the following are true:
				 *
				 *   1) this is a read operation, and
				 *   2) data is required from the start of this overflow page, and
				 *   3) the database is file-backed, and
				 *   4) there is no open write-transaction, and
				 *   5) the database is not a WAL database,
				 *   6) all data from the page is being read.
				 *   7) at least 4 bytes have already been read into the output buffer
				 *
				 * then data can be read directly from the database file into the
				 * output buffer, bypassing the page-cache altogether. This speeds
				 * up loading large records that span many overflow pages.
				 */
				if ((eOp & 0x01) == 0	/* (1) */
				    && offset == 0	/* (2) */
				    && (bEnd || a == ovflSize)	/* (6) */
				    &&pBt->inTransaction == TRANS_READ	/* (4) */
				    && (fd = sqlite3PagerFile(pBt->pPager))->pMethods	/* (3) */
				    && &pBuf[-4] >= pBufStart	/* (7) */
				    ) {
					u8 aSave[4];
					u8 *aWrite = &pBuf[-4];
					assert(aWrite >= pBufStart);	/* hence (7) */
					memcpy(aSave, aWrite, 4);
					rc = sqlite3OsRead(fd, aWrite, a + 4,
							   (i64) pBt->pageSize *
							   (nextPage - 1));
					nextPage = get4byte(aWrite);
					memcpy(aWrite, aSave, 4);
				} else
#endif

				{
					DbPage *pDbPage;
					rc = sqlite3PagerGet(pBt->pPager,
							     nextPage, &pDbPage,
							     ((eOp & 0x01) ==
							      0 ?
							      PAGER_GET_READONLY
							      : 0)
					    );
					if (rc == SQLITE_OK) {
						aPayload =
						    sqlite3PagerGetData
						    (pDbPage);
						nextPage = get4byte(aPayload);
						rc = copyPayload(&aPayload
								 [offset + 4],
								 pBuf, a,
								 (eOp & 0x01),
								 pDbPage);
						sqlite3PagerUnref(pDbPage);
						offset = 0;
					}
				}
				amt -= a;
				pBuf += a;
			}
		}
	}

	if (rc == SQLITE_OK && amt > 0) {
		return SQLITE_CORRUPT_BKPT;
	}
	return rc;
}

/*
 * Read part of the payload for the row at which that cursor pCur is currently
 * pointing.  "amt" bytes will be transferred into pBuf[].  The transfer
 * begins at "offset".
 *
 * pCur can be pointing to either a table or an index b-tree.
 * If pointing to a table btree, then the content section is read.  If
 * pCur is pointing to an index b-tree then the key section is read.
 *
 * For sqlite3BtreePayload(), the caller must ensure that pCur is pointing
 * to a valid row in the table.  For sqlite3BtreePayloadChecked(), the
 * cursor might be invalid or might need to be restored before being read.
 *
 * Return SQLITE_OK on success or an error code if anything goes
 * wrong.  An error is returned if "offset+amt" is larger than
 * the available payload.
 */
int
sqlite3BtreePayload(BtCursor * pCur, u32 offset, u32 amt, void *pBuf)
{
	assert(cursorHoldsMutex(pCur));
	assert(pCur->eState == CURSOR_VALID);
	assert((pCur->curFlags & BTCF_TaCursor) ||
	       (pCur->iPage >= 0 && pCur->apPage[pCur->iPage]));
	assert((pCur->curFlags & BTCF_TaCursor) ||
	       pCur->aiIdx[pCur->iPage] < pCur->apPage[pCur->iPage]->nCell);
	return accessPayload(pCur, offset, amt, (unsigned char *)pBuf, 0);
}

#ifndef SQLITE_OMIT_INCRBLOB
int
sqlite3BtreePayloadChecked(BtCursor * pCur, u32 offset, u32 amt, void *pBuf)
{
	int rc;
	if (pCur->eState == CURSOR_INVALID) {
		return SQLITE_ABORT;
	}
	assert(cursorOwnsBtShared(pCur));
	rc = restoreCursorPosition(pCur);
	if (rc == SQLITE_OK) {
		assert(pCur->eState == CURSOR_VALID);
		assert((pCur->curFlags & BTCF_TaCursor) ||
		       (pCur->iPage >= 0 && pCur->apPage[pCur->iPage]));
		assert((pCur->curFlags & BTCF_TaCursor) ||
		       pCur->aiIdx[pCur->iPage] <
		       pCur->apPage[pCur->iPage]->nCell);
		rc = accessPayload(pCur, offset, amt, pBuf, 0);
	}
	return rc;
}
#endif				/* SQLITE_OMIT_INCRBLOB */

/*
 * Return a pointer to payload information from the entry that the
 * pCur cursor is pointing to.  The pointer is to the beginning of
 * the key if index btrees (pPage->intKey==0) and is the data for
 * table btrees (pPage->intKey==1). The number of bytes of available
 * key/data is written into *pAmt.  If *pAmt==0, then the value
 * returned will not be a valid pointer.
 *
 * This routine is an optimization.  It is common for the entire key
 * and data to fit on the local page and for there to be no overflow
 * pages.  When that is so, this routine can be used to access the
 * key and data without making a copy.  If the key and/or data spills
 * onto overflow pages, then accessPayload() must be used to reassemble
 * the key/data and copy it into a preallocated buffer.
 *
 * The pointer returned by this routine looks directly into the cached
 * page of the database.  The data might change or move the next time
 * any btree routine is called.
 */
static const void *
fetchPayload(BtCursor * pCur,	/* Cursor pointing to entry to read from */
	     u32 * pAmt		/* Write the number of available bytes here */
    )
{
	u32 amt;
	assert(pCur != 0 && pCur->iPage >= 0 && pCur->apPage[pCur->iPage]);
	assert(pCur->eState == CURSOR_VALID);
	assert(sqlite3_mutex_held(pCur->pBtree->db->mutex));
	assert(cursorOwnsBtShared(pCur));
	assert(pCur->aiIdx[pCur->iPage] < pCur->apPage[pCur->iPage]->nCell);
	assert(pCur->info.nSize > 0);
	assert(pCur->info.pPayload > pCur->apPage[pCur->iPage]->aData
	       || CORRUPT_DB);
	assert(pCur->info.pPayload < pCur->apPage[pCur->iPage]->aDataEnd
	       || CORRUPT_DB);
	amt = (int)(pCur->apPage[pCur->iPage]->aDataEnd - pCur->info.pPayload);
	if (pCur->info.nLocal < amt)
		amt = pCur->info.nLocal;
	*pAmt = amt;
	return (void *)pCur->info.pPayload;
}

/*
 * For the entry that cursor pCur is point to, return as
 * many bytes of the key or data as are available on the local
 * b-tree page.  Write the number of available bytes into *pAmt.
 *
 * The pointer returned is ephemeral.  The key/data may move
 * or be destroyed on the next call to any Btree routine,
 * including calls from other threads against the same cache.
 * Hence, a mutex on the BtShared should be held prior to calling
 * this routine.
 *
 * These routines is used to get quick access to key and data
 * in the common case where no overflow pages are used.
 */
const void *
sqlite3BtreePayloadFetch(BtCursor * pCur, u32 * pAmt)
{
	if (pCur->curFlags & BTCF_TaCursor) {
		return tarantoolSqlite3PayloadFetch(pCur, pAmt);
	}
	return fetchPayload(pCur, pAmt);
}

/*
 * Move the cursor down to a new child page.  The newPgno argument is the
 * page number of the child page to move to.
 *
 * This function returns SQLITE_CORRUPT if the page-header flags field of
 * the new child page does not match the flags field of the parent (i.e.
 * if an intkey page appears to be the parent of a non-intkey page, or
 * vice-versa).
 */
static int
moveToChild(BtCursor * pCur, u32 newPgno)
{
	BtShared *pBt = pCur->pBt;

	assert(cursorOwnsBtShared(pCur));
	assert(pCur->eState == CURSOR_VALID);
	assert(pCur->iPage < BTCURSOR_MAX_DEPTH);
	assert(pCur->iPage >= 0);
	if (pCur->iPage >= (BTCURSOR_MAX_DEPTH - 1)) {
		return SQLITE_CORRUPT_BKPT;
	}
	pCur->info.nSize = 0;
	pCur->curFlags &= ~(BTCF_ValidNKey | BTCF_ValidOvfl);
	pCur->iPage++;
	pCur->aiIdx[pCur->iPage] = 0;
	return getAndInitPage(pBt, newPgno, &pCur->apPage[pCur->iPage],
			      pCur, pCur->curPagerFlags);
}

#if SQLITE_DEBUG
/*
 * Page pParent is an internal (non-leaf) tree page. This function
 * asserts that page number iChild is the left-child if the iIdx'th
 * cell in page pParent. Or, if iIdx is equal to the total number of
 * cells in pParent, that page number iChild is the right-child of
 * the page.
 */
static void
assertParentIndex(MemPage * pParent, int iIdx, Pgno iChild)
{
	if (CORRUPT_DB)
		return;		/* The conditions tested below might not be true
				 * in a corrupt database
				 */
	assert(iIdx <= pParent->nCell);
	if (iIdx == pParent->nCell) {
		assert(get4byte(&pParent->aData[pParent->hdrOffset + 8]) ==
		       iChild);
	} else {
		assert(get4byte(findCell(pParent, iIdx)) == iChild);
	}
}
#else
#define assertParentIndex(x,y,z)
#endif

/*
 * Move the cursor up to the parent page.
 *
 * pCur->idx is set to the cell index that contains the pointer
 * to the page we are coming from.  If we are coming from the
 * right-most child page then pCur->idx is set to one more than
 * the largest cell index.
 */
static void
moveToParent(BtCursor * pCur)
{
	assert(cursorOwnsBtShared(pCur));
	assert(pCur->eState == CURSOR_VALID);
	assert(pCur->iPage > 0);
	assert(pCur->apPage[pCur->iPage]);
	assertParentIndex(pCur->apPage[pCur->iPage - 1],
			  pCur->aiIdx[pCur->iPage - 1],
			  pCur->apPage[pCur->iPage]->pgno);
	testcase(pCur->aiIdx[pCur->iPage - 1] >
		 pCur->apPage[pCur->iPage - 1]->nCell);
	pCur->info.nSize = 0;
	pCur->curFlags &= ~(BTCF_ValidNKey | BTCF_ValidOvfl);
	releasePageNotNull(pCur->apPage[pCur->iPage--]);
}

/*
 * Move the cursor to point to the root page of its b-tree structure.
 *
 * If the table has a virtual root page, then the cursor is moved to point
 * to the virtual root page instead of the actual root page. A table has a
 * virtual root page when the actual root page contains no cells and a
 * single child page. This can only happen with the table rooted at page 1.
 *
 * If the b-tree structure is empty, the cursor state is set to
 * CURSOR_INVALID. Otherwise, the cursor is set to point to the first
 * cell located on the root (or virtual root) page and the cursor state
 * is set to CURSOR_VALID.
 *
 * If this function returns successfully, it may be assumed that the
 * page-header flags indicate that the [virtual] root-page is the expected
 * kind of b-tree page (i.e. if when opening the cursor the caller did not
 * specify a KeyInfo structure the flags byte is set to 0x05 or 0x0D,
 * indicating a table b-tree, or if the caller did specify a KeyInfo
 * structure the flags byte is set to 0x02 or 0x0A, indicating an index
 * b-tree).
 */
static int
moveToRoot(BtCursor * pCur)
{
	MemPage *pRoot;
	int rc = SQLITE_OK;

	assert(cursorOwnsBtShared(pCur));
	assert(CURSOR_INVALID < CURSOR_REQUIRESEEK);
	assert(CURSOR_VALID < CURSOR_REQUIRESEEK);
	assert(CURSOR_FAULT > CURSOR_REQUIRESEEK);
	if (pCur->eState >= CURSOR_REQUIRESEEK) {
		if (pCur->eState == CURSOR_FAULT) {
			assert(pCur->skipNext != SQLITE_OK);
			return pCur->skipNext;
		}
		sqlite3BtreeClearCursor(pCur);
	}

	if (pCur->iPage >= 0) {
		if (pCur->iPage) {
			do {
				assert(pCur->apPage[pCur->iPage] != 0);
				releasePageNotNull(pCur->apPage[pCur->iPage--]);
			} while (pCur->iPage);
			goto skip_init;
		}
	} else if (pCur->pgnoRoot == 0) {
		pCur->eState = CURSOR_INVALID;
		return SQLITE_OK;
	} else {
		assert(pCur->iPage == (-1));
		rc = getAndInitPage(pCur->pBtree->pBt, pCur->pgnoRoot,
				    &pCur->apPage[0], 0, pCur->curPagerFlags);
		if (rc != SQLITE_OK) {
			pCur->eState = CURSOR_INVALID;
			return rc;
		}
		pCur->iPage = 0;
		pCur->curIntKey = pCur->apPage[0]->intKey;
	}
	pRoot = pCur->apPage[0];
	assert(pRoot->pgno == pCur->pgnoRoot);

	/* If pCur->pKeyInfo is not NULL, then the caller that opened this cursor
	 * expected to open it on an index b-tree. Otherwise, if pKeyInfo is
	 * NULL, the caller expects a table b-tree. If this is not the case,
	 * return an SQLITE_CORRUPT error.
	 *
	 * Earlier versions of SQLite assumed that this test could not fail
	 * if the root page was already loaded when this function was called (i.e.
	 * if pCur->iPage>=0). But this is not so if the database is corrupted
	 * in such a way that page pRoot is linked into a second b-tree table
	 * (or the freelist).
	 */
	assert(pRoot->intKey == 1 || pRoot->intKey == 0);
	if (pRoot->isInit == 0 || (pCur->pKeyInfo == 0) != pRoot->intKey) {
		return SQLITE_CORRUPT_BKPT;
	}

 skip_init:
	pCur->aiIdx[0] = 0;
	pCur->info.nSize = 0;
	pCur->curFlags &= ~(BTCF_AtLast | BTCF_ValidNKey | BTCF_ValidOvfl);

	pRoot = pCur->apPage[0];
	if (pRoot->nCell > 0) {
		pCur->eState = CURSOR_VALID;
	} else if (!pRoot->leaf) {
		Pgno subpage;
		if (pRoot->pgno != 1)
			return SQLITE_CORRUPT_BKPT;
		subpage = get4byte(&pRoot->aData[pRoot->hdrOffset + 8]);
		pCur->eState = CURSOR_VALID;
		rc = moveToChild(pCur, subpage);
	} else {
		pCur->eState = CURSOR_INVALID;
	}
	return rc;
}

/*
 * Move the cursor down to the left-most leaf entry beneath the
 * entry to which it is currently pointing.
 *
 * The left-most leaf is the one with the smallest key - the first
 * in ascending order.
 */
static int
moveToLeftmost(BtCursor * pCur)
{
	Pgno pgno;
	int rc = SQLITE_OK;
	MemPage *pPage;

	assert(cursorOwnsBtShared(pCur));
	assert(pCur->eState == CURSOR_VALID);
	while (rc == SQLITE_OK && !(pPage = pCur->apPage[pCur->iPage])->leaf) {
		assert(pCur->aiIdx[pCur->iPage] < pPage->nCell);
		pgno = get4byte(findCell(pPage, pCur->aiIdx[pCur->iPage]));
		rc = moveToChild(pCur, pgno);
	}
	return rc;
}

/*
 * Move the cursor down to the right-most leaf entry beneath the
 * page to which it is currently pointing.  Notice the difference
 * between moveToLeftmost() and moveToRightmost().  moveToLeftmost()
 * finds the left-most entry beneath the *entry* whereas moveToRightmost()
 * finds the right-most entry beneath the *page*.
 *
 * The right-most entry is the one with the largest key - the last
 * key in ascending order.
 */
static int
moveToRightmost(BtCursor * pCur)
{
	Pgno pgno;
	int rc = SQLITE_OK;
	MemPage *pPage = 0;

	assert(cursorOwnsBtShared(pCur));
	assert(pCur->eState == CURSOR_VALID);
	while (!(pPage = pCur->apPage[pCur->iPage])->leaf) {
		pgno = get4byte(&pPage->aData[pPage->hdrOffset + 8]);
		pCur->aiIdx[pCur->iPage] = pPage->nCell;
		rc = moveToChild(pCur, pgno);
		if (rc)
			return rc;
	}
	pCur->aiIdx[pCur->iPage] = pPage->nCell - 1;
	assert(pCur->info.nSize == 0);
	assert((pCur->curFlags & BTCF_ValidNKey) == 0);
	return SQLITE_OK;
}

/* Move the cursor to the first entry in the table.  Return SQLITE_OK
 * on success.  Set *pRes to 0 if the cursor actually points to something
 * or set *pRes to 1 if the table is empty.
 */
int
sqlite3BtreeFirst(BtCursor * pCur, int *pRes)
{
	int rc;

	assert(cursorOwnsBtShared(pCur));
	assert(sqlite3_mutex_held(pCur->pBtree->db->mutex));
	if (pCur->curFlags & BTCF_TaCursor) {
		return tarantoolSqlite3First(pCur, pRes);
	}
	rc = moveToRoot(pCur);
	if (rc == SQLITE_OK) {
		if (pCur->eState == CURSOR_INVALID) {
			assert(pCur->pgnoRoot == 0
			       || pCur->apPage[pCur->iPage]->nCell == 0);
			*pRes = 1;
		} else {
			assert(pCur->apPage[pCur->iPage]->nCell > 0);
			*pRes = 0;
			rc = moveToLeftmost(pCur);
		}
	}
	return rc;
}

/* Move the cursor to the last entry in the table.  Return SQLITE_OK
 * on success.  Set *pRes to 0 if the cursor actually points to something
 * or set *pRes to 1 if the table is empty.
 */
int
sqlite3BtreeLast(BtCursor * pCur, int *pRes)
{
	int rc;

	assert(cursorOwnsBtShared(pCur));
	assert(sqlite3_mutex_held(pCur->pBtree->db->mutex));

	if (pCur->curFlags & BTCF_TaCursor) {
		return tarantoolSqlite3Last(pCur, pRes);
	}

	/* If the cursor already points to the last entry, this is a no-op. */
	if (CURSOR_VALID == pCur->eState && (pCur->curFlags & BTCF_AtLast) != 0) {
#ifdef SQLITE_DEBUG
		/* This block serves to assert() that the cursor really does point
		 * to the last entry in the b-tree.
		 */
		int ii;
		for (ii = 0; ii < pCur->iPage; ii++) {
			assert(pCur->aiIdx[ii] == pCur->apPage[ii]->nCell);
		}
		assert(pCur->aiIdx[pCur->iPage] ==
		       pCur->apPage[pCur->iPage]->nCell - 1);
		assert(pCur->apPage[pCur->iPage]->leaf);
#endif
		return SQLITE_OK;
	}

	rc = moveToRoot(pCur);
	if (rc == SQLITE_OK) {
		if (CURSOR_INVALID == pCur->eState) {
			assert(pCur->pgnoRoot == 0
			       || pCur->apPage[pCur->iPage]->nCell == 0);
			*pRes = 1;
		} else {
			assert(pCur->eState == CURSOR_VALID);
			*pRes = 0;
			rc = moveToRightmost(pCur);
			if (rc == SQLITE_OK) {
				pCur->curFlags |= BTCF_AtLast;
			} else {
				pCur->curFlags &= ~BTCF_AtLast;
			}

		}
	}
	return rc;
}

/* Move the cursor so that it points to an entry near the key
 * specified by pIdxKey or intKey.   Return a success code.
 *
 * For INTKEY tables, the intKey parameter is used.  pIdxKey
 * must be NULL.  For index tables, pIdxKey is used and intKey
 * is ignored.
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
 *                  is smaller than intKey/pIdxKey or if the table is empty
 *                  and the cursor is therefore left point to nothing.
 *
 *     *pRes==0     The cursor is left pointing at an entry that
 *                  exactly matches intKey/pIdxKey.
 *
 *     *pRes>0      The cursor is left pointing at an entry that
 *                  is larger than intKey/pIdxKey.
 *
 * For index tables, the pIdxKey->eqSeen field is set to 1 if there
 * exists an entry in the table that exactly matches pIdxKey.
 */
int
sqlite3BtreeMovetoUnpacked(BtCursor * pCur,	/* The cursor to be moved */
			   UnpackedRecord * pIdxKey,	/* Unpacked index key */
			   i64 intKey,	/* The table key */
			   int biasRight,	/* If true, bias the search to the high end */
			   int *pRes	/* Write search results here */
    )
{
	int rc;
	RecordCompare xRecordCompare;

	assert(cursorOwnsBtShared(pCur));
	assert(sqlite3_mutex_held(pCur->pBtree->db->mutex));
	assert(pRes);
	assert((pIdxKey == 0) == (pCur->pKeyInfo == 0));
	assert(pCur->eState != CURSOR_VALID
	       || (pIdxKey == 0) == (pCur->curIntKey != 0));

	if (pCur->curFlags & BTCF_TaCursor) {
		assert(pIdxKey);
		/*
		 * Note: pIdxKey/intKey are mutually-exclusive and all Tarantool
		 * tables are WITHOUT ROWID, hence no intKey parameter.
		 * BiasRight is a hint used during binary search; ignore it for now.
		 */
		return tarantoolSqlite3MovetoUnpacked(pCur, pIdxKey, pRes);
	}

	/* If the cursor is already positioned at the point we are trying
	 * to move to, then just return without doing any work
	 */
	if (pIdxKey == 0
	    && pCur->eState == CURSOR_VALID
	    && (pCur->curFlags & BTCF_ValidNKey) != 0) {
		if (pCur->info.nKey == intKey) {
			*pRes = 0;
			return SQLITE_OK;
		}
		if ((pCur->curFlags & BTCF_AtLast) != 0
		    && pCur->info.nKey < intKey) {
			*pRes = -1;
			return SQLITE_OK;
		}
	}

	if (pIdxKey) {
		xRecordCompare = sqlite3VdbeFindCompare(pIdxKey);
		pIdxKey->errCode = 0;
		assert(pIdxKey->default_rc == 1
		       || pIdxKey->default_rc == 0
		       || pIdxKey->default_rc == -1);
	} else {
		xRecordCompare = 0;	/* All keys are integers */
	}

	rc = moveToRoot(pCur);
	if (rc) {
		return rc;
	}
	assert(pCur->pgnoRoot == 0 || pCur->apPage[pCur->iPage]);
	assert(pCur->pgnoRoot == 0 || pCur->apPage[pCur->iPage]->isInit);
	assert(pCur->eState == CURSOR_INVALID
	       || pCur->apPage[pCur->iPage]->nCell > 0);
	if (pCur->eState == CURSOR_INVALID) {
		*pRes = -1;
		assert(pCur->pgnoRoot == 0
		       || pCur->apPage[pCur->iPage]->nCell == 0);
		return SQLITE_OK;
	}
	assert(pCur->apPage[0]->intKey == pCur->curIntKey);
	assert(pCur->curIntKey || pIdxKey);
	for (;;) {
		int lwr, upr, idx, c;
		Pgno chldPg;
		MemPage *pPage = pCur->apPage[pCur->iPage];
		u8 *pCell;	/* Pointer to current cell in pPage */

		/* pPage->nCell must be greater than zero. If this is the root-page
		 * the cursor would have been INVALID above and this for(;;) loop
		 * not run. If this is not the root-page, then the moveToChild() routine
		 * would have already detected db corruption. Similarly, pPage must
		 * be the right kind (index or table) of b-tree page. Otherwise
		 * a moveToChild() or moveToRoot() call would have detected corruption.
		 */
		assert(pPage->nCell > 0);
		assert(pPage->intKey == (pIdxKey == 0));
		lwr = 0;
		upr = pPage->nCell - 1;
		assert(biasRight == 0 || biasRight == 1);
		idx = upr >> (1 - biasRight);	/* idx = biasRight ? upr : (lwr+upr)/2; */
		pCur->aiIdx[pCur->iPage] = (u16) idx;
		if (xRecordCompare == 0) {
			for (;;) {
				i64 nCellKey;
				pCell = findCellPastPtr(pPage, idx);
				if (pPage->intKeyLeaf) {
					while (0x80 <= *(pCell++)) {
						if (pCell >= pPage->aDataEnd)
							return
							    SQLITE_CORRUPT_BKPT;
					}
				}
				getVarint(pCell, (u64 *) & nCellKey);
				if (nCellKey < intKey) {
					lwr = idx + 1;
					if (lwr > upr) {
						c = -1;
						break;
					}
				} else if (nCellKey > intKey) {
					upr = idx - 1;
					if (lwr > upr) {
						c = +1;
						break;
					}
				} else {
					assert(nCellKey == intKey);
					pCur->aiIdx[pCur->iPage] = (u16) idx;
					if (!pPage->leaf) {
						lwr = idx;
						goto moveto_next_layer;
					} else {
						pCur->curFlags |=
						    BTCF_ValidNKey;
						pCur->info.nKey = nCellKey;
						pCur->info.nSize = 0;
						*pRes = 0;
						return SQLITE_OK;
					}
				}
				assert(lwr + upr >= 0);
				idx = (lwr + upr) >> 1;	/* idx = (lwr+upr)/2; */
			}
		} else {
			for (;;) {
				int nCell;	/* Size of the pCell cell in bytes */
				pCell = findCellPastPtr(pPage, idx);

				/* The maximum supported page-size is 65536 bytes. This means that
				 * the maximum number of record bytes stored on an index B-Tree
				 * page is less than 16384 bytes and may be stored as a 2-byte
				 * varint. This information is used to attempt to avoid parsing
				 * the entire cell by checking for the cases where the record is
				 * stored entirely within the b-tree page by inspecting the first
				 * 2 bytes of the cell.
				 */
				nCell = pCell[0];
				if (nCell <= pPage->max1bytePayload) {
					/* This branch runs if the record-size field of the cell is a
					 * single byte varint and the record fits entirely on the main
					 * b-tree page.
					 */
					testcase(pCell + nCell + 1 ==
						 pPage->aDataEnd);
					c = xRecordCompare(nCell,
							   (void *)&pCell[1],
							   pIdxKey);
				} else if (!(pCell[1] & 0x80)
					   && (nCell =
					       ((nCell & 0x7f) << 7) +
					       pCell[1]) <= pPage->maxLocal) {
					/* The record-size field is a 2 byte varint and the record
					 * fits entirely on the main b-tree page.
					 */
					testcase(pCell + nCell + 2 ==
						 pPage->aDataEnd);
					c = xRecordCompare(nCell,
							   (void *)&pCell[2],
							   pIdxKey);
				} else {
					/* The record flows over onto one or more overflow pages. In
					 * this case the whole cell needs to be parsed, a buffer allocated
					 * and accessPayload() used to retrieve the record into the
					 * buffer before VdbeRecordCompare() can be called.
					 *
					 * If the record is corrupt, the xRecordCompare routine may read
					 * up to two varints past the end of the buffer. An extra 18
					 * bytes of padding is allocated at the end of the buffer in
					 * case this happens.
					 */
					void *pCellKey;
					u8 *const pCellBody =
					    pCell - pPage->childPtrSize;
					pPage->xParseCell(pPage, pCellBody,
							  &pCur->info);
					nCell = (int)pCur->info.nKey;
					testcase(nCell < 0);	/* True if key size is 2^32 or more */
					testcase(nCell == 0);	/* Invalid key size:  0x80 0x80 0x00 */
					testcase(nCell == 1);	/* Invalid key size:  0x80 0x80 0x01 */
					testcase(nCell == 2);	/* Minimum legal index key size */
					if (nCell < 2) {
						rc = SQLITE_CORRUPT_BKPT;
						goto moveto_finish;
					}
					pCellKey = sqlite3Malloc(nCell + 18);
					if (pCellKey == 0) {
						rc = SQLITE_NOMEM_BKPT;
						goto moveto_finish;
					}
					pCur->aiIdx[pCur->iPage] = (u16) idx;
					rc = accessPayload(pCur, 0, nCell,
							   (unsigned char *)
							   pCellKey, 2);
					if (rc) {
						sqlite3_free(pCellKey);
						goto moveto_finish;
					}
					c = xRecordCompare(nCell, pCellKey,
							   pIdxKey);
					sqlite3_free(pCellKey);
				}
				assert((pIdxKey->errCode != SQLITE_CORRUPT
					|| c == 0)
				       && (pIdxKey->errCode != SQLITE_NOMEM
					   || pCur->pBtree->db->mallocFailed)
				    );
				if (c < 0) {
					lwr = idx + 1;
				} else if (c > 0) {
					upr = idx - 1;
				} else {
					assert(c == 0);
					*pRes = 0;
					rc = SQLITE_OK;
					pCur->aiIdx[pCur->iPage] = (u16) idx;
					if (pIdxKey->errCode)
						rc = SQLITE_CORRUPT;
					goto moveto_finish;
				}
				if (lwr > upr)
					break;
				assert(lwr + upr >= 0);
				idx = (lwr + upr) >> 1;	/* idx = (lwr+upr)/2 */
			}
		}
		assert(lwr == upr + 1 || (pPage->intKey && !pPage->leaf));
		assert(pPage->isInit);
		if (pPage->leaf) {
			assert(pCur->aiIdx[pCur->iPage] <
			       pCur->apPage[pCur->iPage]->nCell);
			pCur->aiIdx[pCur->iPage] = (u16) idx;
			*pRes = c;
			rc = SQLITE_OK;
			goto moveto_finish;
		}
 moveto_next_layer:
		if (lwr >= pPage->nCell) {
			chldPg = get4byte(&pPage->aData[pPage->hdrOffset + 8]);
		} else {
			chldPg = get4byte(findCell(pPage, lwr));
		}
		pCur->aiIdx[pCur->iPage] = (u16) lwr;
		rc = moveToChild(pCur, chldPg);
		if (rc)
			break;
	}
 moveto_finish:
	pCur->info.nSize = 0;
	assert((pCur->curFlags & BTCF_ValidOvfl) == 0);
	return rc;
}

/*
 * Return TRUE if the cursor is not pointing at an entry of the table.
 *
 * TRUE will be returned after a call to sqlite3BtreeNext() moves
 * past the last entry in the table or sqlite3BtreePrev() moves past
 * the first entry.  TRUE is also returned if the table is empty.
 */
int
sqlite3BtreeEof(BtCursor * pCur)
{
	/* TODO: What if the cursor is in CURSOR_REQUIRESEEK but all table entries
	 * have been deleted? This API will need to change to return an error code
	 * as well as the boolean result value.
	 */
	return (CURSOR_VALID != pCur->eState);
}

/*
 * Advance the cursor to the next entry in the database.  If
 * successful then set *pRes=0.  If the cursor
 * was already pointing to the last entry in the database before
 * this routine was called, then set *pRes=1.
 *
 * The main entry point is sqlite3BtreeNext().  That routine is optimized
 * for the common case of merely incrementing the cell counter BtCursor.aiIdx
 * to the next cell on the current page.  The (slower) btreeNext() helper
 * routine is called when it is necessary to move to a different page or
 * to restore the cursor.
 *
 * The calling function will set *pRes to 0 or 1.  The initial *pRes value
 * will be 1 if the cursor being stepped corresponds to an SQL index and
 * if this routine could have been skipped if that SQL index had been
 * a unique index.  Otherwise the caller will have set *pRes to zero.
 * Zero is the common case. The btree implementation is free to use the
 * initial *pRes value as a hint to improve performance, but the current
 * SQLite btree implementation does not. (Note that the comdb2 btree
 * implementation does use this hint, however.)
 */
static SQLITE_NOINLINE int
btreeNext(BtCursor * pCur, int *pRes)
{
	int rc;
	int idx;
	MemPage *pPage;

	assert(cursorOwnsBtShared(pCur));
	assert(pCur->skipNext == 0 || pCur->eState != CURSOR_VALID);
	assert(*pRes == 0);
	if (pCur->eState != CURSOR_VALID) {
		assert((pCur->curFlags & BTCF_ValidOvfl) == 0);
		rc = restoreCursorPosition(pCur);
		if (rc != SQLITE_OK) {
			return rc;
		}
		if (CURSOR_INVALID == pCur->eState) {
			*pRes = 1;
			return SQLITE_OK;
		}
		if (pCur->skipNext) {
			assert(pCur->eState == CURSOR_VALID
			       || pCur->eState == CURSOR_SKIPNEXT);
			pCur->eState = CURSOR_VALID;
			if (pCur->skipNext > 0) {
				pCur->skipNext = 0;
				return SQLITE_OK;
			}
			pCur->skipNext = 0;
		}
	}

	pPage = pCur->apPage[pCur->iPage];
	idx = ++pCur->aiIdx[pCur->iPage];
	assert(pPage->isInit);

	/* If the database file is corrupt, it is possible for the value of idx
	 * to be invalid here. This can only occur if a second cursor modifies
	 * the page while cursor pCur is holding a reference to it. Which can
	 * only happen if the database is corrupt in such a way as to link the
	 * page into more than one b-tree structure.
	 */
	testcase(idx > pPage->nCell);

	if (idx >= pPage->nCell) {
		if (!pPage->leaf) {
			rc = moveToChild(pCur,
					 get4byte(&pPage->
						  aData[pPage->hdrOffset + 8]));
			if (rc)
				return rc;
			return moveToLeftmost(pCur);
		}
		do {
			if (pCur->iPage == 0) {
				*pRes = 1;
				pCur->eState = CURSOR_INVALID;
				return SQLITE_OK;
			}
			moveToParent(pCur);
			pPage = pCur->apPage[pCur->iPage];
		} while (pCur->aiIdx[pCur->iPage] >= pPage->nCell);
		if (pPage->intKey) {
			return sqlite3BtreeNext(pCur, pRes);
		} else {
			return SQLITE_OK;
		}
	}
	if (pPage->leaf) {
		return SQLITE_OK;
	} else {
		return moveToLeftmost(pCur);
	}
}

int
sqlite3BtreeNext(BtCursor * pCur, int *pRes)
{
	MemPage *pPage;
	assert(cursorOwnsBtShared(pCur));
	assert(pRes != 0);
	assert(*pRes == 0 || *pRes == 1);
	assert(pCur->skipNext == 0 || pCur->eState != CURSOR_VALID);
	pCur->info.nSize = 0;
	pCur->curFlags &= ~(BTCF_ValidNKey | BTCF_ValidOvfl);
	*pRes = 0;
	if (pCur->curFlags & BTCF_TaCursor) {
		if (pCur->eState != CURSOR_VALID) {
			int rc = restoreCursorPosition(pCur);
			if (rc != SQLITE_OK) {
				return rc;
			}
		}
		return tarantoolSqlite3Next(pCur, pRes);
	}
	if (pCur->eState != CURSOR_VALID)
		return btreeNext(pCur, pRes);
	pPage = pCur->apPage[pCur->iPage];
	if ((++pCur->aiIdx[pCur->iPage]) >= pPage->nCell) {
		pCur->aiIdx[pCur->iPage]--;
		return btreeNext(pCur, pRes);
	}
	if (pPage->leaf) {
		return SQLITE_OK;
	} else {
		return moveToLeftmost(pCur);
	}
}

/*
 * Step the cursor to the back to the previous entry in the database.  If
 * successful then set *pRes=0.  If the cursor
 * was already pointing to the first entry in the database before
 * this routine was called, then set *pRes=1.
 *
 * The main entry point is sqlite3BtreePrevious().  That routine is optimized
 * for the common case of merely decrementing the cell counter BtCursor.aiIdx
 * to the previous cell on the current page.  The (slower) btreePrevious()
 * helper routine is called when it is necessary to move to a different page
 * or to restore the cursor.
 *
 * The calling function will set *pRes to 0 or 1.  The initial *pRes value
 * will be 1 if the cursor being stepped corresponds to an SQL index and
 * if this routine could have been skipped if that SQL index had been
 * a unique index.  Otherwise the caller will have set *pRes to zero.
 * Zero is the common case. The btree implementation is free to use the
 * initial *pRes value as a hint to improve performance, but the current
 * SQLite btree implementation does not. (Note that the comdb2 btree
 * implementation does use this hint, however.)
 */
static SQLITE_NOINLINE int
btreePrevious(BtCursor * pCur, int *pRes)
{
	int rc;
	MemPage *pPage;

	assert(cursorOwnsBtShared(pCur));
	assert(pRes != 0);
	assert(*pRes == 0);
	assert(pCur->skipNext == 0 || pCur->eState != CURSOR_VALID);
	assert((pCur->
		curFlags & (BTCF_AtLast | BTCF_ValidOvfl | BTCF_ValidNKey)) ==
	       0);
	assert(pCur->info.nSize == 0);
	if (pCur->eState != CURSOR_VALID) {
		rc = restoreCursorPosition(pCur);
		if (rc != SQLITE_OK) {
			return rc;
		}
		if (CURSOR_INVALID == pCur->eState) {
			*pRes = 1;
			return SQLITE_OK;
		}
		if (pCur->skipNext) {
			assert(pCur->eState == CURSOR_VALID
			       || pCur->eState == CURSOR_SKIPNEXT);
			pCur->eState = CURSOR_VALID;
			if (pCur->skipNext < 0) {
				pCur->skipNext = 0;
				return SQLITE_OK;
			}
			pCur->skipNext = 0;
		}
	}

	pPage = pCur->apPage[pCur->iPage];
	assert(pPage->isInit);
	if (!pPage->leaf) {
		int idx = pCur->aiIdx[pCur->iPage];
		rc = moveToChild(pCur, get4byte(findCell(pPage, idx)));
		if (rc)
			return rc;
		rc = moveToRightmost(pCur);
	} else {
		while (pCur->aiIdx[pCur->iPage] == 0) {
			if (pCur->iPage == 0) {
				pCur->eState = CURSOR_INVALID;
				*pRes = 1;
				return SQLITE_OK;
			}
			moveToParent(pCur);
		}
		assert(pCur->info.nSize == 0);
		assert((pCur->curFlags & (BTCF_ValidOvfl)) == 0);

		pCur->aiIdx[pCur->iPage]--;
		pPage = pCur->apPage[pCur->iPage];
		if (pPage->intKey && !pPage->leaf) {
			rc = sqlite3BtreePrevious(pCur, pRes);
		} else {
			rc = SQLITE_OK;
		}
	}
	return rc;
}

int
sqlite3BtreePrevious(BtCursor * pCur, int *pRes)
{
	assert(cursorOwnsBtShared(pCur));
	assert(pRes != 0);
	assert(*pRes == 0 || *pRes == 1);
	assert(pCur->skipNext == 0 || pCur->eState != CURSOR_VALID);
	*pRes = 0;
	pCur->curFlags &= ~(BTCF_AtLast | BTCF_ValidOvfl | BTCF_ValidNKey);
	pCur->info.nSize = 0;
	if (pCur->curFlags & BTCF_TaCursor) {
		return tarantoolSqlite3Previous(pCur, pRes);
	}
	if (pCur->eState != CURSOR_VALID
	    || pCur->aiIdx[pCur->iPage] == 0
	    || pCur->apPage[pCur->iPage]->leaf == 0) {
		return btreePrevious(pCur, pRes);
	}
	pCur->aiIdx[pCur->iPage]--;
	return SQLITE_OK;
}

/*
 * Allocate a new page from the database file.
 *
 * The new page is marked as dirty.  (In other words, sqlite3PagerWrite()
 * has already been called on the new page.)  The new page has also
 * been referenced and the calling routine is responsible for calling
 * sqlite3PagerUnref() on the new page when it is done.
 *
 * SQLITE_OK is returned on success.  Any other return value indicates
 * an error.  *ppPage is set to NULL in the event of an error.
 *
 * If the "nearby" parameter is not 0, then an effort is made to
 * locate a page close to the page number "nearby".  This can be used in an
 * attempt to keep related pages close to each other in the database file,
 * which in turn can make database access faster.
 *
 * If the eMode parameter is BTALLOC_EXACT and the nearby page exists
 * anywhere on the free-list, then it is guaranteed to be returned.  If
 * eMode is BTALLOC_LT then the page returned will be less than or equal
 * to nearby if any such page exists.  If eMode is BTALLOC_ANY then there
 * are no restrictions on which page is returned.
 */
static int
allocateBtreePage(BtShared * pBt,	/* The btree */
		  MemPage ** ppPage,	/* Store pointer to the allocated page here */
		  Pgno * pPgno,	/* Store the page number here */
		  Pgno nearby,	/* Search for a page near this one */
		  u8 eMode	/* BTALLOC_EXACT, BTALLOC_LT, or BTALLOC_ANY */
    )
{
	MemPage *pPage1;
	int rc;
	u32 n;			/* Number of pages on the freelist */
	u32 k;			/* Number of leaves on the trunk of the freelist */
	MemPage *pTrunk = 0;
	MemPage *pPrevTrunk = 0;
	Pgno mxPage;		/* Total size of the database file */

	assert(sqlite3_mutex_held(pBt->mutex));
	assert(eMode == BTALLOC_ANY);
	pPage1 = pBt->pPage1;
	mxPage = btreePagecount(pBt);
	/* EVIDENCE-OF: R-05119-02637 The 4-byte big-endian integer at offset 36
	 * stores stores the total number of pages on the freelist.
	 */
	n = get4byte(&pPage1->aData[36]);
	testcase(n == mxPage - 1);
	if (n >= mxPage) {
		return SQLITE_CORRUPT_BKPT;
	}
	if (n > 0) {
		/* There are pages on the freelist.  Reuse one of those pages. */
		Pgno iTrunk;
		u8 searchList = 0;	/* If the free-list must be searched for 'nearby' */
		u32 nSearch = 0;	/* Count of the number of search attempts */

		/* If eMode==BTALLOC_EXACT and a query of the pointer-map
		 * shows that the page 'nearby' is somewhere on the free-list, then
		 * the entire-list will be searched for that page.
		 */

		/* Decrement the free-list count by 1. Set iTrunk to the index of the
		 * first free-list trunk page. iPrevTrunk is initially 1.
		 */
		rc = sqlite3PagerWrite(pPage1->pDbPage);
		if (rc)
			return rc;
		put4byte(&pPage1->aData[36], n - 1);

		/* The code within this loop is run only once if the 'searchList' variable
		 * is not true. Otherwise, it runs once for each trunk-page on the
		 * free-list until the page 'nearby' is located (eMode==BTALLOC_EXACT)
		 * or until a page less than 'nearby' is located (eMode==BTALLOC_LT)
		 */
		do {
			pPrevTrunk = pTrunk;
			if (pPrevTrunk) {
				/* EVIDENCE-OF: R-01506-11053 The first integer on a freelist trunk page
				 * is the page number of the next freelist trunk page in the list or
				 * zero if this is the last freelist trunk page.
				 */
				iTrunk = get4byte(&pPrevTrunk->aData[0]);
			} else {
				/* EVIDENCE-OF: R-59841-13798 The 4-byte big-endian integer at offset 32
				 * stores the page number of the first page of the freelist, or zero if
				 * the freelist is empty.
				 */
				iTrunk = get4byte(&pPage1->aData[32]);
			}
			testcase(iTrunk == mxPage);
			if (iTrunk > mxPage || nSearch++ > n) {
				rc = SQLITE_CORRUPT_BKPT;
			} else {
				rc = btreeGetUnusedPage(pBt, iTrunk, &pTrunk,
							0);
			}
			if (rc) {
				pTrunk = 0;
				goto end_allocate_page;
			}
			assert(pTrunk != 0);
			assert(pTrunk->aData != 0);
			/* EVIDENCE-OF: R-13523-04394 The second integer on a freelist trunk page
			 * is the number of leaf page pointers to follow.
			 */
			k = get4byte(&pTrunk->aData[4]);
			if (k == 0 && !searchList) {
				/* The trunk has no leaves and the list is not being searched.
				 * So extract the trunk page itself and use it as the newly
				 * allocated page
				 */
				assert(pPrevTrunk == 0);
				rc = sqlite3PagerWrite(pTrunk->pDbPage);
				if (rc) {
					goto end_allocate_page;
				}
				*pPgno = iTrunk;
				memcpy(&pPage1->aData[32], &pTrunk->aData[0],
				       4);
				*ppPage = pTrunk;
				pTrunk = 0;
				TRACE(("ALLOCATE: %d trunk - %d free pages left\n", *pPgno, n - 1));
			} else if (k > (u32) (pBt->usableSize / 4 - 2)) {
				/* Value of k is out of range.  Database corruption */
				rc = SQLITE_CORRUPT_BKPT;
				goto end_allocate_page;
			} else if (k > 0) {
				/* Extract a leaf from the trunk */
				u32 closest;
				Pgno iPage;
				unsigned char *aData = pTrunk->aData;
				if (nearby > 0) {
					u32 i;
					closest = 0;
					if (eMode == BTALLOC_LE) {
						for (i = 0; i < k; i++) {
							iPage =
							    get4byte(&aData
								     [8 +
								      i * 4]);
							if (iPage <= nearby) {
								closest = i;
								break;
							}
						}
					} else {
						int dist;
						dist =
						    sqlite3AbsInt32(get4byte
								    (&aData[8])
								    - nearby);
						for (i = 1; i < k; i++) {
							int d2 =
							    sqlite3AbsInt32
							    (get4byte
							     (&aData[8 + i * 4])
							     - nearby);
							if (d2 < dist) {
								closest = i;
								dist = d2;
							}
						}
					}
				} else {
					closest = 0;
				}

				iPage = get4byte(&aData[8 + closest * 4]);
				testcase(iPage == mxPage);
				if (iPage > mxPage) {
					rc = SQLITE_CORRUPT_BKPT;
					goto end_allocate_page;
				}
				testcase(iPage == mxPage);
				if (!searchList
				    || (iPage == nearby
					|| (iPage < nearby
					    && eMode == BTALLOC_LE))
				    ) {
					int noContent;
					*pPgno = iPage;
					TRACE(("ALLOCATE: %d was leaf %d of %d on trunk %d" ": %d more free pages\n", *pPgno, closest + 1, k, pTrunk->pgno, n - 1));
					rc = sqlite3PagerWrite(pTrunk->pDbPage);
					if (rc)
						goto end_allocate_page;
					if (closest < k - 1) {
						memcpy(&aData[8 + closest * 4],
						       &aData[4 + k * 4], 4);
					}
					put4byte(&aData[4], k - 1);
					noContent =
					    !btreeGetHasContent(pBt,
								*pPgno) ?
					    PAGER_GET_NOCONTENT : 0;
					rc = btreeGetUnusedPage(pBt, *pPgno,
								ppPage,
								noContent);
					if (rc == SQLITE_OK) {
						rc = sqlite3PagerWrite((*ppPage)->pDbPage);
						if (rc != SQLITE_OK) {
							releasePage(*ppPage);
							*ppPage = 0;
						}
					}
					searchList = 0;
				}
			}
			releasePage(pPrevTrunk);
			pPrevTrunk = 0;
		} while (searchList);
	} else {
		/* There are no pages on the freelist, so append a new page to the
		 * database image.
		 *
		 * Normally, new pages allocated by this block can be requested from the
		 * pager layer with the 'no-content' flag set. This prevents the pager
		 * from trying to read the pages content from disk.
		 * Note that the pager will not actually attempt to load or journal
		 * content for any page that really does lie past the end of the database
		 * file on disk. So the effects of disabling the no-content optimization
		 * here are confined to those pages that lie between the end of the
		 * database image and the end of the database file.
		 */
		int bNoContent = PAGER_GET_NOCONTENT;

		rc = sqlite3PagerWrite(pBt->pPage1->pDbPage);
		if (rc)
			return rc;
		pBt->nPage++;
		if (pBt->nPage == PENDING_BYTE_PAGE(pBt))
			pBt->nPage++;

		put4byte(28 + (u8 *) pBt->pPage1->aData, pBt->nPage);
		*pPgno = pBt->nPage;

		assert(*pPgno != PENDING_BYTE_PAGE(pBt));
		rc = btreeGetUnusedPage(pBt, *pPgno, ppPage, bNoContent);
		if (rc)
			return rc;
		rc = sqlite3PagerWrite((*ppPage)->pDbPage);
		if (rc != SQLITE_OK) {
			releasePage(*ppPage);
			*ppPage = 0;
		}
		TRACE(("ALLOCATE: %d from end of file\n", *pPgno));
	}

	assert(*pPgno != PENDING_BYTE_PAGE(pBt));

 end_allocate_page:
	releasePage(pTrunk);
	releasePage(pPrevTrunk);
	assert(rc != SQLITE_OK
	       || sqlite3PagerPageRefcount((*ppPage)->pDbPage) <= 1);
	assert(rc != SQLITE_OK || (*ppPage)->isInit == 0);
	return rc;
}

/*
 * This function is used to add page iPage to the database file free-list.
 * It is assumed that the page is not already a part of the free-list.
 *
 * The value passed as the second argument to this function is optional.
 * If the caller happens to have a pointer to the MemPage object
 * corresponding to page iPage handy, it may pass it as the second value.
 * Otherwise, it may pass NULL.
 *
 * If a pointer to a MemPage object is passed as the second argument,
 * its reference count is not altered by this function.
 */
static int
freePage2(BtShared * pBt, MemPage * pMemPage, Pgno iPage)
{
	MemPage *pTrunk = 0;	/* Free-list trunk page */
	Pgno iTrunk = 0;	/* Page number of free-list trunk page */
	MemPage *pPage1 = pBt->pPage1;	/* Local reference to page 1 */
	MemPage *pPage;		/* Page being freed. May be NULL. */
	int rc;			/* Return Code */
	int nFree;		/* Initial number of pages on free-list */

	assert(sqlite3_mutex_held(pBt->mutex));
	assert(CORRUPT_DB || iPage > 1);
	assert(!pMemPage || pMemPage->pgno == iPage);

	if (iPage < 2)
		return SQLITE_CORRUPT_BKPT;
	if (pMemPage) {
		pPage = pMemPage;
		sqlite3PagerRef(pPage->pDbPage);
	} else {
		pPage = btreePageLookup(pBt, iPage);
	}

	/* Increment the free page count on pPage1 */
	rc = sqlite3PagerWrite(pPage1->pDbPage);
	if (rc)
		goto freepage_out;
	nFree = get4byte(&pPage1->aData[36]);
	put4byte(&pPage1->aData[36], nFree + 1);

	if (pBt->btsFlags & BTS_SECURE_DELETE) {
		/* If the secure_delete option is enabled, then
		 * always fully overwrite deleted information with zeros.
		 */
		if ((!pPage
		     && ((rc = btreeGetPage(pBt, iPage, &pPage, 0)) != 0))
		    || ((rc = sqlite3PagerWrite(pPage->pDbPage)) != 0)
		    ) {
			goto freepage_out;
		}
		memset(pPage->aData, 0, pPage->pBt->pageSize);
	}

	/* Now manipulate the actual database free-list structure. There are two
	 * possibilities. If the free-list is currently empty, or if the first
	 * trunk page in the free-list is full, then this page will become a
	 * new free-list trunk page. Otherwise, it will become a leaf of the
	 * first trunk page in the current free-list. This block tests if it
	 * is possible to add the page as a new free-list leaf.
	 */
	if (nFree != 0) {
		u32 nLeaf;	/* Initial number of leaf cells on trunk page */

		iTrunk = get4byte(&pPage1->aData[32]);
		rc = btreeGetPage(pBt, iTrunk, &pTrunk, 0);
		if (rc != SQLITE_OK) {
			goto freepage_out;
		}

		nLeaf = get4byte(&pTrunk->aData[4]);
		assert(pBt->usableSize > 32);
		if (nLeaf > (u32) pBt->usableSize / 4 - 2) {
			rc = SQLITE_CORRUPT_BKPT;
			goto freepage_out;
		}
		if (nLeaf < (u32) pBt->usableSize / 4 - 8) {
			/* In this case there is room on the trunk page to insert the page
			 * being freed as a new leaf.
			 *
			 * Note that the trunk page is not really full until it contains
			 * usableSize/4 - 2 entries, not usableSize/4 - 8 entries as we have
			 * coded.  But due to a coding error in versions of SQLite prior to
			 * 3.6.0, databases with freelist trunk pages holding more than
			 * usableSize/4 - 8 entries will be reported as corrupt.  In order
			 * to maintain backwards compatibility with older versions of SQLite,
			 * we will continue to restrict the number of entries to usableSize/4 - 8
			 * for now.  At some point in the future (once everyone has upgraded
			 * to 3.6.0 or later) we should consider fixing the conditional above
			 * to read "usableSize/4-2" instead of "usableSize/4-8".
			 *
			 * EVIDENCE-OF: R-19920-11576 However, newer versions of SQLite still
			 * avoid using the last six entries in the freelist trunk page array in
			 * order that database files created by newer versions of SQLite can be
			 * read by older versions of SQLite.
			 */
			rc = sqlite3PagerWrite(pTrunk->pDbPage);
			if (rc == SQLITE_OK) {
				put4byte(&pTrunk->aData[4], nLeaf + 1);
				put4byte(&pTrunk->aData[8 + nLeaf * 4], iPage);
				if (pPage
				    && (pBt->btsFlags & BTS_SECURE_DELETE) ==
				    0) {
					sqlite3PagerDontWrite(pPage->pDbPage);
				}
				rc = btreeSetHasContent(pBt, iPage);
			}
			TRACE(("FREE-PAGE: %d leaf on trunk page %d\n",
			       pPage->pgno, pTrunk->pgno));
			goto freepage_out;
		}
	}

	/* If control flows to this point, then it was not possible to add the
	 * the page being freed as a leaf page of the first trunk in the free-list.
	 * Possibly because the free-list is empty, or possibly because the
	 * first trunk in the free-list is full. Either way, the page being freed
	 * will become the new first trunk page in the free-list.
	 */
	if (pPage == 0
	    && SQLITE_OK != (rc = btreeGetPage(pBt, iPage, &pPage, 0))) {
		goto freepage_out;
	}
	rc = sqlite3PagerWrite(pPage->pDbPage);
	if (rc != SQLITE_OK) {
		goto freepage_out;
	}
	put4byte(pPage->aData, iTrunk);
	put4byte(&pPage->aData[4], 0);
	put4byte(&pPage1->aData[32], iPage);
	TRACE(("FREE-PAGE: %d new trunk page replacing %d\n", pPage->pgno,
	       iTrunk));

 freepage_out:
	if (pPage) {
		pPage->isInit = 0;
	}
	releasePage(pPage);
	releasePage(pTrunk);
	return rc;
}

static void
freePage(MemPage * pPage, int *pRC)
{
	if ((*pRC) == SQLITE_OK) {
		*pRC = freePage2(pPage->pBt, pPage, pPage->pgno);
	}
}

/*
 * Free any overflow pages associated with the given Cell.  Write the
 * local Cell size (the number of bytes on the original page, omitting
 * overflow) into *pnSize.
 */
static int
clearCell(MemPage * pPage,	/* The page that contains the Cell */
	  unsigned char *pCell,	/* First byte of the Cell */
	  CellInfo * pInfo	/* Size information about the cell */
    )
{
	BtShared *pBt = pPage->pBt;
	Pgno ovflPgno;
	int rc;
	int nOvfl;
	u32 ovflPageSize;

	assert(sqlite3_mutex_held(pPage->pBt->mutex));
	pPage->xParseCell(pPage, pCell, pInfo);
	if (pInfo->nLocal == pInfo->nPayload) {
		return SQLITE_OK;	/* No overflow pages. Return without doing anything */
	}
	if (pCell + pInfo->nSize - 1 > pPage->aData + pPage->maskPage) {
		return SQLITE_CORRUPT_BKPT;	/* Cell extends past end of page */
	}
	ovflPgno = get4byte(pCell + pInfo->nSize - 4);
	assert(pBt->usableSize > 4);
	ovflPageSize = pBt->usableSize - 4;
	nOvfl =
	    (pInfo->nPayload - pInfo->nLocal + ovflPageSize - 1) / ovflPageSize;
	assert(nOvfl > 0
	       || (CORRUPT_DB
		   && (pInfo->nPayload + ovflPageSize) < ovflPageSize)
	    );
	while (nOvfl--) {
		Pgno iNext = 0;
		MemPage *pOvfl = 0;
		if (ovflPgno < 2 || ovflPgno > btreePagecount(pBt)) {
			/* 0 is not a legal page number and page 1 cannot be an
			 * overflow page. Therefore if ovflPgno<2 or past the end of the
			 * file the database must be corrupt.
			 */
			return SQLITE_CORRUPT_BKPT;
		}
		if (nOvfl) {
			rc = getOverflowPage(pBt, ovflPgno, &pOvfl, &iNext);
			if (rc)
				return rc;
		}

		if ((pOvfl || ((pOvfl = btreePageLookup(pBt, ovflPgno)) != 0))
		    && sqlite3PagerPageRefcount(pOvfl->pDbPage) != 1) {
			/* There is no reason any cursor should have an outstanding reference
			 * to an overflow page belonging to a cell that is being deleted/updated.
			 * So if there exists more than one reference to this page, then it
			 * must not really be an overflow page and the database must be corrupt.
			 * It is helpful to detect this before calling freePage2(), as
			 * freePage2() may zero the page contents if secure-delete mode is
			 * enabled. If this 'overflow' page happens to be a page that the
			 * caller is iterating through or using in some other way, this
			 * can be problematic.
			 */
			rc = SQLITE_CORRUPT_BKPT;
		} else {
			rc = freePage2(pBt, pOvfl, ovflPgno);
		}

		if (pOvfl) {
			sqlite3PagerUnref(pOvfl->pDbPage);
		}
		if (rc)
			return rc;
		ovflPgno = iNext;
	}
	return SQLITE_OK;
}

/*
 * Create the byte sequence used to represent a cell on page pPage
 * and write that byte sequence into pCell[].  Overflow pages are
 * allocated and filled in as necessary.  The calling procedure
 * is responsible for making sure sufficient space has been allocated
 * for pCell[].
 *
 * Note that pCell does not necessary need to point to the pPage->aData
 * area.  pCell might point to some temporary storage.  The cell will
 * be constructed in this temporary area then copied into pPage->aData
 * later.
 */
static int
fillInCell(MemPage * pPage,	/* The page that contains the cell */
	   unsigned char *pCell,	/* Complete text of the cell */
	   const BtreePayload * pX,	/* Payload with which to construct the cell */
	   int *pnSize		/* Write cell size here */
    )
{
	int nPayload;
	const u8 *pSrc;
	int nSrc, n, rc;
	int spaceLeft;
	MemPage *pOvfl = 0;
	MemPage *pToRelease = 0;
	unsigned char *pPrior;
	unsigned char *pPayload;
	BtShared *pBt = pPage->pBt;
	Pgno pgnoOvfl = 0;
	int nHeader;

	assert(sqlite3_mutex_held(pPage->pBt->mutex));

	/* pPage is not necessarily writeable since pCell might be auxiliary
	 * buffer space that is separate from the pPage buffer area
	 */
	assert(pCell < pPage->aData || pCell >= &pPage->aData[pBt->pageSize]
	       || sqlite3PagerIswriteable(pPage->pDbPage));

	/* Fill in the header. */
	nHeader = pPage->childPtrSize;
	if (pPage->intKey) {
		nPayload = pX->nData + pX->nZero;
		pSrc = pX->pData;
		nSrc = pX->nData;
		assert(pPage->intKeyLeaf);	/* fillInCell() only called for leaves */
		nHeader += putVarint32(&pCell[nHeader], nPayload);
		nHeader += putVarint(&pCell[nHeader], *(u64 *) & pX->nKey);
	} else {
		assert(pX->nKey <= 0x7fffffff && pX->pKey != 0);
		nSrc = nPayload = (int)pX->nKey;
		pSrc = pX->pKey;
		nHeader += putVarint32(&pCell[nHeader], nPayload);
	}

	/* Fill in the payload */
	if (nPayload <= pPage->maxLocal) {
		n = nHeader + nPayload;
		testcase(n == 3);
		testcase(n == 4);
		if (n < 4)
			n = 4;
		*pnSize = n;
		spaceLeft = nPayload;
		pPrior = pCell;
	} else {
		int mn = pPage->minLocal;
		n = mn + (nPayload - mn) % (pPage->pBt->usableSize - 4);
		testcase(n == pPage->maxLocal);
		testcase(n == pPage->maxLocal + 1);
		if (n > pPage->maxLocal)
			n = mn;
		spaceLeft = n;
		*pnSize = n + nHeader + 4;
		pPrior = &pCell[nHeader + n];
	}
	pPayload = &pCell[nHeader];

	/* At this point variables should be set as follows:
	 *
	 *   nPayload           Total payload size in bytes
	 *   pPayload           Begin writing payload here
	 *   spaceLeft          Space available at pPayload.  If nPayload>spaceLeft,
	 *                      that means content must spill into overflow pages.
	 *   *pnSize            Size of the local cell (not counting overflow pages)
	 *   pPrior             Where to write the pgno of the first overflow page
	 *
	 * Use a call to btreeParseCellPtr() to verify that the values above
	 * were computed correctly.
	 */
#if SQLITE_DEBUG
	{
		CellInfo info;
		pPage->xParseCell(pPage, pCell, &info);
		assert(nHeader == (int)(info.pPayload - pCell));
		assert(info.nKey == pX->nKey);
		assert(*pnSize == info.nSize);
		assert(spaceLeft == info.nLocal);
	}
#endif

	/* Write the payload into the local Cell and any extra into overflow pages */
	while (nPayload > 0) {
		if (spaceLeft == 0) {
			rc = allocateBtreePage(pBt, &pOvfl, &pgnoOvfl, pgnoOvfl,
					       0);
			if (rc) {
				releasePage(pToRelease);
				return rc;
			}

			/* If pToRelease is not zero than pPrior points into the data area
			 * of pToRelease.  Make sure pToRelease is still writeable.
			 */
			assert(pToRelease == 0
			       || sqlite3PagerIswriteable(pToRelease->pDbPage));

			/* If pPrior is part of the data area of pPage, then make sure pPage
			 * is still writeable
			 */
			assert(pPrior < pPage->aData
			       || pPrior >= &pPage->aData[pBt->pageSize]
			       || sqlite3PagerIswriteable(pPage->pDbPage));

			put4byte(pPrior, pgnoOvfl);
			releasePage(pToRelease);
			pToRelease = pOvfl;
			pPrior = pOvfl->aData;
			put4byte(pPrior, 0);
			pPayload = &pOvfl->aData[4];
			spaceLeft = pBt->usableSize - 4;
		}
		n = nPayload;
		if (n > spaceLeft)
			n = spaceLeft;

		/* If pToRelease is not zero than pPayload points into the data area
		 * of pToRelease.  Make sure pToRelease is still writeable.
		 */
		assert(pToRelease == 0
		       || sqlite3PagerIswriteable(pToRelease->pDbPage));

		/* If pPayload is part of the data area of pPage, then make sure pPage
		 * is still writeable
		 */
		assert(pPayload < pPage->aData
		       || pPayload >= &pPage->aData[pBt->pageSize]
		       || sqlite3PagerIswriteable(pPage->pDbPage));

		if (nSrc > 0) {
			if (n > nSrc)
				n = nSrc;
			assert(pSrc);
			memcpy(pPayload, pSrc, n);
		} else {
			memset(pPayload, 0, n);
		}
		nPayload -= n;
		pPayload += n;
		pSrc += n;
		nSrc -= n;
		spaceLeft -= n;
	}
	releasePage(pToRelease);
	return SQLITE_OK;
}

/*
 * Remove the i-th cell from pPage.  This routine effects pPage only.
 * The cell content is not freed or deallocated.  It is assumed that
 * the cell content has been copied someplace else.  This routine just
 * removes the reference to the cell from pPage.
 *
 * "sz" must be the number of bytes in the cell.
 */
static void
dropCell(MemPage * pPage, int idx, int sz, int *pRC)
{
	u32 pc;			/* Offset to cell content of cell being deleted */
	u8 *data;		/* pPage->aData */
	u8 *ptr;		/* Used to move bytes around within data[] */
	int rc;			/* The return code */
	int hdr;		/* Beginning of the header.  0 most pages.  100 page 1 */

	if (*pRC)
		return;
	assert(idx >= 0 && idx < pPage->nCell);
	assert(CORRUPT_DB || sz == cellSize(pPage, idx));
	assert(sqlite3PagerIswriteable(pPage->pDbPage));
	assert(sqlite3_mutex_held(pPage->pBt->mutex));
	data = pPage->aData;
	ptr = &pPage->aCellIdx[2 * idx];
	pc = get2byte(ptr);
	hdr = pPage->hdrOffset;
	testcase(pc == get2byte(&data[hdr + 5]));
	testcase(pc + sz == pPage->pBt->usableSize);
	if (pc < (u32) get2byte(&data[hdr + 5])
	    || pc + sz > pPage->pBt->usableSize) {
		*pRC = SQLITE_CORRUPT_BKPT;
		return;
	}
	rc = freeSpace(pPage, pc, sz);
	if (rc) {
		*pRC = rc;
		return;
	}
	pPage->nCell--;
	if (pPage->nCell == 0) {
		memset(&data[hdr + 1], 0, 4);
		data[hdr + 7] = 0;
		put2byte(&data[hdr + 5], pPage->pBt->usableSize);
		pPage->nFree = pPage->pBt->usableSize - pPage->hdrOffset
		    - pPage->childPtrSize - 8;
	} else {
		memmove(ptr, ptr + 2, 2 * (pPage->nCell - idx));
		put2byte(&data[hdr + 3], pPage->nCell);
		pPage->nFree += 2;
	}
}

/*
 * Insert a new cell on pPage at cell index "i".  pCell points to the
 * content of the cell.
 *
 * If the cell content will fit on the page, then put it there.  If it
 * will not fit, then make a copy of the cell content into pTemp if
 * pTemp is not null.  Regardless of pTemp, allocate a new entry
 * in pPage->apOvfl[] and make it point to the cell content (either
 * in pTemp or the original pCell) and also record its index.
 * Allocating a new entry in pPage->aCell[] implies that
 * pPage->nOverflow is incremented.
 *
 * *pRC must be SQLITE_OK when this routine is called.
 */
static void
insertCell(MemPage * pPage,	/* Page into which we are copying */
	   int i,		/* New cell becomes the i-th cell of the page */
	   u8 * pCell,		/* Content of the new cell */
	   int sz,		/* Bytes of content in pCell */
	   u8 * pTemp,		/* Temp storage space for pCell, if needed */
	   Pgno iChild,		/* If non-zero, replace first 4 bytes with this value */
	   int *pRC		/* Read and write return code from here */
    )
{
	int idx = 0;		/* Where to write new cell content in data[] */
	int j;			/* Loop counter */
	u8 *data;		/* The content of the whole page */
	u8 *pIns;		/* The point in pPage->aCellIdx[] where no cell inserted */

	assert(*pRC == SQLITE_OK);
	assert(i >= 0 && i <= pPage->nCell + pPage->nOverflow);
	assert(MX_CELL(pPage->pBt) <= 10921);
	assert(pPage->nCell <= MX_CELL(pPage->pBt) || CORRUPT_DB);
	assert(pPage->nOverflow <= ArraySize(pPage->apOvfl));
	assert(ArraySize(pPage->apOvfl) == ArraySize(pPage->aiOvfl));
	assert(sqlite3_mutex_held(pPage->pBt->mutex));
	/* The cell should normally be sized correctly.  However, when moving a
	 * malformed cell from a leaf page to an interior page, if the cell size
	 * wanted to be less than 4 but got rounded up to 4 on the leaf, then size
	 * might be less than 8 (leaf-size + pointer) on the interior node.  Hence
	 * the term after the || in the following assert().
	 */
	assert(sz == pPage->xCellSize(pPage, pCell) || (sz == 8 && iChild > 0));
	if (pPage->nOverflow || sz + 2 > pPage->nFree) {
		if (pTemp) {
			memcpy(pTemp, pCell, sz);
			pCell = pTemp;
		}
		if (iChild) {
			put4byte(pCell, iChild);
		}
		j = pPage->nOverflow++;
		/* Comparison against ArraySize-1 since we hold back one extra slot
		 * as a contingency.  In other words, never need more than 3 overflow
		 * slots but 4 are allocated, just to be safe.
		 */
		assert(j < ArraySize(pPage->apOvfl) - 1);
		pPage->apOvfl[j] = pCell;
		pPage->aiOvfl[j] = (u16) i;

		/* When multiple overflows occur, they are always sequential and in
		 * sorted order.  This invariants arise because multiple overflows can
		 * only occur when inserting divider cells into the parent page during
		 * balancing, and the dividers are adjacent and sorted.
		 */
		assert(j == 0 || pPage->aiOvfl[j - 1] < (u16) i);	/* Overflows in sorted order */
		assert(j == 0 || i == pPage->aiOvfl[j - 1] + 1);	/* Overflows are sequential */
	} else {
		int rc = sqlite3PagerWrite(pPage->pDbPage);
		if (rc != SQLITE_OK) {
			*pRC = rc;
			return;
		}
		assert(sqlite3PagerIswriteable(pPage->pDbPage));
		data = pPage->aData;
		assert(&data[pPage->cellOffset] == pPage->aCellIdx);
		rc = allocateSpace(pPage, sz, &idx);
		if (rc) {
			*pRC = rc;
			return;
		}
		/* The allocateSpace() routine guarantees the following properties
		 * if it returns successfully
		 */
		assert(idx >= 0);
		assert(idx >= pPage->cellOffset + 2 * pPage->nCell + 2
		       || CORRUPT_DB);
		assert(idx + sz <= (int)pPage->pBt->usableSize);
		pPage->nFree -= (u16) (2 + sz);
		memcpy(&data[idx], pCell, sz);
		if (iChild) {
			put4byte(&data[idx], iChild);
		}
		pIns = pPage->aCellIdx + i * 2;
		memmove(pIns + 2, pIns, 2 * (pPage->nCell - i));
		put2byte(pIns, idx);
		pPage->nCell++;
		/* increment the cell count */
		if ((++data[pPage->hdrOffset + 4]) == 0)
			data[pPage->hdrOffset + 3]++;
		assert(get2byte(&data[pPage->hdrOffset + 3]) == pPage->nCell);
	}
}

/*
 * A CellArray object contains a cache of pointers and sizes for a
 * consecutive sequence of cells that might be held on multiple pages.
 */
typedef struct CellArray CellArray;
struct CellArray {
	int nCell;		/* Number of cells in apCell[] */
	MemPage *pRef;		/* Reference page */
	u8 **apCell;		/* All cells begin balanced */
	u16 *szCell;		/* Local size of all cells in apCell[] */
};

/*
 * Make sure the cell sizes at idx, idx+1, ..., idx+N-1 have been
 * computed.
 */
static void
populateCellCache(CellArray * p, int idx, int N)
{
	assert(idx >= 0 && idx + N <= p->nCell);
	while (N > 0) {
		assert(p->apCell[idx] != 0);
		if (p->szCell[idx] == 0) {
			p->szCell[idx] =
			    p->pRef->xCellSize(p->pRef, p->apCell[idx]);
		} else {
			assert(CORRUPT_DB ||
			       p->szCell[idx] == p->pRef->xCellSize(p->pRef,
								    p->
								    apCell
								    [idx]));
		}
		idx++;
		N--;
	}
}

/*
 * Return the size of the Nth element of the cell array
 */
static SQLITE_NOINLINE u16
computeCellSize(CellArray * p, int N)
{
	assert(N >= 0 && N < p->nCell);
	assert(p->szCell[N] == 0);
	p->szCell[N] = p->pRef->xCellSize(p->pRef, p->apCell[N]);
	return p->szCell[N];
}

static u16
cachedCellSize(CellArray * p, int N)
{
	assert(N >= 0 && N < p->nCell);
	if (p->szCell[N])
		return p->szCell[N];
	return computeCellSize(p, N);
}

/*
 * Array apCell[] contains pointers to nCell b-tree page cells. The
 * szCell[] array contains the size in bytes of each cell. This function
 * replaces the current contents of page pPg with the contents of the cell
 * array.
 *
 * Some of the cells in apCell[] may currently be stored in pPg. This
 * function works around problems caused by this by making a copy of any
 * such cells before overwriting the page data.
 *
 * The MemPage.nFree field is invalidated by this function. It is the
 * responsibility of the caller to set it correctly.
 */
static int
rebuildPage(MemPage * pPg,	/* Edit this page */
	    int nCell,		/* Final number of cells on page */
	    u8 ** apCell,	/* Array of cells */
	    u16 * szCell	/* Array of cell sizes */
    )
{
	const int hdr = pPg->hdrOffset;	/* Offset of header on pPg */
	u8 *const aData = pPg->aData;	/* Pointer to data for pPg */
	const int usableSize = pPg->pBt->usableSize;
	u8 *const pEnd = &aData[usableSize];
	int i;
	u8 *pCellptr = pPg->aCellIdx;
	u8 *pTmp = sqlite3PagerTempSpace(pPg->pBt->pPager);
	u8 *pData;

	i = get2byte(&aData[hdr + 5]);
	memcpy(&pTmp[i], &aData[i], usableSize - i);

	pData = pEnd;
	for (i = 0; i < nCell; i++) {
		u8 *pCell = apCell[i];
		if (SQLITE_WITHIN(pCell, aData, pEnd)) {
			pCell = &pTmp[pCell - aData];
		}
		pData -= szCell[i];
		put2byte(pCellptr, (pData - aData));
		pCellptr += 2;
		if (pData < pCellptr)
			return SQLITE_CORRUPT_BKPT;
		memcpy(pData, pCell, szCell[i]);
		assert(szCell[i] == pPg->xCellSize(pPg, pCell) || CORRUPT_DB);
		testcase(szCell[i] != pPg->xCellSize(pPg, pCell));
	}

	/* The pPg->nFree field is now set incorrectly. The caller will fix it. */
	pPg->nCell = nCell;
	pPg->nOverflow = 0;

	put2byte(&aData[hdr + 1], 0);
	put2byte(&aData[hdr + 3], pPg->nCell);
	put2byte(&aData[hdr + 5], pData - aData);
	aData[hdr + 7] = 0x00;
	return SQLITE_OK;
}

/*
 * Array apCell[] contains nCell pointers to b-tree cells. Array szCell
 * contains the size in bytes of each such cell. This function attempts to
 * add the cells stored in the array to page pPg. If it cannot (because
 * the page needs to be defragmented before the cells will fit), non-zero
 * is returned. Otherwise, if the cells are added successfully, zero is
 * returned.
 *
 * Argument pCellptr points to the first entry in the cell-pointer array
 * (part of page pPg) to populate. After cell apCell[0] is written to the
 * page body, a 16-bit offset is written to pCellptr. And so on, for each
 * cell in the array. It is the responsibility of the caller to ensure
 * that it is safe to overwrite this part of the cell-pointer array.
 *
 * When this function is called, *ppData points to the start of the
 * content area on page pPg. If the size of the content area is extended,
 * *ppData is updated to point to the new start of the content area
 * before returning.
 *
 * Finally, argument pBegin points to the byte immediately following the
 * end of the space required by this page for the cell-pointer area (for
 * all cells - not just those inserted by the current call). If the content
 * area must be extended to before this point in order to accomodate all
 * cells in apCell[], then the cells do not fit and non-zero is returned.
 */
static int
pageInsertArray(MemPage * pPg,	/* Page to add cells to */
		u8 * pBegin,	/* End of cell-pointer array */
		u8 ** ppData,	/* IN/OUT: Page content -area pointer */
		u8 * pCellptr,	/* Pointer to cell-pointer area */
		int iFirst,	/* Index of first cell to add */
		int nCell,	/* Number of cells to add to pPg */
		CellArray * pCArray	/* Array of cells */
    )
{
	int i;
	u8 *aData = pPg->aData;
	u8 *pData = *ppData;
	int iEnd = iFirst + nCell;
	assert(CORRUPT_DB || pPg->hdrOffset == 0);	/* Never called on page 1 */
	for (i = iFirst; i < iEnd; i++) {
		int sz, rc;
		u8 *pSlot;
		sz = cachedCellSize(pCArray, i);
		if ((aData[1] == 0 && aData[2] == 0)
		    || (pSlot = pageFindSlot(pPg, sz, &rc)) == 0) {
			if ((pData - pBegin) < sz)
				return 1;
			pData -= sz;
			pSlot = pData;
		}
		/* pSlot and pCArray->apCell[i] will never overlap on a well-formed
		 * database.  But they might for a corrupt database.  Hence use memmove()
		 * since memcpy() sends SIGABORT with overlapping buffers on OpenBSD
		 */
		assert((pSlot + sz) <= pCArray->apCell[i]
		       || pSlot >= (pCArray->apCell[i] + sz)
		       || CORRUPT_DB);
		memmove(pSlot, pCArray->apCell[i], sz);
		put2byte(pCellptr, (pSlot - aData));
		pCellptr += 2;
	}
	*ppData = pData;
	return 0;
}

/*
 * Array apCell[] contains nCell pointers to b-tree cells. Array szCell
 * contains the size in bytes of each such cell. This function adds the
 * space associated with each cell in the array that is currently stored
 * within the body of pPg to the pPg free-list. The cell-pointers and other
 * fields of the page are not updated.
 *
 * This function returns the total number of cells added to the free-list.
 */
static int
pageFreeArray(MemPage * pPg,	/* Page to edit */
	      int iFirst,	/* First cell to delete */
	      int nCell,	/* Cells to delete */
	      CellArray * pCArray	/* Array of cells */
    )
{
	u8 *const aData = pPg->aData;
	u8 *const pEnd = &aData[pPg->pBt->usableSize];
	u8 *const pStart = &aData[pPg->hdrOffset + 8 + pPg->childPtrSize];
	int nRet = 0;
	int i;
	int iEnd = iFirst + nCell;
	u8 *pFree = 0;
	int szFree = 0;

	for (i = iFirst; i < iEnd; i++) {
		u8 *pCell = pCArray->apCell[i];
		if (SQLITE_WITHIN(pCell, pStart, pEnd)) {
			int sz;
			/* No need to use cachedCellSize() here.  The sizes of all cells that
			 * are to be freed have already been computing while deciding which
			 * cells need freeing
			 */
			sz = pCArray->szCell[i];
			assert(sz > 0);
			if (pFree != (pCell + sz)) {
				if (pFree) {
					assert(pFree > aData
					       && (pFree - aData) < 65536);
					freeSpace(pPg, (u16) (pFree - aData),
						  szFree);
				}
				pFree = pCell;
				szFree = sz;
				if (pFree + sz > pEnd)
					return 0;
			} else {
				pFree = pCell;
				szFree += sz;
			}
			nRet++;
		}
	}
	if (pFree) {
		assert(pFree > aData && (pFree - aData) < 65536);
		freeSpace(pPg, (u16) (pFree - aData), szFree);
	}
	return nRet;
}

/*
 * apCell[] and szCell[] contains pointers to and sizes of all cells in the
 * pages being balanced.  The current page, pPg, has pPg->nCell cells starting
 * with apCell[iOld].  After balancing, this page should hold nNew cells
 * starting at apCell[iNew].
 *
 * This routine makes the necessary adjustments to pPg so that it contains
 * the correct cells after being balanced.
 *
 * The pPg->nFree field is invalid when this function returns. It is the
 * responsibility of the caller to set it correctly.
 */
static int
editPage(MemPage * pPg,		/* Edit this page */
	 int iOld,		/* Index of first cell currently on page */
	 int iNew,		/* Index of new first cell on page */
	 int nNew,		/* Final number of cells on page */
	 CellArray * pCArray	/* Array of cells and sizes */
    )
{
	u8 *const aData = pPg->aData;
	const int hdr = pPg->hdrOffset;
	u8 *pBegin = &pPg->aCellIdx[nNew * 2];
	int nCell = pPg->nCell;	/* Cells stored on pPg */
	u8 *pData;
	u8 *pCellptr;
	int i;
	int iOldEnd = iOld + pPg->nCell + pPg->nOverflow;
	int iNewEnd = iNew + nNew;

#ifdef SQLITE_DEBUG
	u8 *pTmp = sqlite3PagerTempSpace(pPg->pBt->pPager);
	memcpy(pTmp, aData, pPg->pBt->usableSize);
#endif

	/* Remove cells from the start and end of the page */
	if (iOld < iNew) {
		int nShift = pageFreeArray(pPg, iOld, iNew - iOld, pCArray);
		memmove(pPg->aCellIdx, &pPg->aCellIdx[nShift * 2], nCell * 2);
		nCell -= nShift;
	}
	if (iNewEnd < iOldEnd) {
		nCell -=
		    pageFreeArray(pPg, iNewEnd, iOldEnd - iNewEnd, pCArray);
	}

	pData = &aData[get2byteNotZero(&aData[hdr + 5])];
	if (pData < pBegin)
		goto editpage_fail;

	/* Add cells to the start of the page */
	if (iNew < iOld) {
		int nAdd = MIN(nNew, iOld - iNew);
		assert((iOld - iNew) < nNew || nCell == 0 || CORRUPT_DB);
		pCellptr = pPg->aCellIdx;
		memmove(&pCellptr[nAdd * 2], pCellptr, nCell * 2);
		if (pageInsertArray(pPg, pBegin, &pData, pCellptr,
				    iNew, nAdd, pCArray))
			goto editpage_fail;
		nCell += nAdd;
	}

	/* Add any overflow cells */
	for (i = 0; i < pPg->nOverflow; i++) {
		int iCell = (iOld + pPg->aiOvfl[i]) - iNew;
		if (iCell >= 0 && iCell < nNew) {
			pCellptr = &pPg->aCellIdx[iCell * 2];
			memmove(&pCellptr[2], pCellptr, (nCell - iCell) * 2);
			nCell++;
			if (pageInsertArray(pPg, pBegin, &pData, pCellptr,
					    iCell + iNew, 1, pCArray))
				goto editpage_fail;
		}
	}

	/* Append cells to the end of the page */
	pCellptr = &pPg->aCellIdx[nCell * 2];
	if (pageInsertArray(pPg, pBegin, &pData, pCellptr,
			    iNew + nCell, nNew - nCell, pCArray))
		goto editpage_fail;

	pPg->nCell = nNew;
	pPg->nOverflow = 0;

	put2byte(&aData[hdr + 3], pPg->nCell);
	put2byte(&aData[hdr + 5], pData - aData);

#ifdef SQLITE_DEBUG
	for (i = 0; i < nNew && !CORRUPT_DB; i++) {
		u8 *pCell = pCArray->apCell[i + iNew];
		int iOff = get2byteAligned(&pPg->aCellIdx[i * 2]);
		if (SQLITE_WITHIN(pCell, aData, &aData[pPg->pBt->usableSize])) {
			pCell = &pTmp[pCell - aData];
		}
		assert(0 == memcmp(pCell, &aData[iOff],
				   pCArray->pRef->xCellSize(pCArray->pRef,
							    pCArray->apCell[i +
									    iNew])));
	}
#endif

	return SQLITE_OK;
 editpage_fail:
	/* Unable to edit this page. Rebuild it from scratch instead. */
	populateCellCache(pCArray, iNew, nNew);
	return rebuildPage(pPg, nNew, &pCArray->apCell[iNew],
			   &pCArray->szCell[iNew]);
}

/*
 * The following parameters determine how many adjacent pages get involved
 * in a balancing operation.  NN is the number of neighbors on either side
 * of the page that participate in the balancing operation.  NB is the
 * total number of pages that participate, including the target page and
 * NN neighbors on either side.
 *
 * The minimum value of NN is 1 (of course).  Increasing NN above 1
 * (to 2 or 3) gives a modest improvement in SELECT and DELETE performance
 * in exchange for a larger degradation in INSERT and UPDATE performance.
 * The value of NN appears to give the best results overall.
 */
#define NN 1			/* Number of neighbors on either side of pPage */
#define NB (NN*2+1)		/* Total pages involved in the balance */

#ifndef SQLITE_OMIT_QUICKBALANCE
/*
 * This version of balance() handles the common special case where
 * a new entry is being inserted on the extreme right-end of the
 * tree, in other words, when the new entry will become the largest
 * entry in the tree.
 *
 * Instead of trying to balance the 3 right-most leaf pages, just add
 * a new page to the right-hand side and put the one new entry in
 * that page.  This leaves the right side of the tree somewhat
 * unbalanced.  But odds are that we will be inserting new entries
 * at the end soon afterwards so the nearly empty page will quickly
 * fill up.  On average.
 *
 * pPage is the leaf page which is the right-most page in the tree.
 * pParent is its parent.  pPage must have a single overflow entry
 * which is also the right-most entry on the page.
 *
 * The pSpace buffer is used to store a temporary copy of the divider
 * cell that will be inserted into pParent. Such a cell consists of a 4
 * byte page number followed by a variable length integer. In other
 * words, at most 13 bytes. Hence the pSpace buffer must be at
 * least 13 bytes in size.
 */
static int
balance_quick(MemPage * pParent, MemPage * pPage, u8 * pSpace)
{
	BtShared *const pBt = pPage->pBt;	/* B-Tree Database */
	MemPage *pNew;		/* Newly allocated page */
	int rc;			/* Return Code */
	Pgno pgnoNew;		/* Page number of pNew */

	assert(sqlite3_mutex_held(pPage->pBt->mutex));
	assert(sqlite3PagerIswriteable(pParent->pDbPage));
	assert(pPage->nOverflow == 1);

	/* This error condition is now caught prior to reaching this function */
	if (NEVER(pPage->nCell == 0))
		return SQLITE_CORRUPT_BKPT;

	/* Allocate a new page. This page will become the right-sibling of
	 * pPage. Make the parent page writable, so that the new divider cell
	 * may be inserted. If both these operations are successful, proceed.
	 */
	rc = allocateBtreePage(pBt, &pNew, &pgnoNew, 0, 0);

	if (rc == SQLITE_OK) {

		u8 *pOut = &pSpace[4];
		u8 *pCell = pPage->apOvfl[0];
		u16 szCell = pPage->xCellSize(pPage, pCell);
		u8 *pStop;

		assert(sqlite3PagerIswriteable(pNew->pDbPage));
		assert(pPage->aData[0] ==
		       (PTF_INTKEY | PTF_LEAFDATA | PTF_LEAF));
		zeroPage(pNew, PTF_INTKEY | PTF_LEAFDATA | PTF_LEAF);
		rc = rebuildPage(pNew, 1, &pCell, &szCell);
		if (NEVER(rc))
			return rc;
		pNew->nFree = pBt->usableSize - pNew->cellOffset - 2 - szCell;

		/* Create a divider cell to insert into pParent. The divider cell
		 * consists of a 4-byte page number (the page number of pPage) and
		 * a variable length key value (which must be the same value as the
		 * largest key on pPage).
		 *
		 * To find the largest key value on pPage, first find the right-most
		 * cell on pPage. The first two fields of this cell are the
		 * record-length (a variable length integer at most 32-bits in size)
		 * and the key value (a variable length integer, may have any value).
		 * The first of the while(...) loops below skips over the record-length
		 * field. The second while(...) loop copies the key value from the
		 * cell on pPage into the pSpace buffer.
		 */
		pCell = findCell(pPage, pPage->nCell - 1);
		pStop = &pCell[9];
		while ((*(pCell++) & 0x80) && pCell < pStop) ;
		pStop = &pCell[9];
		while (((*(pOut++) = *(pCell++)) & 0x80) && pCell < pStop) ;

		/* Insert the new divider cell into pParent. */
		if (rc == SQLITE_OK) {
			insertCell(pParent, pParent->nCell, pSpace,
				   (int)(pOut - pSpace), 0, pPage->pgno, &rc);
		}

		/* Set the right-child pointer of pParent to point to the new page. */
		put4byte(&pParent->aData[pParent->hdrOffset + 8], pgnoNew);

		/* Release the reference to the new page. */
		releasePage(pNew);
	}

	return rc;
}
#endif				/* SQLITE_OMIT_QUICKBALANCE */

/*
 * This function is used to copy the contents of the b-tree node stored
 * on page pFrom to page pTo. If page pFrom was not a leaf page, then
 * the pointer-map entries for each child page are updated so that the
 * parent page stored in the pointer map is page pTo. If pFrom contained
 * any cells with overflow page pointers, then the corresponding pointer
 * map entries are also updated so that the parent page is page pTo.
 *
 * If pFrom is currently carrying any overflow cells (entries in the
 * MemPage.apOvfl[] array), they are not copied to pTo.
 *
 * Before returning, page pTo is reinitialized using btreeInitPage().
 *
 * The performance of this function is not critical. It is only used by
 * the balance_shallower() and balance_deeper() procedures, neither of
 * which are called often under normal circumstances.
 */
static void
copyNodeContent(MemPage * pFrom, MemPage * pTo, int *pRC)
{
	if ((*pRC) == SQLITE_OK) {
		BtShared *const pBt = pFrom->pBt;
		u8 *const aFrom = pFrom->aData;
		u8 *const aTo = pTo->aData;
		int const iFromHdr = pFrom->hdrOffset;
		int const iToHdr = ((pTo->pgno == 1) ? 100 : 0);
		int rc;
		int iData;

		assert(pFrom->isInit);
		assert(pFrom->nFree >= iToHdr);
		assert(get2byte(&aFrom[iFromHdr + 5]) <= (int)pBt->usableSize);

		/* Copy the b-tree node content from page pFrom to page pTo. */
		iData = get2byte(&aFrom[iFromHdr + 5]);
		memcpy(&aTo[iData], &aFrom[iData], pBt->usableSize - iData);
		memcpy(&aTo[iToHdr], &aFrom[iFromHdr],
		       pFrom->cellOffset + 2 * pFrom->nCell);

		/* Reinitialize page pTo so that the contents of the MemPage structure
		 * match the new data. The initialization of pTo can actually fail under
		 * fairly obscure circumstances, even though it is a copy of initialized
		 * page pFrom.
		 */
		pTo->isInit = 0;
		rc = btreeInitPage(pTo);
		if (rc != SQLITE_OK) {
			*pRC = rc;
			return;
		}
	}
}

/*
 * This routine redistributes cells on the iParentIdx'th child of pParent
 * (hereafter "the page") and up to 2 siblings so that all pages have about the
 * same amount of free space. Usually a single sibling on either side of the
 * page are used in the balancing, though both siblings might come from one
 * side if the page is the first or last child of its parent. If the page
 * has fewer than 2 siblings (something which can only happen if the page
 * is a root page or a child of a root page) then all available siblings
 * participate in the balancing.
 *
 * The number of siblings of the page might be increased or decreased by
 * one or two in an effort to keep pages nearly full but not over full.
 *
 * Note that when this routine is called, some of the cells on the page
 * might not actually be stored in MemPage.aData[]. This can happen
 * if the page is overfull. This routine ensures that all cells allocated
 * to the page and its siblings fit into MemPage.aData[] before returning.
 *
 * In the course of balancing the page and its siblings, cells may be
 * inserted into or removed from the parent page (pParent). Doing so
 * may cause the parent page to become overfull or underfull. If this
 * happens, it is the responsibility of the caller to invoke the correct
 * balancing routine to fix this problem (see the balance() routine).
 *
 * If this routine fails for any reason, it might leave the database
 * in a corrupted state. So if this routine fails, the database should
 * be rolled back.
 *
 * The third argument to this function, aOvflSpace, is a pointer to a
 * buffer big enough to hold one page. If while inserting cells into the parent
 * page (pParent) the parent page becomes overfull, this buffer is
 * used to store the parent's overflow cells. Because this function inserts
 * a maximum of four divider cells into the parent page, and the maximum
 * size of a cell stored within an internal node is always less than 1/4
 * of the page-size, the aOvflSpace[] buffer is guaranteed to be large
 * enough for all overflow cells.
 *
 * If aOvflSpace is set to a null pointer, this function returns
 * SQLITE_NOMEM.
 */
static int
balance_nonroot(MemPage * pParent,	/* Parent page of siblings being balanced */
		int iParentIdx,	/* Index of "the page" in pParent */
		u8 * aOvflSpace,	/* page-size bytes of space for parent ovfl */
		int isRoot,	/* True if pParent is a root-page */
		int bBulk	/* True if this call is part of a bulk load */
    )
{
	BtShared *pBt;		/* The whole database */
	int nMaxCells = 0;	/* Allocated size of apCell, szCell, aFrom. */
	int nNew = 0;		/* Number of pages in apNew[] */
	int nOld;		/* Number of pages in apOld[] */
	int i, j, k;		/* Loop counters */
	int nxDiv;		/* Next divider slot in pParent->aCell[] */
	int rc = SQLITE_OK;	/* The return code */
	u16 leafCorrection;	/* 4 if pPage is a leaf.  0 if not */
	int leafData;		/* True if pPage is a leaf of a LEAFDATA tree */
	int usableSpace;	/* Bytes in pPage beyond the header */
	int pageFlags;		/* Value of pPage->aData[0] */
	int iSpace1 = 0;	/* First unused byte of aSpace1[] */
	int iOvflSpace = 0;	/* First unused byte of aOvflSpace[] */
	int szScratch;		/* Size of scratch memory requested */
	MemPage *apOld[NB];	/* pPage and up to two siblings */
	MemPage *apNew[NB + 2];	/* pPage and up to NB siblings after balancing */
	u8 *pRight;		/* Location in parent of right-sibling pointer */
	u8 *apDiv[NB - 1];	/* Divider cells in pParent */
	int cntNew[NB + 2];	/* Index in b.paCell[] of cell after i-th page */
	int cntOld[NB + 2];	/* Old index in b.apCell[] */
	int szNew[NB + 2];	/* Combined size of cells placed on i-th page */
	u8 *aSpace1;		/* Space for copies of dividers cells */
	Pgno pgno;		/* Temp var to store a page number in */
	u8 abDone[NB + 2];	/* True after i'th new page is populated */
	Pgno aPgno[NB + 2];	/* Page numbers of new pages before shuffling */
	Pgno aPgOrder[NB + 2];	/* Copy of aPgno[] used for sorting pages */
	u16 aPgFlags[NB + 2];	/* flags field of new pages before shuffling */
	CellArray b;		/* Parsed information on cells being balanced */

	memset(abDone, 0, sizeof(abDone));
	b.nCell = 0;
	b.apCell = 0;
	pBt = pParent->pBt;
	assert(sqlite3_mutex_held(pBt->mutex));
	assert(sqlite3PagerIswriteable(pParent->pDbPage));

#if 0
	TRACE(("BALANCE: begin page %d child of %d\n", pPage->pgno,
	       pParent->pgno));
#endif

	/* At this point pParent may have at most one overflow cell. And if
	 * this overflow cell is present, it must be the cell with
	 * index iParentIdx. This scenario comes about when this function
	 * is called (indirectly) from sqlite3BtreeDelete().
	 */
	assert(pParent->nOverflow == 0 || pParent->nOverflow == 1);
	assert(pParent->nOverflow == 0 || pParent->aiOvfl[0] == iParentIdx);

	if (!aOvflSpace) {
		return SQLITE_NOMEM_BKPT;
	}

	/* Find the sibling pages to balance. Also locate the cells in pParent
	 * that divide the siblings. An attempt is made to find NN siblings on
	 * either side of pPage. More siblings are taken from one side, however,
	 * if there are fewer than NN siblings on the other side. If pParent
	 * has NB or fewer children then all children of pParent are taken.
	 *
	 * This loop also drops the divider cells from the parent page. This
	 * way, the remainder of the function does not have to deal with any
	 * overflow cells in the parent page, since if any existed they will
	 * have already been removed.
	 */
	i = pParent->nOverflow + pParent->nCell;
	if (i < 2) {
		nxDiv = 0;
	} else {
		assert(bBulk == 0 || bBulk == 1);
		if (iParentIdx == 0) {
			nxDiv = 0;
		} else if (iParentIdx == i) {
			nxDiv = i - 2 + bBulk;
		} else {
			nxDiv = iParentIdx - 1;
		}
		i = 2 - bBulk;
	}
	nOld = i + 1;
	if ((i + nxDiv - pParent->nOverflow) == pParent->nCell) {
		pRight = &pParent->aData[pParent->hdrOffset + 8];
	} else {
		pRight = findCell(pParent, i + nxDiv - pParent->nOverflow);
	}
	pgno = get4byte(pRight);
	while (1) {
		rc = getAndInitPage(pBt, pgno, &apOld[i], 0, 0);
		if (rc) {
			memset(apOld, 0, (i + 1) * sizeof(MemPage *));
			goto balance_cleanup;
		}
		nMaxCells += 1 + apOld[i]->nCell + apOld[i]->nOverflow;
		if ((i--) == 0)
			break;

		if (pParent->nOverflow && i + nxDiv == pParent->aiOvfl[0]) {
			apDiv[i] = pParent->apOvfl[0];
			pgno = get4byte(apDiv[i]);
			szNew[i] = pParent->xCellSize(pParent, apDiv[i]);
			pParent->nOverflow = 0;
		} else {
			apDiv[i] =
			    findCell(pParent, i + nxDiv - pParent->nOverflow);
			pgno = get4byte(apDiv[i]);
			szNew[i] = pParent->xCellSize(pParent, apDiv[i]);

			/* Drop the cell from the parent page. apDiv[i] still points to
			 * the cell within the parent, even though it has been dropped.
			 * This is safe because dropping a cell only overwrites the first
			 * four bytes of it, and this function does not need the first
			 * four bytes of the divider cell. So the pointer is safe to use
			 * later on.
			 *
			 * But not if we are in secure-delete mode. In secure-delete mode,
			 * the dropCell() routine will overwrite the entire cell with zeroes.
			 * In this case, temporarily copy the cell into the aOvflSpace[]
			 * buffer. It will be copied out again as soon as the aSpace[] buffer
			 * is allocated.
			 */
			if (pBt->btsFlags & BTS_SECURE_DELETE) {
				int iOff;

				iOff =
				    SQLITE_PTR_TO_INT(apDiv[i]) -
				    SQLITE_PTR_TO_INT(pParent->aData);
				if ((iOff + szNew[i]) > (int)pBt->usableSize) {
					rc = SQLITE_CORRUPT_BKPT;
					memset(apOld, 0,
					       (i + 1) * sizeof(MemPage *));
					goto balance_cleanup;
				} else {
					memcpy(&aOvflSpace[iOff], apDiv[i],
					       szNew[i]);
					apDiv[i] =
					    &aOvflSpace[apDiv[i] -
							pParent->aData];
				}
			}
			dropCell(pParent, i + nxDiv - pParent->nOverflow,
				 szNew[i], &rc);
		}
	}

	/* Make nMaxCells a multiple of 4 in order to preserve 8-byte
	 * alignment
	 */
	nMaxCells = (nMaxCells + 3) & ~3;

	/*
	 * Allocate space for memory structures
	 */
	szScratch = nMaxCells * sizeof(u8 *)	/* b.apCell */
	    +nMaxCells * sizeof(u16)	/* b.szCell */
	    +pBt->pageSize;	/* aSpace1 */

	/* EVIDENCE-OF: R-28375-38319 SQLite will never request a scratch buffer
	 * that is more than 6 times the database page size.
	 */
	assert(szScratch <= 6 * (int)pBt->pageSize);
	b.apCell = sqlite3ScratchMalloc(szScratch);
	if (b.apCell == 0) {
		rc = SQLITE_NOMEM_BKPT;
		goto balance_cleanup;
	}
	b.szCell = (u16 *) & b.apCell[nMaxCells];
	aSpace1 = (u8 *) & b.szCell[nMaxCells];
	assert(EIGHT_BYTE_ALIGNMENT(aSpace1));

	/*
	 * Load pointers to all cells on sibling pages and the divider cells
	 * into the local b.apCell[] array.  Make copies of the divider cells
	 * into space obtained from aSpace1[]. The divider cells have already
	 * been removed from pParent.
	 *
	 * If the siblings are on leaf pages, then the child pointers of the
	 * divider cells are stripped from the cells before they are copied
	 * into aSpace1[].  In this way, all cells in b.apCell[] are without
	 * child pointers.  If siblings are not leaves, then all cell in
	 * b.apCell[] include child pointers.  Either way, all cells in b.apCell[]
	 * are alike.
	 *
	 * leafCorrection:  4 if pPage is a leaf.  0 if pPage is not a leaf.
	 *       leafData:  1 if pPage holds key+data and pParent holds only keys.
	 */
	b.pRef = apOld[0];
	leafCorrection = b.pRef->leaf * 4;
	leafData = b.pRef->intKeyLeaf;
	for (i = 0; i < nOld; i++) {
		MemPage *pOld = apOld[i];
		int limit = pOld->nCell;
		u8 *aData = pOld->aData;
		u16 maskPage = pOld->maskPage;
		u8 *piCell = aData + pOld->cellOffset;
		u8 *piEnd;

		/* Verify that all sibling pages are of the same "type" (table-leaf,
		 * table-interior, index-leaf, or index-interior).
		 */
		if (pOld->aData[0] != apOld[0]->aData[0]) {
			rc = SQLITE_CORRUPT_BKPT;
			goto balance_cleanup;
		}

		/* Load b.apCell[] with pointers to all cells in pOld.  If pOld
		 * constains overflow cells, include them in the b.apCell[] array
		 * in the correct spot.
		 *
		 * Note that when there are multiple overflow cells, it is always the
		 * case that they are sequential and adjacent.  This invariant arises
		 * because multiple overflows can only occurs when inserting divider
		 * cells into a parent on a prior balance, and divider cells are always
		 * adjacent and are inserted in order.  There is an assert() tagged
		 * with "NOTE 1" in the overflow cell insertion loop to prove this
		 * invariant.
		 *
		 * This must be done in advance.  Once the balance starts, the cell
		 * offset section of the btree page will be overwritten and we will no
		 * long be able to find the cells if a pointer to each cell is not saved
		 * first.
		 */
		memset(&b.szCell[b.nCell], 0,
		       sizeof(b.szCell[0]) * (limit + pOld->nOverflow));
		if (pOld->nOverflow > 0) {
			limit = pOld->aiOvfl[0];
			for (j = 0; j < limit; j++) {
				b.apCell[b.nCell] =
				    aData +
				    (maskPage & get2byteAligned(piCell));
				piCell += 2;
				b.nCell++;
			}
			for (k = 0; k < pOld->nOverflow; k++) {
				assert(k == 0 || pOld->aiOvfl[k - 1] + 1 == pOld->aiOvfl[k]);	/* NOTE 1 */
				b.apCell[b.nCell] = pOld->apOvfl[k];
				b.nCell++;
			}
		}
		piEnd = aData + pOld->cellOffset + 2 * pOld->nCell;
		while (piCell < piEnd) {
			assert(b.nCell < nMaxCells);
			b.apCell[b.nCell] =
			    aData + (maskPage & get2byteAligned(piCell));
			piCell += 2;
			b.nCell++;
		}

		cntOld[i] = b.nCell;
		if (i < nOld - 1 && !leafData) {
			u16 sz = (u16) szNew[i];
			u8 *pTemp;
			assert(b.nCell < nMaxCells);
			b.szCell[b.nCell] = sz;
			pTemp = &aSpace1[iSpace1];
			iSpace1 += sz;
			assert(sz <= pBt->maxLocal + 23);
			assert(iSpace1 <= (int)pBt->pageSize);
			memcpy(pTemp, apDiv[i], sz);
			b.apCell[b.nCell] = pTemp + leafCorrection;
			assert(leafCorrection == 0 || leafCorrection == 4);
			b.szCell[b.nCell] = b.szCell[b.nCell] - leafCorrection;
			if (!pOld->leaf) {
				assert(leafCorrection == 0);
				assert(pOld->hdrOffset == 0);
				/* The right pointer of the child page pOld becomes the left
				 * pointer of the divider cell
				 */
				memcpy(b.apCell[b.nCell], &pOld->aData[8], 4);
			} else {
				assert(leafCorrection == 4);
				while (b.szCell[b.nCell] < 4) {
					/* Do not allow any cells smaller than 4 bytes. If a smaller cell
					 * does exist, pad it with 0x00 bytes.
					 */
					assert(b.szCell[b.nCell] == 3
					       || CORRUPT_DB);
					assert(b.apCell[b.nCell] ==
					       &aSpace1[iSpace1 - 3]
					       || CORRUPT_DB);
					aSpace1[iSpace1++] = 0x00;
					b.szCell[b.nCell]++;
				}
			}
			b.nCell++;
		}
	}

	/*
	 * Figure out the number of pages needed to hold all b.nCell cells.
	 * Store this number in "k".  Also compute szNew[] which is the total
	 * size of all cells on the i-th page and cntNew[] which is the index
	 * in b.apCell[] of the cell that divides page i from page i+1.
	 * cntNew[k] should equal b.nCell.
	 *
	 * Values computed by this block:
	 *
	 *           k: The total number of sibling pages
	 *    szNew[i]: Spaced used on the i-th sibling page.
	 *   cntNew[i]: Index in b.apCell[] and b.szCell[] for the first cell to
	 *              the right of the i-th sibling page.
	 * usableSpace: Number of bytes of space available on each sibling.
	 *
	 */
	usableSpace = pBt->usableSize - 12 + leafCorrection;
	for (i = 0; i < nOld; i++) {
		MemPage *p = apOld[i];
		szNew[i] = usableSpace - p->nFree;
		if (szNew[i] < 0) {
			rc = SQLITE_CORRUPT_BKPT;
			goto balance_cleanup;
		}
		for (j = 0; j < p->nOverflow; j++) {
			szNew[i] += 2 + p->xCellSize(p, p->apOvfl[j]);
		}
		cntNew[i] = cntOld[i];
	}
	k = nOld;
	for (i = 0; i < k; i++) {
		int sz;
		while (szNew[i] > usableSpace) {
			if (i + 1 >= k) {
				k = i + 2;
				if (k > NB + 2) {
					rc = SQLITE_CORRUPT_BKPT;
					goto balance_cleanup;
				}
				szNew[k - 1] = 0;
				cntNew[k - 1] = b.nCell;
			}
			sz = 2 + cachedCellSize(&b, cntNew[i] - 1);
			szNew[i] -= sz;
			if (!leafData) {
				if (cntNew[i] < b.nCell) {
					sz = 2 + cachedCellSize(&b, cntNew[i]);
				} else {
					sz = 0;
				}
			}
			szNew[i + 1] += sz;
			cntNew[i]--;
		}
		while (cntNew[i] < b.nCell) {
			sz = 2 + cachedCellSize(&b, cntNew[i]);
			if (szNew[i] + sz > usableSpace)
				break;
			szNew[i] += sz;
			cntNew[i]++;
			if (!leafData) {
				if (cntNew[i] < b.nCell) {
					sz = 2 + cachedCellSize(&b, cntNew[i]);
				} else {
					sz = 0;
				}
			}
			szNew[i + 1] -= sz;
		}
		if (cntNew[i] >= b.nCell) {
			k = i + 1;
		} else if (cntNew[i] <= (i > 0 ? cntNew[i - 1] : 0)) {
			rc = SQLITE_CORRUPT_BKPT;
			goto balance_cleanup;
		}
	}

	/*
	 * The packing computed by the previous block is biased toward the siblings
	 * on the left side (siblings with smaller keys). The left siblings are
	 * always nearly full, while the right-most sibling might be nearly empty.
	 * The next block of code attempts to adjust the packing of siblings to
	 * get a better balance.
	 *
	 * This adjustment is more than an optimization.  The packing above might
	 * be so out of balance as to be illegal.  For example, the right-most
	 * sibling might be completely empty.  This adjustment is not optional.
	 */
	for (i = k - 1; i > 0; i--) {
		int szRight = szNew[i];	/* Size of sibling on the right */
		int szLeft = szNew[i - 1];	/* Size of sibling on the left */
		int r;		/* Index of right-most cell in left sibling */
		int d;		/* Index of first cell to the left of right sibling */

		r = cntNew[i - 1] - 1;
		d = r + 1 - leafData;
		(void)cachedCellSize(&b, d);
		do {
			assert(d < nMaxCells);
			assert(r < nMaxCells);
			(void)cachedCellSize(&b, r);
			if (szRight != 0
			    && (bBulk
				|| szRight + b.szCell[d] + 2 >
				szLeft - (b.szCell[r] +
					  (i == k - 1 ? 0 : 2)))) {
				break;
			}
			szRight += b.szCell[d] + 2;
			szLeft -= b.szCell[r] + 2;
			cntNew[i - 1] = r;
			r--;
			d--;
		} while (r >= 0);
		szNew[i] = szRight;
		szNew[i - 1] = szLeft;
		if (cntNew[i - 1] <= (i > 1 ? cntNew[i - 2] : 0)) {
			rc = SQLITE_CORRUPT_BKPT;
			goto balance_cleanup;
		}
	}

	/* Sanity check:  For a non-corrupt database file one of the follwing
	 * must be true:
	 *    (1) We found one or more cells (cntNew[0])>0), or
	 *    (2) pPage is a virtual root page.  A virtual root page is when
	 *        the real root page is page 1 and we are the only child of
	 *        that page.
	 */
	assert(cntNew[0] > 0 || (pParent->pgno == 1 && pParent->nCell == 0)
	       || CORRUPT_DB);
	TRACE(("BALANCE: old: %d(nc=%d) %d(nc=%d) %d(nc=%d)\n", apOld[0]->pgno,
	       apOld[0]->nCell, nOld >= 2 ? apOld[1]->pgno : 0,
	       nOld >= 2 ? apOld[1]->nCell : 0, nOld >= 3 ? apOld[2]->pgno : 0,
	       nOld >= 3 ? apOld[2]->nCell : 0));

	/*
	 * Allocate k new pages.  Reuse old pages where possible.
	 */
	pageFlags = apOld[0]->aData[0];
	for (i = 0; i < k; i++) {
		MemPage *pNew;
		if (i < nOld) {
			pNew = apNew[i] = apOld[i];
			apOld[i] = 0;
			rc = sqlite3PagerWrite(pNew->pDbPage);
			nNew++;
			if (rc)
				goto balance_cleanup;
		} else {
			assert(i > 0);
			rc = allocateBtreePage(pBt, &pNew, &pgno,
					       (bBulk ? 1 : pgno), 0);
			if (rc)
				goto balance_cleanup;
			zeroPage(pNew, pageFlags);
			apNew[i] = pNew;
			nNew++;
			cntOld[i] = b.nCell;
		}
	}

	/*
	 * Reassign page numbers so that the new pages are in ascending order.
	 * This helps to keep entries in the disk file in order so that a scan
	 * of the table is closer to a linear scan through the file. That in turn
	 * helps the operating system to deliver pages from the disk more rapidly.
	 *
	 * An O(n^2) insertion sort algorithm is used, but since n is never more
	 * than (NB+2) (a small constant), that should not be a problem.
	 *
	 * When NB==3, this one optimization makes the database about 25% faster
	 * for large insertions and deletions.
	 */
	for (i = 0; i < nNew; i++) {
		aPgOrder[i] = aPgno[i] = apNew[i]->pgno;
		aPgFlags[i] = apNew[i]->pDbPage->flags;
		for (j = 0; j < i; j++) {
			if (aPgno[j] == aPgno[i]) {
				/* This branch is taken if the set of sibling pages somehow contains
				 * duplicate entries. This can happen if the database is corrupt.
				 * It would be simpler to detect this as part of the loop below, but
				 * we do the detection here in order to avoid populating the pager
				 * cache with two separate objects associated with the same
				 * page number.
				 */
				assert(CORRUPT_DB);
				rc = SQLITE_CORRUPT_BKPT;
				goto balance_cleanup;
			}
		}
	}
	for (i = 0; i < nNew; i++) {
		int iBest = 0;	/* aPgno[] index of page number to use */
		for (j = 1; j < nNew; j++) {
			if (aPgOrder[j] < aPgOrder[iBest])
				iBest = j;
		}
		pgno = aPgOrder[iBest];
		aPgOrder[iBest] = 0xffffffff;
		if (iBest != i) {
			if (iBest > i) {
				sqlite3PagerRekey(apNew[iBest]->pDbPage,
						  pBt->nPage + iBest + 1, 0);
			}
			sqlite3PagerRekey(apNew[i]->pDbPage, pgno,
					  aPgFlags[iBest]);
			apNew[i]->pgno = pgno;
		}
	}

	TRACE(("BALANCE: new: %d(%d nc=%d) %d(%d nc=%d) %d(%d nc=%d) "
	       "%d(%d nc=%d) %d(%d nc=%d)\n",
	       apNew[0]->pgno, szNew[0], cntNew[0],
	       nNew >= 2 ? apNew[1]->pgno : 0, nNew >= 2 ? szNew[1] : 0,
	       nNew >= 2 ? cntNew[1] - cntNew[0] - !leafData : 0,
	       nNew >= 3 ? apNew[2]->pgno : 0, nNew >= 3 ? szNew[2] : 0,
	       nNew >= 3 ? cntNew[2] - cntNew[1] - !leafData : 0,
	       nNew >= 4 ? apNew[3]->pgno : 0, nNew >= 4 ? szNew[3] : 0,
	       nNew >= 4 ? cntNew[3] - cntNew[2] - !leafData : 0,
	       nNew >= 5 ? apNew[4]->pgno : 0, nNew >= 5 ? szNew[4] : 0,
	       nNew >= 5 ? cntNew[4] - cntNew[3] - !leafData : 0));

	assert(sqlite3PagerIswriteable(pParent->pDbPage));
	put4byte(pRight, apNew[nNew - 1]->pgno);

	/* If the sibling pages are not leaves, ensure that the right-child pointer
	 * of the right-most new sibling page is set to the value that was
	 * originally in the same field of the right-most old sibling page.
	 */
	if ((pageFlags & PTF_LEAF) == 0 && nOld != nNew) {
		MemPage *pOld = (nNew > nOld ? apNew : apOld)[nOld - 1];
		memcpy(&apNew[nNew - 1]->aData[8], &pOld->aData[8], 4);
	}

	/* Insert new divider cells into pParent. */
	for (i = 0; i < nNew - 1; i++) {
		u8 *pCell;
		u8 *pTemp;
		int sz;
		MemPage *pNew = apNew[i];
		j = cntNew[i];

		assert(j < nMaxCells);
		assert(b.apCell[j] != 0);
		pCell = b.apCell[j];
		sz = b.szCell[j] + leafCorrection;
		pTemp = &aOvflSpace[iOvflSpace];
		if (!pNew->leaf) {
			memcpy(&pNew->aData[8], pCell, 4);
		} else if (leafData) {
			/* If the tree is a leaf-data tree, and the siblings are leaves,
			 * then there is no divider cell in b.apCell[]. Instead, the divider
			 * cell consists of the integer key for the right-most cell of
			 * the sibling-page assembled above only.
			 */
			CellInfo info;
			j--;
			pNew->xParseCell(pNew, b.apCell[j], &info);
			pCell = pTemp;
			sz = 4 + putVarint(&pCell[4], info.nKey);
			pTemp = 0;
		} else {
			pCell -= 4;
			/* Obscure case for non-leaf-data trees: If the cell at pCell was
			 * previously stored on a leaf node, and its reported size was 4
			 * bytes, then it may actually be smaller than this
			 * (see btreeParseCellPtr(), 4 bytes is the minimum size of
			 * any cell). But it is important to pass the correct size to
			 * insertCell(), so reparse the cell now.
			 *
			 * This can only happen for b-trees used to evaluate "IN (SELECT ...)"
			 * and WITHOUT ROWID tables with exactly one column which is the
			 * primary key.
			 */
			if (b.szCell[j] == 4) {
				assert(leafCorrection == 4);
				sz = pParent->xCellSize(pParent, pCell);
			}
		}
		iOvflSpace += sz;
		assert(sz <= pBt->maxLocal + 23);
		assert(iOvflSpace <= (int)pBt->pageSize);
		insertCell(pParent, nxDiv + i, pCell, sz, pTemp, pNew->pgno,
			   &rc);
		if (rc != SQLITE_OK)
			goto balance_cleanup;
		assert(sqlite3PagerIswriteable(pParent->pDbPage));
	}

	/* Now update the actual sibling pages. The order in which they are updated
	 * is important, as this code needs to avoid disrupting any page from which
	 * cells may still to be read. In practice, this means:
	 *
	 *  (1) If cells are moving left (from apNew[iPg] to apNew[iPg-1])
	 *      then it is not safe to update page apNew[iPg] until after
	 *      the left-hand sibling apNew[iPg-1] has been updated.
	 *
	 *  (2) If cells are moving right (from apNew[iPg] to apNew[iPg+1])
	 *      then it is not safe to update page apNew[iPg] until after
	 *      the right-hand sibling apNew[iPg+1] has been updated.
	 *
	 * If neither of the above apply, the page is safe to update.
	 *
	 * The iPg value in the following loop starts at nNew-1 goes down
	 * to 0, then back up to nNew-1 again, thus making two passes over
	 * the pages.  On the initial downward pass, only condition (1) above
	 * needs to be tested because (2) will always be true from the previous
	 * step.  On the upward pass, both conditions are always true, so the
	 * upwards pass simply processes pages that were missed on the downward
	 * pass.
	 */
	for (i = 1 - nNew; i < nNew; i++) {
		int iPg = i < 0 ? -i : i;
		assert(iPg >= 0 && iPg < nNew);
		if (abDone[iPg])
			continue;	/* Skip pages already processed */
		if (i >= 0	/* On the upwards pass, or... */
		    || cntOld[iPg - 1] >= cntNew[iPg - 1]	/* Condition (1) is true */
		    ) {
			int iNew;
			int iOld;
			int nNewCell;

			/* Verify condition (1):  If cells are moving left, update iPg
			 * only after iPg-1 has already been updated.
			 */
			assert(iPg == 0 || cntOld[iPg - 1] >= cntNew[iPg - 1]
			       || abDone[iPg - 1]);

			/* Verify condition (2):  If cells are moving right, update iPg
			 * only after iPg+1 has already been updated.
			 */
			assert(cntNew[iPg] >= cntOld[iPg] || abDone[iPg + 1]);

			if (iPg == 0) {
				iNew = iOld = 0;
				nNewCell = cntNew[0];
			} else {
				iOld =
				    iPg <
				    nOld ? (cntOld[iPg - 1] +
					    !leafData) : b.nCell;
				iNew = cntNew[iPg - 1] + !leafData;
				nNewCell = cntNew[iPg] - iNew;
			}

			rc = editPage(apNew[iPg], iOld, iNew, nNewCell, &b);
			if (rc)
				goto balance_cleanup;
			abDone[iPg]++;
			apNew[iPg]->nFree = usableSpace - szNew[iPg];
			assert(apNew[iPg]->nOverflow == 0);
			assert(apNew[iPg]->nCell == nNewCell);
		}
	}

	/* All pages have been processed exactly once */
	assert(memcmp(abDone, "\01\01\01\01\01", nNew) == 0);

	assert(nOld > 0);
	assert(nNew > 0);

	if (isRoot && pParent->nCell == 0
	    && pParent->hdrOffset <= apNew[0]->nFree) {
		/* The root page of the b-tree now contains no cells. The only sibling
		 * page is the right-child of the parent. Copy the contents of the
		 * child page into the parent, decreasing the overall height of the
		 * b-tree structure by one. This is described as the "balance-shallower"
		 * sub-algorithm in some documentation.
		 *
		 * It is critical that the child page be defragmented before being
		 * copied into the parent, because if the parent is page 1 then it will
		 * by smaller than the child due to the database header, and so all the
		 * free space needs to be up front.
		 */
		assert(nNew == 1 || CORRUPT_DB);
		rc = defragmentPage(apNew[0]);
		testcase(rc != SQLITE_OK);
		assert(apNew[0]->nFree ==
		       (get2byte(&apNew[0]->aData[5]) - apNew[0]->cellOffset -
			apNew[0]->nCell * 2)
		       || rc != SQLITE_OK);
		copyNodeContent(apNew[0], pParent, &rc);
		freePage(apNew[0], &rc);
	}

	assert(pParent->isInit);
	TRACE(("BALANCE: finished: old=%d new=%d cells=%d\n",
	       nOld, nNew, b.nCell));

	/* Free any old pages that were not reused as new pages.
	 */
	for (i = nNew; i < nOld; i++) {
		freePage(apOld[i], &rc);
	}

	/*
	 * Cleanup before returning.
	 */
 balance_cleanup:
	sqlite3ScratchFree(b.apCell);
	for (i = 0; i < nOld; i++) {
		releasePage(apOld[i]);
	}
	for (i = 0; i < nNew; i++) {
		releasePage(apNew[i]);
	}

	return rc;
}

/*
 * This function is called when the root page of a b-tree structure is
 * overfull (has one or more overflow pages).
 *
 * A new child page is allocated and the contents of the current root
 * page, including overflow cells, are copied into the child. The root
 * page is then overwritten to make it an empty page with the right-child
 * pointer pointing to the new page.
 *
 * Before returning, all pointer-map entries corresponding to pages
 * that the new child-page now contains pointers to are updated. The
 * entry corresponding to the new right-child pointer of the root
 * page is also updated.
 *
 * If successful, *ppChild is set to contain a reference to the child
 * page and SQLITE_OK is returned. In this case the caller is required
 * to call releasePage() on *ppChild exactly once. If an error occurs,
 * an error code is returned and *ppChild is set to 0.
 */
static int
balance_deeper(MemPage * pRoot, MemPage ** ppChild)
{
	int rc;			/* Return value from subprocedures */
	MemPage *pChild = 0;	/* Pointer to a new child page */
	Pgno pgnoChild = 0;	/* Page number of the new child page */
	BtShared *pBt = pRoot->pBt;	/* The BTree */

	assert(pRoot->nOverflow > 0);
	assert(sqlite3_mutex_held(pBt->mutex));

	/* Make pRoot, the root page of the b-tree, writable. Allocate a new
	 * page that will become the new right-child of pPage. Copy the contents
	 * of the node stored on pRoot into the new child page.
	 */
	rc = sqlite3PagerWrite(pRoot->pDbPage);
	if (rc == SQLITE_OK) {
		rc = allocateBtreePage(pBt, &pChild, &pgnoChild, pRoot->pgno,
				       0);
		copyNodeContent(pRoot, pChild, &rc);
	}
	if (rc) {
		*ppChild = 0;
		releasePage(pChild);
		return rc;
	}
	assert(sqlite3PagerIswriteable(pChild->pDbPage));
	assert(sqlite3PagerIswriteable(pRoot->pDbPage));
	assert(pChild->nCell == pRoot->nCell);

	TRACE(("BALANCE: copy root %d into %d\n", pRoot->pgno, pChild->pgno));

	/* Copy the overflow cells from pRoot to pChild */
	memcpy(pChild->aiOvfl, pRoot->aiOvfl,
	       pRoot->nOverflow * sizeof(pRoot->aiOvfl[0]));
	memcpy(pChild->apOvfl, pRoot->apOvfl,
	       pRoot->nOverflow * sizeof(pRoot->apOvfl[0]));
	pChild->nOverflow = pRoot->nOverflow;

	/* Zero the contents of pRoot. Then install pChild as the right-child. */
	zeroPage(pRoot, pChild->aData[0] & ~PTF_LEAF);
	put4byte(&pRoot->aData[pRoot->hdrOffset + 8], pgnoChild);

	*ppChild = pChild;
	return SQLITE_OK;
}

/*
 * The page that pCur currently points to has just been modified in
 * some way. This function figures out if this modification means the
 * tree needs to be balanced, and if so calls the appropriate balancing
 * routine. Balancing routines are:
 *
 *   balance_quick()
 *   balance_deeper()
 *   balance_nonroot()
 */
static int
balance(BtCursor * pCur)
{
	int rc = SQLITE_OK;
	const int nMin = pCur->pBt->usableSize * 2 / 3;
	u8 aBalanceQuickSpace[13];
	u8 *pFree = 0;

	VVA_ONLY(int balance_quick_called = 0);
	VVA_ONLY(int balance_deeper_called = 0);

	do {
		int iPage = pCur->iPage;
		MemPage *pPage = pCur->apPage[iPage];

		if (iPage == 0) {
			if (pPage->nOverflow) {
				/* The root page of the b-tree is overfull. In this case call the
				 * balance_deeper() function to create a new child for the root-page
				 * and copy the current contents of the root-page to it. The
				 * next iteration of the do-loop will balance the child page.
				 */
				assert(balance_deeper_called == 0);
				VVA_ONLY(balance_deeper_called++);
				rc = balance_deeper(pPage, &pCur->apPage[1]);
				if (rc == SQLITE_OK) {
					pCur->iPage = 1;
					pCur->aiIdx[0] = 0;
					pCur->aiIdx[1] = 0;
					assert(pCur->apPage[1]->nOverflow);
				}
			} else {
				break;
			}
		} else if (pPage->nOverflow == 0 && pPage->nFree <= nMin) {
			break;
		} else {
			MemPage *const pParent = pCur->apPage[iPage - 1];
			int const iIdx = pCur->aiIdx[iPage - 1];

			rc = sqlite3PagerWrite(pParent->pDbPage);
			if (rc == SQLITE_OK) {
#ifndef SQLITE_OMIT_QUICKBALANCE
				if (pPage->intKeyLeaf
				    && pPage->nOverflow == 1
				    && pPage->aiOvfl[0] == pPage->nCell
				    && pParent->pgno != 1
				    && pParent->nCell == iIdx) {
					/* Call balance_quick() to create a new sibling of pPage on which
					 * to store the overflow cell. balance_quick() inserts a new cell
					 * into pParent, which may cause pParent overflow. If this
					 * happens, the next iteration of the do-loop will balance pParent
					 * use either balance_nonroot() or balance_deeper(). Until this
					 * happens, the overflow cell is stored in the aBalanceQuickSpace[]
					 * buffer.
					 *
					 * The purpose of the following assert() is to check that only a
					 * single call to balance_quick() is made for each call to this
					 * function. If this were not verified, a subtle bug involving reuse
					 * of the aBalanceQuickSpace[] might sneak in.
					 */
					assert(balance_quick_called == 0);
					VVA_ONLY(balance_quick_called++);
					rc = balance_quick(pParent, pPage,
							   aBalanceQuickSpace);
				} else
#endif
				{
					/* In this case, call balance_nonroot() to redistribute cells
					 * between pPage and up to 2 of its sibling pages. This involves
					 * modifying the contents of pParent, which may cause pParent to
					 * become overfull or underfull. The next iteration of the do-loop
					 * will balance the parent page to correct this.
					 *
					 * If the parent page becomes overfull, the overflow cell or cells
					 * are stored in the pSpace buffer allocated immediately below.
					 * A subsequent iteration of the do-loop will deal with this by
					 * calling balance_nonroot() (balance_deeper() may be called first,
					 * but it doesn't deal with overflow cells - just moves them to a
					 * different page). Once this subsequent call to balance_nonroot()
					 * has completed, it is safe to release the pSpace buffer used by
					 * the previous call, as the overflow cell data will have been
					 * copied either into the body of a database page or into the new
					 * pSpace buffer passed to the latter call to balance_nonroot().
					 */
					u8 *pSpace =
					    sqlite3PageMalloc(pCur->pBt->
							      pageSize);
					rc = balance_nonroot(pParent, iIdx,
							     pSpace, iPage == 1,
							     pCur->
							     hints &
							     BTREE_BULKLOAD);
					if (pFree) {
						/* If pFree is not NULL, it points to the pSpace buffer used
						 * by a previous call to balance_nonroot(). Its contents are
						 * now stored either on real database pages or within the
						 * new pSpace buffer, so it may be safely freed here.
						 */
						sqlite3PageFree(pFree);
					}

					/* The pSpace buffer will be freed after the next call to
					 * balance_nonroot(), or just before this function returns, whichever
					 * comes first.
					 */
					pFree = pSpace;
				}
			}

			pPage->nOverflow = 0;

			/* The next iteration of the do-loop balances the parent page. */
			releasePage(pPage);
			pCur->iPage--;
			assert(pCur->iPage >= 0);
		}
	} while (rc == SQLITE_OK);

	if (pFree) {
		sqlite3PageFree(pFree);
	}
	return rc;
}

/*
 * Insert a new record into the BTree.  The content of the new record
 * is described by the pX object.  The pCur cursor is used only to
 * define what table the record should be inserted into, and is left
 * pointing at a random location.
 *
 * For a table btree (used for rowid tables), only the pX.nKey value of
 * the key is used. The pX.pKey value must be NULL.  The pX.nKey is the
 * rowid or INTEGER PRIMARY KEY of the row.  The pX.nData,pData,nZero fields
 * hold the content of the row.
 *
 * For an index btree (used for indexes and WITHOUT ROWID tables), the
 * key is an arbitrary byte sequence stored in pX.pKey,nKey.  The
 * pX.pData,nData,nZero fields must be zero.
 *
 * If the seekResult parameter is non-zero, then a successful call to
 * MovetoUnpacked() to seek cursor pCur to (pKey,nKey) has already
 * been performed.  In other words, if seekResult!=0 then the cursor
 * is currently pointing to a cell that will be adjacent to the cell
 * to be inserted.  If seekResult<0 then pCur points to a cell that is
 * smaller then (pKey,nKey).  If seekResult>0 then pCur points to a cell
 * that is larger than (pKey,nKey).
 *
 * If seekResult==0, that means pCur is pointing at some unknown location.
 * In that case, this routine must seek the cursor to the correct insertion
 * point for (pKey,nKey) before doing the insertion.  For index btrees,
 * if pX->nMem is non-zero, then pX->aMem contains pointers to the unpacked
 * key values and pX->aMem can be used instead of pX->pKey to avoid having
 * to decode the key.
 */
int
sqlite3BtreeInsert(BtCursor * pCur,	/* Insert data into the table of this cursor */
		   const BtreePayload * pX,	/* Content of the row to be inserted */
		   int appendBias,	/* True if this is likely an append */
		   int seekResult	/* Result of prior MovetoUnpacked() call */
    )
{
	int rc;
	int loc = seekResult;	/* -1: before desired location  +1: after */
	int szNew = 0;
	int idx;
	MemPage *pPage;
	Btree *p = pCur->pBtree;
	BtShared *pBt = p->pBt;
	unsigned char *oldCell;
	unsigned char *newCell = 0;

	if (pCur->eState == CURSOR_FAULT) {
		assert(pCur->skipNext != SQLITE_OK);
		return pCur->skipNext;
	}

	assert(cursorOwnsBtShared(pCur));
	/* This asserts are disabled as we are not sure if they are necessary or not
	   assert( (pCur->curFlags & BTCF_WriteFlag)!=0
	   && pBt->inTransaction==TRANS_WRITE
	   && (pBt->btsFlags & BTS_READ_ONLY)==0 );
	 */
	assert(hasSharedCacheTableLock
	       (p, pCur->pgnoRoot, pCur->pKeyInfo != 0, 2));

	/* Assert that the caller has been consistent. If this cursor was opened
	 * expecting an index b-tree, then the caller should be inserting blob
	 * keys with no associated data. If the cursor was opened expecting an
	 * intkey table, the caller should be inserting integer keys with a
	 * blob of associated data.
	 */
	assert((pX->pKey == 0) == (pCur->pKeyInfo == 0));

	if (pCur->curFlags & BTCF_TaCursor) {
		return tarantoolSqlite3Insert(pCur, pX);
	}

	/* Save the positions of any other cursors open on this table.
	 *
	 * In some cases, the call to btreeMoveto() below is a no-op. For
	 * example, when inserting data into a table with auto-generated integer
	 * keys, the VDBE layer invokes sqlite3BtreeLast() to figure out the
	 * integer key to use. It then calls this function to actually insert the
	 * data into the intkey B-Tree. In this case btreeMoveto() recognizes
	 * that the cursor is already where it needs to be and returns without
	 * doing any work. To avoid thwarting these optimizations, it is important
	 * not to clear the cursor here.
	 */
	if (pCur->curFlags & BTCF_Multiple) {
		rc = saveAllCursors(pBt, pCur->pgnoRoot, pCur);
		if (rc)
			return rc;
	}

	if (pCur->pKeyInfo == 0) {
		assert(pX->pKey == 0);
		/* If this is an insert into a table b-tree, invalidate any incrblob
		 * cursors open on the row being replaced
		 */
		invalidateIncrblobCursors(p, pX->nKey, 0);

		/* If the cursor is currently on the last row and we are appending a
		 * new row onto the end, set the "loc" to avoid an unnecessary
		 * btreeMoveto() call
		 */
		if ((pCur->curFlags & BTCF_ValidNKey) != 0
		    && pX->nKey == pCur->info.nKey) {
			loc = 0;
		} else if ((pCur->curFlags & BTCF_ValidNKey) != 0
			   && pX->nKey > 0 && pCur->info.nKey == pX->nKey - 1) {
			loc = -1;
		} else if (loc == 0) {
			rc = sqlite3BtreeMovetoUnpacked(pCur, 0, pX->nKey,
							appendBias, &loc);
			if (rc)
				return rc;
		}
	} else if (loc == 0) {
		if (pX->nMem) {
			UnpackedRecord r;
			r.pKeyInfo = pCur->pKeyInfo;
			r.aMem = pX->aMem;
			r.nField = pX->nMem;
			r.default_rc = 0;
			r.errCode = 0;
			r.r1 = 0;
			r.r2 = 0;
			r.eqSeen = 0;
			rc = sqlite3BtreeMovetoUnpacked(pCur, &r, 0, appendBias,
							&loc);
		} else {
			rc = btreeMoveto(pCur, pX->pKey, pX->nKey, appendBias,
					 &loc);
		}
		if (rc)
			return rc;
	}
	assert(pCur->eState == CURSOR_VALID
	       || (pCur->eState == CURSOR_INVALID && loc));

	pPage = pCur->apPage[pCur->iPage];
	assert(pPage->intKey || pX->nKey >= 0);
	assert(pPage->leaf || !pPage->intKey);

	TRACE(("INSERT: table=%d nkey=%lld ndata=%d page=%d %s\n",
	       pCur->pgnoRoot, pX->nKey, pX->nData, pPage->pgno,
	       loc == 0 ? "overwrite" : "new entry"));
	assert(pPage->isInit);
	newCell = pBt->pTmpSpace;
	assert(newCell != 0);
	rc = fillInCell(pPage, newCell, pX, &szNew);
	if (rc)
		goto end_insert;
	assert(szNew == pPage->xCellSize(pPage, newCell));
	assert(szNew <= MX_CELL_SIZE(pBt));
	idx = pCur->aiIdx[pCur->iPage];
	if (loc == 0) {
		CellInfo info;
		assert(idx < pPage->nCell);
		rc = sqlite3PagerWrite(pPage->pDbPage);
		if (rc) {
			goto end_insert;
		}
		oldCell = findCell(pPage, idx);
		if (!pPage->leaf) {
			memcpy(newCell, oldCell, 4);
		}
		rc = clearCell(pPage, oldCell, &info);
		if (info.nSize == szNew && info.nLocal == info.nPayload) {
			/* Overwrite the old cell with the new if they are the same size.
			 * We could also try to do this if the old cell is smaller, then add
			 * the leftover space to the free list.  But experiments show that
			 * doing that is no faster then skipping this optimization and just
			 * calling dropCell() and insertCell().
			 */
			assert(rc == SQLITE_OK);	/* clearCell never fails when nLocal==nPayload */
			if (oldCell + szNew > pPage->aDataEnd)
				return SQLITE_CORRUPT_BKPT;
			memcpy(oldCell, newCell, szNew);
			return SQLITE_OK;
		}
		dropCell(pPage, idx, info.nSize, &rc);
		if (rc)
			goto end_insert;
	} else if (loc < 0 && pPage->nCell > 0) {
		assert(pPage->leaf);
		idx = ++pCur->aiIdx[pCur->iPage];
	} else {
		assert(pPage->leaf);
	}
	insertCell(pPage, idx, newCell, szNew, 0, 0, &rc);
	assert(pPage->nOverflow == 0 || rc == SQLITE_OK);
	assert(rc != SQLITE_OK || pPage->nCell > 0 || pPage->nOverflow > 0);

	/* If no error has occurred and pPage has an overflow cell, call balance()
	 * to redistribute the cells within the tree. Since balance() may move
	 * the cursor, zero the BtCursor.info.nSize and BTCF_ValidNKey
	 * variables.
	 *
	 * Previous versions of SQLite called moveToRoot() to move the cursor
	 * back to the root page as balance() used to invalidate the contents
	 * of BtCursor.apPage[] and BtCursor.aiIdx[]. Instead of doing that,
	 * set the cursor state to "invalid". This makes common insert operations
	 * slightly faster.
	 *
	 * There is a subtle but important optimization here too. When inserting
	 * multiple records into an intkey b-tree using a single cursor (as can
	 * happen while processing an "INSERT INTO ... SELECT" statement), it
	 * is advantageous to leave the cursor pointing to the last entry in
	 * the b-tree if possible. If the cursor is left pointing to the last
	 * entry in the table, and the next row inserted has an integer key
	 * larger than the largest existing key, it is possible to insert the
	 * row without seeking the cursor. This can be a big performance boost.
	 */
	pCur->info.nSize = 0;
	if (pPage->nOverflow) {
		assert(rc == SQLITE_OK);
		pCur->curFlags &= ~(BTCF_ValidNKey);
		rc = balance(pCur);

		/* Must make sure nOverflow is reset to zero even if the balance()
		 * fails. Internal data structure corruption will result otherwise.
		 * Also, set the cursor state to invalid. This stops saveCursorPosition()
		 * from trying to save the current position of the cursor.
		 */
		pCur->apPage[pCur->iPage]->nOverflow = 0;
		pCur->eState = CURSOR_INVALID;
	}
	assert(pCur->apPage[pCur->iPage]->nOverflow == 0);

 end_insert:
	return rc;
}

/*
 * Delete the entry that the cursor is pointing to.
 *
 * If the BTREE_SAVEPOSITION bit of the flags parameter is zero, then
 * the cursor is left pointing at an arbitrary location after the delete.
 * But if that bit is set, then the cursor is left in a state such that
 * the next call to BtreeNext() or BtreePrev() moves it to the same row
 * as it would have been on if the call to BtreeDelete() had been omitted.
 *
 * The BTREE_AUXDELETE bit of flags indicates that is one of several deletes
 * associated with a single table entry and its indexes.  Only one of those
 * deletes is considered the "primary" delete.  The primary delete occurs
 * on a cursor that is not a BTREE_FORDELETE cursor.  All but one delete
 * operation on non-FORDELETE cursors is tagged with the AUXDELETE flag.
 * The BTREE_AUXDELETE bit is a hint that is not used by this implementation,
 * but which might be used by alternative storage engines.
 */
int
sqlite3BtreeDelete(BtCursor * pCur, u8 flags)
{
	Btree *p = pCur->pBtree;
	BtShared *pBt = p->pBt;
	int rc;			/* Return code */
	MemPage *pPage;		/* Page to delete cell from */
	unsigned char *pCell;	/* Pointer to cell to delete */
	int iCellIdx;		/* Index of cell to delete */
	int iCellDepth;		/* Depth of node containing pCell */
	CellInfo info;		/* Size of the cell being deleted */
	int bSkipnext = 0;	/* Leaf cursor in SKIPNEXT state */
	u8 bPreserve = flags & BTREE_SAVEPOSITION;	/* Keep cursor valid */

	assert(cursorOwnsBtShared(pCur));
	/* This asserts are disabled as we are not sure if they are necessary or not
	   assert( pBt->inTransaction==TRANS_WRITE );
	   assert( (pBt->btsFlags & BTS_READ_ONLY)==0 );
	   assert( pCur->curFlags & BTCF_WriteFlag );
	 */
	assert(hasSharedCacheTableLock
	       (p, pCur->pgnoRoot, pCur->pKeyInfo != 0, 2));
	assert(!hasReadConflicts(p, pCur->pgnoRoot));
	assert(pCur->eState == CURSOR_VALID);
	assert((flags & ~(BTREE_SAVEPOSITION | BTREE_AUXDELETE)) == 0);

	if (pCur->curFlags & BTCF_TaCursor) {
		return tarantoolSqlite3Delete(pCur, flags);
	}

	assert(pCur->aiIdx[pCur->iPage] < pCur->apPage[pCur->iPage]->nCell);

	iCellDepth = pCur->iPage;
	iCellIdx = pCur->aiIdx[iCellDepth];
	pPage = pCur->apPage[iCellDepth];
	pCell = findCell(pPage, iCellIdx);

	/* If the bPreserve flag is set to true, then the cursor position must
	 * be preserved following this delete operation. If the current delete
	 * will cause a b-tree rebalance, then this is done by saving the cursor
	 * key and leaving the cursor in CURSOR_REQUIRESEEK state before
	 * returning.
	 *
	 * Or, if the current delete will not cause a rebalance, then the cursor
	 * will be left in CURSOR_SKIPNEXT state pointing to the entry immediately
	 * before or after the deleted entry. In this case set bSkipnext to true.
	 */
	if (bPreserve) {
		if (!pPage->leaf
		    || (pPage->nFree + cellSizePtr(pPage, pCell) + 2) >
		    (int)(pBt->usableSize * 2 / 3)
		    ) {
			/* A b-tree rebalance will be required after deleting this entry.
			 * Save the cursor key.
			 */
			rc = saveCursorKey(pCur);
			if (rc)
				return rc;
		} else {
			bSkipnext = 1;
		}
	}

	/* If the page containing the entry to delete is not a leaf page, move
	 * the cursor to the largest entry in the tree that is smaller than
	 * the entry being deleted. This cell will replace the cell being deleted
	 * from the internal node. The 'previous' entry is used for this instead
	 * of the 'next' entry, as the previous entry is always a part of the
	 * sub-tree headed by the child page of the cell being deleted. This makes
	 * balancing the tree following the delete operation easier.
	 */
	if (!pPage->leaf) {
		int notUsed = 0;
		rc = sqlite3BtreePrevious(pCur, &notUsed);
		if (rc)
			return rc;
	}

	/* Save the positions of any other cursors open on this table before
	 * making any modifications.
	 */
	if (pCur->curFlags & BTCF_Multiple) {
		rc = saveAllCursors(pBt, pCur->pgnoRoot, pCur);
		if (rc)
			return rc;
	}

	/* If this is a delete operation to remove a row from a table b-tree,
	 * invalidate any incrblob cursors open on the row being deleted.
	 */
	if (pCur->pKeyInfo == 0) {
		invalidateIncrblobCursors(p, pCur->info.nKey, 0);
	}

	/* Make the page containing the entry to be deleted writable. Then free any
	 * overflow pages associated with the entry and finally remove the cell
	 * itself from within the page.
	 */
	rc = sqlite3PagerWrite(pPage->pDbPage);
	if (rc)
		return rc;
	rc = clearCell(pPage, pCell, &info);
	dropCell(pPage, iCellIdx, info.nSize, &rc);
	if (rc)
		return rc;

	/* If the cell deleted was not located on a leaf page, then the cursor
	 * is currently pointing to the largest entry in the sub-tree headed
	 * by the child-page of the cell that was just deleted from an internal
	 * node. The cell from the leaf node needs to be moved to the internal
	 * node to replace the deleted cell.
	 */
	if (!pPage->leaf) {
		MemPage *pLeaf = pCur->apPage[pCur->iPage];
		int nCell;
		Pgno n = pCur->apPage[iCellDepth + 1]->pgno;
		unsigned char *pTmp;

		pCell = findCell(pLeaf, pLeaf->nCell - 1);
		if (pCell < &pLeaf->aData[4])
			return SQLITE_CORRUPT_BKPT;
		nCell = pLeaf->xCellSize(pLeaf, pCell);
		assert(MX_CELL_SIZE(pBt) >= nCell);
		pTmp = pBt->pTmpSpace;
		assert(pTmp != 0);
		rc = sqlite3PagerWrite(pLeaf->pDbPage);
		if (rc == SQLITE_OK) {
			insertCell(pPage, iCellIdx, pCell - 4, nCell + 4, pTmp,
				   n, &rc);
		}
		dropCell(pLeaf, pLeaf->nCell - 1, nCell, &rc);
		if (rc)
			return rc;
	}

	/* Balance the tree. If the entry deleted was located on a leaf page,
	 * then the cursor still points to that page. In this case the first
	 * call to balance() repairs the tree, and the if(...) condition is
	 * never true.
	 *
	 * Otherwise, if the entry deleted was on an internal node page, then
	 * pCur is pointing to the leaf page from which a cell was removed to
	 * replace the cell deleted from the internal node. This is slightly
	 * tricky as the leaf node may be underfull, and the internal node may
	 * be either under or overfull. In this case run the balancing algorithm
	 * on the leaf node first. If the balance proceeds far enough up the
	 * tree that we can be sure that any problem in the internal node has
	 * been corrected, so be it. Otherwise, after balancing the leaf node,
	 * walk the cursor up the tree to the internal node and balance it as
	 * well.
	 */
	rc = balance(pCur);
	if (rc == SQLITE_OK && pCur->iPage > iCellDepth) {
		while (pCur->iPage > iCellDepth) {
			releasePage(pCur->apPage[pCur->iPage--]);
		}
		rc = balance(pCur);
	}

	if (rc == SQLITE_OK) {
		if (bSkipnext) {
			assert(bPreserve
			       && (pCur->iPage == iCellDepth || CORRUPT_DB));
			assert(pPage == pCur->apPage[pCur->iPage]
			       || CORRUPT_DB);
			assert((pPage->nCell > 0 || CORRUPT_DB)
			       && iCellIdx <= pPage->nCell);
			pCur->eState = CURSOR_SKIPNEXT;
			if (iCellIdx >= pPage->nCell) {
				pCur->skipNext = -1;
				pCur->aiIdx[iCellDepth] = pPage->nCell - 1;
			} else {
				pCur->skipNext = 1;
			}
		} else {
			rc = moveToRoot(pCur);
			if (bPreserve) {
				pCur->eState = CURSOR_REQUIRESEEK;
			}
		}
	}
	return rc;
}

/*
 * Create a new BTree table.  Write into *piTable the page
 * number for the root page of the new table.
 *
 * The type of type is determined by the flags parameter.  Only the
 * following values of flags are currently in use.  Other values for
 * flags might not work:
 *
 *     BTREE_INTKEY|BTREE_LEAFDATA     Used for SQL tables with rowid keys
 *     BTREE_ZERODATA                  Used for SQL indices
 */
static int
btreeCreateTable(Btree * p, int *piTable, int createTabFlags)
{
	BtShared *pBt = p->pBt;
	MemPage *pRoot;
	Pgno pgnoRoot;
	int rc;
	int ptfFlags;		/* Page-type flage for the root page of new table */

	assert(sqlite3BtreeHoldsMutex(p));
	assert(pBt->inTransaction == TRANS_WRITE);
	assert((pBt->btsFlags & BTS_READ_ONLY) == 0);

	rc = allocateBtreePage(pBt, &pRoot, &pgnoRoot, 1, 0);
	if (rc) {
		return rc;
	}
	assert(sqlite3PagerIswriteable(pRoot->pDbPage));
	if (createTabFlags & BTREE_INTKEY) {
		ptfFlags = PTF_INTKEY | PTF_LEAFDATA | PTF_LEAF;
	} else {
		ptfFlags = PTF_ZERODATA | PTF_LEAF;
	}
	zeroPage(pRoot, ptfFlags);
	sqlite3PagerUnref(pRoot->pDbPage);
	assert((pBt->openFlags & BTREE_SINGLE) == 0 || pgnoRoot == 2);
	*piTable = (int)pgnoRoot;
	return SQLITE_OK;
}

int
sqlite3BtreeCreateTable(Btree * p, int *piTable, int flags)
{
	int rc;
	sqlite3BtreeEnter(p);
	rc = btreeCreateTable(p, piTable, flags);
	sqlite3BtreeLeave(p);
	return rc;
}

/*
 * Erase the given database page and all its children.  Return
 * the page to the freelist.
 */
static int
clearDatabasePage(BtShared * pBt,	/* The BTree that contains the table */
		  Pgno pgno,	/* Page number to clear */
		  int freePageFlag,	/* Deallocate page if true */
		  int *pnChange	/* Add number of Cells freed to this counter */
    )
{
	MemPage *pPage;
	int rc;
	unsigned char *pCell;
	int i;
	int hdr;
	CellInfo info;

	assert(sqlite3_mutex_held(pBt->mutex));
	if (pgno > btreePagecount(pBt)) {
		return SQLITE_CORRUPT_BKPT;
	}
	rc = getAndInitPage(pBt, pgno, &pPage, 0, 0);
	if (rc)
		return rc;
	if (pPage->bBusy) {
		rc = SQLITE_CORRUPT_BKPT;
		goto cleardatabasepage_out;
	}
	pPage->bBusy = 1;
	hdr = pPage->hdrOffset;
	for (i = 0; i < pPage->nCell; i++) {
		pCell = findCell(pPage, i);
		if (!pPage->leaf) {
			rc = clearDatabasePage(pBt, get4byte(pCell), 1,
					       pnChange);
			if (rc)
				goto cleardatabasepage_out;
		}
		rc = clearCell(pPage, pCell, &info);
		if (rc)
			goto cleardatabasepage_out;
	}
	if (!pPage->leaf) {
		rc = clearDatabasePage(pBt, get4byte(&pPage->aData[hdr + 8]), 1,
				       pnChange);
		if (rc)
			goto cleardatabasepage_out;
	} else if (pnChange) {
		assert(pPage->intKey || CORRUPT_DB);
		testcase(!pPage->intKey);
		*pnChange += pPage->nCell;
	}
	if (freePageFlag) {
		freePage(pPage, &rc);
	} else if ((rc = sqlite3PagerWrite(pPage->pDbPage)) == 0) {
		zeroPage(pPage, pPage->aData[hdr] | PTF_LEAF);
	}

 cleardatabasepage_out:
	pPage->bBusy = 0;
	releasePage(pPage);
	return rc;
}

/*
 * Delete all information from a single table in the database.  iTable is
 * the page number of the root of the table.  After this routine returns,
 * the root page is empty, but still exists.
 *
 * This routine will fail with SQLITE_LOCKED if there are any open
 * read cursors on the table.  Open write cursors are moved to the
 * root of the table.
 *
 * If pnChange is not NULL, then table iTable must be an intkey table. The
 * integer value pointed to by pnChange is incremented by the number of
 * entries in the table.
 */
int
sqlite3BtreeClearTable(Btree * p, int iTable, int *pnChange)
{
	int rc;
	BtShared *pBt = p->pBt;
	sqlite3BtreeEnter(p);
	assert(p->inTrans == TRANS_WRITE);

	rc = saveAllCursors(pBt, (Pgno) iTable, 0);

	if (SQLITE_OK == rc) {
		/* Invalidate all incrblob cursors open on table iTable (assuming iTable
		 * is the root of a table b-tree - if it is not, the following call is
		 * a no-op).
		 */
		invalidateIncrblobCursors(p, 0, 1);
		rc = clearDatabasePage(pBt, (Pgno) iTable, 0, pnChange);
	}
	sqlite3BtreeLeave(p);
	return rc;
}

/*
 * Delete all information from the single table that pCur is open on.
 *
 * This routine only work for pCur on an ephemeral table.
 */
int
sqlite3BtreeClearTableOfCursor(BtCursor * pCur)
{
	return sqlite3BtreeClearTable(pCur->pBtree, pCur->pgnoRoot, 0);
}

/*
 * Erase all information in a table and add the root of the table to
 * the freelist.  Except, the root of the principle table (the one on
 * page 1) is never added to the freelist.
 *
 * This routine will fail with SQLITE_LOCKED if there are any open
 * cursors on the table.
 */
static int
btreeDropTable(Btree * p, Pgno iTable, int *piMoved)
{
	int rc;
	MemPage *pPage = 0;
	BtShared *pBt = p->pBt;

	assert(sqlite3BtreeHoldsMutex(p));
	assert(p->inTrans == TRANS_WRITE);
	assert(iTable >= 2);

	rc = btreeGetPage(pBt, (Pgno) iTable, &pPage, 0);
	if (rc)
		return rc;
	rc = sqlite3BtreeClearTable(p, iTable, 0);
	if (rc) {
		releasePage(pPage);
		return rc;
	}

	*piMoved = 0;

	freePage(pPage, &rc);
	releasePage(pPage);

	return rc;
}

int
sqlite3BtreeDropTable(Btree * p, int iTable, int *piMoved)
{
	int rc;
	sqlite3BtreeEnter(p);
	rc = btreeDropTable(p, iTable, piMoved);
	sqlite3BtreeLeave(p);
	return rc;
}

/*
 * This function may only be called if the b-tree connection already
 * has a read or write transaction open on the database.
 *
 * Read the meta-information out of a database file.  Meta[0]
 * is the number of free pages currently in the database.  Meta[1]
 * through meta[15] are available for use by higher layers.  Meta[0]
 * is read-only, the others are read/write.
 *
 * The schema layer numbers meta values differently.  At the schema
 * layer (and the SetCookie and ReadCookie opcodes) the number of
 * free pages is not visible.  So Cookie[0] is the same as Meta[1].
 *
 * This routine treats Meta[BTREE_DATA_VERSION] as a special case.  Instead
 * of reading the value out of the header, it instead loads the "DataVersion"
 * from the pager.  The BTREE_DATA_VERSION value is not actually stored in the
 * database file.  It is a number computed by the pager.  But its access
 * pattern is the same as header meta values, and so it is convenient to
 * read it from this routine.
 */
void
sqlite3BtreeGetMeta(Btree * p, int idx, u32 * pMeta)
{
	BtShared *pBt = p->pBt;

	sqlite3BtreeEnter(p);
	assert(p->inTrans > TRANS_NONE);
	assert(SQLITE_OK ==
	       querySharedCacheTableLock(p, MASTER_ROOT, READ_LOCK));
	assert(pBt->pPage1);
	assert(idx >= 0 && idx <= 15);

	if (idx == BTREE_DATA_VERSION) {
		*pMeta = sqlite3PagerDataVersion(pBt->pPager) + p->iDataVersion;
	} else {
		*pMeta = get4byte(&pBt->pPage1->aData[36 + idx * 4]);
	}

	sqlite3BtreeLeave(p);
}

/*
 * Write meta-information back into the database.  Meta[0] is
 * read-only and may not be written.
 */
int
sqlite3BtreeUpdateMeta(Btree * p, int idx, u32 iMeta)
{
	BtShared *pBt = p->pBt;
	unsigned char *pP1;
	int rc;
	assert(idx >= 1 && idx <= 15);
	sqlite3BtreeEnter(p);
	/* This asserts are disabled as we are not sure if they are necessary or not
	   assert( p->inTrans==TRANS_WRITE );
	 */
	assert(pBt->pPage1 != 0);
	pP1 = pBt->pPage1->aData;
	rc = sqlite3PagerWrite(pBt->pPage1->pDbPage);
	if (rc == SQLITE_OK) {
		put4byte(&pP1[36 + idx * 4], iMeta);
	}
	sqlite3BtreeLeave(p);
	return rc;
}

#ifndef SQLITE_OMIT_BTREECOUNT
/*
 * The first argument, pCur, is a cursor opened on some b-tree. Count the
 * number of entries in the b-tree and write the result to *pnEntry.
 *
 * SQLITE_OK is returned if the operation is successfully executed.
 * Otherwise, if an error is encountered (i.e. an IO error or database
 * corruption) an SQLite error code is returned.
 */
int
sqlite3BtreeCount(BtCursor * pCur, i64 * pnEntry)
{
	i64 nEntry = 0;		/* Value to return in *pnEntry */
	int rc;			/* Return code */

	if (pCur->curFlags & BTCF_TaCursor) {
		return tarantoolSqlite3Count(pCur, pnEntry);
	}

	if (pCur->pgnoRoot == 0) {
		*pnEntry = 0;
		return SQLITE_OK;
	}
	rc = moveToRoot(pCur);

	/* Unless an error occurs, the following loop runs one iteration for each
	 * page in the B-Tree structure (not including overflow pages).
	 */
	while (rc == SQLITE_OK) {
		int iIdx;	/* Index of child node in parent */
		MemPage *pPage;	/* Current page of the b-tree */

		/* If this is a leaf page or the tree is not an int-key tree, then
		 * this page contains countable entries. Increment the entry counter
		 * accordingly.
		 */
		pPage = pCur->apPage[pCur->iPage];
		if (pPage->leaf || !pPage->intKey) {
			nEntry += pPage->nCell;
		}

		/* pPage is a leaf node. This loop navigates the cursor so that it
		 * points to the first interior cell that it points to the parent of
		 * the next page in the tree that has not yet been visited. The
		 * pCur->aiIdx[pCur->iPage] value is set to the index of the parent cell
		 * of the page, or to the number of cells in the page if the next page
		 * to visit is the right-child of its parent.
		 *
		 * If all pages in the tree have been visited, return SQLITE_OK to the
		 * caller.
		 */
		if (pPage->leaf) {
			do {
				if (pCur->iPage == 0) {
					/* All pages of the b-tree have been visited. Return successfully. */
					*pnEntry = nEntry;
					return moveToRoot(pCur);
				}
				moveToParent(pCur);
			} while (pCur->aiIdx[pCur->iPage] >=
				 pCur->apPage[pCur->iPage]->nCell);

			pCur->aiIdx[pCur->iPage]++;
			pPage = pCur->apPage[pCur->iPage];
		}

		/* Descend to the child node of the cell that the cursor currently
		 * points at. This is the right-child if (iIdx==pPage->nCell).
		 */
		iIdx = pCur->aiIdx[pCur->iPage];
		if (iIdx == pPage->nCell) {
			rc = moveToChild(pCur,
					 get4byte(&pPage->
						  aData[pPage->hdrOffset + 8]));
		} else {
			rc = moveToChild(pCur, get4byte(findCell(pPage, iIdx)));
		}
	}

	/* An error has occurred. Return an error code. */
	return rc;
}
#endif

/*
 * Return the pager associated with a BTree.  This routine is used for
 * testing and debugging only.
 */
Pager *
sqlite3BtreePager(Btree * p)
{
	return p->pBt->pPager;
}

#ifndef SQLITE_OMIT_INTEGRITY_CHECK
/*
 * Append a message to the error message string.
 */
static void
checkAppendMsg(IntegrityCk * pCheck, const char *zFormat, ...
    )
{
	va_list ap;
	if (!pCheck->mxErr)
		return;
	pCheck->mxErr--;
	pCheck->nErr++;
	va_start(ap, zFormat);
	if (pCheck->errMsg.nChar) {
		sqlite3StrAccumAppend(&pCheck->errMsg, "\n", 1);
	}
	if (pCheck->zPfx) {
		sqlite3XPrintf(&pCheck->errMsg, pCheck->zPfx, pCheck->v1,
			       pCheck->v2);
	}
	sqlite3VXPrintf(&pCheck->errMsg, zFormat, ap);
	va_end(ap);
	if (pCheck->errMsg.accError == STRACCUM_NOMEM) {
		pCheck->mallocFailed = 1;
	}
}
#endif				/* SQLITE_OMIT_INTEGRITY_CHECK */

#ifndef SQLITE_OMIT_INTEGRITY_CHECK

/*
 * Return non-zero if the bit in the IntegrityCk.aPgRef[] array that
 * corresponds to page iPg is already set.
 */
static int
getPageReferenced(IntegrityCk * pCheck, Pgno iPg)
{
	assert(iPg <= pCheck->nPage && sizeof(pCheck->aPgRef[0]) == 1);
	return (pCheck->aPgRef[iPg / 8] & (1 << (iPg & 0x07)));
}

/*
 * Set the bit in the IntegrityCk.aPgRef[] array that corresponds to page iPg.
 */
static void
setPageReferenced(IntegrityCk * pCheck, Pgno iPg)
{
	assert(iPg <= pCheck->nPage && sizeof(pCheck->aPgRef[0]) == 1);
	pCheck->aPgRef[iPg / 8] |= (1 << (iPg & 0x07));
}

/*
 * Add 1 to the reference count for page iPage.  If this is the second
 * reference to the page, add an error message to pCheck->zErrMsg.
 * Return 1 if there are 2 or more references to the page and 0 if
 * if this is the first reference to the page.
 *
 * Also check that the page number is in bounds.
 */
static int
checkRef(IntegrityCk * pCheck, Pgno iPage)
{
	if (iPage == 0)
		return 1;
	if (iPage > pCheck->nPage) {
		checkAppendMsg(pCheck, "invalid page number %d", iPage);
		return 1;
	}
	if (getPageReferenced(pCheck, iPage)) {
		checkAppendMsg(pCheck, "2nd reference to page %d", iPage);
		return 1;
	}
	setPageReferenced(pCheck, iPage);
	return 0;
}

/*
 * Check the integrity of the freelist or of an overflow page list.
 * Verify that the number of pages on the list is N.
 */
static void
checkList(IntegrityCk * pCheck,	/* Integrity checking context */
	  int isFreeList,	/* True for a freelist.  False for overflow page list */
	  int iPage,		/* Page number for first page in the list */
	  int N			/* Expected number of pages in the list */
    )
{
	int i;
	int expected = N;
	int iFirst = iPage;
	while (N-- > 0 && pCheck->mxErr) {
		DbPage *pOvflPage;
		unsigned char *pOvflData;
		if (iPage < 1) {
			checkAppendMsg(pCheck,
				       "%d of %d pages missing from overflow list starting at %d",
				       N + 1, expected, iFirst);
			break;
		}
		if (checkRef(pCheck, iPage))
			break;
		if (sqlite3PagerGet
		    (pCheck->pPager, (Pgno) iPage, &pOvflPage, 0)) {
			checkAppendMsg(pCheck, "failed to get page %d", iPage);
			break;
		}
		pOvflData = (unsigned char *)sqlite3PagerGetData(pOvflPage);
		if (isFreeList) {
			int n = get4byte(&pOvflData[4]);
			if (n > (int)pCheck->pBt->usableSize / 4 - 2) {
				checkAppendMsg(pCheck,
					       "freelist leaf count too big on page %d",
					       iPage);
				N--;
			} else {
				for (i = 0; i < n; i++) {
					Pgno iFreePage =
					    get4byte(&pOvflData[8 + i * 4]);
					checkRef(pCheck, iFreePage);
				}
				N -= n;
			}
		}
		iPage = get4byte(pOvflData);
		sqlite3PagerUnref(pOvflPage);

		if (isFreeList && N < (iPage != 0)) {
			checkAppendMsg(pCheck,
				       "free-page count in header is too small");
		}
	}
}
#endif				/* SQLITE_OMIT_INTEGRITY_CHECK */

#ifndef SQLITE_OMIT_INTEGRITY_CHECK
/*
 * This routine does a complete check of the given BTree file.
 * A read-only or read-write transaction must be opened before calling
 * this function.
 *
 * Write the number of error seen in *pnErr.  Except for some memory
 * allocation errors,  an error message held in memory obtained from
 * malloc is returned if *pnErr is non-zero.  If *pnErr==0 then NULL is
 * returned.  If a memory allocation error occurs, NULL is returned.
 */
char *
sqlite3BtreeIntegrityCheck(Btree * p,	/* The btree to be checked */
			   int mxErr,	/* Stop reporting errors after this many */
			   int *pnErr	/* Write number of errors seen to this variable */
    )
{
	Pgno i;
	IntegrityCk sCheck;
	BtShared *pBt = p->pBt;
	char zErr[100];
	VVA_ONLY(int nRef);

	sqlite3BtreeEnter(p);
	assert(p->inTrans > TRANS_NONE && pBt->inTransaction > TRANS_NONE);
	VVA_ONLY(nRef = sqlite3PagerRefcount(pBt->pPager));
	assert(nRef >= 0);
	sCheck.pBt = pBt;
	sCheck.pPager = pBt->pPager;
	sCheck.nPage = btreePagecount(sCheck.pBt);
	sCheck.mxErr = mxErr;
	sCheck.nErr = 0;
	sCheck.mallocFailed = 0;
	sCheck.zPfx = 0;
	sCheck.v1 = 0;
	sCheck.v2 = 0;
	sCheck.aPgRef = 0;
	sCheck.heap = 0;
	sqlite3StrAccumInit(&sCheck.errMsg, 0, zErr, sizeof(zErr),
			    SQLITE_MAX_LENGTH);
	sCheck.errMsg.printfFlags = SQLITE_PRINTF_INTERNAL;
	if (sCheck.nPage == 0) {
		goto integrity_ck_cleanup;
	}

	sCheck.aPgRef = sqlite3MallocZero((sCheck.nPage / 8) + 1);
	if (!sCheck.aPgRef) {
		sCheck.mallocFailed = 1;
		goto integrity_ck_cleanup;
	}
	sCheck.heap = (u32 *) sqlite3PageMalloc(pBt->pageSize);
	if (sCheck.heap == 0) {
		sCheck.mallocFailed = 1;
		goto integrity_ck_cleanup;
	}

	i = PENDING_BYTE_PAGE(pBt);
	if (i <= sCheck.nPage)
		setPageReferenced(&sCheck, i);

	/* Check the integrity of the freelist
	 */
	sCheck.zPfx = "Main freelist: ";
	checkList(&sCheck, 1, get4byte(&pBt->pPage1->aData[32]),
		  get4byte(&pBt->pPage1->aData[36]));
	sCheck.zPfx = 0;

	/* Make sure every page in the file is referenced
	 */
	for (i = 1; i <= sCheck.nPage && sCheck.mxErr; i++) {
		if (getPageReferenced(&sCheck, i) == 0) {
			checkAppendMsg(&sCheck, "Page %d is never used", i);
		}
	}

	/* Clean  up and report errors.
	 */
 integrity_ck_cleanup:
	sqlite3PageFree(sCheck.heap);
	sqlite3_free(sCheck.aPgRef);
	if (sCheck.mallocFailed) {
		sqlite3StrAccumReset(&sCheck.errMsg);
		sCheck.nErr++;
	}
	*pnErr = sCheck.nErr;
	if (sCheck.nErr == 0)
		sqlite3StrAccumReset(&sCheck.errMsg);
	/* Make sure this analysis did not leave any unref() pages. */
	assert(nRef == sqlite3PagerRefcount(pBt->pPager));
	sqlite3BtreeLeave(p);
	return sqlite3StrAccumFinish(&sCheck.errMsg);
}
#endif				/* SQLITE_OMIT_INTEGRITY_CHECK */

/*
 * Return the full pathname of the underlying database file.  Return
 * an empty string if the database is in-memory or a TEMP database.
 *
 * The pager filename is invariant as long as the pager is
 * open so it is safe to access without the BtShared mutex.
 */
const char *
sqlite3BtreeGetFilename(Btree * p)
{
	assert(p->pBt->pPager != 0);
	return sqlite3PagerFilename(p->pBt->pPager, 1);
}

/*
 * Return non-zero if a transaction is active.
 */
int
sqlite3BtreeIsInTrans(Btree * p)
{
	assert(p == 0 || sqlite3_mutex_held(p->db->mutex));
	return (p && (p->inTrans == TRANS_WRITE));
}

/*
 * Return non-zero if a read (or write) transaction is active.
 */
int
sqlite3BtreeIsInReadTrans(Btree * p)
{
	assert(p);
	assert(sqlite3_mutex_held(p->db->mutex));
	return p->inTrans != TRANS_NONE;
}

/*
 * This function returns a pointer to a blob of memory associated with
 * a single shared-btree. The memory is used by client code for its own
 * purposes (for example, to store a high-level schema associated with
 * the shared-btree). The btree layer manages reference counting issues.
 *
 * The first time this is called on a shared-btree, nBytes bytes of memory
 * are allocated, zeroed, and returned to the caller. For each subsequent
 * call the nBytes parameter is ignored and a pointer to the same blob
 * of memory returned.
 *
 * If the nBytes parameter is 0 and the blob of memory has not yet been
 * allocated, a null pointer is returned. If the blob has already been
 * allocated, it is returned as normal.
 *
 * Just before the shared-btree is closed, the function passed as the
 * xFree argument when the memory allocation was made is invoked on the
 * blob of allocated memory. The xFree function should not call sqlite3_free()
 * on the memory, the btree layer does that.
 */
void *
sqlite3BtreeSchema(Btree * p, int nBytes, void (*xFree) (void *))
{
	BtShared *pBt = p->pBt;
	sqlite3BtreeEnter(p);
	if (!pBt->pSchema && nBytes) {
		pBt->pSchema = sqlite3DbMallocZero(0, nBytes);
		pBt->xFreeSchema = xFree;
	}
	sqlite3BtreeLeave(p);
	return pBt->pSchema;
}

/*
 * Return SQLITE_LOCKED_SHAREDCACHE if another user of the same shared
 * btree as the argument handle holds an exclusive lock on the
 * sqlite_master table. Otherwise SQLITE_OK.
 */
int
sqlite3BtreeSchemaLocked(Btree * p)
{
	int rc;
	assert(sqlite3_mutex_held(p->db->mutex));
	sqlite3BtreeEnter(p);
	rc = querySharedCacheTableLock(p, MASTER_ROOT, READ_LOCK);
	assert(rc == SQLITE_OK || rc == SQLITE_LOCKED_SHAREDCACHE);
	sqlite3BtreeLeave(p);
	return rc;
}

#ifndef SQLITE_OMIT_SHARED_CACHE
/*
 * Obtain a lock on the table whose root page is iTab.  The
 * lock is a write lock if isWritelock is true or a read lock
 * if it is false.
 */
int
sqlite3BtreeLockTable(Btree * p, int iTab, u8 isWriteLock)
{
	int rc = SQLITE_OK;
	assert(p->inTrans != TRANS_NONE);
	if (p->sharable) {
		u8 lockType = READ_LOCK + isWriteLock;
		assert(READ_LOCK + 1 == WRITE_LOCK);
		assert(isWriteLock == 0 || isWriteLock == 1);

		sqlite3BtreeEnter(p);
		rc = querySharedCacheTableLock(p, iTab, lockType);
		if (rc == SQLITE_OK) {
			rc = setSharedCacheTableLock(p, iTab, lockType);
		}
		sqlite3BtreeLeave(p);
	}
	return rc;
}
#endif

#ifndef SQLITE_OMIT_INCRBLOB
/*
 * Argument pCsr must be a cursor opened for writing on an
 * INTKEY table currently pointing at a valid table entry.
 * This function modifies the data stored as part of that entry.
 *
 * Only the data content may only be modified, it is not possible to
 * change the length of the data stored. If this function is called with
 * parameters that attempt to write past the end of the existing data,
 * no modifications are made and SQLITE_CORRUPT is returned.
 */
int
sqlite3BtreePutData(BtCursor * pCsr, u32 offset, u32 amt, void *z)
{
	int rc;
	assert(cursorOwnsBtShared(pCsr));
	assert(sqlite3_mutex_held(pCsr->pBtree->db->mutex));
	assert(pCsr->curFlags & BTCF_Incrblob);

	rc = restoreCursorPosition(pCsr);
	if (rc != SQLITE_OK) {
		return rc;
	}
	assert(pCsr->eState != CURSOR_REQUIRESEEK);
	if (pCsr->eState != CURSOR_VALID) {
		return SQLITE_ABORT;
	}

	/* Save the positions of all other cursors open on this table. This is
	 * required in case any of them are holding references to an xFetch
	 * version of the b-tree page modified by the accessPayload call below.
	 *
	 * Note that pCsr must be open on a INTKEY table and saveCursorPosition()
	 * and hence saveAllCursors() cannot fail on a BTREE_INTKEY table, hence
	 * saveAllCursors can only return SQLITE_OK.
	 */
	VVA_ONLY(rc =) saveAllCursors(pCsr->pBt, pCsr->pgnoRoot, pCsr);
	assert(rc == SQLITE_OK);

	/* Check some assumptions:
	 *   (a) the cursor is open for writing,
	 *   (b) there is a read/write transaction open,
	 *   (c) the connection holds a write-lock on the table (if required),
	 *   (d) there are no conflicting read-locks, and
	 *   (e) the cursor points at a valid row of an intKey table.
	 */
	if ((pCsr->curFlags & BTCF_WriteFlag) == 0) {
		return SQLITE_READONLY;
	}
	assert((pCsr->pBt->btsFlags & BTS_READ_ONLY) == 0
	       && pCsr->pBt->inTransaction == TRANS_WRITE);
	assert(hasSharedCacheTableLock(pCsr->pBtree, pCsr->pgnoRoot, 0, 2));
	assert(!hasReadConflicts(pCsr->pBtree, pCsr->pgnoRoot));
	assert(pCsr->apPage[pCsr->iPage]->intKey);

	return accessPayload(pCsr, offset, amt, (unsigned char *)z, 1);
}

/*
 * Mark this cursor as an incremental blob cursor.
 */
void
sqlite3BtreeIncrblobCursor(BtCursor * pCur)
{
	pCur->curFlags |= BTCF_Incrblob;
	pCur->pBtree->hasIncrblobCur = 1;
}
#endif

/*
 * Set both the "read version" (single byte at byte offset 18) and
 * "write version" (single byte at byte offset 19) fields in the database
 * header to iVersion.
 */
int
sqlite3BtreeSetVersion(Btree * pBtree, int iVersion)
{
	BtShared *pBt = pBtree->pBt;
	int rc;			/* Return code */

	assert(iVersion == 1 || iVersion == 2);

	/* If setting the version fields to 1, do not automatically open the
	 * WAL connection, even if the version fields are currently set to 2.
	 */
	pBt->btsFlags &= ~BTS_NO_WAL;
	if (iVersion == 1)
		pBt->btsFlags |= BTS_NO_WAL;

	rc = sqlite3BtreeBeginTrans(pBtree, 0, 0);
	if (rc == SQLITE_OK) {
		u8 *aData = pBt->pPage1->aData;
		if (aData[18] != (u8) iVersion || aData[19] != (u8) iVersion) {
			rc = sqlite3BtreeBeginTrans(pBtree, 0, 2);
			if (rc == SQLITE_OK) {
				rc = sqlite3PagerWrite(pBt->pPage1->pDbPage);
				if (rc == SQLITE_OK) {
					aData[18] = (u8) iVersion;
					aData[19] = (u8) iVersion;
				}
			}
		}
	}

	pBt->btsFlags &= ~BTS_NO_WAL;
	return rc;
}

/*
 * Return true if the cursor has a hint specified.  This routine is
 * only used from within assert() statements
 */
int
sqlite3BtreeCursorHasHint(BtCursor * pCsr, unsigned int mask)
{
	return (pCsr->hints & mask) != 0;
}

/*
 * Return true if the given Btree is read-only.
 */
int
sqlite3BtreeIsReadonly(Btree * p)
{
	return (p->pBt->btsFlags & BTS_READ_ONLY) != 0;
}

/*
 * Return the size of the header added to each page by this module.
 */
int
sqlite3HeaderSizeBtree(void)
{
	return ROUND8(sizeof(MemPage));
}

#if !defined(SQLITE_OMIT_SHARED_CACHE)
/*
 * Return true if the Btree passed as the only argument is sharable.
 */
int
sqlite3BtreeSharable(Btree * p)
{
	return p->sharable;
}

/*
 * Return the number of connections to the BtShared object accessed by
 * the Btree handle passed as the only argument. For private caches
 * this is always 1. For shared caches it may be 1 or greater.
 */
int
sqlite3BtreeConnectionCount(Btree * p)
{
	testcase(p->sharable);
	return p->pBt->nRef;
}
#endif
