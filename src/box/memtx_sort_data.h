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

struct memtx_sort_data;
struct memtx_sort_data_reader;

/*
 * Writing operations.
 */

/** Create a new sort data file writer. */
struct memtx_sort_data *
memtx_sort_data_writer_new(struct read_view *rv, const char *dirname,
			   struct tt_uuid *instance_uuid);

/** Delete the sort data file writer. */
void
memtx_sort_data_writer_delete(struct memtx_sort_data *msd);

/** Create the sort data file and partially initialize headers. */
int
memtx_sort_data_writer_create_file(struct memtx_sort_data *msd,
				   struct vclock *vclock,
				   const char *filename);

/** Close the sort data file and finish file headers. */
int
memtx_sort_data_writer_close_file(struct memtx_sort_data *msd);

/** Prepare to write the PK sort data. */
int
memtx_sort_data_writer_begin_pk(struct memtx_sort_data *msd, uint32_t space_id);

/** Write a PK tuple into the file. */
int
memtx_sort_data_writer_put_pk_tuple(struct memtx_sort_data *msd,
				    struct tuple *tuple);

/** Finish writing the PK data and update related file header fields. */
int
memtx_sort_data_writer_commit_pk(struct memtx_sort_data *msd);

/** Prepare to write the sort data of an index. */
int
memtx_sort_data_writer_begin(struct memtx_sort_data *msd,
			     uint32_t space_id, uint32_t index_id,
			     bool *have_data);

/** Write the sort data into the file (`count` must specify tuple count). */
int
memtx_sort_data_writer_put(struct memtx_sort_data *msd,
			   void *data, size_t size, size_t count);

/** Finish writing the index data and update related file header fields. */
int
memtx_sort_data_writer_commit(struct memtx_sort_data *msd);

/**
 * Reading operations.
 */

/** Create a new sort data reader. */
struct memtx_sort_data_reader *
memtx_sort_data_reader_new(const char *dirname, const struct vclock *vclock,
			   const struct tt_uuid *instance_uuid);

/** Delete the sort data reader. */
void
memtx_sort_data_reader_delete(struct memtx_sort_data_reader *msdr);

/** Introduce a new PK tuple for the given space. */
int
memtx_sort_data_reader_pk_add_tuple(struct memtx_sort_data_reader *msdr,
				    uint32_t space_id, bool is_first,
				    struct tuple *tuple);

/** Check if all PK sort data had been read. */
int
memtx_sort_data_reader_pk_check(struct memtx_sort_data_reader *msdr,
				uint32_t space_id);

/** Prepare to read sort data of index if available. */
int
memtx_sort_data_reader_seek(struct memtx_sort_data_reader *msdr,
			    uint32_t space_id, uint32_t index_id,
			    bool *index_included);

/** Get the binary size of the sort data available. */
size_t
memtx_sort_data_reader_get_size(struct memtx_sort_data_reader *msdr);

/** Read the sort data available. */
int
memtx_sort_data_reader_get(struct memtx_sort_data_reader *msdr, void *buffer);

/** Update a tuple pointer from the sort data. */
struct tuple *
memtx_sort_data_reader_resolve_tuple(struct memtx_sort_data_reader *msdr,
				     struct tuple *old_ptr);

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */
