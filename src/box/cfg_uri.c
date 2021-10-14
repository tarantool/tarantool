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
#include "cfg_uri.h"
#include "tt_static.h"
#include "lua/utils.h"
#include "diag.h"
#include "box/error.h"
#include "box/errcode.h"
#include "trivia/util.h"

enum {
	MAX_OPT_NAME_LEN = 256,
};

struct cfg_uri_array {
	const char *uri;
};

static int
cfg_get_uri_array(struct lua_State *L, const char *param)
{
	const char *buf =
		tt_snprintf(MAX_OPT_NAME_LEN,
			    "return box.internal.cfg_get_%s(box.cfg.%s)",
			    param, param);
	if (luaL_dostring(L, buf) != 0)
		return -1;
	return 0;
}

static void
cfg_uri_array_delete(struct cfg_uri_array *uri_array)
{
	free(uri_array);
}

static struct cfg_uri_array *
cfg_uri_array_new(struct lua_State *L, const char *option_name)
{
	struct cfg_uri_array *uri_array =
		xcalloc(1, sizeof(struct cfg_uri_array));
	if (cfg_get_uri_array(L, option_name) != 0)
		goto fail;
	uri_array->uri = lua_tostring(L, -1);
	lua_pop(L, 1);
	return uri_array;
fail:
	cfg_uri_array_delete(uri_array);
	return NULL;
}

static int
cfg_uri_array_size(const struct cfg_uri_array *uri_array)
{
	return uri_array->uri != NULL ? 1 : 0;
}

static const char *
cfg_uri_array_get_uri(const struct cfg_uri_array *uri_array, int idx)
{
	(void)idx;
	return uri_array->uri;
}

static int
cfg_uri_array_check_uri(const struct cfg_uri_array *uri_array,
			int (*check_uri)(const char *, const char *),
			const char *option_name)
{
	return check_uri(uri_array->uri, option_name);
}

static struct cfg_uri_array_vtab default_cfg_uri_array_vtab = {
	/* .cfg_uri_array_new = */ cfg_uri_array_new,
	/* .cfg_uri_array_delete = */ cfg_uri_array_delete,
	/* .cfg_uri_array_size = */ cfg_uri_array_size,
	/* .cfg_uri_array_get_uri = */ cfg_uri_array_get_uri,
	/* .cfg_uri_array_check_uri = */ cfg_uri_array_check_uri,
};
struct cfg_uri_array_vtab *cfg_uri_array_vtab_ptr = &default_cfg_uri_array_vtab;
