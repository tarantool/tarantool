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
#include "mem.h"
#include "vdbeInt.h"
#include "box/session.h"

/*
 * Invoke the profile callback.  This routine is only called if we already
 * know that the profile callback is defined and needs to be invoked.
 */
static SQL_NOINLINE void
invokeProfileCallback(struct Vdbe *p)
{
	struct sql *db = sql_get();
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

int
sql_stmt_finalize(struct Vdbe *v)
{
	if (v == NULL)
		return 0;
	if (v->startTime > 0)
		invokeProfileCallback(v);
	return sqlVdbeFinalize(v);
}

int
sql_stmt_reset(struct Vdbe *v)
{
	assert(v != NULL);
	if (v->startTime > 0)
		invokeProfileCallback(v);
	int rc = sqlVdbeReset(v);
	sqlVdbeRewind(v);
	return rc;
}

bool
sql_metadata_is_full()
{
	return current_session()->sql_flags & SQL_FullMetadata;
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
	struct sql *db = sql_get();
	int rc;

	assert(p);
	if (p->magic != VDBE_MAGIC_RUN)
		sql_stmt_reset(p);

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
	if (rc != SQL_ROW && p->startTime > 0)
		invokeProfileCallback(p);

	if (rc != SQL_ROW && rc != SQL_DONE) {
		/* If this statement was prepared using sql_prepare(), and an
		 * error has occurred, then return an error.
		 */
		if (p->is_aborted)
			rc = -1;
	}
	return rc;
}

int
sql_step(struct Vdbe *v)
{
	assert(v != NULL);
	return sqlStep(v);
}

int
sql_column_count(struct Vdbe *pVm)
{
	return pVm ? pVm->nResColumn : 0;
}

char *
sql_stmt_result_to_msgpack(struct Vdbe *vdbe, uint32_t *tuple_size,
			   struct region *region)
{
	return mem_encode_array(vdbe->pResultSet, vdbe->nResColumn, tuple_size,
				region);
}

char *
sql_stmt_func_result_to_msgpack(struct Vdbe *vdbe, uint32_t *size,
				struct region *region)
{
	assert(vdbe->nResColumn == 1);
	return mem_to_mp(vdbe->pResultSet, size, region);
}

const char *
sql_column_name(struct Vdbe *p, int n)
{
	assert(n < sql_column_count(p) && n >= 0);
	return p->metadata[n].name;
}

const char *
sql_column_datatype(struct Vdbe *p, int n)
{
	assert(n < sql_column_count(p) && n >= 0);
	return p->metadata[n].type;
}

const char *
sql_column_coll(struct Vdbe *p, int n)
{
	assert(n < sql_column_count(p) && n >= 0);
	return p->metadata[n].collation;
}

int
sql_column_nullable(struct Vdbe *p, int n)
{
	assert(n < sql_column_count(p) && n >= 0);
	return p->metadata[n].nullable;
}

bool
sql_column_is_autoincrement(struct Vdbe *p, int n)
{
	assert(n < sql_column_count(p) && n >= 0);
	return p->metadata[n].is_actoincrement;
}

const char *
sql_column_span(struct Vdbe *p, int n) {
	assert(n < sql_column_count(p) && n >= 0);
	return p->metadata[n].span;
}

uint64_t
sql_stmt_schema_version(const struct Vdbe *v)
{
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
sql_stmt_est_size(const struct Vdbe *v)
{
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
		case P4_DEC:
			size += sizeof(*v->aOp[i].p4.dec);
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
sql_stmt_query_str(const struct Vdbe *v)
{
	return v->zSql;
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
vdbeUnbind(struct Vdbe *p, int i)
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
	mem_destroy(pVar);
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
sql_unbind(struct Vdbe *v)
{
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

void
sql_reset_autoinc_id_list(struct Vdbe *v)
{
	stailq_create(&v->autoinc_id_list);
}

int
sql_bind_double(struct Vdbe *p, int i, double rValue)
{
	if (vdbeUnbind(p, i) != 0)
		return -1;
	int rc = sql_bind_type(p, i, "numeric");
	mem_set_double(&p->aVar[i - 1], rValue);
	return rc;
}

int
sql_bind_boolean(struct Vdbe *p, int i, bool value)
{
	if (vdbeUnbind(p, i) != 0)
		return -1;
	int rc = sql_bind_type(p, i, "boolean");
	mem_set_bool(&p->aVar[i - 1], value);
	return rc;
}

int
sql_bind_int(struct Vdbe *p, int i, int iValue)
{
	return sql_bind_int64(p, i, (i64) iValue);
}

int
sql_bind_int64(struct Vdbe *p, int i, int64_t iValue)
{
	if (vdbeUnbind(p, i) != 0)
		return -1;
	int rc = sql_bind_type(p, i, "integer");
	assert(iValue < 0);
	mem_set_int(&p->aVar[i - 1], iValue, true);
	return rc;
}

int
sql_bind_uint64(struct Vdbe *p, int i, uint64_t value)
{
	if (vdbeUnbind(p, i) != 0)
		return -1;
	int rc = sql_bind_type(p, i, "integer");
	mem_set_uint(&p->aVar[i - 1], value);
	return rc;
}

int
sql_bind_null(struct Vdbe *p, int i)
{
	if (vdbeUnbind(p, i) != 0)
		return -1;
	return sql_bind_type(p, i, "boolean");
}

int
sql_bind_ptr(struct Vdbe *p, int i, void *ptr)
{
	int rc = vdbeUnbind(p, i);
	if (rc == 0) {
		rc = sql_bind_type(p, i, "varbinary");
		mem_set_ptr(&p->aVar[i - 1], ptr);
	}
	return rc;
}

int
sql_bind_str_static(struct Vdbe *vdbe, int i, const char *str, uint32_t len)
{
	mem_set_str_static(&vdbe->aVar[i - 1], (char *)str, len);
	return sql_bind_type(vdbe, i, "text");
}

int
sql_bind_bin_static(struct Vdbe *vdbe, int i, const char *str, uint32_t size)
{
	mem_set_bin_static(&vdbe->aVar[i - 1], (char *)str, size);
	return sql_bind_type(vdbe, i, "text");
}

int
sql_bind_array_static(struct Vdbe *vdbe, int i, const char *str, uint32_t size)
{
	mem_set_array_static(&vdbe->aVar[i - 1], (char *)str, size);
	return sql_bind_type(vdbe, i, "array");
}

int
sql_bind_map_static(struct Vdbe *vdbe, int i, const char *str, uint32_t size)
{
	mem_set_map_static(&vdbe->aVar[i - 1], (char *)str, size);
	return sql_bind_type(vdbe, i, "map");
}

int
sql_bind_uuid(struct Vdbe *p, int i, const struct tt_uuid *uuid)
{
	if (vdbeUnbind(p, i) != 0 || sql_bind_type(p, i, "uuid") != 0)
		return -1;
	mem_set_uuid(&p->aVar[i - 1], uuid);
	return 0;
}

int
sql_bind_dec(struct Vdbe *p, int i, const decimal_t *dec)
{
	if (vdbeUnbind(p, i) != 0 || sql_bind_type(p, i, "decimal") != 0)
		return -1;
	mem_set_dec(&p->aVar[i - 1], dec);
	return 0;
}

int
sql_bind_datetime(struct Vdbe *p, int i, const struct datetime *dt)
{
	if (vdbeUnbind(p, i) != 0 || sql_bind_type(p, i, "datetime") != 0)
		return -1;
	mem_set_datetime(&p->aVar[i - 1], dt);
	return 0;
}

int
sql_bind_interval(struct Vdbe *p, int i, const struct interval *itv)
{
	if (vdbeUnbind(p, i) != 0 || sql_bind_type(p, i, "interval") != 0)
		return -1;
	mem_set_interval(&p->aVar[i - 1], itv);
	return 0;
}

int
sql_bind_parameter_count(const struct Vdbe *p)
{
	return p->nVar;
}

const char *
sql_bind_parameter_name(const struct Vdbe *p, int i)
{
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
sql_bind_parameter_lindex(struct Vdbe *v, const char *zName, int nName)
{
	return sqlVdbeParameterIndex(v, zName, nName);
}

int
sqlTransferBindings(struct Vdbe *pFrom, struct Vdbe *pTo)
{
	int i;
	assert(pTo->nVar == pFrom->nVar);
	for (i = 0; i < pFrom->nVar; i++) {
		mem_move(&pTo->aVar[i], &pFrom->aVar[i]);
	}
	return 0;
}

int
sql_stmt_busy(const struct Vdbe *v)
{
	assert(v != NULL);
	return v->magic == VDBE_MAGIC_RUN && v->pc >= 0;
}

const char *
sql_sql(struct Vdbe *p)
{
	return p ? p->zSql : 0;
}
