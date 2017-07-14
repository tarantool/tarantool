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
#include "port.h"
#include "memtx_tuple.h"

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
 * @todo: merge with sqlite3_value.
 */
struct sql_bind {
	/** Bind name. NULL for ordinal binds. */
	const char *name;
	/** Length of the @name. */
	uint32_t name_len;
	/** Ordinal position of the bind, for ordinal binds. */
	uint32_t pos;

	/** Byte length of the value. */
	uint32_t bytes;
	/** SQL type of the value. */
	uint8_t type;
	/** Bind value. */
	union {
		double d;
		int64_t i64;
		/** For string or blob. */
		const char *s;
	};
};

/**
 * Return a string name of a parameter marker.
 * @param Bind to get name.
 * @retval Zero terminated name.
 */
static inline const char *
sql_bind_name(const struct sql_bind *bind)
{
	if (bind->name)
		return tt_sprintf("'%.*s'", bind->name_len, bind->name);
	else
		return tt_sprintf("%d", (int) bind->pos);
}

/**
 * Decode a single bind column from the binary protocol packet.
 * @param[out] bind Bind to decode to.
 * @param i Ordinal bind number.
 * @param packet MessagePack encoded parameter value. Either
 *        scalar or map: {string_name: scalar_value}.
 *
 * @retval  0 Success.
 * @retval -1 Memory or client error.
 */
static inline int
sql_bind_decode(struct sql_bind *bind, int i, const char **packet)
{
	bind->pos = i + 1;
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
	} else {
		bind->name = NULL;
		bind->name_len = 0;
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
		bind->bytes = sizeof(bind->i64);
		break;
	}
	case MP_INT:
		bind->i64 = mp_decode_int(packet);
		bind->type = SQLITE_INTEGER;
		bind->bytes = sizeof(bind->i64);
		break;
	case MP_STR:
		bind->s = mp_decode_str(packet, &bind->bytes);
		bind->type = SQLITE_TEXT;
		break;
	case MP_DOUBLE:
		bind->d = mp_decode_double(packet);
		bind->type = SQLITE_FLOAT;
		bind->bytes = sizeof(bind->d);
		break;
	case MP_FLOAT:
		bind->d = mp_decode_float(packet);
		bind->type = SQLITE_FLOAT;
		bind->bytes = sizeof(bind->d);
		break;
	case MP_NIL:
		mp_decode_nil(packet);
		bind->type = SQLITE_NULL;
		bind->bytes = 1;
		break;
	case MP_BOOL:
		/* SQLite doesn't support boolean. Use int instead. */
		bind->i64 = mp_decode_bool(packet) ? 1 : 0;
		bind->type = SQLITE_INTEGER;
		bind->bytes = sizeof(bind->i64);
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
 * Parse MessagePack array of SQL parameters and store a result
 * into the @request->bind, bind_count.
 * @param request Request to save decoded parameters.
 * @param data MessagePack array of parameters. Each parameter
 *        either must have scalar type, or must be a map with the
 *        following format: {name: value}. Name - string name of
 *        the named parameter, value - scalar value of the
 *        parameter. Named and positioned parameters can be mixed.
 *        For more details
 *        @sa https://www.sqlite.org/lang_expr.html#varparam.
 * @param region Allocator.
 *
 * @retval  0 Success.
 * @retval -1 Client or memory error.
 */
static inline int
sql_bind_list_decode(struct sql_request *request, const char *data,
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
	if (bind_count > SQL_BIND_PARAMETER_MAX) {
		diag_set(ClientError, ER_SQL_BIND_PARAMETER_MAX,
			 (int) bind_count);
		return -1;
	}
	uint32_t used = region_used(region);
	size_t size = sizeof(struct sql_bind) * bind_count;
	struct sql_bind *bind = (struct sql_bind *) region_alloc(region, size);
	if (bind == NULL) {
		diag_set(OutOfMemory, size, "region_alloc", "struct sql_bind");
		return -1;
	}
	for (uint32_t i = 0; i < bind_count; ++i) {
		if (sql_bind_decode(&bind[i], i, &data) != 0) {
			region_truncate(region, used);
			return -1;
		}
	}
	request->bind_count = bind_count;
	request->bind = bind;
	return 0;
}

