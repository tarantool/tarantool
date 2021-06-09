#ifndef TARANTOOL_JOURNAL_H_INCLUDED
#define TARANTOOL_JOURNAL_H_INCLUDED
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
#include <stdint.h>
#include <stdbool.h>
#include "salad/stailq.h"
#include "fiber.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct xrow_header;
struct journal_entry;

typedef void (*journal_write_async_f)(struct journal_entry *entry);

enum {
	/** Entry didn't attempt a journal write. */
	JOURNAL_ENTRY_ERR_UNKNOWN = -1,
	/** Tried to be written, but something happened related to IO. */
	JOURNAL_ENTRY_ERR_IO = -2,
	/**
	 * Rollback because there is a not finished rollback of a previous
	 * entry.
	 */
	JOURNAL_ENTRY_ERR_CASCADE = -3,
	/**
	 * Anchor for the structs built on top of journal entry so as they
	 * could introduce their own unique errors. Set to a big value in
	 * advance.
	 */
	JOURNAL_ENTRY_ERR_MIN = -100,
};

/**
 * Convert a result of a journal entry write to an error installed into the
 * current diag.
 */
void
diag_set_journal_res_detailed(const char *file, unsigned line, int64_t res);

#define diag_set_journal_res(res)						\
	diag_set_journal_res_detailed(__FILE__, __LINE__, res)

/**
 * An entry for an abstract journal.
 * Simply put, a write ahead log request.
 *
 * In case of synchronous replication, this request will travel
 * first to a Raft leader before going to the local WAL.
 */
struct journal_entry {
	/** A helper to include requests into a FIFO queue. */
	struct stailq_entry fifo;
	/**
	 * On success, contains vclock signature of
	 * the committed transaction, on error is -1
	 */
	int64_t res;
	/**
	 * A journal entry completion callback argument.
	 */
	void *complete_data;
	/** Flags that should be set for the last entry row. */
	uint8_t flags;
	/**
	 * Asynchronous write completion function.
	 */
	journal_write_async_f write_async_cb;
	/**
	 * Approximate size of this request when encoded.
	 */
	size_t approx_len;
	/**
	 * The number of rows in the request.
	 */
	int n_rows;
	/**
	 * The rows.
	 */
	struct xrow_header *rows[];
};

struct region;

/**
 * Initialize a new journal entry.
 */
static inline void
journal_entry_create(struct journal_entry *entry, size_t n_rows,
		     size_t approx_len,
		     journal_write_async_f write_async_cb,
		     void *complete_data)
{
	entry->write_async_cb	= write_async_cb;
	entry->complete_data	= complete_data;
	entry->approx_len	= approx_len;
	entry->n_rows		= n_rows;
	entry->res		= JOURNAL_ENTRY_ERR_UNKNOWN;
	entry->flags		= 0;
}

/**
 * Create a new journal entry.
 *
 * @return NULL if out of memory, fiber diagnostics area is set
 */
struct journal_entry *
journal_entry_new(size_t n_rows, struct region *region,
		  journal_write_async_f write_async_cb,
		  void *complete_data);

/**
 * Treat complete_data like a fiber pointer and wake it up when journal write is
 * done.
 */
void
journal_entry_fiber_wakeup_cb(struct journal_entry *entry);

struct journal_queue {
	/** Maximal size of entries enqueued in journal (in bytes). */
	int64_t max_size;
	/** Current approximate size of journal queue. */
	int64_t size;
	/**
	 * The fibers waiting for some space to free in journal queue.
	 * Once some space is freed they will be waken up in the same order they
	 * entered the queue.
	 */
	struct rlist waiters;
	/** How many waiters there are in a queue. */
	int waiter_count;
};

/** A single queue for all journal instances. */
extern struct journal_queue journal_queue;

/**
 * An API for an abstract journal for all transactions of this
 * instance, as well as for multiple instances in case of
 * synchronous replication.
 */
