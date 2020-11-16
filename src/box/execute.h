#ifndef TARANTOOL_SQL_EXECUTE_H_INCLUDED
#define TARANTOOL_SQL_EXECUTE_H_INCLUDED
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

#include <stdint.h>
#include <stdbool.h>
#include "port.h"
#include "sql_ast.h"

#if defined(__cplusplus)
extern "C" {
#endif

/** Keys of IPROTO_SQL_INFO map. */
enum sql_info_key {
	SQL_INFO_ROW_COUNT = 0,
	SQL_INFO_AUTOINCREMENT_IDS = 1,
	sql_info_key_MAX,
};

/**
 * One of possible formats used to dump msgpack/Lua.
 * For details see port_sql_dump_msgpack() and port_sql_dump_lua().
 */
enum sql_serialization_format {
	DQL_EXECUTE = 0,
	DML_EXECUTE = 1,
	DQL_PREPARE = 2,
	DML_PREPARE = 3,
};

extern const char *sql_info_key_strs[];

struct region;
struct sql_bind;

int
sql_unprepare(uint32_t stmt_id);

int
sql_execute_prepared(uint32_t query_id, const struct sql_bind *bind,
		     uint32_t bind_count, struct port *port,
		     struct region *region);

/**
 * Prepare and execute an SQL statement.
 * @param sql SQL statement.
 * @param len Length of @a sql.
 * @param bind Array of parameters.
 * @param bind_count Length of @a bind.
 * @param[out] port Port to store SQL response.
 * @param region Runtime allocator for temporary objects
 *        (columns, tuples ...).
 *
 * @retval  0 Success.
 * @retval -1 Client or memory error.
 */
int
sql_prepare_and_execute(const char *sql, int len, const struct sql_bind *bind,
			uint32_t bind_count, struct port *port,
			struct region *region);

/**
 * Port implementation that is used to store SQL responses and
 * output them to obuf or Lua. This port implementation is
 * inherited from the port_c structure. This allows us to use
 * this structure in the port_c methods instead of port_c itself.
 *
 * The methods of port_c are called via explicit access to
 * port_c_vtab just like C++ does with BaseClass::method, when
 * it is called in a child method.
 */
struct port_sql {
	/** Base port struct. To be able to use port_c methods. */
	struct port_c base;
	/* Prepared SQL statement. */
	struct sql_stmt *stmt;
	/**
	 * Serialization format depends on type of SQL query: DML or
	 * DQL; and on type of SQL request: execute or prepare.
	 */
	uint8_t serialization_format;
	/**
	 * There's no need in clean-up in case of PREPARE request:
	 * statement remains in cache and will be deleted later.
	 */
	bool do_finalize;
};

extern const struct port_vtab port_sql_vtab;

void
port_sql_destroy(struct port *base);

void
port_sql_create(struct port *port, struct sql_stmt *stmt,
		enum sql_serialization_format format, bool do_finalize);

// FIXME
int
sql_execute(struct sql_stmt *stmt, struct port *port, struct region *region);

int
sql_stmt_finalize(struct sql_stmt *stmt);

/**
 * Calculate estimated size of memory occupied by VM.
 * See sqlVdbeMakeReady() for details concerning allocated
 * memory.
 */
size_t
sql_stmt_est_size(const struct sql_stmt *stmt);

/**
 * Return string of SQL query.
 */
const char *
sql_stmt_query_str(const struct sql_stmt *stmt);

/** Return true if statement executes right now. */
int
sql_stmt_busy(const struct sql_stmt *stmt);

/**
 * Prepare (compile into VDBE byte-code) statement.
 *
 * @param sql UTF-8 encoded SQL statement.
 * @param len Length of @param sql in bytes.
 * @param port Port to store request response.
 */
int
sql_prepare(const char *sql, int len, struct port *port);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif /* TARANTOOL_SQL_EXECUTE_H_INCLUDED */
