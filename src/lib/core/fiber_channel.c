/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include "fiber_channel.h"

#include <stdlib.h>

#include "fiber.h"

enum fiber_channel_wait_status {
	FIBER_CHANNEL_WAIT_READER, /* A reader is waiting for writer */
	FIBER_CHANNEL_WAIT_WRITER, /* A writer waiting for reader. */
	FIBER_CHANNEL_WAIT_DONE, /* Wait is done, message sent/received. */
	FIBER_CHANNEL_WAIT_CLOSED /* Wait is aborted, the channel is closed. */
};

/**
 * Wait pad is a helper data structure for waiting for
 * an incoming message or a reader.
 */
struct ipc_wait_pad {
	struct ipc_msg *msg;
	enum fiber_channel_wait_status status;
};

void
fiber_channel_create(struct fiber_channel *ch, uint32_t size)
{
	ch->size = size;
	ch->count = 0;
	ch->is_closed = false;
	rlist_create(&ch->waiters);
	if (ch->size) {
		ch->buf = (struct ipc_msg **) (char *) &ch[1];
		ch->beg = 0;
	}

}

struct fiber_channel *
fiber_channel_new(uint32_t size)
{
	struct fiber_channel *res = (struct fiber_channel *)
		malloc(fiber_channel_memsize(size));
	if (res == NULL) {
		diag_set(OutOfMemory, size,
			 "malloc", "struct fiber_channel");
		return NULL;
	}
	fiber_channel_create(res, size);
	return res;
}

bool
fiber_channel_has_waiter(struct fiber_channel *ch,
			 enum fiber_channel_wait_status status)
{
	if (rlist_empty(&ch->waiters))
		return false;
	struct fiber *f = rlist_first_entry(&ch->waiters, struct fiber, state);
	return f->wait_pad->status == status;
}

bool
fiber_channel_has_readers(struct fiber_channel *ch)
{
	return fiber_channel_has_waiter(ch, FIBER_CHANNEL_WAIT_READER);
}

bool
fiber_channel_has_writers(struct fiber_channel *ch)
{
	return fiber_channel_has_waiter(ch, FIBER_CHANNEL_WAIT_WRITER);
}

/**
 * Push a message into the channel buffer.
 *
 * @pre The buffer has space for a message.
 */
static inline void
fiber_channel_buffer_push(struct fiber_channel *ch, struct ipc_msg *msg)
{
	assert(ch->count < ch->size);
	/* Find an empty slot in the ring buffer. */
	uint32_t i = ch->beg + ch->count;
	if (i >= ch->size)
		i -= ch->size;
	ch->buf[i] = msg;
	ch->count++;
}

static inline struct ipc_msg *
fiber_channel_buffer_pop(struct fiber_channel *ch)
{
	assert(ch->count > 0);
	struct ipc_msg *msg = ch->buf[ch->beg];
	if (++ch->beg == ch->size)
		ch->beg = 0;
	ch->count--;
	return msg;
}

static inline void
fiber_channel_waiter_wakeup(struct fiber *f,
			    enum fiber_channel_wait_status status)
{
	/*
	 * Safe to overwrite the status without looking at it:
	 * whoever is touching the status, removes the fiber
	 * from the wait list.
	 */
	f->wait_pad->status = status;
	/*
	 * fiber_channel allows an asynchronous cancel. If a fiber
	 * is cancelled while waiting on a timeout, it is done via
	 * fiber_wakeup(), which modifies fiber->state link.
	 * This ensures that a fiber is never on two "state"
	 * lists: it's either waiting on a channel, or is
	 * cancelled, ready for execution. This is why
	 * we use fiber->state, and not (imagine) pad->link as
	 * a list link, and store the pad in the fiber key.
	 *
	 * It's important that the sender removes the receiver
	 * from the wait list, not the receiver, after it's woken
	 * up, to ensure the callee doesn't get two messages
	 * delivered to it. Since 'fiber->state' is used, this
	 * works correctly with fiber_cancel().
	 */
	fiber_wakeup(f);
}


