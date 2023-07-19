/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "port.h"
#include "sqlInt.h"
#include "small/obuf.h"
#include "box/execute.h"
#include "box/lua/execute.h"
#include "box/sql_stmt_cache.h"
#include "box/iproto_constants.h"

/** The size of the metadata encoded in msgpack format. */
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

/** Encode metadata in msgpack format. */
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
	if (!is_full)
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
sql_get_metadata(struct Vdbe *stmt, struct obuf *out, int column_count)
{
	assert(column_count > 0);
	int size = mp_sizeof_uint(IPROTO_METADATA) +
		   mp_sizeof_array(column_count);
	char *pos = obuf_alloc(out, size);
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
		char *pos = obuf_alloc(out, size);
		if (pos == NULL) {
			diag_set(OutOfMemory, size, "obuf_alloc", "pos");
			return -1;
		}
		metadata_map_encode(pos, name, type, coll, span, nullable,
				    is_autoincrement);
	}
	return 0;
}

/** Get metadata of bound variables. */
static inline int
sql_get_params_metadata(struct Vdbe *stmt, struct obuf *out)
{
	int bind_count = sql_bind_parameter_count(stmt);
	int size = mp_sizeof_uint(IPROTO_BIND_METADATA) +
		   mp_sizeof_array(bind_count);
	char *pos = obuf_alloc(out, size);
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
		char *pos = obuf_alloc(out, size);
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

/**
 * Get the metadata part containing prepared statement ID, number of bound
 * variables, and metadata of bound variables.
 */
static int
sql_get_prepare_common_keys(struct Vdbe *stmt, struct obuf *out, int keys)
{
	const char *sql_str = sql_stmt_query_str(stmt);
	uint32_t stmt_id = sql_stmt_calculate_id(sql_str, strlen(sql_str));
	int size = mp_sizeof_map(keys) +
		   mp_sizeof_uint(IPROTO_STMT_ID) +
		   mp_sizeof_uint(stmt_id) +
		   mp_sizeof_uint(IPROTO_BIND_COUNT) +
		   mp_sizeof_uint(sql_bind_parameter_count(stmt));
	char *pos = obuf_alloc(out, size);
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
port_sql_dump_msgpack(struct port *port, struct obuf *out)
{
	assert(port->vtab == &port_sql_vtab);
	struct port_sql *sql_port = (struct port_sql *)port;
	struct Vdbe *stmt = sql_port->stmt;
	switch (sql_port->serialization_format) {
	case DQL_EXECUTE: {
		int keys = 2;
		int size = mp_sizeof_map(keys);
		char *pos = obuf_alloc(out, size);
		if (pos == NULL) {
			diag_set(OutOfMemory, size, "obuf_alloc", "pos");
			return -1;
		}
		pos = mp_encode_map(pos, keys);
		if (sql_get_metadata(stmt, out, sql_column_count(stmt)) != 0)
			return -1;
		size = mp_sizeof_uint(IPROTO_DATA);
		pos = obuf_alloc(out, size);
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
			vdbe_autoinc_id_list(stmt);
		uint32_t map_size = stailq_empty(autoinc_id_list) ? 1 : 2;
		int size = mp_sizeof_map(keys) +
			   mp_sizeof_uint(IPROTO_SQL_INFO) +
			   mp_sizeof_map(map_size);
		char *pos = obuf_alloc(out, size);
		if (pos == NULL) {
			diag_set(OutOfMemory, size, "obuf_alloc", "pos");
			return -1;
		}
		pos = mp_encode_map(pos, keys);
		pos = mp_encode_uint(pos, IPROTO_SQL_INFO);
		pos = mp_encode_map(pos, map_size);
		uint64_t id_count = 0;
		int changes = sql_get()->nChange;
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
	case UNPREPARE: {
		int size = mp_sizeof_map(0);
		char *pos = obuf_alloc(out, size);
		if (pos == NULL) {
			diag_set(OutOfMemory, size, "obuf_alloc", "pos");
			return -1;
		}
		mp_encode_map(pos, 0);
		break;
	}
	default: {
		unreachable();
	}
	}
	return 0;
}

static const char *
port_sql_get_msgpack(struct port *base, uint32_t *size)
{
	return port_c_vtab.get_msgpack(base, size);
}

static void
port_sql_destroy(struct port *base)
{
	port_c_vtab.destroy(base);
	struct port_sql *port_sql = (struct port_sql *)base;
	if (port_sql->do_finalize)
		sql_stmt_finalize(((struct port_sql *)base)->stmt);
}

const struct port_vtab port_sql_vtab = {
	.dump_msgpack = port_sql_dump_msgpack,
	.dump_msgpack_16 = NULL,
	.dump_lua = port_sql_dump_lua,
	.dump_plain = NULL,
	.get_msgpack = port_sql_get_msgpack,
	.get_vdbemem = NULL,
	.destroy = port_sql_destroy,
};

void
port_sql_create(struct port *port, struct Vdbe *stmt,
		enum sql_serialization_format format, bool do_finalize)
{
	port_c_create(port);
	port->vtab = &port_sql_vtab;
	struct port_sql *port_sql = (struct port_sql *)port;
	port_sql->stmt = stmt;
	port_sql->serialization_format = format;
	port_sql->do_finalize = do_finalize;
}
