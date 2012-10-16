/*
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

#include "ifc.h"
#include "fiber.h"
#include <stdlib.h>
#include <rlist.h>


struct ifc_semaphore {
	int count;
	struct rlist fibers, wakeup;
};


struct ifc_semaphore *
ifc_semaphore_alloc(void)
{
	return malloc(sizeof(struct ifc_semaphore));
}


int
ifc_semaphore_counter(struct ifc_semaphore *s)
{
	return s->count;
}

void
ifc_semaphore_init(struct ifc_semaphore *s, int cnt)
{
	s->count = cnt;
	rlist_init(&s->fibers);
	rlist_init(&s->wakeup);
}

int
ifc_semaphore_down_timeout(struct ifc_semaphore *s, ev_tstamp timeout)
{
	int count = --s->count;

	if (count >= 0)		/* semaphore is still unlocked */
		return 0;

	if (timeout < 0)
		timeout = 0;

	rlist_add_tail_entry(&s->fibers, fiber, ifc);

	bool cancellable = fiber_setcancellable(true);

	if (timeout) {
		ev_timer_set(&fiber->timer, timeout, 0);
		ev_timer_start(&fiber->timer);
		fiber_yield();
		ev_timer_stop(&fiber->timer);
	} else {
		fiber_yield();
	}

	fiber_setcancellable(cancellable);

	int timeouted = ETIMEDOUT;
	struct fiber *f;

	rlist_foreach_entry(f, &s->wakeup, ifc) {
		if (f != fiber)
			continue;
		timeouted = 0;
		break;
	}

	if (timeouted)
		s->count++;

	rlist_del_entry(fiber, ifc);
	fiber_testcancel();
	return timeouted;
}

void
ifc_semaphore_down(struct ifc_semaphore *s)
{
	ifc_semaphore_down_timeout(s, 0);
}

void
ifc_semaphore_up(struct ifc_semaphore *s)
{
	s->count++;

	if (rlist_empty(&s->fibers))
		return;

	/* wake up one fiber */
	struct fiber *f = rlist_first_entry(&s->fibers, struct fiber, ifc);
	rlist_del_entry(f, ifc);
	rlist_add_tail_entry(&s->wakeup, f, ifc);
	fiber_wakeup(f);
}

int
ifc_semaphore_trydown(struct ifc_semaphore *s)
{
	if (s->count <= 0)
		return s->count - 1;
	ifc_semaphore_down(s);
	return 0;
}


struct ifc_mutex {
	struct ifc_semaphore semaphore;
};

struct ifc_mutex *
ifc_mutex_alloc(void)
{
	return malloc(sizeof(struct ifc_mutex));
}

void
ifc_mutex_init(struct ifc_mutex *m)
{
	ifc_semaphore_init(&m->semaphore, 1);
}

void
ifc_mutex_lock(struct ifc_mutex *m)
{
	ifc_semaphore_down(&m->semaphore);
}

int
ifc_mutex_lock_timeout(struct ifc_mutex *m, ev_tstamp timeout)
{
	return ifc_semaphore_down_timeout(&m->semaphore, timeout);
}

void
ifc_mutex_unlock(struct ifc_mutex *m)
{
	ifc_semaphore_up(&m->semaphore);
}

int
ifc_mutex_trylock(struct ifc_mutex *m)
{
	return ifc_semaphore_trydown(&m->semaphore);
}

int
ifc_mutex_islocked(struct ifc_mutex *m)
{
	return m->semaphore.count <= 0;
}

/**********************************************************************/

struct ifc_channel {
	struct rlist readers, writers, wakeup;
	unsigned size;
	unsigned beg;
	unsigned count;

	void *bcast_msg;
	struct fiber *bcast;

	void *item[0];
} __attribute__((packed));

int
ifc_channel_isempty(struct ifc_channel *ch)
{
	return ch->count == 0;
}

int
ifc_channel_isfull(struct ifc_channel *ch)
{
	return ch->count >= ch->size;
}


struct ifc_channel *
ifc_channel_alloc(unsigned size)
{
	if (!size)
		size = 1;
	struct ifc_channel *res =
		malloc(sizeof(struct ifc_channel) + sizeof(void *) * size);
	if (res)
		res->size = size;
	return res;
}

