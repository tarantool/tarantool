/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */

#pragma once

#include "core/mp_ctx.h"

#include "box/tuple_format_map.h"

#include <assert.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Context for MsgPack encoding/decoding of box-specific types.
 */
struct mp_box_ctx {
	/** See `mp_ctx::translation`. */
	struct mh_strnu32_t *translation;
	/** See `mp_ctx::destroy`. */
	void (*destroy)(struct mp_ctx *ctx);
	/** See `mp_ctx::move`. */
	void (*move)(struct mp_ctx *dst, struct mp_ctx *src);
	/** Mapping of format identifiers to tuple formats. */
	struct tuple_format_map tuple_format_map;
};

static_assert(sizeof(struct mp_box_ctx) <= sizeof(struct mp_ctx),
	      "sizeof(struct mp_box_ctx) must be <= sizeof(struct mp_ctx)");

/**
 * 'Virtual' destructor. Must not be called directly.
 */
void
mp_box_ctx_destroy(struct mp_ctx *ctx);

/**
 * 'Virtual' move. Must not be called directly.
 */
void
mp_box_ctx_move(struct mp_ctx *dst, struct mp_ctx *src);

static inline struct mp_box_ctx *
mp_box_ctx_check(struct mp_ctx *base)
{
	assert(base->destroy == mp_box_ctx_destroy &&
	       base->move == mp_box_ctx_move);
	return (struct mp_box_ctx *)base;
}

static inline int
mp_box_ctx_create(struct mp_box_ctx *ctx, struct mh_strnu32_t *translation,
		  const char *tuple_formats)
{
	mp_ctx_create((struct mp_ctx *)ctx, translation, mp_box_ctx_destroy,
		      mp_box_ctx_move);
	if (tuple_formats == NULL) {
		tuple_format_map_create_empty(&ctx->tuple_format_map);
		return 0;
	}
	return tuple_format_map_create_from_mp(&ctx->tuple_format_map,
					       tuple_formats);
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
