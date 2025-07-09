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
	if (journal_sync(&out->journal_vclock) != 0)
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
