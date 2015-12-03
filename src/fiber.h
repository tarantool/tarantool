#ifndef TARANTOOL_FIBER_H_INCLUDED
#define TARANTOOL_FIBER_H_INCLUDED
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
#include "trivia/config.h"

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include "tt_pthread.h"
#include "third_party/tarantool_ev.h"
#include "diag.h"
#include "coro.h"
#include "trivia/util.h"
#include "small/mempool.h"
#include "small/region.h"
#include "small/rlist.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

enum { FIBER_NAME_MAX = REGION_NAME_MAX };

enum {
	/**
	 * It's safe to resume (wakeup) this fiber
	 * with a spurious wakeup if it is suspended,
	 * e.g. to force it to check that it's been
	 * cancelled.
	 */
	FIBER_IS_CANCELLABLE	= 1 << 0,
	/**
	 * Indicates that a fiber has been requested to end
	 * prematurely.
	 */
	FIBER_IS_CANCELLED	= 1 << 1,
	/**
	 * The fiber will garbage collect automatically
	 * when fiber function ends. The alternative
	 * is that some other fiber will wait for
	 * the end of this fiber and garbage collect it
	 * with fiber_join().
	 */
	FIBER_IS_JOINABLE = 1 << 2,
	/**
	 * This flag is set when fiber function ends and before
	 * the fiber is recycled.
	 */
	FIBER_IS_DEAD		= 1 << 4,
	FIBER_DEFAULT_FLAGS = FIBER_IS_CANCELLABLE
};

/**
 * \brief Pre-defined key for fiber local storage
 */
enum fiber_key {
	/** box.session */
	FIBER_KEY_SESSION = 0,
	/** Lua fiber.storage */
	FIBER_KEY_LUA_STORAGE = 1,
	/** transaction */
	FIBER_KEY_TXN = 2,
	/** User global privilege and authentication token */
	FIBER_KEY_USER = 3,
	FIBER_KEY_MSG = 4,
	FIBER_KEY_MAX = 5
};

/** \cond public */

struct fiber;
/**
 * Fiber - contains information about fiber
 */

typedef void (*fiber_func)(va_list);

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
 *
 * \param name       string with fiber name
 * \param fiber_func func for run inside fiber
 *
 * \sa fiber_start
 */
API_EXPORT struct fiber *
fiber_new(const char *name, fiber_func f);

/**
 * Return control to another fiber and wait until it'll be woken.
 *
 * \sa fiber_wakeup
 */
API_EXPORT void
fiber_yield(void);

/**
 * Start execution of created fiber.
 *
 * \param callee fiber to start
 * \param ...    arguments to start the fiber with
 *
 * \sa fiber_new
 */
API_EXPORT void
fiber_start(struct fiber *callee, ...);

/**
 * Interrupt a synchronous wait of a fiber
 *
 * \param f fiber to be woken up
 */
API_EXPORT void
fiber_wakeup(struct fiber *f);

/**
 * Cancel the subject fiber. (set FIBER_IS_CANCELLED flag)
 *
 * If target fiber's flag FIBER_IS_CANCELLABLE set, then it would
 * be woken up (maybe prematurely). Then current fiber yields
 * until the target fiber is dead (or is woken up by
 * \sa fiber_wakeup).
 *
 * \param f fiber to be cancelled
 */
API_EXPORT void
fiber_cancel(struct fiber *f);

/**
 * Make it possible or not possible to wakeup the current
 * fiber immediately when it's cancelled.
 *
 * @param yesno status to set
 * @return previous state.
 */
API_EXPORT bool
fiber_set_cancellable(bool yesno);

/**
 * Set fiber to be joinable (false by default).
 * \param yesno status to set
 */
API_EXPORT void
fiber_set_joinable(struct fiber *fiber, bool yesno);

/**
 * Wait until the fiber is dead and then move its execution
 * status to the caller.
 * The fiber must not be detached (@sa fiber_set_joinable()).
 * @pre FIBER_IS_JOINABLE flag is set.
 *
 * \param f fiber to be woken up
 */
API_EXPORT void
fiber_join(struct fiber *f);

/**
 * Put the current fiber to sleep for at least 's' seconds.
 *
 * \param s time to sleep
 *
 * \note this is a cancellation point (\sa fiber_is_cancelled)
 */
API_EXPORT void
fiber_sleep(double s);

/**
 * Check current fiber for cancellation (it must be checked
 * manually).
 */
API_EXPORT bool
fiber_is_cancelled();

/**
 * Report loop begin time as double (cheap).
 */
API_EXPORT double
fiber_time(void);

/**
 * Report loop begin time as 64-bit int.
 */
API_EXPORT uint64_t
fiber_time64(void);

/**
 * Return slab_cache suitable to use with tarantool/small library
 */
struct slab_cache;
API_EXPORT struct slab_cache *
cord_slab_cache(void);

/** \endcond public */

typedef void (*fiber_func)(va_list);

struct fiber {
	struct tarantool_coro coro;
	/* A garbage-collected memory pool. */
	struct region gc;
#ifdef ENABLE_BACKTRACE
	void *last_stack_frame;
#endif
	/**
	 * The fiber which should be scheduled when
	 * this fiber yields.
	 */
	struct fiber *caller;
	/** Number of context switches. */
	int csw;
	/** Fiber id. */
	uint32_t fid;
	/** Fiber flags */
	uint32_t flags;
	/** Link in cord->alive or cord->dead list. */
	struct rlist link;
	/** Link in cord->ready list. */
	struct rlist state;

	/** Triggers invoked before this fiber yields. Must not throw. */
	struct rlist on_yield;
	/** Triggers invoked before this fiber stops.  Must not throw. */
	struct rlist on_stop;
	/**
	 * The list of fibers awaiting for this fiber's timely
	 * (or untimely) death.
	 */
	struct rlist wake;

	/**
	 * This struct is considered as non-POD when compiling by g++.
	 * You can safely ignore all offset_of-related warnings.
	 * See http://gcc.gnu.org/bugzilla/show_bug.cgi?id=31488
	 */
	fiber_func f;
	va_list f_data;
	/** Fiber local storage */
	void *fls[FIBER_KEY_MAX];
	/** Exception which caused this fiber's death. */
	struct diag diag;
};

enum { FIBER_CALL_STACK = 16 };

struct cord_on_exit;

/**
 * @brief An independent execution unit that can be managed by a separate OS
 * thread. Each cord consists of fibers to implement cooperative multitasking
 * model.
 */
struct cord {
	/** The fiber that is currently being executed. */
	struct fiber *fiber;
	struct ev_loop *loop;
	/** Depth of the fiber call stack. */
	int call_stack_depth;
	/**
	 * Every new fiber gets a new monotonic id. Ids 1-100 are
	 * reserved.
	 */
	uint32_t max_fid;
	pthread_t id;
	const struct cord_on_exit *on_exit;
	/** A helper hash to map id -> fiber. */
	struct mh_i32ptr_t *fiber_registry;
	/** All fibers */
	struct rlist alive;
	/** Fibers, ready for execution */
	struct rlist ready;
	/** A cache of dead fibers for reuse */
	struct rlist dead;
	/** A watcher to have a single async event for all ready fibers.
	 * This technique is necessary to be able to suspend
	 * a single fiber on a few watchers (for example,
	 * a timeout and an event from network, whichever comes
	 * first).
	 * */
	ev_async wakeup_event;
	/**
	 * libev sleeps at least backend_mintime, which is 1 ms in
	 * case of poll()/Linux, unless there are idle watchers.
	 * This is a special hack to speed up fiber_sleep(0),
	 * i.e. a sleep with a zero timeout, to ensure that there
	 * is no 1 ms delay in case of zero sleep timeout.
	 */
	ev_idle idle_event;
	/** A memory cache for (struct fiber) */
	struct mempool fiber_pool;
	/** A runtime slab cache for general use in this cord. */
	struct slab_cache slabc;
	/** The "main" fiber of this cord, the scheduler. */
	struct fiber sched;
	char name[FIBER_NAME_MAX];
};

extern __thread struct cord *cord_ptr;

#define cord() cord_ptr
#define fiber() cord()->fiber
#define loop() (cord()->loop)

/**
 * Start a cord with the given thread function.
 * The return value of the function can be collected
 * with cord_join(). The function *must catch* all
 * exceptions and leave them in the diagnostics
 * area, cord_join() moves the exception from the
 * terminated cord to the caller of cord_join().
 */
int
cord_start(struct cord *cord, const char *name,
	   void *(*f)(void *), void *arg);

/**
 * Like cord_start(), but starts the event loop and
 * a fiber in the event loop. The event loop ends when the
 * fiber in main fiber dies/returns. The exception of the main
 * fiber is propagated to cord_cojoin().
 */
int
cord_costart(struct cord *cord, const char *name, fiber_func f, void *arg);

/**
 * Yield until \a cord has terminated.
 *
 * On success:
 *
 * If \a cord has terminated with an uncaught exception
 * the exception is moved to the current fiber's diagnostics
 * area, otherwise the current fiber's diagnostics area is
 * cleared.
 * @param cord cord
 * @sa pthread_join()
 *
 * @return 0 on success, pthread_join return code on error
 */
int
cord_cojoin(struct cord *cord);

/**
 * Wait for \a cord to terminate. If \a cord has already
 * terminated, then returns immediately.
 *
 * @post If the subject cord terminated with an exception,
 * preserves the exception in the caller's cord.
 *
 * @param cord cord
 * @retval  0  pthread_join succeeded.
 *             If the thread function terminated with an
 *             exception, the exception is raised in the
 *             caller cord.
 * @retval -1   pthread_join failed.
 */
int
cord_join(struct cord *cord);

void
cord_set_name(const char *name);

/** True if this cord represents the process main thread. */
bool
cord_is_main();

void
fiber_init(void (*fiber_invoke)(fiber_func f, va_list ap));

void
fiber_free(void);

void
fiber_set_name(struct fiber *fiber, const char *name);

static inline const char *
fiber_name(struct fiber *f)
{
	return region_name(&f->gc);
}

bool
fiber_checkstack();

/**
 * @brief yield & check for timeout
 * @return true if timeout exceeded
 */
bool
fiber_yield_timeout(ev_tstamp delay);

void
fiber_destroy_all();

void
fiber_gc(void);

void
fiber_call(struct fiber *callee);

struct fiber *
fiber_find(uint32_t fid);

void
fiber_schedule_cb(ev_loop * /* loop */, ev_watcher *watcher, int revents);

/**
 * \brief Associate \a value with \a key in fiber local storage
 * \param fiber fiber
 * \param key pre-defined key
 * \param value value to set
 */
inline void
fiber_set_key(struct fiber *fiber, enum fiber_key key, void *value)
{
	assert(key < FIBER_KEY_MAX);
	fiber->fls[key] = value;
}


static inline bool
fiber_is_dead(struct fiber *f)
{
	return f->flags & FIBER_IS_DEAD;
}

/**
 * \brief Retrieve value by \a key from fiber local storage
 * \param fiber fiber
 * \param key pre-defined key
 * \return value from from fiber local storage
 */
inline void *
fiber_get_key(struct fiber *fiber, enum fiber_key key)
{
	assert(key < FIBER_KEY_MAX);
	return fiber->fls[key];
}

/**
 * Finalizer callback
 * \sa fiber_key_on_gc()
 */
typedef void (*fiber_key_gc_cb)(enum fiber_key key, void *arg);
typedef int (*fiber_stat_cb)(struct fiber *f, void *ctx);

int
fiber_stat(fiber_stat_cb cb, void *cb_ctx);

inline void
fiber_c_invoke(fiber_func f, va_list ap)
{
	return f(ap);
}

#if defined(__cplusplus)
} /* extern "C" */

/*
 * Test if this fiber is in a cancellable state and was indeed
 * cancelled, and raise an exception (FiberIsCancelled) if
 * that's the case.
 */
static inline void
fiber_testcancel(void)
{
	/*
	 * Fiber can catch FiberIsCancelled using try..catch
	 * block in C or pcall()/xpcall() in Lua. However,
	 * FIBER_IS_CANCELLED flag is still set and the subject
	 * fiber will be killed by subsequent unprotected call of
	 * this function.
	 */
	if (fiber_is_cancelled())
		tnt_raise(FiberIsCancelled);
}

static inline struct fiber *
fiber_new_xc(const char *name, fiber_func func)
{
	struct fiber *f = fiber_new(name, func);
	if (f == NULL) {
		diag_raise();
		assert(false);
	}
	return f;
}

inline void
fiber_cxx_invoke(fiber_func f, va_list ap)
{
	try {
		f(ap);
		/*
		 * Make sure a leftover exception does not
		 * propagate up to the joiner.
		 */
		diag_clear(&fiber()->diag);
	} catch (struct error *e) {
	}
}

#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_FIBER_H_INCLUDED */
