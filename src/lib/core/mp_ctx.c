/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "core/mp_ctx.h"

void
mp_ctx_move_default(struct mp_ctx *dst, struct mp_ctx *src)
{
	assert(dst->translation == NULL);
	assert(dst->destroy == NULL);
	assert(dst->move == mp_ctx_move_default);
	SWAP(dst->translation, src->translation);
	dst->destroy = src->destroy;
	dst->move = src->move;
}
