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
 * This file contains the sql_get_table() and sql_free_table()
 * interface routines.  These are just wrappers around the main
 * interface routine of sql_exec().
 *
 * These routines are in a separate files so that they will not be linked
 * if they are not used.
 */
#include "sqlInt.h"
#include <stdlib.h>
#include <string.h>

#ifndef SQL_OMIT_GET_TABLE

/*
 * This structure is used to pass data from sql_get_table() through
 * to the callback function is uses to build the result.
 */
typedef struct TabResult {
	char **azResult;	/* Accumulated output */
	char *zErrMsg;		/* Error message text, if an error occurs */
	u32 nAlloc;		/* Slots allocated for azResult[] */
	u32 nRow;		/* Number of rows in the result */
	u32 nColumn;		/* Number of columns in the result */
	u32 nData;		/* Slots used in azResult[].  (nRow+1)*nColumn */
	int rc;			/* Return code from sql_exec() */
} TabResult;

/*
 * This routine is called once for each row in the result table.  Its job
 * is to fill in the TabResult structure appropriately, allocating new
 * memory as necessary.
 */
static int
sql_get_table_cb(void *pArg, int nCol, char **argv, char **colv)
{
	TabResult *p = (TabResult *) pArg;	/* Result accumulator */
	int need;		/* Slots needed in p->azResult[] */
	int i;			/* Loop counter */
	char *z;		/* A single column of result */

	/* Make sure there is enough space in p->azResult to hold everything
	 * we need to remember from this invocation of the callback.
	 */
	if (p->nRow == 0 && argv != 0) {
		need = nCol * 2;
	} else {
		need = nCol;
	}
	if (p->nData + need > p->nAlloc) {
		char **azNew;
		p->nAlloc = p->nAlloc * 2 + need;
		azNew =
		    sql_realloc64(p->azResult, sizeof(char *) * p->nAlloc);
		if (azNew == 0)
			goto malloc_failed;
		p->azResult = azNew;
	}

	/* If this is the first row, then generate an extra row containing
	 * the names of all columns.
	 */
	if (p->nRow == 0) {
		p->nColumn = nCol;
		for (i = 0; i < nCol; i++) {
			z = sql_mprintf("%s", colv[i]);
			if (z == 0)
				goto malloc_failed;
			p->azResult[p->nData++] = z;
		}
	} else if ((int)p->nColumn != nCol) {
		sql_free(p->zErrMsg);
		p->zErrMsg =
		    sql_mprintf
		    ("sql_get_table() called with two or more incompatible queries");
		p->rc = SQL_ERROR;
		return 1;
	}

	/* Copy over the row data
	 */
	if (argv != 0) {
		for (i = 0; i < nCol; i++) {
			if (argv[i] == 0) {
				z = 0;
			} else {
				int n = sqlStrlen30(argv[i]) + 1;
				z = sql_malloc64(n);
				if (z == 0)
					goto malloc_failed;
				memcpy(z, argv[i], n);
			}
			p->azResult[p->nData++] = z;
		}
		p->nRow++;
	}
	return 0;

 malloc_failed:
	p->rc = SQL_NOMEM_BKPT;
	return 1;
}

/*
 * Query the database.  But instead of invoking a callback for each row,
 * malloc() for space to hold the result and return the entire results
 * at the conclusion of the call.
 *
 * The result that is written to ***pazResult is held in memory obtained
 * from malloc().  But the caller cannot free this memory directly.
 * Instead, the entire table should be passed to sql_free_table() when
 * the calling procedure is finished using it.
 */
int
sql_get_table(sql * db,		/* The database on which the SQL executes */
		  const char *zSql,	/* The SQL to be executed */
		  char ***pazResult,	/* Write the result table here */
		  int *pnRow,		/* Write the number of rows in the result here */
		  int *pnColumn,	/* Write the number of columns of result here */
		  char **pzErrMsg)	/* Write error messages here */
{
	int rc;
	TabResult res;

#ifdef SQL_ENABLE_API_ARMOR
	if (!sqlSafetyCheckOk(db) || pazResult == 0)
		return SQL_MISUSE_BKPT;
#endif
	*pazResult = 0;
	if (pnColumn)
		*pnColumn = 0;
	if (pnRow)
		*pnRow = 0;
	if (pzErrMsg)
		*pzErrMsg = 0;
	res.zErrMsg = 0;
	res.nRow = 0;
	res.nColumn = 0;
	res.nData = 1;
	res.nAlloc = 20;
	res.rc = SQL_OK;
	res.azResult = sql_malloc64(sizeof(char *) * res.nAlloc);
	if (res.azResult == 0) {
		db->errCode = SQL_NOMEM;
		return SQL_NOMEM_BKPT;
	}
	res.azResult[0] = 0;
	rc = sql_exec(db, zSql, sql_get_table_cb, &res, pzErrMsg);
	assert(sizeof(res.azResult[0]) >= sizeof(res.nData));
	res.azResult[0] = SQL_INT_TO_PTR(res.nData);
	if ((rc & 0xff) == SQL_ABORT) {
		sql_free_table(&res.azResult[1]);
		if (res.zErrMsg) {
			if (pzErrMsg) {
				sql_free(*pzErrMsg);
				*pzErrMsg = sql_mprintf("%s", res.zErrMsg);
			}
			sql_free(res.zErrMsg);
		}
		/* Assume 32-bit assignment is atomic */
		db->errCode = res.rc;
		return res.rc;
	}
	sql_free(res.zErrMsg);
	if (rc != SQL_OK) {
		sql_free_table(&res.azResult[1]);
		return rc;
	}
	if (res.nAlloc > res.nData) {
		char **azNew;
		azNew =
		    sql_realloc64(res.azResult, sizeof(char *) * res.nData);
		if (azNew == 0) {
			sql_free_table(&res.azResult[1]);
			db->errCode = SQL_NOMEM;
			return SQL_NOMEM_BKPT;
		}
		res.azResult = azNew;
	}
	*pazResult = &res.azResult[1];
	if (pnColumn)
		*pnColumn = res.nColumn;
	if (pnRow)
		*pnRow = res.nRow;
	return rc;
}

/*
 * This routine frees the space the sql_get_table() malloced.
 */
void
sql_free_table(char **azResult) /* Result returned from sql_get_table() */
{
	if (azResult) {
		int i, n;
		azResult--;
		assert(azResult != 0);
		n = SQL_PTR_TO_INT(azResult[0]);
		for (i = 1; i < n; i++) {
			if (azResult[i])
				sql_free(azResult[i]);
		}
		sql_free(azResult);
	}
}

#endif				/* SQL_OMIT_GET_TABLE */
