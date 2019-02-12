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
#include "sql/sqlInt.h"
#include "sql/sqlLimit.h"
#include "errcode.h"
#include "small/region.h"
#include "small/obuf.h"
#include "diag.h"
#include "sql.h"
#include "xrow.h"
#include "schema.h"
#include "port.h"
#include "tuple.h"
#include "sql/vdbe.h"

const char *sql_type_strs[] = {
	NULL,
	"INTEGER",
	"FLOAT",
	"TEXT",
	"BLOB",
	"NULL",
};

const char *sql_info_key_strs[] = {
	"row count",
};

/**
 * Name and value of an SQL prepared statement parameter.
 * @todo: merge with sql_value.
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
		bind->type = SQL_INTEGER;
		bind->bytes = sizeof(bind->i64);
		break;
	}
	case MP_INT:
		bind->i64 = mp_decode_int(packet);
		bind->type = SQL_INTEGER;
		bind->bytes = sizeof(bind->i64);
		break;
	case MP_STR:
		bind->s = mp_decode_str(packet, &bind->bytes);
		bind->type = SQL_TEXT;
		break;
	case MP_DOUBLE:
		bind->d = mp_decode_double(packet);
		bind->type = SQL_FLOAT;
		bind->bytes = sizeof(bind->d);
		break;
	case MP_FLOAT:
		bind->d = mp_decode_float(packet);
		bind->type = SQL_FLOAT;
		bind->bytes = sizeof(bind->d);
		break;
	case MP_NIL:
		mp_decode_nil(packet);
		bind->type = SQL_NULL;
		bind->bytes = 1;
		break;
	case MP_BOOL:
		/* sql doesn't support boolean. Use int instead. */
		bind->i64 = mp_decode_bool(packet) ? 1 : 0;
		bind->type = SQL_INTEGER;
		bind->bytes = sizeof(bind->i64);
		break;
	case MP_BIN:
		bind->s = mp_decode_bin(packet, &bind->bytes);
		bind->type = SQL_BLOB;
		break;
	case MP_EXT:
		bind->s = *packet;
		mp_next(packet);
		bind->bytes = *packet - bind->s;
		bind->type = SQL_BLOB;
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

int
sql_bind_list_decode(const char *data, struct sql_bind **out_bind)
{
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
	struct region *region = &fiber()->gc;
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
	*out_bind = bind;
	return bind_count;
}

/**
 * Serialize a single column of a result set row.
 * @param stmt Prepared and started statement. At least one
 *        sql_step must be called.
 * @param i Column number.
 * @param region Allocator for column value.
 *
 * @retval  0 Success.
 * @retval -1 Out of memory when resizing the output buffer.
 */
