#include "index_build.h"

#include "key_list.h"
#include "space.h"
#include "tuple.h"
#include "trivia/util.h"
#include "txn.h"
#include "txn_limbo.h"

#define RB_COMPACT 1
#include "small/rb.h"
#include "small/region.h"

struct index_build_ctx;

struct index_build_stmt_trigger {
	struct rlist in_ctx;
	bool has_run_before_replace;
	struct trigger before_commit;
	struct trigger on_commit;
	struct trigger on_rollback;
	struct index_build_ctx *ctx;
};

struct index_build_write_point {
	struct tuple *tuple;
	struct txn *txn;
	struct index_build_ctx *ctx;
	rb_node(struct index_build_write_point) in_ctx;
};

typedef rb_tree(struct index_build_write_point) index_build_write_set_t;

struct index_build_processed_key {
	struct tuple *tuple;
	struct index_build_ctx *ctx;
	rb_node(struct index_build_processed_key) in_ctx;
};

typedef rb_tree(struct index_build_processed_key) index_build_processed_set_t;

struct index_build_not_confirmed_key {
	struct tuple *tuple;
	int count;
	struct index_build_ctx *ctx;
	rb_node(struct index_build_not_confirmed_key) in_ctx;
};

typedef rb_tree(struct index_build_not_confirmed_key) index_build_not_confirmed_set_t;

struct index_build_ctx {
	struct index *index;
	struct tuple_format *format;
	bool check_unique_constraint;
	const struct index_build_vtab *vtab;
	struct tuple *cursor;
	struct key_def *cmp_def;
	index_build_write_set_t write_set;
	index_build_processed_set_t processed;
	index_build_not_confirmed_set_t not_confirmed;
	struct rlist stmt_triggers;
	bool is_failed;
	struct diag diag;
	struct trigger on_replace;
};

static inline int
index_build_write_set_cmp(const struct index_build_write_point *a,
			  const struct index_build_write_point *b)
{
	struct key_def *key_def = a->ctx->index->def->key_def;
	int rc = tuple_compare(a->tuple, HINT_NONE,
			       b->tuple, HINT_NONE, key_def);
	if (rc == 0)
		rc = a->txn < b->txn ? -1 : a->txn > b->txn;
	return rc;
}

rb_gen(MAYBE_UNUSED static inline, index_build_write_set_,
       index_build_write_set_t, struct index_build_write_point,
       in_ctx, index_build_write_set_cmp);

static inline int
index_build_processed_set_cmp(const struct index_build_processed_key *a,
			      const struct index_build_processed_key *b)
{
	return tuple_compare(a->tuple, HINT_NONE, b->tuple, HINT_NONE,
			     a->ctx->cmp_def);
}

rb_gen(MAYBE_UNUSED static inline, index_build_processed_set_,
       index_build_processed_set_t, struct index_build_processed_key,
       in_ctx, index_build_processed_set_cmp);

static inline int
index_build_not_confirmed_set_cmp(
	const struct index_build_not_confirmed_key *a,
	const struct index_build_not_confirmed_key *b)
{
	struct key_def *key_def = a->ctx->index->def->key_def;
	return tuple_compare(a->tuple, HINT_NONE,
			     b->tuple, HINT_NONE, key_def);
}

rb_gen(MAYBE_UNUSED static inline, index_build_not_confirmed_set_,
       index_build_not_confirmed_set_t, struct index_build_not_confirmed_key,
       in_ctx, index_build_not_confirmed_set_cmp);

static int
index_build_on_replace(struct trigger *trigger, void *event);

static void
index_build_write_set_free(struct index_build_ctx *ctx);

static void
index_build_processed_set_free(struct index_build_ctx *ctx);

static void
index_build_not_confirmed_set_free(struct index_build_ctx *ctx);

struct mempool index_build_processed_key_pool;

void
index_build_init(void)
{
	mempool_create(&index_build_processed_key_pool, cord_slab_cache(),
		       sizeof(struct index_build_processed_key));
}

void
index_build_free(void)
{
	mempool_destroy(&index_build_processed_key_pool);
}

int
generic_index_build_replace_confirmed(struct index *index, struct tuple *tuple)
{
	(void)index;
	(void)tuple;
	return 0;
}

int
generic_index_build_replace_in_progress(struct index *index,
					struct tuple *old_tuple,
					struct tuple *new_tuple)
{
	(void)index;
	(void)old_tuple;
	(void)new_tuple;
	return 0;
}

int
generic_index_build_rollback(struct index *index,
			     struct tuple *old_tuple,
			     struct tuple *new_tuple)
{
	(void)index;
	(void)old_tuple;
	(void)new_tuple;
	return 0;
}

int
generic_index_build_finalize(struct index *index)
{
	(void)index;
	return 0;
}

static struct index_build_write_point *
index_build_write_point_new(struct tuple *tuple, struct index_build_ctx *ctx)
{
	assert(in_txn() != NULL);
	struct index_build_write_point *point;
	point = xregion_alloc_object(&in_txn()->region,
				     struct index_build_write_point);
	point->tuple = tuple;
	tuple_ref(point->tuple);
	point->txn = in_txn();
	point->ctx = ctx;
	return point;
}

static void
index_build_write_point_delete(struct index_build_write_point *point)
{
	tuple_unref(point->tuple);
}

static struct index_build_not_confirmed_key *
index_build_not_confirmed_key_new(struct tuple *tuple, struct index_build_ctx *ctx)
{
	struct index_build_not_confirmed_key *new_key
		= xmalloc(sizeof(struct index_build_not_confirmed_key));
	new_key->tuple = tuple;
	tuple_ref(new_key->tuple);
	new_key->count = 1;
	new_key->ctx = ctx;
	return new_key;
}

static void
index_build_not_confirmed_key_delete(struct index_build_not_confirmed_key *key)
{
	tuple_unref(key->tuple);
	free(key);
}

static struct index_build_not_confirmed_key *
index_build_not_confirmed_set_free_cb(index_build_not_confirmed_set_t *set,
	struct index_build_not_confirmed_key *key, void *arg)
{
	(void)set;
	(void)arg;
	index_build_not_confirmed_key_delete(key);
	return NULL;
}

static void
index_build_not_confirmed_set_free(struct index_build_ctx *ctx)
{
	index_build_not_confirmed_set_iter(&ctx->not_confirmed, NULL,
		index_build_not_confirmed_set_free_cb, NULL);
}

void
index_build_track(struct tuple *tuple, struct index_build_ctx *ctx)
{
	assert(in_txn() != NULL);
	if (tuple == NULL)
		return;
	struct index_build_write_point *new_point =
		index_build_write_point_new(tuple, ctx);
	/*
	 * One transaction may update the same key multiple times.
	 * Do not insert duplicates into the rb-tree.
	 */
	if (index_build_write_set_search(&ctx->write_set, new_point) != NULL) {
		index_build_write_point_delete(new_point);
		return;
	}
	index_build_write_set_insert(&ctx->write_set, new_point);
}

static void
index_build_untrack(struct tuple *tuple, struct index_build_ctx *ctx)
{
	assert(in_txn() != NULL);
	if (tuple == NULL)
		return;
	struct index_build_write_point key = {
		.tuple = tuple,
		.txn = in_txn(),
		.ctx = ctx
	};
	struct index_build_write_point *point =
		index_build_write_set_search(&ctx->write_set, &key);
	/*
	 * This can happen if one transaction inserts the same key multiple
	 * times. In that case, rollback of the first statement has already
	 * untracked this key.
	 */
	if (point == NULL)
		return;
	index_build_write_set_remove(&ctx->write_set, point);
	index_build_write_point_delete(point);
}

static void
index_build_abort_writers(struct tuple *tuple, struct index_build_ctx *ctx)
{
	if (tuple == NULL)
		return;
	struct index_build_write_point key = {
		.tuple = tuple,
		.txn = NULL,
		.ctx = ctx
	};
	struct key_def *key_def = ctx->index->def->key_def;

	struct index_build_write_set_iterator it;
	index_build_write_set_isearch_ge(&ctx->write_set, &key, &it);
	for (struct index_build_write_point *abort =
	     index_build_write_set_iterator_get(&it);
	     abort != NULL &&
	     tuple_compare(tuple, HINT_NONE,
			   abort->tuple, HINT_NONE, key_def) == 0;
	     abort = index_build_write_set_inext(&it)) {
		assert(abort->txn != NULL);
		assert(abort->txn != in_txn());
		txn_abort_with_conflict(abort->txn);
	}
}

