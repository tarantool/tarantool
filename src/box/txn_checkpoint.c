#include "txn_checkpoint.h"
#include "txn_limbo.h"
#include "raft.h"
#include "txn.h"

struct txn_context {
	struct txn_checkpoint *checkpoint;
	struct fiber *owner;
	bool is_commit;
	bool is_rollback;
};

static int
txn_commit_cb(struct trigger *trigger, void *event)
{
	say_info("txn checkpoint on commit collect");
	(void)event;
	struct txn_context *ctx = (struct txn_context *)trigger->data;
	ctx->is_commit = true;
	txn_limbo_checkpoint(&txn_limbo, &ctx->checkpoint->limbo_checkpoint, &ctx->checkpoint->limbo_vclock);
	box_raft_checkpoint_remote(&ctx->checkpoint->raft_remote_checkpoint);
	fiber_wakeup(ctx->owner);
	return 0;
}

static int
txn_rollback_cb(struct trigger *trigger, void *event)
{
	say_info("txn checkpoint on rollback");
	(void)event;
	struct txn_context *ctx = (struct txn_context *)trigger->data;
	ctx->is_rollback = true;
	fiber_wakeup(ctx->owner);
	return 0;
}

int
txn_checkpoint_build(struct txn_checkpoint *out)
{
	struct txn_limbo *limbo = &txn_limbo;
	if (txn_limbo_is_empty(limbo)) {
		say_info("txn checkpoint no limbo");
		txn_limbo_checkpoint(limbo, &out->limbo_checkpoint, &out->limbo_vclock);
		box_raft_checkpoint_remote(&out->raft_remote_checkpoint);
		return txn_persist_all_prepared(&out->journal_vclock);
	}
	struct txn_context ctx;
	ctx.checkpoint = out;
	ctx.owner = fiber();
	ctx.is_commit = false;
	ctx.is_rollback = false;
	struct trigger on_commit;
	trigger_create(&on_commit, txn_commit_cb, &ctx, NULL);
	struct trigger on_rollback;
	trigger_create(&on_rollback, txn_rollback_cb, &ctx, NULL);
	struct txn_limbo_entry *tle = txn_limbo_last_synchro_entry(limbo);
	txn_on_commit(tle->txn, &on_commit);
	txn_on_rollback(tle->txn, &on_rollback);
	tle->is_corner_stone = true;
	say_info("txn checkpoint wait persisted");
	/*
	 * Make sure that all changes visible from the frozen read view have
	 * reached WAL and get the vclock collected exactly at that moment.
	 *
	 * For async txns the persistence means commit. For sync txns need to
	 * wait for their confirmation explicitly.
	 *
	 * This cannot be done in prepare_join, as we should not
	 * yield between read-view creation. Moreover, wal syncing
	 * should happen after creation of all engine's read-views.
	 */
	if (txn_persist_all_prepared(&out->journal_vclock) != 0)
		return -1;
	/*
	 * The synchronous transactions, persisted above, might still be not
	 * committed. Lets make sure they are, so the read-view won't have any
	 * rolled back data.
	 */
	say_info("txn checkpoint wait commit");
	while (!ctx.is_rollback && !ctx.is_commit) {
		if (fiber_is_cancelled()) {
			trigger_clear(&on_commit);
			trigger_clear(&on_rollback);
			diag_set(FiberIsCancelled);
			say_info("txn checkpoint cancelled");
			return -1;
		}
		fiber_yield();
	}
	if (ctx.is_rollback) {
		say_info("txn checkpoint rollback");
		diag_set(ClientError, ER_SYNC_ROLLBACK);
		return -1;
	}
	say_info("txn checkpoint success");
	return 0;
}
