#include "memtx_tuple.h"
#include "tuple.h"
#include "small/small.h"
#include "box.h"

/** Common quota for memtx tuples and indexes */
extern struct quota memtx_quota;
/** Memtx tuple allocator */
extern struct small_alloc memtx_alloc;
/** Memtx tuple slab arena */
extern struct slab_arena memtx_arena;

struct tuple_format_vtab memtx_tuple_format_vtab = {
	memtx_tuple_new,
	memtx_tuple_delete,
};

struct tuple *
memtx_tuple_new(struct tuple_format *format, const char *data, const char *end)
{
	assert(mp_typeof(*data) == MP_ARRAY);
	size_t tuple_len = end - data;
	size_t total =
		sizeof(struct tuple) + tuple_len + format->field_map_size;
	ERROR_INJECT(ERRINJ_TUPLE_ALLOC,
		     do { diag_set(OutOfMemory, (unsigned) total,
				   "slab allocator", "tuple"); return NULL; }
		     while(false); );
	struct tuple *tuple = (struct tuple *) smalloc(&memtx_alloc, total);
	/**
	 * Use a nothrow version and throw an exception here,
	 * to throw an instance of ClientError. Apart from being
	 * more nice to the user, ClientErrors are ignored in
	 * panic_on_wal_error=false mode, allowing us to start
	 * with lower arena than necessary in the circumstances
	 * of disaster recovery.
	 */
	if (tuple == NULL) {
		if (total > memtx_alloc.objsize_max) {
			diag_set(ClientError, ER_SLAB_ALLOC_MAX,
				 (unsigned) total);
			error_log(diag_last_error(diag_get()));
		} else {
			diag_set(OutOfMemory, (unsigned) total,
				 "slab allocator", "tuple");
		}
		return NULL;
	}
	tuple->refs = 0;
	tuple->version = snapshot_version;
	tuple->bsize = tuple_len;
	tuple->format_id = tuple_format_id(format);
	tuple_format_ref(format, 1);
	tuple->data_offset = sizeof(struct tuple) + format->field_map_size;
	char *raw = (char *) tuple + tuple->data_offset;
	uint32_t *field_map = (uint32_t *) raw;
	memcpy(raw, data, tuple_len);
	if (tuple_init_field_map(format, field_map, raw)) {
		memtx_tuple_delete(format, tuple);
		return NULL;
	}
	say_debug("%s(%zu) = %p", __func__, tuple_len, tuple);
	return tuple;
}

void
memtx_tuple_delete(struct tuple_format *format, struct tuple *tuple)
{
	say_debug("%s(%p)", __func__, tuple);
	assert(tuple->refs == 0);
	size_t total = sizeof(struct tuple) + tuple->bsize +
		       format->field_map_size;
	tuple_format_ref(format, -1);
	if (!memtx_alloc.is_delayed_free_mode ||
	    tuple->version == snapshot_version)
		smfree(&memtx_alloc, tuple, total);
	else
		smfree_delayed(&memtx_alloc, tuple, total);
}
