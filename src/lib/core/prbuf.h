#pragma once
/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include <stddef.h>

/** Data entry of prbuf. Public analogue of struct prbuf_record. */
struct prbuf_entry {
	/**
	 * Size of data which is stored in a chunk; it doesn't account
	 * memory overhead per entry.
	 */
	size_t size;
	/** Pointer to stored user data. */
	char *ptr;
};

/** Defined in prbuf.c */
struct prbuf_header;
struct prbuf_record;

/** prbuf public iterator interface. */
struct prbuf_iterator {
	/** Iterator is related to this buffer. */
	struct prbuf *buf;
	/** Iterator is positioned to this entry. */
	struct prbuf_record *current;
};

/**
 * prbuf stands for partitioned ring buffer. It is designed in the way that
 * buffer can be recovered from raw memory.
 */
struct prbuf {
	/**
	 * Header contains all buffer's metadata. Header is stored in scope
	 * of provided for buffer memory. So it's possible to restore all
	 * buffer's data from raw pointer.
	 */
	struct prbuf_header *header;
};

/**
 * Create prbuf entry. Metadata for the buffer is allocated in the
 * provided @mem, so in fact the capacity of the buffer is less than @a size.
 * Destructor for buffer is not provided.
 */
void
prbuf_create(struct prbuf *buf, void *mem, size_t size);

/**
 * Consider @a mem containing valid prbuf structure. Parse metadata and
 * verify the content of the buffer. In case current buffer version does not
 * match given one or buffer contains spoiled entry - return -1.
 */
int
prbuf_open(struct prbuf *buf, void *mem);

/**
 * Maximum record size we can store in the buffer.
 */
size_t
prbuf_max_record_size(struct prbuf *buf);

/**
 * Returns pointer to memory chunk sizeof @a size.
 * Note that without further prbuf_commit() call this function may return
 * the same chunk twice.
 */
void *
prbuf_prepare(struct prbuf *buf, size_t size);

/** Commits the last prepared memory chunk. */
void
prbuf_commit(struct prbuf *buf);

/** Create iterator pointing to the start of the @a buf. */
void
prbuf_iterator_create(struct prbuf *buf, struct prbuf_iterator *iter);

/**
 * Move iterator to the next entry. In case @a iter already positioned to
 * the last entry (to be more precise - the most freshest entry with
 * @a offset_last) or buffer is empty - returns -1.
 */
int
prbuf_iterator_next(struct prbuf_iterator *iter, struct prbuf_entry *res);
