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
#include "fiber.h"

#include <trivia/config.h>
#include <trivia/util.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pmatomic.h>
#include <tarantool_ev.h>

#include "assoc.h"
#include "memory.h"
#include "trigger.h"
#include "errinj.h"
#include "clock.h"
#include "tt_sigaction.h"
#include "tt_static.h"

extern void cord_on_yield(void);

static struct fiber_slice zero_slice = {.warn = 0.0, .err = 0.0};

/**
 * ASAN build is slow. Turn slice check off to suppress noisy failures
 * on exceeding slice limit in this case.
 */
#if ENABLE_ASAN
static struct fiber_slice default_slice = {
	.warn = TIMEOUT_INFINITY,
	.err = TIMEOUT_INFINITY,
};
#else
static struct fiber_slice default_slice = {.warn = 0.5, .err = 1.0};
#endif

/** Number of cord-threads still running right now. */
static int cord_count = 0;

static inline void
clock_stat_add_delta(struct clock_stat *stat, uint64_t clock_delta)
{
	stat->delta += clock_delta;
}

/**
 * Calculate the exponential moving average for the clock deltas
 * per loop iteration. The coeffitient is 1/16.
 */
static inline uint64_t
clock_diff_accumulate(uint64_t acc, uint64_t delta)
{
	return delta / 16 + 15 * acc / 16;
}

static inline void
clock_stat_update(struct clock_stat *stat, double nsec_per_clock)
{
	stat->acc = clock_diff_accumulate(stat->acc, stat->delta);
	stat->prev_delta = stat->delta;
	stat->cputime += stat->delta * nsec_per_clock;
	stat->delta = 0;
}

static inline void
clock_stat_reset(struct clock_stat *stat)
{
	stat->acc = 0;
	stat->delta = 0;
	stat->prev_delta = 0;
	stat->cputime = 0;
}

static void
cpu_stat_start(struct cpu_stat *stat)
{
	stat->prev_clock = clock_monotonic64();
	/*
	 * We want to measure thread cpu time here to calculate
	 * each fiber's cpu time, so don't use libev's ev_now() or
	 * ev_time() since they use either monotonic or realtime
	 * system clocks.
	 */
	stat->prev_cputime = clock_thread64();
}

static inline void
cpu_stat_reset(struct cpu_stat *stat)
{
	cpu_stat_start(stat);
}

static uint64_t
cpu_stat_on_csw(struct cpu_stat *stat)
{
	uint64_t delta, clock = clock_monotonic64();
	/*
	 * Just in case. On Linux CLOCK_MONOTONIC guarantee that the
	 * time returned by consecutive calls to clock_gettime will not
	 * go backwards, however for other systems it might not be true.
	 */
	if (clock < stat->prev_clock)
		delta = 0;
	else
		delta = clock - stat->prev_clock;

	stat->prev_clock = clock;
	return delta;
}

static double
cpu_stat_end(struct cpu_stat *stat, struct clock_stat *cord_clock_stat)
{
	double nsec_per_clock = 0;
	uint64_t delta_time = clock_thread64();
	if (delta_time > stat->prev_cputime && cord_clock_stat->delta > 0) {
		delta_time -= stat->prev_cputime;
		nsec_per_clock = (double)delta_time / cord()->clock_stat.delta;
	}
	return nsec_per_clock;
}

#include <valgrind/memcheck.h>

static int (*fiber_invoke)(fiber_func f, va_list ap);

#if ENABLE_ASAN
#include <sanitizer/asan_interface.h>

#define ASAN_START_SWITCH_FIBER(fake_stack_save, will_switch_back, bottom,     \
				size)					       \
	/*								       \
	 * When leaving a fiber definitely, NULL must be passed as the first   \
	 * argument so that the fake stack is destroyed.		       \
	 */								       \
	void *fake_stack_save = NULL;					       \
	__sanitizer_start_switch_fiber((will_switch_back) ? &fake_stack_save   \
							  : NULL,	       \
                                       (bottom), (size))
#if ASAN_INTERFACE_OLD
#define ASAN_FINISH_SWITCH_FIBER(fake_stack_save) \
	__sanitizer_finish_switch_fiber(fake_stack_save)
#else
#define ASAN_FINISH_SWITCH_FIBER(fake_stack_save) \
	__sanitizer_finish_switch_fiber(fake_stack_save, NULL, NULL)
#endif

#else
#define ASAN_START_SWITCH_FIBER(fake_stack_save, will_switch_back, bottom, size)
#define ASAN_FINISH_SWITCH_FIBER(fake_stack_save)
#endif

static inline int
fiber_madvise(void *addr, size_t len, int advice)
{
	int rc = 0;

	ERROR_INJECT(ERRINJ_FIBER_MADVISE, {
		errno = ENOMEM;
		rc = -1;
	});

	if (rc != 0 || madvise(addr, len, advice) != 0) {
		diag_set(SystemError, "fiber madvise failed");
		return -1;
	}
	return 0;
}

static inline int
fiber_mprotect(void *addr, size_t len, int prot)
{
	(void)addr;
	(void)len;
	(void)prot;
	struct errinj *inj = errinj(ERRINJ_FIBER_MPROTECT, ERRINJ_INT);
	if (inj != NULL && inj->iparam == prot) {
		errno = ENOMEM;
		goto error;
	}
/*
 * TODO(gh-8423) Disable mprotect temporarily. Leak sanitizer does not work
 * well if memory is protected. We fail to remove protection due to the use of
 * `cord_cancel_and_join` to cancel cords.
 */
#ifndef ENABLE_ASAN
	if (mprotect(addr, len, prot) != 0)
		goto error;
#endif
	return 0;
error:
	diag_set(SystemError, "fiber mprotect failed");
	return -1;
}

static __thread bool fiber_top_enabled = false;

#ifdef ENABLE_BACKTRACE
#ifndef NDEBUG
bool fiber_leak_backtrace_enable = true;
#else
bool fiber_leak_backtrace_enable = false;
#endif
#endif

#ifdef ABORT_ON_LEAK
bool fiber_abort_on_gc_leak = true;
#else
bool fiber_abort_on_gc_leak = false;
#endif

#ifdef ENABLE_BACKTRACE
static __thread bool fiber_parent_backtrace_enabled;
#endif /* ENABLE_BACKTRACE */

/**
 * An action performed each time a context switch happens.
 * Used to count each fiber's processing time.
 */
static inline void
clock_set_on_csw(struct fiber *caller)
{
	caller->csw++;

	if (!fiber_top_enabled)
		return;

	uint64_t delta = cpu_stat_on_csw(&cord()->cpu_stat);

	clock_stat_add_delta(&cord()->clock_stat, delta);
	clock_stat_add_delta(&caller->clock_stat, delta);
}

/*
 * Defines a handler to be executed on exit from cord's thread func,
 * accessible via cord()->on_exit (normally NULL). It is used to
 * implement cord_cojoin.
 */
struct cord_on_exit {
	void (*callback)(void*);
	void *argument;
};

/*
 * A special value distinct from any valid pointer to cord_on_exit
 * structure AND NULL. This value is stored in cord()->on_exit by the
 * thread function prior to thread termination.
 */
static const struct cord_on_exit cord_on_exit_sentinel = { NULL, NULL };
#define CORD_ON_EXIT_WONT_RUN (&cord_on_exit_sentinel)

static struct cord main_cord;
__thread struct cord *cord_ptr = NULL;
pthread_t main_thread_id;

static size_t page_size;
static int stack_direction;

#ifndef FIBER_STACK_SIZE_DEFAULT
#error "Default fiber stack size is not set"
#endif

enum {
	/* The minimum allowable fiber stack size in bytes */
	FIBER_STACK_SIZE_MINIMAL = 16384,
	/* Stack size watermark in bytes. */
	FIBER_STACK_SIZE_WATERMARK = 65536,
};

/** Default fiber attributes */
static const struct fiber_attr fiber_attr_default = {
       .stack_size = FIBER_STACK_SIZE_DEFAULT,
       .flags = FIBER_DEFAULT_FLAGS
};

#ifdef HAVE_MADV_DONTNEED
/*
 * Random values generated with uuid.
 * Used for stack poisoning.
 */
static const uint64_t poison_pool[] = {
	0x74f31d37285c4c37, 0xb10269a05bf10c29,
	0x0994d845bd284e0f, 0x9ffd4f7129c184df,
	0x357151e6711c4415, 0x8c5e5f41aafe6f28,
	0x6917dd79e78049d5, 0xba61957c65ca2465,
};

/*
 * We poison by 8 bytes as it's natural for stack
 * step on x86-64. Also 128 byte gap between poison
 * values should cover common cases.
 */
#define POISON_SIZE	(sizeof(poison_pool) / sizeof(poison_pool[0]))
#define POISON_OFF	(128 / sizeof(poison_pool[0]))

#endif /* HAVE_MADV_DONTNEED */

void
fiber_attr_create(struct fiber_attr *fiber_attr)
{
       *fiber_attr = fiber_attr_default;
}

struct fiber_attr *
fiber_attr_new(void)
{
	struct fiber_attr *fiber_attr = malloc(sizeof(*fiber_attr));
	if (fiber_attr == NULL)  {
		diag_set(OutOfMemory, sizeof(*fiber_attr),
			 "runtime", "fiber attr");
		return NULL;
	}
	fiber_attr_create(fiber_attr);
	return fiber_attr;
}

void
fiber_attr_delete(struct fiber_attr *fiber_attr)
{
	free(fiber_attr);
}

int
fiber_attr_setstacksize(struct fiber_attr *fiber_attr, size_t stack_size)
{
	if (stack_size < FIBER_STACK_SIZE_MINIMAL) {
		errno = EINVAL;
		diag_set(SystemError, "stack size is too small");
		return -1;
	}
	fiber_attr->stack_size = stack_size;
	if (stack_size != FIBER_STACK_SIZE_DEFAULT) {
		fiber_attr->flags |= FIBER_CUSTOM_STACK;
	} else {
		fiber_attr->flags &= ~FIBER_CUSTOM_STACK;
	}
	return 0;
}

size_t
fiber_attr_getstacksize(struct fiber_attr *fiber_attr)
{
	return fiber_attr != NULL ? fiber_attr->stack_size :
				    fiber_attr_default.stack_size;
}

void
fiber_on_stop(struct fiber *f)
{
	/*
	 * The most common case is when the list is empty. Do an
	 * inlined check before calling trigger_run().
	 */
	if (rlist_empty(&f->on_stop))
		return;
	if (trigger_run(&f->on_stop, f) != 0)
		panic("On_stop triggers can't fail");
	/*
	 * All on_stop triggers are supposed to remove themselves.
	 * So as no to waste time on that here, and to make them
	 * all work uniformly.
	 */
	assert(rlist_empty(&f->on_stop));
}

static void
fiber_recycle(struct fiber *fiber);

static void
fiber_stack_recycle(struct fiber *fiber);

static void
fiber_delete(struct cord *cord, struct fiber *f);

/**
 * Try to delete a fiber right now or later if can't do now. The latter happens
 * for self fiber - can't delete own stack.
 */
static void
cord_add_garbage(struct cord *cord, struct fiber *f);

/**
 * Reset slice for currently running fiber.
 * The function is used on yields by main cord.
 */
static inline void
cord_reset_slice(struct fiber *f)
{
	assert(cord_is_main());
	cord()->call_time = clock_lowres_monotonic();
	struct fiber_slice new_slice = cord()->max_slice;
	if ((f->flags & FIBER_CUSTOM_SLICE) != 0)
		new_slice = f->max_slice;
	cord()->slice = new_slice;
}

/**
 * True if a fiber with `fiber_flags` can be reused.
 * A fiber can not be reused if it is somehow non-standard.
 */
static bool
fiber_is_reusable(uint32_t fiber_flags)
{
	/* For now we can not reuse fibers with custom stack size. */
	return (fiber_flags & FIBER_CUSTOM_STACK) == 0;
}

/**
 * Transfer control to callee fiber.
 */
static void
fiber_call_impl(struct fiber *callee)
{
	struct fiber *caller = fiber();
	struct cord *cord = cord();

	/* Ensure we aren't switching to a fiber parked in fiber_loop */
	assert(callee->f != NULL && callee->fid != 0);
	assert(callee->flags & FIBER_IS_READY || callee == &cord->sched);
	assert(! (callee->flags & FIBER_IS_DEAD));
	/*
	 * Ensure the callee was removed from cord->ready list.
	 * If it wasn't, the callee will observe a 'spurious' wakeup
	 * later, due to a fiber_wakeup() performed in the past.
	 */
	assert(rlist_empty(&callee->state));
	assert(caller);
	assert(caller != callee);
	assert((caller->flags & FIBER_IS_RUNNING) != 0);
	assert((callee->flags & FIBER_IS_RUNNING) == 0);

	caller->flags &= ~FIBER_IS_RUNNING;
	cord->fiber = callee;
	callee->flags = (callee->flags & ~FIBER_IS_READY) | FIBER_IS_RUNNING;

	if (cord_is_main())
		cord_reset_slice(callee);

	ASAN_START_SWITCH_FIBER(asan_state, 1,
				callee->stack,
				callee->stack_size);
	coro_transfer(&caller->ctx, &callee->ctx);
	ASAN_FINISH_SWITCH_FIBER(asan_state);
}

void
fiber_call(struct fiber *callee)
{
	struct fiber *caller = fiber();
	assert(! (caller->flags & FIBER_IS_READY));
	assert(rlist_empty(&callee->state));
	assert(! (callee->flags & FIBER_IS_READY));

	/** By convention, these triggers must not throw. */
	if (! rlist_empty(&caller->on_yield))
		trigger_run(&caller->on_yield, NULL);

	if (cord_is_main())
		cord_on_yield();

	clock_set_on_csw(caller);
	callee->caller = caller;
	callee->flags |= FIBER_IS_READY;
	caller->flags |= FIBER_IS_READY;
	fiber_call_impl(callee);
}

void
fiber_start(struct fiber *callee, ...)
{
	va_start(callee->f_data, callee);
	fiber_call(callee);
	va_end(callee->f_data);
}

bool
fiber_checkstack(void)
{
	return false;
}

static void
fiber_make_ready(struct fiber *f)
{
	/**
	 * Do nothing if the fiber is already in cord->ready
	 * list *or* is in the call chain created by
	 * fiber_schedule_list(). While it's harmless to re-add
	 * a fiber to cord->ready, even if it's already there,
	 * but the same game is deadly when the fiber is in
	 * the callee list created by fiber_schedule_list().
	 *
	 * To put it another way, fiber_make_ready() is a 'request' to
	 * schedule the fiber for execution, and once it is executing
	 * the 'make ready' request is considered complete and it must be
	 * removed.
	 */
	assert((f->flags & (FIBER_IS_DEAD | FIBER_IS_READY)) == 0);
	struct cord *cord = cord();
	if (rlist_empty(&cord->ready)) {
		/*
		 * ev_feed_event(EV_CUSTOM) gets scheduled in the
		 * same event loop iteration, and we rely on this
		 * for quick scheduling. For a wakeup which
		 * actually can invoke a poll() in libev,
		 * use fiber_sleep(0)
		 */
		ev_feed_event(cord->loop, &cord->wakeup_event, EV_CUSTOM);
	}
	/**
	 * Removes the fiber from whatever wait list it is on.
	 *
	 * It's critical that the newly scheduled fiber is
	 * added to the tail of the list, to preserve correct
	 * transaction commit order after a successful WAL write.
	 * (see tx_schedule_queue() in box/wal.c)
	 */
	rlist_move_tail_entry(&cord->ready, f, state);
	f->flags |= FIBER_IS_READY;
}

void
fiber_set_ctx(struct fiber *f, void *f_arg)
{
	f->f_arg = f_arg;
}

void *
fiber_get_ctx(struct fiber *f)
{
	return f->f_arg;
}

void
fiber_wakeup(struct fiber *f)
{
	/*
	 * DEAD fiber can be lingering in the cord fiber list
	 * if it is joinable. And once its execution is complete
	 * it should be reaped with fiber_join() call.
	 *
	 * Still our API allows to call fiber_wakeup() on dead
	 * joinable fibers so simply ignore it.
	 */
	assert((f->flags & FIBER_IS_DEAD) == 0 ||
	       (f->flags & FIBER_IS_JOINABLE) != 0);
	const int no_flags = FIBER_IS_READY | FIBER_IS_DEAD | FIBER_IS_RUNNING;
	if ((f->flags & no_flags) == 0)
		fiber_make_ready(f);
}

void
fiber_cancel(struct fiber *f)
{
	/**
	 * Do nothing if the fiber is dead, since cancelling
	 * the fiber would clear the diagnostics area and
	 * the cause of death would be lost.
	 */
	if (fiber_is_dead(f)) {
		if ((f->flags & FIBER_IS_JOINABLE) == 0) {
			panic("Cancel of a finished and already "
			      "recycled fiber");
		}
		assert(f->fid != 0);
		return;
	}

	f->flags |= FIBER_IS_CANCELLED;
	fiber_wakeup(f);
}

bool
fiber_set_cancellable(MAYBE_UNUSED bool yesno)
{
	return true;
}

bool
fiber_is_cancelled(void)
{
	return fiber()->flags & FIBER_IS_CANCELLED;
}

void
fiber_set_joinable(struct fiber *fiber, bool yesno)
{
	if (yesno == true)
		fiber->flags |= FIBER_IS_JOINABLE;
	else
		fiber->flags &= ~FIBER_IS_JOINABLE;
}

/** Report libev time (cheap). */
double
fiber_time(void)
{
	return ev_now(loop());
}

int64_t
fiber_time64(void)
{
	return (int64_t)(ev_now(loop()) * 1000000 + 0.5);
}

double
fiber_clock(void)
{
	return ev_monotonic_now(loop());
}

int64_t
fiber_clock64(void)
{
	return (int64_t)(ev_monotonic_now(loop()) * 1000000 + 0.5);
}

/**
 * Move current fiber to the end of ready fibers list and switch to next
 */
void
fiber_reschedule(void)
{
	struct fiber *f = fiber();
	/*
	 * The current fiber can't be dead, the flag is set when the fiber
	 * function returns. Can't be ready, because such status is assigned
	 * only to the queued fibers in the ready-list.
	 */
	assert((f->flags & (FIBER_IS_READY | FIBER_IS_DEAD)) == 0);
	fiber_make_ready(f);
	fiber_yield();
}

int
fiber_join(struct fiber *fiber)
{
	return fiber_join_timeout(fiber, TIMEOUT_INFINITY);
}

bool
fiber_wait_on_deadline(struct fiber *fiber, double deadline)
{
	rlist_add_tail_entry(&fiber->wake, fiber(), state);

	return fiber_yield_deadline(deadline);
}

int
fiber_join_timeout(struct fiber *fiber, double timeout)
{
	if ((fiber->flags & FIBER_IS_JOINABLE) == 0)
		panic("the fiber is not joinable");

	if (!fiber_is_dead(fiber)) {
		double deadline = fiber_clock() + timeout;
		while (!fiber_wait_on_deadline(fiber, deadline) &&
		       !fiber_is_dead(fiber)) {
		}
		if (!fiber_is_dead(fiber)) {
			/*
			 * Not exactly the right error message for this place.
			 * Error message is generated based on the ETIMEDOUT
			 * code, that is used for network timeouts in linux. But
			 * in other places, this type of error is always used
			 * when the timeout expires, regardless of whether it is
			 * related to the network (see cbus_call for example).
			 */
			diag_set(TimedOut);
			return -1;
		}
	}
	assert((fiber->flags & FIBER_IS_RUNNING) == 0);
	assert((fiber->flags & FIBER_IS_JOINABLE) != 0);
	fiber->flags &= ~FIBER_IS_JOINABLE;

	/* Move exception to the caller */
	int ret = fiber->f_ret;
	if (ret != 0) {
		assert(!diag_is_empty(&fiber->diag));
		diag_move(&fiber->diag, &fiber()->diag);
	}
	/* The fiber is already dead. */
	fiber_recycle(fiber);
	return ret;
}

/**
 * Implementation of `fiber_yield()` and `fiber_yield_final()`.
 * `will_switch_back` argument is used only by ASAN.
 */
static void
fiber_yield_impl(MAYBE_UNUSED bool will_switch_back)
{
	struct cord *cord = cord();
	struct fiber *caller = cord->fiber;
	struct fiber *callee = caller->caller;
	caller->caller = &cord->sched;

	/** By convention, these triggers must not throw. */
	if (! rlist_empty(&caller->on_yield))
		trigger_run(&caller->on_yield, NULL);

	if (cord_is_main()) {
		cord_on_yield();
		cord_reset_slice(callee);
	}

	clock_set_on_csw(caller);

	assert(callee->flags & FIBER_IS_READY || callee == &cord->sched);
	assert(! (callee->flags & FIBER_IS_DEAD));
	assert((caller->flags & FIBER_IS_RUNNING) != 0);
	assert((callee->flags & FIBER_IS_RUNNING) == 0);

	caller->flags &= ~FIBER_IS_RUNNING;
	cord->fiber = callee;
	callee->flags = (callee->flags & ~FIBER_IS_READY) | FIBER_IS_RUNNING;

	ASAN_START_SWITCH_FIBER(asan_state, will_switch_back, callee->stack,
				callee->stack_size);
	coro_transfer(&caller->ctx, &callee->ctx);
	ASAN_FINISH_SWITCH_FIBER(asan_state);
}

void
fiber_yield(void)
{
	fiber_yield_impl(true);
}

/**
 * Like `fiber_yield()`, but should be used when this is the last switch from
 * a dead fiber to the scheduler.
 */
static void
fiber_yield_final(void)
{
	fiber_yield_impl(false);
}

struct fiber_watcher_data {
	struct fiber *f;
	bool timed_out;
};

