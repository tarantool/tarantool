/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS
 * file.
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
#include "constraint_id.h"
#include "assoc.h"
#include "errcode.h"
#include "diag.h"

const char *constraint_type_strs[] = {
	[CONSTRAINT_TYPE_PK]		= "PRIMARY KEY",
	[CONSTRAINT_TYPE_UNIQUE]	= "UNIQUE",
	[CONSTRAINT_TYPE_FK]		= "FOREIGN KEY",
	[CONSTRAINT_TYPE_CK]		= "CHECK",
};

struct constraint_id *
constraint_id_new(enum constraint_type type, const char *name)
{
	uint32_t len = strlen(name);
	uint32_t size = sizeof(struct constraint_id) + len + 1;
	struct constraint_id *ret = malloc(size);
	if (ret == NULL) {
		diag_set(OutOfMemory, size, "malloc", "ret");
		return NULL;
	}
	ret->type = type;
	memcpy(ret->name, name, len + 1);
	return ret;
}

void
constraint_id_delete(struct constraint_id *id)
{
	free(id);
}
