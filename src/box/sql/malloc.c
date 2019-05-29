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
 *
 * Memory allocation functions used throughout sql.
 */
#include "sqlInt.h"
#include <stdarg.h>

/*
 * Like malloc(), but remember the size of the allocation
 * so that we can find it later using sqlMemSize().
 *
 * For this low-level routine, we are guaranteed that nByte>0 because
 * cases of nByte<=0 will be intercepted and dealt with by higher level
 * routines.
 */
static void *
sql_sized_malloc(int nByte)
{
	sql_int64 *p;
	assert(nByte > 0);
	nByte = ROUND8(nByte);
	p = malloc(nByte + 8);
	if (p == NULL) {
		sql_get()->mallocFailed = 1;
		diag_set(OutOfMemory, nByte, "malloc", "p");
		return NULL;
	}
	p[0] = nByte;
	p++;
	return (void *)p;
}

/*
 * Like free() but works for allocations obtained from sql_sized_malloc()
 * or sql_sized_realloc().
 *
 * For this low-level routine, we already know that pPrior!=0 since
 * cases where pPrior==0 will have been intecepted and dealt with
 * by higher-level routines.
 */
static void
sql_sized_free(void *pPrior)
{
	sql_int64 *p = (sql_int64 *) pPrior;
	assert(pPrior != 0);
	p--;
	free(p);
}

/*
 * Report the allocated size of a prior return from sql_sized_malloc()
 * or sql_sized_realloc().
 */
static int
sql_sized_sizeof(void *pPrior)
{
	sql_int64 *p;
	assert(pPrior != 0);
	p = (sql_int64 *) pPrior;
	p--;
	return (int)p[0];
}

/*
 * Like realloc().  Resize an allocation previously obtained from
 * sqlMemMalloc().
 *
 * For this low-level interface, we know that pPrior!=0.  Cases where
 * pPrior==0 while have been intercepted by higher-level routine and
 * redirected to sql_sized_malloc.  Similarly, we know that nByte>0 because
 * cases where nByte<=0 will have been intercepted by higher-level
 * routines and redirected to sql_sized_free.
 */
static void *
sql_sized_realloc(void *pPrior, int nByte)
{
	sql_int64 *p = (sql_int64 *) pPrior;
	assert(pPrior != 0 && nByte > 0);
	assert(nByte == ROUND8(nByte));	/* EV: R-46199-30249 */
	p--;
	p = realloc(p, nByte + 8);
	if (p == NULL) {
		sql_get()->mallocFailed = 1;
		diag_set(OutOfMemory, nByte, "realloc", "p");
		return NULL;
	}
	p[0] = nByte;
	p++;
	return (void *)p;
}

/*
 * Attempt to release up to n bytes of non-essential memory currently
 * held by sql. An example of non-essential memory is memory used to
 * cache database pages that are not currently in use.
 */
int
sql_release_memory(int n)
{
	/* IMPLEMENTATION-OF: R-34391-24921 The sql_release_memory() routine
	 * is a no-op returning zero if sql is not compiled with
	 * SQL_ENABLE_MEMORY_MANAGEMENT.
	 */
	UNUSED_PARAMETER(n);
	return 0;
}

/*
 * An instance of the following object records the location of
 * each unused scratch buffer.
 */
typedef struct ScratchFreeslot {
	struct ScratchFreeslot *pNext;	/* Next unused scratch buffer */
} ScratchFreeslot;

/*
 * State information local to the memory allocation subsystem.
 */
static SQL_WSD struct Mem0Global {
	sql_int64 alarmThreshold;	/* The soft heap limit */

	/*
	 * Pointers to the end of sqlGlobalConfig.pScratch memory
	 * (so that a range test can be used to determine if an allocation
	 * being freed came from pScratch) and a pointer to the list of
	 * unused scratch allocations.
	 */
	void *pScratchEnd;
	ScratchFreeslot *pScratchFree;
	u32 nScratchFree;

	/*
	 * True if heap is nearly "full" where "full" is defined by the
	 * sql_soft_heap_limit() setting.
	 */
	int nearlyFull;
} mem0 = {
0, 0, 0, 0, 0};

#define mem0 GLOBAL(struct Mem0Global, mem0)


/*
 * Set the soft heap-size limit for the library. Passing a zero or
 * negative value indicates no limit.
 */
sql_int64
sql_soft_heap_limit64(sql_int64 n)
{
	sql_int64 priorLimit;
	sql_int64 excess;
	sql_int64 nUsed;
	priorLimit = mem0.alarmThreshold;
	if (n < 0) {
		return priorLimit;
	}
	mem0.alarmThreshold = n;
	nUsed = sqlStatusValue(SQL_STATUS_MEMORY_USED);
	mem0.nearlyFull = (n > 0 && n <= nUsed);
	excess = sql_memory_used() - n;
	if (excess > 0)
		sql_release_memory((int)(excess & 0x7fffffff));
	return priorLimit;
}

void
sql_soft_heap_limit(int n)
{
	if (n < 0)
		n = 0;
	sql_soft_heap_limit64(n);
}

/*
 * Initialize the memory allocation subsystem.
 */
void
sqlMallocInit(void)
{
	memset(&mem0, 0, sizeof(mem0));
	if (sqlGlobalConfig.pScratch && sqlGlobalConfig.szScratch >= 100
	    && sqlGlobalConfig.nScratch > 0) {
		int i, n, sz;
		ScratchFreeslot *pSlot;
		sz = ROUNDDOWN8(sqlGlobalConfig.szScratch);
		sqlGlobalConfig.szScratch = sz;
		pSlot = (ScratchFreeslot *) sqlGlobalConfig.pScratch;
		n = sqlGlobalConfig.nScratch;
		mem0.pScratchFree = pSlot;
		mem0.nScratchFree = n;
		for (i = 0; i < n - 1; i++) {
			pSlot->pNext = (ScratchFreeslot *) (sz + (char *)pSlot);
			pSlot = pSlot->pNext;
		}
		pSlot->pNext = 0;
		mem0.pScratchEnd = (void *)&pSlot[1];
	} else {
		mem0.pScratchEnd = 0;
		sqlGlobalConfig.pScratch = 0;
		sqlGlobalConfig.szScratch = 0;
		sqlGlobalConfig.nScratch = 0;
	}
	if (sqlGlobalConfig.pPage == 0 || sqlGlobalConfig.szPage < 512
	    || sqlGlobalConfig.nPage <= 0) {
		sqlGlobalConfig.pPage = 0;
		sqlGlobalConfig.szPage = 0;
	}
}

/*
 * Return true if the heap is currently under memory pressure - in other
 * words if the amount of heap used is close to the limit set by
 * sql_soft_heap_limit().
 */
int
sqlHeapNearlyFull(void)
{
	return mem0.nearlyFull;
}

/*
 * Return the amount of memory currently checked out.
 */
sql_int64
sql_memory_used(void)
{
	sql_int64 res, mx;
	sql_status64(SQL_STATUS_MEMORY_USED, &res, &mx, 0);
	return res;
}

/*
 * Trigger the alarm
 */
static void
sqlMallocAlarm(int nByte)
{
	if (mem0.alarmThreshold <= 0)
		return;
	sql_release_memory(nByte);
}

/*
 * Do a memory allocation with statistics and alarms.  Assume the
 * lock is already held.
 */
