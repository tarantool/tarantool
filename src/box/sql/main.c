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
	int rc = SQL_OK;

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
		return SQL_OK;

	if (!sqlGlobalConfig.isMallocInit)
		sqlMallocInit();
	if (rc == SQL_OK)
		sqlGlobalConfig.isMallocInit = 1;

	/* If rc is not SQL_OK at this point, then the malloc
	 * subsystem could not be initialized.
	 */
	if (rc != SQL_OK)
		return rc;

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
		memset(&sqlBuiltinFunctions, 0,
		       sizeof(sqlBuiltinFunctions));
		sqlRegisterBuiltinFunctions();
		if (rc == SQL_OK) {
			rc = sqlOsInit();
		}
		if (rc == SQL_OK) {
			sqlGlobalConfig.isInit = 1;
		}
		sqlGlobalConfig.inProgress = 0;
	}

	/* The following is just a sanity check to make sure sql has
	 * been compiled correctly.  It is important to run this code, but
	 * we don't want to run it too often and soak up CPU cycles for no
	 * reason.  So we run it once during initialization.
	 */
#ifndef NDEBUG
	/* This section of code's only "output" is via assert() statements. */
	if (rc == SQL_OK) {
		u64 x = (((u64) 1) << 63) - 1;
		double y;
		assert(sizeof(x) == 8);
		assert(sizeof(x) == sizeof(y));
		memcpy(&y, &x, 8);
		assert(sqlIsNaN(y));
	}
#endif

	return rc;
}

/*
 * Set up the lookaside buffers for a database connection.
 * Return SQL_OK on success.
 * If lookaside is already active, return SQL_BUSY.
 *
 * The sz parameter is the number of bytes in each lookaside slot.
 * The cnt parameter is the number of slots.  If pStart is NULL the
 * space for the lookaside memory is obtained from sql_malloc().
 * If pStart is not NULL then it is sz*cnt bytes of memory to use for
 * the lookaside memory.
 */
static int
setupLookaside(sql * db, void *pBuf, int sz, int cnt)
{
	void *pStart;
	if (db->lookaside.nOut) {
		return SQL_BUSY;
	}
	/* Free any existing lookaside buffer for this handle before
	 * allocating a new one so we don't have to have space for
	 * both at the same time.
	 */
	if (db->lookaside.bMalloced) {
		sql_free(db->lookaside.pStart);
	}
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
		sqlBeginBenignMalloc();
		pStart = sqlMalloc(sz * cnt);	/* IMP: R-61949-35727 */
		sqlEndBenignMalloc();
		if (pStart)
			cnt = sqlMallocSize(pStart) / sz;
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
	return SQL_OK;
}

