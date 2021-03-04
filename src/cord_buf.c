/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "cord_buf.h"
#include "fiber.h"

#include "small/ibuf.h"

enum {
	/* No any reason why that value. Historical constant. */
	CORD_IBUF_START_CAPACITY = 16384,
};

static struct ibuf *cord_buf_global = NULL;

struct ibuf *
cord_ibuf_take(void)
{
	assert(cord_is_main());
	struct ibuf *buf = cord_buf_global;
	if (buf != NULL) {
		ibuf_reset(buf);
		return buf;
	}
	buf = malloc(sizeof(*buf));
	if (buf == NULL)
		panic("Couldn't allocate thread buffer");
	ibuf_create(buf, &cord()->slabc, CORD_IBUF_START_CAPACITY);
	cord_buf_global = buf;
	return buf;
}

void
cord_ibuf_put(struct ibuf *ibuf)
{
	(void)ibuf;
	assert(ibuf == cord_buf_global);
}

void
cord_ibuf_drop(struct ibuf *ibuf)
{
	ibuf_reinit(ibuf);
	assert(ibuf == cord_buf_global);
}
