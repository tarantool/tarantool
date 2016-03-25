/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include "ipc.h"
#include "fiber.h"
#include <stdlib.h>

enum ipc_wait_status {
	IPC_WAIT_READER, /* A reader is waiting for writer */
	IPC_WAIT_WRITER, /* A writer waiting for reader. */
	IPC_WAIT_DONE, /* Wait is done, message sent/received. */
	IPC_WAIT_CLOSED /* Wait is aborted, the channel is closed. */
};

/**
 * Wait pad is a helper data structure for waiting for
 * an incoming message or a reader.
 */
struct ipc_wait_pad {
	struct ipc_msg *msg;
	enum ipc_wait_status status;
};

void
ipc_channel_create(struct ipc_channel *ch, uint32_t size)
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

struct ipc_channel *
ipc_channel_new(uint32_t size)
{
	struct ipc_channel *res = (struct ipc_channel *)
		malloc(ipc_channel_memsize(size));
	if (res == NULL) {
		diag_set(OutOfMemory, size,
			 "malloc", "struct ipc_channel");
	}
	ipc_channel_create(res, size);
	return res;
}

bool
ipc_channel_has_waiter(struct ipc_channel *ch, enum ipc_wait_status status)
{
	if (rlist_empty(&ch->waiters))
		return false;
	struct fiber *f = rlist_first_entry(&ch->waiters, struct fiber, state);
	struct ipc_wait_pad *pad = (struct ipc_wait_pad *)
		fiber_get_key(f, FIBER_KEY_MSG);
	return pad->status == status;
}

bool
ipc_channel_has_readers(struct ipc_channel *ch)
{
	return ipc_channel_has_waiter(ch, IPC_WAIT_READER);
}

bool
ipc_channel_has_writers(struct ipc_channel *ch)
{
	return ipc_channel_has_waiter(ch, IPC_WAIT_WRITER);
}

/**
 * Push a message into the channel buffer.
 *
 * @pre The buffer has space for a message.
 */
static inline void
ipc_channel_buffer_push(struct ipc_channel *ch, struct ipc_msg *msg)
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
ipc_channel_buffer_pop(struct ipc_channel *ch)
{
	assert(ch->count > 0);
	struct ipc_msg *msg = ch->buf[ch->beg];
	if (++ch->beg == ch->size)
		ch->beg = 0;
	ch->count--;
	return msg;
}

