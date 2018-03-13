/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "vy_index.h"

#include "trivia/util.h"
#include <stdbool.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "assoc.h"
#include "diag.h"
#include "errcode.h"
#include "histogram.h"
#include "index_def.h"
#include "say.h"
#include "schema.h"
#include "tuple.h"
#include "vy_log.h"
#include "vy_mem.h"
#include "vy_range.h"
#include "vy_run.h"
#include "vy_stat.h"
#include "vy_stmt.h"
#include "vy_upsert.h"
#include "vy_read_set.h"

void
vy_index_validate_formats(const struct vy_index *index)
{
	(void) index;
	assert(index->disk_format != NULL);
	assert(index->mem_format != NULL);
	assert(index->mem_format_with_colmask != NULL);
	assert(index->upsert_format != NULL);
	uint32_t index_field_count = index->mem_format->index_field_count;
	(void) index_field_count;
	if (index->id == 0) {
		assert(index->disk_format == index->mem_format);
		assert(index->disk_format->index_field_count ==
		       index_field_count);
		assert(index->mem_format_with_colmask->index_field_count ==
		       index_field_count);
	} else {
		assert(index->disk_format != index->mem_format);
		assert(index->disk_format->index_field_count <=
		       index_field_count);
	}
	assert(index->upsert_format->index_field_count == index_field_count);
	assert(index->mem_format_with_colmask->index_field_count ==
	       index_field_count);
}

int
vy_index_env_create(struct vy_index_env *env, const char *path,
		    int64_t *p_generation,
		    vy_upsert_thresh_cb upsert_thresh_cb,
		    void *upsert_thresh_arg)
{
	env->key_format = tuple_format_new(&vy_tuple_format_vtab,
					   NULL, 0, 0, NULL, 0, NULL);
	if (env->key_format == NULL)
		return -1;
	tuple_format_ref(env->key_format);
	env->empty_key = vy_stmt_new_select(env->key_format, NULL, 0);
	if (env->empty_key == NULL) {
		tuple_format_unref(env->key_format);
		return -1;
	}
	env->path = path;
	env->p_generation = p_generation;
	env->upsert_thresh_cb = upsert_thresh_cb;
	env->upsert_thresh_arg = upsert_thresh_arg;
	env->too_long_threshold = TIMEOUT_INFINITY;
	env->index_count = 0;
	return 0;
}

void
vy_index_env_destroy(struct vy_index_env *env)
{
	tuple_unref(env->empty_key);
	tuple_format_unref(env->key_format);
}

const char *
vy_index_name(struct vy_index *index)
{
	char *buf = tt_static_buf();
	snprintf(buf, TT_STATIC_BUF_LEN, "%u/%u",
		 (unsigned)index->space_id, (unsigned)index->id);
	return buf;
}

size_t
vy_index_mem_tree_size(struct vy_index *index)
{
	struct vy_mem *mem;
	size_t size = index->mem->tree_extent_size;
	rlist_foreach_entry(mem, &index->sealed, in_sealed)
		size += mem->tree_extent_size;
	return size;
}

struct vy_index *
vy_index_new(struct vy_index_env *index_env, struct vy_cache_env *cache_env,
	     struct vy_mem_env *mem_env, struct index_def *index_def,
	     struct tuple_format *format, struct vy_index *pk)
{
	static int64_t run_buckets[] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 20, 25, 50, 100,
	};

	assert(index_def->key_def->part_count > 0);
	assert(index_def->iid == 0 || pk != NULL);

	struct vy_index *index = calloc(1, sizeof(struct vy_index));
	if (index == NULL) {
		diag_set(OutOfMemory, sizeof(struct vy_index),
			 "calloc", "struct vy_index");
		goto fail;
	}
	index->env = index_env;

	index->tree = malloc(sizeof(*index->tree));
	if (index->tree == NULL) {
		diag_set(OutOfMemory, sizeof(*index->tree),
			 "malloc", "vy_range_tree_t");
		goto fail_tree;
	}

	struct key_def *key_def = key_def_dup(index_def->key_def);
	if (key_def == NULL)
		goto fail_key_def;

	struct key_def *cmp_def = key_def_dup(index_def->cmp_def);
	if (cmp_def == NULL)
		goto fail_cmp_def;

	index->cmp_def = cmp_def;
	index->key_def = key_def;
	if (index_def->iid == 0) {
		/*
		 * Disk tuples can be returned to an user from a
		 * primary key. And they must have field
		 * definitions as well as space->format tuples.
		 */
		index->disk_format = format;
		tuple_format_ref(format);
	} else {
		index->disk_format = tuple_format_new(&vy_tuple_format_vtab,
						      &cmp_def, 1, 0, NULL, 0,
						      NULL);
		if (index->disk_format == NULL)
			goto fail_format;
		for (uint32_t i = 0; i < cmp_def->part_count; ++i) {
			uint32_t fieldno = cmp_def->parts[i].fieldno;
			index->disk_format->fields[fieldno].is_nullable =
				format->fields[fieldno].is_nullable;
		}
	}
	tuple_format_ref(index->disk_format);

	if (index_def->iid == 0) {
		index->upsert_format =
			vy_tuple_format_new_upsert(format);
		if (index->upsert_format == NULL)
			goto fail_upsert_format;
		tuple_format_ref(index->upsert_format);

		index->mem_format_with_colmask =
			vy_tuple_format_new_with_colmask(format);
		if (index->mem_format_with_colmask == NULL)
			goto fail_mem_format_with_colmask;
		tuple_format_ref(index->mem_format_with_colmask);
	} else {
		index->mem_format_with_colmask = pk->mem_format_with_colmask;
		index->upsert_format = pk->upsert_format;
		tuple_format_ref(index->mem_format_with_colmask);
		tuple_format_ref(index->upsert_format);
	}

	if (vy_index_stat_create(&index->stat) != 0)
		goto fail_stat;

	index->run_hist = histogram_new(run_buckets, lengthof(run_buckets));
	if (index->run_hist == NULL)
		goto fail_run_hist;

	index->mem = vy_mem_new(mem_env, *index->env->p_generation,
				cmp_def, format, index->mem_format_with_colmask,
				index->upsert_format, schema_version);
	if (index->mem == NULL)
		goto fail_mem;

	index->refs = 1;
	index->commit_lsn = -1;
	index->dump_lsn = -1;
	vy_cache_create(&index->cache, cache_env, cmp_def);
	rlist_create(&index->sealed);
	vy_range_tree_new(index->tree);
	vy_range_heap_create(&index->range_heap);
	rlist_create(&index->runs);
	index->pk = pk;
	if (pk != NULL)
		vy_index_ref(pk);
	index->mem_format = format;
	tuple_format_ref(index->mem_format);
	index->in_dump.pos = UINT32_MAX;
	index->in_compact.pos = UINT32_MAX;
	index->space_id = index_def->space_id;
	index->id = index_def->iid;
	index->opts = index_def->opts;
	index->check_is_unique = index->opts.is_unique;
	vy_index_read_set_new(&index->read_set);

	index_env->index_count++;
	vy_index_validate_formats(index);
	return index;

fail_mem:
	histogram_delete(index->run_hist);
fail_run_hist:
	vy_index_stat_destroy(&index->stat);
fail_stat:
	tuple_format_unref(index->mem_format_with_colmask);
fail_mem_format_with_colmask:
	tuple_format_unref(index->upsert_format);
fail_upsert_format:
	tuple_format_unref(index->disk_format);
fail_format:
	free(cmp_def);
fail_cmp_def:
	free(key_def);
fail_key_def:
	free(index->tree);
fail_tree:
	free(index);
fail:
	return NULL;
}

static struct vy_range *
vy_range_tree_free_cb(vy_range_tree_t *t, struct vy_range *range, void *arg)
{
	(void)t;
	(void)arg;
	struct vy_slice *slice;
	rlist_foreach_entry(slice, &range->slices, in_range)
		vy_slice_wait_pinned(slice);
	vy_range_delete(range);
	return NULL;
}

void
vy_index_delete(struct vy_index *index)
{
	assert(index->refs == 0);
	assert(index->in_dump.pos == UINT32_MAX);
	assert(index->in_compact.pos == UINT32_MAX);
	assert(vy_index_read_set_empty(&index->read_set));
	assert(index->env->index_count > 0);

	index->env->index_count--;

	if (index->pk != NULL)
		vy_index_unref(index->pk);

	struct vy_mem *mem, *next_mem;
	rlist_foreach_entry_safe(mem, &index->sealed, in_sealed, next_mem)
		vy_mem_delete(mem);
	vy_mem_delete(index->mem);

	struct vy_run *run, *next_run;
	rlist_foreach_entry_safe(run, &index->runs, in_index, next_run)
		vy_index_remove_run(index, run);

	vy_range_tree_iter(index->tree, NULL, vy_range_tree_free_cb, NULL);
	vy_range_heap_destroy(&index->range_heap);
	tuple_format_unref(index->disk_format);
	tuple_format_unref(index->mem_format_with_colmask);
	tuple_format_unref(index->upsert_format);
	free(index->cmp_def);
	free(index->key_def);
	histogram_delete(index->run_hist);
	vy_index_stat_destroy(&index->stat);
	vy_cache_destroy(&index->cache);
	tuple_format_unref(index->mem_format);
	free(index->tree);
	TRASH(index);
	free(index);
}

void
vy_index_swap(struct vy_index *old_index, struct vy_index *new_index)
{
	assert(old_index->stat.memory.count.rows == 0);
	assert(new_index->stat.memory.count.rows == 0);

	SWAP(old_index->dump_lsn, new_index->dump_lsn);
	SWAP(old_index->range_count, new_index->range_count);
	SWAP(old_index->run_count, new_index->run_count);
	SWAP(old_index->stat, new_index->stat);
	SWAP(old_index->run_hist, new_index->run_hist);
	SWAP(old_index->tree, new_index->tree);
	SWAP(old_index->range_heap, new_index->range_heap);
	rlist_swap(&old_index->runs, &new_index->runs);
}

int
vy_index_init_range_tree(struct vy_index *index)
{
	struct vy_range *range = vy_range_new(vy_log_next_id(), NULL, NULL,
					      index->cmp_def);
	if (range == NULL)
		return -1;

	assert(index->range_count == 0);
	vy_index_add_range(index, range);
	vy_index_acct_range(index, range);
	return 0;
}

int
vy_index_create(struct vy_index *index)
{
	/* Make index directory. */
	int rc;
	char path[PATH_MAX];
	vy_index_snprint_path(path, sizeof(path), index->env->path,
			      index->space_id, index->id);
	char *path_sep = path;
	while (*path_sep == '/') {
		/* Don't create root */
		++path_sep;
	}
	while ((path_sep = strchr(path_sep, '/'))) {
		/* Recursively create path hierarchy */
		*path_sep = '\0';
		rc = mkdir(path, 0777);
		if (rc == -1 && errno != EEXIST) {
			diag_set(SystemError, "failed to create directory '%s'",
		                 path);
			*path_sep = '/';
			return -1;
		}
		*path_sep = '/';
		++path_sep;
	}
	rc = mkdir(path, 0777);
	if (rc == -1 && errno != EEXIST) {
		diag_set(SystemError, "failed to create directory '%s'",
			 path);
		return -1;
	}

	/* Allocate initial range. */
	return vy_index_init_range_tree(index);
}

/** vy_index_recovery_cb() argument. */
struct vy_index_recovery_cb_arg {
	/** Index being recovered. */
	struct vy_index *index;
	/** Last recovered range. */
	struct vy_range *range;
	/** Vinyl run environment. */
	struct vy_run_env *run_env;
	/**
	 * All recovered runs hashed by ID.
	 * It is needed in order not to load the same
	 * run each time a slice is created for it.
	 */
	struct mh_i64ptr_t *run_hash;
	/**
	 * True if force_recovery mode is enabled.
	 */
	bool force_recovery;
};

/** Index recovery callback, passed to vy_recovery_load_index(). */
static int
vy_index_recovery_cb(const struct vy_log_record *record, void *cb_arg)
{
	struct vy_index_recovery_cb_arg *arg = cb_arg;
	struct vy_index *index = arg->index;
	struct vy_range *range = arg->range;
	struct vy_run_env *run_env = arg->run_env;
	struct mh_i64ptr_t *run_hash = arg->run_hash;
	bool force_recovery = arg->force_recovery;
	struct tuple_format *key_format = index->env->key_format;
	struct tuple *begin = NULL, *end = NULL;
	struct vy_run *run;
	struct vy_slice *slice;
	bool success = false;

	assert(record->type == VY_LOG_CREATE_INDEX || index->commit_lsn >= 0);

	if (record->type == VY_LOG_INSERT_RANGE ||
	    record->type == VY_LOG_INSERT_SLICE) {
		if (record->begin != NULL) {
			begin = vy_key_from_msgpack(key_format, record->begin);
			if (begin == NULL)
				goto out;
		}
		if (record->end != NULL) {
			end = vy_key_from_msgpack(key_format, record->end);
			if (end == NULL)
				goto out;
		}
	}

	switch (record->type) {
	case VY_LOG_CREATE_INDEX:
		assert(record->index_id == index->id);
		assert(record->space_id == index->space_id);
		assert(index->commit_lsn < 0);
		assert(record->index_lsn >= 0);
		index->commit_lsn = record->index_lsn;
		break;
	case VY_LOG_DUMP_INDEX:
		assert(record->index_lsn == index->commit_lsn);
		index->dump_lsn = record->dump_lsn;
		break;
	case VY_LOG_TRUNCATE_INDEX:
		assert(record->index_lsn == index->commit_lsn);
		index->truncate_count = record->truncate_count;
		break;
	case VY_LOG_DROP_INDEX:
		assert(record->index_lsn == index->commit_lsn);
		index->is_dropped = true;
		/*
		 * If the index was dropped, we don't need to replay
		 * truncate (see vy_prepare_truncate_space()).
		 */
		index->truncate_count = UINT64_MAX;
		break;
	case VY_LOG_PREPARE_RUN:
		break;
	case VY_LOG_CREATE_RUN:
		if (record->is_dropped)
			break;
		assert(record->index_lsn == index->commit_lsn);
		run = vy_run_new(run_env, record->run_id);
		if (run == NULL)
			goto out;
		run->dump_lsn = record->dump_lsn;
		if (vy_run_recover(run, index->env->path,
				   index->space_id, index->id) != 0 &&
		     (!force_recovery ||
		     vy_run_rebuild_index(run, index->env->path,
					  index->space_id, index->id,
					  index->cmp_def, index->key_def,
					  index->mem_format,
					  index->upsert_format,
					  &index->opts) != 0)) {
			vy_run_unref(run);
			goto out;
		}
		struct mh_i64ptr_node_t node = { run->id, run };
		if (mh_i64ptr_put(run_hash, &node,
				  NULL, NULL) == mh_end(run_hash)) {
			diag_set(OutOfMemory, 0,
				 "mh_i64ptr_put", "mh_i64ptr_node_t");
			vy_run_unref(run);
			goto out;
		}
		break;
	case VY_LOG_DROP_RUN:
		break;
	case VY_LOG_INSERT_RANGE:
		assert(record->index_lsn == index->commit_lsn);
		range = vy_range_new(record->range_id, begin, end,
				     index->cmp_def);
		if (range == NULL)
			goto out;
		if (range->begin != NULL && range->end != NULL &&
		    vy_key_compare(range->begin, range->end,
				   index->cmp_def) >= 0) {
			diag_set(ClientError, ER_INVALID_VYLOG_FILE,
				 tt_sprintf("begin >= end for range id %lld",
					    (long long)range->id));
			vy_range_delete(range);
			goto out;
		}
		vy_index_add_range(index, range);
		arg->range = range;
		break;
	case VY_LOG_INSERT_SLICE:
		assert(range != NULL);
		assert(range->id == record->range_id);
		mh_int_t k = mh_i64ptr_find(run_hash, record->run_id, NULL);
		assert(k != mh_end(run_hash));
		run = mh_i64ptr_node(run_hash, k)->val;
		slice = vy_slice_new(record->slice_id, run, begin, end,
				     index->cmp_def);
		if (slice == NULL)
			goto out;
		vy_range_add_slice(range, slice);
		break;
	default:
		unreachable();
	}
	success = true;
out:
	if (begin != NULL)
		tuple_unref(begin);
	if (end != NULL)
		tuple_unref(end);
	return success ? 0 : -1;
}

int
vy_index_recover(struct vy_index *index, struct vy_recovery *recovery,
		 struct vy_run_env *run_env, int64_t lsn,
		 bool is_checkpoint_recovery, bool force_recovery)
{
	assert(index->range_count == 0);

	struct vy_index_recovery_cb_arg arg = {
		.index = index,
		.range = NULL,
		.run_env = run_env,
		.run_hash = NULL,
		.force_recovery = force_recovery,
	};
	arg.run_hash = mh_i64ptr_new();
	if (arg.run_hash == NULL) {
		diag_set(OutOfMemory, 0, "mh_i64ptr_new", "mh_i64ptr_t");
		return -1;
	}

	/*
	 * Backward compatibility fixup: historically, we used
	 * box.info.signature for LSN of index creation, which
	 * lags behind the LSN of the record that created the
	 * index by 1. So for legacy indexes use the LSN from
	 * index options.
	 */
	if (index->opts.lsn != 0)
		lsn = index->opts.lsn;

	int rc = vy_recovery_load_index(recovery, index->space_id, index->id,
					lsn, is_checkpoint_recovery,
					vy_index_recovery_cb, &arg);

	mh_int_t k;
	mh_foreach(arg.run_hash, k) {
		struct vy_run *run = mh_i64ptr_node(arg.run_hash, k)->val;
		if (run->refs > 1)
			vy_index_add_run(index, run);
		if (run->refs == 1 && rc == 0) {
			diag_set(ClientError, ER_INVALID_VYLOG_FILE,
				 tt_sprintf("Unused run %lld in index %lld",
					    (long long)run->id,
					    (long long)index->commit_lsn));
			rc = -1;
			/*
			 * Continue the loop to unreference
			 * all runs in the hash.
			 */
		}
		/* Drop the reference held by the hash. */
		vy_run_unref(run);
	}
	mh_i64ptr_delete(arg.run_hash);

	if (rc != 0) {
		/* Recovery callback failed. */
		return -1;
	}

	if (index->commit_lsn < 0) {
		/* Index was not found in the metadata log. */
		if (is_checkpoint_recovery) {
			/*
			 * All indexes created from snapshot rows must
			 * be present in vylog, because snapshot can
			 * only succeed if vylog has been successfully
			 * flushed.
			 */
			diag_set(ClientError, ER_INVALID_VYLOG_FILE,
				 tt_sprintf("Index %lld not found",
					    (long long)index->commit_lsn));
			return -1;
		}
		/*
		 * If we failed to log index creation before restart,
		 * we won't find it in the log on recovery. This is
		 * OK as the index doesn't have any runs in this case.
		 * We will retry to log index in vy_index_commit_create().
		 * For now, just create the initial range.
		 */
		return vy_index_init_range_tree(index);
	}

	if (index->is_dropped) {
		/*
		 * Initial range is not stored in the metadata log
		 * for dropped indexes, but we need it for recovery.
		 */
		return vy_index_init_range_tree(index);
	}

	/*
	 * Account ranges to the index and check that the range tree
	 * does not have holes or overlaps.
	 */
	struct vy_range *range, *prev = NULL;
	for (range = vy_range_tree_first(index->tree); range != NULL;
	     prev = range, range = vy_range_tree_next(index->tree, range)) {
		if (prev == NULL && range->begin != NULL) {
			diag_set(ClientError, ER_INVALID_VYLOG_FILE,
				 tt_sprintf("Range %lld is leftmost but "
					    "starts with a finite key",
					    (long long)range->id));
			return -1;
		}
		int cmp = 0;
		if (prev != NULL &&
		    (prev->end == NULL || range->begin == NULL ||
		     (cmp = vy_key_compare(prev->end, range->begin,
					   index->cmp_def)) != 0)) {
			const char *errmsg = cmp > 0 ?
				"Nearby ranges %lld and %lld overlap" :
				"Keys between ranges %lld and %lld not spanned";
			diag_set(ClientError, ER_INVALID_VYLOG_FILE,
				 tt_sprintf(errmsg,
					    (long long)prev->id,
					    (long long)range->id));
			return -1;
		}
		vy_index_acct_range(index, range);
	}
	if (prev == NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Index %lld has empty range tree",
				    (long long)index->commit_lsn));
		return -1;
	}
	if (prev->end != NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Range %lld is rightmost but "
				    "ends with a finite key",
				    (long long)prev->id));
		return -1;
	}
	return 0;
}

int64_t
vy_index_generation(struct vy_index *index)
{
	struct vy_mem *oldest = rlist_empty(&index->sealed) ? index->mem :
		rlist_last_entry(&index->sealed, struct vy_mem, in_sealed);
	return oldest->generation;
}

int
vy_index_compact_priority(struct vy_index *index)
{
	struct heap_node *n = vy_range_heap_top(&index->range_heap);
	if (n == NULL)
		return 0;
	struct vy_range *range = container_of(n, struct vy_range, heap_node);
	return range->compact_priority;
}

void
vy_index_add_run(struct vy_index *index, struct vy_run *run)
{
	assert(rlist_empty(&run->in_index));
	rlist_add_entry(&index->runs, run, in_index);
	index->run_count++;
	vy_disk_stmt_counter_add(&index->stat.disk.count, &run->count);

	index->bloom_size += vy_run_bloom_size(run);
	index->page_index_size += run->page_index_size;

	index->env->bloom_size += vy_run_bloom_size(run);
	index->env->page_index_size += run->page_index_size;
}

void
vy_index_remove_run(struct vy_index *index, struct vy_run *run)
{
	assert(index->run_count > 0);
	assert(!rlist_empty(&run->in_index));
	rlist_del_entry(run, in_index);
	index->run_count--;
	vy_disk_stmt_counter_sub(&index->stat.disk.count, &run->count);

	index->bloom_size -= vy_run_bloom_size(run);
	index->page_index_size -= run->page_index_size;

	index->env->bloom_size -= vy_run_bloom_size(run);
	index->env->page_index_size -= run->page_index_size;
}

void
vy_index_add_range(struct vy_index *index, struct vy_range *range)
{
	assert(range->heap_node.pos == UINT32_MAX);
	vy_range_heap_insert(&index->range_heap, &range->heap_node);
	vy_range_tree_insert(index->tree, range);
	index->range_count++;
}

void
vy_index_remove_range(struct vy_index *index, struct vy_range *range)
{
	assert(range->heap_node.pos != UINT32_MAX);
	vy_range_heap_delete(&index->range_heap, &range->heap_node);
	vy_range_tree_remove(index->tree, range);
	index->range_count--;
}

void
vy_index_acct_range(struct vy_index *index, struct vy_range *range)
{
	histogram_collect(index->run_hist, range->slice_count);
}

void
vy_index_unacct_range(struct vy_index *index, struct vy_range *range)
{
	histogram_discard(index->run_hist, range->slice_count);
}

int
vy_index_rotate_mem(struct vy_index *index)
{
	struct vy_mem *mem;

	assert(index->mem != NULL);
	mem = vy_mem_new(index->mem->env, *index->env->p_generation,
			 index->cmp_def, index->mem_format,
			 index->mem_format_with_colmask,
			 index->upsert_format, schema_version);
	if (mem == NULL)
		return -1;

	rlist_add_entry(&index->sealed, index->mem, in_sealed);
	index->mem = mem;
	index->mem_list_version++;
	return 0;
}

void
vy_index_delete_mem(struct vy_index *index, struct vy_mem *mem)
{
	assert(!rlist_empty(&mem->in_sealed));
	rlist_del_entry(mem, in_sealed);
	vy_stmt_counter_sub(&index->stat.memory.count, &mem->count);
	vy_mem_delete(mem);
	index->mem_list_version++;
}

int
vy_index_set(struct vy_index *index, struct vy_mem *mem,
	     const struct tuple *stmt, const struct tuple **region_stmt)
{
	assert(vy_stmt_is_refable(stmt));
	assert(*region_stmt == NULL || !vy_stmt_is_refable(*region_stmt));

	/* Allocate region_stmt on demand. */
	if (*region_stmt == NULL) {
		*region_stmt = vy_stmt_dup_lsregion(stmt, &mem->env->allocator,
						    mem->generation);
		if (*region_stmt == NULL)
			return -1;
	}

	/* We can't free region_stmt below, so let's add it to the stats */
	index->stat.memory.count.bytes += tuple_size(stmt);

	uint32_t format_id = stmt->format_id;
	if (vy_stmt_type(*region_stmt) != IPROTO_UPSERT) {
		/* Abort transaction if format was changed by DDL */
		if (format_id != tuple_format_id(mem->format_with_colmask) &&
		    format_id != tuple_format_id(mem->format)) {
			diag_set(ClientError, ER_TRANSACTION_CONFLICT);
			return -1;
		}
		return vy_mem_insert(mem, *region_stmt);
	} else {
		/* Abort transaction if format was changed by DDL */
		if (format_id != tuple_format_id(mem->upsert_format)) {
			diag_set(ClientError, ER_TRANSACTION_CONFLICT);
			return -1;
		}
		return vy_mem_insert_upsert(mem, *region_stmt);
	}
}

/**
 * Calculate and record the number of sequential upserts, squash
 * immediately or schedule upsert process if needed.
 * Additional handler used in vy_index_commit_stmt() for UPSERT
 * statements.
 *
 * @param index Index the statement was committed to.
 * @param mem   In-memory tree where the statement was saved.
 * @param stmt  UPSERT statement to squash.
 */
static void
vy_index_commit_upsert(struct vy_index *index, struct vy_mem *mem,
		       const struct tuple *stmt)
{
	assert(vy_stmt_type(stmt) == IPROTO_UPSERT);
	assert(vy_stmt_lsn(stmt) < MAX_LSN);
	/*
	 * UPSERT is enabled only for the spaces with the single
	 * index.
	 */
	assert(index->id == 0);

	const struct tuple *older;
	int64_t lsn = vy_stmt_lsn(stmt);
	uint8_t n_upserts = vy_stmt_n_upserts(stmt);
	/*
	 * If there are a lot of successive upserts for the same key,
	 * select might take too long to squash them all. So once the
	 * number of upserts exceeds a certain threshold, we schedule
	 * a fiber to merge them and insert the resulting statement
	 * after the latest upsert.
	 */
	if (n_upserts == VY_UPSERT_INF) {
		/*
		 * If UPSERT has n_upserts > VY_UPSERT_THRESHOLD,
		 * it means the mem has older UPSERTs for the same
		 * key which already are beeing processed in the
		 * squashing task. At the end, the squashing task
		 * will merge its result with this UPSERT
		 * automatically.
		 */
		return;
	}
	if (n_upserts == VY_UPSERT_THRESHOLD) {
		/*
		 * Start single squashing task per one-mem and
		 * one-key continous UPSERTs sequence.
		 */
#ifndef NDEBUG
		older = vy_mem_older_lsn(mem, stmt);
		assert(older != NULL && vy_stmt_type(older) == IPROTO_UPSERT &&
		       vy_stmt_n_upserts(older) == VY_UPSERT_THRESHOLD - 1);
#endif
		if (index->env->upsert_thresh_cb == NULL) {
			/* Squash callback is not installed. */
			return;
		}

		struct tuple *dup = vy_stmt_dup(stmt, index->upsert_format);
		if (dup != NULL) {
			index->env->upsert_thresh_cb(index, dup,
					index->env->upsert_thresh_arg);
			tuple_unref(dup);
		}
		/*
		 * Ignore dup == NULL, because the optimization is
		 * good, but is not necessary.
		 */
		return;
	}

	/*
	 * If there are no other mems and runs and n_upserts == 0,
	 * then we can turn the UPSERT into the REPLACE.
	 */
	if (n_upserts == 0 &&
	    index->stat.memory.count.rows == index->mem->count.rows &&
	    index->run_count == 0) {
		older = vy_mem_older_lsn(mem, stmt);
		assert(older == NULL || vy_stmt_type(older) != IPROTO_UPSERT);
		struct tuple *upserted =
			vy_apply_upsert(stmt, older, index->cmp_def,
					index->mem_format,
					index->upsert_format, false);
		index->stat.upsert.applied++;

		if (upserted == NULL) {
			/* OOM */
			diag_clear(diag_get());
			return;
		}
		int64_t upserted_lsn = vy_stmt_lsn(upserted);
		if (upserted_lsn != lsn) {
			/**
			 * This could only happen if the upsert completely
			 * failed and the old tuple was returned.
			 * In this case we shouldn't insert the same replace
			 * again.
			 */
			assert(older == NULL ||
			       upserted_lsn == vy_stmt_lsn(older));
			tuple_unref(upserted);
			return;
		}
		assert(older == NULL || upserted_lsn != vy_stmt_lsn(older));
		assert(vy_stmt_type(upserted) == IPROTO_REPLACE);

		const struct tuple *region_stmt =
			vy_stmt_dup_lsregion(upserted, &mem->env->allocator,
					     mem->generation);
		if (region_stmt == NULL) {
			/* OOM */
			tuple_unref(upserted);
			diag_clear(diag_get());
			return;
		}

		int rc = vy_index_set(index, mem, upserted, &region_stmt);
		/**
		 * Since we have already allocated mem statement and
		 * now we replacing one statement with another, the
		 * vy_index_set() cannot fail.
		 */
		assert(rc == 0); (void)rc;
		tuple_unref(upserted);
		vy_mem_commit_stmt(mem, region_stmt);
		index->stat.upsert.squashed++;
	}
}

void
vy_index_commit_stmt(struct vy_index *index, struct vy_mem *mem,
		     const struct tuple *stmt)
{
	vy_mem_commit_stmt(mem, stmt);

	index->stat.memory.count.rows++;

	if (vy_stmt_type(stmt) == IPROTO_UPSERT)
		vy_index_commit_upsert(index, mem, stmt);

	vy_stmt_counter_acct_tuple(&index->stat.put, stmt);

	/* Invalidate cache element. */
	vy_cache_on_write(&index->cache, stmt, NULL);
}

void
vy_index_rollback_stmt(struct vy_index *index, struct vy_mem *mem,
		       const struct tuple *stmt)
{
	vy_mem_rollback_stmt(mem, stmt);

	/* Invalidate cache element. */
	vy_cache_on_write(&index->cache, stmt, NULL);
}

