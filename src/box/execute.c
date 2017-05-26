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
#include "execute.h"

#include "iproto_constants.h"
#include "sql/sqlite3.h"
#include "sql/sqliteLimit.h"
#include "errcode.h"
#include "small/region.h"
#include "small/obuf.h"
#include "diag.h"
#include "sql.h"
#include "xrow.h"
#include "schema.h"

const char *sql_type_strs[] = {
	NULL,
	"INTEGER",
	"FLOAT",
	"TEXT",
	"BLOB",
	"NULL",
};

/**
 * Name and value of an SQL prepared statement parameter.
 * @todo: merge with sqlite3_value
 */
struct sql_bind {
	/** Parameter name. NULL for positioned parameters. */
	const char *name;
	/** Length of the @name. */
	uint32_t name_len;
	/** Ordinal position of the bind, for ordinal binds. */
	uint32_t pos;

	/** Length of the @s, if the @type is MP_STR or MP_BIN. */
	uint32_t bytes;
	/** SQL type of the value. */
	uint8_t type;
	/** Parameter value. */
	union {
		double d;
		int64_t i64;
		/** For string or blob. */
		const char *s;
	};
};

/**
 * Return a string name of a parameter marker
 */
static inline const char *
sql_bind_name(const struct sql_bind *bind)
{
	char *buf = tt_static_buf();
	if (bind->name) {
		snprintf(buf, TT_STATIC_BUF_LEN, "'%.*s'", bind->name_len,
			bind->name);
	} else {
		snprintf(buf, TT_STATIC_BUF_LEN, "%d", (int) bind->pos);
	}
	return buf;
}

/**
 * Decode a single bind column from the binary protocol packet.
 */
static inline int
sql_bind_decode(struct sql_bind *bind, int i, const char **packet)
{
	bind->pos = i + 1;
	bind->name = NULL;
	bind->name_len = 0;
	if (mp_typeof(**packet) == MP_MAP) {
		uint32_t len = mp_decode_map(packet);
		/*
		 * A named parameter is an MP_MAP with
		 * one key - {'name': value}.
		 * Report parse error otherwise.
		 */
		if (len != 1 || mp_typeof(**packet) != MP_STR) {
			diag_set(ClientError, ER_INVALID_MSGPACK,
				 "SQL bind parameter");
			return -1;
		}
		bind->name = mp_decode_str(packet, &bind->name_len);
	}
	switch (mp_typeof(**packet)) {
	case MP_UINT: {
		uint64_t n = mp_decode_uint(packet);
		if (n > INT64_MAX) {
			diag_set(ClientError, ER_SQL_BIND_VALUE,
				 sql_bind_name(bind), "INTEGER");
			return -1;
		}
		bind->i64 = (int64_t) n;
		bind->type = SQLITE_INTEGER;
		break;
	}
	case MP_INT:
		bind->i64 = mp_decode_int(packet);
		bind->type = SQLITE_INTEGER;
		break;
	case MP_STR:
		bind->s = mp_decode_str(packet, &bind->bytes);
		bind->type = SQLITE_TEXT;
		break;
	case MP_DOUBLE:
		bind->d = mp_decode_double(packet);
		bind->type = SQLITE_FLOAT;
		break;
	case MP_FLOAT:
		bind->d = mp_decode_float(packet);
		bind->type = SQLITE_FLOAT;
		break;
	case MP_NIL:
		mp_decode_nil(packet);
		bind->type = SQLITE_NULL;
		break;
	case MP_BOOL:
		/* SQLite doesn't support boolean. Use int instead. */
		bind->i64 = mp_decode_bool(packet) ? 1 : 0;
		bind->type = SQLITE_INTEGER;
		break;
	case MP_BIN:
		bind->s = mp_decode_bin(packet, &bind->bytes);
		bind->type = SQLITE_BLOB;
		break;
	case MP_EXT:
		bind->s = *packet;
		mp_next(packet);
		bind->bytes = *packet - bind->s;
		bind->type = SQLITE_BLOB;
		break;
	case MP_ARRAY:
		diag_set(ClientError, ER_SQL_BIND_TYPE, "ARRAY",
			 sql_bind_name(bind));
		return -1;
	case MP_MAP:
		diag_set(ClientError, ER_SQL_BIND_TYPE, "MAP",
			 sql_bind_name(bind));
		return -1;
	default:
		unreachable();
	}
	return 0;
}

/**
 * Decode SQL parameter values and names. Named and positioned
 * parameters are supported.
 * @param data MessagePack array of parameters without a
 *        header. Each parameter either must have scalar type, or
 *        must be a map with the following format: {name: value}.
 *        Name - string name of the named parameter,
 *        value - scalar value of the parameter. Named and
 *        positioned parameters can be mixed. For more details
 *        @sa https://www.sqlite.org/lang_expr.html#varparam.
 * @param bind_count Length of @parameters.
 * @param region Allocator.
 *
 * @retval not NULL Array of parameters with @parameter_count
 *         length.
 * @retval     NULL Client or memory error.
 */
static inline struct sql_bind *
sql_bind_list_decode(const char *data, uint32_t bind_count,
		     struct region *region)
{
	assert(bind_count > 0);
	assert(data != NULL);
	if (bind_count > SQL_VARIABLE_NUMBER_MAX) {
		diag_set(ClientError, ER_SQL_BIND_COUNT_MAX);
		return NULL;
	}
	uint32_t used = region_used(region);
	size_t size = sizeof(struct sql_bind) * bind_count;
	struct sql_bind *bind = (struct sql_bind *) region_alloc(region, size);
	if (bind == NULL) {
		diag_set(OutOfMemory, size, "region_alloc", "struct sql_bind");
		return NULL;
	}
	for (uint32_t i = 0; i < bind_count; ++i) {
		if (sql_bind_decode(&bind[i], i, &data)) {
			region_truncate(region, used);
			return NULL;
		}
	}
	return bind;
}

/**
 * Parse MessagePack array of SQL parameters and remember a result
 * into the @request->tuple, tuple_end.
 * @param request Request to save decoded parameters.
 * @param data MessagePack array of parameters.
 * @param region Allocator.
 *
 * @retval  0 Success.
 * @retval -1 Client or memory error.
 */
static inline int
sql_bind_parse(struct sql_request *request, const char *data,
	       struct region *region)
{
	assert(request != NULL);
	assert(data != NULL);
	if (mp_typeof(*data) != MP_ARRAY) {
		diag_set(ClientError, ER_INVALID_MSGPACK, "SQL parameter list");
		return -1;
	}
	uint32_t bind_count = mp_decode_array(&data);
	if (bind_count == 0)
		return 0;
	request->bind = sql_bind_list_decode(data, bind_count, region);
	if (request->bind == NULL)
		return -1;
	request->bind_count = bind_count;
	return 0;
}

int
sql_request_decode(struct sql_request *request, const char *data, uint32_t len,
		   struct region *region, uint64_t sync)
{
	const char *end = data + len;
	if (mp_typeof(*data) != MP_MAP || mp_check_map(data, end) > 0) {
mp_error:
		diag_set(ClientError, ER_INVALID_MSGPACK, "packet body");
		return -1;
	}
	uint32_t size = mp_decode_map(&data);
	request->query = NULL;
	request->query_end = NULL;
	request->bind = NULL;
	request->bind_count = 0;
	request->sync = sync;
	for (uint32_t i = 0; i < size; ++i) {
		uint64_t key = mp_decode_uint(&data);
		const char *value = data;
		if (mp_check(&data, end) != 0)
			goto mp_error;
		switch (key) {
			case IPROTO_SQL_BIND:
				if (sql_bind_parse(request, value, region) != 0)
					return -1;
				break;
			case IPROTO_SQL_TEXT:
				request->query = value;
				request->query_end = data;
				break;
			default:
				break;
		}
	}
#ifndef NDEBUG
	if (data != end) {
		diag_set(ClientError, ER_INVALID_MSGPACK, "packet end");
		return -1;
	}
#endif
	if (request->query == NULL) {
		diag_set(ClientError, ER_MISSING_REQUEST_FIELD,
			 iproto_key_name(IPROTO_SQL_TEXT));
		return -1;
	}
	return 0;
}

/**
 * Serialize a single column of a result set row.
 * @retval  0 Success.
 * @retval -1 Out of memory when resizing the output buffer.
 */
static inline int
sql_column_to_obuf(struct sqlite3_stmt *stmt, int i, struct obuf *out)
{
	size_t size;
	int type = sqlite3_column_type(stmt, i);
	switch (type) {
	case SQLITE_INTEGER: {
		int64_t n = sqlite3_column_int64(stmt, i);
		if (n >= 0)
			size = mp_sizeof_uint(n);
		else
			size = mp_sizeof_int(n);
		char *pos = (char *) obuf_alloc(out, size);
		if (pos == NULL)
			goto oom;
		if (n >= 0)
			mp_encode_uint(pos, n);
		else
			mp_encode_int(pos, n);
		break;
	}
	case SQLITE_FLOAT: {
		double d = sqlite3_column_double(stmt, i);
		size = mp_sizeof_double(d);
		char *pos = (char *) obuf_alloc(out, size);
		if (pos == NULL)
			goto oom;
		mp_encode_double(pos, d);
		break;
	}
	case SQLITE_TEXT: {
		uint32_t len = sqlite3_column_bytes(stmt, i);
		size = mp_sizeof_str(len);
		char *pos = (char *) obuf_alloc(out, size);
		if (pos == NULL)
			goto oom;
		const char *s;
		s = (const char *)sqlite3_column_text(stmt, i);
		mp_encode_str(pos, s, len);
		break;
	}
	case SQLITE_BLOB: {
		uint32_t len = sqlite3_column_bytes(stmt, i);
		size = mp_sizeof_bin(len);
		char *pos = (char *) obuf_alloc(out, size);
		if (pos == NULL)
			goto oom;
		const char *s;
		s = (const char *)sqlite3_column_blob(stmt, i);
		mp_encode_bin(pos, s, len);
		break;
	}
	case SQLITE_NULL: {
		size = mp_sizeof_nil();
		char *pos = (char *) obuf_alloc(out, size);
		if (pos == NULL)
			goto oom;
		mp_encode_nil(pos);
		break;
	}
	default:
		unreachable();
	}
	return 0;
oom:
	diag_set(OutOfMemory, size, "obuf_alloc", "SQL value");
	return -1;
}

