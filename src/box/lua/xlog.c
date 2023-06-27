/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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

#include "xlog.h"

#include <ctype.h>

#include <say.h>
#include <diag.h>
#include <msgpuck/msgpuck.h>

#include <box/error.h>
#include <box/xlog.h>
#include <box/iproto_constants.h>
#include <box/tuple.h>
#include <box/lua/tuple.h>
#include <lua/msgpack.h>
#include <lua/utils.h>

/* {{{ Helpers */

static uint32_t CTID_STRUCT_XLOG_CURSOR_REF = 0;

static int
lbox_pushcursor(struct lua_State *L, struct xlog_cursor *cur)
{
	struct xlog_cursor **pcur = NULL;
	pcur = (struct xlog_cursor **)luaL_pushcdata(L,
			CTID_STRUCT_XLOG_CURSOR_REF);
	*pcur = cur;
	return 1;
}

static struct xlog_cursor *
lbox_checkcursor(struct lua_State *L, int narg, const char *src)
{
	uint32_t ctypeid;
	void *data = NULL;
	data = (struct xlog_cursor *)luaL_checkcdata(L, narg, &ctypeid);
	assert(ctypeid == CTID_STRUCT_XLOG_CURSOR_REF);
	if (ctypeid != (uint32_t )CTID_STRUCT_XLOG_CURSOR_REF)
		luaL_error(L, "%s: expecting xlog_cursor object", src);
	return *(struct xlog_cursor **)data;
}

/* }}} */

/* {{{ Xlog Parser */

/**
 * Converts xlog key names to lower case, for example:
 * "SPACE_ID" => "space_id"
 */
static void
lbox_xlog_pushkey(lua_State *L, const char *key)
{
	luaL_Buffer b;
	luaL_buffinit(L, &b);
	for (const char *pos = key; *pos; pos++)
		luaL_addchar(&b, tolower(*pos));
	luaL_pushresult(&b);
}

/**
 * Helper function for lbox_xlog_parse_body that parses one key value pair
 * and adds it to the result table. The MsgPack data must be checked.
 */
static void
lbox_xlog_parse_body_kv(struct lua_State *L, int type, const char **beg)
{
	if (mp_typeof(**beg) != MP_UINT) {
		/* Invalid key type - ignore. */
		mp_next(beg);
		mp_next(beg);
		return;
	}
	uint32_t v = mp_decode_uint(beg);
	if (iproto_type_is_dml(type) && iproto_key_name(v)) {
		/*
		 * Historically, the xlog reader outputs IPROTO_OPS as
		 * "operations", not "ops".
		 */
		lbox_xlog_pushkey(L, (v == IPROTO_OPS ? "operations" :
				      iproto_key_name(v)));
	} else if (type == VY_INDEX_RUN_INFO && vy_run_info_key_name(v)) {
		lbox_xlog_pushkey(L, vy_run_info_key_name(v));
	} else if (type == VY_INDEX_PAGE_INFO && vy_page_info_key_name(v)) {
		lbox_xlog_pushkey(L, vy_page_info_key_name(v));
	} else if (type == VY_RUN_ROW_INDEX && vy_row_index_key_name(v)) {
		lbox_xlog_pushkey(L, vy_row_index_key_name(v));
	} else {
		lua_pushinteger(L, v); /* unknown key */
	}

	if ((v == IPROTO_KEY || v == IPROTO_TUPLE || v == IPROTO_OLD_TUPLE ||
	     v == IPROTO_NEW_TUPLE) && (mp_typeof(**beg) == MP_ARRAY)) {
		/*
		 * Push tuple if possible.
		 */
		const char *tuple_beg = *beg;
		mp_next(beg);
		struct tuple_format *format = box_tuple_format_default();
		struct tuple *tuple = box_tuple_new(format, tuple_beg, *beg);
		if (tuple == NULL)
			luaT_error(L);
		luaT_pushtuple(L, tuple);
	} else {
		/*
		 * Push Lua objects
		 */
		luamp_decode(L, luaL_msgpack_default, beg);
	}
	lua_settable(L, -3);
}

/**
 * Parses a request body and pushes it to the Lua stack.
 * The MsgPack data must be checked and represent a map.
 */
