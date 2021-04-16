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
 * This file contains code use to manipulate "Mem" structure.  A "Mem"
 * stores a single value in the VDBE.  Mem is an opaque structure visible
 * only within the VDBE.  Interface routines refer to a Mem using the
 * name sql_value
 */
#include "sqlInt.h"
#include "mem.h"
#include "vdbeInt.h"
#include "tarantoolInt.h"
#include "box/schema.h"
#include "box/tuple.h"
#include "mpstream/mpstream.h"

#if 0

/*
 * Context object passed by sqlStat4ProbeSetValue() through to
 * valueNew(). See comments above valueNew() for details.
 */
struct ValueNewStat4Ctx {
	Parse *pParse;
	struct index_def *pIdx;
	UnpackedRecord **ppRec;
	int iVal;
};

/*
 * Allocate and return a pointer to a new sql_value object. If
 * the second argument to this function is NULL, the object is allocated
 * by calling sqlValueNew().
 *
 * Otherwise, if the second argument is non-zero, then this function is
 * being called indirectly by sqlStat4ProbeSetValue(). If it has not
 * already been allocated, allocate the UnpackedRecord structure that
 * that function will return to its caller here. Then return a pointer to
 * an sql_value within the UnpackedRecord.a[] array.
 */
static sql_value *
valueNew(sql * db, struct ValueNewStat4Ctx *p)
{
	if (p) {
		UnpackedRecord *pRec = p->ppRec[0];

		if (pRec == NULL) {
			struct index_def *idx = p->pIdx;
			uint32_t part_count = idx->key_def->part_count;

			int nByte = sizeof(Mem) * part_count +
				    ROUND8(sizeof(UnpackedRecord));
			pRec = (UnpackedRecord *) sqlDbMallocZero(db,
								      nByte);
			if (pRec == NULL)
				return NULL;
			pRec->key_def = key_def_dup(idx->key_def);
			if (pRec->key_def == NULL) {
				sqlDbFree(db, pRec);
				sqlOomFault(db);
				return NULL;
			}
			pRec->aMem = (Mem *)((char *) pRec +
					     ROUND8(sizeof(UnpackedRecord)));
			for (uint32_t i = 0; i < part_count; i++) {
				pRec->aMem[i].type = MEM_NULL;
				assert(pRec->aMem[i].flags == 0);
				pRec->aMem[i].db = db;
			}
			p->ppRec[0] = pRec;
		}

		pRec->nField = p->iVal + 1;
		return &pRec->aMem[p->iVal];
	}

	return sqlValueNew(db);
}

/*
 * The expression object indicated by the second argument is guaranteed
 * to be a scalar SQL function. If
 *
 *   * all function arguments are SQL literals,
 *   * one of the SQL_FUNC_CONSTANT or _SLOCHNG function flags is set, and
 *   * the SQL_FUNC_NEEDCOLL function flag is not set,
 *
 * then this routine attempts to invoke the SQL function. Assuming no
 * error occurs, output parameter (*ppVal) is set to point to a value
 * object containing the result before returning 0.
 *
 * Type @type is applied to the result of the function before returning.
 * If the result is a text value, the sql_value object uses encoding
 * enc.
 *
 * If the conditions above are not met, this function returns 0
 * and sets (*ppVal) to NULL. Or, if an error occurs, (*ppVal) is set to
 * NULL and an sql error code returned.
 */
