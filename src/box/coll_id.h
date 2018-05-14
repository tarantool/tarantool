#ifndef TARANTOOL_BOX_COLL_ID_H_INCLUDED
#define TARANTOOL_BOX_COLL_ID_H_INCLUDED
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
#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct coll_id_def;
struct coll;

/**
 * A collation identifier. It gives a name, owner and unique
 * identifier to a base collation. Multiple coll_id can reference
 * the same collation if their functional parts match.
 */
struct coll_id {
	/** Personal ID */
	uint32_t id;
	/** Owner ID */
	uint32_t owner_id;
	/** Collation object. */
	struct coll *coll;
	/** Collation name. */
	size_t name_len;
	char name[0];
};

/**
 * Create a collation identifier by definition.
 * @param def Collation definition.
 * @retval NULL Illegal parameters or memory error.
 * @retval not NULL Collation.
 */
struct coll_id *
coll_id_new(const struct coll_id_def *def);

/** Delete collation identifier, unref the basic collation. */
void
coll_id_delete(struct coll_id *coll);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_COLL_ID_H_INCLUDED */
