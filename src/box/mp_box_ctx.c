/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "box/mp_box_ctx.h"

void
mp_box_ctx_destroy(struct mp_ctx *ctx)
{
	tuple_format_map_destroy(&mp_box_ctx_check(ctx)->tuple_format_map);
}

void
mp_box_ctx_move(struct mp_ctx *dst, struct mp_ctx *src)
{
	/* Destination is not necessarily an mp_box_ctx instance. */
	struct mp_box_ctx *dst_box = (struct mp_box_ctx *)dst;
	tuple_format_map_move(&dst_box->tuple_format_map,
			      &mp_box_ctx_check(src)->tuple_format_map);
	/*
	 * Move base mp_ctx after format map to be able to check virtual
	 * methods of src.
	 */
	mp_ctx_move_default(dst, src);
}

void
mp_box_ctx_copy(struct mp_ctx *dst, struct mp_ctx *src)
{
	(void)dst;
	(void)src;
	unreachable();
}