static int
mallocWithAlarm(int n, void **pp)
{
	int nFull;
	void *p;
	nFull = ROUND8(n);
	sqlStatusHighwater(SQL_STATUS_MALLOC_SIZE, n);
	if (mem0.alarmThreshold > 0) {
		sql_int64 nUsed =
		    sqlStatusValue(SQL_STATUS_MEMORY_USED);
		if (nUsed >= mem0.alarmThreshold - nFull) {
			mem0.nearlyFull = 1;
			sqlMallocAlarm(nFull);
		} else {
			mem0.nearlyFull = 0;
		}
	}
	p = sql_sized_malloc(nFull);
	if (p) {
		nFull = sqlMallocSize(p);
		sqlStatusUp(SQL_STATUS_MEMORY_USED, nFull);
		sqlStatusUp(SQL_STATUS_MALLOC_COUNT, 1);
	}
	*pp = p;
	return nFull;
}

/*
 * Allocate memory.  This routine is like sql_malloc() except that it
 * assumes the memory subsystem has already been initialized.
 */
void *
sqlMalloc(u64 n)
{
	void *p;
	if (n == 0 || n >= 0x7fffff00) {
		/* A memory allocation of a number of bytes which is near the maximum
		 * signed integer value might cause an integer overflow inside of the
		 * sql_sized_malloc().  Hence we limit the maximum size to 0x7fffff00, giving
		 * 255 bytes of overhead.  sql itself will never use anything near
		 * this amount.  The only way to reach the limit is with sql_malloc()
		 */
		p = 0;
	} else if (sqlGlobalConfig.bMemstat) {
		mallocWithAlarm((int)n, &p);
	} else {
		p = sql_sized_malloc((int)n);
	}
	assert(EIGHT_BYTE_ALIGNMENT(p));	/* IMP: R-11148-40995 */
	return p;
}

/*
 * This version of the memory allocation is for use by the application.
 * First make sure the memory subsystem is initialized, then do the
 * allocation.
 */
void *
sql_malloc(int n)
{
	return n <= 0 ? 0 : sqlMalloc(n);
}

void *
sql_malloc64(sql_uint64 n)
{
	return sqlMalloc(n);
}

/*
 * TRUE if p is a lookaside memory allocation from db
 */
static int
isLookaside(sql * db, void *p)
{
	return SQL_WITHIN(p, db->lookaside.pStart, db->lookaside.pEnd);
}

/*
 * Return the size of a memory allocation previously obtained from
 * sqlMalloc() or sql_malloc().
 */
int
sqlMallocSize(void *p)
{
	assert(sqlMemdebugHasType(p, MEMTYPE_HEAP));
	return sql_sized_sizeof(p);
}

int
sqlDbMallocSize(sql * db, void *p)
{
	assert(p != 0);
	if (db == 0 || !isLookaside(db, p)) {
#if SQL_DEBUG
		if (db == 0) {
			assert(sqlMemdebugNoType(p, (u8) ~ MEMTYPE_HEAP));
			assert(sqlMemdebugHasType(p, MEMTYPE_HEAP));
		} else {
			assert(sqlMemdebugHasType
			       (p, (MEMTYPE_LOOKASIDE | MEMTYPE_HEAP)));
			assert(sqlMemdebugNoType
			       (p, (u8) ~ (MEMTYPE_LOOKASIDE | MEMTYPE_HEAP)));
		}
#endif
		return sql_sized_sizeof(p);
	} else
		return db->lookaside.sz;
}

/*
 * Free memory previously obtained from sqlMalloc().
 */
void
sql_free(void *p)
{
	if (p == 0)
		return;		/* IMP: R-49053-54554 */
	assert(sqlMemdebugHasType(p, MEMTYPE_HEAP));
	assert(sqlMemdebugNoType(p, (u8) ~ MEMTYPE_HEAP));
	if (sqlGlobalConfig.bMemstat) {
		sqlStatusDown(SQL_STATUS_MEMORY_USED,
				  sqlMallocSize(p));
		sqlStatusDown(SQL_STATUS_MALLOC_COUNT, 1);
		sql_sized_free(p);
	} else
		sql_sized_free(p);
}

/*
 * Add the size of memory allocation "p" to the count in
 * *db->pnBytesFreed.
 */
static SQL_NOINLINE void
measureAllocationSize(sql * db, void *p)
{
	*db->pnBytesFreed += sqlDbMallocSize(db, p);
}