/**
 * Encode sqlite3 row into an obuf using MessagePack.
 * @param stmt Started prepared statement. At least one
 *        sqlite3_step must be done.
 * @param out Out buffer.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
static inline int
sql_row_to_obuf(struct sqlite3_stmt *stmt, struct obuf *out)
{
	int column_count = sqlite3_column_count(stmt);
	assert(column_count > 0);
	size_t size = mp_sizeof_array(column_count);
	char *pos = (char *) obuf_alloc(out, size);
	if (pos == NULL) {
		diag_set(OutOfMemory, size, "obuf_alloc", "SQL row");
		return -1;
	}
	mp_encode_array(pos, column_count);

	for (int i = 0; i < column_count; ++i) {
		if (sql_column_to_obuf(stmt, i, out))
			return -1;
	}
	return 0;
}

/**
 * Bind SQL parameter value to its position.
 * @param stmt Prepared statement.
 * @param p Parameter value.
 * @param pos Position, if the parameter is positioned.
 *
 * @retval  0 Success.
 * @retval -1 SQL error.
 */
static inline int
sql_bind_column(struct sqlite3_stmt *stmt, const struct sql_bind *p,
		uint32_t pos)
{
	int rc;
	if (p->name != NULL) {
		pos = sqlite3_bind_parameter_lindex(stmt, p->name, p->name_len);
		if (pos == 0) {
			diag_set(ClientError, ER_SQL_BIND_NOT_FOUND,
				 tt_cstr(p->name, p->name_len));
			return -1;
		}
	}
	switch (p->type) {
	case SQLITE_INTEGER:
		rc = sqlite3_bind_int64(stmt, pos, p->i64);
		break;
	case SQLITE_FLOAT:
		rc = sqlite3_bind_double(stmt, pos, p->d);
		break;
	case SQLITE_TEXT:
		/*
		 * Parameters are allocated within message pack,
		 * received from the iproto thread. IProto thread
		 * now is waiting for the response and it will not
		 * free the packet until sqlite3_finalize. So
		 * there is no need to copy the packet and we can
		 * use SQLITE_STATIC.
		 */
		rc = sqlite3_bind_text64(stmt, pos, p->s, p->bytes,
					 SQLITE_STATIC, SQLITE_UTF8);
		break;
	case SQLITE_NULL:
		rc = sqlite3_bind_null(stmt, pos);
		break;
	case SQLITE_BLOB:
		rc = sqlite3_bind_blob64(stmt, pos, (const void *) p->s,
					 p->bytes, SQLITE_STATIC);
		break;
	default:
		unreachable();
	}

	if (rc == SQLITE_OK)
		return 0;
	if (rc == SQLITE_TOOBIG)
		diag_set(ClientError, ER_SQL_BIND_VALUE, sql_bind_name(p),
			 sql_type_strs[p->type]);
	else if (rc == SQLITE_NOMEM)
		diag_set(ClientError, ER_SQL_BIND_NOMEM, sql_bind_name(p),
			 sql_type_strs[p->type]);
	else
		unreachable();
	return -1;
}

/**
 * Bind parameter values to the prepared statement.
 * @param request Parsed SQL request.
 * @param stmt Prepared statement.
 *
 * @retval  0 Success.
 * @retval -1 Client or memory error.
 */
static inline int
sql_bind(struct sql_request *request, struct sqlite3_stmt *stmt)
{
	assert(stmt != NULL);
	uint32_t pos = 1;
	for (uint32_t i = 0; i < request->bind_count; pos = ++i + 1)
		if (sql_bind_column(stmt, &request->bind[i], pos) != 0)
			return -1;
	return 0;
}

/**
 * Prepare an SQL query.
 * @param db SQLite engine.
 * @param[out] stmt SQL statement to prepare.
 * @param sql SQL query.
 * @param length Length of the @sql.
 *
 * @retval  0 Success.
 * @retval -1 Client or memory error.
 */
