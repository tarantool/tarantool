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
#include "vy_lsm.h"

#include "trivia/util.h"
#include <stdbool.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <small/mempool.h>

#include "diag.h"
#include "fiber.h"
#include "errcode.h"
#include "histogram.h"
#include "index_def.h"
#include "say.h"
#include "schema.h"
#include "tuple.h"
#include "trigger.h"
#include "vy_log.h"
#include "vy_mem.h"
#include "vy_range.h"
#include "vy_run.h"
#include "vy_stat.h"
#include "vy_stmt.h"
#include "vy_upsert.h"
#include "vy_history.h"
#include "vy_read_set.h"

/*
 * It doesn't make much sense to create too small ranges as this
 * would make the overhead associated with file creation prominent
 * and increase the number of open files. So we never create ranges
 * less than 16 MB.
 */
static const int64_t VY_MIN_RANGE_SIZE = 128 * 1024 * 1024;

/**
 * We want a single compaction job to finish in reasonable time
 * so we limit the range size to 2 GB.
 */
static const int64_t VY_MAX_RANGE_SIZE = 2LL * 1024 * 1024 * 1024;

int
vy_lsm_env_create(struct vy_lsm_env *env, const char *path,
		  int64_t *p_generation, struct tuple_format *key_format,
		  vy_upsert_thresh_cb upsert_thresh_cb,
		  void *upsert_thresh_arg)
{
	env->empty_key.hint = HINT_NONE;
	env->empty_key.stmt = vy_key_new(key_format, NULL, 0);
	if (env->empty_key.stmt == NULL)
		return -1;
	env->path = path;
	env->p_generation = p_generation;
	env->key_format = key_format;
	tuple_format_ref(key_format);
	env->upsert_thresh_cb = upsert_thresh_cb;
	env->upsert_thresh_arg = upsert_thresh_arg;
	env->too_long_threshold = TIMEOUT_INFINITY;
	env->lsm_count = 0;
	mempool_create(&env->history_node_pool, cord_slab_cache(),
		       sizeof(struct vy_history_node));
	return 0;
}

void
vy_lsm_env_destroy(struct vy_lsm_env *env)
{
	tuple_unref(env->empty_key.stmt);
	tuple_format_unref(env->key_format);
	mempool_destroy(&env->history_node_pool);
}

const char *
vy_lsm_name(struct vy_lsm *lsm)
{
	char *buf = tt_static_buf();
	snprintf(buf, TT_STATIC_BUF_LEN, "%u/%u",
		 (unsigned)lsm->space_id, (unsigned)lsm->index_id);
	return buf;
}

size_t
vy_lsm_mem_tree_size(struct vy_lsm *lsm)
{
	struct vy_mem *mem;
	size_t size = lsm->mem->tree_extent_size;
	rlist_foreach_entry(mem, &lsm->sealed, in_sealed)
		size += mem->tree_extent_size;
	return size;
}

struct vy_lsm *
vy_lsm_new(struct vy_lsm_env *lsm_env, struct vy_cache_env *cache_env,
	   struct vy_mem_env *mem_env, struct index_def *index_def,
	   struct tuple_format *format, struct vy_lsm *pk, uint32_t group_id)
{
	static int64_t run_buckets[] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 20, 25, 50, 100,
	};

	assert(index_def->key_def->part_count > 0);
	assert(index_def->iid == 0 || pk != NULL);

	struct vy_lsm *lsm = calloc(1, sizeof(struct vy_lsm));
	if (lsm == NULL) {
		diag_set(OutOfMemory, sizeof(struct vy_lsm),
			 "calloc", "struct vy_lsm");
		goto fail;
	}
	lsm->env = lsm_env;

	struct key_def *key_def = key_def_dup(index_def->key_def);
	if (key_def == NULL)
		goto fail_key_def;

	struct key_def *cmp_def = key_def_dup(index_def->cmp_def);
	if (cmp_def == NULL)
		goto fail_cmp_def;

	lsm->cmp_def = cmp_def;
	lsm->key_def = key_def;
	if (index_def->iid == 0) {
		/*
		 * Disk tuples can be returned to an user from a
		 * primary key. And they must have field
		 * definitions as well as space->format tuples.
		 */
		lsm->disk_format = format;
	} else {
		/*
		 * To save disk space, we do not store full tuples
		 * in secondary index runs. Instead we only store
		 * extended keys (i.e. keys consisting of secondary
		 * and primary index parts). This is enough to look
		 * up a full tuple in the primary index.
		 */
		lsm->disk_format = lsm_env->key_format;

		lsm->pk_in_cmp_def = key_def_find_pk_in_cmp_def(lsm->cmp_def,
								pk->key_def,
								&fiber()->gc);
		if (lsm->pk_in_cmp_def == NULL)
			goto fail_pk_in_cmp_def;
	}
	tuple_format_ref(lsm->disk_format);

	if (vy_lsm_stat_create(&lsm->stat) != 0)
		goto fail_stat;

	lsm->run_hist = histogram_new(run_buckets, lengthof(run_buckets));
	if (lsm->run_hist == NULL)
		goto fail_run_hist;

	lsm->mem = vy_mem_new(mem_env, cmp_def, format,
			      *lsm->env->p_generation,
			      space_cache_version);
	if (lsm->mem == NULL)
		goto fail_mem;

	lsm->id = -1;
	lsm->dump_lsn = -1;
	lsm->commit_lsn = -1;
	vy_cache_create(&lsm->cache, cache_env, cmp_def, index_def->iid == 0);
	rlist_create(&lsm->sealed);
	vy_range_tree_new(&lsm->range_tree);
	vy_range_heap_create(&lsm->range_heap);
	rlist_create(&lsm->runs);
	lsm->pk = pk;
	if (pk != NULL)
		vy_lsm_ref(pk);
	lsm->mem_format = format;
	tuple_format_ref(lsm->mem_format);
	heap_node_create(&lsm->in_dump);
	heap_node_create(&lsm->in_compaction);
	lsm->space_id = index_def->space_id;
	lsm->index_id = index_def->iid;
	lsm->group_id = group_id;
	lsm->opts = index_def->opts;
	vy_lsm_read_set_new(&lsm->read_set);
	rlist_create(&lsm->on_destroy);

	lsm_env->lsm_count++;
	return lsm;