static int
valueFromFunction(sql * db,	/* The database connection */
		  Expr * p,	/* The expression to evaluate */
		  enum field_type type,
		  sql_value ** ppVal,	/* Write the new value here */
		  struct ValueNewStat4Ctx *pCtx	/* Second argument for valueNew() */
    )
{
	sql_context ctx;	/* Context object for function invocation */
	sql_value **apVal = 0;	/* Function arguments */
	int nVal = 0;		/* Size of apVal[] array */
	sql_value *pVal = 0;	/* New value */
	int rc = 0;	/* Return code */
	ExprList *pList = 0;	/* Function arguments */
	int i;			/* Iterator variable */

	assert(pCtx != 0);
	assert((p->flags & EP_TokenOnly) == 0);
	pList = p->x.pList;
	if (pList)
		nVal = pList->nExpr;
	struct func *func = sql_func_by_signature(p->u.zToken, nVal);
	if (func == NULL || func->def->language != FUNC_LANGUAGE_SQL_BUILTIN ||
	    !func->def->is_deterministic ||
	    sql_func_flag_is_set(func, SQL_FUNC_NEEDCOLL))
		return 0;

	if (pList) {
		apVal =
		    (sql_value **) sqlDbMallocZero(db,
							   sizeof(apVal[0]) *
							   nVal);
		if (apVal == 0) {
			rc = -1;
			goto value_from_function_out;
		}
		for (i = 0; i < nVal; i++) {
			rc = sqlValueFromExpr(db, pList->a[i].pExpr,
						  type, &apVal[i]);
			if (apVal[i] == 0 || rc != 0)
				goto value_from_function_out;
		}
	}

	pVal = valueNew(db, pCtx);
	if (pVal == 0) {
		rc = -1;
		goto value_from_function_out;
	}

	assert(!pCtx->pParse->is_aborted);
	memset(&ctx, 0, sizeof(ctx));
	ctx.pOut = pVal;
	ctx.func = func;
	((struct func_sql_builtin *)func)->call(&ctx, nVal, apVal);
	assert(!ctx.is_aborted);
	sql_value_apply_type(pVal, type);
	assert(rc == 0);

 value_from_function_out:
	if (rc != 0)
		pVal = 0;
	if (apVal) {
		for (i = 0; i < nVal; i++) {
			sqlValueFree(apVal[i]);
		}
		sqlDbFree(db, apVal);
	}

	*ppVal = pVal;
	return rc;
}

/*
 * Extract a value from the supplied expression in the manner described
 * above sqlValueFromExpr(). Allocate the sql_value object
 * using valueNew().
 *
 * If pCtx is NULL and an error occurs after the sql_value object
 * has been allocated, it is freed before returning. Or, if pCtx is not
 * NULL, it is assumed that the caller will free any allocated object
 * in all cases.
 */
