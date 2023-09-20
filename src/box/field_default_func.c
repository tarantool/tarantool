/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "port.h"
#include "engine.h"
#include "tt_static.h"
#include "tuple_format.h"
#include "field_default_func.h"

/**
 * Check `func` for compliance with the field default function rules.
 *
 * Returns 0 on success. On error, sets diag and returns -1.
 */
static int
field_default_func_verify(struct func *func)
{
	if (func->def->language == FUNC_LANGUAGE_SQL_EXPR) {
		assert(func->def->body != NULL);
		return 0;
	}

	const char *func_name = func->def->name;
	if (func->def->language == FUNC_LANGUAGE_LUA) {
		if (func->def->body != NULL)
			return 0;
		diag_set(ClientError, ER_CREATE_DEFAULT_FUNC, func_name,
			 "Lua function must have persistent body");
		return -1;
	}

	diag_set(ClientError, ER_CREATE_DEFAULT_FUNC, func_name,
		 "unsupported language");
	return -1;
}

/**
 * Implementation of field_default_func_call().
 * Called by pointer to avoid linking dependencies.
 */
static int
field_default_func_call_impl(struct field_default_func *default_func,
			     const char *arg_data, uint32_t arg_size,
			     const char **ret_data, uint32_t *ret_size)
{
	struct port out_port, in_port;
	port_c_create(&in_port);
	if (arg_data != NULL)
		port_c_add_mp(&in_port, arg_data, arg_data + arg_size);

	int rc = func_call_no_access_check(default_func->holder.func,
					   &in_port, &out_port);
	port_destroy(&in_port);
	if (rc != 0) {
		diag_set(ClientError, ER_DEFAULT_FUNC_FAILED,
			 default_func->holder.func->def->name,
			 diag_last_error(diag_get())->errmsg);
		return -1;
	}

	*ret_data = port_get_msgpack(&out_port, ret_size);
	port_destroy(&out_port);
	assert(mp_typeof(**ret_data) == MP_ARRAY);
	unsigned ret_count = mp_decode_array(ret_data);
	if (ret_count != 1) {
		diag_set(ClientError, ER_DEFAULT_FUNC_FAILED,
			 default_func->holder.func->def->name,
			 tt_sprintf("expected 1 return value, got %u",
				    ret_count));
		return -1;
	}
	*ret_size -= mp_sizeof_array(ret_count);
	return 0;
}

/**
 * Implementation of field_default_func_destroy().
 * Called by pointer to avoid linking dependencies.
 */
static void
field_default_func_destroy_impl(struct field_default_func *default_func)
{
	field_default_func_unpin(default_func);
}

void
field_default_func_unpin(struct field_default_func *default_func)
{
	if (default_func->holder.func != NULL)
		func_unpin(&default_func->holder);
}

void
field_default_func_pin(struct field_default_func *default_func)
{
	struct func *func = func_by_id(default_func->id);
	assert(func != NULL);
	func_pin(func, &default_func->holder, FUNC_HOLDER_FIELD_DEFAULT);
}

int
field_default_func_init(struct field_default_func *default_func)
{
	struct func *func = func_by_id(default_func->id);
	if (func == NULL) {
		diag_set(ClientError, ER_NO_SUCH_FUNCTION,
			 int2str(default_func->id));
	}
	if (func == NULL && recovery_state <= INITIAL_RECOVERY) {
		/*
		 * That's an initial recovery and func space is not loaded yet,
		 * we have to leave it and return to it after.
		 */
		return 0;
	}
	if (func == NULL || func_access_check(func) != 0 ||
	    field_default_func_verify(func) != 0) {
		default_func->holder.func = NULL;
		return -1;
	}
	func_pin(func, &default_func->holder, FUNC_HOLDER_FIELD_DEFAULT);
	default_func->call = field_default_func_call_impl;
	default_func->destroy = field_default_func_destroy_impl;
	return 0;
}
