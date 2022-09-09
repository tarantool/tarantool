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
#include "box/sql/port.h"

const char *sql_info_key_strs[] = {
	"row_count",
	"autoincrement_ids",
};

/**
 * Convert sql row into a tuple and append to a port.
 * @param stmt Started prepared statement. At least one
 *        sql_step must be done.
 * @param region Runtime allocator for temporary objects.
 * @param port Port to store tuples.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
static inline int
sql_row_to_port(struct sql_stmt *stmt, struct region *region, struct port *port)
{
	uint32_t size;
	size_t svp = region_used(region);
	char *pos = sql_stmt_result_to_msgpack(stmt, &size, region);
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
static inline int
sql_execute(struct sql_stmt *stmt, struct port *port, struct region *region)
{
	int rc, column_count = sql_column_count(stmt);
	rmean_collect(rmean_box, IPROTO_EXECUTE, 1);
	if (column_count > 0) {
		/* Either ROW or DONE or ERROR. */
		while ((rc = sql_step(stmt)) == SQL_ROW) {
			if (sql_row_to_port(stmt, region, port) != 0)
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
