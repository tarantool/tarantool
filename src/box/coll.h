#ifndef TARANTOOL_BOX_COLL_H_INCLUDED
#define TARANTOOL_BOX_COLL_H_INCLUDED
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

#include "coll_def.h"
#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct coll;

typedef int (*coll_cmp_f)(const char *s, size_t s_len,
			  const char *t, size_t t_len,
			  const struct coll *coll);

typedef uint32_t (*coll_hash_f)(const char *s, size_t s_len,
				uint32_t *ph, uint32_t *pcarry,
				struct coll *coll);

/**
 * ICU collation specific data.
 */
struct UCollator;

struct coll_icu {
	struct UCollator *collator;
};

/**
 * A collation.
 */
struct coll {
	/** Personal ID */
	uint32_t id;
	/** Owner ID */
	uint32_t owner_id;
	/** Collation type. */
	enum coll_type type;
	/** Type specific data. */
	struct coll_icu icu;
	/** String comparator. */
	coll_cmp_f cmp;
	coll_hash_f hash;
	/** Collation name. */
	size_t name_len;
	char name[0];
};

/**
 * Return true, if a collation is case sensitive.
 * @param coll Collation to check.
 * @retval Case sensitivity.
 */
bool
coll_is_case_sensitive(const struct coll *coll);

/**
 * Create a collation by definition.
 * @param def - collation definition.
 * @return - the collation OR NULL on memory error (diag is set).
 */
struct coll *
coll_new(const struct coll_def *def);

/**
 * Delete a collation.
 * @param cool - collation to delete.
 */
void
coll_delete(struct coll *coll);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_COLL_H_INCLUDED */
