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
#include "fiber.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "say.h"
#include "stat.h"
#include "assoc.h"
#include "memory.h"
#include "trigger.h"

static struct cord main_cord;
__thread struct cord *cord_ptr = NULL;
pthread_t main_thread_id;

static void
update_last_stack_frame(struct fiber *fiber)
{
#ifdef ENABLE_BACKTRACE
	fiber->last_stack_frame = __builtin_frame_address(0);
#else
	(void)fiber;
#endif /* ENABLE_BACKTRACE */
}

void
fiber_call(struct fiber *callee, ...)
{
	struct fiber *caller = fiber();
	struct cord *cord = cord();

	assert(cord->sp + 1 - cord->stack < FIBER_CALL_STACK);
	assert(caller);

	cord->fiber = callee;
	*cord->sp++ = caller;

	update_last_stack_frame(caller);

	callee->csw++;

	callee->flags &= ~FIBER_READY;

	va_start(callee->f_data, callee);
	coro_transfer(&caller->coro.ctx, &callee->coro.ctx);
	va_end(callee->f_data);
}

void
fiber_checkstack()
{
	struct cord *cord = cord();
	if (cord->sp + 1 - cord->stack >= FIBER_CALL_STACK)
		tnt_raise(ClientError, ER_FIBER_STACK);
}

/** Interrupt a synchronous wait of a fiber inside the event loop.
 * We do so by keeping an "async" event in every fiber, solely
 * for this purpose, and raising this event here.
 */

void
fiber_wakeup(struct fiber *f)
{
	if (f->flags & FIBER_READY)
		return;
	f->flags |= FIBER_READY;
	struct cord *cord = cord();
	if (rlist_empty(&cord->ready_fibers))
		ev_async_send(cord->loop, &cord->ready_async);
	rlist_move_tail_entry(&cord->ready_fibers, f, state);
}

/** Cancel the subject fiber.
 *
 * Note: this is not guaranteed to succeed, and requires a level
 * of cooperation on behalf of the fiber. A fiber may opt to set
 * FIBER_CANCELLABLE to false, and never test that it was
 * cancelled.  Such fiber can not ever be cancelled, and
 * for such fiber this call will lead to an infinite wait.
 * However, fiber_testcancel() is embedded to the rest of fiber_*
 * API (@sa fiber_yield()), which makes most of the fibers that opt in,
 * cancellable.
 *
 * Currently cancellation can only be synchronous: this call
 * returns only when the subject fiber has terminated.
 *
 * The fiber which is cancelled, has FiberCancelException raised
 * in it. For cancellation to work, this exception type should be
 * re-raised whenever (if) it is caught.
 */

void
fiber_cancel(struct fiber *f)
{
	assert(f->fid != 0);

	f->flags |= FIBER_CANCEL;

	if (f == fiber()) {
		fiber_testcancel();
		return;
	}
	/*
	 * The subject fiber is passing through a wait
	 * point and can be kicked out of it right away.
	 */
	if (f->flags & FIBER_CANCELLABLE)
		fiber_call(f);

	if (f->fid) {
		/*
		 * The fiber is not dead. We have no other
		 * choice but wait for it to discover that
		 * it has been cancelled, and die.
		 */
		assert(f->waiter == NULL);
		f->waiter = fiber();
		fiber_yield();
	}
	/*
	 * Here we can't even check f->fid is 0 since
	 * f could have already been reused. Knowing
	 * at least that we can't get scheduled ourselves
	 * unless asynchronously woken up is somewhat a relief.
	 */
	fiber_testcancel(); /* Check if we're ourselves cancelled. */
}

bool
fiber_is_cancelled()
{
	return fiber()->flags & FIBER_CANCEL;
}

/** Test if this fiber is in a cancellable state and was indeed
 * cancelled, and raise an exception (FiberCancelException) if
 * that's the case.
 */
void
fiber_testcancel(void)
{
	/*
	 * Fiber can catch FiberCancelException using try..catch block in C or
	 * pcall()/xpcall() in Lua. However, FIBER_CANCEL flag is still set
	 * and the subject fiber will be killed by subsequent unprotected call
	 * of this function.
	 */
	if (fiber_is_cancelled())
		tnt_raise(FiberCancelException);
}



/** Change the current cancellation state of a fiber. This is not
 * a cancellation point.
 */