int
sql_request_decode(struct sql_request *request, const char *data, uint32_t len,
		   struct region *region, uint64_t sync)
{
	const char *end = data + len;
	if (mp_typeof(*data) != MP_MAP || mp_check_map(data, end) > 0) {
error:
		diag_set(ClientError, ER_INVALID_MSGPACK, "packet body");
		return -1;
	}
	uint32_t map_size = mp_decode_map(&data);
	request->sql_text = NULL;
	request->bind = NULL;
	request->bind_count = 0;
	request->sync = sync;
	for (uint32_t i = 0; i < map_size; ++i) {
		uint8_t key = *data;
		if (key != IPROTO_SQL_BIND && key != IPROTO_SQL_TEXT) {
			mp_check(&data, end);   /* skip the key */
			mp_check(&data, end);   /* skip the value */
			continue;
		}
		const char *value = ++data;     /* skip the key */
		if (mp_check(&data, end) != 0)  /* check the value */
			goto error;
		if (key == IPROTO_SQL_BIND) {
			if (sql_bind_list_decode(request, value, region) != 0)
				return -1;
		} else {
			request->sql_text = value;
		}
	}
	if (request->sql_text == NULL) {
		diag_set(ClientError, ER_MISSING_REQUEST_FIELD,
			 iproto_key_name(IPROTO_SQL_TEXT));
		return -1;
	}
	if (data != end)
		goto error;
	return 0;
}

/**
 * Serialize a single column of a result set row.
 * @param stmt Prepared and started statement. At least one
 *        sqlite3_step must be called.
 * @param i Column number.
 * @param out Output buffer.
 *
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
 * Convert sqlite3 row into a tuple and append to a port.
 * @param stmt Started prepared statement. At least one
 *        sqlite3_step must be done.
 * @param column_count Statement's column count.
 * @param buf Temporary buffer for tuples construction.
 * @param port Port to store tuples.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
static inline int
sql_row_to_port(struct sqlite3_stmt *stmt, int column_count, struct obuf *buf,
		struct port *port)
{
	assert(column_count > 0);
	size_t size = mp_sizeof_array(column_count);
	/* Use obuf as a temporary storage for a raw tuple. */
	struct obuf_svp svp = obuf_create_svp(buf);
	char *pos = (char *) obuf_alloc(buf, size);
	if (pos == NULL) {
		diag_set(OutOfMemory, size, "obuf_alloc", "SQL row");
		return -1;
	}
	mp_encode_array(pos, column_count);

	for (int i = 0; i < column_count; ++i) {
		if (sql_column_to_obuf(stmt, i, buf) != 0) {
			obuf_rollback_to_svp(buf, &svp);
			return -1;
		}
	}
	size = obuf_size(buf) - svp.used;
	struct tuple *tuple =
		memtx_tuple_new(tuple_format_default, pos, pos + size);
	obuf_rollback_to_svp(buf, &svp);
	if (tuple == NULL)
		return -1;
	return port_add_tuple(port, tuple);
}

/**
 * Bind SQL parameter value to its position.
 * @param stmt Prepared statement.
 * @param p Parameter value.
 * @param pos Ordinal bind position.
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
				 sql_bind_name(p));
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

	switch (rc) {
	case SQLITE_NOMEM:
		diag_set(OutOfMemory, p->bytes, "vdbe", "bind value");
		break;
	case SQLITE_TOOBIG:
	default:
		diag_set(ClientError, ER_SQL_BIND_VALUE, sql_bind_name(p),
			 sql_type_strs[p->type]);
		break;
	}
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
sql_bind(const struct sql_request *request, struct sqlite3_stmt *stmt)
{
	assert(stmt != NULL);
	uint32_t pos = 1;
	for (uint32_t i = 0; i < request->bind_count; pos = ++i + 1) {
		if (sql_bind_column(stmt, &request->bind[i], pos) != 0)
			return -1;
	}
	return 0;
}

/**
 * Serialize a description of the prepared statement.
 * @param stmt Prepared statement.
 * @param out Out buffer.
 * @param column_count Statement's column count.
 *
 * @retval  0 Success.
 * @retval -1 Client or memory error.
 */
static inline int
sql_get_description(struct sqlite3_stmt *stmt, struct obuf *out,
		    int column_count)
{
	assert(column_count > 0);
	if (iproto_reply_array_key(out, column_count, IPROTO_METADATA) != 0)
		return -1;

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
	return 0;
}

/**
 * Execute the prepared statement and write to the @out obuf the
 * result. Result is either rows array in a case of not zero
 * column count (SELECT), or SQL info in other cases.
 * @param db SQLite engine.
 * @param stmt Prepared statement.
 * @param out Out buffer.
 * @param sync IProto request sync.
 *
 * @retval  0 Success.
 * @retval -1 Client or memory error.
 */
static inline int
sql_execute_and_encode(sqlite3 *db, struct sqlite3_stmt *stmt, struct obuf *out,
		       uint64_t sync)
{
	/*
	 * Execute request.
	 */
	int rc;
	struct port port;
	port_create(&port);
	int column_count = sqlite3_column_count(stmt);
	if (column_count > 0) {
		/* Either ROW or DONE or ERROR. */
		while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
			if (sql_row_to_port(stmt, column_count, out,
					    &port) != 0)
				goto err_execute;
		}
		assert(rc == SQLITE_DONE || rc != SQLITE_OK);
	} else {
		/* No rows. Either DONE or ERROR. */
		rc = sqlite3_step(stmt);
		assert(rc != SQLITE_ROW && rc != SQLITE_OK);
	}
	if (rc != SQLITE_DONE) {
		diag_set(ClientError, ER_SQL_EXECUTE, sqlite3_errmsg(db));
		goto err_execute;
	}

	/*
	 * Encode response.
	 */
	struct obuf_svp header_svp;
	/* Prepare memory for the iproto header. */
	if (iproto_prepare_header(out, &header_svp, IPROTO_SQL_HEADER_LEN) != 0)
		goto err_execute;
	int keys;
	if (column_count > 0) {
		if (sql_get_description(stmt, out, column_count) != 0)
			goto err_body;
		keys = 2;
		if (iproto_reply_array_key(out, port.size, IPROTO_DATA) != 0)
			goto err_body;
		if (port_dump(&port, out) != 0) {
			/* Failed port dump destroyes the port. */
			port_create(&port);
			goto err_body;
		}
	} else {
		keys = 1;
		assert(port.size == 0);
		if (iproto_reply_map_key(out, 1, IPROTO_SQL_INFO) != 0)
			goto err_body;
		int changes = sqlite3_changes(db);
		int size = mp_sizeof_uint(IPROTO_SQL_ROW_COUNT) +
			   mp_sizeof_uint(changes);
		char *buf = obuf_alloc(out, size);
		if (buf == NULL) {
			diag_set(OutOfMemory, size, "obuf_alloc", "buf");
			goto err_body;
		}
		buf = mp_encode_uint(buf, IPROTO_SQL_ROW_COUNT);
		buf = mp_encode_uint(buf, changes);
	}
	/* Port already is destroyed during dump. */
	iproto_reply_sql(out, &header_svp, sync, schema_version, keys);
	return 0;

err_body:
	obuf_rollback_to_svp(out, &header_svp);
err_execute:
	port_destroy(&port);
	return -1;
}

int
sql_prepare_and_execute(const struct sql_request *request, struct obuf *out)
{
	const char *sql = request->sql_text;
	uint32_t len;
	sql = mp_decode_str(&sql, &len);
	struct sqlite3_stmt *stmt;
	sqlite3 *db = sql_get();
	if (db == NULL) {
		diag_set(ClientError, ER_LOADING);
		return -1;
	}
	if (sqlite3_prepare_v2(db, sql, len, &stmt, NULL) != SQLITE_OK) {
		diag_set(ClientError, ER_SQL_EXECUTE, sqlite3_errmsg(db));
		return -1;
	}
	assert(stmt != NULL);
	if (sql_bind(request, stmt) != 0)
		goto err_stmt;
	if (sql_execute_and_encode(db, stmt, out, request->sync) != 0)
		goto err_stmt;
	sqlite3_finalize(stmt);
	return 0;
err_stmt:
	sqlite3_finalize(stmt);
	return -1;
}
