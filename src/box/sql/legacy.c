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
#include "box/execute.h"
#include "box/session.h"

#if 0

/*
 * Execute SQL code.  Return one of the SQL_ success/failure
 * codes.
 *
 * If the SQL is a query, then for each row in the query result
 * the xCallback() function is called.  pArg becomes the first
 * argument to xCallback().  If xCallback=NULL then no callback
 * is invoked, even for queries.
 */
int
sql_exec(sql * db,	/* The database on which the SQL executes */
	     const char *zSql,	/* The SQL to be executed */
	     sql_callback xCallback,	/* Invoke this callback routine */
	     void *pArg	/* First argument to xCallback() */
    )
{
	int rc = 0;	/* Return code */
	const char *zLeftover;	/* Tail of unprocessed SQL */
	sql_stmt *pStmt = 0;	/* The current SQL statement */
	char **azCols = 0;	/* Names of result columns */
	int callbackIsInit;	/* True if callback data is initialized */

	assert(db != NULL);
	if (zSql == 0)
		zSql = "";

	while (rc == 0 && zSql[0] != 0) {
		int nCol;
		char **azVals = 0;

		pStmt = 0;
		rc = sql_stmt_compile(zSql, -1, NULL, &pStmt, &zLeftover);
		assert(rc == 0 || pStmt == NULL);
		if (rc != 0)
			continue;
		if (!pStmt) {
			/* this happens for a comment or white-space */
			zSql = zLeftover;
			continue;
		}

		callbackIsInit = 0;
		nCol = sql_column_count(pStmt);

		while (1) {
			int i;
			rc = sql_step(pStmt);
			/* Invoke the callback function if required */
			if (xCallback != NULL && rc == SQL_ROW) {
				if (!callbackIsInit) {
					azCols =
					    sqlDbMallocZero(db,
								2 * nCol *
								sizeof(const
								       char *) +
								1);
					if (azCols == 0) {
						goto exec_out;
					}
					for (i = 0; i < nCol; i++) {
						azCols[i] =
						    (char *)
						    sql_column_name(pStmt,
									i);
						/* vdbe_metadata_set_col_name() installs column names as UTF8
						 * strings so there is no way for sql_column_name() to fail.
						 */
						assert(azCols[i] != 0);
					}
					callbackIsInit = 1;
				}
				if (rc == SQL_ROW) {
					azVals = &azCols[nCol];
					for (i = 0; i < nCol; i++) {
						azVals[i] =
						    (char *)
						    sql_column_text(pStmt,
									i);
						if (!azVals[i]
						    &&
						    sql_column_type(pStmt,
									i) !=
						    MP_NIL) {
							sqlOomFault(db);
							goto exec_out;
						}
					}
				}
				if (xCallback(pArg, nCol, azVals, azCols)) {
					/* EVIDENCE-OF: R-38229-40159 If the callback function to
					 * sql_exec() returns non-zero, then sql_exec() will
					 * return -1.
					 */
					rc = -1;
					sqlVdbeFinalize((Vdbe *) pStmt);
					pStmt = 0;
					goto exec_out;
				}
			}

			if (rc != SQL_ROW) {
				rc = sqlVdbeFinalize((Vdbe *) pStmt);
				pStmt = 0;
				zSql = zLeftover;
				while (sqlIsspace(zSql[0]))
					zSql++;
				break;
			}
		}

		sqlDbFree(db, azCols);
		azCols = 0;
	}

 exec_out:
	if (pStmt)
		sqlVdbeFinalize((Vdbe *) pStmt);
	sqlDbFree(db, azCols);

	assert(rc == 0);
	return rc;
}

#endif
