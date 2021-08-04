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
#include "lua/utils.h"
#include "tt_static.h"
#include "diag.h"
#include "box/error.h"
#include "box/errcode.h"

enum {
	CFG_URI_OPTION_HOST = 0,
	CFG_URI_OPTION_TRANSPORT = 1,
	CFG_URI_OPTION_MAX
};

struct cfg_uri_option {
	const char **values;
	int size;
};

struct cfg_uri {
	const char *host;
	struct cfg_uri_option transport;
};

struct cfg_uri_array {
	struct cfg_uri *uris;
	int size;
};

static int
cfg_get_uri_array(const char *param)
{
	const char *buf =
		tt_snprintf(MAX_OPT_NAME_LEN,
			    "return box.internal.cfg_get_%s(box.cfg.%s)",
			    param, param);
	if (luaL_dostring(tarantool_L, buf) != 0)
		return -1;
	return 0;
}

static int
cfg_uri_get_option(const char *name, struct cfg_uri_option *uri_option)
{
	if (lua_isnil(tarantool_L, -1))
		return 0;
	if (!lua_istable(tarantool_L, -1)) {
		diag_set(ClientError, ER_CFG, name,
			 "URI option should be a table");
		return -1;
	}
	int size = lua_objlen(tarantool_L, -1);
	if (size == 0)
		return 0;
	uri_option->values =
		(const char **)calloc(size, sizeof(char *));
	if (uri_option->values == NULL) {
		diag_set(OutOfMemory, size * sizeof(char *),
			 "calloc", "cfg_uri_option");
		return -1;
	}
	uri_option->size = size;
	for (int i = 0; i < uri_option->size; i++) {
		lua_rawgeti(tarantool_L, -1, i + 1);
		uri_option->values[i] = lua_tostring(tarantool_L, -1);
		lua_pop(tarantool_L, 1);
	}
	return 0;
}

static void
cfg_uri_destroy(struct cfg_uri *uri)
{
	free(uri->transport.values);
}

static void
cfg_uri_init(struct cfg_uri *uri)
{
	memset(uri, 0, sizeof(struct cfg_uri));
}

static int
cfg_uri_get(const char *name, struct cfg_uri *uri, int idx)
{
	const char *cfg_uri_options[CFG_URI_OPTION_MAX] = {
		/* CFG_URI_OPTION_HOST */      "uri",
		/* CFG_URI_OPTION_TRANSPORT */ "transport",
	};
	for (unsigned i = 0; i < lengthof(cfg_uri_options); i++) {
		lua_rawgeti(tarantool_L, -1, idx + 1);
		lua_pushstring(tarantool_L, cfg_uri_options[i]);
		lua_gettable(tarantool_L, -2);
		switch (i) {
		case CFG_URI_OPTION_HOST:
			if (!lua_isstring(tarantool_L, -1)) {
				diag_set(ClientError, ER_CFG, name,
					 "URI should be a string");
				goto fail;
			}
			uri->host = lua_tostring(tarantool_L, -1);
			break;
		case CFG_URI_OPTION_TRANSPORT:
			if (cfg_uri_get_option(name, &uri->transport) != 0)
				goto fail;
			break;
		default:
			unreachable();
		}
		lua_pop(tarantool_L, 2);
	}
	return 0;
fail:
	lua_pop(tarantool_L, 2);
	cfg_uri_destroy(uri);
	return -1;
}

struct cfg_uri_array *
cfg_uri_array_new(void)
{
	return xcalloc(1, sizeof(struct cfg_uri_array));
}

void
cfg_uri_array_delete(struct cfg_uri_array *uri_array)
{
	free(uri_array);
}

int
cfg_uri_array_create(const char *name, struct cfg_uri_array *uri_array)
{
	int rc = 0;
	memset(uri_array, 0, sizeof(*uri_array));
	if (cfg_get_uri_array(name))
		return -1;
	if (!lua_istable(tarantool_L, -1)) {
		if (!lua_isnil(tarantool_L, -1)) {
			diag_set(ClientError, ER_CFG, name,
				 "should be a table");
			rc = -1;
		}
		lua_pop(tarantool_L, 1);
		return rc;
	}
	int size = lua_objlen(tarantool_L, -1);
	if (size == 0) {
		diag_set(ClientError, ER_CFG, name,
			 "URI table should not be empty");
		lua_pop(tarantool_L, 1);
		return -1;
	}
	uri_array->uris = (struct cfg_uri *)calloc(size, sizeof(struct cfg_uri));
	if (uri_array->uris == NULL) {
		diag_set(OutOfMemory, size * sizeof(struct cfg_uri),
			 "calloc", "cfg_uri");
		lua_pop(tarantool_L, 1);
		return -1;
	}
	for (uri_array->size = 0; uri_array->size < size; uri_array->size++) {
		int i = uri_array->size;
		cfg_uri_init(&uri_array->uris[i]);
		rc = cfg_uri_get(name, &uri_array->uris[i], i);
		if (rc != 0)
			break;
	}
	lua_pop(tarantool_L, 1);
	if (rc != 0)
		cfg_uri_array_destroy(uri_array);
	return rc;
}

void
cfg_uri_array_destroy(struct cfg_uri_array *uri_array)
{
	for (int i = 0; i < uri_array->size; i++)
		cfg_uri_destroy(&uri_array->uris[i]);
	free(uri_array->uris);
}

int
cfg_uri_array_size(const struct cfg_uri_array *uri_array)
{
	return uri_array->size;
}

const char *
cfg_uri_array_get_uri(const struct cfg_uri_array *uri_array, int idx)
{
	assert(idx < uri_array->size);
	return uri_array->uris[idx].host;
}

int
cfg_uri_array_check(const struct cfg_uri_array *uri_array,
		    cfg_uri_array_checker checker,
		    const char *option_name)
{
	for (int i = 0; i < uri_array->size; i++) {
		if (checker(uri_array->uris[i].host, option_name) != 0)
			return -1;
	}
	return 0;
}