bool
vy_index_split_range(struct vy_index *index, struct vy_range *range)
{
	struct tuple_format *key_format = index->env->key_format;

	const char *split_key_raw;
	if (!vy_range_needs_split(range, &index->opts, &split_key_raw))
		return false;

	/* Split a range in two parts. */
	const int n_parts = 2;

	/*
	 * Determine new ranges' boundaries.
	 */
	struct tuple *split_key = vy_key_from_msgpack(key_format,
						      split_key_raw);
	if (split_key == NULL)
		goto fail;

	struct tuple *keys[3];
	keys[0] = range->begin;
	keys[1] = split_key;
	keys[2] = range->end;

	/*
	 * Allocate new ranges and create slices of
	 * the old range's runs for them.
	 */
	struct vy_slice *slice, *new_slice;
	struct vy_range *part, *parts[2] = {NULL, };
	for (int i = 0; i < n_parts; i++) {
		part = vy_range_new(vy_log_next_id(), keys[i], keys[i + 1],
				    index->cmp_def);
		if (part == NULL)
			goto fail;
		parts[i] = part;
		/*
		 * vy_range_add_slice() adds a slice to the list head,
		 * so to preserve the order of the slices list, we have
		 * to iterate backward.
		 */
		rlist_foreach_entry_reverse(slice, &range->slices, in_range) {
			if (vy_slice_cut(slice, vy_log_next_id(), part->begin,
					 part->end, index->cmp_def,
					 &new_slice) != 0)
				goto fail;
			if (new_slice != NULL)
				vy_range_add_slice(part, new_slice);
		}
		part->compact_priority = range->compact_priority;
	}

	/*
	 * Log change in metadata.
	 */
	vy_log_tx_begin();
	rlist_foreach_entry(slice, &range->slices, in_range)
		vy_log_delete_slice(slice->id);
	vy_log_delete_range(range->id);
	for (int i = 0; i < n_parts; i++) {
		part = parts[i];
		vy_log_insert_range(index->commit_lsn, part->id,
				    tuple_data_or_null(part->begin),
				    tuple_data_or_null(part->end));
		rlist_foreach_entry(slice, &part->slices, in_range)
			vy_log_insert_slice(part->id, slice->run->id, slice->id,
					    tuple_data_or_null(slice->begin),
					    tuple_data_or_null(slice->end));
	}
	if (vy_log_tx_commit() < 0)
		goto fail;

	/*
	 * Replace the old range in the index.
	 */
	vy_index_unacct_range(index, range);
	vy_index_remove_range(index, range);

	for (int i = 0; i < n_parts; i++) {
		part = parts[i];
		vy_index_add_range(index, part);
		vy_index_acct_range(index, part);
	}
	index->range_tree_version++;

	say_info("%s: split range %s by key %s", vy_index_name(index),
		 vy_range_str(range), tuple_str(split_key));

	rlist_foreach_entry(slice, &range->slices, in_range)
		vy_slice_wait_pinned(slice);
	vy_range_delete(range);
	tuple_unref(split_key);
	return true;
fail:
	for (int i = 0; i < n_parts; i++) {
		if (parts[i] != NULL)
			vy_range_delete(parts[i]);
	}
	if (split_key != NULL)
		tuple_unref(split_key);

	diag_log();
	say_error("%s: failed to split range %s",
		  vy_index_name(index), vy_range_str(range));
	return false;
}

bool
vy_index_coalesce_range(struct vy_index *index, struct vy_range *range)
{
	struct vy_range *first, *last;
	if (!vy_range_needs_coalesce(range, index->tree, &index->opts,
				     &first, &last))
		return false;

	struct vy_range *result = vy_range_new(vy_log_next_id(),
			first->begin, last->end, index->cmp_def);
	if (result == NULL)
		goto fail_range;

	struct vy_range *it;
	struct vy_range *end = vy_range_tree_next(index->tree, last);

	/*
	 * Log change in metadata.
	 */
	vy_log_tx_begin();
	vy_log_insert_range(index->commit_lsn, result->id,
			    tuple_data_or_null(result->begin),
			    tuple_data_or_null(result->end));
	for (it = first; it != end; it = vy_range_tree_next(index->tree, it)) {
		struct vy_slice *slice;
		rlist_foreach_entry(slice, &it->slices, in_range)
			vy_log_delete_slice(slice->id);
		vy_log_delete_range(it->id);
		rlist_foreach_entry(slice, &it->slices, in_range) {
			vy_log_insert_slice(result->id, slice->run->id, slice->id,
					    tuple_data_or_null(slice->begin),
					    tuple_data_or_null(slice->end));
		}
	}
	if (vy_log_tx_commit() < 0)
		goto fail_commit;

	/*
	 * Move run slices of the coalesced ranges to the
	 * resulting range and delete the former.
	 */
	it = first;
	while (it != end) {
		struct vy_range *next = vy_range_tree_next(index->tree, it);
		vy_index_unacct_range(index, it);
		vy_index_remove_range(index, it);
		rlist_splice(&result->slices, &it->slices);
		result->slice_count += it->slice_count;
		vy_disk_stmt_counter_add(&result->count, &it->count);
		vy_range_delete(it);
		it = next;
	}
	/*
	 * Coalescing increases read amplification and breaks the log
	 * structured layout of the run list, so, although we could
	 * leave the resulting range as it is, we'd better compact it
	 * as soon as we can.
	 */
	result->compact_priority = result->slice_count;
	vy_index_acct_range(index, result);
	vy_index_add_range(index, result);
	index->range_tree_version++;

	say_info("%s: coalesced ranges %s",
		 vy_index_name(index), vy_range_str(result));
	return true;

fail_commit:
	vy_range_delete(result);
fail_range:
	diag_log();
	say_error("%s: failed to coalesce range %s",
		  vy_index_name(index), vy_range_str(range));
	return false;
}