fail_mem:
	histogram_delete(lsm->run_hist);
fail_run_hist:
	vy_lsm_stat_destroy(&lsm->stat);
fail_stat:
	tuple_format_unref(lsm->disk_format);
	if (lsm->pk_in_cmp_def != NULL)
		key_def_delete(lsm->pk_in_cmp_def);
fail_pk_in_cmp_def:
	key_def_delete(cmp_def);
fail_cmp_def:
	key_def_delete(key_def);
fail_key_def:
	free(lsm);
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
vy_lsm_delete(struct vy_lsm *lsm)
{
	trigger_run(&lsm->on_destroy, lsm);

	assert(heap_node_is_stray(&lsm->in_dump));
	assert(heap_node_is_stray(&lsm->in_compaction));
	assert(vy_lsm_read_set_empty(&lsm->read_set));
	assert(lsm->env->lsm_count > 0);

	lsm->env->lsm_count--;
	lsm->env->compaction_queue_size -=
			lsm->stat.disk.compaction.queue.bytes;
	if (lsm->index_id == 0)
		lsm->env->compacted_data_size -=
			lsm->stat.disk.last_level_count.bytes;
	if (lsm->pk != NULL)
		vy_lsm_unref(lsm->pk);

	struct vy_mem *mem, *next_mem;
	rlist_foreach_entry_safe(mem, &lsm->sealed, in_sealed, next_mem)
		vy_mem_delete(mem);
	vy_mem_delete(lsm->mem);

	struct vy_run *run, *next_run;
	rlist_foreach_entry_safe(run, &lsm->runs, in_lsm, next_run)
		vy_lsm_remove_run(lsm, run);

	vy_range_tree_iter(&lsm->range_tree, NULL, vy_range_tree_free_cb, NULL);
	vy_range_heap_destroy(&lsm->range_heap);
	tuple_format_unref(lsm->disk_format);
	key_def_delete(lsm->cmp_def);
	key_def_delete(lsm->key_def);
	if (lsm->pk_in_cmp_def != NULL)
		key_def_delete(lsm->pk_in_cmp_def);
	histogram_delete(lsm->run_hist);
	vy_lsm_stat_destroy(&lsm->stat);
	vy_cache_destroy(&lsm->cache);
	tuple_format_unref(lsm->mem_format);
	TRASH(lsm);
	free(lsm);
}

int
vy_lsm_create(struct vy_lsm *lsm)
{
	/* Make LSM tree directory. */
	int rc;
	char path[PATH_MAX];
	vy_lsm_snprint_path(path, sizeof(path), lsm->env->path,
			    lsm->space_id, lsm->index_id);
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

	/*
	 * Allocate a unique id for the new LSM tree, but don't assign
	 * it until information about the new LSM tree is successfully
	 * written to vylog as vinyl_index_abort_create() uses id to
	 * decide whether it needs to clean up.
	 */
	int64_t id = vy_log_next_id();

	/* Create the initial range. */
	struct vy_range *range = vy_range_new(vy_log_next_id(), vy_entry_none(),
					      vy_entry_none(), lsm->cmp_def);
	if (range == NULL)
		return -1;
	assert(lsm->range_count == 0);
	vy_lsm_add_range(lsm, range);
	vy_lsm_acct_range(lsm, range);

	/* Write the new LSM tree record to vylog. */
	vy_log_tx_begin();
	vy_log_prepare_lsm(id, lsm->space_id, lsm->index_id,
			   lsm->group_id, lsm->key_def);
	vy_log_insert_range(id, range->id, NULL, NULL);
	vy_log_tx_try_commit();

	/* Assign the id. */
	assert(lsm->id < 0);
	lsm->id = id;
	return 0;
}

static struct vy_run *
vy_lsm_recover_run(struct vy_lsm *lsm, struct vy_run_recovery_info *run_info,
		   struct vy_run_env *run_env, bool force_recovery)
{
	assert(!run_info->is_dropped);
	assert(!run_info->is_incomplete);

	if (run_info->data != NULL) {
		/* Already recovered. */
		return run_info->data;
	}

	struct vy_run *run = vy_run_new(run_env, run_info->id);
	if (run == NULL)
		return NULL;

	run->dump_lsn = run_info->dump_lsn;
	run->dump_count = run_info->dump_count;
	if (vy_run_recover(run, lsm->env->path, lsm->space_id, lsm->index_id,
			   lsm->cmp_def) != 0 &&
	    (!force_recovery ||
	     vy_run_rebuild_index(run, lsm->env->path,
				  lsm->space_id, lsm->index_id,
				  lsm->cmp_def, lsm->key_def,
				  lsm->disk_format, &lsm->opts) != 0)) {
		vy_run_unref(run);
		return NULL;
	}
	vy_lsm_add_run(lsm, run);

	/*
	 * The same run can be referenced by more than one slice
	 * so we cache recovered runs in run_info to avoid loading
	 * the same run multiple times.
	 *
	 * Runs are stored with their reference counters elevated.
	 * We drop the extra references as soon as LSM tree recovery
	 * is complete (see vy_lsm_recover()).
	 */
	run_info->data = run;
	return run;
}

static struct vy_slice *
vy_lsm_recover_slice(struct vy_lsm *lsm, struct vy_range *range,
		     struct vy_slice_recovery_info *slice_info,
		     struct vy_run_env *run_env, bool force_recovery)
{
	struct vy_entry begin = vy_entry_none();
	struct vy_entry end = vy_entry_none();
	struct vy_slice *slice = NULL;
	struct vy_run *run;

	if (slice_info->begin != NULL) {
		begin = vy_entry_key_from_msgpack(lsm->env->key_format,
						  lsm->cmp_def,
						  slice_info->begin);
		if (begin.stmt == NULL)
			goto out;
	}
	if (slice_info->end != NULL) {
		end = vy_entry_key_from_msgpack(lsm->env->key_format,
						lsm->cmp_def,
						slice_info->end);
		if (end.stmt == NULL)
			goto out;
	}
	if (begin.stmt != NULL && end.stmt != NULL &&
	    vy_entry_compare(begin, end, lsm->cmp_def) >= 0) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("begin >= end for slice %lld",
				    (long long)slice_info->id));
		goto out;
	}

	run = vy_lsm_recover_run(lsm, slice_info->run,
				 run_env, force_recovery);
	if (run == NULL)
		goto out;

	slice = vy_slice_new(slice_info->id, run, begin, end, lsm->cmp_def);
	if (slice == NULL)
		goto out;

	vy_range_add_slice(range, slice);
out:
	if (begin.stmt != NULL)
		tuple_unref(begin.stmt);
	if (end.stmt != NULL)
		tuple_unref(end.stmt);
	return slice;
}

static struct vy_range *
vy_lsm_recover_range(struct vy_lsm *lsm,
		     struct vy_range_recovery_info *range_info,
		     struct vy_run_env *run_env, bool force_recovery)
{
	struct vy_entry begin = vy_entry_none();
	struct vy_entry end = vy_entry_none();
	struct vy_range *range = NULL;

	if (range_info->begin != NULL) {
		begin = vy_entry_key_from_msgpack(lsm->env->key_format,
						  lsm->cmp_def,
						  range_info->begin);
		if (begin.stmt == NULL)
			goto out;
	}
	if (range_info->end != NULL) {
		end = vy_entry_key_from_msgpack(lsm->env->key_format,
						lsm->cmp_def,
						range_info->end);
		if (end.stmt == NULL)
			goto out;
	}
	if (begin.stmt != NULL && end.stmt != NULL &&
	    vy_entry_compare(begin, end, lsm->cmp_def) >= 0) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("begin >= end for range %lld",
				    (long long)range_info->id));
		goto out;
	}

	range = vy_range_new(range_info->id, begin, end, lsm->cmp_def);
	if (range == NULL)
		goto out;

	/*
	 * Newer slices are stored closer to the head of the list,
	 * while we are supposed to add slices in chronological
	 * order, so use reverse iterator.
	 */
	struct vy_slice_recovery_info *slice_info;
	rlist_foreach_entry_reverse(slice_info, &range_info->slices, in_range) {
		if (vy_lsm_recover_slice(lsm, range, slice_info,
					 run_env, force_recovery) == NULL) {
			vy_range_delete(range);
			range = NULL;
			goto out;
		}
	}
	vy_lsm_add_range(lsm, range);
out:
	if (begin.stmt != NULL)
		tuple_unref(begin.stmt);
	if (end.stmt != NULL)
		tuple_unref(end.stmt);
	return range;
}

int
vy_lsm_recover(struct vy_lsm *lsm, struct vy_recovery *recovery,
		 struct vy_run_env *run_env, int64_t lsn,
		 bool is_checkpoint_recovery, bool force_recovery)
{
	assert(lsm->id < 0);
	assert(lsm->commit_lsn < 0);
	assert(lsm->range_count == 0);

	/*
	 * Backward compatibility fixup: historically, we used
	 * box.info.signature for LSN of index creation, which
	 * lags behind the LSN of the record that created the
	 * index by 1. So for legacy indexes use the LSN from
	 * index options.
	 */
	if (lsm->opts.lsn != 0)
		lsn = lsm->opts.lsn;

	/*
	 * Look up the last incarnation of the LSM tree in vylog.
	 */
	struct vy_lsm_recovery_info *lsm_info;
	lsm_info = vy_recovery_lsm_by_index_id(recovery,
			lsm->space_id, lsm->index_id);
	if (is_checkpoint_recovery) {
		if (lsm_info == NULL || lsm_info->create_lsn < 0) {
			/*
			 * All LSM trees created from snapshot rows must
			 * be present in vylog, because snapshot can
			 * only succeed if vylog has been successfully
			 * flushed.
			 */
			diag_set(ClientError, ER_INVALID_VYLOG_FILE,
				 tt_sprintf("LSM tree %u/%u not found",
					    (unsigned)lsm->space_id,
					    (unsigned)lsm->index_id));
			return -1;
		}
		if (lsn > lsm_info->create_lsn) {
			/*
			 * The last incarnation of the LSM tree was
			 * created before the last checkpoint, load
			 * it now.
			 */
			lsn = lsm_info->create_lsn;
		}
	}

	if (lsm_info == NULL || (lsm_info->prepared == NULL &&
				 lsm_info->create_lsn >= 0 &&
				 lsn > lsm_info->create_lsn)) {
		/*
		 * If we failed to log LSM tree creation before restart,
		 * we won't find it in the log on recovery. This is OK as
		 * the LSM tree doesn't have any runs in this case. We will
		 * retry to log LSM tree in vinyl_index_commit_create().
		 * For now, just create the initial range and assign id.
		 *
		 * Note, this is needed only for backward compatibility
		 * since now we write VY_LOG_PREPARE_LSM before WAL write
		 * and hence if the index was committed to WAL, it must be
		 * present in vylog as well.
		 */
		return vy_lsm_create(lsm);
	}

	if (lsm_info->create_lsn >= 0 && lsn > lsm_info->create_lsn) {
		/*
		 * The index we are recovering was prepared, successfully
		 * built, and committed to WAL, but it was not marked as
		 * created in vylog. Recover the prepared LSM tree. We will
		 * retry vylog write in vinyl_index_commit_create().
		 */
		lsm_info = lsm_info->prepared;
		assert(lsm_info != NULL);
	}

	lsm->id = lsm_info->id;
	lsm->commit_lsn = lsm_info->modify_lsn;

	if (lsn < lsm_info->create_lsn || lsm_info->drop_lsn >= 0) {
		/*
		 * Loading a past incarnation of the LSM tree, i.e.
		 * the LSM tree is going to dropped during final
		 * recovery. Mark it as such.
		 */
		lsm->is_dropped = true;
		/*
		 * We need range tree initialized for all LSM trees,
		 * even for dropped ones.
		 */
		struct vy_range *range;
		range = vy_range_new(vy_log_next_id(), vy_entry_none(),
				     vy_entry_none(), lsm->cmp_def);
		if (range == NULL)
			return -1;
		vy_lsm_add_range(lsm, range);
		vy_lsm_acct_range(lsm, range);
		return 0;
	}

	/*
	 * Loading the last incarnation of the LSM tree from vylog.
	 */
	lsm->dump_lsn = lsm_info->dump_lsn;

	int rc = 0;
	struct vy_range_recovery_info *range_info;
	rlist_foreach_entry(range_info, &lsm_info->ranges, in_lsm) {
		if (vy_lsm_recover_range(lsm, range_info, run_env,
					 force_recovery) == NULL) {
			rc = -1;
			break;
		}
	}

	/*
	 * vy_lsm_recover_run() elevates reference counter
	 * of each recovered run. We need to drop the extra
	 * references once we are done.
	 */
	struct vy_run *run, *next_run;
	rlist_foreach_entry_safe(run, &lsm->runs, in_lsm, next_run) {
		/*
		 * In case vy_lsm_recover_range() failed, slices
		 * are already deleted and runs are unrefed. So
		 * we have nothing to do but finish run clean-up.
		 */
		if (run->refs == 1) {
			assert(rc != 0);
			assert(run->slice_count == 0);
			vy_lsm_remove_run(lsm, run);
		}
		vy_run_unref(run);
	}

	if (rc != 0)
		return -1;

	/*
	 * Account ranges to the LSM tree and check that the range tree
	 * does not have holes or overlaps.
	 */
	struct vy_range *range, *prev = NULL;
	for (range = vy_range_tree_first(&lsm->range_tree); range != NULL;
	     prev = range, range = vy_range_tree_next(&lsm->range_tree, range)) {
		if (prev == NULL && range->begin.stmt != NULL) {
			diag_set(ClientError, ER_INVALID_VYLOG_FILE,
				 tt_sprintf("Range %lld is leftmost but "
					    "starts with a finite key",
					    (long long)range->id));
			return -1;
		}
		int cmp = 0;
		if (prev != NULL &&
		    (prev->end.stmt == NULL || range->begin.stmt == NULL ||
		     (cmp = vy_entry_compare(prev->end, range->begin,
					     lsm->cmp_def)) != 0)) {
			const char *errmsg = cmp > 0 ?
				"Nearby ranges %lld and %lld overlap" :
				"Keys between ranges %lld and %lld not spanned";
			diag_set(ClientError, ER_INVALID_VYLOG_FILE,
				 tt_sprintf(errmsg,
					    (long long)prev->id,
					    (long long)range->id));
			return -1;
		}
		vy_range_update_dumps_per_compaction(range);
		vy_lsm_acct_range(lsm, range);
	}
	if (prev == NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("LSM tree %lld has empty range tree",
				    (long long)lsm->id));
		return -1;
	}
	if (prev->end.stmt != NULL) {
		diag_set(ClientError, ER_INVALID_VYLOG_FILE,
			 tt_sprintf("Range %lld is rightmost but "
				    "ends with a finite key",
				    (long long)prev->id));
		return -1;
	}
	return 0;
}

int64_t
vy_lsm_generation(struct vy_lsm *lsm)
{
	struct vy_mem *oldest = rlist_empty(&lsm->sealed) ? lsm->mem :
		rlist_last_entry(&lsm->sealed, struct vy_mem, in_sealed);
	return oldest->generation;
}

int
vy_lsm_compaction_priority(struct vy_lsm *lsm)
{
	struct vy_range *range = vy_range_heap_top(&lsm->range_heap);
	if (range == NULL)
		return 0;
	/*
	 * There's no point in compacting dropped LSM trees. Moreover, since we
	 * don't commit a new run for a dropped LSM tree so as not to mess with
	 * garbage collection (see vy_task_compaction_complete()), enabling
	 * compaction in this case would result in rescheduling it over and
	 * over again, which is no good.
	 */
	if (lsm->is_dropped)
		return 0;
	return range->compaction_priority;
}

int64_t
vy_lsm_range_size(struct vy_lsm *lsm)
{
	/* Use the configured range size if available. */
	if (lsm->opts.range_size > 0)
		return lsm->opts.range_size;
	/*
	 * Ideally, we want to compact roughly the same amount of
	 * data after each dump so as to avoid IO bursts caused by
	 * simultaneous major compaction of a bunch of ranges,
	 * because such IO bursts can lead to a deviation of the
	 * LSM tree from the configured shape and, as a result,
	 * increased read amplification. To achieve that, we need
	 * to have at least as many ranges as the number of dumps
	 * it takes to trigger major compaction in a range. We
	 * create four times more than that for better smoothing.
	 */
	int range_count = 4 * vy_lsm_dumps_per_compaction(lsm);
	int64_t range_size = range_count == 0 ? 0 :
		lsm->stat.disk.last_level_count.bytes / range_count;
	range_size = MAX(range_size, VY_MIN_RANGE_SIZE);
	range_size = MIN(range_size, VY_MAX_RANGE_SIZE);
	return range_size;
}

void
vy_lsm_add_run(struct vy_lsm *lsm, struct vy_run *run)
{
	struct vy_lsm_env *env = lsm->env;
	size_t bloom_size = vy_run_bloom_size(run);
	size_t page_index_size = run->page_index_size;

	assert(rlist_empty(&run->in_lsm));
	rlist_add_entry(&lsm->runs, run, in_lsm);
	lsm->run_count++;
	vy_disk_stmt_counter_add(&lsm->stat.disk.count, &run->count);
	vy_stmt_stat_add(&lsm->stat.disk.stmt, &run->info.stmt_stat);

	lsm->bloom_size += bloom_size;
	lsm->page_index_size += page_index_size;

	env->bloom_size += bloom_size;
	env->page_index_size += page_index_size;

	/* Data size is consistent with space.bsize. */
	if (lsm->index_id == 0)
		env->disk_data_size += run->count.bytes;
	/* Index size is consistent with index.bsize. */
	env->disk_index_size += bloom_size + page_index_size;
	if (lsm->index_id > 0)
		env->disk_index_size += run->count.bytes;
}

void
vy_lsm_remove_run(struct vy_lsm *lsm, struct vy_run *run)
{
	struct vy_lsm_env *env = lsm->env;
	size_t bloom_size = vy_run_bloom_size(run);
	size_t page_index_size = run->page_index_size;

	assert(lsm->run_count > 0);
	assert(!rlist_empty(&run->in_lsm));
	rlist_del_entry(run, in_lsm);
	lsm->run_count--;
	vy_disk_stmt_counter_sub(&lsm->stat.disk.count, &run->count);
	vy_stmt_stat_sub(&lsm->stat.disk.stmt, &run->info.stmt_stat);

	lsm->bloom_size -= bloom_size;
	lsm->page_index_size -= page_index_size;

	env->bloom_size -= bloom_size;
	env->page_index_size -= page_index_size;

	/* Data size is consistent with space.bsize. */
	if (lsm->index_id == 0)
		env->disk_data_size -= run->count.bytes;
	/* Index size is consistent with index.bsize. */
	env->disk_index_size -= bloom_size + page_index_size;
	if (lsm->index_id > 0)
		env->disk_index_size -= run->count.bytes;
}

void
vy_lsm_add_range(struct vy_lsm *lsm, struct vy_range *range)
{
	assert(heap_node_is_stray(&range->heap_node));
	vy_range_heap_insert(&lsm->range_heap, range);
	vy_range_tree_insert(&lsm->range_tree, range);
	lsm->range_count++;
}

void
vy_lsm_remove_range(struct vy_lsm *lsm, struct vy_range *range)
{
	assert(! heap_node_is_stray(&range->heap_node));
	vy_range_heap_delete(&lsm->range_heap, range);
	vy_range_tree_remove(&lsm->range_tree, range);
	lsm->range_count--;
}

void
vy_lsm_acct_range(struct vy_lsm *lsm, struct vy_range *range)
{
	histogram_collect(lsm->run_hist, range->slice_count);
	lsm->sum_dumps_per_compaction += range->dumps_per_compaction;
	vy_disk_stmt_counter_add(&lsm->stat.disk.compaction.queue,
				 &range->compaction_queue);
	lsm->env->compaction_queue_size += range->compaction_queue.bytes;
	if (!rlist_empty(&range->slices)) {
		struct vy_slice *slice = rlist_last_entry(&range->slices,
						struct vy_slice, in_range);
		vy_disk_stmt_counter_add(&lsm->stat.disk.last_level_count,
					 &slice->count);
		if (lsm->index_id == 0)
			lsm->env->compacted_data_size += slice->count.bytes;
	}
}

void
vy_lsm_unacct_range(struct vy_lsm *lsm, struct vy_range *range)
{
	histogram_discard(lsm->run_hist, range->slice_count);
	lsm->sum_dumps_per_compaction -= range->dumps_per_compaction;
	vy_disk_stmt_counter_sub(&lsm->stat.disk.compaction.queue,
				 &range->compaction_queue);
	lsm->env->compaction_queue_size -= range->compaction_queue.bytes;
	if (!rlist_empty(&range->slices)) {
		struct vy_slice *slice = rlist_last_entry(&range->slices,
						struct vy_slice, in_range);
		vy_disk_stmt_counter_sub(&lsm->stat.disk.last_level_count,
					 &slice->count);
		if (lsm->index_id == 0)
			lsm->env->compacted_data_size -= slice->count.bytes;
	}
}

void
vy_lsm_acct_dump(struct vy_lsm *lsm, double time,
		 const struct vy_stmt_counter *input,
		 const struct vy_disk_stmt_counter *output)
{
	lsm->stat.disk.dump.count++;
	lsm->stat.disk.dump.time += time;
	vy_stmt_counter_add(&lsm->stat.disk.dump.input, input);
	vy_disk_stmt_counter_add(&lsm->stat.disk.dump.output, output);
}

void
vy_lsm_acct_compaction(struct vy_lsm *lsm, double time,
		       const struct vy_disk_stmt_counter *input,
		       const struct vy_disk_stmt_counter *output)
{
	lsm->stat.disk.compaction.count++;
	lsm->stat.disk.compaction.time += time;
	vy_disk_stmt_counter_add(&lsm->stat.disk.compaction.input, input);
	vy_disk_stmt_counter_add(&lsm->stat.disk.compaction.output, output);
}

int
vy_lsm_rotate_mem(struct vy_lsm *lsm)
{
	struct vy_mem *mem;

	assert(lsm->mem != NULL);
	mem = vy_mem_new(lsm->mem->env, lsm->cmp_def, lsm->mem_format,
			 *lsm->env->p_generation, space_cache_version);
	if (mem == NULL)
		return -1;

	rlist_add_entry(&lsm->sealed, lsm->mem, in_sealed);
	lsm->mem = mem;
	lsm->mem_list_version++;
	return 0;
}

void
vy_lsm_delete_mem(struct vy_lsm *lsm, struct vy_mem *mem)
{
	assert(!rlist_empty(&mem->in_sealed));
	rlist_del_entry(mem, in_sealed);
	vy_stmt_counter_sub(&lsm->stat.memory.count, &mem->count);
	vy_mem_delete(mem);
	lsm->mem_list_version++;
}

int
vy_lsm_set(struct vy_lsm *lsm, struct vy_mem *mem,
	   struct vy_entry entry, struct tuple **region_stmt)
{
	uint32_t format_id = entry.stmt->format_id;

	assert(vy_stmt_is_refable(entry.stmt));
	assert(*region_stmt == NULL || !vy_stmt_is_refable(*region_stmt));

	/*
	 * Allocate region_stmt on demand.
	 *
	 * Also, reallocate region_stmt if it uses a different tuple
	 * format. This may happen during ALTER, when the LSM tree
	 * that is currently being built uses the new space format
	 * while other LSM trees still use the old space format.
	 */
	if (*region_stmt == NULL || (*region_stmt)->format_id != format_id) {
		*region_stmt = vy_stmt_dup_lsregion(entry.stmt,
						    &mem->env->allocator,
						    mem->generation);
		if (*region_stmt == NULL)
			return -1;
	}
	entry.stmt = *region_stmt;

	/* We can't free region_stmt below, so let's add it to the stats */
	lsm->stat.memory.count.bytes += tuple_size(entry.stmt);

	/* Abort transaction if format was changed by DDL */
	if (!vy_stmt_is_key(entry.stmt) &&
	    format_id != tuple_format_id(mem->format)) {
		diag_set(ClientError, ER_TRANSACTION_CONFLICT);
		return -1;
	}
	if (vy_stmt_type(*region_stmt) != IPROTO_UPSERT)
		return vy_mem_insert(mem, entry);
	else
		return vy_mem_insert_upsert(mem, entry);
}

/**
 * Calculate and record the number of sequential upserts, squash
 * immediately or schedule upsert process if needed.
 * Additional handler used in vy_lsm_commit_stmt() for UPSERT
 * statements.
 *
 * @param lsm   LSM tree the statement was committed to.
 * @param mem   In-memory tree where the statement was saved.
 * @param entry UPSERT statement to squash.
 */
static void
vy_lsm_commit_upsert(struct vy_lsm *lsm, struct vy_mem *mem,
		     struct vy_entry entry)
{
	assert(vy_stmt_type(entry.stmt) == IPROTO_UPSERT);
	assert(vy_stmt_lsn(entry.stmt) < MAX_LSN);
	/*
	 * UPSERT is enabled only for the spaces with the single
	 * index.
	 */
	assert(lsm->index_id == 0);

