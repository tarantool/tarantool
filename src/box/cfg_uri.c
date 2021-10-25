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
#include "say.h"
#include "diag.h"
#include "error.h"
#include "trivia/util.h"

static const char *valid_options[CFG_URI_OPTION_MAX] = {
	/* CFG_URI_OPTION_BACKLOG */   "backlog",
	/* CFG_URI_OPTION_READAHEAD */ "readahead",
	/* CFG_URI_OPTION_TRANSPORT */ "transport",
};

/**
 * Returns URI option index by @a option_name. If the option name
 * is not in the registry, returns 'CFG_URI_OPTION_MAX'.
 */
static enum cfg_uri_options
cfg_uri_option_idx_from_name(const char *option_name)
{
	for (enum cfg_uri_options i = 0; i < CFG_URI_OPTION_MAX; i++)
		if (strcmp(option_name, valid_options[i]) == 0)
			return i;
	return CFG_URI_OPTION_MAX;
}

/**
 * Splits @a source string by ';' and adds new option values to
 * array in @option structure. Expected input:
 * "val1;val2;val3" -- string separated by ';'.
 */
static int
cfg_uri_option_values_from_string(struct cfg_uri_option *option,
				  const char *source, const char *cfg_option)
{
	unsigned int size = (option->values_copy_size + 1) * sizeof(char *);
	option->values_copy = xrealloc(option->values_copy, size);
	option->values_copy[option->values_copy_size++] = xstrdup(source);
	char *source_copy = option->values_copy[option->values_copy_size - 1];
	char *delim = strchr(source_copy, ';');
	bool found_next;
	do {
		char *value = source_copy;
		if (delim != NULL) {
			source_copy = delim + 1;
			*delim = '\0';
			delim = strchr(source_copy, ';');
			found_next = true;
		} else {
			found_next = false;
		}
		if (strlen(source_copy) == 0 || strlen(value) == 0) {
			diag_set(ClientError, ER_CFG, cfg_option,
				 "not found option value for URI");
			return -1;
		}
		size = (option->size + 1) * sizeof(char *);
		option->values = xrealloc(option->values, size);
		option->values[option->size++] = value;
	} while (found_next);
	return 0;
}

/**
 * Adds new option values to array in @option structure. Expected
 * table with option values at the top of lua stack. Each item in
 * this table should be a string which contains option value or
 * several option values separated by ";". For example:
 * {"10", 10;20;30", "40;50;60"}
 */
static int
cfg_uri_option_values_from_table(struct cfg_uri_option *option,
				 lua_State *L, const char *cfg_option)
{
	int size = lua_objlen(L, -1);
	for (int i = 0; i < size; i++) {
		lua_rawgeti(L, -1, i + 1);
		if (!lua_isstring(L, -1)) {
			diag_set(ClientError, ER_CFG, cfg_option,
				 "URI option value"
				 "should be one of types string, numer");
			lua_pop(L, 1);
			return -1;
		}
		const char *source = lua_tostring(L, -1);
		if (cfg_uri_option_values_from_string(option, source,
						      cfg_option) != 0) {
			lua_pop(L, 1);
			return -1;
		}
		lua_pop(L, 1);
	}
	return 0;
}

/**
 * Destroys option and all associated resources.
 */
static void
cfg_uri_option_destroy(struct cfg_uri_option *option)
{
	for (int i = 0; i < option->values_copy_size; i++)
		free(option->values_copy[i]);
	free(option->values);
	free(option->copy);
	memset(option, 0, sizeof(struct cfg_uri_option));
}

/**
 * Creates URI option at appropriate position in @a options array
 * from @source string. Expected @a source format is a string which
 * contains option name and option values separated by '=':
 * "backlog="10;20;30"
 */
static int
cfg_uri_option_create_from_string(struct cfg_uri_option *options,
				  const char *source,
				  const char *cfg_option)
{
	char *copy = xstrdup(source);
	char *delim = strchr(copy, '=');
	if (delim == NULL) {
		diag_set(ClientError, ER_CFG, cfg_option,
			 "not found option value for URI");
		free(copy);
		return -1;
	}
	char *values = delim + 1;
	*delim = '\0';
	enum cfg_uri_options opt_idx =
		cfg_uri_option_idx_from_name(copy);
	if (opt_idx == CFG_URI_OPTION_MAX) {
		diag_set(ClientError, ER_CFG, cfg_option,
			 "invalid option name for URI");
		free(copy);
		return -1;
	}
	struct cfg_uri_option *option = &options[opt_idx];
	option->copy = copy;
	option->name = option->copy;
	if (strlen(values) == 0) {
		diag_set(ClientError, ER_CFG, cfg_option,
			 "not found option value for URI after '='");
		goto fail;
	}
	if (cfg_uri_option_values_from_string(option, values,
					      cfg_option) != 0)
		goto fail;
	return 0;
fail:
	cfg_uri_option_destroy(option);
	return -1;
}