static inline void
ipc_channel_waiter_wakeup(struct fiber *f, enum ipc_wait_status status)
{
	struct ipc_wait_pad *pad = (struct ipc_wait_pad *)
		fiber_get_key(f, FIBER_KEY_MSG);
	/*
	 * Safe to overwrite the status without looking at it:
	 * whoever is touching the status, removes the fiber
	 * from the wait list.
	 */
	pad->status = status;
	/*
	 * ipc_channel allows an asynchronous cancel. If a fiber
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
ipc_channel_check_wait(struct ipc_channel *ch, ev_tstamp start_time,
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
	if (timeout == 0 || ev_now(loop()) > start_time + timeout) {
		diag_set(TimedOut);
		return -1;
	}
	return 0;
}

void
ipc_channel_close(struct ipc_channel *ch)
{
	if (ch->is_closed)
		return;

	while (ch->count) {
		struct ipc_msg *msg = ipc_channel_buffer_pop(ch);
		msg->destroy(msg);
	}

	struct fiber *f;
	while (! rlist_empty(&ch->waiters)) {
		f = rlist_first_entry(&ch->waiters, struct fiber, state);
		ipc_channel_waiter_wakeup(f, IPC_WAIT_CLOSED);
	}
	ch->is_closed = true;
}

void
ipc_channel_destroy(struct ipc_channel *ch)
{
	ipc_channel_close(ch);
}

void
ipc_channel_delete(struct ipc_channel *ch)
{
	ipc_channel_destroy(ch);
	free(ch);
}

static __thread struct mempool ipc_value_pool;

struct ipc_value *
ipc_value_new()
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
ipc_channel_put_timeout(struct ipc_channel *ch,
			void *data,
			ev_tstamp timeout)
{
	struct ipc_value *value = ipc_value_new();
	if (value == NULL)
		return -1;
	value->data = data;
	int rc = ipc_channel_put_msg_timeout(ch, &value->base, timeout);
	if (rc < 0)
		ipc_value_delete(&value->base);
	return rc;
}

int
ipc_channel_get_timeout(struct ipc_channel *ch, void **data,
			ev_tstamp timeout)
{
	struct ipc_value *value;
	int rc = ipc_channel_get_msg_timeout(ch, (struct ipc_msg **) &value,
					     timeout);
	if (rc < 0)
		return rc;
	*data = value->data;
	ipc_value_delete(&value->base);
	return rc;
}

int
ipc_channel_put_msg_timeout(struct ipc_channel *ch,
			    struct ipc_msg *msg,
			    ev_tstamp timeout)
{
	/** Ensure delivery fairness in case of prolonged wait. */
	bool first_try = true;
	ev_tstamp start_time = ev_now(loop());

	while (true) {
		/*
		 * Check if there is a ready reader first, and
		 * only if there is no reader try to put a message
		 * into the channel buffer.
		 */
		if (ipc_channel_has_readers(ch)) {
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
			struct ipc_wait_pad *pad = (struct ipc_wait_pad *)
				fiber_get_key(f, FIBER_KEY_MSG);

			pad->msg = msg;

			ipc_channel_waiter_wakeup(f, IPC_WAIT_DONE);
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
			ipc_channel_buffer_push(ch, msg);
			return 0;
		}
		/**
		 * No reader and no space in the buffer.
		 * Have to wait.
		 */
		struct fiber *f = fiber();

		if (ipc_channel_check_wait(ch, start_time, timeout))
			return -1;

		/* Prepare a wait pad. */
		struct ipc_wait_pad pad;
		pad.status = IPC_WAIT_WRITER;
		pad.msg = msg;
		fiber_set_key(f, FIBER_KEY_MSG, &pad);

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
		fiber_set_key(f, FIBER_KEY_MSG, NULL);

		if (pad.status == IPC_WAIT_CLOSED) {
			/*
			 * The channel is closed.  Do not touch
			 * the channel object. It might be gone
			 * already.
			 */
			diag_set(ChannelIsClosed);
			return -1;
		}
		if (pad.status == IPC_WAIT_DONE)
			return 0;  /* OK, someone took the message. */
		timeout -= ev_now(loop()) - start_time;
	}
}

int
ipc_channel_get_msg_timeout(struct ipc_channel *ch,
			    struct ipc_msg **msg,
			    ev_tstamp timeout)
{
	/** Ensure delivery fairness in case of prolonged wait. */
	bool first_try = true;
	ev_tstamp start_time = ev_now(loop());

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

			*msg = ipc_channel_buffer_pop(ch);

			if (ipc_channel_has_writers(ch)) {
				/*
				 * Move a waiting writer, if any,
				 * from the wait list to the tail
				 * the buffer, to preserve fairness
				 * in message delivery order.
				 */
				f = rlist_first_entry(&ch->waiters,
						      struct fiber,
						      state);
				struct ipc_wait_pad *pad =
					(struct ipc_wait_pad *)
					fiber_get_key(f, FIBER_KEY_MSG);
				ipc_channel_buffer_push(ch, pad->msg);
				ipc_channel_waiter_wakeup(f, IPC_WAIT_DONE);
			}
			return 0;
		}
		if (ipc_channel_has_writers(ch)) {
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
			struct ipc_wait_pad *pad =
				(struct ipc_wait_pad *)
				fiber_get_key(f, FIBER_KEY_MSG);
			*msg = pad->msg;
			ipc_channel_waiter_wakeup(f, IPC_WAIT_DONE);
			return 0;
		}
		if (ipc_channel_check_wait(ch, start_time, timeout))
			return -1;
		f = fiber();
		/**
		 * No reader and no space in the buffer.
		 * Have to wait.
		 */
		struct ipc_wait_pad pad;
		pad.status = IPC_WAIT_READER;
		fiber_set_key(f, FIBER_KEY_MSG, &pad);
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
		fiber_set_key(f, FIBER_KEY_MSG, NULL);
		if (pad.status == IPC_WAIT_CLOSED) {
			diag_set(ChannelIsClosed);
			return -1;
		}
		if (pad.status == IPC_WAIT_DONE) {
			*msg = pad.msg;
			return 0;
		}
		timeout -= ev_now(loop()) - start_time;
	}
}