static void
fiber_schedule_timeout(ev_loop *loop,
		       ev_timer *watcher, int revents)
{
	(void) loop;
	(void) revents;

	assert(fiber() == &cord()->sched);
	struct fiber_watcher_data *state =
			(struct fiber_watcher_data *) watcher->data;
	state->timed_out = true;
	fiber_wakeup(state->f);
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

bool
fiber_yield_deadline(ev_tstamp deadline)
{
	ev_tstamp timeout = deadline - ev_monotonic_now(loop());
	return fiber_yield_timeout(timeout);
}

/**
 * Yield the current fiber to events in the event loop.
 */
void
fiber_sleep(double delay)
{
	/*
	 * libev sleeps at least backend_mintime, which is 1 ms in
	 * case of poll()/Linux, unless there are idle watchers.
	 * So, to properly implement fiber_sleep(0), i.e. a sleep
	 * with a zero timeout, we set up an idle watcher, and
	 * it triggers libev to poll() with zero timeout.
	 */
	if (delay == 0) {
		ev_idle_start(loop(), &cord()->idle_event);
	}
	fiber_yield_timeout(delay);

	if (delay == 0) {
		ev_idle_stop(loop(), &cord()->idle_event);
	}
}

void
fiber_schedule_cb(ev_loop *loop, ev_watcher *watcher, int revents)
{
	(void) loop;
	(void) revents;
	struct fiber *fiber = watcher->data;
	assert(fiber() == &cord()->sched);
	fiber_wakeup(fiber);
}

static inline void
fiber_schedule_list(struct rlist *list)
{
	struct fiber *first;
	struct fiber *last;

	/*
	 * Happens when a fiber exits and is removed from cord->ready
	 * resulting in the empty list.
	 */
	if (rlist_empty(list))
		return;

	first = last = rlist_shift_entry(list, struct fiber, state);
	assert(last->flags & FIBER_IS_READY);

	while (! rlist_empty(list)) {
		last->caller = rlist_shift_entry(list, struct fiber, state);
		last = last->caller;
		assert(last->flags & FIBER_IS_READY);
	}
	last->caller = fiber();
	assert(fiber() == &cord()->sched);
	clock_set_on_csw(fiber());
	fiber_call_impl(first);
}

static void
fiber_schedule_wakeup(ev_loop *loop, ev_async *watcher, int revents)
{
	(void) loop;
	(void) watcher;
	(void) revents;
	struct cord *cord = cord();
	fiber_check_gc();
	fiber_schedule_list(&cord->ready);
}

static void
fiber_schedule_idle(ev_loop *loop, ev_idle *watcher,
		    int revents)
{
	(void) loop;
	(void) watcher;
	(void) revents;
}


struct fiber *
fiber_find(uint64_t fid)
{
	struct mh_i64ptr_t *fiber_registry = cord()->fiber_registry;
	mh_int_t k = mh_i64ptr_find(fiber_registry, fid, NULL);

	if (k == mh_end(fiber_registry))
		return NULL;
	return mh_i64ptr_node(fiber_registry, k)->val;
}

static void
register_fid(struct fiber *fiber)
{
	struct mh_i64ptr_node_t node = { fiber->fid, fiber };
	mh_i64ptr_put(cord()->fiber_registry, &node, NULL, NULL);
}

static void
unregister_fid(struct fiber *fiber)
{
	struct mh_i64ptr_node_t node = { fiber->fid, NULL };
	mh_i64ptr_remove(cord()->fiber_registry, &node, NULL);
}

struct fiber *
fiber_self(void)
{
	return fiber();
}

/** Common part of fiber_new() and fiber_recycle(). */
static void
fiber_reset(struct fiber *fiber)
{
	rlist_create(&fiber->on_yield);
	rlist_create(&fiber->on_stop);
	clock_stat_reset(&fiber->clock_stat);
}

/** Destroy an active fiber and prepare it for reuse or delete it. */
static void
fiber_recycle(struct fiber *fiber)
{
	assert((fiber->flags & FIBER_IS_DEAD) != 0);
	/* no exceptions are leaking */
	assert(diag_is_empty(&fiber->diag));
	/* no pending wakeup */
	assert(rlist_empty(&fiber->state));
	fiber_stack_recycle(fiber);
	fiber_reset(fiber);
	fiber->name[0] = '\0';
	fiber->f = NULL;
	fiber->wait_pad = NULL;
	memset(&fiber->storage, 0, sizeof(fiber->storage));
	fiber->storage.lua.storage_ref = FIBER_LUA_NOREF;
	fiber->storage.lua.fid_ref = FIBER_LUA_NOREF;
	unregister_fid(fiber);
	fiber->fid = 0;
	fiber->gc_initial_size = 0;
#ifdef ENABLE_BACKTRACE
	fiber->parent_bt = NULL;
	fiber->first_alloc_bt = NULL;
	region_set_callbacks(&fiber->gc, NULL, NULL, NULL);
#endif
	region_free(&fiber->gc);
	if (fiber_is_reusable(fiber->flags)) {
		rlist_move_entry(&cord()->dead, fiber, link);
	} else {
		cord_add_garbage(cord(), fiber);
	}
}

#ifdef ENABLE_BACKTRACE
/**
 * Called on allocation on region gc. Saves allocation caller backtrace
 * to report it later if region gc leak is found.
 */
static void
fiber_on_gc_alloc(struct region *region, size_t size, void *opaque)
{
	(void)region;
	(void)size;
	struct fiber *fiber = opaque;

	assert(region_used(&fiber->gc) >= fiber->gc_initial_size);
	if (region_used(&fiber->gc) == fiber->gc_initial_size) {
		/* 1 is to skip not interesting frames. */
		backtrace_collect(fiber->first_alloc_bt, NULL, 1);
		assert(fiber->first_alloc_bt->frame_count > 0);
	}
}

/**
 * Called on region gc truncation. Resets previously saved backtrace so
 * it won't be falsely reported in case of programmatic mistake.
 */
static void
fiber_on_gc_truncate(struct region *region, size_t used, void *opaque)
{
	(void)region;
	struct fiber *fiber = opaque;

	assert(used >= fiber->gc_initial_size);
	if (used == fiber->gc_initial_size)
		fiber->first_alloc_bt->frame_count = 0;
}
#endif /* ENABLE_BACKTRACE */

void
fiber_check_gc(void)
{
	struct fiber *fiber = fiber();

	assert(region_used(&fiber->gc) >= fiber->gc_initial_size);
	if (region_used(&fiber->gc) == fiber->gc_initial_size)
		return;

#ifdef ENABLE_BACKTRACE
	if (fiber->first_alloc_bt) {
		char *buf = tt_static_buf();
		int rc = snprintf(buf, TT_STATIC_BUF_LEN,
				  "Fiber gc leak is found. "
				  "First leaked fiber gc allocation"
				  " backtrace:\n");
		assert(rc > 0 && rc < TT_STATIC_BUF_LEN);
		backtrace_snprint(buf + rc, TT_STATIC_BUF_LEN - rc,
				  fiber->first_alloc_bt);
		say_error("%s", buf);
	} else {
		say_error("Fiber gc leak is found. "
			  "Leak backtrace is not available. "
			  "Make sure fiber.leak_backtrace_enable() is called"
			  " before starting this fiber to obtain "
			  " the backtrace.");
	}
#else
	say_error("Fiber gc leak is found. "
		  "Leak backtrace is not available on your platform.");
#endif

	if (fiber_abort_on_gc_leak)
		abort();
}

static void
fiber_loop(MAYBE_UNUSED void *data)
{
	ASAN_FINISH_SWITCH_FIBER(NULL);
	for (;;) {
		struct fiber *fiber = fiber();

		assert(fiber != NULL && fiber->f != NULL && fiber->fid != 0);
		fiber->f_ret = fiber_invoke(fiber->f, fiber->f_data);
		fiber_check_gc();
		if (fiber->f_ret != 0) {
			struct error *e = diag_last_error(&fiber->diag);
			/* diag must not be empty on error */
			assert(e != NULL || fiber->flags & FIBER_IS_CANCELLED);
			/*
			 * For joinable fibers, it's the business
			 * of the caller to deal with the error.
			 */
			if (!(fiber->flags & FIBER_IS_JOINABLE)) {
				if (!(fiber->flags & FIBER_IS_CANCELLED))
					error_log(e);
				diag_clear(&fiber()->diag);
			}
		} else {
			/*
			 * Make sure a leftover exception does not
			 * propagate up to the joiner.
			 */
			diag_clear(&fiber()->diag);
		}
		fiber->flags |= FIBER_IS_DEAD;
		while (! rlist_empty(&fiber->wake)) {
		       struct fiber *f;
		       f = rlist_shift_entry(&fiber->wake, struct fiber,
					     state);
		       assert(f != fiber);
		       fiber_wakeup(f);
	        }
		fiber_on_stop(fiber);
		/* reset pending wakeups */
		rlist_del(&fiber->state);
		if (! (fiber->flags & FIBER_IS_JOINABLE))
			fiber_recycle(fiber);
		/*
		 * Crash if spurious wakeup happens, don't call the old
		 * function again, ap is garbage by now.
		 */
		fiber->f = NULL;
		/*
		 * Give control back to the scheduler.
		 * If the fiber is not reusable, this is its final yield.
		 */
		if (fiber_is_reusable(fiber->flags))
			fiber_yield();
		else
			fiber_yield_final();
	}
}

void
fiber_set_name(struct fiber *fiber, const char *name)
{
	size_t size = strlen(name) + 1;
	if (size <= FIBER_NAME_INLINE) {
		if (fiber->name != fiber->inline_name) {
			free(fiber->name);
			fiber->name = fiber->inline_name;
		}
	} else {
		if (size > FIBER_NAME_MAX)
			size = FIBER_NAME_MAX;
		char *new_name;
		if (fiber->name != fiber->inline_name)
			new_name = realloc(fiber->name, size);
		else
			new_name = malloc(size);
		if (new_name == NULL)
			panic("fiber_set_name() failed with OOM");
		fiber->name = new_name;
	}
	--size;
	memcpy(fiber->name, name, size);
	fiber->name[size] = 0;
}

static inline void *
page_align_down(void *ptr)
{
	return (void *)((intptr_t)ptr & ~(page_size - 1));
}

static inline void *
page_align_up(void *ptr)
{
	return page_align_down(ptr + page_size - 1);
}

/**
 * Call madvise(2) on given range but align start on page up and
 * end on page down.
 *
 * This way madvise(2) requirement on alignment of start address is met.
 * Also we won't touch memory after end due to rounding up of range length.
 */
static inline int
fiber_madvise_unaligned(void *start, void *end, int advice)
{
	start = page_align_up(start);
	end = page_align_down(end);
	return fiber_madvise(start, (char *)end - (char *)start, advice);
}

#ifdef HAVE_MADV_DONTNEED
/**
 * Check if stack poison values are present starting from
 * the address provided.
 */
static bool
stack_has_watermark(void *addr)
{
	const uint64_t *src = poison_pool;
	const uint64_t *dst = addr;
	size_t i;

	for (i = 0; i < POISON_SIZE; i++) {
		if (*dst != src[i])
			return false;
		dst += POISON_OFF;
	}
	return true;
}

/**
 * Put stack poison values starting from the address provided.
 */
static void
stack_put_watermark(void *addr)
{
	const uint64_t *src = poison_pool;
	uint64_t *dst = addr;
	size_t i;

	for (i = 0; i < POISON_SIZE; i++) {
		*dst = src[i];
		dst += POISON_OFF;
	}
}

/**
 * Free stack memory above the watermark when a fiber is recycled.
 * To avoid a pointless syscall invocation in case the fiber hasn't
 * touched memory above the watermark, we only call madvise() if
 * the fiber has overwritten a poison value.
 */
static void
fiber_stack_recycle(struct fiber *fiber)
{
	if (fiber->stack_watermark == NULL ||
	    stack_has_watermark(fiber->stack_watermark))
		return;
	/*
	 * When dropping pages make sure the page containing
	 * the watermark isn't touched since we're updating
	 * it anyway.
	 */
	void *start, *end;
	if (stack_direction < 0) {
		start = fiber->stack;
		end = fiber->stack_watermark;
	} else {
		start = fiber->stack_watermark;
		end = fiber->stack + fiber->stack_size;
	}

	/*
	 * Ignore errors on MADV_DONTNEED because this is
	 * just a hint for OS and not critical for
	 * functionality.
	 */
	fiber_madvise_unaligned(start, end, MADV_DONTNEED);
	stack_put_watermark(fiber->stack_watermark);
}

/**
 * Initialize fiber stack watermark.
 */
static void
fiber_stack_watermark_create(struct fiber *fiber,
			     const struct fiber_attr *fiber_attr)
{
	assert(fiber->stack_watermark == NULL);

	/* No tracking on custom stacks for simplicity. */
	if (fiber_attr->flags & FIBER_CUSTOM_STACK)
		return;

	/*
	 * We don't expect the whole stack usage in regular
	 * loads, let's try to minimize rss pressure. But do
	 * not exit if MADV_DONTNEED failed, it is just a hint
	 * for OS, not critical one.
	 */
	fiber_madvise_unaligned(fiber->stack, fiber->stack + fiber->stack_size,
				MADV_DONTNEED);
	/*
	 * To increase probability of stack overflow detection
	 * we put the first mark at a random position.
	 */
	size_t offset = rand() % POISON_OFF * sizeof(poison_pool[0]);
	if (stack_direction < 0) {
		fiber->stack_watermark  = fiber->stack + fiber->stack_size;
		fiber->stack_watermark -= FIBER_STACK_SIZE_WATERMARK;
		fiber->stack_watermark += offset;
	} else {
		fiber->stack_watermark  = fiber->stack;
		fiber->stack_watermark += FIBER_STACK_SIZE_WATERMARK;
		fiber->stack_watermark -= page_size;
		fiber->stack_watermark += offset;
	}
	stack_put_watermark(fiber->stack_watermark);
}
#else
static void
fiber_stack_recycle(struct fiber *fiber)
{
	(void)fiber;
}

static void
fiber_stack_watermark_create(struct fiber *fiber,
			     const struct fiber_attr *fiber_attr)
{
	(void)fiber;
	(void)fiber_attr;
}
#endif /* HAVE_MADV_DONTNEED */

static void
fiber_stack_destroy(struct fiber *fiber, struct slab_cache *slabc)
{
	static const int mprotect_flags = PROT_READ | PROT_WRITE;

	if (fiber->stack != NULL) {
		VALGRIND_STACK_DEREGISTER(fiber->stack_id);
		void *guard;
		if (stack_direction < 0)
			guard = page_align_down(fiber->stack - page_size);
		else
			guard = page_align_up(fiber->stack + fiber->stack_size);

		if (fiber_mprotect(guard, page_size, mprotect_flags) != 0) {
			/*
			 * FIXME: We need some intelligent handling:
			 * say put this slab into a queue and retry
			 * to setup the original protection back in
			 * background.
			 *
			 * For now lets keep such slab referenced and
			 * leaked: if mprotect failed we must not allow
			 * to reuse such slab with PROT_NONE'ed page
			 * inside.
			 *
			 * Note that in case if we're called from
			 * fiber_stack_create() the @a mprotect_flags is
			 * the same as the slab been created with, so
			 * calling mprotect for VMA with same flags
			 * won't fail.
			 */
			say_syserror("fiber: Can't put guard page to slab. "
				     "Leak %zu bytes", (size_t)fiber->stack_size);
			/*
			 * Suppress memory leak report for this object.
			 *
			 * Works even though it is not a beginning of
			 * allocation (there is ASAN slab cache allocation
			 * header).
			 */
			LSAN_IGNORE_OBJECT(fiber->stack_slab);
		} else {
			slab_put(slabc, fiber->stack_slab);
		}
	}
}

static int
fiber_stack_create(struct fiber *fiber, const struct fiber_attr *fiber_attr,
		   struct slab_cache *slabc)
{
	size_t stack_size = fiber_attr->stack_size - slab_sizeof();
	fiber->stack_slab = slab_get(slabc, stack_size);

	if (fiber->stack_slab == NULL) {
		diag_set(OutOfMemory, stack_size,
			 "runtime arena", "fiber stack");
		return -1;
	}
	void *guard;
	/* Adjust begin and size for stack memory chunk. */
	if (stack_direction < 0) {
		/*
		 * A stack grows down. First page after begin of a
		 * stack memory chunk should be protected and memory
		 * after protected page until end of memory chunk can be
		 * used for coro stack usage.
		 */
		guard = page_align_up(slab_data(fiber->stack_slab));
		fiber->stack = guard + page_size;
		fiber->stack_size = slab_data(fiber->stack_slab) + stack_size -
				    fiber->stack;
	} else {
		/*
		 * A stack grows up. Last page should be protected and
		 * memory from begin of chunk until protected page can
		 * be used for coro stack usage
		 */
		guard = page_align_down(fiber->stack_slab + stack_size) -
			page_size;
		fiber->stack = fiber->stack_slab + slab_sizeof();
		fiber->stack_size = guard - fiber->stack;
	}

	fiber->stack_id = VALGRIND_STACK_REGISTER(fiber->stack,
						  (char *)fiber->stack +
						  fiber->stack_size);

	if (fiber_mprotect(guard, page_size, PROT_NONE)) {
		/*
		 * Write an error into the log since a guard
		 * page is critical for functionality.
		 */
		diag_log();
		fiber_stack_destroy(fiber, slabc);
		return -1;
	}

	fiber_stack_watermark_create(fiber, fiber_attr);
	return 0;
}

static void
fiber_gc_checker_init(struct fiber *fiber)
{
#ifdef ENABLE_BACKTRACE
	if (!fiber_leak_backtrace_enable) {
		fiber->first_alloc_bt = NULL;
		fiber->gc_initial_size = 0;
		return;
	}

	fiber->first_alloc_bt =
		xregion_alloc_object(&fiber->gc,
				     typeof(*fiber->first_alloc_bt));
	fiber->gc_initial_size = region_used(&fiber->gc);
	region_set_callbacks(&fiber->gc,
			     fiber_on_gc_alloc, fiber_on_gc_truncate,
			     fiber);
#else
	fiber->gc_initial_size = 0;
#endif
}

struct fiber *
fiber_new_ex(const char *name, const struct fiber_attr *fiber_attr,
	     fiber_func f)
{
	struct cord *cord = cord();
	struct fiber *fiber = NULL;
	assert(fiber_attr != NULL);
	cord_collect_garbage(cord);

	if (fiber_is_reusable(fiber_attr->flags) && !rlist_empty(&cord->dead)) {
		fiber = rlist_first_entry(&cord->dead, struct fiber, link);
		rlist_move_entry(&cord->alive, fiber, link);
		assert(fiber_is_dead(fiber));
	} else {
		fiber = (struct fiber *)
			mempool_alloc(&cord->fiber_mempool);
		if (fiber == NULL) {
			diag_set(OutOfMemory, sizeof(struct fiber),
				 "fiber pool", "fiber");
			return NULL;
		}
		memset(fiber, 0, sizeof(struct fiber));
		fiber->storage.lua.storage_ref = FIBER_LUA_NOREF;
		fiber->storage.lua.fid_ref = FIBER_LUA_NOREF;

		if (fiber_stack_create(fiber, fiber_attr, &cord()->slabc)) {
			mempool_free(&cord->fiber_mempool, fiber);
			return NULL;
		}
		coro_create(&fiber->ctx, fiber_loop, NULL,
			    fiber->stack, fiber->stack_size);

		region_create(&fiber->gc, &cord->slabc);

		rlist_create(&fiber->state);
		rlist_create(&fiber->wake);
		diag_create(&fiber->diag);
		fiber_reset(fiber);

		rlist_add_entry(&cord->alive, fiber, link);
	}
	fiber->flags = fiber_attr->flags;
	fiber->f = f;
	fiber->fid = cord->next_fid;
	fiber_set_name(fiber, name);
	register_fid(fiber);
	fiber->max_slice = zero_slice;
	fiber->csw = 0;
#ifdef ENABLE_BACKTRACE
	fiber->parent_bt = NULL;
#endif /* ENABLE_BACKTRACE */
	fiber_gc_checker_init(fiber);
	cord->next_fid++;
	assert(cord->next_fid > FIBER_ID_MAX_RESERVED);

	return fiber;

}

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
fiber_new(const char *name, fiber_func f)
{
	return fiber_new_ex(name, &fiber_attr_default, f);
}

/**
 * Create a new system fiber.
 *
 * Works the same way as fiber_new(), but uses fiber_attr_default
 * supplemented by the FIBER_IS_SYSTEM flag in order to create a
 * fiber.
 */
struct fiber *
fiber_new_system(const char *name, fiber_func f)
{
	struct fiber_attr system_attrs;
	fiber_attr_create(&system_attrs);
	system_attrs.flags |= FIBER_IS_SYSTEM;
	return fiber_new_ex(name, &system_attrs, f);
}

/** Free all fiber's resources. */
static void
fiber_destroy(struct cord *cord, struct fiber *f)
{
	assert(f != cord->fiber);
	trigger_destroy(&f->on_yield);
	trigger_destroy(&f->on_stop);
	rlist_del(&f->state);
	rlist_del(&f->link);
#ifdef ENABLE_BACKTRACE
	region_set_callbacks(&f->gc, NULL, NULL, NULL);
#endif
	region_destroy(&f->gc);
	fiber_stack_destroy(f, &cord->slabc);
	diag_destroy(&f->diag);
	if (f->name != f->inline_name)
		free(f->name);
	TRASH(f);
}

/** Free all fiber's resources and the fiber itself. */
static void
fiber_delete(struct cord *cord, struct fiber *f)
{
	assert(f != &cord->sched);
	fiber_destroy(cord, f);
	mempool_free(&cord->fiber_mempool, f);
}

/** Delete all fibers in the given list so it becomes empty. */
static void
cord_delete_fibers_in_list(struct cord *cord, struct rlist *list)
{
	while (!rlist_empty(list)) {
		struct fiber *f = rlist_first_entry(list, struct fiber, link);
		fiber_delete(cord, f);
	}
}

void
fiber_delete_all(struct cord *cord)
{
	cord_collect_garbage(cord);
	cord_delete_fibers_in_list(cord, &cord->alive);
	cord_delete_fibers_in_list(cord, &cord->dead);
	cord_delete_fibers_in_list(cord, &cord->ready);
}

static void
loop_on_iteration_start(ev_loop *loop, ev_check *watcher, int revents)
{
	(void) loop;
	(void) watcher;
	(void) revents;

	cpu_stat_start(&cord()->cpu_stat);
}

static void
loop_on_iteration_end(ev_loop *loop, ev_prepare *watcher, int revents)
{
	(void) loop;
	(void) watcher;
	(void) revents;
	struct fiber *fiber;
	assert(fiber() == &cord()->sched);

	/*
	 * Record the scheduler's latest clock change, even though
	 * it's not a context switch, but an event loop iteration
	 * end.
	 */
	clock_set_on_csw(&cord()->sched);

	double nsec_per_clock = cpu_stat_end(&cord()->cpu_stat,
					     &cord()->clock_stat);

	clock_stat_update(&cord()->clock_stat, nsec_per_clock);
	clock_stat_update(&cord()->sched.clock_stat, nsec_per_clock);

	rlist_foreach_entry(fiber, &cord()->alive, link) {
		clock_stat_update(&fiber->clock_stat, nsec_per_clock);
	}
}

static inline void
fiber_top_init(void)
{
	ev_prepare_init(&cord()->prepare_event, loop_on_iteration_end);
	ev_check_init(&cord()->check_event, loop_on_iteration_start);
}

bool
fiber_top_is_enabled(void)
{
	return fiber_top_enabled;
}

inline void
fiber_top_enable(void)
{
	if (!fiber_top_enabled) {
		ev_prepare_start(cord()->loop, &cord()->prepare_event);
		ev_check_start(cord()->loop, &cord()->check_event);
		fiber_top_enabled = true;

		cpu_stat_reset(&cord()->cpu_stat);
		clock_stat_reset(&cord()->clock_stat);
		clock_stat_reset(&cord()->sched.clock_stat);

		struct fiber *fiber;
		rlist_foreach_entry(fiber, &cord()->alive, link) {
			clock_stat_reset(&fiber->clock_stat);
		}
	}
}

inline void
fiber_top_disable(void)
{
	if (fiber_top_enabled) {
		ev_prepare_stop(cord()->loop, &cord()->prepare_event);
		ev_check_stop(cord()->loop, &cord()->check_event);
		fiber_top_enabled = false;
	}
}

#ifdef ENABLE_BACKTRACE
bool
fiber_parent_backtrace_is_enabled(void)
{
	return fiber_parent_backtrace_enabled;
}

void
fiber_parent_backtrace_enable(void)
{
	fiber_parent_backtrace_enabled = true;
}

void
fiber_parent_backtrace_disable(void)
{
	fiber_parent_backtrace_enabled = false;
}
#endif /* ENABLE_BACKTRACE */

size_t
box_region_used(void)
{
	return region_used(&fiber()->gc);
}

void *
box_region_alloc(size_t size)
{
	void *res = region_alloc(&fiber()->gc, size);
	if (res == NULL)
		diag_set(OutOfMemory, size, "region_alloc", "data");
	return res;
}

void *
box_region_aligned_alloc(size_t size, size_t alignment)
{
	void *res = region_aligned_alloc(&fiber()->gc, size, alignment);
	if (res == NULL)
		diag_set(OutOfMemory, size, "region_alloc", "aligned data");
	return res;
}

void
box_region_truncate(size_t size)
{
	return region_truncate(&fiber()->gc, size);
}

void
cord_create(struct cord *cord, const char *name)
{
	cord_ptr = cord;
	slab_cache_set_thread(&cord()->slabc);

	cord->id = pthread_self();
	cord->on_exit = NULL;
	slab_cache_create(&cord->slabc, &runtime);
	mempool_create(&cord->fiber_mempool, &cord->slabc,
		       sizeof(struct fiber));
	rlist_create(&cord->alive);
	rlist_create(&cord->ready);
	rlist_create(&cord->dead);
	cord->garbage = NULL;
	cord->fiber_registry = mh_i64ptr_new();

	/* sched fiber is not present in alive/ready/dead list. */
	rlist_create(&cord->sched.state);
	rlist_create(&cord->sched.link);
	cord->sched.fid = FIBER_ID_SCHED;
	fiber_reset(&cord->sched);
	diag_create(&cord->sched.diag);
	region_create(&cord->sched.gc, &cord->slabc);
	fiber_gc_checker_init(&cord->sched);
	cord->sched.name = NULL;
	fiber_set_name(&cord->sched, "sched");
	cord->fiber = &cord->sched;
	cord->sched.flags = FIBER_IS_RUNNING;
	cord->sched.max_slice = zero_slice;
	cord->max_slice = default_slice;

	cord->next_fid = FIBER_ID_MAX_RESERVED + 1;
	/*
	 * No need to start this event since it's only used for
	 * ev_feed_event(). Saves a few cycles on every
	 * event loop iteration.
	 */
	ev_async_init(&cord->wakeup_event, fiber_schedule_wakeup);

	ev_idle_init(&cord->idle_event, fiber_schedule_idle);

	/* fiber.top() currently works only for the main thread. */
	if (cord_is_main()) {
		fiber_top_init();
	}
	cord_set_name(name);

	trigger_init_in_thread();
#if ENABLE_ASAN
	/* Record stack extents */
	tt_pthread_attr_getstack(cord->id, &cord->sched.stack,
				 &cord->sched.stack_size);
#else
	cord->sched.stack = NULL;
	cord->sched.stack_size = 0;
#endif

#ifdef HAVE_MADV_DONTNEED
	cord->sched.stack_watermark = NULL;
#endif
}

void
cord_collect_garbage(struct cord *cord)
{
	struct fiber *garbage = cord->garbage;
	if (garbage == NULL)
		return;
	fiber_delete(cord, garbage);
	cord->garbage = NULL;
}

static void
cord_add_garbage(struct cord *cord, struct fiber *f)
{
	cord_collect_garbage(cord);
	assert(cord->garbage == NULL);
	if (f != cord->fiber)
		fiber_delete(cord, f);
	else
		cord->garbage = f;
}

void
cord_exit(struct cord *cord)
{
	assert(cord == cord());
	(void)cord;
	trigger_free_in_thread();
}

void
cord_destroy(struct cord *cord)
{
	assert(cord->id == 0 || pthread_equal(cord->id, pthread_self()));
	cord->id = 0;
	slab_cache_set_thread(&cord->slabc);
	if (cord->loop)
		ev_loop_destroy(cord->loop);
	/* Only clean up if initialized. */
	if (cord->fiber_registry) {
		fiber_delete_all(cord);
		mh_i64ptr_delete(cord->fiber_registry);
	}
	cord->fiber = NULL;
#if ENABLE_ASAN
	cord->sched.stack = NULL;
	cord->sched.stack_size = 0;
#endif
	fiber_destroy(cord, &cord->sched);
	slab_cache_destroy(&cord->slabc);
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

/**
 * Cord main thread function. It's not exception-safe, the
 * body function must catch all exceptions instead.
 */
void *cord_thread_func(void *p)
{
	struct cord_thread_arg *ct_arg = (struct cord_thread_arg *) p;
	cord_create(ct_arg->cord, (ct_arg->name));
	/** Can't possibly be the main thread */
	assert(cord()->id != main_thread_id);
	tt_pthread_mutex_lock(&ct_arg->start_mutex);
	void *(*f)(void *) = ct_arg->f;
	void *arg = ct_arg->arg;
	ct_arg->is_started = true;
	tt_pthread_cond_signal(&ct_arg->start_cond);
	tt_pthread_mutex_unlock(&ct_arg->start_mutex);
	ERROR_INJECT_SIGILL(ERRINJ_SIGILL_NONMAIN_THREAD);
	void *res = f(arg);
	/*
	 * cord()->on_exit initially holds NULL. This field is
	 * change-once.
	 * Either handler installation succeeds (in cord_cojoin())
	 * or prior to thread exit the thread function discovers
	 * that no handler was installed so far and it stores
	 * CORD_ON_EXIT_WONT_RUN to prevent a future handler
	 * installation (since a handler won't run anyway).
	 */
	const struct cord_on_exit *handler = NULL; /* expected value */
	bool changed;

	changed = pm_atomic_compare_exchange_strong(&cord()->on_exit,
	                                            &handler,
	                                            CORD_ON_EXIT_WONT_RUN);
	if (!changed)
		handler->callback(handler->argument);

	cord_exit(cord());

	return res;
}

int
cord_start(struct cord *cord, const char *name, void *(*f)(void *), void *arg)
{
	int res = -1;
	struct cord_thread_arg ct_arg = { cord, name, f, arg, false,
		PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER };
	tt_pthread_mutex_lock(&ct_arg.start_mutex);
	cord->loop = ev_loop_new(EVFLAG_AUTO | EVFLAG_ALLOCFD);
	if (cord->loop == NULL) {
		diag_set(OutOfMemory, 0, "ev_loop_new", "ev_loop");
		goto end;
	}
	if (tt_pthread_create(&cord->id, NULL,
			      cord_thread_func, &ct_arg) != 0) {
		diag_set(SystemError, "failed to create thread");
		goto end;
	}
	pm_atomic_fetch_add(&cord_count, 1);
	res = 0;
	while (! ct_arg.is_started)
		tt_pthread_cond_wait(&ct_arg.start_cond, &ct_arg.start_mutex);
end:
	if (res != 0) {
		if (cord->loop) {
			ev_loop_destroy(cord->loop);
			cord->loop = NULL;
		}
	}
	tt_pthread_mutex_unlock(&ct_arg.start_mutex);
	tt_pthread_mutex_destroy(&ct_arg.start_mutex);
	tt_pthread_cond_destroy(&ct_arg.start_cond);
	return res;
}

int
cord_join(struct cord *cord)
{
	assert(cord() != cord);
	assert(cord->id != 0);

	void *retval = NULL;
	int res = tt_pthread_join(cord->id, &retval);
	if (res == 0) {
		int old_cord_count = pm_atomic_fetch_sub(&cord_count, 1);
		assert(old_cord_count > 0);
		(void)old_cord_count;
		cord->id = 0;
		struct fiber *f = cord->fiber;
		if (f->f_ret != 0) {
			assert(!diag_is_empty(&f->diag));
			diag_move(&f->diag, diag_get());
			res = -1;
		}
	} else {
		diag_set(SystemError, "failed to join with thread");
	}
	cord_destroy(cord);
	return res;
}

/** The state of the waiter for a thread to complete. */
struct cord_cojoin_ctx
{
	struct ev_loop *loop;
	/** Waiting fiber. */
	struct fiber *fiber;
	/*
	 * This event is signalled when the subject thread is
	 * about to die.
	 */
	struct ev_async async;
	bool task_complete;
};

static void
cord_cojoin_on_exit(void *arg)
{
	struct cord_cojoin_ctx *ctx = (struct cord_cojoin_ctx *)arg;

	ev_async_send(ctx->loop, &ctx->async);
}

static void
cord_cojoin_wakeup(struct ev_loop *loop, struct ev_async *ev, int revents)
{
	(void)loop;
	(void)revents;

	struct cord_cojoin_ctx *ctx = (struct cord_cojoin_ctx *)ev->data;

	ctx->task_complete = true;
	fiber_wakeup(ctx->fiber);
}

int
cord_cojoin(struct cord *cord)
{
	assert(cord() != cord);
	assert(cord->id != 0);

	struct cord_cojoin_ctx ctx;
	ctx.loop = loop();
	ctx.fiber = fiber();
	ctx.task_complete = false;

	ev_async_init(&ctx.async, cord_cojoin_wakeup);
	ctx.async.data = &ctx;
	ev_async_start(loop(), &ctx.async);

	struct cord_on_exit handler = { cord_cojoin_on_exit, &ctx };

	/*
	 * cord->on_exit initially holds a NULL value. This field is
	 * change-once.
	 */
	const struct cord_on_exit *prev_handler = NULL; /* expected value */
	bool changed = pm_atomic_compare_exchange_strong(&cord->on_exit,
	                                                 &prev_handler,
	                                                 &handler);
	/*
	 * A handler installation fails either if the thread did exit or
	 * if someone is already joining this cord (BUG).
	 */
	if (!changed) {
		/* Assume cord's thread already exited. */
		assert(prev_handler == CORD_ON_EXIT_WONT_RUN);
	} else {
		/*
		 * Wait until the thread exits. Prior to exit the
		 * thread invokes cord_cojoin_on_exit, signaling
		 * ev_async, making the event loop call
		 * cord_cojoin_wakeup, waking up this fiber again.
		 */
		do {
			assert(cord->id != 0);
			fiber_yield();
		} while (!ctx.task_complete);
	}

	ev_async_stop(loop(), &ctx.async);
	return cord_join(cord);
}

int
break_ev_loop_f(struct trigger *trigger, void *event)
{
	(void) event;
	trigger_clear(trigger);
	ev_break(loop(), EVBREAK_ALL);
	return 0;
}

struct costart_ctx
{
	fiber_func run;
	void *arg;
};

/** Replication acceptor fiber handler. */
static void *
cord_costart_thread_func(void *arg)
{
	struct costart_ctx ctx = *(struct costart_ctx *) arg;
	free(arg);

	struct fiber *f = fiber_new("main", ctx.run);
	if (f == NULL)
		return NULL;

	TRIGGER(break_ev_loop, break_ev_loop_f);
	/*
	 * Got to be in a trigger, to break the loop even
	 * in case of an exception.
	 */
	trigger_add(&f->on_stop, &break_ev_loop);
	fiber_set_joinable(f, true);
	fiber_start(f, ctx.arg);
	if (!fiber_is_dead(f)) {
		/* The fiber hasn't died right away at start. */
		ev_run(loop(), 0);
	}
	/*
	 * Preserve the exception with which the main fiber
	 * terminated, if any.
	 */
	assert(fiber_is_dead(f));
	fiber()->f_ret = fiber_join(f);
	fiber_check_gc();

	return NULL;
}

int
cord_costart(struct cord *cord, const char *name, fiber_func f, void *arg)
{
	/** Must be allocated to avoid races. */
	struct costart_ctx *ctx = (struct costart_ctx *) malloc(sizeof(*ctx));
	if (ctx == NULL) {
		diag_set(OutOfMemory, sizeof(struct costart_ctx),
			 "malloc", "costart_ctx");
		return -1;
	}
	ctx->run = f;
	ctx->arg = arg;
	if (cord_start(cord, name, cord_costart_thread_func, ctx) == -1) {
		free(ctx);
		return -1;
	}
	return 0;
}

void
cord_set_name(const char *name)
{
	snprintf(cord()->name, sizeof(cord()->name), "%s", name);
	/* Main thread's name will replace process title in ps, skip it */
	if (cord_is_main())
		return;
	tt_pthread_setname(name);
}

bool
cord_is_main(void)
{
	return cord() == &main_cord;
}

void
cord_cancel_and_join(struct cord *cord)
{
	assert(cord->id != 0);
	tt_pthread_cancel(cord->id);
	if (tt_pthread_join(cord->id, NULL) != 0)
		panic("failed to join a canceled thread");
	int old_cord_count = pm_atomic_fetch_sub(&cord_count, 1);
	assert(old_cord_count > 0);
	(void)old_cord_count;
	/*
	 * Can't destroy the cord safely. The cancellation could even happen
	 * before the cord was properly initialized in its own thread. It might
	 * be fixed if cord would be initialized before its thread is started.
	 *
	 * Also obviously even if the creation would be fine, the destruction
	 * can't free everything. The cord could have some resources allocated
	 * on the heap with pointers not stored anywhere in struct cord - they
	 * can't be possibly located.
	 */
	memset(cord, 0, sizeof(*cord));
}

static NOINLINE int
check_stack_direction(void *prev_stack_frame)
{
	return __builtin_frame_address(0) < prev_stack_frame ? -1: 1;
}

void
fiber_init(int (*invoke)(fiber_func f, va_list ap))
{
	page_size = small_getpagesize();
	stack_direction = check_stack_direction(__builtin_frame_address(0));
	fiber_invoke = invoke;
	main_thread_id = pthread_self();
	main_cord.loop = ev_default_loop(EVFLAG_AUTO | EVFLAG_ALLOCFD);
	if (main_cord.loop == NULL)
		panic("can't init event loop");
	cord_create(&main_cord, "main");
	fiber_signal_init();
}

void
fiber_free(void)
{
	assert(pm_atomic_load(&cord_count) == 0);
	cord_exit(&main_cord);
	cord_destroy(&main_cord);
}

/**
 * True if fiber_signal_init was called.
 * Needed for re-entrancy of fiber signal initialization.
 */
static bool signal_initialized;

/** Reset current slice on SIGURG. */
static void
signal_sigurg_cb(int signum)
{
	(void)signum;
	assert(cord_is_main());
	fiber_set_slice(zero_slice);
}

void
fiber_signal_init(void)
{
	assert(cord_is_main());
	if (signal_initialized)
		return;
	signal_initialized = true;
	clock_lowres_signal_init();

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = signal_sigurg_cb;
	if (tt_sigaction(SIGURG, &sa, NULL) == -1)
		panic_syserror("cannot set fiber sigurg handler");
}

void
fiber_signal_reset(void)
{
	assert(cord_is_main());
	assert(signal_initialized);
	signal_initialized = false;
	clock_lowres_signal_reset();

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_DFL;
	if (tt_sigaction(SIGURG, &sa, NULL) == -1)
		say_syserror("cannot reset fiber sigurg handler");
}

int fiber_stat(fiber_stat_cb cb, void *cb_ctx)
{
	struct fiber *fiber;
	struct cord *cord = cord();
	int res;
	rlist_foreach_entry(fiber, &cord->alive, link) {
		res = cb(fiber, cb_ctx);
		if (res != 0)
			return res;
	}
	return 0;
}

struct lua_State *
fiber_lua_state(struct fiber *f)
{
	return f->storage.lua.stack;
}