/**
 * Destroys all URI options in @a cfg_uri.
 */
static void
cfg_uri_options_destroy(struct cfg_uri *cfg_uri)
{
	for (int i = 0; i < CFG_URI_OPTION_MAX; i++)
		cfg_uri_option_destroy(&cfg_uri->options[i]);
}

/**
 * Creates URI options from @a source string. Expected @a source format
 * is a string which contains options separated by '&'. For example:
 * "backlog=10;20;30&transport=tls;plain".
 * User can several values for one option separated by '&'. For this
 * case he should use same syntax:
 * "backlog=10;20&backlog=30;40"
 */
static int
cfg_uri_options_create_from_string(struct cfg_uri *cfg_uri, const char *source,
				   const char *cfg_option)
{
	cfg_uri->options_copy = xstrdup(source);
	char *options_copy = cfg_uri->options_copy;
	char *delim = strchr(options_copy, '&');
	bool found_next;
	do {
		char *option = options_copy;
		if (delim != NULL) {
			options_copy = delim + 1;
			*delim = '\0';
			delim = strchr(options_copy, '&');
			found_next = true;
		} else {
			found_next = false;
		}
		if (strlen(options_copy) == 0 || strlen(option) == 0) {
			diag_set(ClientError, ER_CFG, cfg_option,
				 "not found option for URI after '&'");
			goto fail;
		}
		if (cfg_uri_option_create_from_string(cfg_uri->options,
						      option, cfg_option) != 0)
			goto fail;
	} while (found_next);
	return 0;
fail:
	cfg_uri_options_destroy(cfg_uri);
	return -1;
}

/**
 * Creates URI options from the table which located at the top position of
 * lua stack. Ignores options with names out of option registry. For example:
 * { backlog="10;20;30", transport="tls;plain", unexpected = "unexpected" }.
 * In this case only backlog and transport options will be processed.
 */
static int
cfg_uri_options_create_from_table(struct cfg_uri *cfg_uri, lua_State *L,
				  const char *cfg_option)
{
	for (enum cfg_uri_options i = 0; i < lengthof(valid_options); i++) {
		lua_pushstring(L, valid_options[i]);
		lua_gettable(L, -2);
		if (lua_isstring(L, -1)) {
			const char *source = lua_tostring(L, -1);
			char *copy = xstrdup(source);
			struct cfg_uri_option *option =
				&cfg_uri->options[i];
			option->copy = copy;
			if (cfg_uri_option_values_from_string(option,
							      option->copy,
							      cfg_option) != 0)
				goto fail;
		} else if (lua_istable(L, -1)) {
			struct cfg_uri_option *option =
				&cfg_uri->options[i];
			if (cfg_uri_option_values_from_table(option, L,
							     cfg_option) != 0)
				goto fail;
		} else if (!lua_isnil(L ,-1)) {
			diag_set(ClientError, ER_CFG, cfg_option, "common URI option"
				 "should be one of types string, table");
			lua_pop(L, 1);
			goto fail;
		}
		lua_pop(L, 1);
	}
	return 0;
fail:
	lua_pop(L, 1);
	cfg_uri_options_destroy(cfg_uri);
	return -1;
}

/**
 * Destroys @a cfg_uri and all associated resources.
 */
static void
cfg_uri_destroy(struct cfg_uri *cfg_uri)
{
	cfg_uri_options_destroy(cfg_uri);
	free(cfg_uri->options_copy);
	memset(cfg_uri, 0, sizeof(struct cfg_uri));
}

/**
 * Creates @a cfg_uri from string @a source. Expected format of
 * @a source string: "uri?query", where query contains options
 * separated by '&'. See 'cfg_uri_options_create_from_string'
 * for details.
 */
static int
cfg_uri_create_from_string(struct cfg_uri *cfg_uri, char *source,
			   const char *cfg_option)
{
	cfg_uri->uri = source;
	char *delim = strchr(source, '?');
	if (delim == NULL)
		return 0;
	char *query = delim + 1;
	if (strlen(query) == 0) {
		diag_set(ClientError, ER_CFG, cfg_option,
			 "not found query for URI after '?'");
		return -1;
	}
	*delim = '\0';
	if (cfg_uri_options_create_from_string(cfg_uri, query, cfg_option) != 0)
		goto fail;
	return 0;
fail:
	cfg_uri_destroy(cfg_uri);
	return -1;
}