void
sql_row_count(struct sql_context *context, MAYBE_UNUSED int unused1,
	      MAYBE_UNUSED sql_value **unused2)
{
	sql *db = sql_context_db_handle(context);
	sql_result_int(context, db->nChange);
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
 * Invoke the destructor function associated with FuncDef p, if any. Except,
 * if this is not the last copy of the function, do not invoke it. Multiple
 * copies of a single function are created when create_function() is called
 * with SQL_ANY as the encoding.
 */
static void
functionDestroy(sql * db, FuncDef * p)
{
	FuncDestructor *pDestructor = p->u.pDestructor;
	if (pDestructor) {
		pDestructor->nRef--;
		if (pDestructor->nRef == 0) {
			pDestructor->xDestroy(pDestructor->pUserData);
			sqlDbFree(db, pDestructor);
		}
	}
}

/*
 * Return TRUE if database connection db has unfinalized prepared
 * statement.
 */
static int
connectionIsBusy(sql * db)
{
	if (db->pVdbe)
		return 1;
	return 0;
}

/*
 * Close an existing sql database
 */
static int
sqlClose(sql * db, int forceZombie)
{
	assert(db);
	if (!sqlSafetyCheckSickOrOk(db)) {
		return SQL_MISUSE;
	}
	if (db->mTrace & SQL_TRACE_CLOSE) {
		db->xTrace(SQL_TRACE_CLOSE, db->pTraceArg, db, 0);
	}

	/* Legacy behavior (sql_close() behavior) is to return
	 * SQL_BUSY if the connection can not be closed immediately.
	 */
	if (!forceZombie && connectionIsBusy(db)) {
		sqlErrorWithMsg(db, SQL_BUSY,
				    "unable to close due to unfinalized "
				    "statements");
		return SQL_BUSY;
	}

	/* Convert the connection into a zombie and then close it.
	 */
	db->magic = SQL_MAGIC_ZOMBIE;

	return SQL_OK;
}

/*
 * Two variations on the public interface for closing a database
 * connection. The sql_close() version returns SQL_BUSY and
 * leaves the connection option if there are unfinalized prepared
 * statements.
 */
int
sql_close(sql * db)
{
	return sqlClose(db, 0);
}

/*
 * Rollback all database files.  If tripCode is not SQL_OK, then
 * any write cursors are invalidated ("tripped" - as in "tripping a circuit
 * breaker") and made to return tripCode if there are any further
 * attempts to use that cursor.  Read cursors remain open and valid
 * but are "saved" in case the table pages are moved around.
 */
void
sqlRollbackAll(Vdbe * pVdbe)
{
	sql *db = pVdbe->db;

	/* If one has been configured, invoke the rollback-hook callback */
	if (db->xRollbackCallback && (!pVdbe->auto_commit)) {
		db->xRollbackCallback(db->pRollbackArg);
	}
}

/*
 * Return a static string that describes the kind of error specified in the
 * argument.
 */
const char *
sqlErrStr(int rc)
{
	static const char *const aMsg[] = {
		/* SQL_OK          */ "not an error",
		/* SQL_ERROR       */ "SQL logic error or missing database",
		/* SQL_PERM        */ "access permission denied",
		/* SQL_ABORT       */ "callback requested query abort",
		/* SQL_BUSY        */ "database is locked",
		/* SQL_LOCKED      */ "database table is locked",
		/* SQL_NOMEM       */ "out of memory",
		/* SQL_INTERRUPT   */ "interrupted",
		/* SQL_IOERR       */ "disk I/O error",
		/* SQL_NOTFOUND    */ "unknown operation",
		/* SQL_FULL        */ "database or disk is full",
		/* SQL_CANTOPEN    */ "unable to open database file",
		/* SQL_SCHEMA      */ "database schema has changed",
		/* SQL_TOOBIG      */ "string or blob too big",
		/* SQL_CONSTRAINT  */ "constraint failed",
		/* SQL_MISMATCH    */ "datatype mismatch",
		/* SQL_MISUSE      */
		    "library routine called out of sequence",
		/* SQL_RANGE       */ "bind or column index out of range",
		/* SQL_TARANTOOL_ITERATOR_FAIL */ "Tarantool's iterator failed",
		/* SQL_TARANTOOL_INSERT_FAIL */ "Tarantool's insert failed",
		/* SQL_TARANTOOL_DELETE_FAIL */ "Tarantool's delete failed",
		/* SQL_TARANTOOL_ERROR */ "SQL-/Tarantool error",
	};
	const char *zErr = "unknown error";
	rc &= 0xff;
	if (ALWAYS(rc >= 0) && rc < ArraySize(aMsg) && aMsg[rc] != 0)
		zErr = aMsg[rc];
	return zErr;
}

/*
 * This function is exactly the same as sql_create_function(), except
 * that it is designed to be called by internal code. The difference is
 * that if a malloc() fails in sql_create_function(), an error code
 * is returned and the mallocFailed flag cleared.
 */
int
sqlCreateFunc(sql * db,
		  const char *zFunctionName,
		  enum field_type type,
		  int nArg,
		  int flags,
		  void *pUserData,
		  void (*xSFunc) (sql_context *, int, sql_value **),
		  void (*xStep) (sql_context *, int, sql_value **),
		  void (*xFinal) (sql_context *),
		  FuncDestructor * pDestructor)
{
	FuncDef *p;
	int extraFlags;

	if (zFunctionName == 0 ||
	    (xSFunc && (xFinal || xStep)) ||
	    (!xSFunc && (xFinal && !xStep)) ||
	    (!xSFunc && (!xFinal && xStep)) ||
	    (nArg < -1 || nArg > SQL_MAX_FUNCTION_ARG) ||
	    (255 < (sqlStrlen30(zFunctionName)))) {
		return SQL_MISUSE;
	}

	assert(SQL_FUNC_CONSTANT == SQL_DETERMINISTIC);
	extraFlags = flags & SQL_DETERMINISTIC;


	/* Check if an existing function is being overridden or deleted. If so,
	 * and there are active VMs, then return SQL_BUSY. If a function
	 * is being overridden/deleted but there are no active VMs, allow the
	 * operation to continue but invalidate all precompiled statements.
	 */
	p = sqlFindFunction(db, zFunctionName, nArg, 0);
	if (p && p->nArg == nArg) {
		if (db->nVdbeActive) {
			sqlErrorWithMsg(db, SQL_BUSY,
					    "unable to delete/modify user-function due to active statements");
			assert(!db->mallocFailed);
			return SQL_BUSY;
		} else {
			sqlExpirePreparedStatements(db);
		}
	}

	p = sqlFindFunction(db, zFunctionName, nArg, 1);
	assert(p || db->mallocFailed);
	if (!p) {
		return SQL_NOMEM;
	}

	/* If an older version of the function with a configured destructor is
	 * being replaced invoke the destructor function here.
	 */
	functionDestroy(db, p);

	if (pDestructor) {
		pDestructor->nRef++;
	}
	p->u.pDestructor = pDestructor;
	p->funcFlags = extraFlags;
	testcase(p->funcFlags & SQL_DETERMINISTIC);
	p->xSFunc = xSFunc ? xSFunc : xStep;
	p->xFinalize = xFinal;
	p->pUserData = pUserData;
	p->nArg = (u16) nArg;
	p->ret_type = type;
	return SQL_OK;
}

int
sql_create_function_v2(sql * db,
			   const char *zFunc,
			   enum field_type type,
			   int nArg,
			   int flags,
			   void *p,
			   void (*xSFunc) (sql_context *, int,
					   sql_value **),
			   void (*xStep) (sql_context *, int,
					  sql_value **),
			   void (*xFinal) (sql_context *),
			   void (*xDestroy) (void *))
{
	int rc = SQL_ERROR;
	FuncDestructor *pArg = 0;

	if (xDestroy) {
		pArg =
		    (FuncDestructor *) sqlDbMallocZero(db,
							   sizeof
							   (FuncDestructor));
		if (!pArg) {
			xDestroy(p);
			goto out;
		}
		pArg->xDestroy = xDestroy;
		pArg->pUserData = p;
	}
	rc = sqlCreateFunc(db, zFunc, type, nArg, flags, p, xSFunc, xStep,
			       xFinal, pArg);
	if (pArg && pArg->nRef == 0) {
		assert(rc != SQL_OK);
		xDestroy(p);
		sqlDbFree(db, pArg);
	}

 out:
	rc = sqlApiExit(db, rc);
	return rc;
}

/*
 * Return the most recent error code generated by an sql routine. If NULL is
 * passed to this function, we assume a malloc() failed during sql_open().
 */
int
sql_errcode(sql * db)
{
	if (db && !sqlSafetyCheckSickOrOk(db)) {
		return SQL_MISUSE;
	}
	if (!db || db->mallocFailed) {
		return SQL_NOMEM;
	}
	return db->errCode & db->errMask;
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

/*
 * Change the value of a limit.  Report the old value.
 * If an invalid limit index is supplied, report -1.
 * Make no changes but still report the old value if the
 * new limit is negative.
 *
 * A new lower limit does not shrink existing constructs.
 * It merely prevents new constructs that exceed the limit
 * from forming.
 */
int
sql_limit(sql * db, int limitId, int newLimit)
{
	int oldLimit;

	/* EVIDENCE-OF: R-30189-54097 For each limit category SQL_LIMIT_NAME
	 * there is a hard upper bound set at compile-time by a C preprocessor
	 * macro called SQL_MAX_NAME. (The "_LIMIT_" in the name is changed to
	 * "_MAX_".)
	 */
	assert(aHardLimit[SQL_LIMIT_LENGTH] == SQL_MAX_LENGTH);
	assert(aHardLimit[SQL_LIMIT_SQL_LENGTH] == SQL_MAX_SQL_LENGTH);
	assert(aHardLimit[SQL_LIMIT_COLUMN] == SQL_MAX_COLUMN);
	assert(aHardLimit[SQL_LIMIT_EXPR_DEPTH] == SQL_MAX_EXPR_DEPTH);
	assert(aHardLimit[SQL_LIMIT_COMPOUND_SELECT] ==
	       SQL_MAX_COMPOUND_SELECT);
	assert(aHardLimit[SQL_LIMIT_VDBE_OP] == SQL_MAX_VDBE_OP);
	assert(aHardLimit[SQL_LIMIT_FUNCTION_ARG] ==
	       SQL_MAX_FUNCTION_ARG);
	assert(aHardLimit[SQL_LIMIT_ATTACHED] == SQL_MAX_ATTACHED);
	assert(aHardLimit[SQL_LIMIT_LIKE_PATTERN_LENGTH] ==
	       SQL_MAX_LIKE_PATTERN_LENGTH);
	assert(aHardLimit[SQL_LIMIT_TRIGGER_DEPTH] ==
	       SQL_MAX_TRIGGER_DEPTH);

	if (limitId < 0 || limitId >= SQL_N_LIMIT) {
		return -1;
	}
	oldLimit = db->aLimit[limitId];
	if (newLimit >= 0) {	/* IMP: R-52476-28732 */
		if (newLimit > aHardLimit[limitId]) {
			newLimit = aHardLimit[limitId];	/* IMP: R-51463-25634 */
		}
		db->aLimit[limitId] = newLimit;
	}
	return oldLimit;	/* IMP: R-53341-35419 */
}

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
	int rc;			/* Return code */

	rc = sql_initialize();
	if (rc)
		return rc;

	/* Allocate the sql data structure */
	db = sqlMallocZero(sizeof(sql));
	if (db == 0)
		goto opendb_out;
	db->errMask = 0xff;
	db->magic = SQL_MAGIC_BUSY;

	db->pVfs = sql_vfs_find(0);

	assert(sizeof(db->aLimit) == sizeof(aHardLimit));
	memcpy(db->aLimit, aHardLimit, sizeof(db->aLimit));
	db->aLimit[SQL_LIMIT_COMPOUND_SELECT] = SQL_DEFAULT_COMPOUND_SELECT;
	db->szMmap = sqlGlobalConfig.szMmap;
	db->nMaxSorterMmap = 0x7FFFFFFF;

	db->magic = SQL_MAGIC_OPEN;
	if (db->mallocFailed) {
		goto opendb_out;
	}

	/* Register all built-in functions, but do not attempt to read the
	 * database schema yet. This is delayed until the first time the database
	 * is accessed.
	 */
	sqlError(db, SQL_OK);
	sqlRegisterPerConnectionBuiltinFunctions(db);
	rc = sql_errcode(db);

	if (rc)
		sqlError(db, rc);

	/* Enable the lookaside-malloc subsystem */
	setupLookaside(db, 0, sqlGlobalConfig.szLookaside,
		       sqlGlobalConfig.nLookaside);

opendb_out:
	rc = sql_errcode(db);
	assert(db != 0 || rc == SQL_NOMEM);
	if (rc == SQL_NOMEM) {
		sql_close(db);
		db = 0;
	} else if (rc != SQL_OK)
		db->magic = SQL_MAGIC_SICK;

	*out_db = db;

	return rc;
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
