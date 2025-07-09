#include "txn_checkpoint.h"
#include "txn_limbo.h"
#include "raft.h"
#include "txn.h"

int
txn_checkpoint_build(struct txn_checkpoint *out)
{
	struct txn_limbo *limbo = &txn_limbo;
	/*
	 * Make sure that all changes at the time of checkpoint start have
	 * reached WAL and get the vclock collected exactly at that moment.
	 *
	 * For async txns the persistence means commit. For sync txns need to
	 * wait for their confirmation explicitly.
	 */
	if (txn_persist_all_prepared(&out->journal_vclock) != 0)
		return -1;
	/*
	 * The synchronous transactions, persisted above, might still be not
	 * committed. Lets make sure they are, so the checkpoint won't have any
	 * rolled back data.
	 */
	if (txn_limbo_wait_confirm(limbo) != 0)
		return -1;

	txn_limbo_checkpoint(limbo, &out->limbo_checkpoint, &out->limbo_vclock);
	box_raft_checkpoint_remote(&out->raft_remote_checkpoint);
	return 0;
}

int
txn_persist_all_prepared(struct vclock *out)
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
	return journal_sync(out);
}