	struct vy_entry older;
	int64_t lsn = vy_stmt_lsn(entry.stmt);
	uint8_t n_upserts = vy_stmt_n_upserts(entry.stmt);
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
		older = vy_mem_older_lsn(mem, entry);
		assert(older.stmt != NULL &&
		       vy_stmt_type(older.stmt) == IPROTO_UPSERT &&
		       vy_stmt_n_upserts(older.stmt) == VY_UPSERT_THRESHOLD - 1);
#endif
		if (lsm->env->upsert_thresh_cb == NULL) {
			/* Squash callback is not installed. */
			return;
		}

		struct vy_entry dup;
		dup.hint = entry.hint;
		dup.stmt = vy_stmt_dup(entry.stmt);
		if (dup.stmt != NULL) {
			lsm->env->upsert_thresh_cb(lsm, dup,
					lsm->env->upsert_thresh_arg);
			tuple_unref(dup.stmt);
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
	    lsm->stat.memory.count.rows == lsm->mem->count.rows &&
	    lsm->run_count == 0) {
		older = vy_mem_older_lsn(mem, entry);
		assert(older.stmt == NULL ||
		       vy_stmt_type(older.stmt) != IPROTO_UPSERT);
		struct vy_entry upserted;
		upserted = vy_entry_apply_upsert(entry, older,
						lsm->cmp_def, false);
		lsm->stat.upsert.applied++;

		if (upserted.stmt == NULL) {
			/* OOM */
			diag_clear(diag_get());
			return;
		}
		int64_t upserted_lsn = vy_stmt_lsn(upserted.stmt);
		if (upserted_lsn != lsn) {
			/**
			 * This could only happen if the upsert completely
			 * failed and the old tuple was returned.
			 * In this case we shouldn't insert the same replace
			 * again.
			 */
			assert(older.stmt == NULL ||
			       upserted_lsn == vy_stmt_lsn(older.stmt));
			tuple_unref(upserted.stmt);
			return;
		}
		assert(older.stmt == NULL ||
		       upserted_lsn != vy_stmt_lsn(older.stmt));
		assert(vy_stmt_type(upserted.stmt) == IPROTO_REPLACE);

		struct tuple *region_stmt =
			vy_stmt_dup_lsregion(upserted.stmt,
					     &mem->env->allocator,
					     mem->generation);
		if (region_stmt == NULL) {
			/* OOM */
			tuple_unref(upserted.stmt);
			diag_clear(diag_get());
			return;
		}

		int rc = vy_lsm_set(lsm, mem, upserted, &region_stmt);
		/**
		 * Since we have already allocated mem statement and
		 * now we replacing one statement with another, the
		 * vy_lsm_set() cannot fail.
		 */
		assert(rc == 0); (void)rc;
		tuple_unref(upserted.stmt);
		upserted.stmt = region_stmt;
		vy_mem_commit_stmt(mem, upserted);
		lsm->stat.upsert.squashed++;
	}
}

void
vy_lsm_commit_stmt(struct vy_lsm *lsm, struct vy_mem *mem,
		   struct vy_entry entry)
{
	vy_mem_commit_stmt(mem, entry);

	lsm->stat.memory.count.rows++;

	if (vy_stmt_type(entry.stmt) == IPROTO_UPSERT)
		vy_lsm_commit_upsert(lsm, mem, entry);

	vy_stmt_counter_acct_tuple(&lsm->stat.put, entry.stmt);

	/* Invalidate cache element. */
	vy_cache_on_write(&lsm->cache, entry, NULL);
}

void
vy_lsm_rollback_stmt(struct vy_lsm *lsm, struct vy_mem *mem,
		     struct vy_entry entry)
{
	vy_mem_rollback_stmt(mem, entry);

	/* Invalidate cache element. */
	vy_cache_on_write(&lsm->cache, entry, NULL);
}

int
vy_lsm_find_range_intersection(struct vy_lsm *lsm,
		const char *min_key, const char *max_key,
		struct vy_range **begin, struct vy_range **end)
{
	struct tuple_format *key_format = lsm->env->key_format;
	struct vy_entry entry;

	entry = vy_entry_key_from_msgpack(key_format, lsm->cmp_def, min_key);
	if (entry.stmt == NULL)
		return -1;
	*begin = vy_range_tree_psearch(&lsm->range_tree, entry);
	tuple_unref(entry.stmt);

	entry = vy_entry_key_from_msgpack(key_format, lsm->cmp_def, max_key);
	if (entry.stmt == NULL)
		return -1;
	*end = vy_range_tree_psearch(&lsm->range_tree, entry);
	*end = vy_range_tree_next(&lsm->range_tree, *end);
	tuple_unref(entry.stmt);

	return 0;
}