bool
fiber_setcancellable(bool enable)
{
	bool prev = fiber()->flags & FIBER_CANCELLABLE;
	if (enable == true)
		fiber()->flags |= FIBER_CANCELLABLE;
	else
		fiber()->flags &= ~FIBER_CANCELLABLE;
	return prev;
}

/**
 * @note: this is not a cancellation point (@sa fiber_testcancel())
 * but it is considered good practice to call testcancel()
 * after each yield.
 */
void
fiber_yield(void)
{
	struct cord *cord = cord();
	struct fiber *callee = *(--cord->sp);
	struct fiber *caller = cord->fiber;

	/** By convention, these triggers must not throw. */
	if (! rlist_empty(&caller->on_yield))
		trigger_run(&caller->on_yield, NULL);

	cord->fiber = callee;
	update_last_stack_frame(caller);

	callee->csw++;
	coro_transfer(&caller->coro.ctx, &callee->coro.ctx);
}


struct fiber_watcher_data {
	struct fiber *f;
	bool timed_out;
};

static void
fiber_schedule_timeout(ev_loop * /* loop */,
		       ev_timer *watcher, int revents)
{
	(void) revents;

	assert(fiber() == &cord()->sched);
	struct fiber_watcher_data *state =
			(struct fiber_watcher_data *) watcher->data;
	state->timed_out = true;
	fiber_call(state->f);
}

/**
 * @brief yield & check timeout
 * @return true if timeout exceeded
 */
bool
fiber_yield_timeout(ev_tstamp delay)
{
	struct ev_timer timer;
	ev_timer_init(&timer, fiber_schedule_timeout, delay, 0);
	struct fiber_watcher_data state = { fiber(), false };
	timer.data = &state;
	ev_timer_start(loop(), &timer);
	fiber_yield();
	ev_timer_stop(loop(), &timer);
	return state.timed_out;
}

void
fiber_yield_to(struct fiber *f)
{
	fiber_wakeup(f);
	fiber_yield();
	fiber_testcancel();
}

/**
 * @note: this is a cancellation point (@sa fiber_testcancel())
 */

void
fiber_sleep(ev_tstamp delay)
{
	fiber_yield_timeout(delay);
	fiber_testcancel();
}

/** Wait for a forked child to complete.
 * @note: this is a cancellation point (@sa fiber_testcancel()).
 * @return process return status
*/
void
fiber_schedule_child(ev_loop * /* loop */, ev_child *watcher, int event)
{
	return fiber_schedule((ev_watcher *) watcher, event);
}

int
wait_for_child(pid_t pid)
{
	assert(cord() == &main_cord);
	ev_child cw;
	ev_init(&cw, fiber_schedule_child);
	ev_child_set(&cw, pid, 0);
	cw.data = fiber();
	ev_child_start(loop(), &cw);
	fiber_yield();
	ev_child_stop(loop(), &cw);
	int status = cw.rstatus;
	fiber_testcancel();
	return status;
}

void
fiber_schedule(ev_watcher *watcher, int event __attribute__((unused)))
{
	assert(fiber() == &cord()->sched);
	fiber_call((struct fiber *) watcher->data);
}

static void
fiber_ready_async(ev_loop * /* loop */, ev_async *watcher, int revents)
{
	(void) watcher;
	(void) revents;
	struct cord *cord = cord();

	while (! rlist_empty(&cord->ready_fibers)) {
		struct fiber *f = rlist_first_entry(&cord->ready_fibers,
						    struct fiber, state);
		rlist_del_entry(f, state);
		fiber_call(f);
	}
}

struct fiber *
fiber_find(uint32_t fid)
{
	struct mh_i32ptr_t *fiber_registry = cord()->fiber_registry;
	mh_int_t k = mh_i32ptr_find(fiber_registry, fid, NULL);

	if (k == mh_end(fiber_registry))
		return NULL;
	return (struct fiber *) mh_i32ptr_node(fiber_registry, k)->val;
}

static void
register_fid(struct fiber *fiber)
{
	struct mh_i32ptr_node_t node = { fiber->fid, fiber };
	mh_i32ptr_put(cord()->fiber_registry, &node, NULL, NULL);
}

static void
unregister_fid(struct fiber *fiber)
{
	struct mh_i32ptr_node_t node = { fiber->fid, NULL };
	mh_i32ptr_remove(cord()->fiber_registry, &node, NULL);
}

void
fiber_gc(void)
{
	if (region_used(&fiber()->gc) < 128 * 1024) {
		region_reset(&fiber()->gc);
		return;
	}

	region_free(&fiber()->gc);
}


/** Destroy the currently active fiber and prepare it for reuse.
 */

static void
fiber_zombificate()
{
	struct fiber *fiber = fiber();
	if (fiber->waiter)
		fiber_wakeup(fiber->waiter);
	rlist_del(&fiber->state);
	fiber->waiter = NULL;
	fiber_set_name(fiber, "zombie");
	fiber->f = NULL;
#if !defined(NDEBUG)
	memset(fiber->fls, '#', sizeof(fiber->fls));
#endif /* !defined(NDEBUG) */
	unregister_fid(fiber);
	fiber->fid = 0;
	fiber->flags = 0;
	region_free(&fiber->gc);
	rlist_move_entry(&cord()->zombie_fibers, fiber, link);
}

static void
fiber_loop(void *data __attribute__((unused)))
{
	for (;;) {
		assert(fiber() != NULL && fiber()->f != NULL &&
		       fiber()->fid != 0);
		try {
			fiber()->f(fiber()->f_data);
		} catch (FiberCancelException *e) {
			say_info("fiber `%s' has been cancelled",
				 fiber_name(fiber()));
			say_info("fiber `%s': exiting", fiber_name(fiber()));
		} catch (Exception *e) {
			e->log();
		} catch (...) {
			/*
			 * This can only happen in case of a bug
			 * server bug.
			 */
			say_error("fiber `%s': unknown exception",
				fiber_name(fiber()));
			panic("fiber `%s': exiting", fiber_name(fiber()));
		}
		/** By convention, these triggers must not throw. */
		if (! rlist_empty(&fiber()->on_stop))
			trigger_run(&fiber()->on_stop, NULL);
		fiber_zombificate();
		fiber_yield();	/* give control back to scheduler */
	}
}

/** Set fiber name.
 *
 * @param[in] name the new name of the fiber. Truncated to
 * FIBER_NAME_MAXLEN.
*/

void
fiber_set_name(struct fiber *fiber, const char *name)
{
	assert(name != NULL);
	region_set_name(&fiber->gc, name);
}

extern inline void
fiber_set_key(struct fiber *fiber, enum fiber_key key, void *value);

extern inline void *
fiber_get_key(struct fiber *fiber, enum fiber_key key);

/**
 * Create a new fiber.
 *
 * Takes a fiber from fiber cache, if it's not empty.
 * Can fail only if there is not enough memory for
 * the fiber structure or fiber stack.
 *
 * The created fiber automatically returns itself
 * to the fiber cache when its "main" function
 * completes.
 */
struct fiber *
fiber_new(const char *name, void (*f) (va_list))
{
	struct cord *cord = cord();
	struct fiber *fiber = NULL;

	if (! rlist_empty(&cord->zombie_fibers)) {
		fiber = rlist_first_entry(&cord->zombie_fibers,
					  struct fiber, link);
		rlist_move_entry(&cord->fibers, fiber, link);
	} else {
		fiber = (struct fiber *) mempool_alloc(&cord->fiber_pool);
		memset(fiber, 0, sizeof(*fiber));

		tarantool_coro_create(&fiber->coro, fiber_loop, NULL);

		region_create(&fiber->gc, &cord->slabc);

		rlist_add_entry(&cord->fibers, fiber, link);
	}

	fiber->f = f;

	/* fids from 0 to 100 are reserved */
	if (++cord->max_fid < 100)
		cord->max_fid = 100;
	fiber->fid = cord->max_fid;
	rlist_create(&fiber->state);
	rlist_create(&fiber->on_yield);
	rlist_create(&fiber->on_stop);
	memset(fiber->fls, 0, sizeof(fiber->fls)); /* clear local storage */
	fiber->flags = 0;
	fiber->waiter = NULL;
	fiber_set_name(fiber, name);
	register_fid(fiber);

	return fiber;
}

/**
 * Free as much memory as possible taken by the fiber.
 *
 * @todo release memory allocated for
 * struct fiber and some of its members.
 */
void
fiber_destroy(struct fiber *f)
{
	if (f == fiber()) /* do not destroy running fiber */
		return;
	if (strcmp(fiber_name(f), "sched") == 0)
		return;

	trigger_destroy(&f->on_yield);
	trigger_destroy(&f->on_stop);
	rlist_del(&f->state);
	region_destroy(&f->gc);
	tarantool_coro_destroy(&f->coro);
}