static struct index_build_write_point *
index_build_write_set_free_cb(index_build_write_set_t *write_set,
		       	      struct index_build_write_point *point, void *arg)
{
	(void)arg;
	(void)write_set;
	index_build_write_point_delete(point);
	return NULL;
}

static void
index_build_write_set_free(struct index_build_ctx *ctx)
{
	index_build_write_set_iter(
		&ctx->write_set, NULL, index_build_write_set_free_cb, NULL);
}

static struct index_build_processed_key *
index_build_processed_key_new(struct tuple *tuple, struct index_build_ctx *ctx)
{
	struct index_build_processed_key *key;
	key = xmempool_alloc(&index_build_processed_key_pool);
	key->tuple = tuple;
	tuple_ref(key->tuple);
	key->ctx = ctx;
	return key;
}

static void
index_build_processed_key_delete(struct index_build_processed_key *key)
{
	tuple_unref(key->tuple);
	mempool_free(&index_build_processed_key_pool, key);
}

static void
index_build_mark_processed(struct tuple *tuple, struct index_build_ctx *ctx)
{
	assert(tuple != NULL);
	struct index_build_processed_key *new_key =
		index_build_processed_key_new(tuple, ctx);
	if (index_build_processed_set_search(&ctx->processed, new_key) != NULL) {
		index_build_processed_key_delete(new_key);
		return;
	}
	index_build_processed_set_insert(&ctx->processed, new_key);
}

static bool
index_build_processed(struct tuple *tuple, struct index_build_ctx *ctx)
{
	assert(tuple != NULL);
	struct index_build_processed_key key = {
		.tuple = tuple,
		.ctx = ctx,
	};
	struct index_build_processed_key *found =
		index_build_processed_set_search(&ctx->processed, &key);
	return found != NULL;
}

static bool
index_build_take_processed(struct tuple *tuple, struct index_build_ctx *ctx)
{
	if (tuple == NULL)
		return false;
	struct index_build_processed_key key = {
		.tuple = tuple,
		.ctx = ctx,
	};
	struct index_build_processed_key *found =
		index_build_processed_set_search(&ctx->processed, &key);
	if (found == NULL)
		return false;
	index_build_processed_set_remove(&ctx->processed, found);
	index_build_processed_key_delete(found);
	return true;
}

static void
index_build_discard_processed_lt(struct tuple *tuple, struct index_build_ctx *ctx)
{
	if (tuple == NULL)
		return;
	struct index_build_processed_key *key =
		index_build_processed_set_first(&ctx->processed);
	while (key != NULL &&
	       tuple_compare(key->tuple, HINT_NONE, tuple, HINT_NONE,
			     ctx->cmp_def) < 0) {
		index_build_processed_set_remove(&ctx->processed, key);
		index_build_processed_key_delete(key);
		key = index_build_processed_set_first(&ctx->processed);
	}
}

static struct index_build_processed_key *
index_build_processed_set_free_cb(index_build_processed_set_t *processed,
				  struct index_build_processed_key *key,
				  void *arg)
{
	(void)arg;
	(void)processed;
	index_build_processed_key_delete(key);
	return NULL;
}

static void
index_build_processed_set_free(struct index_build_ctx *ctx)
{
	index_build_processed_set_iter(&ctx->processed, NULL,
				       index_build_processed_set_free_cb,
				       NULL);
}

static int
index_build_get_by_key(struct tuple *tuple, struct index *index,
		       struct tuple **result)
{
	struct key_def *key_def = index->def->key_def;
	uint32_t part_count = key_def->part_count;
	const char *key = tuple_data(tuple);
	/* Cut array header */
	mp_decode_array(&key);

	return index_get_internal(index, key, part_count, result);
}

static int
index_build_get_by_tuple(struct tuple *tuple, struct index *index,
			 struct tuple **result, int multikey_idx)
{
	struct key_def *key_def = index->def->key_def;
	uint32_t part_count = key_def->part_count;
	size_t region_svp = region_used(&fiber()->gc);
	/* Extract the key from tuple. */
	const char *key =
		tuple_extract_key(tuple, key_def, multikey_idx, NULL);
	if (key == NULL)
		return -1;
	/* Cut array header */
	mp_decode_array(&key);

	int rc = index_get_internal(index, key, part_count, result);
	region_truncate(&fiber()->gc, region_svp);
	return rc;
}

