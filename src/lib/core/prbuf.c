/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "prbuf.h"
#include "bit/bit.h"

/**
 * Partitioned ring buffer. Each entry stores size before user data.
 * So the typical buffer looks like:
 * HEADER uint32 DATA uint32 DATA ...
 *
 * We have to store offsets to be able to restore buffer (including
 * all metadata) from raw pointer. Otherwise it is impossible to point
 * out where head/tail are located.
 */
struct PACKED prbuf_header {
	/**
	 * Buffer's data layout can be changed in the future, so for the sake
	 * of proper recovery of the buffer we store its version.
	 */
	uint32_t version;
	/** Total size of buffer (including header). */
	uint32_t size;
	/**
	 * Offset of the oldest entry - it is the first candidate to be
	 * overwritten. Note that in contrast to iterator/entry - this offset
	 * is calculated to the first byte of entry (i.e. header containing
	 * size of entry).
	 */
	uint32_t begin;
	/** Offset of the next byte after the last (written) record. */
	uint32_t end;
};

/**
 * Structure representing record stored in the buffer so it has the same
 * memory layout.
 */
struct PACKED prbuf_record {
	/** Size of data. */
	uint32_t size;
	/** Data. */
	char data[];
};

/**
 * Current prbuf implementation version. Must be bumped in case storage
 * format is changed.
 */
static const uint32_t prbuf_version = 0;

/**
 * prbuf is assumed to store all metadata in little-endian format. Beware
 * when decoding its content. Size overhead per store operation is 4 bytes;
 * moreover not the whole space of the buffer can be used since we do not
 * wrap entry if it doesn't fit til the buffer's end.
 *
 * There are several assumptions regarding the buffer:
 * - The end of the buffer (in the linear sense) contains "end mark";
 * - The minimal size of the buffer is restricted;
 * - Iteration direction - from the oldest entry to the newest.
 */

/** A mark of unused space in the buffer: trash is located after this point. */
static const uint32_t prbuf_end_position = (uint32_t)(-1);

/** Before storing a data in the buffer we place its size (i.e. header). */
static const size_t record_size_overhead = sizeof(struct prbuf_record);

/** Real size of allocation is (data size + record's header). */
static uint32_t
prbuf_record_alloc_size(size_t size)
{
	return size + record_size_overhead;
}

/** Returns pointer to the next byte after end of given buffer. */
static char *
prbuf_linear_end(struct prbuf *buf)
{
	return (char *)buf->header + buf->header->size;
}

/** Returns pointer to the first writable byte of given buffer. */
static char *
prbuf_linear_begin(struct prbuf *buf)
{
	return (char *)buf->header + sizeof(struct prbuf_header);
}

/** Returns pointer to the next byte after the last written record. */
static char *
prbuf_current_raw(struct prbuf *buf)
{
	return prbuf_linear_begin(buf) + buf->header->end;
}

static struct prbuf_record *
prbuf_current_record(struct prbuf *buf)
{
	return (struct prbuf_record *)prbuf_current_raw(buf);
}

/** Returns first (in historical sense) record. */
static struct prbuf_record *
prbuf_first_record(struct prbuf *buf)
{
	assert(buf->header->begin != prbuf_end_position);
	char *first_ptr = prbuf_linear_begin(buf) + buf->header->begin;
	return (struct prbuf_record *)first_ptr;
}

/** Calculate offset from the buffer's start to the given entry. */
static uint32_t
prbuf_record_offset(struct prbuf *buf, struct prbuf_record *record)
{
	assert((char *)record >= prbuf_linear_begin(buf));
	return (uint32_t)((char *)record - prbuf_linear_begin(buf));
}

/** Returns true in case buffer has at least @a size bytes until
 * its linear end.
 */
static bool
prbuf_has_before_end(struct prbuf *buf, uint32_t size)
{
	assert(prbuf_linear_end(buf) >= prbuf_current_raw(buf));
	if ((uint32_t)(prbuf_linear_end(buf) - prbuf_current_raw(buf)) >= size)
		return true;
	return false;
}

static uint32_t
prbuf_size(struct prbuf *buf)
{
	return buf->header->size - sizeof(struct prbuf_header);
}

size_t
prbuf_max_record_size(struct prbuf *buf)
{
	return prbuf_size(buf) - record_size_overhead;
}

void
prbuf_create(struct prbuf *buf, void *mem, size_t size)
{
	assert(size > (sizeof(struct prbuf_header) + record_size_overhead));
#ifndef NDEBUG
	memset(mem, '#', size);
#endif
	buf->header = (struct prbuf_header *)mem;
	buf->header->end = 0;
	buf->header->begin = 0;
	buf->header->version = prbuf_version;
	buf->header->size = size;
}

/** Returns true in case buffer contains no entries. */
static bool
prbuf_is_empty(struct prbuf *buf)
{
	return buf->header->end == 0;
}

/**
 * Assuming @a record pointing to the valid buffer position, move it to the
 * next entry. In case of current entry is broken (contains wrong size or
 * points out of the buffer) function returns -1.
 */
static int
prbuf_next_record(struct prbuf *buf, struct prbuf_record **record)
{
	struct prbuf_record *current = *record;
	*record = NULL;
	if (current->size > prbuf_size(buf))
		return -1;
	if ((char *)current < prbuf_linear_begin(buf))
		return -1;
	if ((char *)current > prbuf_linear_end(buf))
		return -1;
	char *next_record_ptr = current->data + current->size;
	if (next_record_ptr > prbuf_linear_end(buf))
		return -1;

	/* We have reached the end of buffer. */
	if (next_record_ptr == prbuf_current_raw(buf))
		return 0;

	current = (struct prbuf_record *)next_record_ptr;
	if ((uint32_t)(prbuf_linear_end(buf) - next_record_ptr) <
	    record_size_overhead)
		current = (struct prbuf_record *)prbuf_linear_begin(buf);
	else if (current->size == prbuf_end_position)
		current = (struct prbuf_record *)prbuf_linear_begin(buf);
	*record = current;
	return 0;
}

