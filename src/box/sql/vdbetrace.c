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
 * This file contains code used to insert the values of host parameters
 * (aka "wildcards") into the SQL text output by sql_trace().
 *
 * The Vdbe parse-tree explainer is also found here.
 */
#include "sqlInt.h"
#include "mem.h"
#include "vdbeInt.h"

/*
 * zSql is a zero-terminated string of UTF-8 SQL text.  Return the number of
 * bytes in this text up to but excluding the first character in
 * a host parameter.  If the text contains no host parameters, return
 * the total number of bytes in the text.
 */
static int
findNextHostParameter(const char *zSql, int *pnToken)
{
	int tokenType;
	int nTotal = 0;
	int n;
	bool unused;

	*pnToken = 0;
	while (zSql[0]) {
		n = sql_token(zSql, &tokenType, &unused);
		assert(n > 0 && tokenType != TK_ILLEGAL);
		if (tokenType == TK_VARIABLE) {
			*pnToken = n;
			break;
		}
		nTotal += n;
		zSql += n;
	}
	return nTotal;
}

/*
 * This function returns a pointer to a nul-terminated string in memory
 * obtained from sqlDbMalloc(). If sql.nVdbeExec is 1, then the
 * string contains a copy of zRawSql but with host parameters expanded to
 * their current bindings. Or, if sql.nVdbeExec is greater than 1,
 * then the returned string holds a copy of zRawSql with "-- " prepended
 * to each line of text.
 *
 * The calling function is responsible for making sure the memory returned
 * is eventually freed.
 *
 * ALGORITHM:  Scan the input string looking for host parameters in any of
 * these forms:  ?, ?N, $A, @A, :A.  Take care to avoid text within
 * string literals, quoted identifier names, and comments.  For text forms,
 * the host parameter index is found by scanning the prepared
 * statement for the corresponding OP_Variable opcode.  Once the host
 * parameter index is known, locate the value in p->aVar[].  Then render
 * the value as a literal in place of the host parameter name.
 */
char *
sqlVdbeExpandSql(Vdbe * p,	/* The prepared statement being evaluated */
		     const char *zRawSql	/* Raw text of the SQL statement */
    )
{
	sql *db;		/* The database connection */
	int idx = 0;		/* Index of a host parameter */
	int nextIndex = 1;	/* Index of next ? host parameter */
	int n;			/* Length of a token prefix */
	int nToken;		/* Length of the parameter token */
	StrAccum out;		/* Accumulate the output here */
	char zBase[100];	/* Initial working space */

	db = p->db;
	sqlStrAccumInit(&out, 0, zBase, sizeof(zBase),
			    db->aLimit[SQL_LIMIT_LENGTH]);
	if (db->nVdbeExec > 1) {
		while (*zRawSql) {
			const char *zStart = zRawSql;
			while (*(zRawSql++) != '\n' && *zRawSql) ;
			sqlStrAccumAppend(&out, "-- ", 3);
			assert((zRawSql - zStart) > 0);
			sqlStrAccumAppend(&out, zStart,
					      (int)(zRawSql - zStart));
		}
	} else if (p->nVar == 0) {
		sqlStrAccumAppend(&out, zRawSql, sqlStrlen30(zRawSql));
	} else {
		while (zRawSql[0]) {
			n = findNextHostParameter(zRawSql, &nToken);
			assert(n > 0);
			sqlStrAccumAppend(&out, zRawSql, n);
			zRawSql += n;
			assert(zRawSql[0] || nToken == 0);
			if (nToken == 0)
				break;
			if (zRawSql[0] == '?') {
				if (nToken > 1) {
					assert(sqlIsdigit(zRawSql[1]));
					sqlGetInt32(&zRawSql[1], &idx);
				} else {
					idx = nextIndex;
				}
			} else {
				assert(zRawSql[0] == ':' || zRawSql[0] == '$' ||
				       zRawSql[0] == '@' || zRawSql[0] == '#');
				testcase(zRawSql[0] == ':');
				testcase(zRawSql[0] == '$');
				testcase(zRawSql[0] == '@');
				testcase(zRawSql[0] == '#');
				idx =
				    sqlVdbeParameterIndex(p, zRawSql,
							      nToken);
				assert(idx > 0);
			}
			zRawSql += nToken;
			nextIndex = idx + 1;
			assert(idx > 0 && idx <= p->nVar);
			const char *value = mem_str(&p->aVar[idx - 1]);
			sqlStrAccumAppend(&out, value, strlen(value));
		}
	}
	if (out.accError)
		sqlStrAccumReset(&out);
	return sqlStrAccumFinish(&out);
}

