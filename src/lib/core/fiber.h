#ifndef TARANTOOL_LIB_CORE_FIBER_H_INCLUDED
#define TARANTOOL_LIB_CORE_FIBER_H_INCLUDED
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
#include "trivia/config.h"

#include <stdbool.h>
#include <stdint.h>
#include "tt_pthread.h"
#include <tarantool_ev.h>
#include "diag.h"
#include "trivia/util.h"
#include "small/mempool.h"
#include "small/region.h"
#include "small/rlist.h"
#include "salad/stailq.h"

#include <coro/coro.h>

/*
 * Fiber top doesn't work on ARM processors at the moment,
 * because we haven't chosen an alternative to rdtsc.
 */
#if !defined(__amd64__) && !defined(__i386__) && !defined(__x86_64__)
#define ENABLE_FIBER_TOP 0
#else
#define ENABLE_FIBER_TOP 1
#endif

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#if ENABLE_FIBER_TOP
/* A fiber reports used up CPU time with nanosecond resolution. */
#define FIBER_TIME_RES 1000000000

/**
 * A struct containing all the info gathered for current fiber or
 * thread as a whole when fiber.top() is enabled.
 */
struct clock_stat {
	/**
	 * Accumulated clock value calculated using exponential
	 * moving average.
	 */
	uint64_t acc;
	/**
	 * Clock delta counter used on current event loop
	 * iteration.
	 */
	uint64_t delta;
	/**
	 * Clock delta calculated on previous event loop
	 * iteration.
	 */
	uint64_t prev_delta;
	/**
	 * Total processor time this fiber (or cord as a whole)
	 * has spent with 1 / FIBER_TIME_RES second precision.
	 */
	uint64_t cputime;
};

/**
 * A struct encapsulating all knowledge this cord has about cpu
 * clocks and their state.
 */
struct cpu_stat {
	uint64_t prev_clock;
	/**
	 * This thread's CPU time at the beginning of event loop
	 * iteration. Used to calculate how much cpu time has
	 * each loop iteration consumed and update fiber cpu
	 * times propotionally. The resolution is
	 * 1 / FIBER_TIME_RES seconds.
	 */
	uint64_t prev_cputime;
	uint32_t prev_cpu_id;
	uint32_t cpu_miss_count;
	uint32_t prev_cpu_miss_count;
};

#endif /* ENABLE_FIBER_TOP */

enum {
	/** Both limits include terminating 0. */
	FIBER_NAME_INLINE = 40,
	FIBER_NAME_MAX = 256
};

/**
 * Fiber ids [0; 100] are reserved.
 */
enum {
	FIBER_ID_SCHED		= 1,
	FIBER_ID_MAX_RESERVED	= 100
};

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
	 * The fiber is in cord->ready list or in
	 * a call chain created by fiber_schedule_list().
	 * The flag is set to help fiber_wakeup() avoid
	 * double wakeup of an already scheduled fiber.
	 */
	FIBER_IS_READY = 1 << 3,
	/**
	 * This flag is set when fiber function ends and before
	 * the fiber is recycled.
	 */
	FIBER_IS_DEAD		= 1 << 4,
	/**
	 * This flag is set when fiber uses custom stack size.
	 */
	FIBER_CUSTOM_STACK	= 1 << 5,
	/**
	 * The flag is set for the fiber currently being executed by the cord.
	 */
	FIBER_IS_RUNNING	= 1 << 6,
	/**
	 * This flag is set when fiber is in the idle list
	 * of fiber_pool.
	 */
	FIBER_IS_IDLE		= 1 << 7,
	FIBER_DEFAULT_FLAGS = FIBER_IS_CANCELLABLE
};

/** \cond public */

/**
 * Fiber attributes container
 */
struct fiber_attr;

/**
 * Create a new fiber attribute container and initialize it
 * with default parameters.
 * Can be used for many fibers creation, corresponding fibers
 * will not take ownership.
 */
API_EXPORT struct fiber_attr *
fiber_attr_new(void);

/**
 * Delete the fiber_attr and free all allocated resources.
 * This is safe when fibers created with this attribute still exist.
 *
 *\param fiber_attr fiber attribute
 */
API_EXPORT void
fiber_attr_delete(struct fiber_attr *fiber_attr);

/**
 * Set stack size for the fiber attribute.
 *
 * \param fiber_attribute fiber attribute container
 * \param stacksize stack size for new fibers
 */
