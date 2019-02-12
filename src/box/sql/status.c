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
 * This module implements the sql_status() interface and related
 * functionality.
 */
#include "sqlInt.h"
#include "vdbeInt.h"
/*
 * Variables in which to record status information.
 */
#if SQL_PTRSIZE>4
typedef sql_int64 sqlStatValueType;
#else
typedef u32 sqlStatValueType;
#endif
typedef struct sqlStatType sqlStatType;
static SQL_WSD struct sqlStatType {
	sqlStatValueType nowValue[10];	/* Current value */
	sqlStatValueType mxValue[10];	/* Maximum value */
} sqlStat = { {
0,}, {
0,}};


/* The "wsdStat" macro will resolve to the status information
 * state vector.  If writable static data is unsupported on the target,
 * we have to locate the state vector at run-time.  In the more common
 * case where writable static data is supported, wsdStat can refer directly
 * to the "sqlStat" state vector declared above.
 */
#ifdef SQL_OMIT_WSD
#define wsdStatInit  sqlStatType *x = &GLOBAL(sqlStatType,sqlStat)
#define wsdStat x[0]
#else
#define wsdStatInit
#define wsdStat sqlStat
#endif

/*
 * Return the current value of a status parameter.
 */
sql_int64
sqlStatusValue(int op)
{
	wsdStatInit;
	assert(op >= 0 && op < ArraySize(wsdStat.nowValue));

	return wsdStat.nowValue[op];
}

/*
 * Add N to the value of a status record.
 *
 * The StatusUp() routine can accept positive or negative values for N.
 * The value of N is added to the current status value and the high-water
 * mark is adjusted if necessary.
 *
 * The StatusDown() routine lowers the current value by N.  The highwater
 * mark is unchanged.  N must be non-negative for StatusDown().
 */
void
sqlStatusUp(int op, int N)
{
	wsdStatInit;
	assert(op >= 0 && op < ArraySize(wsdStat.nowValue));

	wsdStat.nowValue[op] += N;
	if (wsdStat.nowValue[op] > wsdStat.mxValue[op]) {
		wsdStat.mxValue[op] = wsdStat.nowValue[op];
	}
}

void
sqlStatusDown(int op, int N)
{
	wsdStatInit;
	assert(N >= 0);

	assert(op >= 0 && op < ArraySize(wsdStat.nowValue));
	wsdStat.nowValue[op] -= N;
}

/*
 * Adjust the highwater mark if necessary.
 */
void
sqlStatusHighwater(int op, int X)
{
	sqlStatValueType newValue;
	wsdStatInit;
	assert(X >= 0);
	newValue = (sqlStatValueType) X;
	assert(op >= 0 && op < ArraySize(wsdStat.nowValue));

	assert(op == SQL_STATUS_MALLOC_SIZE
	       || op == SQL_STATUS_PAGECACHE_SIZE
	       || op == SQL_STATUS_SCRATCH_SIZE
	       || op == SQL_STATUS_PARSER_STACK);
	if (newValue > wsdStat.mxValue[op]) {
		wsdStat.mxValue[op] = newValue;
	}
}

/*
 * Query status information.
 */
int
sql_status64(int op,
		 sql_int64 * pCurrent,
		 sql_int64 * pHighwater, int resetFlag)
{
	wsdStatInit;
	if (op < 0 || op >= ArraySize(wsdStat.nowValue)) {
		return SQL_MISUSE_BKPT;
	}
#ifdef SQL_ENABLE_API_ARMOR
	if (pCurrent == 0 || pHighwater == 0)
		return SQL_MISUSE_BKPT;
#endif
	*pCurrent = wsdStat.nowValue[op];
	*pHighwater = wsdStat.mxValue[op];
	if (resetFlag) {
		wsdStat.mxValue[op] = wsdStat.nowValue[op];
	}
	return SQL_OK;
}

int
sql_status(int op, int *pCurrent, int *pHighwater, int resetFlag)
{
	sql_int64 iCur = 0, iHwtr = 0;
	int rc;
#ifdef SQL_ENABLE_API_ARMOR
	if (pCurrent == 0 || pHighwater == 0)
		return SQL_MISUSE_BKPT;
#endif
	rc = sql_status64(op, &iCur, &iHwtr, resetFlag);
	if (rc == 0) {
		*pCurrent = (int)iCur;
		*pHighwater = (int)iHwtr;
	}
	return rc;
}

/*
 * Query status information for a single database connection
 */