/**
 * Verify that prbuf remains in the consistent state: header is valid and
 * all entries have readable sizes.
 */
static bool
prbuf_check(struct prbuf *buf)
{
	if (buf->header->version != prbuf_version)
		return false;
	if (buf->header->begin > prbuf_size(buf))
		return false;
	if (buf->header->end > prbuf_size(buf))
		return false;
	if (prbuf_is_empty(buf))
		return true;
	struct prbuf_record *current;
	current = prbuf_first_record(buf);
	uint32_t total_size = current->size;
	int rc = 0;
	while ((rc = prbuf_next_record(buf, &current)) == 0 &&
	       current != NULL)
		total_size += current->size;
	return rc == 0 && (total_size <= buf->header->size);
}

int
prbuf_open(struct prbuf *buf, void *mem)
{
	buf->header = (struct prbuf_header *)mem;
	if (!prbuf_check(buf))
		return -1;
	return 0;
}

/**
 * Starting from @a current issue skip @a to_store bytes and return
 * the next record after that.
 */
static struct prbuf_record *
prbuf_skip_record(struct prbuf *buf, struct prbuf_record *current,
		  ssize_t to_store)
{
	assert(to_store > 0);
	assert(to_store <= buf->header->size);

	while (to_store > 0) {
		assert(current->size != prbuf_end_position);
		assert(current->size != 0);
		to_store -= prbuf_record_alloc_size(current->size);
		int rc = prbuf_next_record(buf, &current);
		assert(rc == 0);
		(void)rc;
		if (current == NULL)
			return (struct prbuf_record *)prbuf_linear_begin(buf);
	}
	return current;
}

/** Place special mark at the end of buffer to avoid out-of-bound access. */
static void
prbuf_set_end_position(struct prbuf *buf)
{
	if (prbuf_has_before_end(buf, record_size_overhead))
		prbuf_current_record(buf)->size = prbuf_end_position;
}

/** Store entry's size. */
static void *
prbuf_prepare_record(struct prbuf_record *record, size_t size)
{
	record->size = size;
	return record->data;
}

void *
prbuf_prepare(struct prbuf *buf, size_t size)
{
	assert(size > 0);
	uint32_t alloc_size = prbuf_record_alloc_size(size);
	if (alloc_size > prbuf_size(buf))
		return NULL;
	if (prbuf_has_before_end(buf, alloc_size)) {
		/*
		 * Head points to the byte right after the last
		 * written entry.
		 */
		struct prbuf_record *head = prbuf_current_record(buf);
		if (prbuf_is_empty(buf))
			return prbuf_prepare_record(head, size);
		struct prbuf_record *next = prbuf_first_record(buf);
		uint32_t free_space = next >= head ?
			(char *)next - (char *)head : UINT32_MAX;
		/*
		 * We can safely write entry in case it won't overwrite
		 * anything. Either trash space between two entries
		 * is large enough or the next entry to be overwritten
		 * is located at the start of the buffer.
		 */
		if (free_space < alloc_size) {
			struct prbuf_record *next_overwritten =
				prbuf_skip_record(buf, next,
						  alloc_size - free_space);
			buf->header->begin =
				prbuf_record_offset(buf, next_overwritten);
		}
		return prbuf_prepare_record(head, size);
	}
	/*
	 * Data doesn't fit till the end of buffer, so we'll put the entry
	 * at the buffer's start. Moreover, we should mark the last entry
	 * (in linear sense) to avoid oud-of-bound access while parsing buffer
	 * (after this mark trash is stored so we can't process further).
	 */
	prbuf_set_end_position(buf);
	struct prbuf_record *head =
		(struct prbuf_record *)prbuf_linear_begin(buf);
	struct prbuf_record *next_overwritten =
		prbuf_skip_record(buf, head, alloc_size);
	buf->header->begin = prbuf_record_offset(buf, next_overwritten);
	buf->header->end = 0;
	return prbuf_prepare_record(head, size);
}

void
prbuf_commit(struct prbuf *buf)
{
	if (prbuf_has_before_end(buf, record_size_overhead)) {
		struct prbuf_record *last = prbuf_current_record(buf);
		if (prbuf_has_before_end(buf, last->size)) {
			buf->header->end +=
				prbuf_record_alloc_size(last->size);
			return;
		}
	}
	struct prbuf_record *last =
		(struct prbuf_record *)prbuf_linear_begin(buf);
	buf->header->end = prbuf_record_alloc_size(last->size);
}

void
prbuf_iterator_create(struct prbuf *buf, struct prbuf_iterator *iter)
{
	iter->buf = buf;
	iter->current = NULL;
}

int
prbuf_iterator_next(struct prbuf_iterator *iter, struct prbuf_entry *result)
{
	struct prbuf *buf = iter->buf;
	if (iter->current == NULL) {
		/* Check if the buffer is empty. */
		if (prbuf_is_empty(buf))
			return -1;
		iter->current = prbuf_first_record(buf);
		result->size = iter->current->size;
		result->ptr = iter->current->data;
		assert(iter->current->size < buf->header->size);
		return 0;
	}

	int rc = prbuf_next_record(buf, &iter->current);
	assert(rc == 0);
	(void)rc;
	if (iter->current == NULL)
		return -1;
	result->size = iter->current->size;
	result->ptr = iter->current->data;
	return 0;
}
