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

#ifdef SQL_ENABLE_FTS3
#include "fts3.h"
#endif
#ifdef SQL_ENABLE_RTREE
#include "rtree.h"
#endif
#ifdef SQL_ENABLE_ICU
#include "sqlicu.h"
#endif
#ifdef SQL_ENABLE_JSON1
int sqlJson1Init(sql *);
#endif
#ifdef SQL_ENABLE_FTS5
int sqlFts5Init(sql *);
#endif

#if !defined(SQL_OMIT_TRACE) && defined(SQL_ENABLE_IOTRACE)
/*
 * If the following function pointer is not NULL and if
 * SQL_ENABLE_IOTRACE is enabled, then messages describing
 * I/O active are written using this function.  These messages
 * are intended for debugging activity only.
 */
SQL_API void (SQL_CDECL * sqlIoTrace) (const char *, ...) = 0;
#endif

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
 * sql.  But as long as you do not compile with SQL_OMIT_AUTOINIT
 * this routine will be called automatically by key routines such as
 * sql_open().
 *
 * This routine is a no-op except on its very first call for the process,
 * or for the first call after a call to sql_shutdown.
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
#ifdef SQL_EXTRA_INIT
	int bRunExtraInit = 0;	/* Extra initialization needed */
#endif

#ifdef SQL_OMIT_WSD
	rc = sql_wsd_init(4096, 24);
	if (rc != SQL_OK) {
		return rc;
	}
#endif

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
#ifdef SQL_ENABLE_SQLLOG
		{
			extern void sql_init_sqllog(void);
			sql_init_sqllog();
		}
#endif
		memset(&sqlBuiltinFunctions, 0,
		       sizeof(sqlBuiltinFunctions));
		sqlRegisterBuiltinFunctions();
		if (rc == SQL_OK) {
			rc = sqlOsInit();
		}
		if (rc == SQL_OK) {
			sqlGlobalConfig.isInit = 1;
#ifdef SQL_EXTRA_INIT
			bRunExtraInit = 1;
#endif
		}
		sqlGlobalConfig.inProgress = 0;
	}

	/* The following is just a sanity check to make sure sql has
	 * been compiled correctly.  It is important to run this code, but
	 * we don't want to run it too often and soak up CPU cycles for no
	 * reason.  So we run it once during initialization.
	 */
#ifndef NDEBUG
#ifndef SQL_OMIT_FLOATING_POINT
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
#endif

	/* Do extra initialization steps requested by the SQL_EXTRA_INIT
	 * compile-time option.
	 */
#ifdef SQL_EXTRA_INIT
	if (bRunExtraInit) {
		int SQL_EXTRA_INIT(const char *);
		rc = SQL_EXTRA_INIT(0);
	}
#endif

	return rc;
}

/*
 * Undo the effects of sql_initialize().  Must not be called while
 * there are outstanding database connections or memory allocations or
 * while any part of sql is otherwise in use in any thread.  This
 * routine is not threadsafe.  But it is safe to invoke this routine
 * on when sql is already shut down.  If sql is already shut down
 * when this routine is invoked, then this routine is a harmless no-op.
 */
int
sql_shutdown(void)
{
#ifdef SQL_OMIT_WSD
	int rc = sql_wsd_init(4096, 24);
	if (rc != SQL_OK) {
		return rc;
	}
#endif

	if (sqlGlobalConfig.isInit) {
#ifdef SQL_EXTRA_SHUTDOWN
		void SQL_EXTRA_SHUTDOWN(void);
		SQL_EXTRA_SHUTDOWN();
#endif
		sql_os_end();
		sqlGlobalConfig.isInit = 0;
	}
	if (sqlGlobalConfig.isMallocInit) {
		sqlMallocEnd();
		sqlGlobalConfig.isMallocInit = 0;

#ifndef SQL_OMIT_SHUTDOWN_DIRECTORIES
		/* The heap subsystem has now been shutdown and these values are supposed
		 * to be NULL or point to memory that was obtained from sql_malloc(),
		 * which would rely on that heap subsystem; therefore, make sure these
		 * values cannot refer to heap memory that was just invalidated when the
		 * heap subsystem was shutdown.  This is only done if the current call to
		 * this function resulted in the heap subsystem actually being shutdown.
		 */
		sql_data_directory = 0;
		sql_temp_directory = 0;
#endif
	}

	return SQL_OK;
}

/*
 * This API allows applications to modify the global configuration of
 * the sql library at run-time.
 *
 * This routine should only be called when there are no outstanding
 * database connections or memory allocations.  This routine is not
 * threadsafe.  Failure to heed these warnings can lead to unpredictable
 * behavior.
 */