static void
lbox_xlog_parse_body(struct lua_State *L, int type, const char **beg)
{
	lua_newtable(L);
	uint32_t size = mp_decode_map(beg);
	for (uint32_t i = 0; i < size; i++)
		lbox_xlog_parse_body_kv(L, type, beg);
}

/**
 * Parses a row and pushes it along with its LSN to the Lua stack.
 * On success returns the number of values pushed (> 0). On EOF returns 0.
 */
static int
lbox_xlog_parse_row(struct lua_State *L, const char **pos, const char *end)
{
	int top = lua_gettop(L);
	const char *tmp = *pos;
	if (mp_check(&tmp, end) != 0 || mp_typeof(**pos) != MP_MAP)
		goto bad_row;
	/*
	 * Sic: The nrec argument of lua_createtable is chosen so that the
	 * output looks pretty when encoded in YAML.
	 */
	lua_createtable(L, 0, 8);
	lua_pushliteral(L, "HEADER");
	lua_createtable(L, 0, 8);
	uint64_t type = 0;
	uint64_t tsn = 0;
	uint64_t lsn = 0;
	bool has_tsn = false;
	bool is_commit = false;
	uint32_t size = mp_decode_map(pos);
	for (uint32_t i = 0; i < size; i++) {
		if (mp_typeof(**pos) != MP_UINT) {
			/* Invalid key type - ignore. */
			mp_next(pos);
			mp_next(pos);
			continue;
		}
		uint64_t key = mp_decode_uint(pos);
		const char *key_name = iproto_key_name(key);
		if (key < iproto_key_MAX &&
		    mp_typeof(**pos) != iproto_key_type[key]) {
			/* Bad value type - dump as is. */
			goto dump;
		}
		switch (key) {
		case IPROTO_REQUEST_TYPE: {
			type = mp_decode_uint(pos);
			lua_pushliteral(L, "type");
			const char *type_name = iproto_type_name(type);
			if (type_name != NULL)
				lua_pushstring(L, type_name);
			else
				luaL_pushuint64(L, type);
			lua_settable(L, -3);
			continue;
		}
		case IPROTO_FLAGS: {
			/* We're only interested in the commit flag. */
			uint64_t flags = mp_decode_uint(pos);
			if ((flags & IPROTO_FLAG_COMMIT) != 0)
				is_commit = true;
			continue;
		}
		case IPROTO_TSN:
			/*
			 * TSN is encoded as diff so we dump it after we finish
			 * parsing the header.
			 */
			tsn = mp_decode_uint(pos);
			has_tsn = true;
			continue;
		case IPROTO_LSN:
			/* Remember LSN to calculate TSN later. */
			tmp = *pos;
			lsn = mp_decode_uint(&tmp);
			break;
		default:
			break;
		}
dump:
		if (key_name != NULL)
			lbox_xlog_pushkey(L, key_name);
		else
			luaL_pushuint64(L, key);
		luamp_decode(L, luaL_msgpack_default, pos);
		lua_settable(L, -3);
	}
	/* The commit flag isn't set for single-statement transactions. */
	if (!has_tsn)
		is_commit = true;
	tsn = lsn - tsn;
	/* Show TSN and commit flag only for multi-statement transactions. */
	if (tsn != lsn || !is_commit) {
		lua_pushliteral(L, "tsn");
		luaL_pushuint64(L, tsn);
		lua_settable(L, -3);
	}
	if (is_commit && tsn != lsn) {
		lua_pushliteral(L, "commit");
		lua_pushboolean(L, true);
		lua_settable(L, -3);
	}
	lua_settable(L, -3); /* HEADER */
	if (*pos < end && type != IPROTO_NOP) {
		tmp = *pos;
		if (mp_check(&tmp, end) != 0 || mp_typeof(**pos) != MP_MAP)
			goto bad_row;
		lua_pushliteral(L, "BODY");
		lbox_xlog_parse_body(L, type, pos);
		lua_settable(L, -3); /* BODY */
	}
	luaL_pushuint64(L, lsn);
	lua_insert(L, -2);
	return 2;
bad_row:
	/* Silently assume EOF on bad row. */
	lua_settop(L, top);
	return 0;
}

