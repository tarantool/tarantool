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

const char *func_language_strs[] = {"LUA", "C", "SQL", "SQL_BUILTIN"};

const char *func_aggregate_strs[] = {"none", "group"};

const struct func_opts func_opts_default = {
	/* .is_multikey = */ false,
};

const struct opt_def func_opts_reg[] = {
	OPT_DEF("is_multikey", OPT_BOOL, struct func_opts, is_multikey),
};

int
func_opts_cmp(struct func_opts *o1, struct func_opts *o2)
{
	if (o1->is_multikey != o2->is_multikey)
		return o1->is_multikey - o2->is_multikey;
	return 0;
}

int
func_def_cmp(struct func_def *def1, struct func_def *def2)
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
	return func_opts_cmp(&def1->opts, &def2->opts);
}

/**
 * Check if a function definition is valid.
 * @retval  0 the definition is correct
 * @retval -1 the definition has incompatible options set,
 *            diagnostics message is provided
 */
int
func_def_check(struct func_def *def)
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
	case FUNC_LANGUAGE_SQL_BUILTIN:
		if (def->body != NULL || def->is_sandboxed) {
			diag_set(ClientError, ER_CREATE_FUNCTION, def->name,
				 "body and is_sandboxed options are not compatible "
				 "with SQL language");
			return -1;
		}
		break;
	default:
		break;
	}
	return 0;
}