/*
 * Free memory that might be associated with a particular database
 * connection.
 */
void
sqlDbFree(sql * db, void *p)
{
	if (p == 0)
		return;
	if (db) {
		if (db->pnBytesFreed) {
			measureAllocationSize(db, p);
			return;
		}
		if (isLookaside(db, p)) {
			LookasideSlot *pBuf = (LookasideSlot *) p;
#if SQL_DEBUG
			/* Trash all content in the buffer being freed */
			memset(p, 0xaa, db->lookaside.sz);
#endif
			pBuf->pNext = db->lookaside.pFree;
			db->lookaside.pFree = pBuf;
			db->lookaside.nOut--;
			return;
		}
	}
	assert(sqlMemdebugHasType(p, (MEMTYPE_LOOKASIDE | MEMTYPE_HEAP)));
	assert(sqlMemdebugNoType
	       (p, (u8) ~ (MEMTYPE_LOOKASIDE | MEMTYPE_HEAP)));
	assert(db != 0 || sqlMemdebugNoType(p, MEMTYPE_LOOKASIDE));
	sqlMemdebugSetType(p, MEMTYPE_HEAP);
	sql_free(p);
}

/*
 * Change the size of an existing memory allocation
 */
void *
sqlRealloc(void *pOld, u64 nBytes)
{
	int nOld, nNew, nDiff;
	void *pNew;
	assert(sqlMemdebugHasType(pOld, MEMTYPE_HEAP));
	assert(sqlMemdebugNoType(pOld, (u8) ~ MEMTYPE_HEAP));
	if (pOld == 0) {
		return sqlMalloc(nBytes);	/* IMP: R-04300-56712 */
	}
	if (nBytes == 0) {
		sql_free(pOld);	/* IMP: R-26507-47431 */
		return 0;
	}
	if (nBytes >= 0x7fffff00) {
		/* The 0x7ffff00 limit term is explained in comments on sqlMalloc() */
		return 0;
	}
	nOld = sqlMallocSize(pOld);
	nNew = ROUND8((int)nBytes);
	if (nOld == nNew) {
		pNew = pOld;
	} else if (sqlGlobalConfig.bMemstat) {
		sqlStatusHighwater(SQL_STATUS_MALLOC_SIZE, (int)nBytes);
		nDiff = nNew - nOld;
		if (nDiff > 0
		    && sqlStatusValue(SQL_STATUS_MEMORY_USED) >=
		    mem0.alarmThreshold - nDiff) {
			sqlMallocAlarm(nDiff);
		}
		pNew = sql_sized_realloc(pOld, nNew);
		if (pNew == 0 && mem0.alarmThreshold > 0) {
			sqlMallocAlarm((int)nBytes);
			pNew = sql_sized_realloc(pOld, nNew);
		}
		if (pNew) {
			nNew = sqlMallocSize(pNew);
			sqlStatusUp(SQL_STATUS_MEMORY_USED, nNew - nOld);
		}
	} else {
		pNew = sql_sized_realloc(pOld, nNew);
	}
	assert(EIGHT_BYTE_ALIGNMENT(pNew));	/* IMP: R-11148-40995 */
	return pNew;
}

/*
 * The public interface to sqlRealloc.  Make sure that the memory
 * subsystem is initialized prior to invoking sqlRealloc.
 */
void *
sql_realloc64(void *pOld, sql_uint64 n)
{
	return sqlRealloc(pOld, n);
}

/*
 * Allocate and zero memory.
 */
void *
sqlMallocZero(u64 n)
{
	void *p = sqlMalloc(n);
	if (p) {
		memset(p, 0, (size_t) n);
	}
	return p;
}

/*
 * Allocate and zero memory.  If the allocation fails, make
 * the mallocFailed flag in the connection pointer.
 */
void *
sqlDbMallocZero(sql * db, u64 n)
{
	void *p;
	testcase(db == 0);
	p = sqlDbMallocRaw(db, n);
	if (p)
		memset(p, 0, (size_t) n);
	return p;
}

/* Finish the work of sqlDbMallocRawNN for the unusual and
 * slower case when the allocation cannot be fulfilled using lookaside.
 */
static SQL_NOINLINE void *
dbMallocRawFinish(sql * db, u64 n)
{
	void *p;
	assert(db != 0);
	p = sqlMalloc(n);
	if (!p)
		sqlOomFault(db);
	sqlMemdebugSetType(p,
			       (db->lookaside.bDisable ==
				0) ? MEMTYPE_LOOKASIDE : MEMTYPE_HEAP);
	return p;
}

/*
 * Allocate memory, either lookaside (if possible) or heap.
 * If the allocation fails, set the mallocFailed flag in
 * the connection pointer.
 *
 * If db!=0 and db->mallocFailed is true (indicating a prior malloc
 * failure on the same database connection) then always return 0.
 * Hence for a particular database connection, once malloc starts
 * failing, it fails consistently until mallocFailed is reset.
 * This is an important assumption.  There are many places in the
 * code that do things like this:
 *
 *         int *a = (int*)sqlDbMallocRaw(db, 100);
 *         int *b = (int*)sqlDbMallocRaw(db, 200);
 *         if( b ) a[10] = 9;
 *
 * In other words, if a subsequent malloc (ex: "b") worked, it is assumed
 * that all prior mallocs (ex: "a") worked too.
 *
 * The sqlMallocRawNN() variant guarantees that the "db" parameter is
 * not a NULL pointer.
 */
void *
sqlDbMallocRaw(sql * db, u64 n)
{
	void *p;
	if (db)
		return sqlDbMallocRawNN(db, n);
	p = sqlMalloc(n);
	sqlMemdebugSetType(p, MEMTYPE_HEAP);
	return p;
}

void *
sqlDbMallocRawNN(sql * db, u64 n)
{
	LookasideSlot *pBuf;
	assert(db != 0);
	assert(db->pnBytesFreed == 0);
	if (db->lookaside.bDisable == 0) {
		assert(db->mallocFailed == 0);
		if (n > db->lookaside.sz) {
			db->lookaside.anStat[1]++;
		} else if ((pBuf = db->lookaside.pFree) == 0) {
			db->lookaside.anStat[2]++;
		} else {
			db->lookaside.pFree = pBuf->pNext;
			db->lookaside.nOut++;
			db->lookaside.anStat[0]++;
			if (db->lookaside.nOut > db->lookaside.mxOut) {
				db->lookaside.mxOut = db->lookaside.nOut;
			}
			return (void *)pBuf;
		}
	} else if (db->mallocFailed) {
		return 0;
	}
	return dbMallocRawFinish(db, n);
}

/* Forward declaration */
static SQL_NOINLINE void *dbReallocFinish(sql * db, void *p, u64 n);

/*
 * Resize the block of memory pointed to by p to n bytes. If the
 * resize fails, set the mallocFailed flag in the connection object.
 */
void *
sqlDbRealloc(sql * db, void *p, u64 n)
{
	assert(db != 0);
	if (p == 0)
		return sqlDbMallocRawNN(db, n);
	if (isLookaside(db, p) && n <= db->lookaside.sz)
		return p;
	return dbReallocFinish(db, p, n);
}

static SQL_NOINLINE void *
dbReallocFinish(sql * db, void *p, u64 n)
{
	void *pNew = 0;
	assert(db != 0);
	assert(p != 0);
	if (db->mallocFailed == 0) {
		if (isLookaside(db, p)) {
			pNew = sqlDbMallocRawNN(db, n);
			if (pNew) {
				memcpy(pNew, p, db->lookaside.sz);
				sqlDbFree(db, p);
			}
		} else {
			assert(sqlMemdebugHasType
			       (p, (MEMTYPE_LOOKASIDE | MEMTYPE_HEAP)));
			assert(sqlMemdebugNoType
			       (p, (u8) ~ (MEMTYPE_LOOKASIDE | MEMTYPE_HEAP)));
			sqlMemdebugSetType(p, MEMTYPE_HEAP);
			pNew = sql_realloc64(p, n);
			if (!pNew) {
				sqlOomFault(db);
			}
			sqlMemdebugSetType(pNew,
					       (db->lookaside.bDisable ==
						0 ? MEMTYPE_LOOKASIDE :
						MEMTYPE_HEAP));
		}
	}
	return pNew;
}

