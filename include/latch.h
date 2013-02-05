#ifndef TARANTOOL_LATCH_H_INCLUDED
#define TARANTOOL_LATCH_H_INCLUDED
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
#include <stdbool.h>

struct fiber;

/*
 * Internal implementation of a container for a mutex like object
 * with similar interface. It's used boolean variable because of
 * single threaded nature of tarantool. But it's rather simple to change
 * this variable to a mutex object to maintain multi threaded approach.
 */
struct tnt_latch {
	bool locked;

	struct fiber *owner;
};

/**
 * Initialize the given latch.
 *
 * @param latch Latch to be initialized.
 */
void tnt_latch_create(struct tnt_latch *latch);
/**
 * Destroy the given latch.
 */
void tnt_latch_destroy(struct tnt_latch *latch);
/**
 * Set the latch to the locked state. If it's already locked
 * returns -1 value immediately otherwise returns 0.
 *
 * @param latch Latch to be locked.
 */
int tnt_latch_trylock(struct tnt_latch *latch);
/**
 * Unlock the locked latch.
 *
 * @param latch Latch to be unlocked.
 */
void tnt_latch_unlock(struct tnt_latch *latch);


#endif /* TARANTOOL_LATCH_H_INCLUDED */
