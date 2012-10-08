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

#ifndef TARANTOOL_IFC_H_INCLUDED
#define TARANTOOL_IFC_H_INCLUDED

#include <tarantool_ev.h>
#include "third_party/queue.h"

/**
@brief SEMAPHORES
*/


struct fiber_semaphore;
/**
@brief allocator
@return malloced semaphore
@code
	struct fiber_semaphore *s = fiber_semaphore_alloc();
@endcode
*/
struct fiber_semaphore *fiber_semaphore_alloc(void);

/**
@brief init semaphore
@param s semaphore
@param cnt initial value of semaphore
@code
	struct fiber_semaphore *s = fiber_semaphore_alloc();
	fiber_semaphore_init(s);
@endcode
*/
void fiber_semaphore_init(struct fiber_semaphore *s, int cnt);

/**
@brief down semafore
@detail decrement semaphore's counter and lock current
fiber (if semaphore counter < 0) until the other fiber
increments the counter.
@param s semaphore
@code
	fiber_semaphore_down(s);
	// do something
	fiber_semaphore_up(s);
@endcode
*/
void fiber_semaphore_down(struct fiber_semaphore *s);

/**
@brief up semaphore
@detail increment semaphore's counter and unlock one locked fiber
@param s semaphore
@code
	fiber_semaphore_down(s);
	// do something
	fiber_semaphore_up(s);
@endcode
*/

void fiber_semaphore_up(struct fiber_semaphore *s);

/**
@brief down semaphore in timeout
@param semaphore
@param timeout
@detail decrement semaphore's counter and lock current
fiber (if semaphore counter < 0) until the other fiber
increments the counter or timeout exceeded.
@return 0 if success
@return ETIMEDOUT if timeout exceeded
*/
int  fiber_semaphore_down_timeout(struct fiber_semaphore *s, ev_tstamp timeout);

/**
@brief try to down semaphore
@detail down semafore if its counter >= 0
@param s semaphore
@return 0 if success
*/
int  fiber_semaphore_trydown(struct fiber_semaphore *s);

/**
@brief get semaphore's counter
@param s semaphore
@return semaphore's counter
*/
int  fiber_semaphore_counter(struct fiber_semaphore *s);



/**
@brief MUTEXES
*/

struct fiber_mutex;

/**
@brief allocate new mutex
@return malloced mutex structure
@code
	struct fiber_mutex *m = fiber_mutex_alloc();
@endcode
*/
struct fiber_mutex *fiber_mutex_alloc(void);

/**
@brief init mutex
@param m mutex
@code
	struct fiber_mutex *m = fiber_mutex_alloc();
	fiber_mutex_init(m);
@endcode
*/
void fiber_mutex_init(struct fiber_mutex *m);

/**
@brief lock mutex
@detail lock mutex. lock current fiber if mutex is already locked until
the other fiber unlock the mutex.
@param m mutex
@code
	fiber_mutex_lock(m);
	// do something
	fiber_mutex_unlock(m);
@endcode
*/
void fiber_mutex_lock(struct fiber_mutex *m);

/**
@brief lock mutex in timeout
@detail lock mutex and lock current
fiber mutex is already locked until the other fiber
unlock the mutex.
@param mutex
@param timeout
@code
	fiber_mutex_lock(m);
	// do something
	fiber_mutex_unlock(m);
@endcode
*/
int  fiber_mutex_lock_timeout(struct fiber_mutex *m, ev_tstamp timeout);

/**
@brief unlock mutex
@detail unlock one locked fiber
@param mutex
@code
	fiber_mutex_lock(m);
	// do something
	fiber_mutex_unlock(m);
@endcode
*/
void fiber_mutex_unlock(struct fiber_mutex *m);

/**
@brief try to lock mutex
@param mutex
@return 0 if mutex locked
@return ETIMEDOUT if mutex is already locked
*/
int  fiber_mutex_trylock(struct fiber_mutex *m);

/**
@brief check if mutex is locked
@param mutex
@return 0 if mutex is free
@return 1 if mutex is locked
*/
int  fiber_mutex_islocked(struct fiber_mutex *m);

/**
@brief CHANNELS
*/



struct fiber_channel;
/**
@brief allocator
@param size
@return malloced channel (or NULL)
@code
	struct fiber_channel *ch = fiber_channel_alloc(10);
@endcode
*/
struct fiber_channel *fiber_channel_alloc(unsigned size);

/**
@brief init channel
@param channel
@code
	struct fiber_channel *ch = fiber_channel_alloc(10);
	fiber_channel_init(ch);
@endcode
*/
void fiber_channel_init(struct fiber_channel *ch);

/**
@brief put data into channel
@detail lock current fiber if channel is full
@param channel
@param data
@code
	fiber_channel_put(ch, "message");
@endcode
*/
void fiber_channel_put(struct fiber_channel *ch, void *data);

/**
@brief get data from channel
@detail lock current fiber if channel is empty
@param channel
@return data that was put into channel by fiber_channel_put
@code
	char *msg = fiber_channel_get(ch);
@endcode
*/
void *fiber_channel_get(struct fiber_channel *ch);

/**
@brief check if channel is empty
@param channel
@return 1 (TRUE) if channel is empty
@return 0 otherwise
@code
	if (!fiber_channel_isempty(ch))
		char *msg = fiber_channel_get(ch);
@endcode
*/
int  fiber_channel_isempty(struct fiber_channel *ch);

/**
@brief check if channel is full
@param channel
@return 1 (TRUE) if channel is full
@return 0 otherwise
@code
	if (!fiber_channel_isfull(ch))
		fiber_channel_put(ch, "message");
@endcode
*/
int  fiber_channel_isfull(struct fiber_channel *ch);

/**
@brief put data into channel in timeout
@param channel
@param data
@param timeout
@return 0 if success
@return ETIMEDOUT if timeout exceeded
@code
	if (fiber_channel_put_timeout(ch, "message", 0.25) == 0)
		return "ok";
	else
		return "timeout exceeded";
@endcode
*/
int  fiber_channel_put_timeout(struct fiber_channel *ch,
						void *data, ev_tstamp timeout);
/**
@brief get data into channel in timeout
@param channel
@param timeout
@return data if success
@return NULL if timeout exceeded
@code
	do {
		char *msg = fiber_channel_get_timeout(ch, 0.5);
		printf("message: %p\n", msg);
	} until(msg);
	return msg;
@endcode
*/
void *fiber_channel_get_timeout(struct fiber_channel *ch, ev_tstamp timeout);

#endif /* TARANTOOL_IFC_H_INCLUDED */

