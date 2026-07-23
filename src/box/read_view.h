/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "small/rlist.h"
#include "vclock/vclock.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct cord;
struct engine;
struct field_def;
struct index;
struct index_read_view;
struct index_read_view_iterator;
struct port;
struct space;
struct space_upgrade_read_view;
struct space_upgrade_read_view_handle;
struct tuple;
struct tuple_format;

/** Read view of a space. */
struct space_read_view {
	/** Link in read_view::spaces. */
	struct rlist link;
	/** Read view that owns this space. */
	struct read_view *rv;
	/** Space id. */
	uint32_t id;
	/** Space name. */
	char *name;
	/** Space engine */
	struct engine *engine;
	/**
	 * Tuple field definition array used by this space. Allocated only if
	 * read_view_opts::enable_field_names is set, otherwise set to NULL.
	 */
	struct field_def *fields;
	/** Number of entries in the fields array. */
	uint32_t field_count;
	/**
	 * Tuple format data used by this space. Allocated only if
	 * read_view_opts::enable_field_names is set, otherwise set to NULL.
	 * Used for creation of space_read_view::format.
	 */
	char *format_data;
	/** Length of tuple format data. */
	size_t format_data_len;
	/**
	 * Upgrade function for this space read view or NULL if there wasn't
	 * a space upgrade in progress at the time when this read view was
	 * created or read_view_opts::enable_space_upgrade wasn't set.
	 */
	struct space_upgrade_read_view *upgrade;
	/** Replication group id. See space_opts::group_id. */
	uint32_t group_id;
	/**
	 * Max index id.
	 *
	 * (The number of entries in index_map is index_id_max + 1.)
	 */
	uint32_t index_id_max;
	/** The amount on non-NULL entries in the index_map. */
	uint32_t index_count;
	/**
	 * Sparse (may contain nulls) array of index read views,
	 * indexed by index id.
	 */
	struct index_read_view **index_map;
};

/**
 * Space read view handle.
 *
 * See the comment to read_view_handle for details what handles are used for.
 */
struct space_read_view_handle {
	/** Link in read_view_handle::spaces. */
	struct rlist link;
	/** Pointer to the space read view this handle is for. */
	struct space_read_view *ptr;
	/** Thread that exclusively owns this handle. */
	struct cord *owner;
	/**
	 * Runtime tuple format needed to access tuple field names by name.
	 * Referenced (ref counter incremented).
	 *
	 * A new format is created only if read_view_opts::enable_field_names
	 * is set, otherwise runtime_tuple_format is used.
	 *
	 * We can't just use the space tuple format as is because it allocates
	 * tuples from the space engine arena, which is single-threaded, while
	 * a read view may be used from threads other than tx. Good news is
	 * runtime tuple formats are reusable so if we create more than one
	 * read view of the same space, we will use just one tuple format for
	 * them all.
	 */
	struct tuple_format *format;
	/** Handle for space_read_view::upgrade. */
	struct space_upgrade_read_view_handle *upgrade;
};

static inline struct index_read_view *
space_read_view_index(struct space_read_view *space_rv, uint32_t id)
{
	return id <= space_rv->index_id_max ? space_rv->index_map[id] : NULL;
}

/** Read view of the entire database. */
struct read_view {
	/** Unique read view identifier. */
	uint64_t id;
	/** Read view name. Used for introspection. */
	char *name;
	/**
	 * Set if this read view is needed for system purposes (for example, to
	 * make a checkpoint). Initialized with read_view_opts::is_system.
	 */
	bool is_system;
	/**
	 * Set if tuples read from the read view don't need to be decompressed.
	 * Initialized with read_view_opts::disable_decompression.
	 */
	bool disable_decompression;
	/** Monotonic clock at the time when the read view was created. */
	double timestamp;
	/** Replicaset vclock at the time when the read view was created. */
	struct vclock vclock;
	/** List of engine read views, linked by engine_read_view::link. */
	struct rlist engines;
	/** List of space read views, linked by space_read_view::link. */
	struct rlist spaces;
};

