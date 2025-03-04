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
#include "journal.h"
#include <small/region.h>
#include <diag.h>
#include "error.h"
#include "xrow.h"

struct journal *current_journal = NULL;

struct journal_queue journal_queue = {
	.max_size = 16 * 1024 * 1024, /* 16 megabytes */
	.size = 0,
	.requests = STAILQ_INITIALIZER(journal_queue.requests),
};

void
diag_set_journal_res_detailed(const char *file, unsigned line, int64_t res)
{
	switch(res) {
	case JOURNAL_ENTRY_ERR_IO:
		diag_set_detailed(file, line, ClientError, ER_WAL_IO);
		return;
	case JOURNAL_ENTRY_ERR_CASCADE:
		diag_set_detailed(file, line, ClientError, ER_CASCADE_ROLLBACK);
		return;
	}
	panic("Journal result code %lld can't be converted to an error "
	      "at %s:%u", (long long)res, file, line);
}

struct journal_entry *
journal_entry_new(size_t n_rows, struct region *region,
		  journal_write_async_f write_async_cb,
		  void *complete_data)
{
	struct journal_entry *entry;

	size_t size = (sizeof(struct journal_entry) +
		       sizeof(entry->rows[0]) * n_rows);

	entry = region_aligned_alloc(region, size,
				     alignof(struct journal_entry));
	if (entry == NULL) {
		diag_set(OutOfMemory, size, "region", "struct journal_entry");
		return NULL;
	}

	journal_entry_create(entry, n_rows, 0, write_async_cb,
			     complete_data);
	return entry;
}

void
journal_entry_fiber_wakeup_cb(struct journal_entry *entry)
{
	fiber_wakeup(entry->complete_data);
}

void
journal_queue_wakeup(void)
{
	if (!stailq_empty(&journal_queue.requests) &&
	    !journal_queue_is_full()) {
		struct journal_entry *req =
				stailq_first_entry(&journal_queue.requests,
						   typeof(*req), fifo);
		fiber_wakeup(req->fiber);
	}
}

int
journal_queue_wait(struct journal_entry *entry)
{
	if (!journal_queue_is_full() &&
	    stailq_empty(&journal_queue.requests))
		return 0;
	int rc = -1;
	struct journal_entry *prev_entry =
			stailq_last_entry(&journal_queue.requests,
					  typeof(*prev_entry), fifo);
	stailq_add_tail_entry(&journal_queue.requests, entry, fifo);
	assert(entry->fiber == NULL);
	entry->fiber = fiber();
	fiber_yield();
	if (entry->is_complete) {
		/* Already rolled back on cascade rollback. */
		diag_set_journal_res(entry->res);
	} else if (fiber_is_cancelled()) {
		struct stailq rollback;
		stailq_cut_tail(&journal_queue.requests, &prev_entry->fifo,
				&rollback);
		/* Pop this request. */
		VERIFY(stailq_shift_entry(&rollback,
					  typeof(*entry), fifo) == entry);
		stailq_reverse(&rollback);
		/* Cascade rollback of newer requests. */
		struct journal_entry *req;
		stailq_foreach_entry(req, &rollback, fifo) {
			req->res = JOURNAL_ENTRY_ERR_CASCADE;
			req->is_complete = true;
			req->write_async_cb(req);
		}
		/* Rollback this request. */
		entry->res = JOURNAL_ENTRY_ERR_CANCELLED;
		entry->is_complete = true;
		entry->write_async_cb(entry);
		diag_set(FiberIsCancelled);
	} else {
		/* There is a space in queue to handle this request. */
		VERIFY(stailq_shift_entry(&journal_queue.requests,
					  typeof(*entry), fifo) == entry);
		journal_queue_wakeup();
		rc = 0;
	}
	entry->fiber = NULL;
	return rc;
}

void
journal_queue_flush(void)
{
	if (stailq_empty(&journal_queue.requests))
		return;
	struct journal_entry *req;
	stailq_foreach_entry(req, &journal_queue.requests, fifo)
		fiber_wakeup(req->fiber);
	/* Schedule after all fibers waiting in journal queue. */
	fiber_sleep(0);
}

int
journal_write_row(struct xrow_header *row)
{
	char buf[sizeof(struct journal_entry) + sizeof(struct xrow_header *)];
	struct journal_entry *entry = (struct journal_entry *)buf;
	entry->rows[0] = row;
	journal_entry_create(entry, 1, xrow_approx_len(row),
			     journal_entry_fiber_wakeup_cb, fiber());

	if (journal_write(entry) != 0)
		return -1;
	if (entry->res < 0) {
		diag_set_journal_res(entry->res);
		return -1;
	}
	return 0;
}
