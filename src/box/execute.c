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

#include "assoc.h"
#include "bind.h"
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
#include "box/lua/execute.h"
#include "box/sql_stmt_cache.h"
#include "session.h"
#include "rmean.h"

const char *sql_info_key_strs[] = {
	"row_count",
	"autoincrement_ids",
};

static_assert(sizeof(struct port_sql) <= sizeof(struct port),
	      "sizeof(struct port_sql) must be <= sizeof(struct port)");

/**
 * Dump data from port to buffer. Data in port contains tuples,
 * metadata, or information obtained from an executed SQL query.
 *
 * Dumped msgpack structure:
 * +----------------------------------------------+
 * | IPROTO_BODY: {                               |
 * |     IPROTO_METADATA: [                       |
 * |         {IPROTO_FIELD_NAME: column name1},   |
 * |         {IPROTO_FIELD_NAME: column name2},   |
 * |         ...                                  |
 * |     ],                                       |
 * |                                              |
 * |     IPROTO_DATA: [                           |
 * |         tuple, tuple, tuple, ...             |
 * |     ]                                        |
 * | }                                            |
 * +-------------------- OR ----------------------+
 * | IPROTO_BODY: {                               |
 * |     IPROTO_SQL_INFO: {                       |
 * |         SQL_INFO_ROW_COUNT: number           |
 * |         SQL_INFO_AUTOINCREMENT_IDS: [        |
 * |             id, id, id, ...                  |
 * |         ]                                    |
 * |     }                                        |
 * | }                                            |
 * +-------------------- OR ----------------------+
 * | IPROTO_BODY: {                               |
 * |     IPROTO_SQL_INFO: {                       |
 * |         SQL_INFO_ROW_COUNT: number           |
 * |     }                                        |
 * | }                                            |
 * +----------------------------------------------+
 * @param port Port that contains SQL response.
 * @param[out] out Output buffer.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
static int
port_sql_dump_msgpack(struct port *port, struct obuf *out);

void
port_sql_destroy(struct port *base)
{
	port_c_vtab.destroy(base);
	struct port_sql *port_sql = (struct port_sql *) base;
	if (port_sql->do_finalize)
		sql_stmt_finalize(((struct port_sql *)base)->stmt);
}

const struct port_vtab port_sql_vtab = {
	/* .dump_msgpack = */ port_sql_dump_msgpack,
	/* .dump_msgpack_16 = */ NULL,
	/* .dump_lua = */ port_sql_dump_lua,
	/* .dump_plain = */ NULL,
	/* .get_msgpack = */ NULL,
	/* .get_vdbemem = */ NULL,
	/* .destroy = */ port_sql_destroy,
};