static int
valueFromExpr(sql * db,	/* The database connection */
	      Expr * pExpr,	/* The expression to evaluate */
	      enum field_type type,
	      sql_value ** ppVal,	/* Write the new value here */
	      struct ValueNewStat4Ctx *pCtx	/* Second argument for valueNew() */
    )
{
	int op;
	char *zVal = 0;
	sql_value *pVal = 0;
	int negInt = 1;
	const char *zNeg = "";
	int rc = 0;

	assert(pExpr != 0);
	while ((op = pExpr->op) == TK_UPLUS || op == TK_SPAN)
		pExpr = pExpr->pLeft;
	if (NEVER(op == TK_REGISTER))
		op = pExpr->op2;

	/* Compressed expressions only appear when parsing the DEFAULT clause
	 * on a table column definition, and hence only when pCtx==0.  This
	 * check ensures that an EP_TokenOnly expression is never passed down
	 * into valueFromFunction().
	 */
	assert((pExpr->flags & EP_TokenOnly) == 0 || pCtx == 0);

	if (op == TK_CAST) {
		rc = valueFromExpr(db, pExpr->pLeft, pExpr->type, ppVal, pCtx);
		testcase(rc != 0);
		if (*ppVal) {
			sqlVdbeMemCast(*ppVal, pExpr->type);
			sql_value_apply_type(*ppVal, type);
		}
		return rc;
	}

	/* Handle negative integers in a single step.  This is needed in the
	 * case when the value is -9223372036854775808.
	 */
	if (op == TK_UMINUS
	    && (pExpr->pLeft->op == TK_INTEGER
		|| pExpr->pLeft->op == TK_FLOAT)) {
		pExpr = pExpr->pLeft;
		op = pExpr->op;
		negInt = -1;
		zNeg = "-";
	}

	if (op == TK_STRING || op == TK_FLOAT || op == TK_INTEGER) {
		pVal = valueNew(db, pCtx);
		if (pVal == 0)
			goto no_mem;
		if (ExprHasProperty(pExpr, EP_IntValue)) {
			mem_set_i64(pVal, (i64) pExpr->u.iValue * negInt);
		} else {
			zVal =
			    sqlMPrintf(db, "%s%s", zNeg, pExpr->u.zToken);
			if (zVal == 0)
				goto no_mem;
			sqlValueSetStr(pVal, -1, zVal, SQL_DYNAMIC);
		}
		if ((op == TK_INTEGER || op == TK_FLOAT) &&
		    type == FIELD_TYPE_SCALAR) {
			sql_value_apply_type(pVal, FIELD_TYPE_NUMBER);
		} else {
			sql_value_apply_type(pVal, type);
		}
		if (pVal->flags & (MEM_Int | MEM_Real))
			pVal->flags &= ~MEM_Str;
	} else if (op == TK_UMINUS) {
		/* This branch happens for multiple negative signs.  Ex: -(-5) */
		if (0 ==
		    sqlValueFromExpr(db, pExpr->pLeft, type, &pVal)
		    && pVal != 0) {
			if ((rc = vdbe_mem_numerify(pVal)) != 0)
				return rc;
			if (pVal->flags & MEM_Real) {
				pVal->u.r = -pVal->u.r;
			} else if ((pVal->flags & MEM_Int) != 0) {
				mem_set_u64(pVal, (uint64_t)(-pVal->u.i));
			} else if ((pVal->flags & MEM_UInt) != 0) {
				if (pVal->u.u > (uint64_t) INT64_MAX + 1) {
					/*
					 * FIXME: while resurrecting this func
					 * come up with way of dealing with
					 * this situation. In previous
					 * implementation it was conversion to
					 * double, but in this case
					 * -(UINT) x -> (DOUBLE) y and -y != x.
					 */
					unreachable();
				} else {
					mem_set_i64(pVal, (int64_t)(-pVal->u.u));
				}
			}
			sql_value_apply_type(pVal, type);
		}
	} else if (op == TK_NULL) {
		pVal = valueNew(db, pCtx);
		if (pVal == 0)
			goto no_mem;
		if ((rc = vdbe_mem_numerify(pVal)) != 0)
			return rc;
	}
#ifndef SQL_OMIT_BLOB_LITERAL
	else if (op == TK_BLOB) {
		int nVal;
		assert(pExpr->u.zToken[0] == 'x' || pExpr->u.zToken[0] == 'X');
		assert(pExpr->u.zToken[1] == '\'');
		pVal = valueNew(db, pCtx);
		if (!pVal)
			goto no_mem;
		zVal = &pExpr->u.zToken[2];
		nVal = sqlStrlen30(zVal) - 1;
		assert(zVal[nVal] == '\'');
		sqlVdbeMemSetStr(pVal, sqlHexToBlob(db, zVal, nVal),
				     nVal / 2, 0, SQL_DYNAMIC);
	}
#endif

	else if (op == TK_FUNCTION && pCtx != 0) {
		rc = valueFromFunction(db, pExpr, type, &pVal, pCtx);
	}

	*ppVal = pVal;
	return rc;

 no_mem:
	sqlOomFault(db);
	sqlDbFree(db, zVal);
	assert(*ppVal == 0);
	if (pCtx == 0)
		sqlValueFree(pVal);

	return -1;
}

/*
 * Create a new sql_value object, containing the value of pExpr.
 *
 * This only works for very simple expressions that consist of one constant
 * token (i.e. "5", "5.1", "'a string'"). If the expression can
 * be converted directly into a value, then the value is allocated and
 * a pointer written to *ppVal. The caller is responsible for deallocating
 * the value by passing it to sqlValueFree() later on. If the expression
 * cannot be converted to a value, then *ppVal is set to NULL.
 */
int
sqlValueFromExpr(sql * db,	/* The database connection */
		     Expr * pExpr,	/* The expression to evaluate */
		     enum field_type type,
		     sql_value ** ppVal	/* Write the new value here */
    )
{
	return pExpr ? valueFromExpr(db, pExpr, type, ppVal, 0) : 0;
}

/*
 * Attempt to extract a value from pExpr and use it to construct *ppVal.
 *
 * If pAlloc is not NULL, then an UnpackedRecord object is created for
 * pAlloc if one does not exist and the new value is added to the
 * UnpackedRecord object.
 *
 * A value is extracted in the following cases:
 *
 *  * (pExpr==0). In this case the value is assumed to be an SQL NULL,
 *
 *  * The expression is a bound variable, and this is a reprepare, or
 *
 *  * The expression is a literal value.
 *
 * On success, *ppVal is made to point to the extracted value.  The caller
 * is responsible for ensuring that the value is eventually freed.
 */
static int
stat4ValueFromExpr(Parse * pParse,	/* Parse context */
		   Expr * pExpr,	/* The expression to extract a value from */
		   enum field_type type,
		   struct ValueNewStat4Ctx *pAlloc,	/* How to allocate space.  Or NULL */
		   sql_value ** ppVal	/* OUT: New value object (or NULL) */
    )
{
	int rc = 0;
	sql_value *pVal = 0;
	sql *db = pParse->db;

	/* Skip over any TK_COLLATE nodes */
	pExpr = sqlExprSkipCollate(pExpr);

	if (!pExpr) {
		pVal = valueNew(db, pAlloc);
		if (pVal) {
			sqlVdbeMemSetNull((Mem *) pVal);
		}
	} else if (pExpr->op == TK_VARIABLE
		   || NEVER(pExpr->op == TK_REGISTER
			    && pExpr->op2 == TK_VARIABLE)
	    ) {
		Vdbe *v;
		int iBindVar = pExpr->iColumn;
		if ((v = pParse->pReprepare) != 0) {
			pVal = valueNew(db, pAlloc);
			if (pVal) {
				rc = mem_copy(pVal, &v->aVar[iBindVar - 1]);
				if (rc == 0)
					sql_value_apply_type(pVal, type);
				pVal->db = pParse->db;
			}
		}
	} else {
		rc = valueFromExpr(db, pExpr, type, &pVal, pAlloc);
	}

	assert(pVal == 0 || pVal->db == db);
	*ppVal = pVal;
	return rc;
}

/*
 * This function is used to allocate and populate UnpackedRecord
 * structures intended to be compared against sample index keys stored
 * in the sql_stat4 table.
 *
 * A single call to this function populates zero or more fields of the
 * record starting with field iVal (fields are numbered from left to
 * right starting with 0). A single field is populated if:
 *
 *  * (pExpr==0). In this case the value is assumed to be an SQL NULL,
 *
 *  * The expression is a bound variable, and this is a reprepare, or
 *
 *  * The sqlValueFromExpr() function is able to extract a value
 *    from the expression (i.e. the expression is a literal value).
 *
 * Or, if pExpr is a TK_VECTOR, one field is populated for each of the
 * vector components that match either of the two latter criteria listed
 * above.
 *
 * Before any value is appended to the record, the type of the
 * corresponding column within index pIdx is applied to it. Before
 * this function returns, output parameter *pnExtract is set to the
 * number of values appended to the record.
 *
 * When this function is called, *ppRec must either point to an object
 * allocated by an earlier call to this function, or must be NULL. If it
 * is NULL and a value can be successfully extracted, a new UnpackedRecord
 * is allocated (and *ppRec set to point to it) before returning.
 *
 * Unless an error is encountered, 0 is returned. It is not an
 * error if a value cannot be extracted from pExpr. If an error does
 * occur, an sql error code is returned.
 */
int
sqlStat4ProbeSetValue(Parse * pParse,	/* Parse context */
			  struct index_def *idx,
			  UnpackedRecord ** ppRec,	/* IN/OUT: Probe record */
			  Expr * pExpr,	/* The expression to extract a value from */
			  int nElem,	/* Maximum number of values to append */
			  int iVal,	/* Array element to populate */
			  int *pnExtract	/* OUT: Values appended to the record */
    )
{
	int rc = 0;
	int nExtract = 0;

	if (pExpr == 0 || pExpr->op != TK_SELECT) {
		int i;
		struct ValueNewStat4Ctx alloc;

		alloc.pParse = pParse;
		alloc.pIdx = idx;
		alloc.ppRec = ppRec;

		for (i = 0; i < nElem; i++) {
			sql_value *pVal = 0;
			Expr *pElem =
			    (pExpr ? sqlVectorFieldSubexpr(pExpr, i) : 0);
			enum field_type type =
				idx->key_def->parts[iVal + i].type;
			alloc.iVal = iVal + i;
			rc = stat4ValueFromExpr(pParse, pElem, type, &alloc,
						&pVal);
			if (!pVal)
				break;
			nExtract++;
		}
	}

	*pnExtract = nExtract;
	return rc;
}

/*
 * Attempt to extract a value from expression pExpr using the methods
 * as described for sqlStat4ProbeSetValue() above.
 *
 * If successful, set *ppVal to point to a new value object and return
 * 0. If no value can be extracted, but no other error occurs
 * (e.g. OOM), return 0 and set *ppVal to NULL. Or, if an error
 * does occur, return an sql error code. The final value of *ppVal
 * is undefined in this case.
 */
int
sqlStat4ValueFromExpr(Parse * pParse,	/* Parse context */
			  Expr * pExpr,	/* The expression to extract a value from */
			  enum field_type type,
			  sql_value ** ppVal	/* OUT: New value object (or NULL) */
    )
{
	return stat4ValueFromExpr(pParse, pExpr, type, 0, ppVal);
}

/**
 * Extract the col_num-th column from the record.  Write
 * the column value into *res.  If *res is initially NULL
 * then a new sql_value object is allocated.
 *
 * If *res is initially NULL then the caller is responsible for
 * ensuring that the value written into *res is eventually
 * freed.
 *
 * @param db Database handle.
 * @param record Pointer to buffer containing record.
 * @param col_num Column to extract.
 * @param[out] res Extracted value.
 *
 * @retval -1 on error or 0.
 */
int
sql_stat4_column(struct sql *db, const char *record, uint32_t col_num,
		 sql_value **res)
{
	/* Write result into this Mem object. */
	struct Mem *mem = *res;
	const char *a = record;
	assert(mp_typeof(a[0]) == MP_ARRAY);
	uint32_t col_cnt = mp_decode_array(&a);
	(void) col_cnt;
	assert(col_cnt > col_num);
	for (uint32_t i = 0; i < col_num; i++)
		mp_next(&a);
	if (mem == NULL) {
		mem = sqlValueNew(db);
		*res = mem;
		if (mem == NULL) {
			diag_set(OutOfMemory, sizeof(struct Mem),
				 "sqlValueNew", "mem");
			return -1;
		}
	}
	uint32_t unused;
	return mem_from_mp(mem, a, &unused);
}

/*
 * Unless it is NULL, the argument must be an UnpackedRecord object returned
 * by an earlier call to sqlStat4ProbeSetValue(). This call deletes
 * the object.
 */
void
sqlStat4ProbeFree(UnpackedRecord * pRec)
{
	if (pRec != NULL) {
		int part_count = pRec->key_def->part_count;
		struct Mem *aMem = pRec->aMem;
		for (int i = 0; i < part_count; i++)
			mem_destroy(&aMem[i]);
		sqlDbFree(aMem[0].db, pRec);
	}
}

#endif