int
sql_config(int op, ...)
{
	va_list ap;
	int rc = SQL_OK;

	/* sql_config() shall return SQL_MISUSE if it is invoked while
	 * the sql library is in use.
	 */
	if (sqlGlobalConfig.isInit)
		return SQL_MISUSE;

	va_start(ap, op);
	switch (op) {
	case SQL_CONFIG_MEMSTATUS:{
			/* EVIDENCE-OF: R-61275-35157 The SQL_CONFIG_MEMSTATUS option takes
			 * single argument of type int, interpreted as a boolean, which enables
			 * or disables the collection of memory allocation statistics.
			 */
			sqlGlobalConfig.bMemstat = va_arg(ap, int);
			break;
		}
	case SQL_CONFIG_SCRATCH:{
			/* EVIDENCE-OF: R-08404-60887 There are three arguments to
			 * SQL_CONFIG_SCRATCH: A pointer an 8-byte aligned memory buffer from
			 * which the scratch allocations will be drawn, the size of each scratch
			 * allocation (sz), and the maximum number of scratch allocations (N).
			 */
			sqlGlobalConfig.pScratch = va_arg(ap, void *);
			sqlGlobalConfig.szScratch = va_arg(ap, int);
			sqlGlobalConfig.nScratch = va_arg(ap, int);
			break;
		}

	case SQL_CONFIG_LOOKASIDE:{
			sqlGlobalConfig.szLookaside = va_arg(ap, int);
			sqlGlobalConfig.nLookaside = va_arg(ap, int);
			break;
		}

		/* Record a pointer to the logger function and its first argument.
		 * The default is NULL.  Logging is disabled if the function pointer is
		 * NULL.
		 */
	case SQL_CONFIG_LOG:{
			typedef void (*LOGFUNC_t) (void *, int, const char *);
			sqlGlobalConfig.xLog = va_arg(ap, LOGFUNC_t);
			sqlGlobalConfig.pLogArg = va_arg(ap, void *);
			break;
		}

		/* EVIDENCE-OF: R-55548-33817 The compile-time setting for URI filenames
		 * can be changed at start-time using the
		 * sql_config(SQL_CONFIG_URI,1) or
		 * sql_config(SQL_CONFIG_URI,0) configuration calls.
		 */
	case SQL_CONFIG_URI:{
			/* EVIDENCE-OF: R-25451-61125 The SQL_CONFIG_URI option takes a single
			 * argument of type int. If non-zero, then URI handling is globally
			 * enabled. If the parameter is zero, then URI handling is globally
			 * disabled.
			 */
			sqlGlobalConfig.bOpenUri = va_arg(ap, int);
			break;
		}

	case SQL_CONFIG_COVERING_INDEX_SCAN:{
			/* EVIDENCE-OF: R-36592-02772 The SQL_CONFIG_COVERING_INDEX_SCAN
			 * option takes a single integer argument which is interpreted as a
			 * boolean in order to enable or disable the use of covering indices for
			 * full table scans in the query optimizer.
			 */
			sqlGlobalConfig.bUseCis = va_arg(ap, int);
			break;
		}

#ifdef SQL_ENABLE_SQLLOG
	case SQL_CONFIG_SQLLOG:{
			typedef void (*SQLLOGFUNC_t) (void *, sql *,
						      const char *, int);
			sqlGlobalConfig.xSqllog = va_arg(ap, SQLLOGFUNC_t);
			sqlGlobalConfig.pSqllogArg = va_arg(ap, void *);
			break;
		}
#endif

	case SQL_CONFIG_MMAP_SIZE:{
			/* EVIDENCE-OF: R-58063-38258 SQL_CONFIG_MMAP_SIZE takes two 64-bit
			 * integer (sql_int64) values that are the default mmap size limit
			 * (the default setting for PRAGMA mmap_size) and the maximum allowed
			 * mmap size limit.
			 */
			sql_int64 szMmap = va_arg(ap, sql_int64);
			sql_int64 mxMmap = va_arg(ap, sql_int64);
			/* EVIDENCE-OF: R-53367-43190 If either argument to this option is
			 * negative, then that argument is changed to its compile-time default.
			 *
			 * EVIDENCE-OF: R-34993-45031 The maximum allowed mmap size will be
			 * silently truncated if necessary so that it does not exceed the
			 * compile-time maximum mmap size set by the SQL_MAX_MMAP_SIZE
			 * compile-time option.
			 */
			if (mxMmap < 0 || mxMmap > SQL_MAX_MMAP_SIZE) {
				mxMmap = SQL_MAX_MMAP_SIZE;
			}
			if (szMmap < 0)
				szMmap = SQL_DEFAULT_MMAP_SIZE;
			if (szMmap > mxMmap)
				szMmap = mxMmap;
			sqlGlobalConfig.mxMmap = mxMmap;
			sqlGlobalConfig.szMmap = szMmap;
			break;
		}

	case SQL_CONFIG_PMASZ:{
			sqlGlobalConfig.szPma = va_arg(ap, unsigned int);
			break;
		}

	case SQL_CONFIG_STMTJRNL_SPILL:{
			sqlGlobalConfig.nStmtSpill = va_arg(ap, int);
			break;
		}

	default:{
			rc = SQL_ERROR;
			break;
		}
	}
	va_end(ap);
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
#ifndef SQL_OMIT_LOOKASIDE
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
#endif				/* SQL_OMIT_LOOKASIDE */
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
#ifdef SQL_ENABLE_SQLLOG
	if (sqlGlobalConfig.xSqllog) {
		/* Closing the handle. Fourth parameter is passed the value 2. */
		sqlGlobalConfig.xSqllog(sqlGlobalConfig.pSqllogArg, db,
					    0, 2);
	}
#endif

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

#ifndef SQL_OMIT_PROGRESS_CALLBACK
/*
 * This routine sets the progress callback for an Sqlite database to the
 * given callback function with the given argument. The progress callback will
 * be invoked every nOps opcodes.
 */
void
sql_progress_handler(sql * db,
			 int nOps, int (*xProgress) (void *), void *pArg)
{
#ifdef SQL_ENABLE_API_ARMOR
	if (!sqlSafetyCheckOk(db)) {
		return;
	}
#endif
	if (nOps > 0) {
		db->xProgress = xProgress;
		db->nProgressOps = (unsigned)nOps;
		db->pProgressArg = pArg;
	} else {
		db->xProgress = 0;
		db->nProgressOps = 0;
		db->pProgressArg = 0;
	}
}
#endif

/*
 * Cause any pending operation to stop at its earliest opportunity.
 */
void
sql_interrupt(sql * db)
{
#ifdef SQL_ENABLE_API_ARMOR
	if (!sqlSafetyCheckOk(db)
	    && (db == 0 || db->magic != SQL_MAGIC_ZOMBIE)) {
		return;
	}
#endif
	db->u1.isInterrupted = 1;
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

#ifdef SQL_ENABLE_API_ARMOR
	if (!sqlSafetyCheckOk(db)) {
		return SQL_MISUSE;
	}
#endif
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

#ifndef SQL_OMIT_TRACE
/* Register a trace callback using the version-2 interface.
 */
int
sql_trace_v2(sql * db,		/* Trace this connection */
		 unsigned mTrace,	/* Mask of events to be traced */
		 int (*xTrace) (unsigned, void *, void *, void *),	/* Callback to invoke */
		 void *pArg)		/* Context */
{
#ifdef SQL_ENABLE_API_ARMOR
	if (!sqlSafetyCheckOk(db)) {
		return SQL_MISUSE;
	}
#endif
	if (mTrace == 0)
		xTrace = 0;
	if (xTrace == 0)
		mTrace = 0;
	db->mTrace = mTrace;
	db->xTrace = xTrace;
	db->pTraceArg = pArg;
	return SQL_OK;
}

#endif				/* SQL_OMIT_TRACE */

/*
 * Register a function to be invoked when a transaction commits.
 * If the invoked function returns non-zero, then the commit becomes a
 * rollback.
 */
void *
sql_commit_hook(sql * db,	/* Attach the hook to this database */
		    int (*xCallback) (void *),	/* Function to invoke on each commit */
		    void *pArg)		/* Argument to the function */
{
	void *pOld;

#ifdef SQL_ENABLE_API_ARMOR
	if (!sqlSafetyCheckOk(db)) {
		return 0;
	}
#endif
	pOld = db->pCommitArg;
	db->xCommitCallback = xCallback;
	db->pCommitArg = pArg;
	return pOld;
}

/*
 * Register a callback to be invoked each time a row is updated,
 * inserted or deleted using this database connection.
 */
void *
sql_update_hook(sql * db,	/* Attach the hook to this database */
		    void (*xCallback) (void *, int, char const *,
				       char const *, sql_int64),
		    void *pArg)		/* Argument to the function */
{
	void *pRet;

#ifdef SQL_ENABLE_API_ARMOR
	if (!sqlSafetyCheckOk(db)) {
		return 0;
	}
#endif
	pRet = db->pUpdateArg;
	db->xUpdateCallback = xCallback;
	db->pUpdateArg = pArg;
	return pRet;
}

/*
 * Register a callback to be invoked each time a transaction is rolled
 * back by this database connection.
 */
void *
sql_rollback_hook(sql * db,	/* Attach the hook to this database */
		      void (*xCallback) (void *),	/* Callback function */
		      void *pArg)	/* Argument to the function */
{
	void *pRet;

#ifdef SQL_ENABLE_API_ARMOR
	if (!sqlSafetyCheckOk(db)) {
		return 0;
	}
#endif
	pRet = db->pRollbackArg;
	db->xRollbackCallback = xCallback;
	db->pRollbackArg = pArg;
	return pRet;
}

/*
 * Configure an sql_wal_hook() callback to automatically checkpoint
 * a database after committing a transaction if there are nFrame or
 * more frames in the log file. Passing zero or a negative value as the
 * nFrame parameter disables automatic checkpoints entirely.
 *
 * The callback registered by this function replaces any existing callback
 * registered using sql_wal_hook(). Likewise, registering a callback
 * using sql_wal_hook() disables the automatic checkpoint mechanism
 * configured by this function.
 */
int
sql_wal_autocheckpoint(sql * db, int nFrame)
{
	UNUSED_PARAMETER(db);
	UNUSED_PARAMETER(nFrame);
	return SQL_OK;
}

/*
 * This function returns true if main-memory should be used instead of
 * a temporary file for transient pager files and statement journals.
 * The value returned depends on the value of db->temp_store (runtime
 * parameter) and the compile time value of SQL_TEMP_STORE. The
 * following table describes the relationship between these two values
 * and this functions return value.
 *
 *   SQL_TEMP_STORE     db->temp_store     Location of temporary database
 *   -----------------     --------------     ------------------------------
 *   0                     any                file      (return 0)
 *   1                     1                  file      (return 0)
 *   1                     2                  memory    (return 1)
 *   1                     0                  file      (return 0)
 *   2                     1                  file      (return 0)
 *   2                     2                  memory    (return 1)
 *   2                     0                  memory    (return 1)
 *   3                     any                memory    (return 1)
 */
int
sqlTempInMemory(const sql * db)
{
#if SQL_TEMP_STORE==1
	return (db->temp_store == 2);
#endif
#if SQL_TEMP_STORE==2
	return (db->temp_store != 1);
#endif
#if SQL_TEMP_STORE==3
	UNUSED_PARAMETER(db);
	return 1;
#endif
#if SQL_TEMP_STORE<1 || SQL_TEMP_STORE>3
	UNUSED_PARAMETER(db);
	return 0;
#endif
}

/*
 * Return UTF-8 encoded English language explanation of the most recent
 * error.
 */
const char *
sql_errmsg(sql * db)
{
	const char *z;
	if (!db) {
		return sqlErrStr(SQL_NOMEM);
	}
	if (!sqlSafetyCheckSickOrOk(db)) {
		return sqlErrStr(SQL_MISUSE);
	}
	if (db->mallocFailed) {
		z = sqlErrStr(SQL_NOMEM);
	} else {
		testcase(db->pErr == 0);
		assert(!db->mallocFailed);
		if (db->errCode != SQL_TARANTOOL_ERROR) {
			z = (char *)sql_value_text(db->pErr);
			if (z == NULL)
				z = sqlErrStr(db->errCode);
		} else {
			z = diag_last_error(diag_get())->errmsg;
		}
		assert(z != NULL);
	}
	return z;
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

int
sql_extended_errcode(sql * db)
{
	if (db && !sqlSafetyCheckSickOrOk(db)) {
		return SQL_MISUSE;
	}
	if (!db || db->mallocFailed) {
		return SQL_NOMEM;
	}
	return db->errCode;
}

int
sql_system_errno(sql * db)
{
	return db ? db->iSysErrno : 0;
}

/*
 * Return a string that describes the kind of error specified in the
 * argument.  For now, this simply calls the internal sqlErrStr()
 * function.
 */
const char *
sql_errstr(int rc)
{
	return sqlErrStr(rc);
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
	SQL_MAX_WORKER_THREADS,
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
#if SQL_MAX_WORKER_THREADS<0 || SQL_MAX_WORKER_THREADS>50
#error SQL_MAX_WORKER_THREADS must be between 0 and 50
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

#ifdef SQL_ENABLE_API_ARMOR
	if (!sqlSafetyCheckOk(db)) {
		return -1;
	}
#endif

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
	assert(aHardLimit[SQL_LIMIT_WORKER_THREADS] ==
	       SQL_MAX_WORKER_THREADS);
	assert(SQL_LIMIT_WORKER_THREADS == (SQL_N_LIMIT - 1));

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

/*
 * This function is used to parse both URIs and non-URI filenames passed by the
 * user to API functions sql_open() or sql_open_v2(), and for database
 * URIs specified as part of ATTACH statements.
 *
 * The first argument to this function is the name of the VFS to use (or
 * a NULL to signify the default VFS) if the URI does not contain a "vfs=xxx"
 * query parameter. The second argument contains the URI (or non-URI filename)
 * itself. When this function is called the *pFlags variable should contain
 * the default flags to open the database handle with. The value stored in
 * *pFlags may be updated before returning if the URI filename contains
 * "cache=xxx" or "mode=xxx" query parameters.
 *
 * If successful, SQL_OK is returned. In this case *ppVfs is set to point to
 * the VFS that should be used to open the database file. *pzFile is set to
 * point to a buffer containing the name of the file to open. It is the
 * responsibility of the caller to eventually call sql_free() to release
 * this buffer.
 *
 * If an error occurs, then an sql error code is returned and *pzErrMsg
 * may be set to point to a buffer containing an English language error
 * message. It is the responsibility of the caller to eventually release
 * this buffer by calling sql_free().
 */
int
sqlParseUri(const char *zDefaultVfs,	/* VFS to use if no "vfs=xxx" query option */
		const char *zUri,		/* Nul-terminated URI to parse */
		unsigned int *pFlags,		/* IN/OUT: SQL_OPEN_XXX flags */
		sql_vfs ** ppVfs,		/* OUT: VFS to use */
		char **pzFile,			/* OUT: Filename component of URI */
		char **pzErrMsg)		/* OUT: Error message (if rc!=SQL_OK) */
{
	int rc = SQL_OK;
	unsigned int flags = *pFlags;
	const char *zVfs = zDefaultVfs;
	char *zFile;
	char c;
	int nUri = sqlStrlen30(zUri);

	assert(*pzErrMsg == 0);

	if (((flags & SQL_OPEN_URI)	/* IMP: R-48725-32206 */
	     ||sqlGlobalConfig.bOpenUri)	/* IMP: R-51689-46548 */
	    &&nUri >= 5 && memcmp(zUri, "file:", 5) == 0	/* IMP: R-57884-37496 */
	    ) {
		char *zOpt;
		int eState;	/* Parser state when parsing URI */
		int iIn;	/* Input character index */
		int iOut = 0;	/* Output character index */
		u64 nByte = nUri + 2;	/* Bytes of space to allocate */

		/* Make sure the SQL_OPEN_URI flag is set to indicate to the VFS xOpen
		 * method that there may be extra parameters following the file-name.
		 */
		flags |= SQL_OPEN_URI;

		for (iIn = 0; iIn < nUri; iIn++)
			nByte += (zUri[iIn] == '&');
		zFile = sql_malloc64(nByte);
		if (!zFile)
			return SQL_NOMEM;

		iIn = 5;
#ifdef SQL_ALLOW_URI_AUTHORITY
		if (strncmp(zUri + 5, "///", 3) == 0) {
			iIn = 7;
			/* The following condition causes URIs with five leading / characters
			 * like file://///host/path to be converted into UNCs like //host/path.
			 * The correct URI for that UNC has only two or four leading / characters
			 * file://host/path or file:////host/path.  But 5 leading slashes is a
			 * common error, we are told, so we handle it as a special case.
			 */
			if (strncmp(zUri + 7, "///", 3) == 0) {
				iIn++;
			}
		} else if (strncmp(zUri + 5, "//localhost/", 12) == 0) {
			iIn = 16;
		}
#else
		/* Discard the scheme and authority segments of the URI. */
		if (zUri[5] == '/' && zUri[6] == '/') {
			iIn = 7;
			while (zUri[iIn] && zUri[iIn] != '/')
				iIn++;
			if (iIn != 7
			    && (iIn != 16
				|| memcmp("localhost", &zUri[7], 9))) {
				*pzErrMsg =
				    sql_mprintf
				    ("invalid uri authority: %.*s", iIn - 7,
				     &zUri[7]);
				rc = SQL_ERROR;
				goto parse_uri_out;
			}
		}
#endif

		/* Copy the filename and any query parameters into the zFile buffer.
		 * Decode %HH escape codes along the way.
		 *
		 * Within this loop, variable eState may be set to 0, 1 or 2, depending
		 * on the parsing context. As follows:
		 *
		 *   0: Parsing file-name.
		 *   1: Parsing name section of a name=value query parameter.
		 *   2: Parsing value section of a name=value query parameter.
		 */
		eState = 0;
		while ((c = zUri[iIn]) != 0 && c != '#') {
			iIn++;
			if (c == '%' && sqlIsxdigit(zUri[iIn])
			    && sqlIsxdigit(zUri[iIn + 1])
			    ) {
				int octet = (sqlHexToInt(zUri[iIn++]) << 4);
				octet += sqlHexToInt(zUri[iIn++]);

				assert(octet >= 0 && octet < 256);
				if (octet == 0) {
#ifndef SQL_ENABLE_URI_00_ERROR
					/* This branch is taken when "%00" appears within the URI. In this
					 * case we ignore all text in the remainder of the path, name or
					 * value currently being parsed. So ignore the current character
					 * and skip to the next "?", "=" or "&", as appropriate.
					 */
					while ((c = zUri[iIn]) != 0 && c != '#'
					       && (eState != 0 || c != '?')
					       && (eState != 1
						   || (c != '=' && c != '&'))
					       && (eState != 2 || c != '&')
					    ) {
						iIn++;
					}
					continue;
#else
					/* If ENABLE_URI_00_ERROR is defined, "%00" in a URI is an error. */
					*pzErrMsg =
					    sql_mprintf
					    ("unexpected %%00 in uri");
					rc = SQL_ERROR;
					goto parse_uri_out;
#endif
				}
				c = octet;
			} else if (eState == 1 && (c == '&' || c == '=')) {
				if (zFile[iOut - 1] == 0) {
					/* An empty option name. Ignore this option altogether. */
					while (zUri[iIn] && zUri[iIn] != '#'
					       && zUri[iIn - 1] != '&')
						iIn++;
					continue;
				}
				if (c == '&') {
					zFile[iOut++] = '\0';
				} else {
					eState = 2;
				}
				c = 0;
			} else if ((eState == 0 && c == '?')
				   || (eState == 2 && c == '&')) {
				c = 0;
				eState = 1;
			}
			zFile[iOut++] = c;
		}
		if (eState == 1)
			zFile[iOut++] = '\0';
		zFile[iOut++] = '\0';
		zFile[iOut++] = '\0';

		/* Check if there were any options specified that should be interpreted
		 * here. Options that are interpreted here include "vfs" and those that
		 * correspond to flags that may be passed to the sql_open_v2()
		 * method.
		 */
		zOpt = &zFile[sqlStrlen30(zFile) + 1];
		while (zOpt[0]) {
			int nOpt = sqlStrlen30(zOpt);
			char *zVal = &zOpt[nOpt + 1];
			unsigned int nVal = sqlStrlen30(zVal);

			if (nOpt == 3 && memcmp("vfs", zOpt, 3) == 0) {
				zVfs = zVal;
			} else {
				struct OpenMode {
					const char *z;
					int mode;
				} *aMode = 0;
				char *zModeType = 0;
				int mask = 0;
				int limit = 0;

				if (nOpt == 5 && memcmp("cache", zOpt, 5) == 0) {
					static struct OpenMode aCacheMode[] = {
						{"shared",
						 SQL_OPEN_SHAREDCACHE},
						{"private",
						 SQL_OPEN_PRIVATECACHE},
						{0, 0}
					};

					mask =
					    SQL_OPEN_SHAREDCACHE |
					    SQL_OPEN_PRIVATECACHE;
					aMode = aCacheMode;
					limit = mask;
					zModeType = "cache";
				}
				if (nOpt == 4 && memcmp("mode", zOpt, 4) == 0) {
					static struct OpenMode aOpenMode[] = {
						{"ro", SQL_OPEN_READONLY},
						{"rw", SQL_OPEN_READWRITE},
						{"rwc",
						 SQL_OPEN_READWRITE |
						 SQL_OPEN_CREATE},
						{"memory", SQL_OPEN_MEMORY},
						{0, 0}
					};

					mask =
					    SQL_OPEN_READONLY |
					    SQL_OPEN_READWRITE |
					    SQL_OPEN_CREATE |
					    SQL_OPEN_MEMORY;
					aMode = aOpenMode;
					limit = mask & flags;
					zModeType = "access";
				}

				if (aMode) {
					int i;
					int mode = 0;
					for (i = 0; aMode[i].z; i++) {
						const char *z = aMode[i].z;
						if (nVal == sqlStrlen30(z)
						    && 0 == memcmp(zVal, z,
								   nVal)) {
							mode = aMode[i].mode;
							break;
						}
					}
					if (mode == 0) {
						*pzErrMsg =
						    sql_mprintf
						    ("no such %s mode: %s",
						     zModeType, zVal);
						rc = SQL_ERROR;
						goto parse_uri_out;
					}
					if ((mode & ~SQL_OPEN_MEMORY) >
					    limit) {
						*pzErrMsg =
						    sql_mprintf
						    ("%s mode not allowed: %s",
						     zModeType, zVal);
						rc = SQL_PERM;
						goto parse_uri_out;
					}
					flags = (flags & ~mask) | mode;
				}
			}

			zOpt = &zVal[nVal + 1];
		}

	} else {
		zFile = sql_malloc64(nUri + 2);
		if (!zFile)
			return SQL_NOMEM;
		if (nUri) {
			memcpy(zFile, zUri, nUri);
		}
		zFile[nUri] = '\0';
		zFile[nUri + 1] = '\0';
		flags &= ~SQL_OPEN_URI;
	}

	*ppVfs = sql_vfs_find(zVfs);
	if (*ppVfs == 0) {
		*pzErrMsg = sql_mprintf("no such vfs: %s", zVfs);
		rc = SQL_ERROR;
	}
 parse_uri_out:
	if (rc != SQL_OK) {
		sql_free(zFile);
		zFile = 0;
	}
	*pFlags = flags;
	*pzFile = zFile;
	return rc;
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

#ifdef SQL_ENABLE_API_ARMOR
	if (ppDb == 0)
		return SQL_MISUSE;
#endif
#ifndef SQL_OMIT_AUTOINIT
	rc = sql_initialize();
	if (rc)
		return rc;
#endif

	/* Allocate the sql data structure */
	db = sqlMallocZero(sizeof(sql));
	if (db == 0)
		goto opendb_out;
	db->errMask = 0xff;
	db->magic = SQL_MAGIC_BUSY;

	db->pVfs = sql_vfs_find(0);

	assert(sizeof(db->aLimit) == sizeof(aHardLimit));
	memcpy(db->aLimit, aHardLimit, sizeof(db->aLimit));
	db->aLimit[SQL_LIMIT_WORKER_THREADS] = SQL_DEFAULT_WORKER_THREADS;
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

#ifdef SQL_ENABLE_FTS5
	/* Register any built-in FTS5 module before loading the automatic
	 * extensions. This allows automatic extensions to register FTS5
	 * tokenizers and auxiliary functions.
	 */
	if (!db->mallocFailed && rc == SQL_OK) {
		rc = sqlFts5Init(db);
	}
#endif

#ifdef SQL_ENABLE_FTS1
	if (!db->mallocFailed) {
		extern int sqlFts1Init(sql *);
		rc = sqlFts1Init(db);
	}
#endif

#ifdef SQL_ENABLE_FTS2
	if (!db->mallocFailed && rc == SQL_OK) {
		extern int sqlFts2Init(sql *);
		rc = sqlFts2Init(db);
	}
#endif

#ifdef SQL_ENABLE_FTS3	/* automatically defined by SQL_ENABLE_FTS4 */
	if (!db->mallocFailed && rc == SQL_OK) {
		rc = sqlFts3Init(db);
	}
#endif

#ifdef SQL_ENABLE_ICU
	if (!db->mallocFailed && rc == SQL_OK) {
		rc = sqlIcuInit(db);
	}
#endif

#ifdef SQL_ENABLE_RTREE
	if (!db->mallocFailed && rc == SQL_OK) {
		rc = sqlRtreeInit(db);
	}
#endif

#ifdef SQL_ENABLE_JSON1
	if (!db->mallocFailed && rc == SQL_OK) {
		rc = sqlJson1Init(db);
	}
#endif

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
#ifdef SQL_ENABLE_SQLLOG
	if (sqlGlobalConfig.xSqllog) {
		/* Opening a db handle. Fourth parameter is passed 0. */
		void *pArg = sqlGlobalConfig.pSqllogArg;
		sqlGlobalConfig.xSqllog(pArg, db, zFilename, 0);
	}
#endif

	return rc;
}

/*
 * Enable or disable the extended result codes.
 */
int
sql_extended_result_codes(sql * db, int onoff)
{
#ifdef SQL_ENABLE_API_ARMOR
	if (!sqlSafetyCheckOk(db))
		return SQL_MISUSE;
#endif
	db->errMask = onoff ? 0xffffffff : 0xff;
	return SQL_OK;
}

/*
 * Interface to the testing logic.
 */
int
sql_test_control(int op, ...)
{
	int rc = 0;
#ifdef SQL_UNTESTABLE
	UNUSED_PARAMETER(op);
#else
	va_list ap;
	va_start(ap, op);
	switch (op) {

		/*
		 * Save the current state of the PRNG.
		 */
	case SQL_TESTCTRL_PRNG_SAVE:{
			sqlPrngSaveState();
			break;
		}

		/*
		 * Restore the state of the PRNG to the last state saved using
		 * PRNG_SAVE.  If PRNG_SAVE has never before been called, then
		 * this verb acts like PRNG_RESET.
		 */
	case SQL_TESTCTRL_PRNG_RESTORE:{
			sqlPrngRestoreState();
			break;
		}

		/*
		 * Reset the PRNG back to its uninitialized state.  The next call
		 * to sql_randomness() will reseed the PRNG using a single call
		 * to the xRandomness method of the default VFS.
		 */
	case SQL_TESTCTRL_PRNG_RESET:{
			sql_randomness(0, 0);
			break;
		}

		/*
		 *  sql_test_control(FAULT_INSTALL, xCallback)
		 *
		 * Arrange to invoke xCallback() whenever sqlFaultSim() is called,
		 * if xCallback is not NULL.
		 *
		 * As a test of the fault simulator mechanism itself, sqlFaultSim(0)
		 * is called immediately after installing the new callback and the return
		 * value from sqlFaultSim(0) becomes the return from
		 * sql_test_control().
		 */
	case SQL_TESTCTRL_FAULT_INSTALL:{
			typedef int (*TESTCALLBACKFUNC_t) (int);
			sqlGlobalConfig.xTestCallback =
			    va_arg(ap, TESTCALLBACKFUNC_t);
			rc = sqlFaultSim(0);
			break;
		}

		/*
		 *  sql_test_control(BENIGN_MALLOC_HOOKS, xBegin, xEnd)
		 *
		 * Register hooks to call to indicate which malloc() failures
		 * are benign.
		 */
	case SQL_TESTCTRL_BENIGN_MALLOC_HOOKS:{
			typedef void (*void_function) (void);
			void_function xBenignBegin;
			void_function xBenignEnd;
			xBenignBegin = va_arg(ap, void_function);
			xBenignEnd = va_arg(ap, void_function);
			sqlBenignMallocHooks(xBenignBegin, xBenignEnd);
			break;
		}

		/*
		 *  sql_test_control(SQL_TESTCTRL_PENDING_BYTE, unsigned int X)
		 *
		 * Set the PENDING byte to the value in the argument, if X>0.
		 * Make no changes if X==0.  Return the value of the pending byte
		 * as it existing before this routine was called.
		 *
		 * IMPORTANT:  Changing the PENDING byte from 0x40000000 results in
		 * an incompatible database file format.  Changing the PENDING byte
		 * while any database connection is open results in undefined and
		 * deleterious behavior.
		 */
	case SQL_TESTCTRL_PENDING_BYTE:{
			rc = PENDING_BYTE;
#ifndef SQL_OMIT_WSD
			{
				unsigned int newVal = va_arg(ap, unsigned int);
				if (newVal)
					sqlPendingByte = newVal;
			}
#endif
			break;
		}

		/*
		 *  sql_test_control(SQL_TESTCTRL_ASSERT, int X)
		 *
		 * This action provides a run-time test to see whether or not
		 * assert() was enabled at compile-time.  If X is true and assert()
		 * is enabled, then the return value is true.  If X is true and
		 * assert() is disabled, then the return value is zero.  If X is
		 * false and assert() is enabled, then the assertion fires and the
		 * process aborts.  If X is false and assert() is disabled, then the
		 * return value is zero.
		 */
	case SQL_TESTCTRL_ASSERT:{
			volatile int x = 0;
			assert( /*side-effects-ok */ (x = va_arg(ap, int)) !=
			       0);
			rc = x;
			break;
		}

		/*
		 *  sql_test_control(SQL_TESTCTRL_ALWAYS, int X)
		 *
		 * This action provides a run-time test to see how the ALWAYS and
		 * NEVER macros were defined at compile-time.
		 *
		 * The return value is ALWAYS(X).
		 *
		 * The recommended test is X==2.  If the return value is 2, that means
		 * ALWAYS() and NEVER() are both no-op pass-through macros, which is the
		 * default setting.  If the return value is 1, then ALWAYS() is either
		 * hard-coded to true or else it asserts if its argument is false.
		 * The first behavior (hard-coded to true) is the case if
		 * SQL_TESTCTRL_ASSERT shows that assert() is disabled and the second
		 * behavior (assert if the argument to ALWAYS() is false) is the case if
		 * SQL_TESTCTRL_ASSERT shows that assert() is enabled.
		 *
		 * The run-time test procedure might look something like this:
		 *
		 *    if( sql_test_control(SQL_TESTCTRL_ALWAYS, 2)==2 ){
		 *      // ALWAYS() and NEVER() are no-op pass-through macros
		 *    }else if( sql_test_control(SQL_TESTCTRL_ASSERT, 1) ){
		 *      // ALWAYS(x) asserts that x is true. NEVER(x) asserts x is false.
		 *    }else{
		 *      // ALWAYS(x) is a constant 1.  NEVER(x) is a constant 0.
		 *    }
		 */
	case SQL_TESTCTRL_ALWAYS:{
			int x = va_arg(ap, int);
			rc = ALWAYS(x);
			break;
		}

		/*
		 *   sql_test_control(SQL_TESTCTRL_BYTEORDER);
		 *
		 * The integer returned reveals the byte-order of the computer on which
		 * sql is running:
		 *
		 *       1     big-endian,    determined at run-time
		 *      10     little-endian, determined at run-time
		 *  432101     big-endian,    determined at compile-time
		 *  123410     little-endian, determined at compile-time
		 */
	case SQL_TESTCTRL_BYTEORDER:{
			rc = SQL_BYTEORDER * 100 + SQL_LITTLEENDIAN * 10 +
			    SQL_BIGENDIAN;
			break;
		}

		/*  sql_test_control(SQL_TESTCTRL_OPTIMIZATIONS, sql *db, int N)
		 *
		 * Enable or disable various optimizations for testing purposes.  The
		 * argument N is a bitmask of optimizations to be disabled.  For normal
		 * operation N should be 0.  The idea is that a test program (like the
		 * SQL Logic Test or SLT test module) can run the same SQL multiple times
		 * with various optimizations disabled to verify that the same answer
		 * is obtained in every case.
		 */
	case SQL_TESTCTRL_OPTIMIZATIONS:{
			sql *db = va_arg(ap, sql *);
			db->dbOptFlags = (u16) (va_arg(ap, int) & 0xffff);
			break;
		}

#ifdef SQL_N_KEYWORD
		/* sql_test_control(SQL_TESTCTRL_ISKEYWORD, const char *zWord)
		 *
		 * If zWord is a keyword recognized by the parser, then return the
		 * number of keywords.  Or if zWord is not a keyword, return 0.
		 *
		 * This test feature is only available in the amalgamation since
		 * the SQL_N_KEYWORD macro is not defined in this file if sql
		 * is built using separate source files.
		 */
	case SQL_TESTCTRL_ISKEYWORD:{
			const char *zWord = va_arg(ap, const char *);
			int n = sqlStrlen30(zWord);
			rc = (sqlKeywordCode((u8 *) zWord, n) !=
			      TK_ID) ? SQL_N_KEYWORD : 0;
			break;
		}
#endif

		/* sql_test_control(SQL_TESTCTRL_SCRATCHMALLOC, sz, &pNew, pFree);
		 *
		 * Pass pFree into sqlScratchFree().
		 * If sz>0 then allocate a scratch buffer into pNew.
		 */
	case SQL_TESTCTRL_SCRATCHMALLOC:{
			void *pFree, **ppNew;
			int sz;
			sz = va_arg(ap, int);
			ppNew = va_arg(ap, void **);
			pFree = va_arg(ap, void *);
			if (sz)
				*ppNew = sqlScratchMalloc(sz);
			sqlScratchFree(pFree);
			break;
		}

		/*   sql_test_control(SQL_TESTCTRL_LOCALTIME_FAULT, int onoff);
		 *
		 * If parameter onoff is non-zero, configure the wrappers so that all
		 * subsequent calls to localtime() and variants fail. If onoff is zero,
		 * undo this setting.
		 */
	case SQL_TESTCTRL_LOCALTIME_FAULT:{
			sqlGlobalConfig.bLocaltimeFault = va_arg(ap, int);
			break;
		}

		/*   sql_test_control(SQL_TESTCTRL_NEVER_CORRUPT, int);
		 *
		 * Set or clear a flag that indicates that the database file is always well-
		 * formed and never corrupt.  This flag is clear by default, indicating that
		 * database files might have arbitrary corruption.  Setting the flag during
		 * testing causes certain assert() statements in the code to be activated
		 * that demonstrat invariants on well-formed database files.
		 */
	case SQL_TESTCTRL_NEVER_CORRUPT:{
			sqlGlobalConfig.neverCorrupt = va_arg(ap, int);
			break;
		}

		/* Set the threshold at which OP_Once counters reset back to zero.
		 * By default this is 0x7ffffffe (over 2 billion), but that value is
		 * too big to test in a reasonable amount of time, so this control is
		 * provided to set a small and easily reachable reset value.
		 */
	case SQL_TESTCTRL_ONCE_RESET_THRESHOLD:{
			sqlGlobalConfig.iOnceResetThreshold =
			    va_arg(ap, int);
			break;
		}

		/*   sql_test_control(SQL_TESTCTRL_VDBE_COVERAGE, xCallback, ptr);
		 *
		 * Set the VDBE coverage callback function to xCallback with context
		 * pointer ptr.
		 */
	case SQL_TESTCTRL_VDBE_COVERAGE:{
#ifdef SQL_VDBE_COVERAGE
			typedef void (*branch_callback) (void *, int, u8, u8);
			sqlGlobalConfig.xVdbeBranch =
			    va_arg(ap, branch_callback);
			sqlGlobalConfig.pVdbeBranchArg = va_arg(ap, void *);
#endif
			break;
		}

		/*   sql_test_control(SQL_TESTCTRL_SORTER_MMAP, db, nMax); */
	case SQL_TESTCTRL_SORTER_MMAP:{
			sql *db = va_arg(ap, sql *);
			db->nMaxSorterMmap = va_arg(ap, int);
			break;
		}

		/*   sql_test_control(SQL_TESTCTRL_ISINIT);
		 *
		 * Return SQL_OK if sql has been initialized and SQL_ERROR if
		 * not.
		 */
	case SQL_TESTCTRL_ISINIT:{
			if (sqlGlobalConfig.isInit == 0)
				rc = SQL_ERROR;
			break;
		}
	}
	va_end(ap);
#endif				/* SQL_UNTESTABLE */
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

/*
 * Return a boolean value for a query parameter.
 */
int
sql_uri_boolean(const char *zFilename, const char *zParam, int bDflt)
{
	const char *z = sql_uri_parameter(zFilename, zParam);
	bDflt = bDflt != 0;
	return z ? sqlGetBoolean(z, bDflt) : bDflt;
}

/*
 * Return a 64-bit integer value for a query parameter.
 */
sql_int64
sql_uri_int64(const char *zFilename,	/* Filename as passed to xOpen */
		  const char *zParam,	/* URI parameter sought */
		  sql_int64 bDflt)	/* return if parameter is missing */
{
	const char *z = sql_uri_parameter(zFilename, zParam);
	int64_t v;
	if (z != NULL && sql_dec_or_hex_to_i64(z, &v) == 0)
		bDflt = v;
	return bDflt;
}


#ifdef SQL_ENABLE_SNAPSHOT
/*
 * Obtain a snapshot handle for the snapshot of database zDb currently
 * being read by handle db.
 */
int
sql_snapshot_get(sql * db,
		     const char *zDb, sql_snapshot ** ppSnapshot)
{
	int rc = SQL_ERROR;
	return rc;
}

/*
 * Open a read-transaction on the snapshot idendified by pSnapshot.
 */
int
sql_snapshot_open(sql * db,
		      const char *zDb, sql_snapshot * pSnapshot)
{
	int rc = SQL_ERROR;
	return rc;
}

/*
 * Recover as many snapshots as possible from the wal file associated with
 * schema zDb of database db.
 */
int
sql_snapshot_recover(sql * db, const char *zDb)
{
	int rc = SQL_ERROR;
	return rc;
}

/*
 * Free a snapshot handle obtained from sql_snapshot_get().
 */
void
sql_snapshot_free(sql_snapshot * pSnapshot)
{
	sql_free(pSnapshot);
}
#endif				/* SQL_ENABLE_SNAPSHOT */
