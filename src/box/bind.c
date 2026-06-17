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
#include "mp_interval.h"
#include "mp_datetime.h"
#include "mp_decimal.h"
#include "mp_uuid.h"

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
	case MP_EXT: {
		int8_t ext_type;
		uint32_t size = mp_decode_extl(packet, &ext_type);
		switch (ext_type) {
		case MP_UUID:
			VERIFY(uuid_unpack(packet, size, &bind->uuid) != NULL);
			break;
		case MP_DECIMAL:
			VERIFY(decimal_unpack(packet, size,
					      &bind->dec) != NULL);
			break;
		case MP_DATETIME:
			VERIFY(datetime_unpack(packet, size,
					       &bind->dt) != NULL);
			break;
		case MP_INTERVAL:
			VERIFY(interval_unpack(packet, size,
					       &bind->itv) != NULL);
			break;
		default:
			diag_set(ClientError, ER_SQL_BIND_TYPE, "USERDATA",
				 sql_bind_name(bind));
			return -1;
		}
		bind->ext_type = ext_type;
		break;
	}
	case MP_ARRAY:
	case MP_MAP:
		bind->s = *packet;
		mp_next(packet);
		bind->bytes = *packet - bind->s;
		break;
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
	struct sql_bind *bind = xregion_alloc_array(region, typeof(bind[0]),
						    bind_count);
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
sql_bind_column(struct Vdbe *stmt, const struct sql_bind *p, uint32_t pos)
{
	if (pos == 0) {
		diag_set(ClientError, ER_SQL_BIND_NOT_FOUND,
			 sql_bind_name(p));
		return -1;
	}
	switch (p->type) {
	case MP_INT:
		return sql_bind_int64(stmt, pos);
	case MP_UINT:
		return sql_bind_uint64(stmt, pos);
	case MP_BOOL:
		return sql_bind_boolean(stmt, pos);
	case MP_DOUBLE:
	case MP_FLOAT:
		return sql_bind_double(stmt, pos);
	case MP_STR:
		/*
		 * Parameters are allocated within message pack,
		 * received from the iproto thread. IProto thread
		 * now is waiting for the response and it will not
		 * free the packet until sql_stmt_finalize. So
		 * there is no need to copy the packet and we can
		 * use SQL_STATIC.
		 */
		return sql_bind_str_static(stmt, pos);
	case MP_NIL:
		return sql_bind_null(stmt, pos);
	case MP_BIN:
		return sql_bind_bin_static(stmt, pos);
	case MP_ARRAY:
		return sql_bind_array_static(stmt, pos);
	case MP_MAP:
		return sql_bind_map_static(stmt, pos);
	case MP_EXT:
		switch (p->ext_type) {
		case MP_UUID:
			return sql_bind_uuid(stmt, pos);
		case MP_DECIMAL:
			return sql_bind_dec(stmt, pos);
		case MP_DATETIME:
			return sql_bind_datetime(stmt, pos);
		case MP_INTERVAL:
			return sql_bind_interval(stmt, pos);
		default:
			unreachable();
		}
	default:
		unreachable();
	}
	return 0;
}

int
sql_bind(struct Vdbe *stmt, const struct sql_bind *bind, uint32_t bind_count)
{
	assert(stmt != NULL);
	uint32_t last_idx = 0;
	if (bind_count > 0) {
	    for (uint32_t i = 0; i < sql_get_count_original_names(stmt); i++) {
		    uint32_t n = sql_get_original_names_len(stmt, i);
		    char *name = sql_get_original_names(stmt, i);
		    int flag = 1;
		    if (n == 1) {
			    if (name[0] == '?') {
				    if (sql_bind_column(stmt, &bind[last_idx], i + 1) != 0)
					    return -1;
				    last_idx++;
			    }
		    }
		    else if (n > 1) {
			    if (name[0] == '$') {
				    flag = 0;
				    uint32_t number = 0;
				    for (uint32_t idx = 1; idx < n; idx++) {
					    number += name[idx] - '0';
				    }
				    if (number > bind_count) {
					    printf("Very Bad\n");
				    }
				    last_idx = number;
				    if (sql_bind_column(stmt, &bind[number - 1], i + 1) != 0)
					    return -1;
			    }
		    }
		    if (flag) {
			    for (uint32_t j = 0; j < bind_count; j++) {
				    if (bind[j].name_len == n) {
					    if (strcmp(name, bind[j].name) == 0) {
						    last_idx = j;
						    if (sql_bind_column(stmt, &bind[j], i + 1) != 0)
							    return -1;
						    break;
					    }
				    }
			    }
		    }
	    }
	}
	for (uint32_t i = 0; i < bind_count; i++) {
		const struct sql_bind *p = &bind[i];
		switch (p->type) {
		case MP_INT:
			mem_set_int2(stmt, i, (int64_t)p->i64);
			break;
		case MP_UINT:
			mem_set_uint2(stmt, i, p->u64);
			break;
		case MP_BOOL:
			mem_set_boolean2(stmt, i, p->b);
			break;
		case MP_DOUBLE:
		case MP_FLOAT:
			mem_set_double2(stmt, i, p->d);
			break;
		case MP_STR:
			/*
			 * Parameters are allocated within message pack,
			 * received from the iproto thread. IProto thread
			 * now is waiting for the response and it will not
			 * free the packet until sql_stmt_finalize. So
			 * there is no need to copy the packet and we can
			 * use SQL_STATIC.
			 */
			mem_set_str_static2(stmt, i, p->s, p->bytes);
			break;
		case MP_NIL:
			break;
		case MP_BIN:
			mem_set_bin_static2(stmt, i, p->s, p->bytes);
			break;
		case MP_ARRAY:
			mem_set_array_static2(stmt, i, p->s, p->bytes);
			break;
		case MP_MAP:
			mem_set_map_static2(stmt, i, p->s, p->bytes);
			break;
		case MP_EXT:
			switch (p->ext_type) {
			case MP_UUID:
				mem_set_uuid2(stmt, i, &p->uuid);
				break;
			case MP_DECIMAL:
				mem_set_dec2(stmt, i, &p->dec);
				break;
			case MP_DATETIME:
				mem_set_datetime2(stmt, i, &p->dt);
				break;
			case MP_INTERVAL:
				mem_set_interval2(stmt, i, &p->itv);
				break;
			default:
				unreachable();
				break;
			}
			break;
		default:
			unreachable();
			break;
		}
	}
	return 0;
}
