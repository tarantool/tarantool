/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2025, Tarantool AUTHORS, please see AUTHORS file.
 */
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

/**
 * The tarantool recovery process involves loading data from snapshots or xlogs
 * and building primary and secondary indexes. When loading a snapshot (initial
 * recovery), the data is loaded in the primary key order. But the secondary
 * keys have to be sorted to be built appropriately. This used to be done using
 * a regular multithreaded qsort. But it can be slow or consume a lot of CPU
 * resources (cfg.memtx_sort_threads).
 *
 * In order to solve the problem, a new O(n) SK sort algorithm introduced. The
 * idea is simple: let's just save the tuple order on the persistent storage
 * along with the snapshot and sort secondary keys using this data. This way
 * we'll be able to reduce the sort complexity from O(n*log(n)) to O(n). The
 * file containing the index order data is called "sort data file".
 *
 * The information consists of two types of data: primary key tuple pointers
 * and secondary key raw data (including hints if enabled in the index). The
 * algorithm is the following:
 *
 * 1. When saving the snapshot we dump all PK tuple pointers in index order
 *    into the sort data file:
 *    - memtx_sort_data_writer_begin
 *    - memtx_sort_data_writer_put_tuple
 *    - memtx_sort_data_writer_commit
 *
 *    Then we dump all secondary key data (tuple pointers, optionally including
 *    hints), so we could directly build the index from it:
 *    - memtx_sort_data_writer_begin
 *    - memtx_sort_data_writer_put
 *    - memtx_sort_data_writer_commit
 *
 * 2. When loading the snapshot data into a primary key, we map tuple pointers,
 *    written in the sort data file to the newly allocated ones. So any tuple
 *    pointer from the instance of Tarantool that created the snapshot is mapped
 *    to the corresponding tuple in the recovering one (so we have the old2new
 *    tuple pointer map filled):
 *    - memtx_sort_data_reader_pk_add_tuple
 *
 *    Then, when building a secondary key, we read the SK data:
 *    - memtx_sort_data_reader_seek
 *    - memtx_sort_data_reader_get_size
 *    - memtx_sort_data_reader_get
 *
 *    Translate old tuple pointers to new ones using the map filled previously:
 *    - memtx_sort_data_reader_resolve_tuple
 *
 *    And build the secondary index using the updated data.
 *
 * So instead of performing the SK sort using an O(n*log(n)) algorithm, we just
 * read index data for O(n) and then actualize each tuple pointer in the data
 * for antother O(n). As a result, we have the O(n) complexity, meaning that the
 * time consumption of the algorithm grows linearly with the tuple count.
 *
 * In practice, the algorithm executed on a single core can run as fast as the
 * multihreaded sort using up to 20 (measured) cores depending on the hardware,
 * but it will require extra memory for the old2new tuple pointer map and more
 * disk reads for the sort data.
 *
 * Regarding API, there're also service functions for file management in writer:
 * - memtx_sort_data_writer_create_file
 * - memtx_sort_data_writer_close_file
 * - memtx_sort_data_writer_materialize
 * - memtx_sort_data_writer_discard
 *
 * And for resource management in reader:
 * - memtx_sort_data_reader_space_init
 * - memtx_sort_data_reader_space_free
 *
 * See descriptions for more information.
 */

struct memtx_sort_data_writer;
struct memtx_sort_data_reader;

/* {{{ Utilities **************************************************************/

/**
 * Returns the sort data file name based on the corresponding @a snap_filename.
 */
const char *
memtx_sort_data_filename(const char *snap_filename);

/* }}} */

/* {{{ memtx_sort_data_writer *************************************************/

/**
 * Create a new sort data file writer, never fails (never returns NULL).
 */
struct memtx_sort_data_writer *
memtx_sort_data_writer_new(void);

/**
 * Delete the sort data file writer.
 *
 * @param writer - the sort data writer to delete.
 */
void
memtx_sort_data_writer_delete(struct memtx_sort_data_writer *writer);

/**
 * Create the sort data file and partially initialize headers.
 *
 * @param writer - the sort data writer.
 * @param snap_filename - the corresponding .snap file name.
 * @param vclock - the VClock of the .snap file.
 * @param instance_uuid - the UUID of the .snap file.
 * @param rv - the read view to create a sort data file for.
 */
int
memtx_sort_data_writer_create_file(struct memtx_sort_data_writer *writer,
				   const char *snap_filename,
				   struct vclock *vclock,
				   const struct tt_uuid *instance_uuid,
				   struct read_view *rv);

/**
 * Close the sort data file.
 *
 * @param writer - the sort data writer that has created a file.
 */
int
memtx_sort_data_writer_close_file(struct memtx_sort_data_writer *writer);

/**
 * Materialize the sort data file (remove the .inprogress suffix).
 *
 * @param writer - the sort data writer that has created a file.
 */
int
memtx_sort_data_writer_materialize(struct memtx_sort_data_writer *writer);

