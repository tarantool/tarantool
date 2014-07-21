#ifndef TARANTOOL_FIBER_H_INCLUDED
#define TARANTOOL_FIBER_H_INCLUDED
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
#include "trivia/config.h"

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include "tt_pthread.h"
#include "third_party/tarantool_ev.h"
#include "coro.h"
#include "trivia/util.h"
#include "third_party/queue.h"
#include "small/mempool.h"
#include "small/region.h"

#if defined(__cplusplus)
#include "exception.h"
#endif /* defined(__cplusplus) */
#include "salad/rlist.h"

#define FIBER_NAME_MAX REGION_NAME_MAX
#define FIBER_READING_INBOX (1 << 0)
/** This fiber can be cancelled synchronously. */
#define FIBER_CANCELLABLE   (1 << 1)
/** Indicates that a fiber has been cancelled. */
#define FIBER_CANCEL        (1 << 2)
/** This fiber was created via stored procedures API. */
#define FIBER_USER_MODE     (1 << 3)
/** This fiber was marked as ready for wake up */
#define FIBER_READY	    (1 << 4)

/** This is thrown by fiber_* API calls when the fiber is
 * cancelled.
 */

#if defined(__cplusplus)
class FiberCancelException: public Exception {
public:
	FiberCancelException(const char *file, unsigned line)
		: Exception(file, line) {
		/* Nothing */
	}

	virtual void log() const {
		say_debug("FiberCancelException");
	}
};
#endif /* defined(__cplusplus) */

/**
 * \brief Pre-defined key for fiber local storage
 */
enum fiber_key {
	/** box.session */
	FIBER_KEY_SESSION = 0,
	/** Lua fiber.storage */
	FIBER_KEY_LUA_STORAGE = 1,
	FIBER_KEY_MAX = 2
};

struct fiber {
#ifdef ENABLE_BACKTRACE
	void *last_stack_frame;
#endif
	int csw;
	struct tarantool_coro coro;
	/* A garbage-collected memory pool. */
	struct region gc;
	/** Fiber id. */
	uint32_t fid;

	struct rlist link;
	struct rlist state;

	/** Triggers invoked before this fiber yields. Must not throw. */
	struct rlist on_yield;

	/* This struct is considered as non-POD when compiling by g++.
	 * You can safetly ignore all offset_of-related warnings.
	 * See http://gcc.gnu.org/bugzilla/show_bug.cgi?id=31488
	 */
	void (*f) (va_list);
	va_list f_data;
	uint32_t flags;
	struct fiber *waiter;
	void *fls[FIBER_KEY_MAX]; /* fiber local storage */
};

enum { FIBER_CALL_STACK = 16 };
class Exception;

/**
 * @brief An independent execution unit that can be managed by a separate OS
 * thread. Each cord consists of fibers to implement cooperative multitasking
 * model.
 */
struct cord {
	/** The fiber that is currently being executed. */
	struct fiber *fiber;
	/** The "main" fiber of this cord, the scheduler. */
	struct fiber sched;
	struct ev_loop *loop;
	/** Call stack - in case one fiber decides to call
	 * another with fiber_call(). This is not used
	 * currently, all fibers are called by the sched
         */
	struct fiber *stack[FIBER_CALL_STACK];
	/** Stack pointer in fiber call stack. */
	struct fiber **sp;
	/**
	 * Every new fiber gets a new monotonic id. Ids 1-100 are
	 * reserved.
         */
	uint32_t max_fid;
	pthread_t id;
	/** A helper hash to map id -> fiber. */
	struct mh_i32ptr_t *fiber_registry;
	/** All fibers */
	struct rlist fibers;
	/** A cache of dead fibers for reuse */
	struct rlist zombie_fibers;
	/** Fibers, ready for execution */
	struct rlist ready_fibers;
	/** A watcher to have a single async event for all ready fibers. */
	ev_async ready_async;
	/** A memory cache for (struct fiber) */
	struct mempool fiber_pool;
	/** A runtime slab cache for general use in this cord. */
	struct slab_cache slabc;
	char name[FIBER_NAME_MAX];
	/** Last thrown exception */
	class Exception *exception;
	size_t exception_size;
};

extern __thread struct cord *cord_ptr;

#define cord() cord_ptr
#define fiber() cord()->fiber
#define loop() (cord()->loop)

int
cord_start(struct cord *cord, const char *name,
	   void *(*f)(void *), void *arg);

int
cord_join(struct cord *cord);

static inline void
cord_set_name(const char *name)
{
	snprintf(cord()->name, FIBER_NAME_MAX, "%s", name);
}


void fiber_init(void);
void fiber_free(void);
typedef void(*fiber_func)(va_list);
struct fiber *fiber_new(const char *name, fiber_func f);
void fiber_set_name(struct fiber *fiber, const char *name);
int wait_for_child(pid_t pid);

static inline const char *
fiber_name(struct fiber *f)
{
	return region_name(&f->gc);
}

void
fiber_checkstack();

void fiber_yield(void);
void fiber_yield_to(struct fiber *f);

/**
 * @brief yield & check for timeout
 * @return true if timeout exceeded
 */
bool fiber_yield_timeout(ev_tstamp delay);


void fiber_destroy_all();

void fiber_gc(void);
void fiber_call(struct fiber *callee, ...);
void fiber_wakeup(struct fiber *f);
struct fiber *fiber_find(uint32_t fid);
/** Cancel a fiber. A cancelled fiber will have
 * tnt_FiberCancelException raised in it.
 *
 * A fiber can be cancelled only if it is
 * FIBER_CANCELLABLE flag is set.
 */
void fiber_cancel(struct fiber *f);
/** Check if the current fiber has been cancelled.  Raises
 * tnt_FiberCancelException
 */
void fiber_testcancel(void);
/** Make it possible or not possible to cancel the current
 * fiber.
 *
 * return previous state.
 */
bool fiber_setcancellable(bool enable);
void fiber_sleep(ev_tstamp s);
struct tbuf;
void fiber_schedule(ev_watcher *watcher, int event __attribute__((unused)));

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

/**
 * \brief Set finalizing callback invoked on destroy local storage value
 * \param key key
 * \param cb callback
 * Finalizers are global (i.e. are not cord/thread-local).
 */
void
fiber_key_on_gc(enum fiber_key key, fiber_key_gc_cb cb, void *arg);

typedef int (*fiber_stat_cb)(struct fiber *f, void *ctx);

int fiber_stat(fiber_stat_cb cb, void *cb_ctx);

#endif /* TARANTOOL_FIBER_H_INCLUDED */