bool
vy_lsm_split_range(struct vy_lsm *lsm, struct vy_range *range)
{
	struct tuple_format *key_format = lsm->env->key_format;

	const char *split_key_raw;
	if (!vy_range_needs_split(range, vy_lsm_range_size(lsm),
				  &split_key_raw))
		return false;

	/* Split a range in two parts. */
	const int n_parts = 2;
	struct vy_range *parts[2] = { NULL, NULL };
	/*
	 * Determine new ranges' boundaries.
	 */
	struct vy_entry split_key;
	split_key = vy_entry_key_from_msgpack(key_format, lsm->cmp_def,
					      split_key_raw);
	if (split_key.stmt == NULL)
		goto fail;

	struct vy_entry keys[3];
	keys[0] = range->begin;
	keys[1] = split_key;
	keys[2] = range->end;

	/*
	 * Allocate new ranges and create slices of
	 * the old range's runs for them.
	 */
	struct vy_slice *slice, *new_slice;
	struct vy_range *part = NULL;
	for (int i = 0; i < n_parts; i++) {
		part = vy_range_new(vy_log_next_id(), keys[i], keys[i + 1],
				    lsm->cmp_def);
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
					 part->end, lsm->cmp_def,
					 &new_slice) != 0)
				goto fail;
			if (new_slice != NULL)
				vy_range_add_slice(part, new_slice);
		}
		part->needs_compaction = range->needs_compaction;
		vy_range_update_compaction_priority(part, &lsm->opts);
		vy_range_update_dumps_per_compaction(part);
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
		vy_log_insert_range(lsm->id, part->id,
				    tuple_data_or_null(part->begin.stmt),
				    tuple_data_or_null(part->end.stmt));
		rlist_foreach_entry(slice, &part->slices, in_range)
			vy_log_insert_slice(part->id, slice->run->id, slice->id,
					    tuple_data_or_null(slice->begin.stmt),
					    tuple_data_or_null(slice->end.stmt));
	}
	if (vy_log_tx_commit() < 0)
		goto fail;

	/*
	 * Replace the old range in the LSM tree.
	 */
	vy_lsm_unacct_range(lsm, range);
	vy_lsm_remove_range(lsm, range);

	for (int i = 0; i < n_parts; i++) {
		part = parts[i];
		vy_lsm_add_range(lsm, part);
		vy_lsm_acct_range(lsm, part);
	}
	lsm->range_tree_version++;

	say_info("%s: split range %s by key %s", vy_lsm_name(lsm),
		 vy_range_str(range), tuple_str(split_key.stmt));

	rlist_foreach_entry(slice, &range->slices, in_range)
		vy_slice_wait_pinned(slice);
	vy_range_delete(range);
	tuple_unref(split_key.stmt);
	return true;
fail:
	for (int i = 0; i < n_parts; i++) {
		if (parts[i] != NULL)
			vy_range_delete(parts[i]);
	}
	if (split_key.stmt != NULL)
		tuple_unref(split_key.stmt);

	diag_log();
	say_error("%s: failed to split range %s",
		  vy_lsm_name(lsm), vy_range_str(range));
	return false;
}

bool
vy_lsm_coalesce_range(struct vy_lsm *lsm, struct vy_range *range)
{
	struct vy_range *first, *last;
	if (!vy_range_needs_coalesce(range, &lsm->range_tree,
				     vy_lsm_range_size(lsm), &first, &last))
		return false;

	struct vy_range *result = vy_range_new(vy_log_next_id(),
			first->begin, last->end, lsm->cmp_def);
	if (result == NULL)
		goto fail_range;

	struct vy_range *it;
	struct vy_range *end = vy_range_tree_next(&lsm->range_tree, last);

	/*
	 * Log change in metadata.
	 */
	vy_log_tx_begin();
	vy_log_insert_range(lsm->id, result->id,
			    tuple_data_or_null(result->begin.stmt),
			    tuple_data_or_null(result->end.stmt));
	for (it = first; it != end;
	     it = vy_range_tree_next(&lsm->range_tree, it)) {
		struct vy_slice *slice;
		rlist_foreach_entry(slice, &it->slices, in_range)
			vy_log_delete_slice(slice->id);
		vy_log_delete_range(it->id);
		rlist_foreach_entry(slice, &it->slices, in_range) {
			vy_log_insert_slice(result->id, slice->run->id, slice->id,
					    tuple_data_or_null(slice->begin.stmt),
					    tuple_data_or_null(slice->end.stmt));
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
		struct vy_range *next = vy_range_tree_next(&lsm->range_tree, it);
		vy_lsm_unacct_range(lsm, it);
		vy_lsm_remove_range(lsm, it);
		rlist_splice(&result->slices, &it->slices);
		result->slice_count += it->slice_count;
		vy_disk_stmt_counter_add(&result->count, &it->count);
		if (it->needs_compaction)
			result->needs_compaction = true;
		vy_range_delete(it);
		it = next;
	}
	/*
	 * Even though coalescing increases read amplification,
	 * we don't need to compact the resulting range as long
	 * as it fits the configured LSM tree shape.
	 */
	vy_range_update_compaction_priority(result, &lsm->opts);
	vy_range_update_dumps_per_compaction(result);
	vy_lsm_acct_range(lsm, result);
	vy_lsm_add_range(lsm, result);
	lsm->range_tree_version++;

	say_info("%s: coalesced ranges %s",
		 vy_lsm_name(lsm), vy_range_str(result));
	return true;

fail_commit:
	vy_range_delete(result);
fail_range:
	diag_log();
	say_error("%s: failed to coalesce range %s",
		  vy_lsm_name(lsm), vy_range_str(range));
	return false;
}

void
vy_lsm_force_compaction(struct vy_lsm *lsm)
{
	struct vy_range *range;
	struct vy_range_tree_iterator it;

	vy_range_tree_ifirst(&lsm->range_tree, &it);
	while ((range = vy_range_tree_inext(&it)) != NULL) {
		vy_lsm_unacct_range(lsm, range);
		range->needs_compaction = true;
		vy_range_update_compaction_priority(range, &lsm->opts);
		vy_lsm_acct_range(lsm, range);
	}

	vy_range_heap_update_all(&lsm->range_heap);
}
