#ifndef INCLUDES_TARANTOOL_SMALL_QUOTA_H
#define INCLUDES_TARANTOOL_SMALL_QUOTA_H
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
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>
#include "third_party/pmatomic.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#define QUOTA_UNIT_SIZE 1024ULL

static const size_t QUOTA_MAX = QUOTA_UNIT_SIZE * UINT32_MAX > SIZE_MAX ?
				SIZE_MAX : QUOTA_UNIT_SIZE * UINT32_MAX;

/** A basic limit on memory usage */
struct quota {
	/**
	 * High order dword is the total available memory
	 * and the low order dword is the  currently used amount.
	 * Both values are represented in units of size
	 * QUOTA_UNIT_SIZE.
	 */
	uint64_t value;
};

/**
 * Initialize quota with a given memory limit
 */
static inline void
quota_init(struct quota *quota, size_t total)
{
	uint64_t new_total = (total + (QUOTA_UNIT_SIZE - 1)) /
				QUOTA_UNIT_SIZE;
	quota->value = new_total << 32;
}

/**
 * Get current quota limit
 */
static inline size_t
quota_total(const struct quota *quota)
{
	return (quota->value >> 32) * QUOTA_UNIT_SIZE;
}

/**
 * Get current quota usage
 */
static inline size_t
quota_used(const struct quota *quota)
{
	return (quota->value & UINT32_MAX) * QUOTA_UNIT_SIZE;
}

static inline void
quota_get_total_and_used(struct quota *quota, size_t *total, size_t *used)
{
	uint64_t value = quota->value;
	*total = (value >> 32) * QUOTA_UNIT_SIZE;
	*used = (value & UINT32_MAX) * QUOTA_UNIT_SIZE;
}

/**
 * Set quota memory limit.
 * @retval > 0   aligned size set on success
 * @retval -1    error, i.e. when  it is not possible to decrease
 *               limit due to greater current usage
 */
static inline ssize_t
quota_set(struct quota *quota, size_t new_total)
{
	assert(new_total <= QUOTA_MAX);
	/* Align the new total */
	uint32_t new_total_in_units = (new_total + (QUOTA_UNIT_SIZE - 1)) /
					QUOTA_UNIT_SIZE;
	while (1) {
		uint64_t value = quota->value;
		uint32_t used_in_units = value & UINT32_MAX;
		if (new_total_in_units < used_in_units)
			return  -1;
		uint64_t new_value =
			((uint64_t) new_total_in_units << 32) | used_in_units;
		if (pm_atomic_compare_exchange_strong(&quota->value, &value, new_value))
			break;
	}
	return new_total_in_units * QUOTA_UNIT_SIZE;
}

/**
 * Use up a quota
 * @retval > 0 aligned value on success
 * @retval -1  on error - if quota limit reached
 */
static inline ssize_t
quota_use(struct quota *quota, size_t size)
{
	assert(size < QUOTA_MAX);
	uint32_t size_in_units = (size + (QUOTA_UNIT_SIZE - 1))
				  / QUOTA_UNIT_SIZE;
	assert(size_in_units);
	while (1) {
		uint64_t value = quota->value;
		uint32_t total_in_units = value >> 32;
		uint32_t used_in_units = value & UINT32_MAX;

		uint32_t new_used_in_units = used_in_units + size_in_units;
		assert(new_used_in_units > used_in_units);

		if (new_used_in_units > total_in_units)
			return -1;

		uint64_t new_value =
			((uint64_t) total_in_units << 32) | new_used_in_units;

		if (pm_atomic_compare_exchange_strong(&quota->value, &value, new_value))
			break;
	}
	return size_in_units * QUOTA_UNIT_SIZE;
}

/** Release used memory */
static inline void
quota_release(struct quota *quota, size_t size)
{
	assert(size < QUOTA_MAX);
	uint32_t size_in_units = (size + (QUOTA_UNIT_SIZE - 1))
				  / QUOTA_UNIT_SIZE;
	assert(size_in_units);
	while (1) {
		uint64_t value = quota->value;
		uint32_t total_in_units = value >> 32;
		uint32_t used_in_units = value & UINT32_MAX;

		assert(size_in_units <= used_in_units);
		uint32_t new_used_in_units = used_in_units - size_in_units;

		uint64_t new_value =
			((uint64_t) total_in_units << 32) | new_used_in_units;

		if (pm_atomic_compare_exchange_strong(&quota->value, &value, new_value))
			break;
	}
}

#undef QUOTA_UNIT_SIZE

#if defined(__cplusplus)
} /* extern "C" { */
#endif /* defined(__cplusplus) */
#endif /* INCLUDES_TARANTOOL_SMALL_QUOTA_H */
