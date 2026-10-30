/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2024, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "trivia/util.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct space;
struct index;

/**
 * Weak index reference.
 *
 * In contrast to a strong reference, it doesn't prevent the index from being
 * destroyed. This is achieved by checking if the index can be found in the
 * space cache by id every time the reference is accessed.
 *
 * Since ephemeral spaces aren't added to the space cache, the check is skipped
 * for them. The user may still use a weak reference to an ephemeral index but
 * it will work exactly like a plain pointer.
 */
struct index_weak_ref {
	/** Referenced space id. 0 iff the space is ephemeral. */
	uint32_t space_id;
	/** Referenced index id. */
	uint32_t index_id;
	/** Space cache version after the last successful dereference. */
	uint32_t space_cache_version;
	/**
	 * Pointer to the referenced space. May be stale.
	 * NULL iff this is a reference to an ephemeral index.
	 *
	 * Do not access directly, use index_weak_ref_get_space() or
	 * index_weak_ref_get_space_checked().
	 */
	struct space *space;
	/**
	 * Pointer to the referenced index. May be stale.
	 *
	 * Do not access directly, use index_weak_ref_get_index() or
	 * index_weak_ref_get_index_checked().
	 */
	struct index *index;
};

/**
 * Creates a weak reference to the given index.
 *
 * A newly created reference is guaranteed to be valid
 * (index_weak_ref_is_checked() returns true).
 */
void
index_weak_ref_create(struct index_weak_ref *ref, struct index *index);

/**
 * Returns true if the pointers to the space and index stored in the given weak
 * reference are guaranteed to be valid.
 */
static inline bool
index_weak_ref_is_checked(struct index_weak_ref *ref)
{
	if (ref->space_id == 0) {
		/*
		 * This is a reference to an ephemeral index. Since ephemeral
		 * spaces aren't stored in the space cache, we can't possibly
		 * check it so we assume it's always valid.
		 */
		return true;
	}
	/*
	 * If the space cache hasn't been updated since the last check,
	 * the reference must be valid, otherwise we need to recheck it.
	 */
	extern uint32_t space_cache_version;
	return ref->space_cache_version == space_cache_version;
}

/** Slow path of index_weak_ref_check(). */
bool
index_weak_ref_check_slow(struct index_weak_ref *ref);

/**
 * Checks a weak reference, updating the pointer to the space if necessary.
 * Returns false if the reference is invalid.
 */
static inline bool
index_weak_ref_check(struct index_weak_ref *ref)
{
	if (likely(index_weak_ref_is_checked(ref)))
		return true;
	return index_weak_ref_check_slow(ref);
}

/**
 * Gets pointers to the space and index from a weak reference, assuming that
 * it was checked with index_weak_ref_check().
 */
static inline void
index_weak_ref_get_checked(struct index_weak_ref *ref,
			   struct space **space, struct index **index)
{
	assert(index_weak_ref_is_checked(ref));
	*space = ref->space;
	*index = ref->index;
}

/**
 * Gets a pointer to the space from a weak reference, assuming that it was
 * checked with index_weak_ref_check(). Returns NULL if the space is ephemeral.
 */
static inline struct space *
index_weak_ref_get_space_checked(struct index_weak_ref *ref)
{
	assert(index_weak_ref_is_checked(ref));
	return ref->space;
}

/**
 * Gets a pointer to the index from a weak reference, assuming that it was
 * checked with index_weak_ref_check().
 */
static inline struct index *
index_weak_ref_get_index_checked(struct index_weak_ref *ref)
{
	assert(index_weak_ref_is_checked(ref));
	return ref->index;
}

/**
 * Gets pointers to the space and index from a weak reference if it's valid.
 * Returns false if the reference is invalid.
 */
static inline bool
index_weak_ref_get(struct index_weak_ref *ref,
		   struct space **space, struct index **index)
{
	if (!index_weak_ref_check(ref))
		return false;
	index_weak_ref_get_checked(ref, space, index);
	return true;
}

/**
 * Gets a pointer to the space from a weak reference if it's valid.
 * Returns NULL if the reference is invalid or the space is ephemeral.
 */
static inline struct space *
index_weak_ref_get_space(struct index_weak_ref *ref)
{
	if (!index_weak_ref_check(ref))
		return NULL;
	return index_weak_ref_get_space_checked(ref);
}

/**
 * Gets a pointer to the index from a weak reference if it's valid.
 * Returns NULL if the reference is invalid.
 */
static inline struct index *
index_weak_ref_get_index(struct index_weak_ref *ref)
{
	if (!index_weak_ref_check(ref))
		return NULL;
	return index_weak_ref_get_index_checked(ref);
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
