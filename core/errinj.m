
/*
 * Copyright (C) 2011 Mail.RU
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "config.h"
#include "util.h"
#include "say.h"
#include "tbuf.h"
#include "errinj.h"

/**
 * Initialize error injection list.
 */
void
errinj_init(void)
{
	TAILQ_INIT(&errinjs);
}

/**
 * Free error injection list.
 */
void
errinj_free(void)
{
	struct errinj *inj, *injn;
	TAILQ_FOREACH_SAFE(inj, &errinjs, next, injn) {
		free(inj->name);
	}
}

static struct errinj*
errinj_match(char *name)
{
	struct errinj *inj;
	TAILQ_FOREACH(inj, &errinjs, next)
		if (strcmp(inj->name, name) == 0)
			return inj;
	return NULL;
}

/**
 * Get state of the error injection handle.
 *
 * @param name error injection name.
 *
 * @return error injection handle state on success, false on error.
 */
bool
errinj_state(char *name)
{
	struct errinj *inj = errinj_match(name);
	if (inj == NULL)
		return false;
	return inj->state;
}

/**
 * Add new error injection handle.
 *
 * @param name error injection name.
 *
 * @return error injection structure pointer on success, NULL on error.
 */
struct errinj*
errinj_add(char *name)
{
	assert(errinj_match(name) == NULL);
	struct errinj *inj = malloc(sizeof(struct errinj));
	if (inj == NULL) 
		return NULL;
	inj->name = strdup(name);
	if (inj->name == NULL) {
		free(inj);
		return NULL;
	}
	inj->state = false;
	TAILQ_INSERT_TAIL(&errinjs, inj, next);
	return inj;
}

/**
 * Set state of the error injection handle.
 *
 * @param name error injection name.
 * @param state error injection handle state.
 *
 * @return true on success, false on error.
 */
bool
errinj_set(char *name, bool state)
{
	struct errinj *inj = errinj_match(name);
	if (inj == NULL)
		return false;
	inj->state = state;
	return true;
}

/**
 * Dump error injection states to the buffer.
 *
 * @param out output buffer
 */
void
errinj_info(struct tbuf *out)
{
	tbuf_printf(out, "error injections:" CRLF);
	struct errinj *inj;
	TAILQ_FOREACH(inj, &errinjs, next) {
		tbuf_printf(out, "  - name: %s" CRLF, inj->name);
		tbuf_printf(out, "    state: %s" CRLF,
			    (inj->state) ? "on" : "off");
	}
}
