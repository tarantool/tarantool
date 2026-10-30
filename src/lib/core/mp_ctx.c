/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "core/mp_ctx.h"

void
mp_ctx_move_default(struct mp_ctx *dst, struct mp_ctx *src)
{
	dst->translation = src->translation;
	src->translation = NULL;
	dst->destroy = src->destroy;
	src->destroy = NULL;
	dst->move = src->move;
	src->move = NULL;
	dst->copy = src->copy;
	src->copy = NULL;
}

void
mp_ctx_copy_default(struct mp_ctx *dst, struct mp_ctx *src)
{
	dst->translation = src->translation;
	dst->destroy = src->destroy;
	dst->move = src->move;
	dst->copy = src->copy;
}
