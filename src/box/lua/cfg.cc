/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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

#include "cfg.h"

#include "exception.h"
#include <cfg.h>
#include "main.h"
#include "lua/utils.h"

#include "box/box.h"

extern "C" {
	#include <lua.h>
} // extern "C"

static int
lbox_cfg_check(struct lua_State *L)
{
	try {
		box_check_config();
	} catch (Exception *) {
		lbox_error(L);
	}
	return 0;
}

static int
lbox_cfg_load(struct lua_State *L)
{
	try {
		load_cfg();
	} catch (Exception *) {
		lbox_error(L);
	}
	return 0;
}

static int
lbox_cfg_set_listen(struct lua_State *L)
{
	try {
		box_set_listen();
	} catch (Exception *) {
		lbox_error(L);
	}
	return 0;
}

static int
lbox_cfg_set_replication_source(struct lua_State *L)
{
	try {
		box_set_replication_source();
	} catch (Exception *) {
		lbox_error(L);
	}
	return 0;
}

static int
lbox_cfg_set_log_level(struct lua_State *L)
{
	try {
		box_set_log_level();
	} catch (Exception *) {
		lbox_error(L);
	}
	return 0;
}

static int
lbox_cfg_set_readahead(struct lua_State *L)
{
	try {
		box_set_readahead();
	} catch (Exception *) {
		lbox_error(L);
	}
	return 0;
}

static int
lbox_cfg_set_io_collect_interval(struct lua_State *L)
{
	try {
		box_set_io_collect_interval();
	} catch (Exception *) {
		lbox_error(L);
	}
	return 0;
}

static int
lbox_cfg_set_too_long_threshold(struct lua_State *L)
{
	try {
		box_set_too_long_threshold();
	} catch (Exception *) {
		lbox_error(L);
	}
	return 0;
}

static int
lbox_cfg_set_snap_io_rate_limit(struct lua_State *L)
{
	try {
		box_set_snap_io_rate_limit();
	} catch (Exception *) {
		lbox_error(L);
	}
	return 0;
}

static int
lbox_cfg_set_read_only(struct lua_State *L)
{
	try {
		box_set_ro(cfg_geti("read_only") != 0);
	} catch (Exception *) {
		lbox_error(L);
	}
	return 0;
}

void
box_lua_cfg_init(struct lua_State *L)
{
	static const struct luaL_reg cfglib_internal[] = {
		{"cfg_check", lbox_cfg_check},
		{"cfg_load", lbox_cfg_load},
		{"cfg_set_listen", lbox_cfg_set_listen},
		{"cfg_set_replication_source", lbox_cfg_set_replication_source},
		{"cfg_set_log_level", lbox_cfg_set_log_level},
		{"cfg_set_readahead", lbox_cfg_set_readahead},
		{"cfg_set_io_collect_interval", lbox_cfg_set_io_collect_interval},
		{"cfg_set_too_long_threshold", lbox_cfg_set_too_long_threshold},
		{"cfg_set_snap_io_rate_limit", lbox_cfg_set_snap_io_rate_limit},
		{"cfg_set_read_only", lbox_cfg_set_read_only},
		{NULL, NULL}
	};

	luaL_register(L, "box.internal", cfglib_internal);
	lua_pop(L, 1);
}