static inline int
sql_prepare(sqlite3 *db, struct sqlite3_stmt **stmt, const char *sql,
	    uint32_t length)
{
	if (sqlite3_prepare_v2(db, sql, length, stmt, &sql) != SQLITE_OK) {
		diag_set(ClientError, ER_SQL, sqlite3_errmsg(db));
		sqlite3_finalize(*stmt);
		return -1;
	}
	return 0;
}

/**
 * Get description of the prepared statement.
 * @param stmt Prepared statement.
 * @param out Out buffer.
 * @param[out] count Count of description pairs.
 *
 * @retval  0 Success.
 * @retval -1 Client or memory error.
 */
static inline int
sql_get_description(struct sqlite3_stmt *stmt, struct obuf *out,
		    uint32_t *count)
{
	assert(stmt != NULL);
	assert(count != NULL);
	int column_count = sqlite3_column_count(stmt);
	for (int i = 0; i < column_count; ++i) {
		size_t size = mp_sizeof_map(1) +
			      mp_sizeof_uint(IPROTO_FIELD_NAME);
		const char *name = sqlite3_column_name(stmt, i);
		/*
		 * Can not fail, since all column names are
		 * preallocated during prepare phase and the
		 * column_name simply returns them.
		 */
		assert(name != NULL);
		size += mp_sizeof_str(strlen(name));
		char *pos = (char *) obuf_alloc(out, size);
		if (pos == NULL) {
			diag_set(OutOfMemory, size, "obuf_alloc", "pos");
			return -1;
		}
		pos = mp_encode_map(pos, 1);
		pos = mp_encode_uint(pos, IPROTO_FIELD_NAME);
		pos = mp_encode_str(pos, name, strlen(name));
	}
	*count = (uint32_t) column_count;
	return 0;
}

/**
 * Execute the prepred SQL query.
 * @param db SQLite engine.
 * @param stmt Prepared statement.
 * @param out Out buffer.
 * @param[out] count Count of statements.
 *
 * @retval  0 Success.
 * @retval -1 Client or memory error.
 */
static inline int
sql_execute(sqlite3 *db, struct sqlite3_stmt *stmt, struct obuf *out,
	    uint32_t *count)
{
	if (stmt == NULL) {
		/* Empty request. */
		return 0;
	}
	assert(count != NULL);
	assert(stmt != NULL);
	*count = 0;
	int column_count = sqlite3_column_count(stmt);
	int rc;
	if (column_count == 0) {
		/*
		 * Query without response:
		 * CREATE/DELETE/INSERT ...
		 */
		while ((rc = sqlite3_step(stmt)) == SQLITE_ROW);
		if (rc != SQLITE_OK && rc != SQLITE_DONE)
			goto sql_error;
		return 0;
	}
	assert(column_count > 0);
	while ((rc = sqlite3_step(stmt) == SQLITE_ROW)) {
		if (sql_row_to_obuf(stmt, out) != 0)
			return -1;
		++*count;
	}
	return rc == SQLITE_OK ? 0 : -1;

sql_error:
	diag_set(ClientError, ER_SQL, sqlite3_errmsg(db));
	return -1;
}

int
sql_prepare_and_execute(struct sql_request *request, struct obuf *out)
{
	const char *sql = request->query;
	uint32_t length;
	sql = mp_decode_str(&sql, &length);
	struct sqlite3_stmt *stmt;
	sqlite3 *db = sql_get();
	if (db == NULL) {
		diag_set(ClientError, ER_SQL, "sql processor is not ready");
		return -1;
	}

	if (sql_prepare(db, &stmt, sql, length) != 0)
		return -1;
	if (sql_bind(request, stmt) != 0)
		goto bind_error;

	/* Prepare memory for the iproto header. */
	struct obuf_svp svp;
	if (iproto_prepare_header(out, &svp, PREPARE_SQL_SIZE) != 0)
		goto error;

	/* Encode description. */
	struct obuf_svp sql_svp;
	if (iproto_prepare_body_key(out, &sql_svp) != 0)
		goto error;
	if (sql_get_description(stmt, out, &length) != 0)
		goto error;
	iproto_reply_body_key(out, &sql_svp, length, IPROTO_DESCRIPTION);

	/* Encode data set. */
	if (iproto_prepare_body_key(out, &sql_svp) != 0)
		goto error;
	if (sql_execute(db, stmt, out, &length) != 0)
		goto error;
	sqlite3_finalize(stmt);
	iproto_reply_body_key(out, &sql_svp, length, IPROTO_DATA);

	iproto_reply_sql(out, &svp, request->sync, schema_version);
	return 0;

error:
	obuf_rollback_to_svp(out, &svp);
bind_error:
	sqlite3_finalize(stmt);
	return -1;
}
