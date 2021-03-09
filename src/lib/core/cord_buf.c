/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "cord_buf.h"
#include "fiber.h"
#include "trigger.h"

#include "small/ibuf.h"

enum {
	/* No any reason why that value. Historical constant. */
	CORD_IBUF_START_CAPACITY = 16384,
};

/** Global buffer with automatic collection on fiber yield. */
struct cord_buf {
	/** Base buffer. */
	struct ibuf base;
	/**
	 * Triggers on fiber stop/yield when the buffer is either destroyed or
	 * cached to the global stash for later reuse.
	 */
	struct trigger on_stop;
	struct trigger on_yield;
#ifndef NDEBUG
	/**
	 * Fiber owning the buffer right now. Used for debug and sanity checks
	 * only.
	 */
	struct fiber *owner;
#endif
};

/**
 * The global buffer last saved to the cache. Having it here is supposed to
 * help to reuse the buffer's already allocated data sometimes.
 */
static struct cord_buf *cord_buf_global = NULL;

static inline void
cord_buf_put(struct cord_buf *buf);

static void
cord_buf_delete(struct cord_buf *buf);

static inline void
cord_buf_set_owner(struct cord_buf *buf)
{
	assert(buf->owner == NULL);
	struct fiber *f = fiber();
	trigger_add(&f->on_stop, &buf->on_stop);
	trigger_add(&f->on_yield, &buf->on_yield);
#ifndef NDEBUG
	buf->owner = f;
#endif
	ibuf_reset(&buf->base);
}

static inline void
cord_buf_clear_owner(struct cord_buf *buf)
{
	assert(buf->owner == fiber());
	trigger_clear(&buf->on_stop);
	trigger_clear(&buf->on_yield);
#ifndef NDEBUG
	buf->owner = NULL;
#endif
}

static int
cord_buf_on_stop(struct trigger *trigger, void *event)
{
	(void)event;
	struct cord_buf *buf = trigger->data;
	assert(trigger == &buf->on_stop);
	cord_buf_put(buf);
	return 0;
}

static int
cord_buf_on_yield(struct trigger *trigger, void *event)
{
	(void)event;
	struct cord_buf *buf = trigger->data;
	assert(trigger == &buf->on_yield);
	cord_buf_put(buf);
	return 0;
}

static struct cord_buf *
cord_buf_new(void)
{
	struct cord_buf *buf = malloc(sizeof(*buf));
	if (buf == NULL)
		panic("Couldn't allocate thread buffer");
	ibuf_create(&buf->base, &cord()->slabc, CORD_IBUF_START_CAPACITY);
	trigger_create(&buf->on_stop, cord_buf_on_stop, buf, NULL);
	trigger_create(&buf->on_yield, cord_buf_on_yield, buf, NULL);
#ifndef NDEBUG
	buf->owner = NULL;
#endif
	return buf;
}

static inline void
cord_buf_put(struct cord_buf *buf)
{
	assert(cord_is_main());
	cord_buf_clear_owner(buf);
	/*
	 * Delete if the stash is busy. It could happen if there was >= 2
	 * buffers at some point and one of them is already saved back to the
	 * stash.
	 *
	 * XXX: in future it might be useful to consider saving the buffers into
	 * a list. Maybe keep always at most 2 buffers, because usually there
	 * are at most 2 contexts: normal Lua and Lua during GC. Recursive
	 * GC is supposed to be rare, no need to optimize it.
	 */
	if (cord_buf_global == NULL)
		cord_buf_global = buf;
	else
		cord_buf_delete(buf);
}

static inline struct cord_buf *
cord_buf_take(void)
{
	assert(cord_is_main());
	struct cord_buf *buf = cord_buf_global;
	if (buf != NULL)
		cord_buf_global = NULL;
	else
		buf = cord_buf_new();
	cord_buf_set_owner(buf);
	return buf;
}

static void
cord_buf_delete(struct cord_buf *buf)
{
	assert(buf->owner == NULL);
	ibuf_destroy(&buf->base);
	TRASH(buf);
	free(buf);
}

struct ibuf *
cord_ibuf_take(void)
{
	return &cord_buf_take()->base;
}

void
cord_ibuf_put(struct ibuf *ibuf)
{
	cord_buf_put((struct cord_buf *)ibuf);
}

void
cord_ibuf_drop(struct ibuf *ibuf)
{
	ibuf_reinit(ibuf);
	cord_ibuf_put(ibuf);
}
