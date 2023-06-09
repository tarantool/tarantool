/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <stdbool.h>

#include "tuple_constraint_func.h"

#include "engine.h"
#include "func.h"
#include "space.h"
#include "func_cache.h"
#include "port.h"
#include "tuple_constraint.h"
#include "tuple.h"
#include "tt_static.h"
#include "trivia/util.h"
#include "sql.h"

/**
 * Find and verify func in func cache.
 * Return NULL if something went wrong (diag is set).
 */
struct func *
tuple_constraint_func_find(struct tuple_constraint *constr)
{
	struct func *func = func_by_id(constr->def.func.id);
	if (func == NULL)
		diag_set(ClientError, ER_CREATE_CONSTRAINT,
			 constr->def.name, constr->space->def->name,
			 tt_sprintf("constraint function '%u' "
				    "was not found by ID",
				    constr->def.func.id));
	return func;
}

/**
 * Verify function @a func comply function rules for constraint @a constr.
 * Return nonzero in case on some problems, diag is set (@a space_name is
 * used for better error message).
 */
static int
tuple_constraint_func_verify(struct tuple_constraint *constr,
			     struct func *func, bool is_field)
{
	const char *func_name = func->def->name;

	if (func->def->language == FUNC_LANGUAGE_LUA &&
	    func->def->body == NULL) {
		diag_set(ClientError, ER_CREATE_CONSTRAINT,
			 constr->def.name, constr->space->def->name,
			 tt_sprintf("constraint lua function '%s' "
				    "must have persistent body",
				    func_name));
		return -1;
	}
	if (func->def->language == FUNC_LANGUAGE_SQL_EXPR && is_field &&
	    !func_sql_expr_has_single_arg(func, NULL)) {
		diag_set(ClientError, ER_CREATE_CONSTRAINT, constr->def.name,
			 constr->space->def->name, "Number of arguments in a "
			 "SQL field constraint function is greater than one");
		constr->space = NULL;
		return -1;
	}
	if (!func->def->is_deterministic) {
		diag_set(ClientError, ER_CREATE_CONSTRAINT,
			 constr->def.name, constr->space->def->name,
			 tt_sprintf("constraint function '%s' "
				    "must be deterministic",
				    func_name));
		return -1;
	}
	return 0;
}

/**
 * Constraint check function that interpret constraint->func_ctx as a pointer
 * to struct func and call it.
 * @param constraint that is checked.
 * @param mp_data, @param mp_data_end - pointers to msgpack data (of a field
 *  or entire tuple, depending on constraint.
 * @return 0 - constraint is passed, not 0 - constraint is failed.
 */
static int
tuple_constraint_call_func(const struct tuple_constraint *constr,
			   const char *mp_data, const char *mp_data_end,
			   const struct tuple_field *field)
{
	struct port out_port, in_port;
	port_c_create(&in_port);
	if (field != NULL)
		port_c_add_mp(&in_port, mp_data, mp_data_end);
	else
		port_c_add_formatted_mp(&in_port, mp_data, mp_data_end,
					constr->space->format);
	port_c_add_str(&in_port, constr->def.name, constr->def.name_len);

	int rc = func_call_no_access_check(constr->func_cache_holder.func,
					   &in_port, &out_port);
	port_destroy(&in_port);
	if (rc == 0) {
		uint32_t ret_size;
		const char *ret = port_get_msgpack(&out_port, &ret_size);
		assert(mp_typeof(*ret) == MP_ARRAY);
		uint32_t ret_count = mp_decode_array(&ret);
		if (ret_count < 1 || mp_typeof(*ret) != MP_BOOL ||
		    mp_decode_bool(&ret) != true)
			rc = -1;
		port_destroy(&out_port);
	} else {
		diag_log();
		diag_clear(diag_get());
	}
	if (rc != 0 && field != NULL) {
		const char *field_path =
			tuple_field_path(field, constr->space->format);
		struct error *e = diag_set(ClientError,
					   ER_FIELD_CONSTRAINT_FAILED,
					   constr->def.name, field_path);
		error_set_str(e, "name", constr->def.name);
		error_set_str(e, "field_path", field_path);
		error_set_uint(e, "field_id", field->id);
	} else if (rc != 0) {
		struct error *e = diag_set(ClientError,
					   ER_TUPLE_CONSTRAINT_FAILED,
					   constr->def.name);
		error_set_str(e, "name", constr->def.name);
	}
	return rc;
}

/**
 * Unpin func from func_cache, removes check.
 */
static void
tuple_constraint_func_detach(struct tuple_constraint *constr)
{
	assert(constr->detach == tuple_constraint_func_detach);
	/* Check that constraint has not been detached yet. */
	assert(constr->check != tuple_constraint_noop_check);
	func_unpin(&constr->func_cache_holder);
	constr->check = tuple_constraint_noop_check;
}

/**
 * Pin func to func_cache, set check.
 */
static void
tuple_constraint_func_reattach(struct tuple_constraint *constr)
{
	assert(constr->reattach == tuple_constraint_func_reattach);
	/* Check that constraint has been detached. */
	assert(constr->check == tuple_constraint_noop_check);
	struct func *func = tuple_constraint_func_find(constr);
	func_pin(func, &constr->func_cache_holder, FUNC_HOLDER_CONSTRAINT);
	constr->check = tuple_constraint_call_func;
}

/**
 * Destructor. Detaches constraint if it has not been detached before and
 * deinitializes its fields.
 */
static void
tuple_constraint_func_destroy(struct tuple_constraint *constr)
{
	assert(constr->destroy == tuple_constraint_func_destroy);
	/** Detach constraint if it has not been detached before. */
	if (constr->check != tuple_constraint_noop_check)
		tuple_constraint_func_detach(constr);
	constr->detach = tuple_constraint_noop_alter;
	constr->reattach = tuple_constraint_noop_alter;
	constr->destroy = tuple_constraint_noop_alter;
	constr->space = NULL;
}

int
tuple_constraint_func_init(struct tuple_constraint *constr,
			   struct space *space, bool is_field)
{
	assert(constr->def.type == CONSTR_FUNC);
	constr->space = space;
	struct func *func = tuple_constraint_func_find(constr);
	if (func == NULL && recovery_state <= INITIAL_RECOVERY) {
		/*
		 * That's an initial recovery and func space is not loaded yet,
		 * we heave to leave it a return to it after.
		 */
		assert(constr->check == tuple_constraint_noop_check);
		return 0;
	}
	if (func == NULL || func_access_check(func) != 0 ||
	    tuple_constraint_func_verify(constr, func, is_field) != 0) {
		constr->space = NULL;
		return -1;
	}
	func_pin(func, &constr->func_cache_holder, FUNC_HOLDER_CONSTRAINT);
	constr->check = tuple_constraint_call_func;
	constr->destroy = tuple_constraint_func_destroy;
	constr->detach = tuple_constraint_func_detach;
	constr->reattach = tuple_constraint_func_reattach;
	return 0;
}
