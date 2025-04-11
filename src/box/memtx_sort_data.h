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

/* Writing operations. */

struct memtx_sort_data *
memtx_sort_data_writer_new(struct read_view *rv, const char *dirname,
			   struct tt_uuid *instance_uuid);

void
memtx_sort_data_writer_delete(struct memtx_sort_data *msd);

int
memtx_sort_data_writer_touch_file(struct memtx_sort_data *msd,
				  struct vclock *vclock);

int
memtx_sort_data_writer_create_file(struct memtx_sort_data *msd,
				   struct vclock *vclock);

int
memtx_sort_data_writer_close_file(struct memtx_sort_data *msd);

int
memtx_sort_data_writer_materialize_file(struct memtx_sort_data *msd);

void
memtx_sort_data_writer_discard_file(struct memtx_sort_data *msd);

int
memtx_sort_data_writer_begin(struct memtx_sort_data *msd,
			     uint32_t space_id, uint32_t index_id,
			     bool *have_data);

int
memtx_sort_data_writer_put(struct memtx_sort_data *msd,
			   void *data, size_t size, size_t count);

int
memtx_sort_data_writer_commit(struct memtx_sort_data *msd);

/** Reading operations. */

struct memtx_sort_data_reader *
memtx_sort_data_reader_new(const char *dirname, const struct vclock *vclock,
			   const struct tt_uuid *instance_uuid);

void
memtx_sort_data_reader_delete(struct memtx_sort_data_reader *msdr);

int
memtx_sort_data_reader_pk_add_tuple(struct memtx_sort_data_reader *msdr,
				    uint32_t space_id, bool is_first,
				    struct tuple *tuple);

int
memtx_sort_data_reader_space_check(struct memtx_sort_data_reader *msdr,
				   uint32_t space_id, bool *has_sort_data);

int
memtx_sort_data_reader_seek(struct memtx_sort_data_reader *msdr,
			    uint32_t space_id, uint32_t index_id,
			    bool *index_included);

size_t
memtx_sort_data_reader_get_size(struct memtx_sort_data_reader *msdr);

int
memtx_sort_data_reader_get(struct memtx_sort_data_reader *msdr, void *buffer);

struct tuple *
memtx_sort_data_reader_resolve_tuple(struct memtx_sort_data_reader *msdr,
				     struct tuple *old_ptr);

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */
