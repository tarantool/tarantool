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
#include "gc.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "coeio_file.h"
#include "diag.h"
#include "errcode.h"
#include "say.h"
#include "vclock.h"
#include "xlog.h"

#include "engine.h"		/* engine_collect_garbage() */
#include "replication.h"	/* INSTANCE_UUID */
#include "wal.h"		/* wal_collect_garbage() */

/** Garbage collection state. */
struct gc_state {
	/** Max signature garbage collection has been called for. */
	int64_t signature;
	/** Uncollected checkpoints, see checkpoint_info. */
	vclockset_t checkpoints;
	/** Snapshot directory. */
	struct xdir snap_dir;
};
static struct gc_state gc;

const struct checkpoint_info *
checkpoint_iterator_next(struct checkpoint_iterator *it)
{
	it->curr = (it->curr == NULL ?
		    vclockset_first(&gc.checkpoints) :
		    vclockset_next(&gc.checkpoints, it->curr));
	return (it->curr == NULL ? NULL :
		container_of(it->curr, struct checkpoint_info, vclock));
}

int
gc_init(const char *snap_dirname)
{
	gc.signature = -1;
	vclockset_new(&gc.checkpoints);
	xdir_create(&gc.snap_dir, snap_dirname, SNAP, &INSTANCE_UUID);

	if (xdir_scan(&gc.snap_dir) < 0)
		goto fail;

	struct vclock *vclock;
	for (vclock = vclockset_first(&gc.snap_dir.index); vclock != NULL;
	     vclock = vclockset_next(&gc.snap_dir.index, vclock)) {
		if (gc_add_checkpoint(vclock) < 0)
			goto fail;
	}
	return 0;
fail:
	gc_free();
	return -1;
}

void
gc_free(void)
{
	struct vclock *vclock = vclockset_first(&gc.checkpoints);
	while (vclock != NULL) {
		struct vclock *next = vclockset_next(&gc.checkpoints, vclock);
		vclockset_remove(&gc.checkpoints, vclock);
		struct checkpoint_info *cpt = container_of(vclock,
				struct checkpoint_info, vclock);
		free(cpt);
		vclock = next;
	}
	xdir_destroy(&gc.snap_dir);
}

int
gc_add_checkpoint(const struct vclock *vclock)
{
	struct vclock *prev = vclockset_last(&gc.checkpoints);
	if (prev != NULL && vclock_compare(vclock, prev) == 0)
		return 0;

	struct checkpoint_info *cpt;
	cpt = (struct checkpoint_info *)malloc(sizeof(*cpt));
	if (cpt == NULL) {
		diag_set(OutOfMemory, sizeof(*cpt),
			 "malloc", "struct checkpoint_info");
		return -1;
	}

	/*
	 * Do not allow to remove the last checkpoint,
	 * because we need it for recovery.
	 */
	cpt->refs = 1;
	vclock_copy(&cpt->vclock, vclock);
	vclockset_insert(&gc.checkpoints, &cpt->vclock);

	if (prev != NULL) {
		assert(vclock_compare(vclock, prev) > 0);
		gc_unref_checkpoint(prev);
	}
	return 0;
}

int64_t
gc_last_checkpoint(struct vclock *vclock)
{
	struct vclock *last = vclockset_last(&gc.checkpoints);
	if (last == NULL)
		return -1;
	vclock_copy(vclock, last);
	return vclock_sum(last);
}

int64_t
gc_ref_last_checkpoint(struct vclock *vclock)
{
	struct vclock *last = vclockset_last(&gc.checkpoints);
	if (last == NULL)
		return -1;
	struct checkpoint_info *cpt = container_of(last,
			struct checkpoint_info, vclock);
	/* The last checkpoint is always pinned. */
	assert(cpt->refs > 0);
	cpt->refs++;
	vclock_copy(vclock, last);
	return vclock_sum(last);
}

void
gc_unref_checkpoint(struct vclock *vclock)
{
	struct vclock *cpt_vclock = vclockset_search(&gc.checkpoints, vclock);
	assert(cpt_vclock != NULL);
	struct checkpoint_info *cpt = container_of(cpt_vclock,
			struct checkpoint_info, vclock);
	assert(cpt->refs > 0);
	cpt->refs--;
	/* Retry gc when a checkpoint is unpinned. */
	if (cpt->refs == 0)
		gc_run(gc.signature);

}

void
gc_run(int64_t signature)
{
	if (gc.signature < signature)
		gc.signature = signature;

	int64_t gc_signature = -1;

	struct vclock *vclock = vclockset_first(&gc.checkpoints);
	while (vclock != NULL) {
		if (vclock_sum(vclock) >= signature)
			break; /* all eligible checkpoints removed */

		struct checkpoint_info *cpt = container_of(vclock,
				struct checkpoint_info, vclock);
		if (cpt->refs > 0)
			break; /* checkpoint still in use */

		const char *filename = xdir_format_filename(&gc.snap_dir,
						vclock_sum(vclock), NONE);
		say_info("removing %s", filename);
		if (coeio_unlink(filename) < 0 && errno != ENOENT) {
			say_syserror("error while removing %s", filename);
			break;
		}

		struct vclock *next = vclockset_next(&gc.checkpoints, vclock);
		vclockset_remove(&gc.checkpoints, vclock);
		free(cpt);
		vclock = next;

		/* Include this checkpoint to gc. */
		gc_signature = (vclock != NULL ?
				vclock_sum(vclock) : signature);
	}

	if (gc_signature >= 0) {
		wal_collect_garbage(gc_signature);
		engine_collect_garbage(gc_signature);
	}
}

int64_t
gc_signature(void)
{
	return gc.signature;
}
