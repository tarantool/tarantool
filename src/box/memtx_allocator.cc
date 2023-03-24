/*
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
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
#include "memtx_allocator.h"
#include "trivia/tuple.h"

struct memtx_tuple_rv *
memtx_tuple_rv_new(uint32_t version, struct rlist *list)
{
	assert(version > 0);
	/* Reuse the last read view if its version matches. */
	struct memtx_tuple_rv *last_rv = rlist_empty(list) ? nullptr :
		rlist_last_entry(list, struct memtx_tuple_rv, link);
	if (last_rv != nullptr) {
		uint32_t last_version = memtx_tuple_rv_version(last_rv);
		assert(last_version <= version);
		assert(last_rv->refs > 0);
		if (last_version == version) {
			last_rv->refs++;
			return last_rv;
		}
	}
	/* Proceed to creation of a new read view. */
	int count = 1;
	struct memtx_tuple_rv *rv;
	rlist_foreach_entry(rv, list, link)
		count++;
	struct memtx_tuple_rv *new_rv = (struct memtx_tuple_rv *)xmalloc(
			sizeof(*new_rv) + count * sizeof(*new_rv->lists));
	new_rv->count = count;
	/* Create one list per each open read view. */
	struct memtx_tuple_rv_list *l = &new_rv->lists[0];
	uint32_t prev_version = 0;
	rlist_foreach_entry(rv, list, link) {
		l->version = memtx_tuple_rv_version(rv);
		/* List must be sorted by read view version. */
		assert(l->version > prev_version);
		stailq_create(&l->tuples);
		l->mem_used = 0;
		prev_version = l->version;
		l++;
	}
	/* And one more list for self. */
	assert(l == &new_rv->lists[count - 1]);
	l->version = version;
	assert(l->version > prev_version);
	(void)prev_version;
	stailq_create(&l->tuples);
	l->mem_used = 0;
	rlist_add_tail_entry(list, new_rv, link);
	new_rv->refs = 1;
	return new_rv;
}

void
memtx_tuple_rv_delete(struct memtx_tuple_rv *rv, struct rlist *list,
		      struct stailq *tuples_to_free, size_t *mem_freed)
{
	*mem_freed = 0;
	assert(rv->refs > 0);
	if (--rv->refs > 0)
		return;
	struct memtx_tuple_rv *prev_rv = rlist_prev_entry_safe(rv, list, link);
	uint32_t prev_version = prev_rv == nullptr ? 0 :
				memtx_tuple_rv_version(prev_rv);
	/*
	 * Move tuples from lists with version <= prev_version to the list of
	 * the previous read view and delete all other tuples.
	 */
	int i = 0;
	int j = 0;
	while (i < rv->count) {
		struct memtx_tuple_rv_list *src = &rv->lists[i];
		if (src->version <= prev_version) {
			/*
			 * The tuples were allocated before the previous read
			 * view was opened. Move them to the previous read
			 * view's list.
			 */
			assert(prev_rv != nullptr);
			assert(j < prev_rv->count);
			struct memtx_tuple_rv_list *dst = &prev_rv->lists[j];
			/*
			 * The previous read view may have more lists, because
			 * some read views could have been closed by the time
			 * this read view was open.  Skip them.
			 */
			while (dst->version != src->version) {
				j++;
				assert(j < prev_rv->count);
				dst = &prev_rv->lists[j];
			}
			stailq_concat(&dst->tuples, &src->tuples);
			dst->mem_used += src->mem_used;
			j++;
		} else {
			/*
			 * The tuples were allocated after the previous read
			 * view was opened and freed before the next read view
			 * was opened. Free them immediately.
			 */
			stailq_concat(tuples_to_free, &src->tuples);
			*mem_freed += src->mem_used;
		}
		i++;
	}
	rlist_del_entry(rv, link);
	free(rv);
}

void
memtx_tuple_rv_add(struct memtx_tuple_rv *rv, struct memtx_tuple *tuple,
		   size_t mem_used)
{
	/*
	 * Binary search the list with min version such that
	 * list->version > tuple->version.
	 */
	int begin = 0;
	int end = rv->count;
	struct memtx_tuple_rv_list *found = nullptr;
	while (begin != end) {
		int middle = begin + (end - begin) / 2;
		struct memtx_tuple_rv_list *l = &rv->lists[middle];
		if (l->version <= tuple->version) {
			begin = middle + 1;
		} else {
			found = l;
			end = middle;
		}
	}
	assert(found != nullptr);
	stailq_add_entry(&found->tuples, tuple, in_gc);
	found->mem_used += mem_used;
}

void
memtx_allocators_init(struct allocator_settings *settings)
{
	foreach_allocator<allocator_create,
		struct allocator_settings *&>(settings);

	foreach_memtx_allocator<allocator_create>();
}

void
memtx_allocators_destroy()
{
	foreach_memtx_allocator<allocator_destroy>();
	foreach_allocator<allocator_destroy>();
}

struct memtx_allocator_open_read_view {
	/** Opens a read view for the specified MemtxAllocator. */
	template<typename Allocator>
	void invoke(memtx_allocators_read_view &rv_all,
		    const struct read_view_opts &opts)
	{
		util::get<typename Allocator::ReadView *>(rv_all) =
			Allocator::open_read_view(&opts);
	}
};

memtx_allocators_read_view
memtx_allocators_open_read_view(const struct read_view_opts *opts)
{
	memtx_allocators_read_view rv;
	foreach_memtx_allocator<memtx_allocator_open_read_view,
				memtx_allocators_read_view &,
				const struct read_view_opts &>(rv, *opts);
	return rv;
}

struct memtx_allocator_close_read_view {
	/** Closes a read view and sets the read view ptr to null. */
	template<typename Allocator>
	void invoke(memtx_allocators_read_view &rv_all)
	{
		typename Allocator::ReadView *&rv =
			util::get<typename Allocator::ReadView *>(rv_all);
		Allocator::close_read_view(rv);
		rv = nullptr;
	}
};

void
memtx_allocators_close_read_view(memtx_allocators_read_view rv)
{
	foreach_memtx_allocator<memtx_allocator_close_read_view,
				memtx_allocators_read_view &>(rv);
}

/** Sums allocator statistics. */
struct memtx_allocator_add_stats {
	template<typename Allocator>
	void invoke(struct memtx_allocator_stats &stats)
	{
		memtx_allocator_stats_add(&stats, &Allocator::stats);
	}
};

void
memtx_allocators_stats(struct memtx_allocator_stats *stats)
{
	memtx_allocator_stats_create(stats);
	foreach_memtx_allocator<memtx_allocator_add_stats,
				struct memtx_allocator_stats &>(*stats);
}