/**
 * Remove the in-progress or completed sort data file if any.
 *
 * @param writer - the sort data writer.
 */
void
memtx_sort_data_writer_discard(struct memtx_sort_data_writer *writer);

/**
 * Prepare to write the sort data of an index.
 *
 * @param writer - the sort data writer.
 * @param space_id - the ID of the space of the index.
 * @param index_id - the ID of the index to write SK data for.
 */
int
memtx_sort_data_writer_begin(struct memtx_sort_data_writer *writer,
			     uint32_t space_id, uint32_t index_id);

/**
 * Write the index data into the file.
 *
 * @param writer - the sort data writer that has began writing SK.
 * @param data - the index data chunk to write (tuple
 *		 pointers, optionally with hints).
 * @param size - the size of the data chunk.
 * @param count - the amount of tuples in the chunk.
 */
int
memtx_sort_data_writer_put(struct memtx_sort_data_writer *writer,
			   void *data, size_t size, size_t count);

/**
 * Write a PK tuple pointer into the file.
 *
 * @param writer - the sort data writer that has began writing PK.
 * @param tuple - the tuple pointer to write.
 */
int
memtx_sort_data_writer_put_tuple(struct memtx_sort_data_writer *writer,
				 struct tuple *tuple);

/**
 * Finish writing the index data and update related file header fields.
 *
 * @param writer - the sort data writer that has began writing SK.
 */
int
memtx_sort_data_writer_commit(struct memtx_sort_data_writer *writer);

/* }}} */

/* {{{ memtx_sort_data_reader *************************************************/

/**
 * Create a new sort data reader and read the file headers.
 *
 * @param snap_filename - name of the corresponding .snap file.
 * @param vclock - VClock of the .snap file (for verificvation).
 * @param instance_uuid - UUID of the .snap file (for verification).
 */
struct memtx_sort_data_reader *
memtx_sort_data_reader_new(const char *snap_filename,
			   const struct vclock *vclock,
			   const struct tt_uuid *instance_uuid);

/**
 * Delete the sort data reader.
 *
 * @param reader - the sort data reader to delete.
 */
void
memtx_sort_data_reader_delete(struct memtx_sort_data_reader *reader);

/**
 * Begin the space recovery if its sort data exists.
 *
 * @param reader - the sort data reader.
 * @param space_id - the ID of the space to recover.
 */
int
memtx_sort_data_reader_space_init(struct memtx_sort_data_reader *reader,
				  uint32_t space_id);

/**
 * Free the data used to recover the space.
 *
 * @param reader - the sort data reader that has began recovering a space.
 */
void
memtx_sort_data_reader_space_free(struct memtx_sort_data_reader *reader);

/**
 * Introduce a new PK tuple for the given space. This maps the next PK tuple
 * pointer from the sort data file with the given one. So once the sort data
 * pointer appears in the SK sort data, it will be translated to the given @a
 * tuple pointer.
 *
 * If no PK tuple pointers found for the space in the sort data file, then the
 * function is a no-op.
 *
 * @param reader - the sort data reader that has began recovering a space.
 * @param tuple - the newly recovered tuple.
 */
int
memtx_sort_data_reader_pk_add_tuple(struct memtx_sort_data_reader *reader,
				    struct tuple *tuple);

/**
 * Prepare to read sort data of index if available.
 *
 * @param reader - the sort data reader that has began recovering a
 *		   space and filled the old2new tuple pointer map.
 * @param index_id - the ID of the index to recover.
 * @param[out] index_included - set if the index is found in the sort data file.
 */
int
memtx_sort_data_reader_seek(struct memtx_sort_data_reader *reader,
			    uint32_t index_id, bool *index_included);

/**
 * Get the amount of tuples in the index sort data.
 *
 * @param reader - the sort data reader that has sought an index.
 */
size_t
memtx_sort_data_reader_get_size(struct memtx_sort_data_reader *reader);

/**
 * Read the index data available.
 *
 * @param reader - the sort data reader that has sought an index.
 * @param buffer - the buffer to read the data into.
 * @param expected_data_size - the amount of bytes to read.
 */
int
memtx_sort_data_reader_get(struct memtx_sort_data_reader *reader,
			   void *buffer, long expected_data_size);

/**
 * Update a tuple pointer from the sort data.
 *
 * @param reader - the sort data reader that has began recovering a
 *		   space and filled the old2new tuple pointer map.
 * @param old_ptr - the old tuple pointer to get a new pointer to.
 */
struct tuple *
memtx_sort_data_reader_resolve_tuple(struct memtx_sort_data_reader *reader,
				     struct tuple *old_ptr);

/* }}} */

/* {{{ Garbage collection *****************************************************/

/**
 * Garbage-collect a sort data file if any.
 *
 * @param snap_filename - the corresponding .snap file name.
 */
void
memtx_sort_data_collect(const char *snap_filename);

/* }}} */

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */
