#ifndef TARANTOOL_LIB_CORE_RANDOM_H_INCLUDED
#define TARANTOOL_LIB_CORE_RANDOM_H_INCLUDED
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

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif
void
random_init(void);

void
random_free(void);

void
random_bytes(char *buf, size_t size);

/**
 * Just 8 random_bytes().
 */
uint64_t
real_random(void);

/**
 * xoshiro256++ pseudo random generator.
 * http://prng.di.unimi.it/
 * State is initialized with random_bytes().
 */
uint64_t
xoshiro_random(void);

const char *
xoshiro_state_str(void);

/**
 * Return a random int64_t number within given boundaries,
 * including both.
 *
 * Instead of blindly calculating a modulo, this function uses
 * unbiased bitmask with rejection method to provide number in
 * given boundaries while preserving uniform distribution and
 * avoiding overflow. It uses random bytes as a basis.
 */
int64_t
real_random_in_range(int64_t min, int64_t max);

/**
 * Return a pseudo random int64_t number within given boundaries,
 * including both.
 *
 * Instead of blindly calculating a modulo, this function uses
 * unbiased bitmask with rejection method to provide number in
 * given boundaries while preserving uniform distribution and
 * avoiding overflow. It uses xoshiro256++ as an uint64 pseudo
 * random generator.
 */
int64_t
pseudo_random_in_range(int64_t min, int64_t max);

#if defined(__cplusplus)
}
#endif /* extern "C" */
#endif /* TARANTOOL_LIB_CORE_RANDOM_H_INCLUDED */
