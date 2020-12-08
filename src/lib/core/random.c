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
#include "random.h"
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include "bit/bit.h"
#include "say.h"
#include "tt_static.h"

static int rfd;

static uint64_t state[4];

void
random_init(void)
{
	int seed;
	rfd = open("/dev/urandom", O_RDONLY);
	if (rfd == -1)
		rfd = open("/dev/random", O_RDONLY | O_NONBLOCK);
	if (rfd == -1) {
		struct timeval tv;
		gettimeofday(&tv, 0);
		seed = (getpid() << 16) ^ getuid() ^ tv.tv_sec ^ tv.tv_usec;
		goto srand;
	}

	int flags;
	if ( (flags = fcntl(rfd, F_GETFD)) < 0 ||
	     fcntl(rfd, F_SETFD, flags | FD_CLOEXEC) < 0)
		say_syserror("fcntl, fd=%i", rfd);

	ssize_t res = read(rfd, &seed, sizeof(seed));
	(void) res;
srand:
	srandom(seed);
	srand(seed);

	random_bytes((char *)state, sizeof(state));
}

void
random_free(void)
{
	if (rfd == -1)
		return;
	close(rfd);
}

void
random_bytes(char *buf, size_t size)
{
	size_t generated = 0;

	if (rfd == -1)
		goto rand;

	int attempt = 0;
	while (generated < size) {
		ssize_t n = read(rfd, buf + generated, size - generated);
		if (n <= 0) {
			if (attempt++ > 5)
				break;
			continue;
		}
		generated += n;
		attempt = 0;
	}
rand:
	/* fill remaining bytes with PRNG */
	while (generated < size)
		buf[generated++] = rand();
}

uint64_t
real_random(void)
{
	uint64_t result;
	random_bytes((char *)&result, sizeof(result));
	return result;
}

/**
 * Helper function for the xoshiro256++ pseudo random generator:
 * rotate left.
 */
static inline uint64_t
rotl(uint64_t x, int k)
{
	return (x << k) | (x >> (64 - k));
}

/**
 * xoshiro256++ pseudo random generator.
 * http://prng.di.unimi.it/
 * State is initialized with random_bytes().
 *
 * It is fast and doesn’t fail any known statistical test.
 * About 2 times faster than conventional LCG rand() and
 * mersenne-twister algorithms. Also both of them do fail
 * some statistical tests.
 * Here are some other reasons to choose xoshiro over
 * mersenne-twister: https://arxiv.org/pdf/1910.06437.pdf
 */
uint64_t
xoshiro_random(void)
{
	const uint64_t result = rotl(state[0] + state[3], 23) + state[0];
	const uint64_t t = state[1] << 17;
	state[2] ^= state[0];
	state[3] ^= state[1];
	state[1] ^= state[2];
	state[0] ^= state[3];
	state[2] ^= t;
	state[3] = rotl(state[3], 45);
	return result;
}

const char *
xoshiro_state_str(void)
{
	return tt_sprintf("%llu %llu %llu %llu", (unsigned long long)state[0],
						 (unsigned long long)state[1],
						 (unsigned long long)state[2],
						 (unsigned long long)state[3]);
}

/**
 * Following functions random in range functions are implemented
 * to provide number in given boundaries while preserving uniform
 * distribution and avoiding overflow. They adopt unbiased bitmask
 * with rejection method from the following article:
 * https://www.pcg-random.org/posts/bounded-rands.html
 *
 * The idea is to completely avoid division and remainder operations.
 * We calculate the bit mask equal to (2^k - 1), where k is the
 * smallest value such that 2^k is greater than (max - min) and use
 * it to get a random number in range [0, 2^k). If the number is too
 * large for our range, it is being discarded. Finally we just add min
 * to get number in given range [min, max].
 */
int64_t
real_random_in_range(int64_t min, int64_t max)
{
	assert(max >= min);
	uint64_t range = (uint64_t)max - min;
	uint64_t mask = UINT64_MAX >> bit_clz_u64(range | 1);
	uint64_t result;
	do {
		result = real_random() & mask;
	} while (result > range);
	return min + result;
}

int64_t
pseudo_random_in_range(int64_t min, int64_t max)
{
	assert(max >= min);
	uint64_t range = (uint64_t)max - min;
	uint64_t mask = UINT64_MAX >> bit_clz_u64(range | 1);
	uint64_t result;
	do {
		result = xoshiro_random() & mask;
	} while (result > range);
	return min + result;
}