static inline int
sql_column_to_messagepack(struct sql_stmt *stmt, int i,
			  struct region *region)
{
	size_t size;
	int type = sql_column_type(stmt, i);
	switch (type) {
	case SQL_INTEGER: {
		int64_t n = sql_column_int64(stmt, i);
		if (n >= 0)
			size = mp_sizeof_uint(n);
		else
			size = mp_sizeof_int(n);
		char *pos = (char *) region_alloc(region, size);
		if (pos == NULL)
			goto oom;
		if (n >= 0)
			mp_encode_uint(pos, n);
		else
			mp_encode_int(pos, n);
		break;
	}
	case SQL_FLOAT: {
		double d = sql_column_double(stmt, i);
		size = mp_sizeof_double(d);
		char *pos = (char *) region_alloc(region, size);
		if (pos == NULL)
			goto oom;
		mp_encode_double(pos, d);
		break;
	}
	case SQL_TEXT: {
		uint32_t len = sql_column_bytes(stmt, i);
		size = mp_sizeof_str(len);
		char *pos = (char *) region_alloc(region, size);
		if (pos == NULL)
			goto oom;
		const char *s;
		s = (const char *)sql_column_text(stmt, i);
		mp_encode_str(pos, s, len);
		break;
	}
	case SQL_BLOB: {
		uint32_t len = sql_column_bytes(stmt, i);
		const char *s =
			(const char *)sql_column_blob(stmt, i);
		if (sql_column_subtype(stmt, i) == SQL_SUBTYPE_MSGPACK) {
			size = len;
			char *pos = (char *)region_alloc(region, size);
			if (pos == NULL)
				goto oom;
			memcpy(pos, s, len);
		} else {
			size = mp_sizeof_bin(len);
			char *pos = (char *)region_alloc(region, size);
			if (pos == NULL)
				goto oom;
			mp_encode_bin(pos, s, len);
		}
		break;
	}
	case SQL_NULL: {
		size = mp_sizeof_nil();
		char *pos = (char *) region_alloc(region, size);
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
	diag_set(OutOfMemory, size, "region_alloc", "SQL value");
	return -1;
}

/**
 * Convert sql row into a tuple and append to a port.
 * @param stmt Started prepared statement. At least one
 *        sql_step must be done.
 * @param column_count Statement's column count.
 * @param region Runtime allocator for temporary objects.
 * @param port Port to store tuples.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
static inline int
sql_row_to_port(struct sql_stmt *stmt, int column_count,
		struct region *region, struct port *port)
{
	assert(column_count > 0);
	size_t size = mp_sizeof_array(column_count);
	size_t svp = region_used(region);
	char *pos = (char *) region_alloc(region, size);
	if (pos == NULL) {
		diag_set(OutOfMemory, size, "region_alloc", "SQL row");
		return -1;
	}
	mp_encode_array(pos, column_count);

	for (int i = 0; i < column_count; ++i) {
		if (sql_column_to_messagepack(stmt, i, region) != 0)
			goto error;
	}
	size = region_used(region) - svp;
	pos = (char *) region_join(region, size);
	if (pos == NULL) {
		diag_set(OutOfMemory, size, "region_join", "pos");
		goto error;
	}
	struct tuple *tuple =
		tuple_new(box_tuple_format_default(), pos, pos + size);
	if (tuple == NULL)
		goto error;
	region_truncate(region, svp);
	return port_tuple_add(port, tuple);

error:
	region_truncate(region, svp);
	return -1;
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
sql_bind_column(struct sql_stmt *stmt, const struct sql_bind *p,
		uint32_t pos)
{
	int rc;
	if (p->name != NULL) {
		pos = sql_bind_parameter_lindex(stmt, p->name, p->name_len);
		if (pos == 0) {
			diag_set(ClientError, ER_SQL_BIND_NOT_FOUND,
				 sql_bind_name(p));
			return -1;
		}
	}
	switch (p->type) {
	case SQL_INTEGER:
		rc = sql_bind_int64(stmt, pos, p->i64);
		break;
	case SQL_FLOAT:
		rc = sql_bind_double(stmt, pos, p->d);
		break;
	case SQL_TEXT:
		/*
		 * Parameters are allocated within message pack,
		 * received from the iproto thread. IProto thread
		 * now is waiting for the response and it will not
		 * free the packet until sql_finalize. So
		 * there is no need to copy the packet and we can
		 * use SQL_STATIC.
		 */
		rc = sql_bind_text64(stmt, pos, p->s, p->bytes,
					 SQL_STATIC);
		break;
	case SQL_NULL:
		rc = sql_bind_null(stmt, pos);
		break;
	case SQL_BLOB:
		rc = sql_bind_blob64(stmt, pos, (const void *) p->s,
					 p->bytes, SQL_STATIC);
		break;
	default:
		unreachable();
	}
	if (rc == SQL_OK)
		return 0;

	switch (rc) {
	case SQL_NOMEM:
		diag_set(OutOfMemory, p->bytes, "vdbe", "bind value");
		break;
	case SQL_TOOBIG:
	default:
		diag_set(ClientError, ER_SQL_BIND_VALUE, sql_bind_name(p),
			 sql_type_strs[p->type]);
		break;
	}
	return -1;
}

