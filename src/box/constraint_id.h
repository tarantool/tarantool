#pragma once

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
#if defined(__cplusplus)
extern "C" {
#endif

enum constraint_type {
	CONSTRAINT_TYPE_PK = 0,
	CONSTRAINT_TYPE_UNIQUE,
	CONSTRAINT_TYPE_FK,
	CONSTRAINT_TYPE_CK,
	constraint_type_MAX,
};

extern const char *constraint_type_strs[];

struct constraint_id {
	/** Constraint type. */
	enum constraint_type type;
	/** Zero-terminated string with name. */
	char name[0];
};

/** Allocate memory and construct constraint id. */
struct constraint_id *
constraint_id_new(enum constraint_type type, const char *name);

/** Free memory of constraint id. */
void
constraint_id_delete(struct constraint_id *id);

#if defined(__cplusplus)
}
#endif