/**
 * Database read view handle.
 *
 * The read view core is MT-safe: it's okay to concurrently access the same
 * read view object from different threads. However, to implement a box-like
 * API with support for field names and space upgrade, we need some extra info
 * that is stored in thread-local objects, such as tuple formats. To attach
 * this info to a read view, we introduce the notion of a read view handle,
 * which stores a pointer to the read view plus thread-local objects. A read
 * view handle may only be used from the thread where it was created, and the
 * read view core for which the handle was created must not be destroyed while
 * the handle is in use.
 *
 * Note that there's no handle for an index read view because an index read
 * view by itself doesn't store any thread-local information.
 */
struct read_view_handle {
	/** Pointer to the database read view this handle is for. */
	struct read_view *ptr;
	/** Thread that exclusively owns this handle. */
	struct cord *owner;
	/**
	 * List of space read view handles, linked by
	 * space_read_view_handle::link.
	 */
	struct rlist spaces;
};

#define read_view_foreach_space(space_rv, rv) \
	rlist_foreach_entry(space_rv, &(rv)->spaces, link)

/** Read view creation options. */
struct read_view_opts {
	/** Read view name. Used for introspection. Must be set. */
	const char *name;
	/**
	 * Set if this read view is needed for system purposes (for example, to
	 * make a checkpoint). Default: false.
	 */
	bool is_system;
	/**
	 * Space filter: should return true if the space should be included
	 * into the read view.
	 *
	 * Default: include all spaces (return true, ignore arguments).
	 */
	bool
	(*filter_space)(struct space *space, void *arg);
	/**
	 * Index filter: should return true if the index should be included
	 * into the read view.
	 *
	 * Default: include all indexes (return true, ignore arguments).
	 */
	bool
	(*filter_index)(struct space *space, struct index *index, void *arg);
	/**
	 * Argument passed to filter functions.
	 */
	void *filter_arg;
	/**
	 * If this flag is set, a new runtime tuple format will be created for
	 * each read view space to support accessing tuple fields by name,
	 * otherwise the preallocated name-less runtime tuple format will be
	 * used instead.
	 */
	bool enable_field_names;
	/**
	 * If this flag is set and there's a space upgrade in progress at the
	 * time when this read view is created, create an upgrade function that
	 * can be applied to tuples retrieved from this read view. See also
	 * space_read_view::upgrade.
	 */
	bool enable_space_upgrade;
	/**
	 * Data-temporary spaces aren't included into this read view unless this
	 * flag is set.
	 */
	bool enable_data_temporary_spaces;
	/**
	 * Memtx-specific. Disables decompression of tuples fetched from
	 * the read view. Setting this flag makes the raw read view methods
	 * (get_raw, next_raw) return a pointer to the data stored in
	 * the read view as is, without any preprocessing or copying to
	 * the fiber region. The user is supposed to decompress the data
	 * encoded in the MP_COMPRESSION MsgPack extension manually.
	 */
	bool disable_decompression;
};

/** Sets read view options to default values. */
void
read_view_opts_create(struct read_view_opts *opts);

/**
 * Opens a database read view: all changes done to the database after a read
 * view was open will not be visible from the read view.
 *
 * Engines that don't support read view creation are silently skipped.
 *
 * Returns 0 on success. On error, returns -1 and sets diag.
 */
int
read_view_open(struct read_view *rv, const struct read_view_opts *opts);

/**
 * Closes a database read view.
 */
void
read_view_close(struct read_view *rv);

/**
 * Looks up an open read view by id.
 *
 * Returns NULL if not found.
 */
struct read_view *
read_view_by_id(uint64_t id);

typedef bool
read_view_foreach_f(struct read_view *rv, void *arg);

/**
 * Invokes a callback for each open read view with no particular order.
 *
 * The callback is passed a read view object and the given argument.
 * If it returns true, iteration continues. Otherwise, iteration breaks,
 * and the function returns false.
 */
