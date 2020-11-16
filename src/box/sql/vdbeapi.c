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
 * This file contains code use to implement APIs that are part of the
 * VDBE.
 */
#include "sqlInt.h"
#include "vdbeInt.h"
#include "box/session.h"

/*
 * Invoke the profile callback.  This routine is only called if we already
 * know that the profile callback is defined and needs to be invoked.
 */
static SQL_NOINLINE void
invokeProfileCallback(sql * db, Vdbe * p)
{
	sql_int64 iNow;
	sql_int64 iElapse;
	assert(p->startTime > 0);
	assert(db->xProfile != 0 || (db->mTrace & SQL_TRACE_PROFILE) != 0);
	assert(db->init.busy == 0);
	assert(p->zSql != 0);
	sqlOsCurrentTimeInt64(db->pVfs, &iNow);
	iElapse = (iNow - p->startTime) * 1000000;
	if (db->xProfile) {
		db->xProfile(db->pProfileArg, p->zSql, iElapse);
	}
	if (db->mTrace & SQL_TRACE_PROFILE) {
		db->xTrace(SQL_TRACE_PROFILE, db->pTraceArg, p,
			   (void *)&iElapse);
	}
	p->startTime = 0;
}

/*
 * The checkProfileCallback(DB,P) macro checks to see if a profile callback
 * is needed, and it invokes the callback if it is needed.
 */
#define checkProfileCallback(DB,P) \
   if( ((P)->startTime)>0 ){ invokeProfileCallback(DB,P); }

/*
 * The following routine destroys a virtual machine that is created by
 * the sql_stmt_compile() routine. The integer returned is an SQL_
 * success/failure code that describes the result of executing the virtual
 * machine.
 */
int
sql_stmt_finalize(sql_stmt * pStmt)
{
	if (pStmt == NULL)
		return 0;
	Vdbe *v = (Vdbe *) pStmt;
	sql *db = v->db;
	assert(db != NULL);
	checkProfileCallback(db, v);
	return sqlVdbeFinalize(v);
}

int
sql_stmt_reset(sql_stmt *pStmt)
{
	assert(pStmt != NULL);
	struct Vdbe *v = (Vdbe *) pStmt;
	struct sql *db = v->db;
	checkProfileCallback(db, v);
	int rc = sqlVdbeReset(v);
	sqlVdbeRewind(v);
	return rc;
}

/*
 * Set all the parameters in the compiled SQL statement to NULL.
 */
int
sql_clear_bindings(sql_stmt * pStmt)
{
	int i;
	int rc = 0;
	Vdbe *p = (Vdbe *) pStmt;
	for (i = 0; i < p->nVar; i++) {
		sqlVdbeMemRelease(&p->aVar[i]);
		p->aVar[i].flags = MEM_Null;
	}
	return rc;
}

bool
sql_metadata_is_full()
{
	return current_session()->sql_flags & SQL_FullMetadata;
}

/**************************** sql_value_  ******************************
 * The following routines extract information from a Mem or sql_value
 * structure.
 */
const void *
sql_value_blob(sql_value * pVal)
{
	Mem *p = (Mem *) pVal;
	if (p->flags & (MEM_Blob | MEM_Str)) {
		if (ExpandBlob(p) != 0) {
			assert(p->flags == MEM_Null && p->z == 0);
			return 0;
		}
		p->flags |= MEM_Blob;
		return p->n ? p->z : 0;
	} else {
		return sql_value_text(pVal);
	}
}

int
sql_value_bytes(sql_value * pVal)
{
	return sqlValueBytes(pVal);
}

double
sql_value_double(sql_value * pVal)
{
	double v = 0.0;
	sqlVdbeRealValue((Mem *) pVal, &v);
	return v;
}

bool
sql_value_boolean(sql_value *val)
{
	bool b = false;
	int rc = mem_value_bool((struct Mem *) val, &b);
	assert(rc == 0);
	(void) rc;
	return b;
}

int
sql_value_int(sql_value * pVal)
{
	int64_t i = 0;
	bool is_neg;
	sqlVdbeIntValue((Mem *) pVal, &i, &is_neg);
	return (int)i;
}

sql_int64
sql_value_int64(sql_value * pVal)
{
	int64_t i = 0;
	bool unused;
	sqlVdbeIntValue((Mem *) pVal, &i, &unused);
	return i;
}

uint64_t
sql_value_uint64(sql_value *val)
{
	int64_t i = 0;
	bool is_neg;
	sqlVdbeIntValue((struct Mem *) val, &i, &is_neg);
	assert(!is_neg);
	return i;
}

