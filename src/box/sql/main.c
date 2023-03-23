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
 * Main file for the sql library.  The routines in this file
 * implement the programmer interface to the library.  Routines in
 * other files are for internal use by sql and should not be
 * accessed by users of the library.
 */
#include "sqlInt.h"
#include "mem.h"
#include "vdbeInt.h"
#include "version.h"
#include "box/session.h"

/*
 * If the following global variable points to a string which is the
 * name of a directory, then that directory will be used to store
 * temporary files.
 *
 * See also the "PRAGMA temp_store_directory" SQL command.
 */
char *sql_temp_directory = 0;

/*
 * If the following global variable points to a string which is the
 * name of a directory, then that directory will be used to store
 * all database files specified with a relative pathname.
 *
 * See also the "PRAGMA data_store_directory" SQL command.
 */
char *sql_data_directory = 0;

enum {
	LOOKASIDE_SLOT_NUMBER = 125,
	LOOKASIDE_SLOT_SIZE = 512,
};

/*
 * Initialize sql.
 *
 * This routine must be called to initialize the memory allocation,
 * and VFS subsystems prior to doing any serious work with
 * sql.
 *
 * This routine is a no-op except on its very first call for the process.
 *
 * The first thread to call this routine runs the initialization to
 * completion.  If subsequent threads call this routine before the first
 * thread has finished the initialization process, then the subsequent
 * threads must block until the first thread finishes with the initialization.
 *
 * The first thread might call this routine recursively.  Recursive
 * calls to this routine should not block, of course.  Otherwise the
 * initialization process would never complete.
 *
 * Let X be the first thread to enter this routine.  Let Y be some other
 * thread.  Then while the initial invocation of this routine by X is
 * incomplete, it is required that:
 *
 *    *  Calls to this routine from Y must block until the outer-most
 *       call by X completes.
 *
 *    *  Recursive calls to this routine from thread X return immediately
 *       without blocking.
 */
int
sql_initialize(void)
{
	/* If the following assert() fails on some obscure processor/compiler
	 * combination, the work-around is to set the correct pointer
	 * size at compile-time using -DSQL_PTRSIZE=n compile-time option
	 */
	assert(SQL_PTRSIZE == sizeof(char *));

	/* If sql is already completely initialized, then this call
	 * to sql_initialize() should be a no-op.  But the initialization
	 * must be complete.  So isInit must not be set until the very end
	 * of this routine.
	 */
	if (sqlGlobalConfig.isInit)
		return 0;

	/* Do the rest of the initialization
	 * that we will be able to handle recursive calls into
	 * sql_initialize().  The recursive calls normally come through
	 * sql_os_init() when it invokes sql_vfs_register(), but other
	 * recursive calls might also be possible.
	 *
	 * IMPLEMENTATION-OF: R-00140-37445 sql automatically serializes calls
	 * to the xInit method, so the xInit method need not be threadsafe.
	 *
         * The sql_pcache_methods.xInit() all is embedded in the
	 * call to sqlPcacheInitialize().
	 */
	if (sqlGlobalConfig.isInit == 0
	    && sqlGlobalConfig.inProgress == 0) {
		sqlGlobalConfig.inProgress = 1;
		sql_os_init();
		sqlGlobalConfig.isInit = 1;
		sqlGlobalConfig.inProgress = 0;
	}

	/* The following is just a sanity check to make sure sql has
	 * been compiled correctly.  It is important to run this code, but
	 * we don't want to run it too often and soak up CPU cycles for no
	 * reason.  So we run it once during initialization.
	 */
#ifndef NDEBUG
	/* This section of code's only "output" is via assert() statements. */
	u64 x = (((u64) 1) << 63) - 1;
	double y;
	assert(sizeof(x) == 8);
	assert(sizeof(x) == sizeof(y));
	memcpy(&y, &x, 8);
	assert(sqlIsNaN(y));
#endif
	return 0;
}

/*
 * Set up the lookaside buffers for a database connection.
 * Return SQL_OK on success.
 * If lookaside is already active, return SQL_BUSY.
 *
 * The sz parameter is the number of bytes in each lookaside slot.
 * The cnt parameter is the number of slots.  If pStart is NULL the
 * space for the lookaside memory is obtained from malloc().
 * If pStart is not NULL then it is sz*cnt bytes of memory to use for
 * the lookaside memory.
 */
static int
setupLookaside(sql * db, void *pBuf, int sz, int cnt)
{
	void *pStart;
	if (db->lookaside.nOut)
		return -1;
	/* Free any existing lookaside buffer for this handle before
	 * allocating a new one so we don't have to have space for
	 * both at the same time.
	 */
	if (db->lookaside.bMalloced)
		free(db->lookaside.pStart);
	/* The size of a lookaside slot after ROUNDDOWN8 needs to be larger
	 * than a pointer to be useful.
	 */
	sz = ROUNDDOWN8(sz);	/* IMP: R-33038-09382 */
	if (sz <= (int)sizeof(LookasideSlot *))
		sz = 0;
	if (cnt < 0)
		cnt = 0;
	if (sz == 0 || cnt == 0) {
		sz = 0;
		pStart = 0;
	} else if (pBuf == 0) {
		pStart = xmalloc(sz * cnt);
	} else {
		pStart = pBuf;
	}
	db->lookaside.pStart = pStart;
	db->lookaside.pFree = 0;
	db->lookaside.sz = (u16) sz;
	if (pStart) {
		int i;
		LookasideSlot *p;
		assert(sz > (int)sizeof(LookasideSlot *));
		p = (LookasideSlot *) pStart;
		for (i = cnt - 1; i >= 0; i--) {
			p->pNext = db->lookaside.pFree;
			db->lookaside.pFree = p;
			p = (LookasideSlot *) & ((u8 *) p)[sz];
		}
		db->lookaside.pEnd = p;
		db->lookaside.bDisable = 0;
		db->lookaside.bMalloced = pBuf == 0 ? 1 : 0;
	} else {
		db->lookaside.pStart = db;
		db->lookaside.pEnd = db;
		db->lookaside.bDisable = 1;
		db->lookaside.bMalloced = 0;
	}
	return 0;
}

