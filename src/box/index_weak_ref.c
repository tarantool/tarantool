/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2024, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "index_weak_ref.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "space.h"
#include "space_cache.h"
#include "index.h"

void
index_weak_ref_create(struct index_weak_ref *ref, struct index *index)
{
	ref->space_id = index->def->space_id;
	ref->index_id = index->def->iid;
	ref->space_cache_version = space_cache_version;
	if (ref->space_id == 0) {
		/* Ephemeral space. Not present in the space cache. */
		ref->space = NULL;
	} else {
		ref->space = space_cache_find(ref->space_id);
		assert(ref->space != NULL);
	}
	ref->index = index;
	assert(index_weak_ref_is_checked(ref));
}

bool
index_weak_ref_check_slow(struct index_weak_ref *ref)
{
	assert(!index_weak_ref_is_checked(ref));
	assert(ref->space_id != 0);
	struct space *space = space_by_id(ref->space_id);
	if (space == NULL) {
		/* Space was dropped. */
		return false;
	}
	struct index *index = space_index(space, ref->index_id);
	if (index != ref->index ||
	    index->space_cache_version > ref->space_cache_version) {
		/* Index was altered. */
		return false;
	}
	ref->space_cache_version = space_cache_version;
	ref->space = space;
	assert(index_weak_ref_is_checked(ref));
	return true;
}
