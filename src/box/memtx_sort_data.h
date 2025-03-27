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
memtx_sort_data_new(struct read_view *rv, const char *dirname,
		    struct tt_uuid *instance_uuid);

void
memtx_sort_data_delete(struct memtx_sort_data *msd);

int
memtx_sort_data_create(struct memtx_sort_data *msd);

void
memtx_sort_data_close(struct memtx_sort_data *msd);

void
memtx_sort_data_discard(struct memtx_sort_data *msd);

bool
memtx_sort_data_begin(struct memtx_sort_data *msd,
		      uint32_t space_id, uint32_t index_id);

int
memtx_sort_data_write(struct memtx_sort_data *msd,
		      void *data, size_t size, size_t count);

void
memtx_sort_data_commit(struct memtx_sort_data *msd);

/** Reading operations. */

struct memtx_sort_data_reader *
memtx_sort_data_start_read(const char *dirname, const struct vclock *vclock,
			   const struct tt_uuid *instance_uuid);

void
memtx_sort_data_init_pk(struct memtx_sort_data_reader *msdr, uint32_t space_id);

void
memtx_sort_data_map_next_pk_tuple(struct memtx_sort_data_reader *msdr,
				  struct tuple *new_ptr);

bool
memtx_sort_data_reader_begin(struct memtx_sort_data_reader *msdr,
			     uint32_t space_id);

bool
memtx_sort_data_seek_index(struct memtx_sort_data_reader *msdr,
			   uint32_t index_id);

size_t
memtx_sort_data_size(struct memtx_sort_data_reader *msdr);

int
memtx_sort_data_read(struct memtx_sort_data_reader *msdr, void *buffer);

struct tuple *
memtx_sort_data_resolve_tuple(struct memtx_sort_data_reader *msdr,
			      struct tuple *old_ptr);

int
memtx_sort_data_reader_commit(struct memtx_sort_data_reader *msdr);

void
memtx_sort_data_reader_delete(struct memtx_sort_data_reader *msdr);

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */
