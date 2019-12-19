#ifndef INCLUDES_TARANTOOL_LUA_EXECUTE_H
#define INCLUDES_TARANTOOL_LUA_EXECUTE_H
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
#include <stdbool.h>

struct port;
struct sql_bind;
struct lua_State;

/**
 * Dump data from port to Lua stack. Data in port contains tuples,
 * metadata, or information obtained from an executed SQL query.
 *
 * @param port Port that contains SQL response.
 * @param L Lua stack.
 */
void
port_sql_dump_lua(struct port *port, struct lua_State *L, bool is_flat);

/**
 * Parse Lua table of SQL parameters.
 *
 * @param L Lua stack contains table with parameters. Each
 *        parameter either must have scalar type, or must be a
 *        single-row table with the following format:
 *        table[name] = value. Name - string name of the named
 *        parameter, value - scalar value of the parameter.
 *        Named and positioned parameters can be mixed.
 * @param[out] out_bind Pointer to save decoded parameters.
 * @param idx Position of table with parameters on Lua stack.
 *
 * @retval  >= 0 Number of decoded parameters.
 * @retval -1 Client or memory error.
 */
int
lua_sql_bind_list_decode(struct lua_State *L, struct sql_bind **out_bind,
			 int idx);

void
box_lua_sql_init(struct lua_State *L);

#endif /* INCLUDES_TARANTOOL_LUA_EXECUTE_H */