API_EXPORT int
fiber_attr_setstacksize(struct fiber_attr *fiber_attr, size_t stack_size);

/**
 * Get stack size from the fiber attribute.
 *
 * \param fiber_attribute fiber attribute container or NULL for default
 * \retval stack size
 */
API_EXPORT size_t
fiber_attr_getstacksize(struct fiber_attr *fiber_attr);

struct fiber;
/**
 * Fiber - contains information about fiber
 */

typedef int (*fiber_func)(va_list);

/**
 * Return the current fiber
 */
API_EXPORT struct fiber *
fiber_self(void);

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
 * Create a new fiber with defined attributes.
 *
 * Can fail only if there is not enough memory for
 * the fiber structure or fiber stack.
 *
 * The created fiber automatically returns itself
 * to the fiber cache if has default stack size
 * when its "main" function completes.
 *
 * \param name       string with fiber name
 * \param fiber_attr fiber attributes
 * \param fiber_func func for run inside fiber
 *
 * \sa fiber_start
 */
API_EXPORT struct fiber *
fiber_new_ex(const char *name, const struct fiber_attr *fiber_attr, fiber_func f);

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
 * Interrupt a synchronous wait of a fiber. Nop for the currently running fiber.
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
 * \return fiber function ret code
 */
API_EXPORT int
fiber_join(struct fiber *f);

/**
 * Wait until the fiber is dead or timeout exceeded.
 * In case timeout == TIMEOUT_INFINITY, this function
 * same as fiber_join function.
 * Return fiber execution status to the caller or -1
 * if timeout exceeded and set diag.
 * The fiber must not be detached (@sa fiber_set_joinable()).
 * @pre FIBER_IS_JOINABLE flag is set.
 *
 * \param f fiber to be woken up
 * \param timeout time during which we wait for the fiber completion
 * \return fiber function ret code or -1 in case if timeout exceeded
 */
API_EXPORT int
fiber_join_timeout(struct fiber *f, double timeout);

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
fiber_is_cancelled(void);

/**
 * Report loop begin time as double (cheap).
 * Uses real time clock.
 */
API_EXPORT double
fiber_time(void);

/**
 * Report loop begin time as 64-bit int.
 * Uses real time clock.
 */
API_EXPORT uint64_t
fiber_time64(void);

/**
 * Report loop begin time as double (cheap).
 * Uses monotonic clock.
 */
API_EXPORT double
fiber_clock(void);

/**
 * Report loop begin time as 64-bit int.
 * Uses monotonic clock.
 */
API_EXPORT uint64_t
fiber_clock64(void);

/**
 * Reschedule fiber to end of event loop cycle.
 */
API_EXPORT void
fiber_reschedule(void);

/**
 * Return slab_cache suitable to use with tarantool/small library
 */
struct slab_cache;
API_EXPORT struct slab_cache *
cord_slab_cache(void);

/**
 * box region allocator
 *
 * It is the region allocator from the small library. It is useful
 * for allocating tons of small objects and free them at once.
 *
 * Typical usage is illustrated in the sketch below.
 *
 *  | size_t region_svp = box_region_used();
 *  | while (<...>) {
 *  |     char *buf = box_region_alloc(<...>);
 *  |     <...>
 *  | }
 *  | box_region_truncate(region_svp);
 *
 * There are module API functions that return a result on
 * this region. In this case a module is responsible to free the
 * result:
 *
 *  | size_t region_svp = box_region_used();
 *  | char *buf = box_<...>(<...>);
 *  | <...>
 *  | box_region_truncate(region_svp);
 *
 * This API provides better compatibility guarantees over using
 * the small library directly in a module. A binary layout of
 * internal structures may be changed in a future, but
 * <box_region_*>() functions will remain API and ABI compatible.
 *
 * Data allocated on the region are guaranteed to be valid until
 * a fiber yield or a call of a function from the certain set:
 *
 * - Related to transactions.
 * - Ones that may cause box initialization (box.cfg()).
 * - Ones that may involve SQL execution.
 *
 * FIXME: Provide more strict list of functions, which may
 * invalidate the data: ones that may lead to calling of
 * fiber_gc().
 *
 * It is safe to call simple box APIs around tuples, key_defs and
 * so on -- they don't invalidate the allocated data.
 *
 * Don't assume this region as fiber local. This is an
 * implementation detail and may be changed in a future.
 */