/** Return true if the current transaction is aborted or statement is rolling back. */
static inline bool
index_build_txn_is_aborted(void)
{
	struct txn *txn = in_txn();
	if (txn == NULL)
		return false;
	return txn->status == TXN_ABORTED ||
	       txn_has_flag(txn, TXN_STMT_ROLLBACK);
}

static int
index_build_check_is_unique_secondary_one(struct tuple *old_tuple,
					  struct tuple *new_tuple,
					  struct index *index,
					  int multikey_idx,
					  bool is_key)
{
	assert(new_tuple != NULL);

	struct key_def *key_def = index->def->key_def;
	if (key_def->is_nullable &&
	    tuple_key_contains_null(new_tuple, key_def, multikey_idx))
		return 0;

	struct tuple *dup_tuple;
	if (is_key) {
		if (index_build_get_by_key(new_tuple, index, &dup_tuple) != 0)
			return -1;
	} else {
		if (index_build_get_by_tuple(new_tuple, index, &dup_tuple,
					     multikey_idx) != 0)
			return -1;
	}

	if (index_build_txn_is_aborted())
			return 0;

	if (dup_tuple != NULL && old_tuple != dup_tuple) {
		diag_set(ClientError, ER_TUPLE_FOUND,
			 index->def->name,
			 index->def->space_name,
			 tuple_str(dup_tuple),
			 tuple_str(new_tuple),
			 dup_tuple, new_tuple);
		return -1;
	}
	return 0;
}

static int
index_build_check_is_unique_secondary(struct tuple *old_tuple,
				      struct tuple *new_tuple,
				      struct index *index)
{
	struct key_def *key_def = index->def->key_def;
	if (key_def->for_func_index) {
		assert(new_tuple != NULL);
		struct region *region = &fiber()->gc;
		size_t region_svp = region_used(region);
		struct key_list_iterator it;
		if (key_list_iterator_create(&it, new_tuple, index->def,
					     true,
					     tuple_format_runtime) != 0) {
			region_truncate(region, region_svp);
			return -1;
		}
		int rc = 0;
		struct tuple *key_tuple = NULL;
		while ((rc = key_list_iterator_next(&it, &key_tuple)) == 0 &&
		       key_tuple != NULL) {
			rc = index_build_check_is_unique_secondary_one(
				old_tuple, key_tuple, index, MULTIKEY_NONE, true);
			key_tuple = NULL;
			if (rc != 0)
				break;
			if (index_build_txn_is_aborted())
				return 0;
		}
		region_truncate(region, region_svp);
		return rc;
	}
	if (!index->def->key_def->is_multikey) {
		return index_build_check_is_unique_secondary_one(old_tuple,
								 new_tuple,
								 index,
								 MULTIKEY_NONE,
								 false);
	}
	int count = tuple_multikey_count(new_tuple, index->def->key_def);
	for (int i = 0; i < count; ++i) {
		if (index_build_check_is_unique_secondary_one(old_tuple,
							      new_tuple,
							      index,
							      i,
							      false) != 0)
			return -1;
	}
	return 0;
}

static int
index_build_on_prepare_stmt(struct trigger *base, void *event);

static int
index_build_on_confirm_stmt(struct trigger *base, void *event);

static int
index_build_on_rollback_stmt(struct trigger *base, void *event);

static void
index_build_stmt_trigger_add(struct txn_stmt *stmt, struct index_build_ctx *ctx)
{
	struct index_build_stmt_trigger *stmt_trigger =
		xregion_alloc_object(&in_txn()->region,
				     struct index_build_stmt_trigger);
	stmt_trigger->has_run_before_replace = false;
	stmt_trigger->ctx = ctx;
	rlist_add_entry(&ctx->stmt_triggers, stmt_trigger, in_ctx);
	trigger_create(&stmt_trigger->before_commit,
		       index_build_on_prepare_stmt, NULL, NULL);
	trigger_create(&stmt_trigger->on_commit,
		       index_build_on_confirm_stmt, NULL, NULL);
	trigger_create(&stmt_trigger->on_rollback,
		       index_build_on_rollback_stmt, NULL, NULL);
	txn_stmt_before_commit(stmt, &stmt_trigger->before_commit);
	txn_stmt_on_commit(stmt, &stmt_trigger->on_commit);
	txn_stmt_on_rollback(stmt, &stmt_trigger->on_rollback);
}

static void
index_build_stmt_trigger_delete(struct index_build_stmt_trigger *trigger)
{
	/* The trigger is fired and will be deleted - remove from the state. */
	rlist_del_entry(trigger, in_ctx);
}

void
index_build_not_confirmed_key_incr(struct tuple *tuple, struct index_build_ctx *ctx)
{
	assert(ctx->check_unique_constraint);
	if (tuple == NULL)
		return;
	struct index_build_not_confirmed_key *new_key =
		index_build_not_confirmed_key_new(tuple, ctx);
	struct index_build_not_confirmed_key *found =
		index_build_not_confirmed_set_search(&ctx->not_confirmed, new_key);
	if (found != NULL) {
		++found->count;
		index_build_not_confirmed_key_delete(new_key);
	} else {
		index_build_not_confirmed_set_insert(&ctx->not_confirmed, new_key);
	}
}

void
index_build_not_confirmed_key_decr(struct tuple *tuple, struct index_build_ctx *ctx)
{
	assert(ctx->check_unique_constraint);
	if (tuple == NULL)
		return;
	struct index_build_not_confirmed_key key = {
		.tuple = tuple,
		.ctx = ctx,
	};
	struct index_build_not_confirmed_key *found =
		index_build_not_confirmed_set_search(&ctx->not_confirmed, &key);
	assert(found != NULL);
	if (--found->count == 0) {
		index_build_not_confirmed_set_remove(&ctx->not_confirmed, found);
		index_build_not_confirmed_key_delete(found);
	}
}

static int
index_build_on_prepare_stmt(struct trigger *base, void *event)
{
	assert(!index_build_txn_is_aborted());
	int csw = fiber()->csw;

	struct txn_stmt *stmt = (struct txn_stmt *)event;
	struct index_build_stmt_trigger *trigger =
		container_of(base, struct index_build_stmt_trigger,
			     before_commit);
	struct index_build_ctx *ctx = trigger->ctx;
	if (ctx->is_failed)
		goto out;

	trigger->has_run_before_replace = true;

	struct tuple *mark = stmt->new_tuple != NULL ? stmt->new_tuple :
						       stmt->old_tuple;

	bool le = ctx->cursor != NULL && tuple_compare(mark, HINT_NONE,
						       ctx->cursor, HINT_NONE,
						       ctx->cmp_def) <= 0;
	struct tuple *old_tuple = (le || index_build_processed(mark, ctx)) ?
		stmt->old_tuple : NULL;

	if (ctx->check_unique_constraint)
		index_build_not_confirmed_key_incr(stmt->old_tuple, ctx);
	
	int rc = ctx->vtab->replace_in_progress(ctx->index, old_tuple,
						stmt->new_tuple);
	VERIFY(fiber()->csw == csw);
	/*
	 * Therefore, if an error occurs, it is most likely an index-related
	 * issue, and there is likely no point in rolling back the transaction;
	 * it would be more correct to abort the build.
	 */
	if (rc != 0) {
		ctx->is_failed = true;
		diag_move(diag_get(), &ctx->diag);
		goto out;
	}

	if (!le)
		index_build_mark_processed(mark, ctx);
	if (ctx->check_unique_constraint) {
		index_build_untrack(stmt->new_tuple, ctx);
		index_build_abort_writers(stmt->new_tuple, ctx);
		assert(!index_build_txn_is_aborted());
	}
out:
	VERIFY(fiber()->csw == csw);
	return 0;
}

static int
index_build_on_confirm_stmt(struct trigger *base, void *event)
{
	(void)event;
	struct index_build_stmt_trigger *trigger =
		container_of(base, struct index_build_stmt_trigger,
			     on_commit);
	index_build_stmt_trigger_delete(trigger);
	return 0;
}

