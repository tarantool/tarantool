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
 * This header file defines the interface that the sqlite page cache
 * subsystem.  The page cache subsystem reads and writes a file a page
 * at a time and provides a journal for rollback.
 */

#ifndef SQLITE_PAGER_H
#define SQLITE_PAGER_H

/*
 * Default maximum size for persistent journal files. A negative
 * value means no limit. This value may be overridden using the
 * sqlite3PagerJournalSizeLimit() API. See also "PRAGMA journal_size_limit".
 */
#ifndef SQLITE_DEFAULT_JOURNAL_SIZE_LIMIT
#define SQLITE_DEFAULT_JOURNAL_SIZE_LIMIT -1
#endif

/*
 * The type used to represent a page number.  The first page in a file
 * is called page 1.  0 is used to represent "not a page".
 */
typedef u32 Pgno;

/*
 * Each open file is managed by a separate instance of the "Pager" structure.
 */
typedef struct Pager Pager;

/*
 * Handle type for pages.
 */
typedef struct PgHdr DbPage;

/*
 * Page number PAGER_MJ_PGNO is never used in an SQLite database (it is
 * reserved for working around a windows/posix incompatibility). It is
 * used in the journal to signify that the remainder of the journal file
 * is devoted to storing a master journal name - there are no more pages to
 * roll back. See comments for function writeMasterJournal() in pager.c
 * for details.
 */
#define PAGER_MJ_PGNO(x) ((Pgno)((PENDING_BYTE/((x)->pageSize))+1))

/*
 * Allowed values for the flags parameter to sqlite3PagerOpen().
 *
 * NOTE: These values must match the corresponding BTREE_ values in btree.h.
 */
#define PAGER_OMIT_JOURNAL  0x0001	/* Do not use a rollback journal */
#define PAGER_MEMORY        0x0002	/* In-memory database */

/*
 * Valid values for the second argument to sqlite3PagerLockingMode().
 */
#define PAGER_LOCKINGMODE_QUERY      -1
#define PAGER_LOCKINGMODE_NORMAL      0
#define PAGER_LOCKINGMODE_EXCLUSIVE   1

/*
 * Numeric constants that encode the journalmode.
 *
 * The numeric values encoded here (other than PAGER_JOURNALMODE_QUERY)
 * are exposed in the API via the "PRAGMA journal_mode" command and
 * therefore cannot be changed without a compatibility break.
 */
#define PAGER_JOURNALMODE_QUERY     (-1)	/* Query the value of journalmode */
#define PAGER_JOURNALMODE_DELETE      0	/* Commit by deleting journal file */
#define PAGER_JOURNALMODE_PERSIST     1	/* Commit by zeroing journal header */
#define PAGER_JOURNALMODE_OFF         2	/* Journal omitted.  */
#define PAGER_JOURNALMODE_TRUNCATE    3	/* Commit by truncating journal */
#define PAGER_JOURNALMODE_MEMORY      4	/* In-memory journal file */
#define PAGER_JOURNALMODE_WAL         5	/* Use write-ahead logging */

/*
 * Flags that make up the mask passed to sqlite3PagerGet().
 */
#define PAGER_GET_NOCONTENT     0x01	/* Do not load data from disk */
#define PAGER_GET_READONLY      0x02	/* Read-only page is acceptable */

/*
 * Flags for sqlite3PagerSetFlags()
 *
 */
#define PAGER_SYNCHRONOUS_OFF       0x01	/* PRAGMA synchronous=OFF */
#define PAGER_SYNCHRONOUS_NORMAL    0x02	/* PRAGMA synchronous=NORMAL */
#define PAGER_SYNCHRONOUS_FULL      0x03	/* PRAGMA synchronous=FULL */
#define PAGER_SYNCHRONOUS_EXTRA     0x04	/* PRAGMA synchronous=EXTRA */
#define PAGER_SYNCHRONOUS_MASK      0x07	/* Mask for four values above */
#define PAGER_FLAGS_MASK            0x38	/* All above except SYNCHRONOUS */

/*
 * The remainder of this file contains the declarations of the functions
 * that make up the Pager sub-system API. See source code comments for
 * a detailed description of each routine.
 */

/* Open and close a Pager connection. */
int sqlite3PagerOpen(sqlite3_vfs *,
		     Pager ** ppPager,
		     const char *, int, int, int, void (*)(DbPage *)
    );
int sqlite3PagerClose(Pager * pPager, sqlite3 *);
int sqlite3PagerReadFileheader(Pager *, int, unsigned char *);

/* Functions used to configure a Pager object. */
int sqlite3PagerSetPagesize(Pager *, u32 *, int);
void sqlite3PagerSetCachesize(Pager *, int);
int sqlite3PagerLockingMode(Pager *, int);
int sqlite3PagerGetJournalMode(Pager *);

/* Functions used to obtain and release page references. */
int sqlite3PagerGet(Pager * pPager, Pgno pgno, DbPage ** ppPage, int clrFlag);
DbPage *sqlite3PagerLookup(Pager * pPager, Pgno pgno);
void sqlite3PagerRef(DbPage *);
void sqlite3PagerUnref(DbPage *);
void sqlite3PagerUnrefNotNull(DbPage *);

/* Operations on page references. */
int sqlite3PagerWrite(DbPage *);
void sqlite3PagerDontWrite(DbPage *);
int sqlite3PagerPageRefcount(DbPage *);
void *sqlite3PagerGetData(DbPage *);
void *sqlite3PagerGetExtra(DbPage *);

/* Functions used to manage pager transactions and savepoints. */
void sqlite3PagerPagecount(Pager *, int *);
int sqlite3PagerCommitPhaseOne(Pager *);
int sqlite3PagerExclusiveLock(Pager *);
int sqlite3PagerSavepoint(Pager * pPager, int op, int iSavepoint);
int sqlite3PagerSharedLock(Pager * pPager);

#define sqlite3PagerUseWal(x) 0

#ifdef SQLITE_ENABLE_ZIPVFS
int sqlite3PagerWalFramesize(Pager * pPager);
#endif

/* Functions used to query pager state and configuration. */
u8 sqlite3PagerIsreadonly(Pager *);
u32 sqlite3PagerDataVersion(Pager *);
#ifdef SQLITE_DEBUG
int sqlite3PagerRefcount(Pager *);
#endif
const char *sqlite3PagerFilename(Pager *, int);
sqlite3_file *sqlite3PagerFile(Pager *);
sqlite3_file *sqlite3PagerJrnlFile(Pager *);
void *sqlite3PagerTempSpace(Pager *);

void sqlite3PagerRekey(DbPage *, Pgno, u16);

/* Functions to support testing and debugging. */
#if !defined(NDEBUG) || defined(SQLITE_TEST)
Pgno sqlite3PagerPagenumber(DbPage *);
int sqlite3PagerIswriteable(DbPage *);
#endif
#ifdef SQLITE_TEST
void disable_simulated_io_errors(void);
void enable_simulated_io_errors(void);
#else
#define disable_simulated_io_errors()
#define enable_simulated_io_errors()
#endif

#endif				/* SQLITE_PAGER_H */
