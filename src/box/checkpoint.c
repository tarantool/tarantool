#include "checkpoint.h"
#include "txn_limbo.h"
#include "raft.h"
#include "txn.h"

/** Data of the in-progress checkpoint to carry into the triggers. */
struct box_checkpoint_context {
	/** The checkpoint to be created. */
	struct box_checkpoint *checkpoint;
	/** The owner fiber sleeping on the result. */
	struct fiber *owner;
	/** If is committed. */
	bool is_commit;
	/** If is rolled back. */
	bool is_rollback;
};

static void
box_checkpoint_collect(struct box_checkpoint *c)
{
	txn_limbo_checkpoint(&txn_limbo, &c->limbo);
	box_raft_checkpoint_remote(&c->raft_remote);
	box_raft_checkpoint_local(&c->raft_local);
}

/** On commit of the limbo txn. */
static int
txn_commit_cb(struct trigger *trigger, void *event)
{
	(void)event;
	struct box_checkpoint_context *ctx = trigger->data;
	ctx->is_commit = true;
	box_checkpoint_collect(ctx->checkpoint);
	fiber_wakeup(ctx->owner);
	return 0;
}

/** On rollback of the limbo txn. */
static int
txn_rollback_cb(struct trigger *trigger, void *event)
{
	(void)event;
	struct box_checkpoint_context *ctx = trigger->data;
	ctx->is_rollback = true;
	fiber_wakeup(ctx->owner);
	return 0;
}

static int
txn_journal_flush(struct journal_checkpoint *out, bool do_journal_rotation)
{
	/*
	 * All the txns after preparation until all the journal write follow the
	 * same path:
	 * - The limbo volatile queue.
	 * - The journal volatile queue.
	 * - The journal write.
	 *
	 * Some steps might be skipped (for instance, the limbo might be if the
	 * txn is force-async or just async and the limbo is empty). But the
	 * order never changes.
	 *
	 * It means, that if one would want to closely follow the latest known
	 * prepared txn until it reaches WAL, then following this path the
	 * needed txn will be surely found before any new txn is added (except
	 * for force-async, which might skip the volatile limbo queue and go
	 * directly to the journal).
	 */
	if (txn_limbo_flush(&txn_limbo) != 0)
		return -1;
	if (do_journal_rotation)
		return journal_begin_checkpoint(out);
	memset(out, 0, sizeof(*out));
	return journal_sync(&out->vclock);
}

int
txn_persist_all_prepared(struct vclock *out)
{
	struct journal_checkpoint journal;
	if (txn_journal_flush(&journal, false /* do journal rotation */) != 0)
		return -1;
	if (out != NULL)
		vclock_copy(out, &journal.vclock);
	return 0;
}

/** Build a checkpoint of all the transaction-related global states. */
static int
txn_checkpoint_build(struct box_checkpoint *out, bool do_journal_rotation)
{
	struct txn_limbo *limbo = &txn_limbo;
	/* Fast path. */
	if (txn_limbo_is_empty(limbo)) {
		box_checkpoint_collect(out);
		return txn_journal_flush(&out->journal, do_journal_rotation);
	}
	/*
	 * Slow path. When the limbo is not empty, it is relatively complicated
	 * to create a checkpoint of it. Because while waiting for its flush and
	 * then waiting for the journal sync, it might receive new volatile
	 * txns. And then it becomes too late to "wait for last synchro txn to
	 * get committed". Because the last synchro txn has changed.
	 *
	 * The only possible way is to remember what was the last txn before
	 * doing any waiting. And then collect the checkpoint **exactly** when
	 * the last txn gets committed. Doing it even one fiber yield later
	 * might result into more synchro txns getting confirmed and moving the
	 * limbo state forward. Making the collected checkpoint "too new".
	 */
	struct box_checkpoint_context ctx = {
		.checkpoint = out,
		.owner = fiber(),
		.is_commit = false,
		.is_rollback = false,
	};
	struct trigger on_commit;
	trigger_create(&on_commit, txn_commit_cb, &ctx, NULL);
	struct trigger on_rollback;
	trigger_create(&on_rollback, txn_rollback_cb, &ctx, NULL);
	struct txn_limbo_entry *tle = txn_limbo_last_synchro_entry(limbo);
	txn_on_commit(tle->txn, &on_commit);
	txn_on_rollback(tle->txn, &on_rollback);
	/*
	 * Make sure that all changes at the time of checkpoint start have
	 * reached WAL and get the vclock collected exactly at that moment.
	 *
	 * For async txns the persistence means commit. For sync txns need to
	 * wait for their confirmation explicitly.
	 */
	if (txn_journal_flush(&out->journal, do_journal_rotation) != 0)
		return -1;
	/*
	 * The synchronous transactions, persisted above, might still be not
	 * committed. Lets make sure they are, so the checkpoint won't have any
	 * rolled back data.
	 */
	while (!ctx.is_rollback && !ctx.is_commit) {
		if (fiber_is_cancelled()) {
			trigger_clear(&on_commit);
			trigger_clear(&on_rollback);
			diag_set(FiberIsCancelled);
			return -1;
		}
		fiber_yield();
	}
	if (ctx.is_rollback) {
		diag_set(ClientError, ER_SYNC_ROLLBACK);
		return -1;
	}
	return 0;
}

int
box_checkpoint_build_in_memory(struct box_checkpoint *out)
{
	return txn_checkpoint_build(out, false /* do journal rotation */);
}

int
box_checkpoint_build_on_disk(struct box_checkpoint *out, bool is_scheduled)
{
	int rc;
	struct engine_checkpoint_params engine_params = {
		.is_scheduled = is_scheduled,
		.box = out,
	};
	rc = engine_begin_checkpoint(&engine_params);
	if (rc != 0)
		goto out;
	rc = txn_checkpoint_build(out, true /* do journal rotation */);
	if (rc != 0)
		goto out;
	rc = engine_commit_checkpoint(&out->journal.vclock);
	if (rc != 0)
		goto out;
	journal_commit_checkpoint(&out->journal);
out:
	if (rc != 0)
		engine_abort_checkpoint();
	return rc;
}

#if defined(ENABLE_FETCH_SNAPSHOT_CURSOR)
#include "checkpoint_from_snapshot.c"
#else
int
box_checkpoint_build_from_snapshot(struct box_checkpoint *out,
				   const struct vclock *vclock)
{
	(void)out;
	(void)vclock;
	diag_set(ClientError, ER_UNSUPPORTED, "Community edition",
		 "checkpoint from snapshot");
	return -1;
}
#endif
