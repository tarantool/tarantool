/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "coll_id.h"
#include "coll_id_def.h"
#include "coll.h"
#include "error.h"
#include "diag.h"

struct coll_id *
coll_id_new(const struct coll_id_def *def)
{
	assert(def->base.type == COLL_TYPE_ICU);
	size_t total_len = sizeof(struct coll_id) + def->name_len + 1;
	struct coll_id *coll_id = (struct coll_id *) malloc(total_len);
	if (coll_id == NULL) {
		diag_set(OutOfMemory, total_len, "malloc", "coll_id");
		return NULL;
	}
	coll_id->coll = coll_new(&def->base);
	if (coll_id->coll == NULL) {
		free(coll_id);
		return NULL;
	}
	coll_id->id = def->id;
	coll_id->owner_id = def->owner_id;
	coll_id->name_len = def->name_len;
	memcpy(coll_id->name, def->name, def->name_len);
	coll_id->name[coll_id->name_len] = 0;
	return coll_id;
}

void
coll_id_delete(struct coll_id *coll_id)
{
	coll_unref(coll_id->coll);
	free(coll_id);
}
