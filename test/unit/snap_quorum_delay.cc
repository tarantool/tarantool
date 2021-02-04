/*
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and iproto forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in iproto form must reproduce the above
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
#include "unit.h"
#include "gc.h"
#include "memory.h"
#include "txn.h"
#include "txn_limbo.h"

/**
 * This test is only about delay in snapshot machinery (needed
 * for qsync replication). It doesn't test the snapshot
 * machinery, txn_limbo or something else and uses some tricks
 * around txn_limbo.
 * The logic of the test is as folows:
 * In fiber_1 ("txn_fiber"):
 *	- start a transaction.
 *	- push the transaction to the limbo.
 *	- start wait confirm (yield).
 * In fiber_2 ("main"):
 *	- do a snapshot.
 *	- start wait while the last transaction
 *	  from the limbo will be completed.
 * In fiber_3 ("confirm_fiber"):
 *	- confirm the transaction (remove the transaction from
 *				   the limbo and wakeup fiber_1).
 * In fiber_1 ("txn_fiber"):
 *	- confirm/rollback/hung the transaction.
 * In fiber_2 ("main"):
 *	- check_results
 */

extern int replication_synchro_quorum;
extern double replication_synchro_timeout;

namespace /* local symbols */ {

int test_result;

/**
 * Variations of a transaction completion.
 */
enum process_type {
	TXN_PROCESS_COMMIT,
	TXN_PROCESS_ROLLBACK,
	TXN_PROCESS_TIMEOUT
};

/**
 * Some fake values needed for work with the limbo
 * (to push a transaction to the limbo and simulate confirm).
 */
const int fake_lsn = 1;
extern "C" int instance_id;
const int relay_id = 2;

int
trg_cb(struct trigger *trigger, void *event)
{
	(void)event;
	bool *check_trg = (bool *)trigger->data;
	*check_trg = true;
	return 0;
}

int
txn_process_func(va_list ap)
{
	bool *check_trg = va_arg(ap, bool *);
	enum process_type process_type = (enum process_type)va_arg(ap, int);
	struct txn *txn = txn_begin();
	txn->fiber = fiber();
	/* Simulate a sync transaction. */
	txn_set_flags(txn, TXN_WAIT_SYNC | TXN_WAIT_ACK);
	/*
	 * The true way to push the transaction to limbo is to call
	 * txn_commit() for sync transaction. But, if txn_commit()
	 * will be called now, the transaction will not be pushed to
	 * the limbo because this is the case txn_commit_nop().
	 * Instead, we push the transaction to the limbo manually
	 * and call txn_commit (or another) later.
	 */
	struct txn_limbo_entry *entry = txn_limbo_append(&txn_limbo,
							 instance_id, txn);
	/*
	 * The trigger is used to verify that the transaction has been
	 * completed.
	 */
	struct trigger trg;
	trigger_create(&trg, trg_cb, check_trg, NULL);

	switch (process_type) {
	case TXN_PROCESS_COMMIT:
		txn_on_commit(txn, &trg);
		break;
	case TXN_PROCESS_ROLLBACK:
		txn_on_rollback(txn, &trg);
		break;
	case TXN_PROCESS_TIMEOUT:
		break;
	default:
		unreachable();
	}

	txn_limbo_assign_local_lsn(&txn_limbo, entry, fake_lsn);
	txn_limbo_ack(&txn_limbo, txn_limbo.owner_id, fake_lsn);
	txn_limbo_wait_complete(&txn_limbo, entry);

	switch (process_type) {
	case TXN_PROCESS_COMMIT:
		txn_commit(txn);
		break;
	case TXN_PROCESS_ROLLBACK:
		txn_rollback(txn);
		break;
	case TXN_PROCESS_TIMEOUT:
		fiber_yield();
		break;
	default:
		unreachable();
	}
	return 0;
}

int
txn_confirm_func(va_list ap)
{
	/*
	 * We shouldn't react on gc_wait_cleanup() yield
	 * inside gc_checkpoint().
	 */
	fiber_sleep(0);
	txn_limbo_ack(&txn_limbo, relay_id, fake_lsn);
	return 0;
}


void
test_snap_delay_common(enum process_type process_type)
{
	plan(1);

	/*
	 * We need to clear the limbo vclock before the new test
	 * variation because the same fake lsn will be used.
	 */
	vclock_clear(&txn_limbo.vclock);
	vclock_create(&txn_limbo.vclock);

	bool check_trg = false;
	struct fiber *txn_fiber = fiber_new("txn_fiber", txn_process_func);
	fiber_start(txn_fiber, &check_trg, process_type);

	struct fiber *confirm_entry = fiber_new("confirm_fiber",
						txn_confirm_func);
	fiber_wakeup(confirm_entry);

	switch (process_type) {
	case TXN_PROCESS_COMMIT:
		ok(gc_checkpoint() == 0 && check_trg,
		   "check snapshot delay confirm");
		break;
	case TXN_PROCESS_ROLLBACK:
		ok(gc_checkpoint() == -1 && check_trg,
		   "check snapshot delay rollback");
		break;
	case TXN_PROCESS_TIMEOUT:
		ok(gc_checkpoint() == -1, "check snapshot delay timeout");
		/* join the "hung" fiber */
		fiber_set_joinable(txn_fiber, true);
		fiber_cancel(txn_fiber);
		fiber_join(txn_fiber);
		break;
	default:
		unreachable();
	}
	check_plan();
}

void
test_snap_delay_timeout()
{
	/* Set the timeout to a small value for the test. */
	replication_synchro_timeout = 0.01;
	test_snap_delay_common(TXN_PROCESS_TIMEOUT);
}

int
test_snap_delay(va_list ap)
{
	header();
	plan(3);
	(void)ap;
	replication_synchro_quorum = 2;

	test_snap_delay_common(TXN_PROCESS_COMMIT);
	test_snap_delay_common(TXN_PROCESS_ROLLBACK);
	test_snap_delay_timeout();

	ev_break(loop(), EVBREAK_ALL);
	footer();
	test_result = check_plan();
	return 0;
}
} /* end of anonymous namespace */

int
main(void)
{
	memory_init();
	fiber_init(fiber_c_invoke);
	gc_init();
	txn_limbo_init();
	instance_id = 1;

	struct fiber *main_fiber = fiber_new("main", test_snap_delay);
	assert(main_fiber != NULL);
	fiber_wakeup(main_fiber);
	ev_run(loop(), 0);

	gc_free();
	fiber_free();
	memory_free();
	return test_result;
}
