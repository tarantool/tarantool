/*
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "tuple.h"

#include "small/small.h"
#include "tbuf.h"

#include "key_def.h"
#include "tuple_update.h"
#include <exception.h>
#include <stdio.h>

/** Global table of tuple formats */
struct tuple_format **tuple_formats;
struct tuple_format *tuple_format_ber;
static intptr_t recycled_format_ids = FORMAT_ID_NIL;

static uint32_t formats_size, formats_capacity;

uint32_t snapshot_version;

struct slab_arena tuple_arena;
static struct slab_cache tuple_slab_cache;
struct small_alloc talloc;

/** Extract all available type info from keys. */
void
field_type_create(enum field_type *types, uint32_t field_count,
		  struct rlist *key_list)
{
	/* There may be fields between indexed fields (gaps). */
	memset(types, 0, sizeof(*types) * field_count);

	struct key_def *key_def;
	/* extract field type info */
	rlist_foreach_entry(key_def, key_list, link) {
		struct key_part *part = key_def->parts;
		struct key_part *pend = part + key_def->part_count;
		for (; part < pend; part++) {
			enum field_type *ptype = &types[part->fieldno];
			if (*ptype != UNKNOWN && *ptype != part->type) {
				tnt_raise(ClientError,
					  ER_FIELD_TYPE_MISMATCH,
					  key_def->iid, part - key_def->parts,
					  field_type_strs[part->type],
					  field_type_strs[*ptype]);
			}
			*ptype = part->type;
		}
	}
}

void
tuple_format_register(struct tuple_format *format)
{
	if (recycled_format_ids != FORMAT_ID_NIL) {

		format->id = (uint16_t) recycled_format_ids;
		recycled_format_ids = (intptr_t) tuple_formats[recycled_format_ids];
	} else {
		if (formats_size == formats_capacity) {
			uint32_t new_capacity = formats_capacity ?
				formats_capacity * 2 : 16;
			struct tuple_format **formats;
			formats = (struct tuple_format **)
				realloc(tuple_formats, new_capacity *
					sizeof(tuple_formats[0]));
			if (formats == NULL)
				tnt_raise(LoggedError, ER_MEMORY_ISSUE,
					  sizeof(struct tuple_format),
					  "tuple_formats", "malloc");

			formats_capacity = new_capacity;
			tuple_formats = formats;
		}
		if (formats_size == FORMAT_ID_MAX + 1) {
			tnt_raise(LoggedError, ER_TUPLE_FORMAT_LIMIT,
				  (unsigned) formats_capacity);
		}
		format->id = formats_size++;
	}
	tuple_formats[format->id] = format;
}

void
tuple_format_deregister(struct tuple_format *format)
{
	if (format->id == FORMAT_ID_NIL)
		return;
	tuple_formats[format->id] = (struct tuple_format *) recycled_format_ids;
	recycled_format_ids = format->id;
	format->id = FORMAT_ID_NIL;
}

static struct tuple_format *
tuple_format_alloc(struct rlist *key_list)
{
	struct key_def *key_def;
	uint32_t max_fieldno = 0;
	uint32_t key_count = 0;

	/* find max max field no */
	rlist_foreach_entry(key_def, key_list, link) {
		struct key_part *part = key_def->parts;
		struct key_part *pend = part + key_def->part_count;
		key_count++;
		for (; part < pend; part++)
			max_fieldno = MAX(max_fieldno, part->fieldno);
	}
	uint32_t field_count = key_count > 0 ? max_fieldno + 1 : 0;

	uint32_t total = sizeof(struct tuple_format) +
		field_count * sizeof(int32_t) +
		field_count * sizeof(enum field_type);

	struct tuple_format *format = (struct tuple_format *) malloc(total);

	if (format == NULL) {
		tnt_raise(LoggedError, ER_MEMORY_ISSUE,
			  sizeof(struct tuple_format),
			  "tuple format", "malloc");
	}

	format->refs = 0;
	format->id = FORMAT_ID_NIL;
	format->max_fieldno = max_fieldno;
	format->field_count = field_count;
	format->types = (enum field_type *)
		((char *) format + sizeof(*format) +
		field_count * sizeof(int32_t));
	return format;
}

void
tuple_format_delete(struct tuple_format *format)
{
	tuple_format_deregister(format);
	free(format);
}

