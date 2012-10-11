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



struct fiber_semaphore {
	int count;
	STAILQ_HEAD(, fiber) fibers, wakeup;
};


struct fiber_semaphore *
fiber_semaphore_alloc(void)
{
	return malloc(sizeof(struct fiber_semaphore));
}


int
fiber_semaphore_counter(struct fiber_semaphore *s)
{
	return s->count;
}

void
fiber_semaphore_init(struct fiber_semaphore *s, int cnt)
{
	s->count = cnt;
	STAILQ_INIT(&s->fibers);
	STAILQ_INIT(&s->wakeup);
}

int
fiber_semaphore_down_timeout(struct fiber_semaphore *s, ev_tstamp timeout)
{
	int count = --s->count;

	if (count >= 0)		/* semaphore is still unlocked */
		return 0;

	if (timeout < 0)
		timeout = 0;

	STAILQ_INSERT_TAIL(&s->fibers, fiber, ifc);

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

	STAILQ_FOREACH(f, &s->wakeup, ifc) {
		if (f != fiber)
			continue;
		STAILQ_REMOVE(&s->wakeup, fiber, fiber, ifc);
		timeouted = 0;
		break;
	}

	if (timeouted) {
		STAILQ_REMOVE(&s->fibers, fiber, fiber, ifc);
		s->count++;
	}

	fiber_testcancel();

	return timeouted;
}

void
fiber_semaphore_down(struct fiber_semaphore *s)
{
	fiber_semaphore_down_timeout(s, 0);
}

void
fiber_semaphore_up(struct fiber_semaphore *s)
{
	s->count++;

	if (STAILQ_EMPTY(&s->fibers))
		return;

	/* wake up one fiber */
	struct fiber *f = STAILQ_FIRST(&s->fibers);
	STAILQ_REMOVE(&s->fibers, f, fiber, ifc);
	STAILQ_INSERT_TAIL(&s->wakeup, f, ifc);
	fiber_wakeup(f);
}

int
fiber_semaphore_trydown(struct fiber_semaphore *s)
{
	if (s->count <= 0)
		return s->count - 1;
	fiber_semaphore_down(s);
	return 0;
}


struct fiber_mutex {
	struct fiber_semaphore semaphore;
};

struct fiber_mutex *
fiber_mutex_alloc(void)
{
	return malloc(sizeof(struct fiber_mutex));
}

void
fiber_mutex_init(struct fiber_mutex *m)
{
	fiber_semaphore_init(&m->semaphore, 1);
}

void
fiber_mutex_lock(struct fiber_mutex *m)
{
	fiber_semaphore_down(&m->semaphore);
}

int
fiber_mutex_lock_timeout(struct fiber_mutex *m, ev_tstamp timeout)
{
	return fiber_semaphore_down_timeout(&m->semaphore, timeout);
}

void
fiber_mutex_unlock(struct fiber_mutex *m)
{
	fiber_semaphore_up(&m->semaphore);
}

int
fiber_mutex_trylock(struct fiber_mutex *m)
{
	return fiber_semaphore_trydown(&m->semaphore);
}

int
fiber_mutex_islocked(struct fiber_mutex *m)
{
	return m->semaphore.count <= 0;
}

/**********************************************************************/

struct fiber_channel {
	STAILQ_HEAD(, fiber) readers, writers, wakeup;
	unsigned size;
	unsigned beg;
	unsigned count;

	void *bcast_msg;
	struct fiber *bcast;

	void *item[0];
} __attribute__((packed));

int
fiber_channel_isempty(struct fiber_channel *ch)
{
	return ch->count == 0;
}

int
fiber_channel_isfull(struct fiber_channel *ch)
{
	return ch->count >= ch->size;
}


struct fiber_channel *
fiber_channel_alloc(unsigned size)
{
	if (!size)
		size = 1;
	struct fiber_channel *res =
		malloc(sizeof(struct fiber_channel) + sizeof(void *) * size);
	if (res)
		res->size = size;
	return res;
}

void
fiber_channel_init(struct fiber_channel *ch)
{

	ch->beg = ch->count = 0;
	ch->bcast = NULL;

	STAILQ_INIT(&ch->readers);
	STAILQ_INIT(&ch->writers);
	STAILQ_INIT(&ch->wakeup);
}

void *
fiber_channel_get_timeout(struct fiber_channel *ch, ev_tstamp timeout)
{
	if (timeout < 0)
		timeout = 0;
	/* channel is empty */
	if (!ch->count) {
		STAILQ_INSERT_TAIL(&ch->readers, fiber, ifc);
		bool cancellable = fiber_setcancellable(true);

		if (timeout) {
			ev_timer_set(&fiber->timer, timeout, 0);
			ev_timer_start(&fiber->timer);
			fiber_yield();
			ev_timer_stop(&fiber->timer);
		} else {
			fiber_yield();
		}


		int timeouted = ETIMEDOUT;
		struct fiber *f;
		STAILQ_FOREACH(f, &ch->wakeup, ifc) {
			if (f != fiber)
				continue;
			STAILQ_REMOVE(&ch->wakeup, fiber, fiber, ifc);
			timeouted = 0;
			break;
		}
		if (timeouted)
			STAILQ_REMOVE(&ch->readers, fiber, fiber, ifc);


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

	if (!STAILQ_EMPTY(&ch->writers)) {
		struct fiber *f = STAILQ_FIRST(&ch->writers);
		STAILQ_REMOVE_HEAD(&ch->writers, ifc);
		STAILQ_INSERT_TAIL(&ch->wakeup, f, ifc);
		fiber_wakeup(f);
	}

	return res;
}

void *
fiber_channel_get(struct fiber_channel *ch)
{
	return fiber_channel_get_timeout(ch, 0);
}

int
fiber_channel_put_timeout(struct fiber_channel *ch, void *data,
							ev_tstamp timeout)
{
	say_info("==== %s(%lu)", __func__, (unsigned long)data);
	if (timeout < 0)
		timeout = 0;

	/* channel is full */
	if (ch->count >= ch->size) {

		STAILQ_INSERT_TAIL(&ch->writers, fiber, ifc);

		bool cancellable = fiber_setcancellable(true);
		if (timeout) {
			ev_timer_set(&fiber->timer, timeout, 0);
			ev_timer_start(&fiber->timer);
			fiber_yield();
			ev_timer_stop(&fiber->timer);
		} else {
			fiber_yield();
		}

		int timeouted = ETIMEDOUT;
		struct fiber *f;
		STAILQ_FOREACH(f, &ch->wakeup, ifc) {
			if (f != fiber)
				continue;
			STAILQ_REMOVE(&ch->wakeup, fiber, fiber, ifc);
			timeouted = 0;
			break;
		}
		if (timeouted)
			STAILQ_REMOVE(&ch->writers, fiber, fiber, ifc);

		fiber_testcancel();
		fiber_setcancellable(cancellable);
		if (timeouted)
			return timeouted;
	}

	unsigned i = ch->beg;
	i += ch->count;
	ch->count++;
	if (i >= ch->size)
		i -= ch->size;

	ch->item[i] = data;
	if (!STAILQ_EMPTY(&ch->readers)) {
		struct fiber *f = STAILQ_FIRST(&ch->readers);
		STAILQ_REMOVE_HEAD(&ch->readers, ifc);
		STAILQ_INSERT_TAIL(&ch->wakeup, f, ifc);
		fiber_wakeup(f);
	}
	return 0;
}

void
fiber_channel_put(struct fiber_channel *ch, void *data)
{
	fiber_channel_put_timeout(ch, data, 0);
}

int
fiber_channel_broadcast(struct fiber_channel *ch, void *data)
{
	if (STAILQ_EMPTY(&ch->readers))
		return 0;

	struct fiber *f;
	int count = 0;
	STAILQ_FOREACH(f, &ch->readers, ifc) {
		count++;
	}

	for (int i = 0; i < count && !STAILQ_EMPTY(&ch->readers); i++) {
		struct fiber *f = STAILQ_FIRST(&ch->readers);
		STAILQ_REMOVE_HEAD(&ch->readers, ifc);
		STAILQ_INSERT_TAIL(&ch->wakeup, f, ifc);
		ch->bcast = fiber;
		ch->bcast_msg = data;
		fiber_wakeup(f);
		fiber_yield();
		ch->bcast = NULL;
		fiber_testcancel();
		if (STAILQ_EMPTY(&ch->readers)) {
			count = i;
			break;
		}
	}

	return count;
}
