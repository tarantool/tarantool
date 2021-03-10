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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>

#include "trivia/config.h"
#include "trivia/util.h"
#include "say.h"
#include "errinj.h"

#define ERRINJ_MEMBER(n, t, s) { /* .name = */ #n, /* .type = */ t, /* .state = */ s },

struct errinj errinjs[errinj_id_MAX] = {
	ERRINJ_LIST(ERRINJ_MEMBER)
};

struct errinj *
errinj_by_name(char *name)
{
	for (enum errinj_id i = 0 ; i < errinj_id_MAX ; i++) {
		if (strcmp(errinjs[i].name, name) == 0)
			return &errinjs[i];
	}
	return NULL;
}

/**
 * Dump error injection states to the callback function.
 */
int errinj_foreach(errinj_cb cb, void *cb_ctx) {
	int i;
	for (i = 0 ; i < errinj_id_MAX ; i++) {
		int res = cb(&errinjs[i], cb_ctx);
		if (res != 0)
			return res;
	}
	return 0;
}

void errinj_set_with_environment_vars(void) {
	for (enum errinj_id i = 0; i < errinj_id_MAX; i++) {
		struct errinj *inj = &errinjs[i];
		const char *env_value = getenv(inj->name);
		if (env_value == NULL || *env_value == '\0')
			continue;

		if (inj->type == ERRINJ_INT) {
			char *end;
			int64_t int_value = strtoll(env_value, &end, 10);
			if (*end == '\0')
				inj->iparam = int_value;
			else
				panic("Incorrect value for integer %s: %s",
				      inj->name, env_value);
		} else if (inj->type == ERRINJ_BOOL) {
			if (strcasecmp(env_value, "false") == 0)
				inj->bparam = false;
			else if (strcasecmp(env_value, "true") == 0)
				inj->bparam = true;
			else
				panic("Incorrect value for boolean %s: %s",
				      inj->name, env_value);
		} else if (inj->type == ERRINJ_DOUBLE) {
			char *end;
			double double_value = strtod(env_value, &end);
			if (*end == '\0')
				inj->dparam = double_value;
			else
				panic("Incorrect value for double %s: %s",
				      inj->name, env_value);
		}
	}
}