void
port_sql_create(struct port *port, struct sql_stmt *stmt,
		enum sql_serialization_format format, bool do_finalize)
{
	port_c_create(port);
	port->vtab = &port_sql_vtab;
	struct port_sql *port_sql = (struct port_sql *) port;
	port_sql->stmt = stmt;
	port_sql->serialization_format = format;
	port_sql->do_finalize = do_finalize;
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
	enum mp_type type = sql_column_type(stmt, i);
	switch (type) {
	case MP_INT: {
		int64_t n = sql_column_int64(stmt, i);
		size = mp_sizeof_int(n);
		char *pos = (char *) region_alloc(region, size);
		if (pos == NULL)
			goto oom;
		mp_encode_int(pos, n);
		break;
	}
	case MP_UINT: {
		uint64_t n = sql_column_uint64(stmt, i);
		size = mp_sizeof_uint(n);
		char *pos = (char *) region_alloc(region, size);
		if (pos == NULL)
			goto oom;
		mp_encode_uint(pos, n);
		break;
	}
	case MP_DOUBLE: {
		double d = sql_column_double(stmt, i);
		size = mp_sizeof_double(d);
		char *pos = (char *) region_alloc(region, size);
		if (pos == NULL)
			goto oom;
		mp_encode_double(pos, d);
		break;
	}
	case MP_STR: {
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
	case MP_BIN:
	case MP_MAP:
	case MP_ARRAY: {
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
	case MP_BOOL: {
		bool b = sql_column_boolean(stmt, i);
		size = mp_sizeof_bool(b);
		char *pos = (char *) region_alloc(region, size);
		if (pos == NULL)
			goto oom;
		mp_encode_bool(pos, b);
		break;
	}
	case MP_NIL: {
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
	return port_c_add_tuple(port, tuple);

error:
	region_truncate(region, svp);
	return -1;
}

static inline size_t
metadata_map_sizeof(const char *name, const char *type, const char *coll,
		    const char *span, int nullable, bool is_autoincrement)
{
	uint32_t members_count = 2;
	size_t map_size = 0;
	if (coll != NULL) {
		members_count++;
		map_size += mp_sizeof_uint(IPROTO_FIELD_COLL);
		map_size += mp_sizeof_str(strlen(coll));
	}
	if (nullable != -1) {
		members_count++;
		map_size += mp_sizeof_uint(IPROTO_FIELD_IS_NULLABLE);
		map_size += mp_sizeof_bool(nullable);
	}
	if (is_autoincrement) {
		members_count++;
		map_size += mp_sizeof_uint(IPROTO_FIELD_IS_AUTOINCREMENT);
		map_size += mp_sizeof_bool(true);
	}
	if (sql_metadata_is_full()) {
		members_count++;
		map_size += mp_sizeof_uint(IPROTO_FIELD_SPAN);
		map_size += span != NULL ? mp_sizeof_str(strlen(span)) :
			    mp_sizeof_nil();
	}
	map_size += mp_sizeof_uint(IPROTO_FIELD_NAME);
	map_size += mp_sizeof_uint(IPROTO_FIELD_TYPE);
	map_size += mp_sizeof_str(strlen(name));
	map_size += mp_sizeof_str(strlen(type));
	map_size += mp_sizeof_map(members_count);
	return map_size;
}

static inline void
metadata_map_encode(char *buf, const char *name, const char *type,
		    const char *coll, const char *span, int nullable,
		    bool is_autoincrement)
{
	bool is_full = sql_metadata_is_full();
	uint32_t map_sz = 2 + (coll != NULL) + (nullable != -1) +
			  is_autoincrement + is_full;
	buf = mp_encode_map(buf, map_sz);
	buf = mp_encode_uint(buf, IPROTO_FIELD_NAME);
	buf = mp_encode_str(buf, name, strlen(name));
	buf = mp_encode_uint(buf, IPROTO_FIELD_TYPE);
	buf = mp_encode_str(buf, type, strlen(type));
	if (coll != NULL) {
		buf = mp_encode_uint(buf, IPROTO_FIELD_COLL);
		buf = mp_encode_str(buf, coll, strlen(coll));
	}
	if (nullable != -1) {
		buf = mp_encode_uint(buf, IPROTO_FIELD_IS_NULLABLE);
		buf = mp_encode_bool(buf, nullable);
	}
	if (is_autoincrement) {
		buf = mp_encode_uint(buf, IPROTO_FIELD_IS_AUTOINCREMENT);
		buf = mp_encode_bool(buf, true);
	}
	if (! is_full)
		return;
	/*
	 * Span is an original expression that forms
	 * result set column. In most cases it is the
	 * same as column name. So to avoid sending
	 * the same string twice simply encode it as
	 * a nil and account this behaviour on client
	 * side (see decode_metadata_optional()).
	 */
	buf = mp_encode_uint(buf, IPROTO_FIELD_SPAN);
	if (span != NULL)
		buf = mp_encode_str(buf, span, strlen(span));
	else
		buf = mp_encode_nil(buf);
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
sql_get_metadata(struct sql_stmt *stmt, struct obuf *out, int column_count)
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
		const char *coll = sql_column_coll(stmt, i);
		const char *name = sql_column_name(stmt, i);
		const char *type = sql_column_datatype(stmt, i);
		const char *span = sql_column_span(stmt, i);
		int nullable = sql_column_nullable(stmt, i);
		bool is_autoincrement = sql_column_is_autoincrement(stmt, i);
		/*
		 * Can not fail, since all column names and types
		 * are preallocated during prepare phase and the
		 * column_name simply returns them.
		 */
		assert(name != NULL);
		assert(type != NULL);
		size = metadata_map_sizeof(name, type, coll, span, nullable,
					   is_autoincrement);
		char *pos = (char *) obuf_alloc(out, size);
		if (pos == NULL) {
			diag_set(OutOfMemory, size, "obuf_alloc", "pos");
			return -1;
		}
		metadata_map_encode(pos, name, type, coll, span, nullable,
				    is_autoincrement);
	}
	return 0;
}

static inline int
sql_get_params_metadata(struct sql_stmt *stmt, struct obuf *out)
{
	int bind_count = sql_bind_parameter_count(stmt);
	int size = mp_sizeof_uint(IPROTO_BIND_METADATA) +
		   mp_sizeof_array(bind_count);
	char *pos = (char *) obuf_alloc(out, size);
	if (pos == NULL) {
		diag_set(OutOfMemory, size, "obuf_alloc", "pos");
		return -1;
	}
	pos = mp_encode_uint(pos, IPROTO_BIND_METADATA);
	pos = mp_encode_array(pos, bind_count);
	for (int i = 0; i < bind_count; ++i) {
		size_t size = mp_sizeof_map(2) +
			      mp_sizeof_uint(IPROTO_FIELD_NAME) +
			      mp_sizeof_uint(IPROTO_FIELD_TYPE);
		const char *name = sql_bind_parameter_name(stmt, i);
		if (name == NULL)
			name = "?";
		const char *type = "ANY";
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

static int
sql_get_prepare_common_keys(struct sql_stmt *stmt, struct obuf *out, int keys)
{
	const char *sql_str = sql_stmt_query_str(stmt);
	uint32_t stmt_id = sql_stmt_calculate_id(sql_str, strlen(sql_str));
	int size = mp_sizeof_map(keys) +
		   mp_sizeof_uint(IPROTO_STMT_ID) +
		   mp_sizeof_uint(stmt_id) +
		   mp_sizeof_uint(IPROTO_BIND_COUNT) +
		   mp_sizeof_uint(sql_bind_parameter_count(stmt));
	char *pos = (char *) obuf_alloc(out, size);
	if (pos == NULL) {
		diag_set(OutOfMemory, size, "obuf_alloc", "pos");
		return -1;
	}
	pos = mp_encode_map(pos, keys);
	pos = mp_encode_uint(pos, IPROTO_STMT_ID);
	pos = mp_encode_uint(pos, stmt_id);
	pos = mp_encode_uint(pos, IPROTO_BIND_COUNT);
	pos = mp_encode_uint(pos, sql_bind_parameter_count(stmt));
	if (sql_get_params_metadata(stmt, out) != 0)
		return -1;
	return 0;
}

static int
port_sql_dump_msgpack(struct port *port, struct obuf *out)
{
	assert(port->vtab == &port_sql_vtab);
	sql *db = sql_get();
	struct port_sql *sql_port = (struct port_sql *)port;
	struct sql_stmt *stmt = sql_port->stmt;
	switch (sql_port->serialization_format) {
	case DQL_EXECUTE: {
		int keys = 2;
		int size = mp_sizeof_map(keys);
		char *pos = (char *) obuf_alloc(out, size);
		if (pos == NULL) {
			diag_set(OutOfMemory, size, "obuf_alloc", "pos");
			return -1;
		}
		pos = mp_encode_map(pos, keys);
		if (sql_get_metadata(stmt, out, sql_column_count(stmt)) != 0)
			return -1;
		size = mp_sizeof_uint(IPROTO_DATA);
		pos = (char *) obuf_alloc(out, size);
		if (pos == NULL) {
			diag_set(OutOfMemory, size, "obuf_alloc", "pos");
			return -1;
		}
		pos = mp_encode_uint(pos, IPROTO_DATA);
		if (port_c_vtab.dump_msgpack(port, out) < 0)
			return -1;
		break;
	}
	case DML_EXECUTE: {
		int keys = 1;
		assert(((struct port_c *)port)->size == 0);
		struct stailq *autoinc_id_list =
			vdbe_autoinc_id_list((struct Vdbe *)stmt);
		uint32_t map_size = stailq_empty(autoinc_id_list) ? 1 : 2;
		int size = mp_sizeof_map(keys) +
			   mp_sizeof_uint(IPROTO_SQL_INFO) +
			   mp_sizeof_map(map_size);
		char *pos = (char *) obuf_alloc(out, size);
		if (pos == NULL) {
			diag_set(OutOfMemory, size, "obuf_alloc", "pos");
			return -1;
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
			return -1;
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
		break;
	}
	case DQL_PREPARE: {
		/* Format is following:
		 * query_id,
		 * param_count,
		 * params {name, type},
		 * metadata {name, type}
		 */
		int keys = 4;
		if (sql_get_prepare_common_keys(stmt, out, keys) != 0)
			return -1;
		return sql_get_metadata(stmt, out, sql_column_count(stmt));
	}
	case DML_PREPARE: {
		/* Format is following:
		 * query_id,
		 * param_count,
		 * params {name, type},
		 */
		int keys = 3;
		return sql_get_prepare_common_keys(stmt, out, keys);
		}
	default: {
		unreachable();
	}
	}
	return 0;
}

static bool
sql_stmt_schema_version_is_valid(struct sql_stmt *stmt)
{
	return sql_stmt_schema_version(stmt) == box_schema_version();
}

/**
 * Re-compile statement and refresh global prepared statement
 * cache with the newest value.
 */
static int
sql_reprepare(struct sql_stmt **stmt)
{
	const char *sql_str = sql_stmt_query_str(*stmt);
	struct sql_stmt *new_stmt;
	if (sql_stmt_compile(sql_str, strlen(sql_str), NULL,
			     &new_stmt, NULL) != 0)
		return -1;
	if (sql_stmt_cache_update(*stmt, new_stmt) != 0)
		return -1;
	*stmt = new_stmt;
	return 0;
}

/**
 * Compile statement and save it to the global holder;
 * update session hash with prepared statement ID (if
 * it's not already there).
 */
int
sql_prepare(const char *sql, int len, struct port *port)
{
	uint32_t stmt_id = sql_stmt_calculate_id(sql, len);
	struct sql_stmt *stmt = sql_stmt_cache_find(stmt_id);
	rmean_collect(rmean_box, IPROTO_PREPARE, 1);
	if (stmt == NULL) {
		if (sql_stmt_compile(sql, len, NULL, &stmt, NULL) != 0)
			return -1;
		if (sql_stmt_cache_insert(stmt) != 0) {
			sql_stmt_finalize(stmt);
			return -1;
		}
	} else {
		if (!sql_stmt_schema_version_is_valid(stmt) &&
		    !sql_stmt_busy(stmt)) {
			if (sql_reprepare(&stmt) != 0)
				return -1;
		}
	}
	assert(stmt != NULL);
	/* Add id to the list of available statements in session. */
	if (!session_check_stmt_id(current_session(), stmt_id))
		session_add_stmt_id(current_session(), stmt_id);
	enum sql_serialization_format format = sql_column_count(stmt) > 0 ?
					   DQL_PREPARE : DML_PREPARE;
	port_sql_create(port, stmt, format, false);

	return 0;
}

/**
 * Deallocate prepared statement from current session:
 * remove its ID from session-local hash and unref entry
 * in global holder.
 */
int
sql_unprepare(uint32_t stmt_id)
{
	if (!session_check_stmt_id(current_session(), stmt_id)) {
		diag_set(ClientError, ER_WRONG_QUERY_ID, stmt_id);
		return -1;
	}
	session_remove_stmt_id(current_session(), stmt_id);
	sql_stmt_unref(stmt_id);
	return 0;
}

/**
 * Execute prepared SQL statement.
 *
 * This function uses region to allocate memory for temporary
 * objects. After this function, region will be in the same state
 * in which it was before this function.
 *
 * @param db SQL handle.
 * @param stmt Prepared statement.
 * @param port Port to store SQL response.
 * @param region Region to allocate temporary objects.
 *
 * @retval  0 Success.
 * @retval -1 Error.
 */
int
sql_execute(struct sql_stmt *stmt, struct port *port, struct region *region)
{
	int rc, column_count = sql_column_count(stmt);
	rmean_collect(rmean_box, IPROTO_EXECUTE, 1);
	if (column_count > 0) {
		/* Either ROW or DONE or ERROR. */
		while ((rc = sql_step(stmt)) == SQL_ROW) {
			if (sql_row_to_port(stmt, column_count, region,
					    port) != 0)
				return -1;
		}
		assert(rc == SQL_DONE || rc != 0);
	} else {
		/* No rows. Either DONE or ERROR. */
		rc = sql_step(stmt);
		assert(rc != SQL_ROW && rc != 0);
	}
	if (rc != SQL_DONE)
		return -1;
	return 0;
}

int
sql_execute_prepared(uint32_t stmt_id, const struct sql_bind *bind,
		     uint32_t bind_count, struct port *port,
		     struct region *region)
{

	if (!session_check_stmt_id(current_session(), stmt_id)) {
		diag_set(ClientError, ER_WRONG_QUERY_ID, stmt_id);
		return -1;
	}
	struct sql_stmt *stmt = sql_stmt_cache_find(stmt_id);
	assert(stmt != NULL);
	if (!sql_stmt_schema_version_is_valid(stmt)) {
		diag_set(ClientError, ER_SQL_EXECUTE, "statement has expired");
		return -1;
	}
	if (sql_stmt_busy(stmt)) {
		const char *sql_str = sql_stmt_query_str(stmt);
		return sql_prepare_and_execute(sql_str, strlen(sql_str), bind,
					       bind_count, port, region);
	}
	/*
	 * Clear all set from previous execution cycle
	 * values to be bound.
	 */
	sql_unbind(stmt);
	if (sql_bind(stmt, bind, bind_count) != 0)
		return -1;
	enum sql_serialization_format format = sql_column_count(stmt) > 0 ?
					       DQL_EXECUTE : DML_EXECUTE;
	port_sql_create(port, stmt, format, false);
	if (sql_execute(stmt, port, region) != 0) {
		port_destroy(port);
		sql_stmt_reset(stmt);
		return -1;
	}
	sql_stmt_reset(stmt);

	return 0;
}

int
sql_prepare_and_execute(const char *sql, int len, const struct sql_bind *bind,
			uint32_t bind_count, struct port *port,
			struct region *region)
{
	struct sql_stmt *stmt;
	if (sql_stmt_compile(sql, len, NULL, &stmt, NULL) != 0)
		return -1;
	assert(stmt != NULL);
	enum sql_serialization_format format = sql_column_count(stmt) > 0 ?
					   DQL_EXECUTE : DML_EXECUTE;
	port_sql_create(port, stmt, format, true);
	if (sql_bind(stmt, bind, bind_count) == 0 &&
	    sql_execute(stmt, port, region) == 0)
		return 0;
	port_destroy(port);
	return -1;
}