static int
index_build_on_rollback_stmt(struct trigger *base, void *event)
{
	int csw = fiber()->csw;
	struct txn_stmt *stmt = (struct txn_stmt *)event;
	assert(stmt != NULL);

	struct index_build_stmt_trigger *trigger =
		container_of(base, struct index_build_stmt_trigger,
			     on_rollback);
	/*
	 * It does not free the memory under the trigger, does not invalidate
	 * the data, so we can safely use it further.
	 */
	index_build_stmt_trigger_delete(trigger);

	struct index_build_ctx *ctx = trigger->ctx;

	if (ctx->is_failed)
		goto out;

	if (!trigger->has_run_before_replace) {
		if (ctx->check_unique_constraint)
			index_build_untrack(stmt->new_tuple, ctx);
		goto out;
	}

	if (stmt->old_tuple != NULL &&
	    engine_tuple_validate(ctx->index->engine, ctx->format,
				  stmt->old_tuple) != 0) {
		ctx->is_failed = true;
		diag_move(diag_get(), &ctx->diag);
		goto out;
	}

	if (ctx->check_unique_constraint && stmt->old_tuple != NULL) {
		index_build_abort_writers(stmt->old_tuple, ctx);
	}

	if (ctx->check_unique_constraint)
		index_build_not_confirmed_key_decr(stmt->old_tuple, ctx);

	int rc = ctx->vtab->rollback(ctx->index, stmt->old_tuple,
				     stmt->new_tuple);
	if (rc != 0) {
		ctx->is_failed = true;
		diag_move(diag_get(), &ctx->diag);
	}

out:
	VERIFY(fiber()->csw == csw);
	return 0;
}

static int
index_build_on_replace(struct trigger *trigger, void *event)
{
	assert(!index_build_txn_is_aborted());

	struct txn *txn = event;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct index_build_ctx *ctx = trigger->data;

	if (stmt->new_tuple != NULL &&
	    engine_tuple_validate(ctx->index->engine, ctx->format,
				  stmt->new_tuple) != 0)
		return -1;

	if (ctx->check_unique_constraint && stmt->new_tuple != NULL) {
		if (index_build_check_is_unique_secondary(stmt->old_tuple,
					        	  stmt->new_tuple,
							  ctx->index) != 0)
			return -1;
		
		if (index_build_txn_is_aborted())
			return 0;

		index_build_track(stmt->new_tuple, ctx);
	}

	index_build_stmt_trigger_add(stmt, ctx);
	return 0;
}