int
sql_db_status(sql * db,	/* The database connection whose status is desired */
		  int op,	/* Status verb */
		  int *pCurrent,	/* Write current value here */
		  int *pHighwater,	/* Write high-water mark here */
		  int resetFlag	/* Reset high-water mark if true */
    )
{
	int rc = SQL_OK;	/* Return code */
#ifdef SQL_ENABLE_API_ARMOR
	if (!sqlSafetyCheckOk(db) || pCurrent == 0 || pHighwater == 0) {
		return SQL_MISUSE_BKPT;
	}
#endif
	switch (op) {
	case SQL_DBSTATUS_LOOKASIDE_USED:{
			*pCurrent = db->lookaside.nOut;
			*pHighwater = db->lookaside.mxOut;
			if (resetFlag) {
				db->lookaside.mxOut = db->lookaside.nOut;
			}
			break;
		}

	case SQL_DBSTATUS_LOOKASIDE_HIT:
	case SQL_DBSTATUS_LOOKASIDE_MISS_SIZE:
	case SQL_DBSTATUS_LOOKASIDE_MISS_FULL:{
			testcase(op == SQL_DBSTATUS_LOOKASIDE_HIT);
			testcase(op == SQL_DBSTATUS_LOOKASIDE_MISS_SIZE);
			testcase(op == SQL_DBSTATUS_LOOKASIDE_MISS_FULL);
			assert((op - SQL_DBSTATUS_LOOKASIDE_HIT) >= 0);
			assert((op - SQL_DBSTATUS_LOOKASIDE_HIT) < 3);
			*pCurrent = 0;
			*pHighwater =
			    db->lookaside.anStat[op -
						 SQL_DBSTATUS_LOOKASIDE_HIT];
			if (resetFlag) {
				db->lookaside.anStat[op -
						     SQL_DBSTATUS_LOOKASIDE_HIT]
				    = 0;
			}
			break;
		}

		/*
		 * Return an approximation for the amount of memory currently used
		 * by all pagers associated with the given database connection.  The
		 * highwater mark is meaningless and is returned as zero.
		 */
	case SQL_DBSTATUS_CACHE_USED_SHARED:
	case SQL_DBSTATUS_CACHE_USED:{
			int totalUsed = 0;
			*pCurrent = totalUsed;
			*pHighwater = 0;
			break;
		}

		/*
		 * *pCurrent gets an accurate estimate of the amount of memory used
		 * to store the schema for database. *pHighwater is set to zero.
		 */
	case SQL_DBSTATUS_SCHEMA_USED:{
			int nByte = 0;	/* Used to accumulate return value */

			*pHighwater = 0;
			*pCurrent = nByte;
			break;
		}

		/*
		 * *pCurrent gets an accurate estimate of the amount of memory used
		 * to store all prepared statements.
		 * *pHighwater is set to zero.
		 */
	case SQL_DBSTATUS_STMT_USED:{
			struct Vdbe *pVdbe;	/* Used to iterate through VMs */
			int nByte = 0;	/* Used to accumulate return value */

			db->pnBytesFreed = &nByte;
			for (pVdbe = db->pVdbe; pVdbe; pVdbe = pVdbe->pNext) {
				sqlVdbeClearObject(db, pVdbe);
				sqlDbFree(db, pVdbe);
			}
			db->pnBytesFreed = 0;

			*pHighwater = 0;	/* IMP: R-64479-57858
			*/
			*pCurrent = nByte;

			break;
		}

		/*
		 * Set *pCurrent to the total cache hits or misses encountered by all
		 * pagers the database handle is connected to. *pHighwater is always set
		 * to zero.
		 */
	case SQL_DBSTATUS_CACHE_HIT:
	case SQL_DBSTATUS_CACHE_MISS:
	case SQL_DBSTATUS_CACHE_WRITE:{
			int nRet = 0;
			assert(SQL_DBSTATUS_CACHE_MISS ==
			       SQL_DBSTATUS_CACHE_HIT + 1);
			assert(SQL_DBSTATUS_CACHE_WRITE ==
			       SQL_DBSTATUS_CACHE_HIT + 2);

			*pHighwater = 0;	/* IMP: R-42420-56072
			*/
			/* IMP: R-54100-20147 */
			/* IMP: R-29431-39229 */
			*pCurrent = nRet;
			break;
		}

		/* Set *pCurrent to non-zero if there are unresolved deferred foreign
		 * key constraints.  Set *pCurrent to zero if all foreign key constraints
		 * have been satisfied.  The *pHighwater is always set to zero.
		 */
	case SQL_DBSTATUS_DEFERRED_FKS:{
			*pHighwater = 0;	/* IMP: R-11967-56545
			*/
			const struct txn *ptxn = in_txn();

			if (!ptxn || !ptxn->psql_txn) {
				*pCurrent = 0;
				break;
			}
			const struct sql_txn *psql_txn = ptxn->psql_txn;
			*pCurrent = psql_txn->fk_deferred_count > 0;
			break;
		}

	default:{
			rc = SQL_ERROR;
		}
	}
	return rc;
}
