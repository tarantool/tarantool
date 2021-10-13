/*
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
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
#include "tarantool_ee.h"
#include "tt_static.h"
#include "lua/utils.h"
#include "diag.h"
#include "box/error.h"
#include "box/errcode.h"
#include "trivia/util.h"

struct cfg_uri_array {
	const char *uri;
	int size;
};

static void
cfg_get_uri_array(struct lua_State *L, const char *param)
{
	const char *buf =
		tt_snprintf(MAX_OPT_NAME_LEN,
			    "return box.internal.cfg_get_%s(box.cfg.%s)",
			    param, param);
	if (luaL_dostring(L, buf) != 0)
		panic("cfg_get_uri_array('%s')", param);
}


static struct cfg_uri_array *
cfg_uri_array_new_impl(void)
{
	return xcalloc(1, sizeof(struct cfg_uri_array));
}
cfg_uri_array_new_ptr cfg_uri_array_new = cfg_uri_array_new_impl;

static void
cfg_uri_array_delete_impl(struct cfg_uri_array *uri_array)
{
	free(uri_array);
}
cfg_uri_array_delete_ptr cfg_uri_array_delete = cfg_uri_array_delete_impl;

static int
cfg_uri_array_create_impl(lua_State *L, const char *option_name,
			  struct cfg_uri_array *uri_array)
{
	memset(uri_array, 0, sizeof(*uri_array));
	cfg_get_uri_array(L, option_name);
	if (!lua_isstring(L, -1)) {
		if (!lua_isnil(L, -1)) {
			diag_set(ClientError, ER_CFG, option_name,
				 "should be a string");
			return -1;
		}
		lua_pop(L, 1);
		return 0;
	}
	uri_array->uri = lua_tostring(L, -1);
	uri_array->size = 1;
	lua_pop(L, 1);
	return 0;
}
cfg_uri_array_create_ptr cfg_uri_array_create = cfg_uri_array_create_impl;

static void
cfg_uri_array_destroy_impl(struct cfg_uri_array *uri_array)
{
	(void)uri_array;
}
cfg_uri_array_destroy_ptr cfg_uri_array_destroy = cfg_uri_array_destroy_impl;

static int
cfg_uri_array_size_impl(const struct cfg_uri_array *uri_array)
{
	return uri_array->size;
}
cfg_uri_array_size_ptr cfg_uri_array_size = cfg_uri_array_size_impl;

static const char *
cfg_uri_array_get_uri_impl(const struct cfg_uri_array *uri_array, int idx)
{
	assert(idx < uri_array->size);
	(void)idx;
	return uri_array->uri;
}
cfg_uri_array_get_uri_ptr cfg_uri_array_get_uri = cfg_uri_array_get_uri_impl;

static int
cfg_uri_array_check_impl(const struct cfg_uri_array *uri_array,
			 cfg_uri_array_checker checker,
			 const char *option_name)
{
	return checker(uri_array->uri, option_name);
}
cfg_uri_array_check_ptr cfg_uri_array_check = cfg_uri_array_check_impl;
