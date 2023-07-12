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
#include "func_def.h"
#include "opt_def.h"
#include "string.h"
#include "diag.h"
#include "error.h"
#include "salad/grp_alloc.h"
#include "trivia/util.h"

#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

const char *func_language_strs[] = {
	"LUA", "C", "SQL", "SQL_BUILTIN", "SQL_EXPR"
};

const char *func_aggregate_strs[] = {"none", "group"};

const struct func_opts func_opts_default = {
	/* .is_multikey = */ false,
	/* .takes_raw_args = */ false,
};

const struct opt_def func_opts_reg[] = {
	OPT_DEF("is_multikey", OPT_BOOL, struct func_opts, is_multikey),
	OPT_DEF("takes_raw_args", OPT_BOOL, struct func_opts, takes_raw_args),
	OPT_END,
};

struct func_def *
func_def_new(uint32_t fid, uint32_t uid, const char *name, uint32_t name_len,
	     enum func_language language, const char *body, uint32_t body_len,
	     const char *comment, uint32_t comment_len)
{
	struct func_def *def;
	struct grp_alloc all = grp_alloc_initializer();
	grp_alloc_reserve_data(&all, sizeof(*def));
	grp_alloc_reserve_str(&all, name_len);
	if (body_len != 0)
		grp_alloc_reserve_str(&all, body_len);
	if (comment_len != 0)
		grp_alloc_reserve_str(&all, comment_len);
	grp_alloc_use(&all, xmalloc(grp_alloc_size(&all)));
	def = grp_alloc_create_data(&all, sizeof(*def));
	def->name = grp_alloc_create_str(&all, name, name_len);
	def->name_len = name_len;
	def->body = body_len == 0 ? NULL :
		    grp_alloc_create_str(&all, body, body_len);
	def->comment = comment_len == 0 ? NULL :
		       grp_alloc_create_str(&all, comment, comment_len);
	assert(grp_alloc_size(&all) == 0);
	def->fid = fid;
	def->uid = uid;
	def->setuid = false;
	def->is_deterministic = false;
	def->is_sandboxed = false;
	def->param_count = 0;
	def->returns = FIELD_TYPE_ANY;
	def->aggregate = FUNC_AGGREGATE_NONE;
	def->language = language;
	def->exports.all = 0;
	def->triggers = NULL;
	func_opts_create(&def->opts);
	return def;
}

void
func_def_delete(struct func_def *def)
{
	free(def);
}

static int
func_opts_cmp(const struct func_opts *o1, const struct func_opts *o2)
{
	if (o1->is_multikey != o2->is_multikey)
		return o1->is_multikey - o2->is_multikey;
	if (o1->takes_raw_args != o2->takes_raw_args)
		return o1->takes_raw_args - o2->takes_raw_args;
	return 0;
}

int
func_def_cmp(const struct func_def *def1, const struct func_def *def2)
{
	if (def1->fid != def2->fid)
		return def1->fid - def2->fid;
	if (def1->uid != def2->uid)
		return def1->uid - def2->uid;
	if (def1->setuid != def2->setuid)
		return def1->setuid - def2->setuid;
	if (def1->language != def2->language)
		return def1->language - def2->language;
	if (def1->is_deterministic != def2->is_deterministic)
		return def1->is_deterministic - def2->is_deterministic;
	if (def1->is_sandboxed != def2->is_sandboxed)
		return def1->is_sandboxed - def2->is_sandboxed;
	if (strcmp(def1->name, def2->name) != 0)
		return strcmp(def1->name, def2->name);
	if ((def1->body != NULL) != (def2->body != NULL))
		return def1->body - def2->body;
	if (def1->body != NULL && strcmp(def1->body, def2->body) != 0)
		return strcmp(def1->body, def2->body);
	if (def1->returns != def2->returns)
		return def1->returns - def2->returns;
	if (def1->exports.all != def2->exports.all)
		return def1->exports.all - def2->exports.all;
	if (def1->aggregate != def2->aggregate)
		return def1->aggregate - def2->aggregate;
	if (def1->param_count != def2->param_count)
		return def1->param_count - def2->param_count;
	if ((def1->comment != NULL) != (def2->comment != NULL))
		return def1->comment - def2->comment;
	if (def1->comment != NULL && strcmp(def1->comment, def2->comment) != 0)
		return strcmp(def1->comment, def2->comment);
	/*
	 * The field is set only if underlying array is not empty. So an empty
	 * array is equivalent to the field absence (NULL) here.
	 */
	if ((def1->triggers != NULL) != (def2->triggers != NULL))
		return def1->triggers - def2->triggers;
	/* See the function description for equality definition. */
	if (def1->triggers != NULL) {
		const char *triggers1 = def1->triggers;
		const char *triggers2 = def2->triggers;
		const char **ptr1 = &triggers1;
		const char **ptr2 = &triggers2;
		uint32_t trigger_count1 = mp_decode_array(ptr1);
		uint32_t trigger_count2 = mp_decode_array(ptr2);
		assert(trigger_count1 != 0 && trigger_count2 != 0);
		if (trigger_count1 != trigger_count2)
			return trigger_count1 - trigger_count2;
		for (uint32_t i = 0; i < trigger_count1; i++) {
			uint32_t len1;
			const char *trigger1 = mp_decode_str(ptr1, &len1);
			uint32_t len2;
			const char *trigger2 = mp_decode_str(ptr2, &len2);
			if (len1 != len2)
				return len1 - len2;
			int trigger_cmp = memcmp(trigger1, trigger2, len1);
			if (trigger_cmp != 0)
				return trigger_cmp;
		}
	}
	return func_opts_cmp(&def1->opts, &def2->opts);
}

struct func_def *
func_def_dup(const struct func_def *def)
{
	struct func_def *copy = func_def_new(
		def->fid, def->uid, def->name, def->name_len, def->language,
		def->body, def->body != NULL ? strlen(def->body) : 0,
		def->comment, def->comment != NULL ? strlen(def->comment) : 0);
	copy->setuid = def->setuid;
	copy->is_deterministic = def->is_deterministic;
	copy->is_sandboxed = def->is_sandboxed;
	copy->param_count = def->param_count;
	copy->returns = def->returns;
	copy->aggregate = def->aggregate;
	copy->exports.all = def->exports.all;
	copy->opts = def->opts;
	copy->triggers = def->triggers;
	return copy;
}

/**
 * Check if a function definition is valid.
 * @retval  0 the definition is correct
 * @retval -1 the definition has incompatible options set,
 *            diagnostics message is provided
 */
int
func_def_check(const struct func_def *def)
{
	switch (def->language) {
	case FUNC_LANGUAGE_C:
		if (def->body != NULL || def->is_sandboxed) {
			diag_set(ClientError, ER_CREATE_FUNCTION, def->name,
				 "body and is_sandboxed options are not compatible "
				 "with C language");
			return -1;
		}
		break;
	case FUNC_LANGUAGE_LUA:
		if (def->is_sandboxed && def->body == NULL) {
			diag_set(ClientError, ER_CREATE_FUNCTION, def->name,
				 "is_sandboxed option may be set only for a persistent "
				 "Lua function (one with a non-empty body)");
			return -1;
		}
		break;
	default:
		break;
	}
	return 0;
}