struct tuple_format *
tuple_format_new(struct rlist *key_list)
{
	struct tuple_format *format = tuple_format_alloc(key_list);

	try {
		tuple_format_register(format);
		field_type_create(format->types, format->field_count,
				  key_list);
	} catch (...) {
		tuple_format_delete(format);
		throw;
	}

	int32_t i = 0;
	int j = 0;
	for (; i < format->max_fieldno; i++) {
		/*
		 * In the tuple, store only offsets necessary to
		 * quickly access indexed fields. Start from
		 * field 1, not field 0, field 0 offset is 0.
		 */
		if (format->types[i + 1] == UNKNOWN)
			format->offset[i] = INT32_MIN;
		else
			format->offset[i] = --j;
	}
	if (format->field_count > 0) {
		/*
		 * The last offset is always there and is unused,
		 * to simplify the loop in tuple_init_field_map()
		 */
		format->offset[format->field_count - 1] = INT32_MIN;
	}
	format->field_map_size = -j * sizeof(uint32_t);
	return format;
}

/*
 * Validate a new tuple format and initialize tuple-local
 * format data.
 */
void
tuple_init_field_map(struct tuple_format *format, struct tuple *tuple, uint32_t *field_map)
{
	if (format->field_count == 0)
		return; /* Nothing to initialize */

	const char *pos = tuple->data;

	/* Check to see if the tuple has a sufficient number of fields. */
	uint32_t field_count = mp_decode_array(&pos);
	if (unlikely(field_count < format->field_count))
		tnt_raise(ClientError, ER_INDEX_ARITY,
			  (unsigned) field_count,
			  (unsigned) format->field_count);

	int32_t *offset = format->offset;
	enum field_type *type = format->types;
	enum field_type *type_end = format->types + format->field_count;
	uint32_t i = 0;

	for (; type < type_end; offset++, type++, i++) {
		const char *d = pos;
		enum mp_type mp_type = mp_typeof(*pos);
		mp_next(&pos);

		key_mp_type_validate(*type, mp_type, ER_FIELD_TYPE, i);

		if (*offset < 0 && *offset != INT32_MIN)
			field_map[*offset] = d - tuple->data;
	}
}

/**
 * Incremented on every snapshot and is used to distinguish tuples
 * which were created after start of a snapshot (these tuples can
 * be freed right away, since they are not used for snapshot) or
 * before start of a snapshot (these tuples can be freed only
 * after the snapshot has finished, otherwise it'll write bad data
 * to the snapshot file).
 */

/** Allocate a tuple */
struct tuple *
tuple_alloc(struct tuple_format *format, size_t size)
{
	size_t total = sizeof(struct tuple) + size + format->field_map_size;
	char *ptr = (char *) smalloc(&talloc, total, "tuple");
	struct tuple *tuple = (struct tuple *)(ptr + format->field_map_size);

	tuple->refs = 0;
	tuple->version = snapshot_version;
	tuple->bsize = size;
	tuple->format_id = tuple_format_id(format);
	tuple_format_ref(format, 1);

	say_debug("tuple_alloc(%zu) = %p", size, tuple);
	return tuple;
}

/**
 * Free the tuple.
 * @pre tuple->refs  == 0
 */
void
tuple_delete(struct tuple *tuple)
{
	say_debug("tuple_delete(%p)", tuple);
	assert(tuple->refs == 0);
	struct tuple_format *format = tuple_format(tuple);
	char *ptr = (char *) tuple - format->field_map_size;
	tuple_format_ref(format, -1);
	if (!talloc.is_delayed_free_mode || tuple->version == snapshot_version)
		smfree(&talloc, ptr);
	else
		smfree_delayed(&talloc, ptr);
}

/**
 * Add count to tuple's reference counter.
 * When the counter goes down to 0, the tuple is destroyed.
 *
 * @pre tuple->refs + count >= 0
 */
void
tuple_ref(struct tuple *tuple, int count)
{
	assert(tuple->refs + count >= 0);
	tuple->refs += count;

	if (tuple->refs == 0)
		tuple_delete(tuple);
}

const char *
tuple_seek(struct tuple_iterator *it, uint32_t i)
{
	const char *field = tuple_field(it->tuple, i);
	if (likely(field != NULL)) {
		it->pos = field;
		it->fieldno = i;
		return tuple_next(it);
	} else {
		it->pos = it->tuple->data + it->tuple->bsize;
		it->fieldno = tuple_arity(it->tuple);
		return NULL;
	}
}

const char *
tuple_next(struct tuple_iterator *it)
{
	const char *tuple_end = it->tuple->data + it->tuple->bsize;
	if (it->pos < tuple_end) {
		const char *field = it->pos;
		mp_next(&it->pos);
		assert(it->pos <= tuple_end);
		it->fieldno++;
		return field;
	}
	return NULL;
}

extern inline uint32_t
tuple_next_u32(struct tuple_iterator *it);

static const char *
tuple_field_to_cstr(const char *field, uint32_t len)
{
	static __thread char buf[256];
	len = MIN(len, sizeof(buf) - 1);
	memcpy(buf, field, len);
	buf[len] = '\0';
	return buf;
}

const char *
tuple_next_cstr(struct tuple_iterator *it)
{
	uint32_t fieldno = it->fieldno;
	const char *field = tuple_next(it);
	if (field == NULL)
		tnt_raise(ClientError, ER_NO_SUCH_FIELD, fieldno);
	if (mp_typeof(*field) != MP_STR)
		tnt_raise(ClientError, ER_FIELD_TYPE, fieldno,
			  field_type_strs[STRING]);
	uint32_t len = 0;
	const char *str = mp_decode_str(&field, &len);
	return tuple_field_to_cstr(str, len);
}

extern inline const char *
tuple_field(const struct tuple *tuple, uint32_t i);

extern inline uint32_t
tuple_field_u32(struct tuple *tuple, uint32_t i);

const char *
tuple_field_cstr(struct tuple *tuple, uint32_t i)
{
	const char *field = tuple_field(tuple, i);
	if (field == NULL)
		tnt_raise(ClientError, ER_NO_SUCH_FIELD, i);
	if (mp_typeof(*field) != MP_STR)
		tnt_raise(ClientError, ER_FIELD_TYPE, i,
			  field_type_strs[STRING]);
	uint32_t len = 0;
	const char *str = mp_decode_str(&field, &len);
	return tuple_field_to_cstr(str, len);
}

struct tuple *
tuple_update(struct tuple_format *format,
	     void *(*region_alloc)(void *, size_t), void *alloc_ctx,
	     const struct tuple *old_tuple, const char *expr,
	     const char *expr_end)
{
	uint32_t new_size = 0;
	const char *new_data = tuple_update_execute(region_alloc, alloc_ctx,
					expr, expr_end, old_tuple->data,
					old_tuple->data + old_tuple->bsize,
					&new_size);

	/* Allocate a new tuple. */
	assert(mp_typeof(*new_data) == MP_ARRAY);
	struct tuple *new_tuple = tuple_new(format, new_data,
					    new_data + new_size);

	try {
		tuple_init_field_map(format, new_tuple, (uint32_t *)new_tuple);
	} catch (Exception *e) {
		tuple_delete(new_tuple);
		throw;
	}
	return new_tuple;
}

struct tuple *
tuple_new(struct tuple_format *format, const char *data, const char *end)
{
	size_t tuple_len = end - data;
	assert(mp_typeof(*data) == MP_ARRAY);
	struct tuple *new_tuple = tuple_alloc(format, tuple_len);
	memcpy(new_tuple->data, data, tuple_len);
	try {
		tuple_init_field_map(format, new_tuple, (uint32_t *)new_tuple);
	} catch (...) {
		tuple_delete(new_tuple);
		throw;
	}
	return new_tuple;
}

/*
 * Compare two tuple fields.
 * Separate version exists since compare is a very
 * often used operation, so any performance speed up
 * in it can have dramatic impact on the overall
 * server performance.
 */
inline __attribute__((always_inline)) int
mp_compare_uint(const char **data_a, const char **data_b);

int
tuple_compare_field(const char *field_a, const char *field_b,
		    enum field_type type)
{
	switch (type) {
	case NUM:
		return mp_compare_uint(field_a, field_b);
	case STRING:
	{
		uint32_t size_a = mp_decode_strl(&field_a);
		uint32_t size_b = mp_decode_strl(&field_b);
		const char *a = field_a;
		const char *b = field_b;
		int r = memcmp(a, b, MIN(size_a, size_b));
		if (r == 0)
			r = size_a < size_b ? -1 : size_a > size_b;
		return r;
	}
	default:
	{
		assert(false);
		return 0;
	} /* end case */
	} /* end switch */
}