/**
 * Creates 'cfg_uri_array' struct @a array from string, which located at the top
 * of lua stack. String should contains one URI or several URIs, separated by commas.
 * URIs format should be appropriate for 'cfg_uri_create_from_string' function.
 */
static int
cfg_uri_array_create_from_string(struct cfg_uri_array *array,
				 struct lua_State *L, const char *cfg_option)
{
	const char *source = lua_tostring(L, -1);
	unsigned int size = (++array->copies_size) * sizeof(char *);
	int pos = array->copies_size - 1;
	array->copies = (char **)xrealloc(array->copies, size);
	array->copies[pos] = xstrdup(source);
	char *saveptr, *token, *uri;
	for (uri = array->copies[pos]; ; array->size++, uri = NULL) {
		token = strtok_r(uri, ", ", &saveptr);
		if (token == NULL)
			break;
		size = (array->size + 1) * sizeof(struct cfg_uri);
		array->uris = xrealloc(array->uris, size);
		memset(&(array->uris[array->size]), 0, sizeof(struct cfg_uri));
		if (cfg_uri_create_from_string(&array->uris[array->size],
					       token, cfg_option) != 0)
			goto fail;
	}
	return 0;
fail:
	cfg_uri_array_destroy(array);
	return -1;
}

/**
 * Creates 'cfg_uri_array' struct @a array from table, which located at the top
 * of lua stack. Table can contains URIs in string or table format and options
 * which are common to all URIs in this table.
 */
static int
cfg_uri_array_create_from_table(struct cfg_uri_array *array,
				struct lua_State *L, const char *cfg_option)
{
	lua_pushstring(L, "options");
	lua_gettable(L, -2);
	if (lua_isstring(L, -1)) {
		const char *source = lua_tostring(L, -1);
		struct cfg_uri *opt_storage = &array->common_options_storage;
		if (cfg_uri_options_create_from_string(opt_storage, source,
						       cfg_option) != 0)
			goto fail;
	} else if (lua_istable(L, -1)) {
		struct cfg_uri *opt_storage = &array->common_options_storage;
		if (cfg_uri_options_create_from_table(opt_storage, L,
						      cfg_option) != 0)
			goto fail;
	} else if (!lua_isnil(L ,-1)) {
		diag_set(ClientError, ER_CFG, cfg_option, "common URI options"
			 "should be one of types string, table");
		goto fail;
	}
	lua_pop(L, 1);
	int size = lua_objlen(L, -1);
	for (int i = 0; i < size; i++) {
		lua_rawgeti(L, -1, i + 1);
		if (lua_isstring(L, -1)) {
			if (cfg_uri_array_create_from_string(array, L,
							     cfg_option) != 0)
				goto fail;
		} else if (lua_istable(L, -1)) {
			/** TODO */
		} else {
			diag_set(ClientError, ER_CFG, cfg_option,
				 "URI should be one of types string, table");
			goto fail;
		}
		lua_pop(L, 1);
	}
	return 0;
fail:
	lua_pop(L, 1);
	cfg_uri_array_destroy(array);
	return -1;
}

int
cfg_uri_array_create(struct cfg_uri_array *array, struct lua_State *L,
		     const char *cfg_option)
{
	memset(array, 0, sizeof(struct cfg_uri_array));
	int rc = 0;
	if (lua_isnil(L, -1)) {
		array->size = 0;
	} else if (lua_isstring(L, -1)) {
		rc = cfg_uri_array_create_from_string(array, L, cfg_option);
	} else if (lua_istable(L, -1)) {
		rc = cfg_uri_array_create_from_table(array, L, cfg_option);
	} else {
		diag_set(ClientError, ER_CFG, cfg_option,
			 "should be one of types string, number, table");
		rc = -1;
	}
	return rc;
}

void
cfg_uri_array_destroy(struct cfg_uri_array *array)
{
	cfg_uri_destroy(&array->common_options_storage);
	for (int i = 0; i < array->size; i++)
		cfg_uri_destroy(&array->uris[i]);
	free(array->uris);
	for (int i = 0; i < array->copies_size; i++)
		free(array->copies[i]);
	memset(array, 0, sizeof(struct cfg_uri_array));
}