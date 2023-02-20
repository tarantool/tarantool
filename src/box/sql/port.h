/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once
#include "box/port.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct Vdbe;

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

/** Methods of struct port_sql. */
extern const struct port_vtab port_sql_vtab;

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
	struct Vdbe *stmt;
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

static_assert(sizeof(struct port_sql) <= sizeof(struct port),
	      "sizeof(struct port_sql) must be <= sizeof(struct port)");

void
port_sql_create(struct port *port, struct Vdbe *stmt,
		enum sql_serialization_format format, bool do_finalize);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined __cplusplus */
