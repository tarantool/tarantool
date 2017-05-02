#ifndef INCLUDES_TARANTOOL_BOX_VY_STMT_ITERATOR_H
#define INCLUDES_TARANTOOL_BOX_VY_STMT_ITERATOR_H
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

#include <trivia/util.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct vy_stmt_iterator;
struct tuple;

typedef NODISCARD int
(*vy_iterator_next_key_f)(struct vy_stmt_iterator *virt_iterator,
			  struct tuple **ret, bool *stop);
typedef NODISCARD int
(*vy_iterator_next_lsn_f)(struct vy_stmt_iterator *virt_iterator,
			  struct tuple **ret);
/**
 * The restore() function moves an iterator to the specified
 * statement (@arg last_stmt) and returns the new statement via @arg ret.
 * In addition two cases are possible either the position of the iterator
 * has been changed after the restoration or it hasn't.
 *
 * 1) The position wasn't changed. This case appears if the iterator is moved
 *    to the statement that equals to the old statement by key and less
 *    or equal by LSN.
 *
 *    Example of the unchanged position:
 *    ┃     ...      ┃                      ┃     ...      ┃
 *    ┃ k2, lsn = 10 ┣▶ read_iterator       ┃ k3, lsn = 20 ┃
 *    ┃ k2, lsn = 9  ┃  position            ┃              ┃
 *    ┃ k2, lsn = 8  ┃                      ┃ k2, lsn = 8  ┣▶ read_iterator
 *    ┃              ┃   restoration ▶▶     ┃              ┃  position - the
 *    ┃ k1, lsn = 10 ┃                      ┃ k1, lsn = 10 ┃  same key and the
 *    ┃ k1, lsn = 9  ┃                      ┃ k1, lsn = 9  ┃  older LSN
 *    ┃     ...      ┃                      ┃     ...      ┃
 *
 * 2) Otherwise the position was changed and points on a statement with another
 *    key or with the same key but the bigger LSN.
 *
 *    Example of the changed position:
 *    ┃     ...      ┃                      ┃     ...      ┃
 *    ┃ k2, lsn = 10 ┣▶ read_iterator       ┃ k2, lsn = 11 ┣▶ read_iterator
 *    ┃ k2, lsn = 9  ┃  position            ┃ k2, lsn = 10 ┃  position - found
 *    ┃ k2, lsn = 8  ┃                      ┃ k2, lsn = 9  ┃  the newer LSN
 *    ┃              ┃   restoration ▶▶     ┃ k2, lsn = 8  ┃
 *    ┃ k1, lsn = 10 ┃                      ┃              ┃
 *    ┃ k1, lsn = 9  ┃                      ┃ k1, lsn = 10 ┃
 *    ┃     ...      ┃                      ┃     ...      ┃
 *
 *    Another example:
 *    ┃     ...      ┃                      ┃              ┃
 *    ┃ k3, lsn = 20 ┃                      ┃     ...      ┃
 *    ┃              ┃                      ┃ k3, lsn = 10 ┃
 *    ┃ k2, lsn = 8  ┣▶ read_iterator       ┃ k3, lsn = 9  ┃
 *    ┃              ┃  position            ┃ k3, lsn = 8  ┣▶ read_iterator
 *    ┃ k1, lsn = 10 ┃                      ┃              ┃  position - k2 was
 *    ┃ k1, lsn = 9  ┃   restoration ▶▶     ┃ k1, lsn = 10 ┃  not found, so go
 *    ┃     ...      ┃                      ┃     ...      ┃  to the next key
 */
typedef NODISCARD int
(*vy_iterator_restore_f)(struct vy_stmt_iterator *virt_iterator,
			 const struct tuple *last_stmt, struct tuple **ret,
			 bool *stop);

typedef void
(*vy_iterator_close_f)(struct vy_stmt_iterator *virt_iterator);

struct vy_stmt_iterator_iface {
	vy_iterator_next_key_f next_key;
	vy_iterator_next_lsn_f next_lsn;
	vy_iterator_restore_f restore;
	vy_iterator_close_f cleanup;
	vy_iterator_close_f close;
};

/**
 * Common interface for iterator over run, mem, etc.
 */
struct vy_stmt_iterator {
	const struct vy_stmt_iterator_iface *iface;
};

/**
 * Usage statisctics of one particular type of iterator
 */
struct vy_iterator_stat {
	/* Number of binary searches performed */
	size_t lookup_count;
	/* Number of sequential iterations */
	size_t step_count;
	/* Number of searches avoided using bloom filter */
	size_t bloom_reflections;
};


/**
 * The stream is a very simple iterator (generally over a mem or a run)
 * that output all the tuples on increasing order.
 */
struct vy_stmt_stream;

/**
 * Get next tuple from a stream.
 */
typedef NODISCARD int
(*vy_stream_next_f)(struct vy_stmt_stream *virt_stream, struct tuple **ret);

/**
 * Close the stream.
 */
typedef void
(*vy_stream_close_f)(struct vy_stmt_stream *virt_stream);

/**
 * The interface description for streams over run and mem.
 */
struct vy_stmt_stream_iface {
	vy_stream_next_f next;
	vy_stream_close_f close;
};

/**
 * Common interface for streams over run and mem.
 */
struct vy_stmt_stream {
	const struct vy_stmt_stream_iface *iface;
};


/** The state of the database the cursor should be looking at. */
struct vy_read_view {
	/**
	 * Consistent read view LSN. Originally read-only transactions
	 * receive a read view lsn upon creation and do not see further
	 * changes.
	 * Other transactions are expected to be read-write and
	 * have vlsn == INT64_MAX to read newest data. Once a value read
	 * by such a transaction (T) is overwritten by another
	 * commiting transaction, T permanently goes to read view that does
	 * not see this change.
	 * If T does not have any write statements by the commit time it will
	 * be committed successfully, or aborted as conflicted otherwise.
	 */
	int64_t vlsn;
	/** The link in read_views of the TX manager */
	struct rlist in_read_views;
	/**
	 * The number of references to this read view. The global
	 * read view has zero refs, we don't do reference
	 * count it as it is missing from read_views list.
	 */
	int refs;
	/**
	 * Is set to true when the read view which includes
	 * a prepared but not committed transaction, is
	 * compromised by a cascading rollback.
	 */
	bool is_aborted;
};


#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_STMT_ITERATOR_H */