enum sql_subtype
sql_value_subtype(sql_value * pVal)
{
	return (pVal->flags & MEM_Subtype) != 0 ? pVal->subtype : SQL_SUBTYPE_NO;
}

const unsigned char *
sql_value_text(sql_value * pVal)
{
	return (const unsigned char *)sqlValueText(pVal);
}

/* EVIDENCE-OF: R-12793-43283 Every value in sql has one of five
 * fundamental datatypes: 64-bit signed integer 64-bit IEEE floating
 * point number string BLOB NULL
 */
enum mp_type
sql_value_type(sql_value *pVal)
{
	struct Mem *mem = (struct Mem *) pVal;
	return mem_mp_type(mem);
}

/* Make a copy of an sql_value object
 */
sql_value *
sql_value_dup(const sql_value * pOrig)
{
	sql_value *pNew;
	if (pOrig == 0)
		return 0;
	pNew = sql_malloc(sizeof(*pNew));
	if (pNew == 0)
		return 0;
	memset(pNew, 0, sizeof(*pNew));
	memcpy(pNew, pOrig, MEMCELLSIZE);
	pNew->flags &= ~MEM_Dyn;
	pNew->db = 0;
	if (pNew->flags & (MEM_Str | MEM_Blob)) {
		pNew->flags &= ~(MEM_Static | MEM_Dyn);
		pNew->flags |= MEM_Ephem;
		if (sqlVdbeMemMakeWriteable(pNew) != 0) {
			sqlValueFree(pNew);
			pNew = 0;
		}
	}
	return pNew;
}

/* Destroy an sql_value object previously obtained from
 * sql_value_dup().
 */
void
sql_value_free(sql_value * pOld)
{
	sqlValueFree(pOld);
}

/**************************** sql_result_  ******************************
 * The following routines are used by user-defined functions to specify
 * the function result.
 *
 * The setStrOrError() function calls sqlVdbeMemSetStr() to store the
 * result as a string or blob but if the string or blob is too large, it
 * then sets the error code.
 *
 * The invokeValueDestructor(P,X) routine invokes destructor function X()
 * on value P is not going to be used and need to be destroyed.
 */
static void
setResultStrOrError(sql_context * pCtx,	/* Function context */
		    const char *z,	/* String pointer */
		    int n,	/* Bytes in string, or negative */
		    void (*xDel) (void *)	/* Destructor function */
    )
{
	if (sqlVdbeMemSetStr(pCtx->pOut, z, n, 1, xDel) != 0)
		pCtx->is_aborted = true;
}

static int
invokeValueDestructor(const void *p,	/* Value to destroy */
		      void (*xDel) (void *),	/* The destructor */
		      sql_context *pCtx	/* Set an error if no NULL */
    )
{
	assert(xDel != SQL_DYNAMIC);
	if (xDel == 0) {
		/* noop */
	} else if (xDel == SQL_TRANSIENT) {
		/* noop */
	} else {
		xDel((void *)p);
	}
	if (pCtx) {
		diag_set(ClientError, ER_SQL_EXECUTE, "string or binary string"\
			 "is too big");
		pCtx->is_aborted = true;
	}
	return -1;
}

void
sql_result_blob(sql_context * pCtx,
		    const void *z, int n, void (*xDel) (void *)
    )
{
	assert(n >= 0);
	if (sqlVdbeMemSetStr(pCtx->pOut, z, n, 0, xDel) != 0)
		pCtx->is_aborted = true;
}

void
sql_result_blob64(sql_context * pCtx,
		      const void *z, sql_uint64 n, void (*xDel) (void *)
    )
{
	assert(xDel != SQL_DYNAMIC);
	if (n > 0x7fffffff) {
		(void)invokeValueDestructor(z, xDel, pCtx);
	} else {
		setResultStrOrError(pCtx, z, (int)n, xDel);
	}
}

void
sql_result_double(sql_context * pCtx, double rVal)
{
	mem_set_double(pCtx->pOut, rVal);
}

void
sql_result_uint(sql_context *ctx, uint64_t u_val)
{
	mem_set_u64(ctx->pOut, u_val);
}

void
sql_result_int(sql_context *ctx, int64_t val)
{
	mem_set_i64(ctx->pOut, val);
}

void
sql_result_bool(struct sql_context *ctx, bool value)
{
	mem_set_bool(ctx->pOut, value);
}

void
sql_result_null(sql_context * pCtx)
{
	sqlVdbeMemSetNull(pCtx->pOut);
}

void
sql_result_text(sql_context * pCtx,
		    const char *z, int n, void (*xDel) (void *)
    )
{
	setResultStrOrError(pCtx, z, n, xDel);
}

void
sql_result_text64(sql_context * pCtx,
		      const char *z,
		      sql_uint64 n,
		      void (*xDel) (void *))
{
	assert(xDel != SQL_DYNAMIC);
	if (n > 0x7fffffff) {
		(void)invokeValueDestructor(z, xDel, pCtx);
	} else {
		setResultStrOrError(pCtx, z, (int)n, xDel);
	}
}

void
sql_result_value(sql_context * pCtx, sql_value * pValue)
{
	sqlVdbeMemCopy(pCtx->pOut, pValue);
}

void
sql_result_zeroblob(sql_context * pCtx, int n)
{
	sqlVdbeMemSetZeroBlob(pCtx->pOut, n);
}

int
sql_result_zeroblob64(sql_context * pCtx, u64 n)
{
	Mem *pOut = pCtx->pOut;
	if (n > (u64) pOut->db->aLimit[SQL_LIMIT_LENGTH]) {
		diag_set(ClientError, ER_SQL_EXECUTE, "string or binary string"\
			 "is too big");
		return -1;
	}
	sqlVdbeMemSetZeroBlob(pCtx->pOut, (int)n);
	return 0;
}

/*
 * Execute the statement pStmt, either until a row of data is ready, the
 * statement is completely executed or an error occurs.
 *
 * This routine implements the bulk of the logic behind the sql_step()
 * API.  The only thing omitted is the automatic recompile if a
 * schema change has occurred.  That detail is handled by the
 * outer sql_step() wrapper procedure.
 */
static int
sqlStep(Vdbe * p)
{
	sql *db;
	int rc;

	assert(p);
	if (p->magic != VDBE_MAGIC_RUN)
		sql_stmt_reset((sql_stmt *) p);

	/* Check that malloc() has not failed. If it has, return early. */
	db = p->db;
	if (db->mallocFailed) {
		p->is_aborted = true;
		return -1;
	}

	if (p->pc <= 0 && p->expired) {
		p->is_aborted = true;
		return -1;
	}
	if (p->pc < 0) {

		if ((db->xProfile || (db->mTrace & SQL_TRACE_PROFILE) != 0)
		    && !db->init.busy && p->zSql) {
			sqlOsCurrentTimeInt64(db->pVfs, &p->startTime);
		} else {
			assert(p->startTime == 0);
		}

		db->nVdbeActive++;
		p->pc = 0;
	}
	if (p->explain) {
		rc = sqlVdbeList(p);
	} else {
		db->nVdbeExec++;
		rc = sqlVdbeExec(p);
		db->nVdbeExec--;
	}

	/* If the statement completed successfully, invoke the profile callback */
	if (rc != SQL_ROW)
		checkProfileCallback(db, p);

	if (rc != SQL_ROW && rc != SQL_DONE) {
		/* If this statement was prepared using sql_prepare(), and an
		 * error has occurred, then return an error.
		 */
		if (p->is_aborted)
			rc = -1;
	}
	return rc;
}

/*
 * This is the top-level implementation of sql_step().  Call
 * sqlStep() to do most of the work.  If a schema error occurs,
 * call sqlReprepare() and try again.
 */
int
sql_step(sql_stmt * pStmt)
{
	Vdbe *v = (Vdbe *) pStmt;	/* the prepared statement */
	assert(v != NULL);
	return sqlStep(v);
}

/*
 * Extract the user data from a sql_context structure and return a
 * pointer to it.
 *
 * IMPLEMENTATION-OF: R-46798-50301 The sql_context_db_handle() interface
 * returns a copy of the pointer to the database connection (the 1st
 * parameter) of the sql_create_function() and
 * sql_create_function16() routines that originally registered the
 * application defined function.
 */
sql *
sql_context_db_handle(sql_context * p)
{
	assert(p && p->pOut);
	return p->pOut->db;
}

/*
 * Return the current time for a statement.  If the current time
 * is requested more than once within the same run of a single prepared
 * statement, the exact same time is returned for each invocation regardless
 * of the amount of time that elapses between invocations.  In other words,
 * the time returned is always the time of the first call.
 */
sql_int64
sqlStmtCurrentTime(sql_context * p)
{
	int rc;
#ifndef SQL_ENABLE_OR_STAT4
	sql_int64 *piTime = &p->pVdbe->iCurrentTime;
	assert(p->pVdbe != 0);
#else
	sql_int64 iTime = 0;
	sql_int64 *piTime =
	    p->pVdbe != 0 ? &p->pVdbe->iCurrentTime : &iTime;
#endif
	if (*piTime == 0) {
		rc = sqlOsCurrentTimeInt64(p->pOut->db->pVfs, piTime);
		if (rc)
			*piTime = 0;
	}
	return *piTime;
}

/*
 * Create a new aggregate context for p and return a pointer to
 * its pMem->z element.
 */
static SQL_NOINLINE void *
createAggContext(sql_context * p, int nByte)
{
	Mem *pMem = p->pMem;
	assert((pMem->flags & MEM_Agg) == 0);
	if (nByte <= 0) {
		sqlVdbeMemSetNull(pMem);
		pMem->z = 0;
	} else {
		sqlVdbeMemClearAndResize(pMem, nByte);
		pMem->flags = MEM_Agg;
		pMem->u.func = p->func;
		if (pMem->z) {
			memset(pMem->z, 0, nByte);
		}
	}
	return (void *)pMem->z;
}

/*
 * Allocate or return the aggregate context for a user function.  A new
 * context is allocated on the first call.  Subsequent calls return the
 * same context that was returned on prior calls.
 */
void *
sql_aggregate_context(sql_context * p, int nByte)
{
	assert(p != NULL && p->func != NULL);
	assert(p->func->def->language == FUNC_LANGUAGE_SQL_BUILTIN);
	assert(p->func->def->aggregate == FUNC_AGGREGATE_GROUP);
	testcase(nByte < 0);
	if ((p->pMem->flags & MEM_Agg) == 0) {
		return createAggContext(p, nByte);
	} else {
		return (void *)p->pMem->z;
	}
}

/*
 * Return the number of columns in the result set for the statement pStmt.
 */
int
sql_column_count(sql_stmt * pStmt)
{
	Vdbe *pVm = (Vdbe *) pStmt;
	return pVm ? pVm->nResColumn : 0;
}

/*
 * Return the number of values available from the current row of the
 * currently executing statement pStmt.
 */
int
sql_data_count(sql_stmt * pStmt)
{
	Vdbe *pVm = (Vdbe *) pStmt;
	if (pVm == 0 || pVm->pResultSet == 0)
		return 0;
	return pVm->nResColumn;
}

/*
 * Return a pointer to static memory containing an SQL NULL value.
 */
static const Mem *
columnNullValue(void)
{
	/* Even though the Mem structure contains an element
	 * of type i64, on certain architectures (x86) with certain compiler
	 * switches (-Os), gcc may align this Mem object on a 4-byte boundary
	 * instead of an 8-byte one. This all works fine, except that when
	 * running with SQL_DEBUG defined the sql code sometimes assert()s
	 * that a Mem structure is located on an 8-byte boundary. To prevent
	 * these assert()s from failing, when building with SQL_DEBUG defined
	 * using gcc, we force nullMem to be 8-byte aligned using the magical
	 * __attribute__((aligned(8))) macro.
	 */
	static const Mem nullMem
#if defined(SQL_DEBUG) && defined(__GNUC__)
	    __attribute__ ((aligned(8)))
#endif
	    = {
		/* .u          = */  {
		0},
		    /* .flags      = */ (u16) MEM_Null,
		    /* .eSubtype   = */ (u8) 0,
		    /* .field_type = */ field_type_MAX,
		    /* .n          = */ (int)0,
		    /* .z          = */ (char *)0,
		    /* .zMalloc    = */ (char *)0,
		    /* .szMalloc   = */ (int)0,
		    /* .uTemp      = */ (u32) 0,
		    /* .db         = */ (sql *) 0,
		    /* .xDel       = */ (void (*)(void *))0,
#ifdef SQL_DEBUG
		    /* .pScopyFrom = */ (Mem *) 0,
		    /* .pFiller    = */ (void *)0,
#endif
	};
	return &nullMem;
}

/*
 * Check to see if column iCol of the given statement is valid.  If
 * it is, return a pointer to the Mem for the value of that column.
 * If iCol is not valid, return a pointer to a Mem which has a value
 * of NULL.
 */
static Mem *
columnMem(sql_stmt * pStmt, int i)
{
	Vdbe *pVm;
	Mem *pOut;

	pVm = (Vdbe *) pStmt;
	if (pVm == 0)
		return (Mem *) columnNullValue();
	assert(pVm->db);
	if (pVm->pResultSet != 0 && i < pVm->nResColumn && i >= 0) {
		pOut = &pVm->pResultSet[i];
	} else {
		pOut = (Mem *) columnNullValue();
	}
	return pOut;
}

/**************************** sql_column_  ******************************
 * The following routines are used to access elements of the current row
 * in the result set.
 */
const void *
sql_column_blob(sql_stmt * pStmt, int i)
{
	const void *val;
	val = sql_value_blob(columnMem(pStmt, i));
	return val;
}

int
sql_column_bytes(sql_stmt * pStmt, int i)
{
	return sql_value_bytes(columnMem(pStmt, i));
}

double
sql_column_double(sql_stmt * pStmt, int i)
{
	return sql_value_double(columnMem(pStmt, i));
}

int
sql_column_int(sql_stmt * pStmt, int i)
{
	return sql_value_int(columnMem(pStmt, i));
}

bool
sql_column_boolean(struct sql_stmt *stmt, int i)
{
	return sql_value_boolean(columnMem(stmt, i));
}

sql_int64
sql_column_int64(sql_stmt * pStmt, int i)
{
	return sql_value_int64(columnMem(pStmt, i));
}

uint64_t
sql_column_uint64(sql_stmt * pStmt, int i)
{
	return sql_value_uint64(columnMem(pStmt, i));
}

const unsigned char *
sql_column_text(sql_stmt * pStmt, int i)
{
	return sql_value_text(columnMem(pStmt, i));
}

sql_value *
sql_column_value(sql_stmt * pStmt, int i)
{
	Mem *pOut = columnMem(pStmt, i);
	if (pOut->flags & MEM_Static) {
		pOut->flags &= ~MEM_Static;
		pOut->flags |= MEM_Ephem;
	}
	return (sql_value *) pOut;
}

enum mp_type
sql_column_type(sql_stmt * pStmt, int i)
{
	return sql_value_type(columnMem(pStmt, i));
}

enum sql_subtype
sql_column_subtype(struct sql_stmt *stmt, int i)
{
	return sql_value_subtype(columnMem(stmt, i));
}

/*
 * Return the name of the Nth column of the result set returned by SQL
 * statement pStmt.
 */
const char *
sql_column_name(sql_stmt *stmt, int n)
{
	struct Vdbe *p = (struct Vdbe *) stmt;
	assert(n < sql_column_count(stmt) && n >= 0);
	return p->metadata[n].name;
}

const char *
sql_column_datatype(sql_stmt *stmt, int n)
{
	struct Vdbe *p = (struct Vdbe *) stmt;
	assert(n < sql_column_count(stmt) && n >= 0);
	return p->metadata[n].type;
}

const char *
sql_column_coll(sql_stmt *stmt, int n)
{
	struct Vdbe *p = (struct Vdbe *) stmt;
	assert(n < sql_column_count(stmt) && n >= 0);
	return p->metadata[n].collation;
}

int
sql_column_nullable(sql_stmt *stmt, int n)
{
	struct Vdbe *p = (struct Vdbe *) stmt;
	assert(n < sql_column_count(stmt) && n >= 0);
	return p->metadata[n].nullable;
}

bool
sql_column_is_autoincrement(sql_stmt *stmt, int n)
{
	struct Vdbe *p = (struct Vdbe *) stmt;
	assert(n < sql_column_count(stmt) && n >= 0);
	return p->metadata[n].is_actoincrement;
}

const char *
sql_column_span(sql_stmt *stmt, int n) {
	struct Vdbe *p = (struct Vdbe *) stmt;
	assert(n < sql_column_count(stmt) && n >= 0);
	return p->metadata[n].span;
}

uint32_t
sql_stmt_schema_version(const struct sql_stmt *stmt)
{
	struct Vdbe *v = (struct Vdbe *) stmt;
	return v->schema_ver;
}

static size_t
sql_metadata_size(const struct sql_column_metadata *metadata)
{
	size_t size = sizeof(*metadata);
	if (metadata->type != NULL)
		size += strlen(metadata->type);
	if (metadata->name != NULL)
		size += strlen(metadata->name);
	if (metadata->collation != NULL)
		size += strlen(metadata->collation);
	return size;
}