static int
lbox_xlog_parser_iterate(struct lua_State *L)
{
	struct xlog_cursor *cur = lbox_checkcursor(L, 1, "xlog:pairs()");

	int rc = 0;
	/* skip all bad read requests */
	while (true) {
		const char **data;
		const char *end;
		rc = xlog_cursor_next_row_raw(cur, &data, &end);
		assert(rc >= 0);
		if (rc == 0) {
			rc = lbox_xlog_parse_row(L, data, end);
			if (rc > 0)
				return rc;
		}
		while ((rc = xlog_cursor_next_tx(cur)) < 0) {
			struct error *e = diag_last_error(diag_get());
			if (e->type != &type_XlogError)
				luaT_error(L);
			if ((rc = xlog_cursor_find_tx_magic(cur)) < 0)
				luaT_error(L);
			if (rc == 1)
				break;
		}
		if (rc == 1)
			break;
	}
	return 0;
}

/* }}} */

static void
lbox_xlog_parser_close(struct xlog_cursor *cur) {
	if (cur == NULL)
		return;
	xlog_cursor_close(cur, false);
	free(cur);
}

static int
lbox_xlog_parser_gc(struct lua_State *L)
{
	struct xlog_cursor *cur = lbox_checkcursor(L, 1, "xlog:gc()");
	lbox_xlog_parser_close(cur);
	return 0;
}

static int
lbox_xlog_parser_open_pairs(struct lua_State *L)
{
	int args_n = lua_gettop(L);
	if (args_n != 1 || !lua_isstring(L, 1))
		luaL_error(L, "Usage: parser.open(log_filename)");

	const char *filename = luaL_checkstring(L, 1);

	/* Construct xlog cursor */
	struct xlog_cursor *cur = (struct xlog_cursor *)calloc(1,
			sizeof(struct xlog_cursor));
	if (cur == NULL) {
		diag_set(OutOfMemory, sizeof(struct xlog_cursor),
			 "malloc", "struct xlog_cursor");
		return luaT_error(L);
	}
	/* Construct xlog object */
	if (xlog_cursor_open(cur, filename) < 0) {
		return luaT_error(L);
	}
	if (strncmp(cur->meta.filetype, "SNAP", 4) != 0 &&
	    strncmp(cur->meta.filetype, "XLOG", 4) != 0 &&
	    strncmp(cur->meta.filetype, "RUN", 3) != 0 &&
	    strncmp(cur->meta.filetype, "INDEX", 5) != 0 &&
	    strncmp(cur->meta.filetype, "DATA", 4) != 0 &&
	    strncmp(cur->meta.filetype, "VYLOG", 4) != 0) {
		char buf[1024];
		snprintf(buf, sizeof(buf), "'%.*s' file type",
			 (int) strlen(cur->meta.filetype),
			 cur->meta.filetype);
		diag_set(ClientError, ER_UNSUPPORTED, "xlog reader", buf);
		xlog_cursor_close(cur, false);
		free(cur);
		return luaT_error(L);
	}
	/* push iteration function */
	lua_pushcclosure(L, &lbox_xlog_parser_iterate, 1);
	/* push log and set GC */
	lbox_pushcursor(L, cur);
	lua_pushcfunction(L, lbox_xlog_parser_gc);
	luaL_setcdatagc(L, -2);
	/* push iterator position */
	lua_pushinteger(L, 0);
	return 3;
}

static const struct luaL_Reg lbox_xlog_parser_lib [] = {
	{ "pairs",	lbox_xlog_parser_open_pairs },
	{ NULL,		NULL                        }
};

void
box_lua_xlog_init(struct lua_State *L)
{
	int rc = 0;
	/* Get CTypeIDs */
	rc = luaL_cdef(L, "struct xlog_cursor;"); assert(rc == 0); (void) rc;
	CTID_STRUCT_XLOG_CURSOR_REF = luaL_ctypeid(L, "struct xlog_cursor&");
	assert(CTID_STRUCT_XLOG_CURSOR_REF != 0);

	luaT_newmodule(L, "xlog.lib", lbox_xlog_parser_lib);

	lua_newtable(L);
	lua_setmetatable(L, -2);
	lua_pop(L, 1);
}

/* }}} */