/*
 * Attempt to reallocate p.  If the reallocation fails, then free p
 * and set the mallocFailed flag in the database connection.
 */
void *
sqlDbReallocOrFree(sql * db, void *p, u64 n)
{
	void *pNew;
	pNew = sqlDbRealloc(db, p, n);
	if (!pNew) {
		sqlDbFree(db, p);
	}
	return pNew;
}

/*
 * Make a copy of a string in memory obtained from sqlMalloc(). These
 * functions call sqlMallocRaw() directly instead of sqlMalloc(). This
 * is because when memory debugging is turned on, these two functions are
 * called via macros that record the current file and line number in the
 * ThreadData structure.
 */
char *
sqlDbStrDup(sql * db, const char *z)
{
	char *zNew;
	size_t n;
	if (z == 0) {
		return 0;
	}
	n = strlen(z) + 1;
	zNew = sqlDbMallocRaw(db, n);
	if (zNew) {
		memcpy(zNew, z, n);
	}
	return zNew;
}

char *
sqlDbStrNDup(sql * db, const char *z, u64 n)
{
	char *zNew;
	assert(db != 0);
	if (z == 0) {
		return 0;
	}
	assert((n & 0x7fffffff) == n);
	zNew = sqlDbMallocRawNN(db, n + 1);
	if (zNew) {
		memcpy(zNew, z, (size_t) n);
		zNew[n] = 0;
	}
	return zNew;
}

/*
 * Call this routine to record the fact that an OOM (out-of-memory) error
 * has happened.  This routine will set db->mallocFailed, and also
 * temporarily disable the lookaside memory allocator and interrupt
 * any running VDBEs.
 */
void
sqlOomFault(sql * db)
{
	if (db->mallocFailed == 0 && db->bBenignMalloc == 0) {
		db->mallocFailed = 1;
		if (db->nVdbeExec > 0) {
			db->u1.isInterrupted = 1;
		}
		db->lookaside.bDisable++;
	}
}

/*
 * This routine reactivates the memory allocator and clears the
 * db->mallocFailed flag as necessary.
 *
 * The memory allocator is not restarted if there are running
 * VDBEs.
 */
void
sqlOomClear(sql * db)
{
	if (db->mallocFailed && db->nVdbeExec == 0) {
		db->mallocFailed = 0;
		db->u1.isInterrupted = 0;
		assert(db->lookaside.bDisable > 0);
		db->lookaside.bDisable--;
	}
}

/*
 * Take actions at the end of an API call to indicate an OOM error
 */
static SQL_NOINLINE int
apiOomError(sql * db)
{
	sqlOomClear(db);
	sqlError(db, SQL_NOMEM);
	return SQL_NOMEM;
}

/*
 * This function must be called before exiting any API function (i.e.
 * returning control to the user) that has called sql_malloc or
 * sql_realloc.
 *
 * The returned value is normally a copy of the second argument to this
 * function. However, if a malloc() failure has occurred since the previous
 * invocation SQL_NOMEM is returned instead.
 *
 * If an OOM as occurred, then the connection error-code (the value
 * returned by sql_errcode()) is set to SQL_NOMEM.
 */
int
sqlApiExit(sql * db, int rc)
{
	assert(db != 0);
	if (db->mallocFailed || rc == SQL_IOERR_NOMEM) {
		return apiOomError(db);
	}
	return rc & db->errMask;
}