int
index_build(struct space *src_space, struct index *new_index,
	    struct tuple_format *new_format,
	    bool check_unique_constraint,
	    const struct index_build_vtab *vtab,
	    bool can_yield, int yield_loops, bool need_wal_sync)
{
	assert(in_txn() == NULL);

	struct index *pk = index_find(src_space, 0);
	if (pk == NULL)
		return -1;

	struct errinj *inj = errinj(ERRINJ_BUILD_INDEX, ERRINJ_INT);
	if (inj != NULL && inj->iparam == (int)new_index->def->iid) {
		diag_set(ClientError, ER_INJECTION, "build index");
		return -1;
	}

	/*
	 * The index is currently built inside an in-progress DDL transaction,
	 * while the transaction is detached from the fiber during the build.
	 * If space_invalidate is called here, the DDL transaction will be
	 * rolled back. Therefore, the space_invalidate call was temporarily
	 * moved up to <engine>_space_build_index, where the fiber still has
	 * a transaction attached.
	 * TODO: In the future, when DDL is committed separately and index
	 * build starts in the background afterwards, space_invalidate can be
	 * moved back here.
	 */
	/* Abort all in-progress transactions. */
	//space_invalidate(src_space);

	inj = errinj(ERRINJ_BUILD_INDEX_DISABLE_YIELD, ERRINJ_BOOL);
	if (inj != NULL && inj->bparam == true)
		can_yield = false;

	if (new_index->def->iid == 0)
		check_unique_constraint = false;

	int rc;
	struct index_build_ctx ctx;

	if (can_yield) {
		ctx.index = new_index,
		ctx.format = new_format,
		ctx.check_unique_constraint = check_unique_constraint,
		ctx.vtab = vtab,
		ctx.cmp_def = pk->def->key_def,
		ctx.cursor = NULL;
		ctx.is_failed = false;
	
		if (ctx.check_unique_constraint) {
			index_build_write_set_new(&ctx.write_set);
			index_build_not_confirmed_set_new(&ctx.not_confirmed);
		}
		index_build_processed_set_new(&ctx.processed);
		rlist_create(&ctx.stmt_triggers);
		diag_create(&ctx.diag);
	
		trigger_create(&ctx.on_replace, index_build_on_replace, &ctx, NULL);
		/* Set on_replace trigger before flushing. */
		trigger_add(&src_space->on_replace, &ctx.on_replace);		

		if (need_wal_sync) {
			/* Flush all queues. */
			if ((rc = journal_sync(NULL)) != 0)
				goto out;
			if ((rc = txn_limbo_flush(&txn_limbo)) != 0)
				goto out;
		}
	}

	/*
	 * It doesn't matter what isolation level the iterator has here -
	 * whether it sees prepared tuples or not, the main loop will still
	 * insert only confirmed tuples that existed before index build
	 * started.
	 */
	struct iterator *it = index_create_iterator(pk, ITER_ALL, NULL, 0);
	if (it == NULL)
		return -1;

	struct tuple *tuple;
	size_t count = 0;
	while ((rc = iterator_next(it, &tuple)) == 0 &&
	       tuple != NULL) {
		if (can_yield) {
			index_build_discard_processed_lt(tuple, &ctx);
			/*
			* The main loop simply skips keys touched by the on_prepare
			* trigger. Consistency of such keys is then handled by
			* on_prepare/on_rollback triggers.
			*/
			if (index_build_take_processed(tuple, &ctx))
				continue;
		}

		rc = engine_tuple_validate(new_index->engine, new_format,
					   tuple);
		if (rc != 0)
			break;

		if (check_unique_constraint) {
			if (index_build_check_is_unique_secondary(
			    NULL, tuple, new_index) != 0) {
				rc = -1;
				break;
			}
			/*
			 * A duplicate may appear here because, besides the on_prepare
			 * trigger for concurrent transactions, we also have the main
			 * loop inserting confirmed tuples.
			 * So if a duplicate appears, at least one of the two
			 * conflicting tuples existed before the build started.
			 */
			if (can_yield) {
				struct index_build_not_confirmed_key key = {
					.tuple = tuple,
					.ctx = &ctx,
				};
				if (index_build_not_confirmed_set_search(&ctx.not_confirmed,
									 &key) != NULL) {
					rc = -1;
					break;
				}
				index_build_abort_writers(tuple, &ctx);
			}
		}

		rc = vtab->replace_confirmed(new_index, tuple);
		if (rc != 0)
			break;

		ERROR_INJECT_DOUBLE(ERRINJ_BUILD_INDEX_TIMEOUT,
				    inj->dparam > 0,
				    thread_sleep(inj->dparam));
		if (!can_yield)
			continue;
		/*
		 * Remember the latest inserted tuple to avoid processing yet
		 * to be added tuples in on_replace triggers.
		 */
		if (ctx.cursor != NULL)
			tuple_unref(ctx.cursor);
		ctx.cursor = tuple;
		tuple_ref(ctx.cursor);

		if (++count % yield_loops == 0)
			fiber_sleep(0);
		/*
		 * Sleep after at least one tuple is inserted to test
		 * on_replace triggers for index build.
		 */
		ERROR_INJECT_YIELD(ERRINJ_BUILD_INDEX_DELAY);
		if (fiber_is_cancelled()) {
			rc = -1;
			diag_set(FiberIsCancelled);
			break;
		}
		/* The on_replace trigger may have failed during the yield. */
		if (ctx.is_failed) {
			rc = -1;
			diag_move(&ctx.diag, diag_get());
			break;
		}
	}

	iterator_delete(it);
	/* vinyl */
	if (rc == 0)
		rc = vtab->finalize(new_index);
out:
	if (can_yield) {
		/*
		 * Immediately after this (without yielding), alter_space_do
		 * will call space_cache_replace and all in-progress transactions
		 * will be aborted.
		 * Prepared transactions in the queue are fine as well: they no
		 * longer need on_rollback triggers, because after
		 * space_cache_replace these tuples will be rolled back in the
		 * standard way. Duplicate absence is guaranteed at the engine
		 * level.
		 */
		if (ctx.check_unique_constraint) {
			index_build_write_set_free(&ctx);
			index_build_not_confirmed_set_free(&ctx);
		}
		index_build_processed_set_free(&ctx);

		struct index_build_stmt_trigger *trg, *tmp;
		rlist_foreach_entry_safe(trg, &ctx.stmt_triggers, in_ctx, tmp) {
			if (!trg->has_run_before_replace)
				trigger_clear(&trg->before_commit);
			trigger_clear(&trg->on_commit);
			trigger_clear(&trg->on_rollback);
		}
		diag_destroy(&ctx.diag);
		trigger_clear(&ctx.on_replace);
		if (ctx.cursor != NULL)
			tuple_unref(ctx.cursor);
	}
	return rc;
}