int
tuple_compare(const struct tuple *tuple_a, const struct tuple *tuple_b,
	      const struct key_def *key_def)
{
	if (key_def->part_count == 1 && key_def->parts[0].fieldno == 0) {
		const char *a = tuple_a->data;
		const char *b = tuple_b->data;
		mp_decode_array(&a);
		mp_decode_array(&b);
		return tuple_compare_field(a, b, key_def->parts[0].type);
	}

	const struct key_part *part = key_def->parts;
	const struct key_part *end = part + key_def->part_count;
	struct tuple_format *format_a = tuple_format(tuple_a);
	struct tuple_format *format_b = tuple_format(tuple_b);
	const char *field_a;
	const char *field_b;
	int r = 0;

	for (; part < end; part++) {
		field_a = tuple_field_old(format_a, tuple_a, part->fieldno);
		field_b = tuple_field_old(format_b, tuple_b, part->fieldno);
		assert(field_a != NULL && field_b != NULL);
		if ((r = tuple_compare_field(field_a, field_b, part->type)))
			break;
	}
	return r;
}

int
tuple_compare_dup(const struct tuple *tuple_a, const struct tuple *tuple_b,
		  const struct key_def *key_def)
{
	int r = tuple_compare(tuple_a, tuple_b, key_def);
	if (r == 0)
		r = tuple_a < tuple_b ? -1 : tuple_a > tuple_b;

	return r;
}

int
tuple_compare_with_key(const struct tuple *tuple, const char *key,
		       uint32_t part_count, const struct key_def *key_def)
{
	assert(key != NULL || part_count == 0);
	assert(part_count <= key_def->part_count);
	struct tuple_format *format = tuple_format(tuple);
	if (likely(part_count == 1)) {
		const struct key_part *part = key_def->parts;
		const char *field = tuple_field_old(format, tuple,
						    part->fieldno);
		return tuple_compare_field(field, key, part->type);
	}

	const struct key_part *part = key_def->parts;
	const struct key_part *end = part + MIN(part_count, key_def->part_count);
	int r = 0; /* Part count can be 0 in wildcard searches. */
	for (; part < end; part++) {
		const char *field = tuple_field_old(format, tuple,
						    part->fieldno);
		r = tuple_compare_field(field, key, part->type);
		if (r != 0)
			break;
		mp_next(&key);
	}
	return r;
}

void
tuple_init(float arena_prealloc, uint32_t objsize_min,
	   float alloc_factor)
{
	tuple_format_ber = tuple_format_new(&rlist_nil);
	/* Make sure this one stays around. */
	tuple_format_ref(tuple_format_ber, 1);

	uint32_t slab_size = 4*1024*1024;
	size_t prealloc = arena_prealloc * 1024 * 1024 * 1024;

	int flags;
	if (access("/proc/user_beancounters", F_OK) == 0) {
		say_warn("disable shared arena since running under OpenVZ "
		    "(https://bugzilla.openvz.org/show_bug.cgi?id=2805)");
		flags = MAP_PRIVATE;
        } else {
		say_info("mapping %zu bytes for a shared arena...",
			 prealloc);
		flags = MAP_SHARED;
	}

	if (slab_arena_create(&tuple_arena, prealloc, prealloc,
			      slab_size, flags)) {
		panic_syserror("failed to preallocate %zu bytes",
			       prealloc);
	}
	slab_cache_create(&tuple_slab_cache, &tuple_arena,
			  slab_size);
	small_alloc_create(&talloc, &tuple_slab_cache,
			   objsize_min, alloc_factor);
}

void
tuple_free()
{
	/* Clear recycled ids. */
	while (recycled_format_ids != FORMAT_ID_NIL) {

		uint16_t id = (uint16_t) recycled_format_ids;
		recycled_format_ids = (intptr_t) tuple_formats[id];
		tuple_formats[id] = NULL;
	}
	for (struct tuple_format **format = tuple_formats;
	     format < tuple_formats + formats_size;
	     format++)
		free(*format); /* ignore the reference count. */
	free(tuple_formats);
}

void
tuple_begin_snapshot()
{
	snapshot_version++;
	small_alloc_setopt(&talloc, SMALL_DELAYED_FREE_MODE, true);
}

void
tuple_end_snapshot()
{
	small_alloc_setopt(&talloc, SMALL_DELAYED_FREE_MODE, false);
}
