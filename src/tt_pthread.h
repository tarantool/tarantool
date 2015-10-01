#ifndef TARANTOOL_PTHREAD_H_INCLUDED
#define TARANTOOL_PTHREAD_H_INCLUDED
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
#include "trivia/util.h"

#include <stdio.h>
#include <pthread.h>
#if HAVE_PTHREAD_NP_H
#include <pthread_np.h>
#endif
#include "say.h"

/**
 * Assert on any pthread* error in debug mode. In release,
 * write into the log file where and what has failed.
 *
 * Still give the user an opportunity to manually
 * check for error, by returning the pthread_* 
 * function status.
 */

#define tt_pthread_error(e)			\
	if (e != 0)				\
		say_syserror("%s error %d", __func__, e);\
	assert(e == 0);				\
	e

/**
 * Debug/logging friendly wrappers around pthread
 * functions.
 */

#define tt_pthread_mutex_init(mutex, attr)	\
({	int e = pthread_mutex_init(mutex, attr);\
	tt_pthread_error(e);			\
})

#define tt_pthread_mutex_destroy(mutex)		\
({	int e = pthread_mutex_destroy(mutex);	\
	tt_pthread_error(e);			\
})

#define tt_pthread_mutex_lock(mutex)		\
({	int e = pthread_mutex_lock(mutex);	\
	say_debug("%s: locking %s", __func__, #mutex);\
	tt_pthread_error(e);\
})

#define tt_pthread_mutex_trylock(mutex)		\
({	int e = pthread_mutex_trylock(mutex);	\
	if (e != 0 && e != EBUSY)		\
		say_error("%s error %d at %s:%d", __func__, e, __FILE__, __LINE__);\
	assert(e == 0 || e == EBUSY);		\
	e;					\
})

#define tt_pthread_mutex_unlock(mutex)		\
({	int e = pthread_mutex_unlock(mutex);	\
	say_debug("%s: unlocking %s", __func__, #mutex);\
	tt_pthread_error(e);			\
})

#define tt_pthread_mutex_destroy(mutex)		\
({	int e = pthread_mutex_destroy(mutex);	\
	tt_pthread_error(e);			\
})

#define tt_pthread_mutexattr_init(attr)		\
({	int e = pthread_mutexattr_init(attr);	\
	tt_pthread_error(e);			\
})

#define tt_pthread_mutexattr_destroy(attr)	\
({	int e = pthread_mutexattr_destroy(attr);\
	tt_pthread_error(e);			\
})

#define tt_pthread_mutexattr_gettype(attr, type)\
({	int e = pthread_mutexattr_gettype(attr, type);\
	tt_pthread_error(e);			\
})

#define tt_pthread_mutexattr_settype(attr, type)\
({	int e = pthread_mutexattr_settype(attr, type);\
	tt_pthread_error(e);			\
})

#define tt_pthread_condattr_init(attr)		\
({	int e = pthread_condattr_init(attr);	\
	tt_pthread_error(e);			\
})

#define tt_pthread_condattr_destroy(attr)	\
({ int e = pthread_condattr_destroy(attr);	\
	tt_pthread_error(e);			\
})

#define tt_pthread_cond_init(cond, attr)	\
({	int e = pthread_cond_init(cond, attr);	\
	tt_pthread_error(e);			\
})

#define tt_pthread_cond_destroy(cond)		\
({	int e = pthread_cond_destroy(cond);	\
	tt_pthread_error(e);			\
})

#define tt_pthread_cond_signal(cond)		\
({	int e = pthread_cond_signal(cond);	\
	tt_pthread_error(e);			\
})

#define tt_pthread_cond_wait(cond, mutex)	\
({	int e = pthread_cond_wait(cond, mutex);\
	tt_pthread_error(e);			\
})

#define tt_pthread_cond_timedwait(cond, mutex, timeout)	\
({	int e = pthread_cond_timedwait(cond, mutex, timeout);\
	if (ETIMEDOUT != e)                           \
		say_error("%s error %d", __func__, e);\
	assert(e == 0 || e == ETIMEDOUT);             \
	e;                                             \
})

#define tt_pthread_once(control, function)	\
({	int e = pthread_once(control, function);\
	tt_pthread_error(e);			\
})

#define tt_pthread_atfork(prepare, parent, child)\
({	int e = pthread_atfork(prepare, parent, child);\
	tt_pthread_error(e);			\
})

/** Make sure the created thread blocks all signals,
 * they are handled in the main thread.
 */
#define tt_pthread_create(thread, attr, run, arg)	\
({	sigset_t set, oldset;				\
	sigfillset(&set);				\
	pthread_sigmask(SIG_BLOCK, &set, &oldset);	\
	int e = pthread_create(thread, attr, run, arg);	\
	pthread_sigmask(SIG_SETMASK, &oldset, NULL);	\
	tt_pthread_error(e);				\
})

#define tt_pthread_join(thread, ret)			\
({	int e = pthread_join(thread, ret);		\
	tt_pthread_error(e);				\
})

/** Set the current thread's name
 */
static inline void
tt_pthread_setname(const char *name)
{
	/* Setting the name fails if the name was too long. Linux limits a
	 * name to 16 bytes (including the trailing NUL), other OS don't
	 * even bother to document the limit.
	 */
	char short_name[16];
	snprintf(short_name, sizeof name, "%s", name);

#if HAVE_PTHREAD_SETNAME_NP
	pthread_setname_np(pthread_self(), short_name);
#elif HAVE_PTHREAD_SETNAME_NP_1
	pthread_setname_np(short_name);
#elif HAVE_PTHREAD_SET_NAME_NP
	pthread_set_name_np(pthread_self(), short_name);
#endif
}

#endif /* TARANTOOL_PTHREAD_H_INCLUDED */