struct journal {
	/** Asynchronous write */
	int (*write_async)(struct journal *journal,
			   struct journal_entry *entry);

	/** Synchronous write */
	int (*write)(struct journal *journal,
		     struct journal_entry *entry);
};

/** Wake the journal queue up. */
void
journal_queue_wakeup(void);

/**
 * Check whether anyone is waiting for the journal queue to empty. If there are
 * other waiters we must go after them to preserve write order.
 */
static inline bool
journal_queue_has_waiters(void)
{
	return journal_queue.waiter_count != 0;
}

/**
 * Check whether any of the queue size limits is reached.
 * If the queue is full, we must wait for some of the entries to be written
 * before proceeding with a new asynchronous write request.
 */
static inline bool
journal_queue_is_full(void)
{
	return journal_queue.size >= journal_queue.max_size;
}

/** Yield until there's some space in the journal queue. */
void
journal_queue_wait(void);

/** Empty the queue by waking everyone in it up and put self to queue tail. */
void
journal_queue_flush(void);

/** Set maximal journal queue size in bytes. */
static inline void
journal_queue_set_max_size(int64_t size)
{
	journal_queue.max_size = size;
	journal_queue_wakeup();
}

/** Increase queue size on a new write request. */
static inline void
journal_queue_on_append(const struct journal_entry *entry)
{
	journal_queue.size += entry->approx_len;
}

/** Decrease queue size once write request is complete. */
static inline void
journal_queue_on_complete(const struct journal_entry *entry)
{
	journal_queue.size -= entry->approx_len;
	assert(journal_queue.size >= 0);
}

/**
 * Complete asynchronous write.
 */
static inline void
journal_async_complete(struct journal_entry *entry)
{
	assert(entry->write_async_cb != NULL);

	journal_queue_on_complete(entry);

	entry->write_async_cb(entry);
}

/**
 * Depending on the step of recovery and instance configuration
 * points at a concrete implementation of the journal.
 */
extern struct journal *current_journal;

/**
 * Write a single entry to the journal in synchronous way.
 *
 * @return 0 if write was processed by a backend or -1 in case of an error.
 */
static inline int
journal_write(struct journal_entry *entry)
{
	journal_queue_flush();
	journal_queue_on_append(entry);

	return current_journal->write(current_journal, entry);
}

/**
 * Queue a single entry to the journal in asynchronous way.
 *
 * @return 0 if write was queued to a backend or -1 in case of an error.
 */
static inline int
journal_write_try_async(struct journal_entry *entry)
{
	journal_queue_wait();
	journal_queue_on_append(entry);

	return current_journal->write_async(current_journal, entry);
}

/**
 * Change the current implementation of the journaling API.
 * Happens during life cycle of an instance:
 *
 * 1. When recovering a snapshot, the log sequence numbers
 *    don't matter and are not used, transactions
 *    can be recovered in any order. A stub API simply
 *    returns 0 for every write request.
 *
 * 2. When recovering from the local write ahead
 * log, the LSN of each entry is already known. In this case,
 * the journal API should simply return the existing
 * log sequence numbers of records and do nothing else.
 *
 * 2. After recovery, in wal_mode = NONE, the implementation
 * fakes a WAL by using a simple counter to provide
 * log sequence numbers.
 *
 * 3. If the write ahead log is on, the WAL thread
 * is issuing the log sequence numbers.
 */
static inline void
journal_set(struct journal *new_journal)
{
	current_journal = new_journal;
}

static inline void
journal_create(struct journal *journal,
	       int (*write_async)(struct journal *journal,
				  struct journal_entry *entry),
	       int (*write)(struct journal *journal,
			    struct journal_entry *entry))
{
	journal->write_async	= write_async;
	journal->write		= write;
}

static inline bool
journal_is_initialized(struct journal *journal)
{
	return journal->write != NULL;
}

#if defined(__cplusplus)
} /* extern "C" */

#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_JOURNAL_H_INCLUDED */