int
fiber_channel_check_wait(struct fiber_channel *ch, ev_tstamp start_time,
		       ev_tstamp timeout)
{
	/*
	 * Preconditions of waiting are:
	 * - the channel is not closed,
	 * - the current fiber has not been
	 *   cancelled,
	 * - the timeout has not expired.
	 * If timeout is non-zero, yield at least once, otherwise
	 * rounding errors can lead to an infinite loop in the
	 * caller, since ev_now() does not get updated without
	 * a yield.
	 */
	if (ch->is_closed) {
		diag_set(ChannelIsClosed);
		return -1;
	}
	if (fiber_is_cancelled()) {
		diag_set(FiberIsCancelled);
		return -1;
	}
	if (timeout == 0 || ev_monotonic_now(loop()) > start_time + timeout) {
		diag_set(TimedOut);
		return -1;
	}
	return 0;
}

void
fiber_channel_close(struct fiber_channel *ch)
{
	if (ch->is_closed)
		return;

	while (ch->count) {
		struct ipc_msg *msg = fiber_channel_buffer_pop(ch);
		msg->destroy(msg);
	}

	struct fiber *f;
	while (! rlist_empty(&ch->waiters)) {
		f = rlist_first_entry(&ch->waiters, struct fiber, state);
		fiber_channel_waiter_wakeup(f, FIBER_CHANNEL_WAIT_CLOSED);
	}
	ch->is_closed = true;
}

void
fiber_channel_destroy(struct fiber_channel *ch)
{
	fiber_channel_close(ch);
}

void
fiber_channel_delete(struct fiber_channel *ch)
{
	fiber_channel_destroy(ch);
	free(ch);
}

static __thread struct mempool ipc_value_pool;

struct ipc_value *
ipc_value_new(void)
{
	if (! mempool_is_initialized(&ipc_value_pool)) {
		/*
		 * We don't need to bother with
		 * destruction since the entire slab cache
		 * is freed when the thread ends.
		 */
		mempool_create(&ipc_value_pool, &cord()->slabc,
			       sizeof(struct ipc_value));
	}
	struct ipc_value *value = (struct ipc_value *)
		mempool_alloc(&ipc_value_pool);
	if (value == NULL) {
		diag_set(OutOfMemory, sizeof(struct ipc_value),
			 "ipc_msg_pool", "struct ipc_value");
		return NULL;
	}
	value->base.destroy = ipc_value_delete;
	return value;
}

void
ipc_value_delete(struct ipc_msg *msg)
{
	mempool_free(&ipc_value_pool, msg);
}

int
fiber_channel_put_timeout(struct fiber_channel *ch,
			void *data,
			ev_tstamp timeout)
{
	struct ipc_value *value = ipc_value_new();
	if (value == NULL)
		return -1;
	value->data = data;
	int rc = fiber_channel_put_msg_timeout(ch, &value->base, timeout);
	if (rc < 0)
		ipc_value_delete(&value->base);
	return rc;
}

int
fiber_channel_get_timeout(struct fiber_channel *ch, void **data,
			ev_tstamp timeout)
{
	struct ipc_value *value;
	int rc = fiber_channel_get_msg_timeout(ch, (struct ipc_msg **) &value,
					     timeout);
	if (rc < 0)
		return rc;
	*data = value->data;
	ipc_value_delete(&value->base);
	return rc;
}

int
fiber_channel_put_msg_timeout(struct fiber_channel *ch,
			    struct ipc_msg *msg,
			    ev_tstamp timeout)
{
	/** Ensure delivery fairness in case of prolonged wait. */
	bool first_try = true;
	ev_tstamp start_time = ev_monotonic_now(loop());

	while (true) {
		/*
		 * Check if there is a ready reader first, and
		 * only if there is no reader try to put a message
		 * into the channel buffer.
		 */
		if (fiber_channel_has_readers(ch)) {
			/**
			 * There is a reader, push the message
			 * immediately.
			 */

			/*
			 * There can be no reader if there is
			 * a buffered message or the channel is
			 * closed.
			 */
			assert(ch->count == 0);
			assert(ch->is_closed == false);

			struct fiber *f = rlist_first_entry(&ch->waiters,
							    struct fiber,
							    state);
			/* Place the message on the pad. */
			f->wait_pad->msg = msg;

			fiber_channel_waiter_wakeup(f, FIBER_CHANNEL_WAIT_DONE);
			return 0;
		}
		if (ch->count < ch->size) {
			/*
			 * No reader, but the channel is buffered.
			 * Nice, drop the message in the buffer.
			 */

			/*
			 * Closed channels, are, well, closed,
			 * even if there is space in the buffer.
			 */
			if (ch->is_closed) {
				diag_set(ChannelIsClosed);
				return -1;
			}
			fiber_channel_buffer_push(ch, msg);
			return 0;
		}
		/**
		 * No reader and no space in the buffer.
		 * Have to wait.
		 */
		struct fiber *f = fiber();

		if (fiber_channel_check_wait(ch, start_time, timeout))
			return -1;

		/* Prepare a wait pad. */
		struct ipc_wait_pad pad;
		pad.status = FIBER_CHANNEL_WAIT_WRITER;
		pad.msg = msg;
		f->wait_pad = &pad;

		if (first_try) {
			rlist_add_tail_entry(&ch->waiters, f, state);
			first_try = false;
		} else {
			rlist_add_entry(&ch->waiters, f, state);
		}
		fiber_yield_timeout(timeout);
		/*
		 * In case of yield timeout, fiber->state
		 * is in the ch->waiters list, remove.
		 * rlist_del_entry() is a no-op if already done.
		 */
		rlist_del_entry(f, state);
		f->wait_pad = NULL;

		if (pad.status == FIBER_CHANNEL_WAIT_CLOSED) {
			/*
			 * The channel is closed.  Do not touch
			 * the channel object. It might be gone
			 * already.
			 */
			diag_set(ChannelIsClosed);
			return -1;
		}
		if (pad.status == FIBER_CHANNEL_WAIT_DONE)
			return 0;  /* OK, someone took the message. */
		timeout -= ev_monotonic_now(loop()) - start_time;
	}
}

