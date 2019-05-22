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
 * state vector. In the common case where writable static data is
 * supported, wsdStat can refer directly  to the "sqlStat" state
 * vector declared above.
 */
#define wsdStatInit
#define wsdStat sqlStat

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
		return SQL_MISUSE;
	}
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
	rc = sql_status64(op, &iCur, &iHwtr, resetFlag);
	if (rc == 0) {
		*pCurrent = (int)iCur;
		*pHighwater = (int)iHwtr;
	}
	return rc;
}
