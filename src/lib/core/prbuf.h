#pragma once
/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#include "small/ibuf.h"

/** Data entry of prbuf. */
struct prbuf_entry {
	/** Size of the data pointed by ptr field. */
	size_t size;
	/** Pointer to the entry data. */
	char *ptr;
};

/** Defined in prbuf.c */
struct prbuf_header;
struct prbuf_record;

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

struct prbuf_reader {
	/**
	 * File with buffer data. It is not owned by the reader.
	 */
	int fd;
	/**
	 * Offset of the beginning of the buffer data in the file.
	 */
	off_t offset;
	/**
	 * Header is read in lazy fashion. We keep whether we read header
	 * or not in this flag.
	 */
	bool header_read;
	/**
	 * File offset of next record to be read in case EOF is not reached
	 * yet.
	 */
	off_t pos;
	/**
	 * File is read ahead into `buf `and here we store the position we
	 * read up to last time. Equals to `pos` if we don't read anything yet
	 * or reset previous readings.
	 */
	off_t read_pos;
	/**
	 * Number of buffer bytes to be processed. It is size of unread payload
	 * bytes. It also includes size of unused area at the end of the buffer
	 * if we not skip it yet.
	 *
	 * If it is 0 then we read all the data.
	 */
	size_t unread_size;
	/**
	 * File offset of the beginning of the data area.
	 * Data area is the area after header till the end of the buffer.
	 */
	off_t data_begin;
	/**
	 * File offset of the end of the data area.
	 */
	off_t data_end;
	/*
	 * Buffer to store data read from file.
	 */
	struct ibuf buf;
};

/**
 * Initialize buffer reader.
 *
 * fd - file descriptor to read buffer from. It is not owned by the reader.
 * offset - starting offset of buffer in file.
 */
void
prbuf_reader_create(struct prbuf_reader *reader, int fd, off_t offset);

/**
 * Read the next record into entry argument. If there are no more records
 * then returned record will be a terminator (EOF, ptr == NULL && size == 0).
 *
 * After EOF the function can be called again and will return EOF.
 *
 * After failure reader is invalid and can only be closed.
 *
 * Return:
 *   0 - success
 *  -1 - failure (diag is set)
 */
int
prbuf_reader_next(struct prbuf_reader *reader,
		  struct prbuf_entry *entry);

/**
 * Free reader resources. Should be called only once.
 */
void
prbuf_reader_destroy(struct prbuf_reader *reader);