void
fiber_destroy_all(struct cord *cord)
{
	struct fiber *f;
	rlist_foreach_entry(f, &cord->fibers, link)
		fiber_destroy(f);
	rlist_foreach_entry(f, &cord->zombie_fibers, link)
		fiber_destroy(f);
}

void
cord_create(struct cord *cord, const char *name)
{
	cord->id = pthread_self();
	cord->loop = cord->id == main_thread_id ?
		ev_default_loop(EVFLAG_AUTO) : ev_loop_new(EVFLAG_AUTO);
	slab_cache_create(&cord->slabc, &runtime, 0);
	mempool_create(&cord->fiber_pool, &cord->slabc,
		       sizeof(struct fiber));
	rlist_create(&cord->fibers);
	rlist_create(&cord->ready_fibers);
	rlist_create(&cord->zombie_fibers);
	cord->fiber_registry = mh_i32ptr_new();

	cord->sched.fid = 1;
	cord->fiber = &cord->sched;
	region_create(&cord->sched.gc, &cord->slabc);
	fiber_set_name(&cord->sched, "sched");

	cord->sp = cord->stack;
	cord->max_fid = 100;

	ev_async_init(&cord->ready_async, fiber_ready_async);
	ev_async_start(cord->loop, &cord->ready_async);
	snprintf(cord->name, sizeof(cord->name), "%s", name);
}

void
cord_destroy(struct cord *cord)
{
	ev_async_stop(cord->loop, &cord->ready_async);
	/* Only clean up if initialized. */
	if (cord->fiber_registry) {
		fiber_destroy_all(cord);
		mh_i32ptr_delete(cord->fiber_registry);
	}
	slab_cache_destroy(&cord->slabc);
	ev_loop_destroy(cord->loop);
	/* Cleanup memory allocated for exceptions */
	if (cord->exception && cord->exception != &out_of_memory) {
		cord->exception->~Exception();
		free(cord->exception);
	}
}

struct cord_thread_arg
{
	struct cord *cord;
	const char *name;
	void *(*f)(void *);
	void *arg;
	bool is_started;
	pthread_mutex_t start_mutex;
	pthread_cond_t start_cond;
};

void *cord_thread_func(void *p)
{
	struct cord_thread_arg *ct_arg = (struct cord_thread_arg *) p;
	struct cord *cord = cord() = ct_arg->cord;
	cord_create(cord, ct_arg->name);
	tt_pthread_mutex_lock(&ct_arg->start_mutex);
	void *(*f)(void *) = ct_arg->f;
	void *arg = ct_arg->arg;
	ct_arg->is_started = true;
	tt_pthread_cond_signal(&ct_arg->start_cond);
	tt_pthread_mutex_unlock(&ct_arg->start_mutex);
	return f(arg);
}

int
cord_start(struct cord *cord, const char *name, void *(*f)(void *), void *arg)
{
	int res = -1;
	struct cord_thread_arg ct_arg = { cord, name, f, arg, false,
		PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER };
	tt_pthread_mutex_lock(&ct_arg.start_mutex);
	if (tt_pthread_create(&cord->id, NULL, cord_thread_func, &ct_arg))
		goto end;
	res = 0;
	while (! ct_arg.is_started)
		tt_pthread_cond_wait(&ct_arg.start_cond, &ct_arg.start_mutex);
end:
	tt_pthread_mutex_unlock(&ct_arg.start_mutex);
	tt_pthread_mutex_destroy(&ct_arg.start_mutex);
	tt_pthread_cond_destroy(&ct_arg.start_cond);
	return res;
}

int
cord_join(struct cord *cord)
{
	int res = 0;
	if (tt_pthread_join(cord->id, NULL)) {
		/* We can't recover from this in any reasonable way. */
		say_syserror("%s: thread join failed", cord->name);
		res = -1;
	}
	cord_destroy(cord);
	return res;
}

void
fiber_init(void)
{
	main_thread_id = pthread_self();
	cord() = &main_cord;
	cord_create(cord(), "main");
}

void
fiber_free(void)
{
	cord_destroy(cord());
}

int fiber_stat(fiber_stat_cb cb, void *cb_ctx)
{
	struct fiber *fiber;
	struct cord *cord = cord();
	int res;
	rlist_foreach_entry(fiber, &cord->fibers, link) {
		res = cb(fiber, cb_ctx);
		if (res != 0)
			return res;
	}
	rlist_foreach_entry(fiber, &cord->zombie_fibers, link) {
		res = cb(fiber, cb_ctx);
		if (res != 0)
			return res;
	}
	return 0;
}