/**
 * Bind parameter values to the prepared statement.
 * @param stmt Prepared statement.
 * @param bind Parameters to bind.
 * @param bind_count Length of @a bind.
 *
 * @retval  0 Success.
 * @retval -1 Client or memory error.
 */
static inline int
sql_bind(struct sql_stmt *stmt, const struct sql_bind *bind,
	 uint32_t bind_count)
{
	assert(stmt != NULL);
	uint32_t pos = 1;
	for (uint32_t i = 0; i < bind_count; pos = ++i + 1) {
		if (sql_bind_column(stmt, &bind[i], pos) != 0)
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
sql_get_description(struct sql_stmt *stmt, struct obuf *out,
		    int column_count)
{
	assert(column_count > 0);
	int size = mp_sizeof_uint(IPROTO_METADATA) +
		   mp_sizeof_array(column_count);
	char *pos = (char *) obuf_alloc(out, size);
	if (pos == NULL) {
		diag_set(OutOfMemory, size, "obuf_alloc", "pos");
		return -1;
	}
	pos = mp_encode_uint(pos, IPROTO_METADATA);
	pos = mp_encode_array(pos, column_count);
	for (int i = 0; i < column_count; ++i) {
		size_t size = mp_sizeof_map(2) +
			      mp_sizeof_uint(IPROTO_FIELD_NAME) +
			      mp_sizeof_uint(IPROTO_FIELD_TYPE);
		const char *name = sql_column_name(stmt, i);
		const char *type = sql_column_datatype(stmt, i);
		/*
		 * Can not fail, since all column names are
		 * preallocated during prepare phase and the
		 * column_name simply returns them.
		 */
		assert(name != NULL);
		size += mp_sizeof_str(strlen(name));
		size += mp_sizeof_str(strlen(type));
		char *pos = (char *) obuf_alloc(out, size);
		if (pos == NULL) {
			diag_set(OutOfMemory, size, "obuf_alloc", "pos");
			return -1;
		}
		pos = mp_encode_map(pos, 2);
		pos = mp_encode_uint(pos, IPROTO_FIELD_NAME);
		pos = mp_encode_str(pos, name, strlen(name));
		pos = mp_encode_uint(pos, IPROTO_FIELD_TYPE);
		pos = mp_encode_str(pos, type, strlen(type));
	}
	return 0;
}

static inline int
sql_execute(sql *db, struct sql_stmt *stmt, struct port *port,
	    struct region *region)
{
	int rc, column_count = sql_column_count(stmt);
	if (column_count > 0) {
		/* Either ROW or DONE or ERROR. */
		while ((rc = sql_step(stmt)) == SQL_ROW) {
			if (sql_row_to_port(stmt, column_count, region,
					    port) != 0)
				return -1;
		}
		assert(rc == SQL_DONE || rc != SQL_OK);
	} else {
		/* No rows. Either DONE or ERROR. */
		rc = sql_step(stmt);
		assert(rc != SQL_ROW && rc != SQL_OK);
	}
	if (rc != SQL_DONE) {
		diag_set(ClientError, ER_SQL_EXECUTE, sql_errmsg(db));
		return -1;
	}
	return 0;
}

int
sql_prepare_and_execute(const char *sql, int len, const struct sql_bind *bind,
			uint32_t bind_count, struct sql_response *response,
			struct region *region)
{
	struct sql_stmt *stmt;
	struct sql *db = sql_get();
	if (db == NULL) {
		diag_set(ClientError, ER_LOADING);
		return -1;
	}
	if (sql_prepare_v2(db, sql, len, &stmt, NULL) != SQL_OK) {
		diag_set(ClientError, ER_SQL_EXECUTE, sql_errmsg(db));
		return -1;
	}
	assert(stmt != NULL);
	port_tuple_create(&response->port);
	response->prep_stmt = stmt;
	if (sql_bind(stmt, bind, bind_count) == 0 &&
	    sql_execute(db, stmt, &response->port, region) == 0)
		return 0;
	port_destroy(&response->port);
	sql_finalize(stmt);
	return -1;
}

int
sql_response_dump(struct sql_response *response, struct obuf *out)
{
	sql *db = sql_get();
	struct sql_stmt *stmt = (struct sql_stmt *) response->prep_stmt;
	struct port_tuple *port_tuple = (struct port_tuple *) &response->port;
	int rc = 0, column_count = sql_column_count(stmt);
	if (column_count > 0) {
		int keys = 2;
		int size = mp_sizeof_map(keys);
		char *pos = (char *) obuf_alloc(out, size);
		if (pos == NULL) {
			diag_set(OutOfMemory, size, "obuf_alloc", "pos");
			goto err;
		}
		pos = mp_encode_map(pos, keys);
		if (sql_get_description(stmt, out, column_count) != 0) {
err:
			rc = -1;
			goto finish;
		}
		size = mp_sizeof_uint(IPROTO_DATA) +
		       mp_sizeof_array(port_tuple->size);
		pos = (char *) obuf_alloc(out, size);
		if (pos == NULL) {
			diag_set(OutOfMemory, size, "obuf_alloc", "pos");
			goto err;
		}
		pos = mp_encode_uint(pos, IPROTO_DATA);
		pos = mp_encode_array(pos, port_tuple->size);
		/*
		 * Just like SELECT, SQL uses output format compatible
		 * with Tarantool 1.6
		 */
		if (port_dump_msgpack_16(&response->port, out) < 0) {
			/* Failed port dump destroyes the port. */
			goto err;
		}
	} else {
		int keys = 1;
		assert(port_tuple->size == 0);
		struct stailq *autoinc_id_list =
			vdbe_autoinc_id_list((struct Vdbe *)stmt);
		uint32_t map_size = stailq_empty(autoinc_id_list) ? 1 : 2;
		int size = mp_sizeof_map(keys) +
			   mp_sizeof_uint(IPROTO_SQL_INFO) +
			   mp_sizeof_map(map_size);
		char *pos = (char *) obuf_alloc(out, size);
		if (pos == NULL) {
			diag_set(OutOfMemory, size, "obuf_alloc", "pos");
			goto err;
		}
		pos = mp_encode_map(pos, keys);
		pos = mp_encode_uint(pos, IPROTO_SQL_INFO);
		pos = mp_encode_map(pos, map_size);
		uint64_t id_count = 0;
		int changes = db->nChange;
		size = mp_sizeof_uint(SQL_INFO_ROW_COUNT) +
		       mp_sizeof_uint(changes);
		if (!stailq_empty(autoinc_id_list)) {
			struct autoinc_id_entry *id_entry;
			stailq_foreach_entry(id_entry, autoinc_id_list, link) {
				size += id_entry->id >= 0 ?
					mp_sizeof_uint(id_entry->id) :
					mp_sizeof_int(id_entry->id);
				id_count++;
			}
			size += mp_sizeof_uint(SQL_INFO_AUTOINCREMENT_IDS) +
				mp_sizeof_array(id_count);
		}
		char *buf = obuf_alloc(out, size);
		if (buf == NULL) {
			diag_set(OutOfMemory, size, "obuf_alloc", "buf");
			goto err;
		}
		buf = mp_encode_uint(buf, SQL_INFO_ROW_COUNT);
		buf = mp_encode_uint(buf, changes);
		if (!stailq_empty(autoinc_id_list)) {
			buf = mp_encode_uint(buf, SQL_INFO_AUTOINCREMENT_IDS);
			buf = mp_encode_array(buf, id_count);
			struct autoinc_id_entry *id_entry;
			stailq_foreach_entry(id_entry, autoinc_id_list, link) {
				buf = id_entry->id >= 0 ?
				      mp_encode_uint(buf, id_entry->id) :
				      mp_encode_int(buf, id_entry->id);
			}
		}
	}
finish:
	port_destroy(&response->port);
	sql_finalize(stmt);
	return rc;
}
