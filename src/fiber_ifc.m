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

#include "fiber_ifc.h"
#include "fiber.h"
#include <stdlib.h>


struct fiber;
struct fiber_semaphore {
	int count;
	STAILQ_HEAD(, fiber) fibers;
	ev_async async;
};

struct fiber_semaphore *
fiber_semaphore_alloc(void)
{
	return malloc(sizeof(struct fiber_semaphore));
}

static void
fiber_semaphore_signal(ev_watcher *watcher, int event __attribute__((unused)))
{
	struct fiber_semaphore *s = watcher->data;
	assert(!STAILQ_EMPTY(&s->fibers));

	struct fiber *f = STAILQ_FIRST(&s->fibers);
	STAILQ_REMOVE_HEAD(&s->fibers, ifc);

	if (STAILQ_EMPTY(&s->fibers))
		ev_async_stop(&s->async);

	fiber_call(f);
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
	ev_async_init(&s->async, (void *)fiber_semaphore_signal);
	s->async.data = s;
}

void
fiber_semaphore_down(struct fiber_semaphore *s)
{
	if (--s->count >= 0)
		return;
	if (STAILQ_EMPTY(&s->fibers))
		ev_async_start(&s->async);
	STAILQ_INSERT_TAIL(&s->fibers, fiber, ifc);
	fiber_yield();
}

void
fiber_semaphore_up(struct fiber_semaphore *s)
{
	if (++s->count < 0 || STAILQ_EMPTY(&s->fibers))
		return;
	ev_async_send(&s->async);
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
fiber_mutex_isfree(struct fiber_mutex *m)
{
	return m->semaphore.count > 0;
}

/**********************************************************************/
struct fiber_rwlock {
	int rcount;
	int wcount;
	STAILQ_HEAD(, fiber) readers, writers;
	ev_async async;
};

static void
fiber_rwlock_signal(ev_watcher *watcher, int event __attribute__((unused)))
{
	struct fiber *f;
	struct fiber_rwlock *rw = watcher->data;
	assert(rw->rcount > 0 || rw->wcount > 0);
	if (rw->wcount) {
		rw->wcount--;
		f = STAILQ_FIRST(&rw->writers);
		STAILQ_REMOVE_HEAD(&rw->writers, ifc);

		if (!rw->rcount && !rw->wcount)
			ev_async_stop(&rw->async);

		fiber_call(f);
		return;
	}

	rw->rcount--;
	f = STAILQ_FIRST(&rw->readers);
	STAILQ_REMOVE_HEAD(&rw->readers, ifc);

	if (rw->rcount)
		ev_async_send(&rw->async);
	else
		ev_async_stop(&rw->async);
	fiber_call(f);
}

void
fiber_rwlock_init(struct fiber_rwlock *rw)
{
	rw->rcount = 0;
	rw->wcount = 0;
	STAILQ_INIT(&rw->readers);
	STAILQ_INIT(&rw->writers);
	ev_async_init(&rw->async, (void *)fiber_rwlock_signal);
	rw->async.data = rw;
}

void
fiber_rwlock_rdlock(struct fiber_rwlock *rw)
{
	rw->rcount++;
	if (rw->wcount) {
		if (STAILQ_EMPTY(&rw->readers) && STAILQ_EMPTY(&rw->writers))
			ev_async_start(&rw->async);
		STAILQ_INSERT_TAIL(&rw->readers, fiber, ifc);
		fiber_yield();
		return;
	}
}

void
fiber_rwlock_wrlock(struct fiber_rwlock *rw)
{
	if (rw->rcount || rw->wcount) {
		rw->wcount++;
		if (STAILQ_EMPTY(&rw->readers) && STAILQ_EMPTY(&rw->writers))
			ev_async_start(&rw->async);
		STAILQ_INSERT_TAIL(&rw->writers, fiber, ifc);
		fiber_yield();
		return;
	}

	rw->wcount++;
}

void
fiber_rwlock_unlock(struct fiber_rwlock *rw)
{
	assert(rw->wcount > 0 || rw->rcount > 0);
	if (rw->wcount) {
		rw->wcount--;
	} else {
		rw->rcount--;
	}

	if (rw->wcount || rw->rcount)
		ev_async_send(&rw->async);
}

/**********************************************************************/

struct fiber_channel {
	STAILQ_HEAD(, fiber) readers, writers;
	unsigned size;
	unsigned beg;
	unsigned count;

	ev_async rasync, wasync;

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

static void
fiber_channel_rsignal(ev_watcher *watcher, int event __attribute__((unused)))
{
	struct fiber_channel *ch = watcher->data;
	assert(!STAILQ_EMPTY(&ch->readers));

	struct fiber *f = STAILQ_FIRST(&ch->readers);
	STAILQ_REMOVE_HEAD(&ch->readers, ifc);

	if (STAILQ_EMPTY(&ch->readers))
		ev_async_stop(&ch->rasync);

	fiber_call(f);
}

static void
fiber_channel_wsignal(ev_watcher *watcher, int event __attribute__((unused)))
{
	struct fiber_channel *ch = watcher->data;
	assert(!STAILQ_EMPTY(&ch->writers));

	struct fiber *f = STAILQ_FIRST(&ch->writers);
	STAILQ_REMOVE_HEAD(&ch->writers, ifc);

	if (STAILQ_EMPTY(&ch->writers))
		ev_async_stop(&ch->wasync);

	fiber_call(f);
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

	STAILQ_INIT(&ch->readers);
	STAILQ_INIT(&ch->writers);
	ev_async_init(&ch->rasync, (void *)fiber_channel_rsignal);
	ch->rasync.data = ch;
	ev_async_init(&ch->wasync, (void *)fiber_channel_wsignal);
	ch->wasync.data = ch;
}

void *
fiber_channel_get(struct fiber_channel *ch)
{
	/* channel is empty */
	if (!ch->count) {
		if (!STAILQ_EMPTY(&ch->writers))
			ev_async_send(&ch->wasync);
		if (STAILQ_EMPTY(&ch->readers))
			ev_async_start(&ch->rasync);
		STAILQ_INSERT_TAIL(&ch->readers, fiber, ifc);
		fiber_yield();
	}

	void *res = ch->item[ ch->beg ];
	if (++ch->beg >= ch->size)
		ch->beg -= ch->size;
	ch->count--;

	if (!STAILQ_EMPTY(&ch->writers))
		ev_async_send(&ch->wasync);
	return res;
}

void
fiber_channel_put(struct fiber_channel *ch, void *data)
{
	/* channel is full */
	if (ch->count >= ch->size) {
		if (!STAILQ_EMPTY(&ch->readers))
			ev_async_send(&ch->rasync);
		if (STAILQ_EMPTY(&ch->writers))
			ev_async_start(&ch->wasync);
		STAILQ_INSERT_TAIL(&ch->writers, fiber, ifc);
		fiber_yield();
	}

	unsigned i = ch->beg;
	i += ch->count;
	ch->count++;
	if (i >= ch->size)
		i -= ch->size;

	ch->item[i] = data;
	if (!STAILQ_EMPTY(&ch->readers))
		ev_async_send(&ch->rasync);
}