size_t
sql_stmt_est_size(const struct sql_stmt *stmt)
{
	if (stmt == NULL)
		return 0;
	struct Vdbe *v = (struct Vdbe *) stmt;
	size_t size = sizeof(*v);
	/* Names and types of result set columns */
	for (int i = 0; i < v->nResColumn; ++i)
		size += sql_metadata_size(&v->metadata[i]);
	/* Opcodes */
	size += sizeof(struct VdbeOp) * v->nOp;
	/* Memory cells */
	size += sizeof(struct Mem) * v->nMem;
	/* Bindings */
	size += sizeof(struct Mem) * v->nVar;
	/* Bindings included in the result set */
	size += sizeof(uint32_t) * v->res_var_count;
	/* Cursors */
	size += sizeof(struct VdbeCursor *) * v->nCursor;

	for (int i = 0; i < v->nOp; ++i) {
		/* Estimate size of p4 operand. */
		if (v->aOp[i].p4type == P4_NOTUSED)
			continue;
		switch (v->aOp[i].p4type) {
		case P4_DYNAMIC:
		case P4_STATIC:
			if (v->aOp[i].opcode == OP_Blob ||
			    v->aOp[i].opcode == OP_String)
				size += v->aOp[i].p1;
			else if (v->aOp[i].opcode == OP_String8)
				size += strlen(v->aOp[i].p4.z);
			break;
		case P4_BOOL:
			size += sizeof(v->aOp[i].p4.b);
			break;
		case P4_INT32:
			size += sizeof(v->aOp[i].p4.i);
			break;
		case P4_UINT64:
		case P4_INT64:
			size += sizeof(*v->aOp[i].p4.pI64);
			break;
		case P4_REAL:
			size += sizeof(*v->aOp[i].p4.pReal);
			break;
		default:
			size += sizeof(v->aOp[i].p4.p);
			break;
		}
	}
	size += strlen(v->zSql);
	return size;
}

const char *
sql_stmt_query_str(const struct sql_stmt *stmt)
{
	if (stmt != NULL) {
		const struct Vdbe *v = (const struct Vdbe *) stmt;
		return v->zSql;
	}

	return NULL;
}

/******************************* sql_bind_  **************************
 *
 * Routines used to attach values to wildcards in a compiled SQL statement.
 */
/*
 * Unbind the value bound to variable i in virtual machine p. This is the
 * the same as binding a NULL value to the column.
 */
static int
vdbeUnbind(Vdbe * p, int i)
{
	Mem *pVar;
	assert(p != NULL);
	assert(p->magic == VDBE_MAGIC_RUN && p->pc < 0);
	assert(i > 0);
	if(i > p->nVar) {
		diag_set(ClientError, ER_SQL_EXECUTE, "The number of "\
			 "parameters is too large");
		return -1;
	}
	i--;
	pVar = &p->aVar[i];
	sqlVdbeMemRelease(pVar);
	pVar->flags = MEM_Null;
	pVar->field_type = field_type_MAX;
	return 0;
}

/**
 * This function sets type for bound variable.
 * We should bind types only for variables which occur in
 * result set of SELECT query. For example:
 *
 * SELECT id, ?, ?, a WHERE id = ?;
 *
 * In this case we should set types only for two variables.
 * That one which is situated under WHERE condition - is out
 * of our interest.
 *
 * For named binding parameters we should propagate type
 * for all occurrences of this parameter - since binding
 * routine takes place only once for each DISTINCT parameter
 * from list.
 *
 * @param v Current VDBE.
 * @param position Ordinal position of binding parameter.
 * @param type String literal representing type of binding param.
 * @retval 0 on success.
 */
static int
sql_bind_type(struct Vdbe *v, uint32_t position, const char *type)
{
	if (v->res_var_count < position)
		return 0;
	int rc = 0;
	if (vdbe_metadata_set_col_type(v, v->var_pos[position - 1], type) != 0)
		rc = -1;
	const char *bind_name = v->metadata[position - 1].name;
	if (strcmp(bind_name, "?") == 0)
		return rc;
	for (uint32_t i = position; i < v->res_var_count; ++i) {
		if (strcmp(bind_name, v->metadata[i].name) == 0) {
			if (vdbe_metadata_set_col_type(v, v->var_pos[i],
						       type) != 0)
				return -1;
		}
	}
	return 0;
}

void
sql_unbind(struct sql_stmt *stmt)
{
	struct Vdbe *v = (struct Vdbe *) stmt;
	for (int i = 1; i < v->nVar + 1; ++i) {
		int rc = vdbeUnbind(v, i);
		assert(rc == 0);
		(void) rc;
		/*
		 * We should re-set boolean type - unassigned
		 * binding slots are assumed to contain NULL
		 * value, which has boolean type.
		 */
		sql_bind_type(v, i, "boolean");
	}
}

/*
 * Bind a text or BLOB value.
 */
static int
bindText(sql_stmt * pStmt,	/* The statement to bind against */
	 int i,			/* Index of the parameter to bind */
	 const void *zData,	/* Pointer to the data to be bound */
	 int nData,		/* Number of bytes of data to be bound */
	 void (*xDel) (void *)	/* Destructor for the data */
    )
{
	Vdbe *p = (Vdbe *) pStmt;
	Mem *pVar;
	if (vdbeUnbind(p, i) != 0) {
		if (xDel != SQL_STATIC && xDel != SQL_TRANSIENT)
			xDel((void *)zData);
		return -1;
	}
	if (zData == NULL)
		return 0;
	pVar = &p->aVar[i - 1];
	if (sqlVdbeMemSetStr(pVar, zData, nData, 1, xDel) != 0)
		return -1;
	return sql_bind_type(p, i, "text");
}

/*
 * Bind a blob value to an SQL statement variable.
 */
int
sql_bind_blob(sql_stmt * pStmt,
		  int i, const void *zData, int nData, void (*xDel) (void *)
    )
{
	struct Vdbe *p = (Vdbe *) pStmt;
	if (vdbeUnbind(p, i) != 0) {
		if (xDel != SQL_STATIC && xDel != SQL_TRANSIENT)
			xDel((void *)zData);
		return -1;
	}
	if (zData == NULL)
		return 0;
	struct Mem *var = &p->aVar[i - 1];
	if (sqlVdbeMemSetStr(var, zData, nData, 0, xDel) != 0)
		return -1;
	return sql_bind_type(p, i, "varbinary");
}

int
sql_bind_blob64(sql_stmt * pStmt,
		    int i,
		    const void *zData,
		    sql_uint64 nData, void (*xDel) (void *)
    )
{
	assert(xDel != SQL_DYNAMIC);
	if (nData > 0x7fffffff) {
		return invokeValueDestructor(zData, xDel, 0);
	} else {
		return sql_bind_blob(pStmt, i, zData, (int)nData, xDel);
	}
}

int
sql_bind_double(sql_stmt * pStmt, int i, double rValue)
{
	Vdbe *p = (Vdbe *) pStmt;
	if (vdbeUnbind(p, i) != 0)
		return -1;
	int rc = sql_bind_type(p, i, "numeric");
	mem_set_double(&p->aVar[i - 1], rValue);
	return rc;
}

int
sql_bind_boolean(struct sql_stmt *stmt, int i, bool value)
{
	struct Vdbe *p = (struct Vdbe *) stmt;
	if (vdbeUnbind(p, i) != 0)
		return -1;
	int rc = sql_bind_type(p, i, "boolean");
	mem_set_bool(&p->aVar[i - 1], value);
	return rc;
}

int
sql_bind_int(sql_stmt * p, int i, int iValue)
{
	return sql_bind_int64(p, i, (i64) iValue);
}

int
sql_bind_int64(sql_stmt * pStmt, int i, sql_int64 iValue)
{
	Vdbe *p = (Vdbe *) pStmt;
	if (vdbeUnbind(p, i) != 0)
		return -1;
	int rc = sql_bind_type(p, i, "integer");
	assert(iValue < 0);
	mem_set_int(&p->aVar[i - 1], iValue, true);
	return rc;
}

int
sql_bind_uint64(struct sql_stmt *stmt, int i, uint64_t value)
{
	struct Vdbe *p = (struct Vdbe *) stmt;
	if (vdbeUnbind(p, i) != 0)
		return -1;
	int rc = sql_bind_type(p, i, "integer");
	mem_set_u64(&p->aVar[i - 1], value);
	return rc;
}

int
sql_bind_null(sql_stmt * pStmt, int i)
{
	Vdbe *p = (Vdbe *) pStmt;
	if (vdbeUnbind(p, i) != 0)
		return -1;
	return sql_bind_type(p, i, "boolean");
}

int
sql_bind_ptr(struct sql_stmt *stmt, int i, void *ptr)
{
	struct Vdbe *p = (struct Vdbe *) stmt;
	int rc = vdbeUnbind(p, i);
	if (rc == 0) {
		rc = sql_bind_type(p, i, "varbinary");
		mem_set_ptr(&p->aVar[i - 1], ptr);
	}
	return rc;
}

int
sql_bind_text(sql_stmt * pStmt,
		  int i, const char *zData, int nData, void (*xDel) (void *)
    )
{
	return bindText(pStmt, i, zData, nData, xDel);
}

int
sql_bind_text64(sql_stmt * pStmt,
		    int i,
		    const char *zData,
		    sql_uint64 nData,
		    void (*xDel) (void *))
{
	assert(xDel != SQL_DYNAMIC);
	if (nData > 0x7fffffff) {
		return invokeValueDestructor(zData, xDel, 0);
	} else {
		return bindText(pStmt, i, zData, (int)nData, xDel);
	}
}

int
sql_bind_zeroblob(sql_stmt * pStmt, int i, int n)
{
	Vdbe *p = (Vdbe *) pStmt;
	if (vdbeUnbind(p, i) != 0)
		return -1;
	sqlVdbeMemSetZeroBlob(&p->aVar[i - 1], n);
	return 0;
}

int
sql_bind_zeroblob64(sql_stmt * pStmt, int i, sql_uint64 n)
{
	Vdbe *p = (Vdbe *) pStmt;
	if (n > (u64) p->db->aLimit[SQL_LIMIT_LENGTH]) {
		diag_set(ClientError, ER_SQL_EXECUTE, "string or binary string"\
			 "is too big");
		return -1;
	}
	assert((n & 0x7FFFFFFF) == n);
	return sql_bind_zeroblob(pStmt, i, n);
}

int
sql_bind_parameter_count(const struct sql_stmt *stmt)
{
	struct Vdbe *p = (struct Vdbe *) stmt;
	return p->nVar;
}

const char *
sql_bind_parameter_name(const struct sql_stmt *stmt, int i)
{
	struct Vdbe *p = (struct Vdbe *) stmt;
	if (p == NULL)
		return NULL;
	return sqlVListNumToName(p->pVList, i+1);
}

/*
 * Given a wildcard parameter name, return the index of the variable
 * with that name.  If there is no variable with the given name,
 * return 0.
 */
int
sqlVdbeParameterIndex(Vdbe * p, const char *zName, int nName)
{
	if (p == 0 || zName == 0)
		return 0;
	return sqlVListNameToNum(p->pVList, zName, nName);
}

int
sql_bind_parameter_index(sql_stmt * pStmt, const char *zName)
{
	return sqlVdbeParameterIndex((Vdbe *) pStmt, zName,
					 sqlStrlen30(zName));
}

int
sql_bind_parameter_lindex(sql_stmt * pStmt, const char *zName,
			      int nName)
{
	return sqlVdbeParameterIndex((Vdbe *) pStmt, zName, nName);
}

/*
 * Transfer all bindings from the first statement over to the second.
 */
int
sqlTransferBindings(sql_stmt * pFromStmt, sql_stmt * pToStmt)
{
	Vdbe *pFrom = (Vdbe *) pFromStmt;
	Vdbe *pTo = (Vdbe *) pToStmt;
	int i;
	assert(pTo->db == pFrom->db);
	assert(pTo->nVar == pFrom->nVar);
	for (i = 0; i < pFrom->nVar; i++) {
		sqlVdbeMemMove(&pTo->aVar[i], &pFrom->aVar[i]);
	}
	return 0;
}

/*
 * Return the sql* database handle to which the prepared statement given
 * in the argument belongs.  This is the same database handle that was
 * the first argument to the sql_prepare() that was used to create
 * the statement in the first place.
 */
sql *
sql_db_handle(sql_stmt * pStmt)
{
	return pStmt ? ((Vdbe *) pStmt)->db : 0;
}

int
sql_stmt_busy(const struct sql_stmt *stmt)
{
	assert(stmt != NULL);
	const struct Vdbe *v = (const struct Vdbe *) stmt;
	return v->magic == VDBE_MAGIC_RUN && v->pc >= 0;
}

/*
 * Return a pointer to the next prepared statement after pStmt associated
 * with database connection pDb.  If pStmt is NULL, return the first
 * prepared statement for the database connection.  Return NULL if there
 * are no more.
 */
sql_stmt *
sql_next_stmt(sql * pDb, sql_stmt * pStmt)
{
	sql_stmt *pNext;
	if (pStmt == 0) {
		pNext = (sql_stmt *) pDb->pVdbe;
	} else {
		pNext = (sql_stmt *) ((Vdbe *) pStmt)->pNext;
	}
	return pNext;
}

/*
 * Return the value of a status counter for a prepared statement
 */
int
sql_stmt_status(sql_stmt * pStmt, int op, int resetFlag)
{
	Vdbe *pVdbe = (Vdbe *) pStmt;
	u32 v;
	v = pVdbe->aCounter[op];
	if (resetFlag)
		pVdbe->aCounter[op] = 0;
	return (int)v;
}

/*
 * Return the SQL associated with a prepared statement
 */
const char *
sql_sql(sql_stmt * pStmt)
{
	Vdbe *p = (Vdbe *) pStmt;
	return p ? p->zSql : 0;
}

/*
 * Return the SQL associated with a prepared statement with
 * bound parameters expanded.  Space to hold the returned string is
 * obtained from sql_malloc().  The caller is responsible for
 * freeing the returned string by passing it to sql_free().
 */
char *
sql_expanded_sql(sql_stmt * pStmt)
{
	char *z = 0;
	const char *zSql = sql_sql(pStmt);
	if (zSql) {
		Vdbe *p = (Vdbe *) pStmt;
		z = sqlVdbeExpandSql(p, zSql);
	}
	return z;
}