void
ifc_channel_init(struct ifc_channel *ch)
{

	ch->beg = ch->count = 0;
	ch->bcast = NULL;

	rlist_init(&ch->readers);
	rlist_init(&ch->writers);
	rlist_init(&ch->wakeup);
}

void *
ifc_channel_get_timeout(struct ifc_channel *ch, ev_tstamp timeout)
{
	if (timeout < 0)
		timeout = 0;
	/* channel is empty */
	if (!ch->count) {
		rlist_add_tail_entry(&ch->readers, fiber, ifc);
		bool cancellable = fiber_setcancellable(true);

		if (timeout) {
			ev_timer_set(&fiber->timer, timeout, 0);
			ev_timer_start(&fiber->timer);
			fiber_yield();
			ev_timer_stop(&fiber->timer);
		} else {
			fiber_yield();
		}


		rlist_del_entry(fiber, ifc);

		fiber_testcancel();
		fiber_setcancellable(cancellable);

		if (ch->bcast) {
			fiber_wakeup(ch->bcast);
			ch->bcast = NULL;
			return ch->bcast_msg;
		}
	}

	/* timeout */
	if (!ch->count)
		return NULL;

	void *res = ch->item[ ch->beg ];
	if (++ch->beg >= ch->size)
		ch->beg -= ch->size;
	ch->count--;

	if (!rlist_empty(&ch->writers)) {
		struct fiber *f =
			rlist_first_entry(&ch->writers, struct fiber, ifc);
		rlist_del_entry(f, ifc);
		rlist_add_tail_entry(&ch->wakeup, f, ifc);
		fiber_wakeup(f);
	}

	return res;
}

void *
ifc_channel_get(struct ifc_channel *ch)
{
	return ifc_channel_get_timeout(ch, 0);
}

int
ifc_channel_put_timeout(struct ifc_channel *ch, void *data,
							ev_tstamp timeout)
{
	if (timeout < 0)
		timeout = 0;

	/* channel is full */
	if (ch->count >= ch->size) {

		rlist_add_tail_entry(&ch->writers, fiber, ifc);

		bool cancellable = fiber_setcancellable(true);
		if (timeout) {
			ev_timer_set(&fiber->timer, timeout, 0);
			ev_timer_start(&fiber->timer);
			fiber_yield();
			ev_timer_stop(&fiber->timer);
		} else {
			fiber_yield();
		}

		rlist_del_entry(fiber, ifc);

		fiber_testcancel();
		fiber_setcancellable(cancellable);
	}

	if (ch->count >= ch->size)
		return ETIMEDOUT;

	unsigned i = ch->beg;
	i += ch->count;
	ch->count++;
	if (i >= ch->size)
		i -= ch->size;

	ch->item[i] = data;
	if (!rlist_empty(&ch->readers)) {
		struct fiber *f =
			rlist_first_entry(&ch->readers, struct fiber, ifc);
		rlist_del_entry(f, ifc);
		rlist_add_tail_entry(&ch->wakeup, f, ifc);
		fiber_wakeup(f);
	}
	return 0;
}

void
ifc_channel_put(struct ifc_channel *ch, void *data)
{
	ifc_channel_put_timeout(ch, data, 0);
}

int
ifc_channel_has_readers(struct ifc_channel *ch)
{
	return !rlist_empty(&ch->readers);
}

int
ifc_channel_has_writers(struct ifc_channel *ch)
{
	return !rlist_empty(&ch->writers);
}

int
ifc_channel_broadcast(struct ifc_channel *ch, void *data)
{
	if (rlist_empty(&ch->readers)) {
		ifc_channel_put(ch, data);
		return 1;
	}

	struct fiber *f;
	int count = 0;
	rlist_foreach_entry(f, &ch->readers, ifc) {
		count++;
	}

	for (int i = 0; i < count && !rlist_empty(&ch->readers); i++) {
		struct fiber *f =
			rlist_first_entry(&ch->readers, struct fiber, ifc);
		rlist_del_entry(f, ifc);
		rlist_add_tail_entry(&ch->wakeup, f, ifc);
		ch->bcast = fiber;
		ch->bcast_msg = data;
		fiber_wakeup(f);
		fiber_yield();
		ch->bcast = NULL;
		fiber_testcancel();
		if (rlist_empty(&ch->readers)) {
			count = i;
			break;
		}
	}

	return count;
}
