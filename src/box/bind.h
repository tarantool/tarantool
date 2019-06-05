#ifndef TARANTOOL_SQL_BIND_H_INCLUDED
#define TARANTOOL_SQL_BIND_H_INCLUDED
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

#if defined(__cplusplus)
extern "C" {
#endif

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "msgpuck.h"

struct sql_stmt;

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
	/** MessagePack type of the value. */
	enum mp_type type;
	/** Bind value. */
	union {
		bool b;
		double d;
		int64_t i64;
		uint64_t u64;
		/** For string or blob. */
		const char *s;
	};
};

/**
 * Return a string name of a parameter marker.
 * @param Bind to get name.
 * @retval Zero terminated name.
 */
const char *
sql_bind_name(const struct sql_bind *bind);

/**
 * Parse MessagePack array of SQL parameters.
 * @param data MessagePack array of parameters. Each parameter
 *        either must have scalar type, or must be a map with the
 *        following format: {name: value}. Name - string name of
 *        the named parameter, value - scalar value of the
 *        parameter. Named and positioned parameters can be mixed.
 * @param[out] out_bind Pointer to save decoded parameters.
 *
 * @retval  >= 0 Number of decoded parameters.
 * @retval -1 Client or memory error.
 */
int
sql_bind_list_decode(const char *data, struct sql_bind **out_bind);

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
int
sql_bind_decode(struct sql_bind *bind, int i, const char **packet);

/**
 * Bind SQL parameter value to its position.
 * @param stmt Prepared statement.
 * @param p Parameter value.
 * @param pos Ordinal bind position.
 *
 * @retval  0 Success.
 * @retval -1 SQL error.
 */
int
sql_bind_column(struct sql_stmt *stmt, const struct sql_bind *p,
		uint32_t pos);

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

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif /* TARANTOOL_SQL_BIND_H_INCLUDED */
