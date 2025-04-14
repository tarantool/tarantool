#pragma once

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include "tuple.h"
#include "read_view.h"
#include "core/tt_uuid.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct memtx_sort_data_writer;
struct memtx_sort_data_reader;

/*
 * Writing operations.
 */

/** Create a new sort data file writer. */
struct memtx_sort_data_writer *
memtx_sort_data_writer_new(struct read_view *rv);

/** Delete the sort data file writer. */
void
memtx_sort_data_writer_delete(struct memtx_sort_data_writer *writer);

/** Create the sort data file and partially initialize headers. */
int
memtx_sort_data_writer_create_file(struct memtx_sort_data_writer *writer,
				   const char *dirname, struct vclock *vclock,
				   const struct tt_uuid *instance_uuid);

/** Close the sort data file and finish file headers. */
int
memtx_sort_data_writer_close_file(struct memtx_sort_data_writer *writer);

/** Prepare to write the PK sort data. */
int
memtx_sort_data_writer_begin_pk(struct memtx_sort_data_writer *writer,
				uint32_t space_id);

/** Write a PK tuple into the file. */
int
memtx_sort_data_writer_put_pk_tuple(struct memtx_sort_data_writer *writer,
				    struct tuple *tuple);

/** Finish writing the PK data and update related file header fields. */
int
memtx_sort_data_writer_commit_pk(struct memtx_sort_data_writer *writer);

/** Prepare to write the sort data of an index. */
int
memtx_sort_data_writer_begin(struct memtx_sort_data_writer *writer,
			     uint32_t space_id, uint32_t index_id,
			     bool *have_data);

/** Write the sort data into the file (`count` must specify tuple count). */
int
memtx_sort_data_writer_put(struct memtx_sort_data_writer *writer,
			   void *data, size_t size, size_t count);

/** Finish writing the index data and update related file header fields. */
int
memtx_sort_data_writer_commit(struct memtx_sort_data_writer *writer);

/** Materialize the sort data file (remove the .inprogress suffix). */
int
memtx_sort_data_writer_materialize(struct memtx_sort_data_writer *writer);

/** Remove the in-progress or completed sort data file. */
void
memtx_sort_data_writer_discard(struct memtx_sort_data_writer *writer);

/**
 * Reading operations.
 */

/** Create a new sort data reader. */
struct memtx_sort_data_reader *
memtx_sort_data_reader_new(const char *dirname, const struct vclock *vclock,
			   const struct tt_uuid *instance_uuid);

/** Delete the sort data reader. */
void
memtx_sort_data_reader_delete(struct memtx_sort_data_reader *reader);

/** Introduce a new PK tuple for the given space. */
int
memtx_sort_data_reader_pk_add_tuple(struct memtx_sort_data_reader *reader,
				    uint32_t space_id, bool is_first,
				    struct tuple *tuple);

/** Check if all PK sort data had been read. */
int
memtx_sort_data_reader_pk_check(struct memtx_sort_data_reader *reader,
				uint32_t space_id);

/** Prepare to read sort data of index if available. */
int
memtx_sort_data_reader_seek(struct memtx_sort_data_reader *reader,
			    uint32_t space_id, uint32_t index_id,
			    bool *index_included);

/** Get the binary size of the sort data available. */
size_t
memtx_sort_data_reader_get_size(struct memtx_sort_data_reader *reader);

/** Read the sort data available. */
int
memtx_sort_data_reader_get(struct memtx_sort_data_reader *reader, void *buffer);

/** Update a tuple pointer from the sort data. */
struct tuple *
memtx_sort_data_reader_resolve_tuple(struct memtx_sort_data_reader *reader,
				     struct tuple *old_ptr);

/*
 * Garbage collection.
 */

/** Garbage-collect the sort data file of the given signature. */
void
memtx_sort_data_collect(const char *dirname, int64_t signature);

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */
