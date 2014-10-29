#ifndef INCLUDES_TARANTOOL_SMALL_QUOTA_H
#define INCLUDES_TARANTOOL_SMALL_QUOTA_H
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

#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

enum {
	QUOTA_MAX_ALLOC = 0xFFFFFFFFull * 1024u,
	QUOTA_GRANULARITY = 1024
};

/** A basic limit on memory usage */
struct quota {
	/* high order dword is total available memory and low order dword
	 * - currently used memory both in QUOTA_GRANULARITY units
	 */
	uint64_t value;
};

/**
 * Initialize quota with gived memory limit
 */
static inline void
quota_init(struct quota *quota, size_t total)
{
	uint64_t new_total_in_granul = (total + (QUOTA_GRANULARITY - 1))
				        / QUOTA_GRANULARITY;
	quota->value = new_total_in_granul << 32;
}

/**
 * Provide wrappers around gcc built-ins for now.
 * These built-ins work with all numeric types - may not
 * be the case when another implementation is used.
 * Private use only.
 */
#define atomic_cas(a, b, c) __sync_val_compare_and_swap(a, b, c)

/**
 * Get current quota limit
 */
static inline size_t
quota_get_total(const struct quota *quota)
{
	return (quota->value >> 32) * QUOTA_GRANULARITY;
}

/**
 * Get current quota usage
 */
static inline size_t
quota_get_used(const struct quota *quota)
{
	return (quota->value & 0xFFFFFFFFu) * QUOTA_GRANULARITY;
}

static inline void
quota_get_total_and_used(struct quota *quota, size_t *total, size_t *used)
{
	uint64_t value = quota->value;
	*total = (value >> 32) * QUOTA_GRANULARITY;
	*used = (value & 0xFFFFFFFFu) * QUOTA_GRANULARITY;
}

/**
 * Set quota memory limit.
 * returns 0 on success
 * returns -1 on error - if it's not possible to decrease limit
 * due to greater current usage
 */
static inline int
quota_set(struct quota *quota, size_t new_total)
{
	uint32_t new_total_in_granul = (new_total + (QUOTA_GRANULARITY - 1))
				   / QUOTA_GRANULARITY;
	while (1) {
		uint64_t old_value = quota->value;
		/* uint32_t cur_total = old_value >> 32; */
		uint32_t cur_used = old_value & 0xFFFFFFFFu;
		if (new_total_in_granul < cur_used)
			return  -1;
		uint64_t new_value = (((uint64_t)new_total_in_granul) << 32)
				     | cur_used;
		if (atomic_cas(&quota->value, old_value, new_value) == old_value)
			break;
	}
	return 0;
}

/**
 * Use up a quota
 * returns 0 on success
 * returns -1 on error - if quota limit reached
 */
static inline int
quota_use(struct quota *quota, size_t size)
{
	uint32_t size_in_granul = (size + (QUOTA_GRANULARITY - 1))
				  / QUOTA_GRANULARITY;
	assert(size_in_granul);
	while (1) {
		uint64_t old_value = quota->value;
		uint32_t cur_total = old_value >> 32;
		uint32_t old_used = old_value & 0xFFFFFFFFu;

		uint32_t new_used = old_used + size_in_granul;
		assert(new_used > old_used);

		if (new_used > cur_total)
			return -1;

		uint64_t new_value = (((uint64_t)cur_total) << 32)
				     | new_used;

		if (atomic_cas(&quota->value, old_value, new_value) == old_value)
			break;
	}
	return 0;
}

/** Release used memory */
static inline void
quota_release(struct quota *quota, size_t size)
{
	uint32_t size_in_granul = (size + (QUOTA_GRANULARITY - 1))
				  / QUOTA_GRANULARITY;
	assert(size_in_granul);
	while (1) {
		uint64_t old_value = quota->value;
		uint32_t cur_total = old_value >> 32;
		uint32_t old_used = old_value & 0xFFFFFFFFu;

		assert(size_in_granul <= old_used);
		uint32_t new_used = old_used - size_in_granul;

		uint64_t new_value = (((uint64_t)cur_total) << 32)
				     | new_used;

		if (atomic_cas(&quota->value, old_value, new_value) == old_value)
			break;
	}
}

#undef atomic_cas

#if defined(__cplusplus)
} /* extern "C" { */
#endif /* defined(__cplusplus) */
#endif /* INCLUDES_TARANTOOL_SMALL_QUOTA_H */
