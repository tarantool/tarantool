/*
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
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
#include "box.h"
#include "raft.h"

struct raft box_raft_global = {
	/*
	 * Set an invalid state to validate in runtime the global raft node is
	 * not used before initialization.
	 */
	.state = 0,
};

/**
 * A trigger executed each time the Raft state machine updates any
 * of its visible attributes.
 */
static struct trigger box_raft_on_update;

static int
box_raft_on_update_f(struct trigger *trigger, void *event)
{
	(void)trigger;
	struct raft *raft = (struct raft *)event;
	assert(raft == box_raft());
	if (raft->state != RAFT_STATE_LEADER)
		return 0;
	/*
	 * If the node became a leader, it means it will ignore all records from
	 * all the other nodes, and won't get late CONFIRM messages anyway. Can
	 * clear the queue without waiting for confirmations.
	 */
	box_clear_synchro_queue(false);
	return 0;
}

void
box_raft_init(void)
{
	raft_create(&box_raft_global);
	trigger_create(&box_raft_on_update, box_raft_on_update_f, NULL, NULL);
	raft_on_update(box_raft(), &box_raft_on_update);
}

void
box_raft_free(void)
{
	struct raft *raft = box_raft();
	/*
	 * Can't join the fiber, because the event loop is stopped already, and
	 * yields are not allowed.
	 */
	raft->worker = NULL;
	raft_destroy(raft);
	/*
	 * Invalidate so as box_raft() would fail if any usage attempt happens.
	 */
	raft->state = 0;
}