/** How much memory is used by the box region. */
API_EXPORT size_t
box_region_used(void);

/**
 * Allocate size bytes from the box region.
 *
 * Don't use this function to allocate a memory block for a value
 * or array of values of a type with alignment requirements. A
 * violation of alignment requirements leads to undefined
 * behaviour.
 *
 * In case of a memory error set a diag and return NULL.
 * @sa <box_error_last>().
 */
API_EXPORT void *
box_region_alloc(size_t size);

/**
 * Allocate size bytes from the box region with given alignment.
 *
 * Alignment must be a power of 2.
 *
 * In case of a memory error set a diag and return NULL.
 * @sa <box_error_last>().
 */
API_EXPORT void *
box_region_aligned_alloc(size_t size, size_t alignment);

/**
 * Truncate the box region to the given size.
 */
API_EXPORT void
box_region_truncate(size_t size);

/** \endcond public */

/**
 * Fiber attribute container
 */
struct fiber_attr {
	/** Fiber stack size. */
	size_t stack_size;
	/** Fiber flags. */
	uint32_t flags;
};

/**
 * Init fiber attr with default values
 */
void
fiber_attr_create(struct fiber_attr *fiber_attr);

/**
 * Under no circumstances this header file is allowed to include
 * application-specific headers like session.h or txn.h. One only
 * is allowed to announce a struct and add opaque pointer to it.
 */
struct session;
struct txn;
struct credentials;
struct lua_State;
struct ipc_wait_pad;

struct fiber {
	coro_context ctx;
	/** Coro stack slab. */
	struct slab *stack_slab;
	/** Coro stack addr. */
	void *stack;
#ifdef HAVE_MADV_DONTNEED
	/**
	 * We want to keep total stack memory usage low while still
	 * allowing tasks that need a greater than average stack.
	 * To achieve that, we write some poison values to stack
	 * at "watermark" position and call madvise(MADV_DONTNEED)
	 * when a fiber is recycled in case a poison value has been
	 * overwritten. This allows to keep per-fiber stack memory
	 * usage below the watermark while avoiding any performance
	 * penalty if there are no tasks eager for stack.
	 */
	void *stack_watermark;
#endif
	/** Coro stack size. */
	size_t stack_size;
	/** Valgrind stack id. */
	unsigned int stack_id;
	/* A garbage-collected memory pool. */
	struct region gc;
	/**
	 * The fiber which should be scheduled when
	 * this fiber yields.
	 */
	struct fiber *caller;
	/** Number of context switches. */
	int csw;
	/** Fiber id. */
	uint64_t fid;
	/** Fiber flags */
	uint32_t flags;
#if ENABLE_FIBER_TOP
	struct clock_stat clock_stat;
#endif /* ENABLE_FIBER_TOP */
	/** Link in cord->alive or cord->dead list. */
	struct rlist link;
	/** Link in cord->ready list. */
	struct rlist state;

	/** Triggers invoked before this fiber yields. Must not throw. */
	struct rlist on_yield;
	/**
	 * Triggers invoked before this fiber is stopped/reset/
	 * recycled/destroyed/reused. In other words, each time
	 * when the fiber has finished execution of a request.
	 * In particular, for fibers not from a fiber pool the
	 * stop event is emitted before destruction and death.
	 * Pooled fibers receive the stop event after each
	 * request, even if they are never destroyed.
	 */
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
	union {
		/**
		 * Argument list passed when the fiber is invoked in a blocking
		 * way, so the caller may pass arguments from its own stack.
		 */
		va_list f_data;
		/**
		 * Fiber function argument which passed asynchronously. Can be
		 * used not to call fiber_start to avoid yields, but still pass
		 * something into the fiber.
		 */
		void *f_arg;
	};
	int f_ret;
	/** Fiber local storage. */
	struct {
		/**
		 * Current transaction, session and the active
		 * user credentials are shared among multiple
		 * requests and valid even out of a former.
		 */
		struct session *session;
		struct credentials *credentials;
		struct txn *txn;
		/** Fields related to Lua code execution. */
		struct {
			/**
			 * Optional Lua state (may be NULL).
			 * Useful as a temporary Lua state to save
			 * time and resources on creating it.
			 * Should not be used in other fibers.
			 */
			struct lua_State *stack;
			/**
			 * Optional fiber.storage Lua reference.
			 */
			int ref;
		} lua;
		/**
		 * Iproto sync.
		 */
		struct {
			uint64_t sync;
		} net;
	} storage;
	/** An object to wait for incoming message or a reader. */
	struct ipc_wait_pad *wait_pad;
	/** Exception which caused this fiber's death. */
	struct diag diag;
	/**
	 * Name points at inline_name in case it is short. Long
	 * name is allocated on the heap.
	 */
	char *name;
	char inline_name[FIBER_NAME_INLINE];
};

/** Invoke on_stop triggers and delete them. */
void
fiber_on_stop(struct fiber *f);

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
	/**
	 * Every new fiber gets a new monotonic id. Ids 0 - 100 are
	 * reserved.
	 */
	uint64_t next_fid;
#if ENABLE_FIBER_TOP
	struct clock_stat clock_stat;
	struct cpu_stat cpu_stat;
#endif /* ENABLE_FIBER_TOP */
	pthread_t id;
	const struct cord_on_exit *on_exit;
	/** A helper hash to map id -> fiber. */
	struct mh_i64ptr_t *fiber_registry;
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
#if ENABLE_FIBER_TOP
	/** An event triggered on every event loop iteration start. */
	ev_check check_event;
	/**
	 * An event triggered on every event loop iteration end.
	 * Just like the event above it is used in per-fiber cpu
	 * time calculations.
	 */
	ev_prepare prepare_event;
#endif /* ENABLE_FIBER_TOP */
	/** A memory cache for (struct fiber) */
	struct mempool fiber_mempool;
	/** A runtime slab cache for general use in this cord. */
	struct slab_cache slabc;
	/** The "main" fiber of this cord, the scheduler. */
	struct fiber sched;
	char name[FIBER_NAME_INLINE];
};

extern __thread struct cord *cord_ptr;

#define cord() cord_ptr
#define fiber() cord()->fiber
#define loop() (cord()->loop)

void
cord_create(struct cord *cord, const char *name);

void
cord_destroy(struct cord *cord);

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
 * @return 0 on success, -1 if pthread_join failed or the
 * thread function terminated with an exception.
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
 * @return 0 on success, -1 if pthread_join failed or the
 * thread function terminated with an exception.
 */
int
cord_join(struct cord *cord);

void
cord_set_name(const char *name);

static inline const char *
cord_name(struct cord *cord)
{
	return cord->name;
}

/** True if this cord represents the process main thread. */
bool
cord_is_main(void);

void
fiber_init(int (*fiber_invoke)(fiber_func f, va_list ap));

void
fiber_free(void);

/**
 * Set fiber name.
 * @param fiber Fiber to set name for.
 * @param name A new name of @a fiber.
 */
void
fiber_set_name(struct fiber *fiber, const char *name);

static inline const char *
fiber_name(struct fiber *f)
{
	return f->name;
}

bool
fiber_checkstack(void);

/**
 * @brief yield & check for timeout
 * @return true if timeout exceeded
 */
bool
fiber_yield_timeout(ev_tstamp delay);

void
fiber_destroy_all(struct cord *cord);

void
fiber_gc(void);

void
fiber_call(struct fiber *callee);

struct fiber *
fiber_find(uint64_t fid);

void
fiber_schedule_cb(ev_loop * /* loop */, ev_watcher *watcher, int revents);

static inline bool
fiber_is_dead(struct fiber *f)
{
	return f->flags & FIBER_IS_DEAD;
}

typedef int (*fiber_stat_cb)(struct fiber *f, void *ctx);

int
fiber_stat(fiber_stat_cb cb, void *cb_ctx);

#if ENABLE_FIBER_TOP
bool
fiber_top_is_enabled(void);

void
fiber_top_enable(void);

void
fiber_top_disable(void);
#endif /* ENABLE_FIBER_TOP */

/** Useful for C unit tests */
static inline int
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
		unreachable();
	}
	return f;
}

static inline int
fiber_cxx_invoke(fiber_func f, va_list ap)
{
	try {
		return f(ap);
	} catch (struct error *e) {
		return -1;
	}
}

#endif /* defined(__cplusplus) */

static inline void *
region_aligned_alloc_cb(void *ctx, size_t size)
{
	void *ptr = region_aligned_alloc((struct region *) ctx, size,
					 alignof(uint64_t));
	if (ptr == NULL)
		diag_set(OutOfMemory, size, "region", "new slab");
	return ptr;
}


#endif /* TARANTOOL_LIB_CORE_FIBER_H_INCLUDED */
