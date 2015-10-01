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

struct ipc_channel *
ipc_channel_new(unsigned size)
{
	if (size == 0)
		size = 1;
	struct ipc_channel *res = (struct ipc_channel *)
		malloc(ipc_channel_memsize(size));
	if (res != NULL)
		ipc_channel_create(res, size);
	return res;
}

void
ipc_channel_delete(struct ipc_channel *ch)
{
	ipc_channel_destroy(ch);
	free(ch);
}

void
ipc_channel_create(struct ipc_channel *ch, unsigned size)
{
	ch->size = size;
	ch->beg = ch->count = 0;
	ch->readonly = ch->closed = false;
	ch->close = NULL;
	rlist_create(&ch->readers);
	rlist_create(&ch->writers);
}

void
ipc_channel_destroy(struct ipc_channel *ch)
{
	/**
	 * XXX: this code is buggy, since the deleted fibers are
	 * not woken up, but luckily it never gets called.
	 * As long as channels are only used in Lua, the situation
	 * that ipc_channel_destroy() is called on a channel which
	 * has waiters is impossible:
	 *
	 * if there is a Lua fiber waiting on a channel, neither
	 * the channel nor the fiber will ever get collected. The
	 * fiber Lua stack will keep a reference to the channel
	 * userdata, and the stack itself is referenced while the
	 * fiber is waiting.  So, as long as channels are used
	 * from Lua, only a channel which has no waiters can get
	 * collected.
	 *
	 * The other part of the problem, however, is that such
	 * orphaned waiters create a garbage collection loop and
	 * leak memory.
	 * The only solution, it seems, is to implement some sort
	 * of shutdown() on a channel, which wakes up all waiters,
	 * and use it explicitly in Lua.  Waking up waiters in
	 * __gc/destroy is not a solution, since __gc will simply
	 * never get called.
         */
	while (!rlist_empty(&ch->writers)) {
		struct fiber *f =
			rlist_first_entry(&ch->writers, struct fiber, state);
		say_error("closing a channel which has a write waiter %d %s",
			  f->fid, fiber_name(f));
		rlist_del_entry(f, state);
	}
	while (!rlist_empty(&ch->readers)) {
		struct fiber *f =
			rlist_first_entry(&ch->readers, struct fiber, state);
		say_error("closing a channel which has a read waiter %d %s",
			  f->fid, fiber_name(f));
		rlist_del_entry(f, state);
	}
}

void *
ipc_channel_get_timeout(struct ipc_channel *ch, ev_tstamp timeout)
{
	if (ch->closed)
		return NULL;

	struct fiber *f;
	bool first_try = true;
	ev_tstamp started = ev_now(loop());
	void *res;
	/* channel is empty */
	while (ch->count == 0) {
		if (ch->readonly)
			return NULL;
		/* try to be in FIFO order */
		if (first_try) {
			rlist_add_tail_entry(&ch->readers, fiber(), state);
			first_try = false;
		} else {
			rlist_add_entry(&ch->readers, fiber(), state);
		}
		fiber_yield_timeout(timeout);
		rlist_del_entry(fiber(), state);

		fiber_testcancel();

		timeout -= ev_now(loop()) - started;
		if (timeout <= 0) {
			res = NULL;
			goto exit;
		}

		if (ch->readonly) {
			res = NULL;
			goto exit;
		}
	}

	res = ch->item[ch->beg];
	if (++ch->beg >= ch->size)
		ch->beg -= ch->size;
	ch->count--;

	if (!rlist_empty(&ch->writers)) {
		f = rlist_first_entry(&ch->writers, struct fiber, state);
		rlist_del_entry(f, state);
		fiber_wakeup(f);
	}

exit:
	if (ch->readonly && ch->close) {
		fiber_wakeup(ch->close);
		ch->close = NULL;
	}

	return res;
}

static void
ipc_channel_close_waiter(struct ipc_channel *ch, struct fiber *f)
{
	ch->close = fiber();

	while (ch->close) {
		fiber_wakeup(f);
		fiber_yield();
		ch->close = NULL;
		rlist_del_entry(fiber(), state);
		fiber_testcancel();
	}
}

void
ipc_channel_shutdown(struct ipc_channel *ch)
{
	if (ch->readonly)
		return;
	ch->readonly = true;

	struct fiber *f;
	while (!rlist_empty(&ch->readers)) {
		f = rlist_first_entry(&ch->readers, struct fiber, state);
		ipc_channel_close_waiter(ch, f);
	}
	while (!rlist_empty(&ch->writers)) {
		f = rlist_first_entry(&ch->writers, struct fiber, state);
		ipc_channel_close_waiter(ch, f);
	}
}

void
ipc_channel_close(struct ipc_channel *ch)
{
	if (ch->closed)
		return;
	if (!ch->readonly)
		ipc_channel_shutdown(ch);
	assert(ch->count == 0);
	assert(rlist_empty(&ch->readers));
	assert(rlist_empty(&ch->writers));
	ch->closed = true;
}

int
ipc_channel_put_timeout(struct ipc_channel *ch, void *data,
			ev_tstamp timeout)
{
	if (ch->readonly) {
		errno = EBADF;
		return -1;
	}

	bool first_try = true;
	int res;
	unsigned i;
	ev_tstamp started = ev_now(loop());
	/* channel is full */
	while (ch->count >= ch->size) {

		/* try to be in FIFO order */
		if (first_try) {
			rlist_add_tail_entry(&ch->writers, fiber(), state);
			first_try = false;
		} else {
			rlist_add_entry(&ch->writers, fiber(), state);
		}

		fiber_yield_timeout(timeout);
		rlist_del_entry(fiber(), state);

		fiber_testcancel();

		timeout -= ev_now(loop()) - started;
		if (timeout <= 0) {
			errno = ETIMEDOUT;
			res = -1;
			goto exit;
		}

		if (ch->readonly) {
			errno = EBADF;
			res = -1;
			goto exit;
		}
	}

	i = ch->beg;
	i += ch->count;
	ch->count++;
	if (i >= ch->size)
		i -= ch->size;

	ch->item[i] = data;
	if (!rlist_empty(&ch->readers)) {
		struct fiber *f;
		f = rlist_first_entry(&ch->readers, struct fiber, state);
		rlist_del_entry(f, state);
		fiber_wakeup(f);
	}
	res = 0;
exit:
	if (ch->readonly && ch->close) {
		int save_errno = errno;
		fiber_wakeup(ch->close);
		ch->close = NULL;
		errno = save_errno;
	}
	return res;
}
