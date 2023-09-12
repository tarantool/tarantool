/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */

#pragma once

#include "trivia/util.h"

#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct mh_strnu32_t;

/**
 * Base context for MsgPack encoding/decoding.
 */
struct mp_ctx {
	/**
	 * If a  first-level `MP_MAP` key has `MP_STRING` type, the key is
	 * looked up and replaced with a translation, if found. The translation
	 * table must use `lua_hash` as the hash function.
	 *
	 * Can be `NULL`.
	 */
	struct mh_strnu32_t *translation;
	/**
	 * Implementation dependent content. Needed to declare an abstract
	 * MsgPack context instance on stack.
	 */
	char padding[80];
};

static inline void
mp_ctx_create(struct mp_ctx *ctx, struct mh_strnu32_t *translation)
{
	ctx->translation = translation;
}

static inline void
mp_ctx_destroy(struct mp_ctx *ctx)
{
	TRASH(ctx);
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
