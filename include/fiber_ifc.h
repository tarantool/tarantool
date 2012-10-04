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

#ifndef TARANTOOL_FIBER_IFC_H_INCLUDED
#define TARANTOOL_FIBER_IFC_H_INCLUDED

#include <tarantool_ev.h>
#include "third_party/queue.h"

/* SEMAPHORES

	static struct fiber_semaphore sm;
	...

	init(...) {
		...
		fiber_semaphore_init(&sm, 10);
		...
	}

	a_fiber(...) {

		...
		fiber_semaphore_down(&sm);
		...
		// do something
		...
		fiber_semaphore_up(&sm);
	}

	the_other_fiber(...) {

		...
		fiber_semaphore_down(&sm);
		...
		// do something
		...
		fiber_semaphore_up(&sm);
	}


*/

struct fiber_semaphore;
struct fiber_semaphore *fiber_semaphore_alloc(void);
void fiber_semaphore_init(struct fiber_semaphore *s, int cnt);
void fiber_semaphore_down(struct fiber_semaphore *s);
void fiber_semaphore_up(struct fiber_semaphore *s);
int  fiber_semaphore_trydown(struct fiber_semaphore *s);
int  fiber_semaphore_counter(struct fiber_semaphore *s);


/* MUTEXES
	static struct fiber_mutex mutex;

	init(...) {
		...
		fiber_mutex_init(&mutex);
		...
	}

	a_fiber(...) {

		...
		fiber_mutex_lock(&mutex);
		...
		// do something
		...
		fiber_mutex_unlock(&mutex);
	}

	the_other_fiber(...) {

		...
		fiber_mutex_lock(&mutex);
		...
		// do something
		...
		fiber_mutex_unlock(&mutex);
	}
*/

struct fiber_mutex;
struct fiber_mutex *fiber_mutex_alloc(void);
void fiber_mutex_init(struct fiber_mutex *m);
void fiber_mutex_lock(struct fiber_mutex *m);
void fiber_mutex_unlock(struct fiber_mutex *m);
int  fiber_mutex_trylock(struct fiber_mutex *m);
int  fiber_mutex_isfree(struct fiber_mutex *m);

/* RW-MUTEXES
	static struct fiber_rwlock rw;

	init(...) {
		...
		fiber_rwlock_init(&rw);
		...
	}

	writer_fiber(...) {

		...
		fiber_rwlock_wrlock(&rw);
		...
		// write
		...
		fiber_rwlock_unlock(&rw);
	}

	the_other_fiber(...) {

		...
		fiber_rwlock_rdlock(&rd);
		...
		// read
		...
		fiber_rwlock_unlock(&rw);
	}
*/

struct fiber_rwlock;
struct fiber_rwlock *fiber_rwlock_alloc(void);
void fiber_rwlock_init(struct fiber_rwlock *rw);
void fiber_rwlock_rdlock(struct fiber_rwlock *rw);
void fiber_rwlock_wrlock(struct fiber_rwlock *rw);
void fiber_rwlock_unlock(struct fiber_rwlock *rw);

/* CHANNELS
	static struct fiber_channel ch;


	init(...) {
		...
		fiber_channel_init(&ch);
		...
	}

	a_fiber(...) {
		while(1) {
			struct my_msg *msg = fiber_channel_get(&ch);
			...
			// do something with msg
			free(msg);
		}
	}

	the_other_fiber(...) {

		...
		struct my_msg *msg = malloc(sizeof(struct my_msg));
		msg->a = 1;
		msg->b = 2;
		fiber_channel_put(&ch, msg);
		...
	}

*/


struct fiber_channel;
struct fiber_channel *fiber_channel_alloc(unsigned size);
void fiber_channel_init(struct fiber_channel *ch);
void *fiber_channel_get(struct fiber_channel *ch);
void fiber_channel_put(struct fiber_channel *ch, void *data);
int  fiber_channel_isempty(struct fiber_channel *ch);
int  fiber_channel_isfull(struct fiber_channel *ch);

#endif /* TARANTOOL_FIBER_IFC_H_INCLUDED */

