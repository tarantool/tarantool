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

#include "coio_file.h"
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

void
checkpoint_ref(struct checkpoint_info *cpt)
{
	assert(cpt->refs >= 0);
	cpt->refs++;
}

void
checkpoint_unref(struct checkpoint_info *cpt)
{
	assert(cpt->refs > 0);
	cpt->refs--;
	/* Retry gc when a checkpoint is unpinned. */
	if (cpt->refs == 0)
		gc_run(gc.signature);
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
	struct checkpoint_info *last = gc_last_checkpoint();
	if (last != NULL && vclock_compare(vclock, &last->vclock) == 0)
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

	if (last != NULL) {
		assert(vclock_compare(vclock, &last->vclock) > 0);
		checkpoint_unref(last);
	}
	return 0;
}

struct checkpoint_info *
gc_last_checkpoint(void)
{
	struct vclock *last = vclockset_last(&gc.checkpoints);
	return (last == NULL ? NULL :
		container_of(last, struct checkpoint_info, vclock));
}

struct checkpoint_info *
gc_lookup_checkpoint(struct vclock *vclock)
{
	struct vclock *found = vclockset_psearch(&gc.checkpoints, vclock);
	return (found == NULL ? NULL :
		container_of(found, struct checkpoint_info, vclock));
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
		if (coio_unlink(filename) < 0 && errno != ENOENT) {
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
