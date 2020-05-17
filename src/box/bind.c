/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
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
#include "bind.h"
#include "errcode.h"
#include "small/region.h"
#include "sql/sqlInt.h"
#include "sql/sqlLimit.h"
#include "sql/vdbe.h"

const char *
sql_bind_name(const struct sql_bind *bind)
{
	if (bind->name)
		return tt_sprintf("'%.*s'", bind->name_len, bind->name);
	else
		return tt_sprintf("%d", (int) bind->pos);
}

int
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
	enum mp_type type = mp_typeof(**packet);
	switch (type) {
	case MP_UINT: {
		uint64_t n = mp_decode_uint(packet);
		bind->u64 = n;
		bind->bytes = sizeof(bind->u64);
		break;
	}
	case MP_INT:
		bind->i64 = mp_decode_int(packet);
		bind->bytes = sizeof(bind->i64);
		break;
	case MP_STR:
		bind->s = mp_decode_str(packet, &bind->bytes);
		break;
	case MP_DOUBLE:
		bind->d = mp_decode_double(packet);
		bind->bytes = sizeof(bind->d);
		break;
	case MP_FLOAT:
		bind->d = mp_decode_float(packet);
		bind->bytes = sizeof(bind->d);
		break;
	case MP_NIL:
		mp_decode_nil(packet);
		bind->bytes = 1;
		break;
	case MP_BOOL:
		bind->b = mp_decode_bool(packet);
		bind->bytes = sizeof(bind->b);
		break;
	case MP_BIN:
		bind->s = mp_decode_bin(packet, &bind->bytes);
		break;
	case MP_EXT:
		bind->s = *packet;
		mp_next(packet);
		bind->bytes = *packet - bind->s;
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
	bind->type = type;
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
	size_t size;
	struct sql_bind *bind = region_alloc_array(region, typeof(bind[0]),
						   bind_count, &size);
	if (bind == NULL) {
		diag_set(OutOfMemory, size, "region_alloc_array", "bind");
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

int
sql_bind_column(struct sql_stmt *stmt, const struct sql_bind *p,
		uint32_t pos)
{
	if (p->name != NULL) {
		pos = sql_bind_parameter_lindex(stmt, p->name, p->name_len);
		if (pos == 0) {
			diag_set(ClientError, ER_SQL_BIND_NOT_FOUND,
				 sql_bind_name(p));
			return -1;
		}
	}
	switch (p->type) {
	case MP_INT:
		return sql_bind_int64(stmt, pos, p->i64);
	case MP_UINT:
		return sql_bind_uint64(stmt, pos, p->u64);
	case MP_BOOL:
		return sql_bind_boolean(stmt, pos, p->b);
	case MP_DOUBLE:
	case MP_FLOAT:
		return sql_bind_double(stmt, pos, p->d);
	case MP_STR:
		/*
		 * Parameters are allocated within message pack,
		 * received from the iproto thread. IProto thread
		 * now is waiting for the response and it will not
		 * free the packet until sql_stmt_finalize. So
		 * there is no need to copy the packet and we can
		 * use SQL_STATIC.
		 */
		return sql_bind_text64(stmt, pos, p->s, p->bytes, SQL_STATIC);
	case MP_NIL:
		return sql_bind_null(stmt, pos);
	case MP_BIN:
		return sql_bind_blob64(stmt, pos, (const void *) p->s, p->bytes,
				       SQL_STATIC);
	default:
		unreachable();
	}
	return 0;
}