int
fiber_channel_get_msg_timeout(struct fiber_channel *ch,
			    struct ipc_msg **msg,
			    ev_tstamp timeout)
{
	/** Ensure delivery fairness in case of prolonged wait. */
	bool first_try = true;
	ev_tstamp start_time = ev_monotonic_now(loop());

	while (true) {
		struct fiber *f;
		/*
		 * Buffered messages take priority over waiting
		 * fibers, if any, since they arrived earlier.
		 * Try to take a message from the buffer first.
		 */
		if (ch->count > 0) {
			/**
			 * There can't be any buffered stuff in
			 * a closed channel - everything is
			 * destroyed at close.
			 */
			assert(ch->is_closed == false);

			*msg = fiber_channel_buffer_pop(ch);

			if (fiber_channel_has_writers(ch)) {
				/*
				 * Move a waiting writer, if any,
				 * from the wait list to the tail
				 * the buffer, to preserve fairness
				 * in message delivery order.
				 */
				f = rlist_first_entry(&ch->waiters,
						      struct fiber,
						      state);
				fiber_channel_buffer_push(ch, f->wait_pad->msg);
				fiber_channel_waiter_wakeup(f,
					FIBER_CHANNEL_WAIT_DONE);
			}
			return 0;
		}
		if (fiber_channel_has_writers(ch)) {
			/**
			 * There is no buffered messages, *but*
			 * there is a writer. This is only
			 * possible when the channel is
			 * unbuffered.
			 * Take the message directly from the
			 * writer and be done with it.
			 */
			assert(ch->size == 0);
			f = rlist_first_entry(&ch->waiters,
					      struct fiber,
					      state);
			*msg = f->wait_pad->msg;
			fiber_channel_waiter_wakeup(f, FIBER_CHANNEL_WAIT_DONE);
			return 0;
		}
		if (fiber_channel_check_wait(ch, start_time, timeout))
			return -1;
		f = fiber();
		/**
		 * No reader and no space in the buffer.
		 * Have to wait.
		 */
		struct ipc_wait_pad pad;
		pad.status = FIBER_CHANNEL_WAIT_READER;
		f->wait_pad = &pad;
		if (first_try) {
			rlist_add_tail_entry(&ch->waiters, f, state);
			first_try = false;
		} else {
			rlist_add_entry(&ch->waiters, f, state);
		}
		fiber_yield_timeout(timeout);
		/*
		 * In case of yield timeout, fiber->state
		 * is in the ch->waiters list, remove.
		 * rlist_del_entry() is a no-op if already done.
		 */
		rlist_del_entry(f, state);
		f->wait_pad = NULL;
		if (pad.status == FIBER_CHANNEL_WAIT_CLOSED) {
			diag_set(ChannelIsClosed);
			return -1;
		}
		if (pad.status == FIBER_CHANNEL_WAIT_DONE) {
			*msg = pad.msg;
			return 0;
		}
		timeout -= ev_monotonic_now(loop()) - start_time;
	}
}