/*
 * Close all open savepoints.
 * This procedure is trivial as savepoints are allocated on the "region" and
 * would be destroyed automatically.
 */
void
sqlCloseSavepoints(Vdbe * pVdbe)
{
	pVdbe->anonymous_savepoint = NULL;
}

/*
 * Rollback all database files.  If tripCode is not 0, then
 * any write cursors are invalidated ("tripped" - as in "tripping a circuit
 * breaker") and made to return tripCode if there are any further
 * attempts to use that cursor.  Read cursors remain open and valid
 * but are "saved" in case the table pages are moved around.
 */
void
sqlRollbackAll(Vdbe * pVdbe)
{
	struct sql *db = sql_get();

	/* If one has been configured, invoke the rollback-hook callback */
	if (db->xRollbackCallback && (!pVdbe->auto_commit)) {
		db->xRollbackCallback(db->pRollbackArg);
	}
}

/*
 * This array defines hard upper bounds on limit values.  The
 * initializer must be kept in sync with the SQL_LIMIT_*
 * #defines in sql.h.
 */
static const int aHardLimit[] = {
	SQL_MAX_LENGTH,
	SQL_MAX_SQL_LENGTH,
	SQL_MAX_COLUMN,
	SQL_MAX_EXPR_DEPTH,
	SQL_MAX_COMPOUND_SELECT,
	SQL_MAX_VDBE_OP,
	SQL_MAX_FUNCTION_ARG,
	SQL_MAX_ATTACHED,
	SQL_MAX_LIKE_PATTERN_LENGTH,
	SQL_MAX_TRIGGER_DEPTH,
};

/*
 * Make sure the hard limits are set to reasonable values
 */
#if SQL_MAX_LENGTH<100
#error SQL_MAX_LENGTH must be at least 100
#endif
#if SQL_MAX_SQL_LENGTH<100
#error SQL_MAX_SQL_LENGTH must be at least 100
#endif
#if SQL_MAX_SQL_LENGTH>SQL_MAX_LENGTH
#error SQL_MAX_SQL_LENGTH must not be greater than SQL_MAX_LENGTH
#endif
#if SQL_MAX_COMPOUND_SELECT<2
#error SQL_MAX_COMPOUND_SELECT must be at least 2
#endif
#if SQL_MAX_VDBE_OP<40
#error SQL_MAX_VDBE_OP must be at least 40
#endif
#if SQL_MAX_FUNCTION_ARG<0 || SQL_MAX_FUNCTION_ARG>127
#error SQL_MAX_FUNCTION_ARG must be between 0 and 127
#endif
#if SQL_MAX_ATTACHED<0 || SQL_MAX_ATTACHED>125
#error SQL_MAX_ATTACHED must be between 0 and 125
#endif
#if SQL_MAX_LIKE_PATTERN_LENGTH<1
#error SQL_MAX_LIKE_PATTERN_LENGTH must be at least 1
#endif
#if SQL_MAX_COLUMN>32767
#error SQL_MAX_COLUMN must not exceed 32767
#endif
#if SQL_MAX_TRIGGER_DEPTH<1
#error SQL_MAX_TRIGGER_DEPTH must be at least 1
#endif

/**
 * This routine does the work of initialization of main
 * SQL connection instance.
 *
 * @param[out] out_db returned database handle.
 * @return error status code.
 */
int
sql_init_db(sql **out_db)
{
	sql *db;

	if (sql_initialize() != 0)
		return -1;

	/* Allocate the sql data structure */
	db = xcalloc(1, sizeof(*db));
	db->magic = SQL_MAGIC_BUSY;

	db->pVfs = sql_vfs_find(0);

	assert(sizeof(db->aLimit) == sizeof(aHardLimit));
	memcpy(db->aLimit, aHardLimit, sizeof(db->aLimit));
	db->aLimit[SQL_LIMIT_COMPOUND_SELECT] = SQL_DEFAULT_COMPOUND_SELECT;
	db->szMmap = sqlGlobalConfig.szMmap;
	db->nMaxSorterMmap = 0x7FFFFFFF;

	db->magic = SQL_MAGIC_OPEN;

	/* Enable the lookaside-malloc subsystem */
	setupLookaside(db, 0, LOOKASIDE_SLOT_SIZE, LOOKASIDE_SLOT_NUMBER);

	*out_db = db;
	return 0;
}

/*
 * This is a utility routine, useful to VFS implementations, that checks
 * to see if a database file was a URI that contained a specific query
 * parameter, and if so obtains the value of the query parameter.
 *
 * The zFilename argument is the filename pointer passed into the xOpen()
 * method of a VFS implementation.  The zParam argument is the name of the
 * query parameter we seek.  This routine returns the value of the zParam
 * parameter if it exists.  If the parameter does not exist, this routine
 * returns a NULL pointer.
 */
const char *
sql_uri_parameter(const char *zFilename, const char *zParam)
{
	if (zFilename == 0 || zParam == 0)
		return 0;
	zFilename += sqlStrlen30(zFilename) + 1;
	while (zFilename[0]) {
		int x = strcmp(zFilename, zParam);
		zFilename += sqlStrlen30(zFilename) + 1;
		if (x == 0)
			return zFilename;
		zFilename += sqlStrlen30(zFilename) + 1;
	}
	return 0;
}
