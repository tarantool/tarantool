#pragma once
/*
 * Copyright 2020, Tarantool AUTHORS, please see AUTHORS file.
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
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

struct lua_State;
struct sql_parsed_ast;
struct sql_stmt;
struct Select;
struct ibuf;
struct Parse;

void
box_lua_sqlparser_init(struct lua_State *L);

int
lbox_sqlparser_serialize(struct lua_State *L);

int
lbox_sqlparser_deserialize(struct lua_State *L);

struct sql_parsed_ast *
luaT_check_sql_parsed_ast(struct lua_State *L, int idx);

void
luaT_push_sql_parsed_ast(struct lua_State *L, struct sql_parsed_ast *ast);

struct sql_stmt *
luaT_check_sql_stmt(struct lua_State *L, int idx);

void
luaT_push_sql_stmt(struct lua_State *L, struct sql_stmt *stmt);

int
sql_stmt_parse(const char *zSql, struct sql_stmt **ppStmt, struct sql_parsed_ast *ast);

int
sql_parser_ast_execute(struct lua_State *L,
		       struct sql_parsed_ast *ast,
		       struct sql_stmt *stmt);

void
sqlparser_generate_msgpack_walker(struct Parse *parser,
				  struct ibuf *ibuf,
				  struct Select *p);

int
sqlparser_msgpack_decode_string(struct lua_State *L, bool check);

// to avoid session.h inclusion
extern uint32_t default_flags;

#if defined(__cplusplus)
}
#endif