bool
read_view_foreach(read_view_foreach_f cb, void *arg);

/**
 * Allocates a read view handle for exclusive use in the current thread.
 *
 * On error, returns NULL and sets diag.
 */
struct read_view_handle *
read_view_handle_new(struct read_view *rv);

/**
 * Frees a read view handle.
 *
 * A handle may only be destroyed in the thread where it was created.
 */
void
read_view_handle_delete(struct read_view_handle *h);

/**
 * Gets a tuple from an index read view by a key.
 * Returns 0 on success. On error, returns -1 and sets diag.
 */
int
box_index_read_view_get(
	struct space_read_view_handle *space, uint32_t index_id,
	const char *key, const char *key_end, struct tuple **result);

/**
 * Counts tuples matching the given key in an index read view.
 * On error, returns -1 and sets diag.
 */
ssize_t
box_index_read_view_count(
	struct space_read_view_handle *space, uint32_t index_id,
	int iterator, const char *key, const char *key_end);

/**
 * Returns a quantile point in an indexed range allocated on the fiber region.
 * Returns 0 on success. On error, returns -1 and sets diag.
 */
int
box_index_read_view_quantile(
	struct space_read_view_handle *space, uint32_t index_id,
	double level, const char *begin_key, const char *begin_key_end,
	const char *end_key, const char *end_key_end,
	const char **quantile_key, const char **quantile_key_end);

/**
 * Selects tuples from an index read view.
 * If packed_pos is not NULL and *packed_pos is not NULL, selection begins
 * right after tuple with position, described by this argument.
 * If update_pos is true, packed_pos and packed_pos_end are updated to
 * position of last selected tuple. Returned position is allocated
 * on the fiber region. If update_pos is true, packed_pos and packed_pos_end
 * must not be NULL.
 * Returns 0 on success. On error, returns -1 and sets diag.
 */
int
box_index_read_view_select(
	struct space_read_view_handle *space, uint32_t index_id,
	int iterator, uint32_t offset, uint32_t limit,
	const char *key, const char *key_end,
	const char **packed_pos, const char **packed_pos_end,
	bool update_pos, struct port *port);

/**
 * Gets packed position of tuple in index, which can be passed to box_select
 * (multikey and func indexes are not supported). Returned position
 * is allocated on the fiber region.
 */
int
box_index_read_view_tuple_position(
	struct space_read_view_handle *space, uint32_t index_id,
	const char *tuple, const char *tuple_end,
	const char **packed_pos, const char **packed_pos_end);

/**
 * Creates an iterator over an index read view, positioned after passed
 * position, if it is not NULL.
 * Returns 0 on success. On error, returns -1 and sets diag.
 */
int
box_index_read_view_create_iterator(
	struct space_read_view_handle *space, uint32_t index_id,
	int iterator, const char *key, const char *key_end,
	const char *packed_pos, const char *packed_pos_end,
	struct index_read_view_iterator *it);

/**
 * Same as box_index_read_view_create_iterator, but skips the specified by the
 * offset parameter amount of tuples.
 */
int
box_index_read_view_create_iterator_with_offset(
	struct space_read_view_handle *space, uint32_t index_id,
	int iterator, const char *key, const char *key_end,
	const char *packed_pos, const char *packed_pos_end,
	uint32_t offset, struct index_read_view_iterator *it);

/**
 * Retrieves the next tuple from an index read view iterator.
 * Returns 0 on success. On error, returns -1 and sets diag.
 * The tuple is returned in the 'result' argument. On EOF, it's set to NULL.
 */
int
box_index_read_view_iterator_next(struct index_read_view_iterator *it,
				  struct space_read_view_handle *space,
				  struct tuple **result);

/**
 * Destroys an index read view iterator.
 */
void
box_index_read_view_iterator_destroy(struct index_read_view_iterator *it);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
