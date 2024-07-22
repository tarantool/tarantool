/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
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

#include "trivia/util.h"
#include "memory.h"
#include "fiber.h"
#include "small/quota.h"
#include "small/small.h"
#include "xrow_update.h"
#include "coll_id_cache.h"

static struct mempool tuple_iterator_pool;
static struct small_alloc runtime_alloc;

enum {
	/** Lowest allowed slab_alloc_minimal */
	OBJSIZE_MIN = 16,
};

const char *tuple_arena_type_strs[tuple_arena_type_MAX] = {
	[TUPLE_ARENA_MEMTX] = "memtx",
	[TUPLE_ARENA_MALLOC] = "malloc",
	[TUPLE_ARENA_RUNTIME] = "runtime",
};

/**
 * Storage for additional reference counter of a tuple.
 */
struct tuple_uploaded_refs {
	struct tuple *tuple;
	uint32_t refs;
};

static uint32_t
tuple_pointer_hash(const struct tuple *a)
{
	uintptr_t u = (uintptr_t)a;
	if (sizeof(uintptr_t) <= sizeof(uint32_t))
		return u;
	else
		return u ^ (u >> 32);
}

#define mh_name _tuple_uploaded_refs
#define mh_key_t struct tuple *
#define mh_node_t struct tuple_uploaded_refs
#define mh_arg_t int
#define mh_hash(a, arg) (tuple_pointer_hash((a)->tuple))
#define mh_hash_key(a, arg) (tuple_pointer_hash(a))
#define mh_cmp(a, b, arg) ((a)->tuple != (b)->tuple)
#define mh_cmp_key(a, b, arg) ((a) != (b)->tuple)
#define MH_SOURCE 1
#include "salad/mhash.h"

static struct mh_tuple_uploaded_refs_t *tuple_uploaded_refs;

static const double ALLOC_FACTOR = 1.05;

/**
 * Last tuple returned by public C API
 * \sa tuple_bless()
 */
struct tuple *box_tuple_last;

struct tuple_format *tuple_format_runtime;

static void
runtime_tuple_delete(struct tuple_format *format, struct tuple *tuple);

static struct tuple *
runtime_tuple_new(struct tuple_format *format, const char *data, const char *end);

/** Fill `tuple_info'. */
static void
runtime_tuple_info(struct tuple_format *format, struct tuple *tuple,
		   struct tuple_info *tuple_info);

/** A virtual method table for tuple_format_runtime */
static struct tuple_format_vtab tuple_format_runtime_vtab = {
	runtime_tuple_delete,
	runtime_tuple_new,
	runtime_tuple_info,
};

static struct tuple *
runtime_tuple_new(struct tuple_format *format, const char *data, const char *end)
{
	assert(format->vtab.tuple_delete == tuple_format_runtime_vtab.tuple_delete);

	mp_tuple_assert(data, end);
	struct tuple *tuple = NULL;
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	struct field_map_builder builder;
	if (tuple_field_map_create(format, data, true, &builder) != 0)
		goto end;
	uint32_t field_map_size = field_map_build_size(&builder);
	uint32_t data_offset = sizeof(struct tuple) + field_map_size;
	if (tuple_check_data_offset(data_offset) != 0)
		goto end;

	size_t data_len = end - data;
	assert(data_len <= UINT32_MAX); /* bsize is UINT32_MAX */
	bool make_compact = tuple_can_be_compact(data_offset, data_len);
	if (make_compact)
		data_offset -= TUPLE_COMPACT_SAVINGS;

	size_t total = data_offset + data_len;
	tuple = (struct tuple *) smalloc(&runtime_alloc, total);
	if (tuple == NULL) {
		diag_set(OutOfMemory, (unsigned) total,
			 "malloc", "tuple");
		goto end;
	}

	tuple_create(tuple, 0, tuple_format_id(format),
		     data_offset, data_len, make_compact);
	tuple_format_ref(format);
	char *raw = (char *) tuple + data_offset;
	field_map_build(&builder, raw - field_map_size);
	memcpy(raw, data, data_len);
end:
	region_truncate(region, region_svp);
	return tuple;
}

static void
runtime_tuple_delete(struct tuple_format *format, struct tuple *tuple)
{
	assert(format->vtab.tuple_delete == tuple_format_runtime_vtab.tuple_delete);
	assert(tuple->local_refs == 0);
	assert(!tuple_has_flag(tuple, TUPLE_HAS_UPLOADED_REFS));
	size_t total = tuple_size(tuple);
	tuple_format_unref(format);
	smfree(&runtime_alloc, tuple, total);
}

static void
runtime_tuple_info(struct tuple_format *format, struct tuple *tuple,
		   struct tuple_info *tuple_info)
{
	assert(format->vtab.tuple_delete ==
	       tuple_format_runtime_vtab.tuple_delete);
	(void)format;

	uint16_t data_offset = tuple_data_offset(tuple);
	tuple_info->data_size = tuple_bsize(tuple);
	tuple_info->header_size = sizeof(struct tuple);
	if (tuple_is_compact(tuple))
		tuple_info->header_size -= TUPLE_COMPACT_SAVINGS;
	tuple_info->field_map_size = data_offset - tuple_info->header_size;
	tuple_info->waste_size = 0;
	tuple_info->arena_type = TUPLE_ARENA_RUNTIME;
}

int
tuple_validate_raw(struct tuple_format *format, const char *tuple)
{
	if (tuple_format_field_count(format) == 0)
		return 0; /* Nothing to check */

	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	struct field_map_builder builder;
	int rc = tuple_field_map_create(format, tuple, true, &builder);
	region_truncate(region, region_svp);
	return rc;
}

/**
 * Incremented on every snapshot and is used to distinguish tuples
 * which were created after start of a snapshot (these tuples can
 * be freed right away, since they are not used for snapshot) or
 * before start of a snapshot (these tuples can be freed only
 * after the snapshot has finished, otherwise it'll write bad data
 * to the snapshot file).
 */

const char *
tuple_seek(struct tuple_iterator *it, uint32_t fieldno)
{
	const char *field = tuple_field(it->tuple, fieldno);
	if (likely(field != NULL)) {
		it->pos = field;
		it->fieldno = fieldno;
		return tuple_next(it);
	} else {
		it->pos = it->end;
		it->fieldno = tuple_field_count(it->tuple);
		return NULL;
	}
}

const char *
tuple_next(struct tuple_iterator *it)
{
	if (it->pos < it->end) {
		const char *field = it->pos;
		mp_next(&it->pos);
		assert(it->pos <= it->end);
		it->fieldno++;
		return field;
	}
	return NULL;
}

/**
 * Reference count portion that is uploaded or acquired to/from external
 * storage in one iteration.
 * Upload/acquire is relatively long operation and should be executed more
 * seldom as possible. The optimal portion should set local ref counter to the
 * farthest value from 0 and TUPLE_LOCAL_REF_MAX and thus should be close to
 * half of TUPLE_LOCAL_REF_MAX.
 */
enum { TUPLE_UPLOAD_REFS = TUPLE_LOCAL_REF_MAX / 2 + 1 };

/** Convenient helper for hash table. */
static uint32_t
tuple_ref_get_uploaded_refs(struct tuple *tuple)
{
	if (!tuple_has_flag(tuple, TUPLE_HAS_UPLOADED_REFS))
		return 0;
	mh_int_t pos = mh_tuple_uploaded_refs_find(tuple_uploaded_refs, tuple,
						   0);
	assert(pos != mh_end(tuple_uploaded_refs));
	struct tuple_uploaded_refs *uploaded =
		mh_tuple_uploaded_refs_node(tuple_uploaded_refs, pos);
	assert(uploaded->tuple == tuple);
	assert(uploaded->refs >= TUPLE_UPLOAD_REFS);
	assert(uploaded->refs % TUPLE_UPLOAD_REFS == 0);
	return uploaded->refs;
}

/** Convenient helper for hash table. */
static void
tuple_ref_set_uploaded_refs(struct tuple *tuple, uint32_t refs)
{
	struct tuple_uploaded_refs put;
	put.tuple = tuple;
	put.refs = refs;
	mh_tuple_uploaded_refs_put(tuple_uploaded_refs, &put, NULL, 0);
}

/** Convenient helper for hash table. */
static void
tuple_ref_drop_uploaded_refs(struct tuple *tuple)
{
	assert(tuple_has_flag(tuple, TUPLE_HAS_UPLOADED_REFS));
	mh_int_t pos = mh_tuple_uploaded_refs_find(tuple_uploaded_refs, tuple,
						   0);
	assert(pos != mh_end(tuple_uploaded_refs));
	mh_tuple_uploaded_refs_del(tuple_uploaded_refs, pos, 0);
}

void
tuple_upload_refs(struct tuple *tuple)
{
	assert(tuple->local_refs == TUPLE_LOCAL_REF_MAX);
	uint32_t refs = tuple_ref_get_uploaded_refs(tuple);
	tuple_ref_set_uploaded_refs(tuple, refs + TUPLE_UPLOAD_REFS);
	tuple_set_flag(tuple, TUPLE_HAS_UPLOADED_REFS);
	tuple->local_refs -= TUPLE_UPLOAD_REFS;
}

void
tuple_acquire_refs(struct tuple *tuple)
{
	assert(tuple->local_refs == 0);
	assert(tuple_has_flag(tuple, TUPLE_HAS_UPLOADED_REFS));
	uint32_t refs = tuple_ref_get_uploaded_refs(tuple);
	if (refs == TUPLE_UPLOAD_REFS) {
		tuple_ref_drop_uploaded_refs(tuple);
		tuple_clear_flag(tuple, TUPLE_HAS_UPLOADED_REFS);
	} else {
		tuple_ref_set_uploaded_refs(tuple, refs - TUPLE_UPLOAD_REFS);
	}
	tuple->local_refs += TUPLE_UPLOAD_REFS;
}

size_t
tuple_bigref_tuple_count()
{
	return tuple_uploaded_refs->size;
}

struct tuple_format *
runtime_tuple_format_new(const char *format_data, size_t format_data_len,
			 bool names_only)
{
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	const char *p = format_data;
	struct field_def *fields = NULL;
	uint32_t field_count = 0;
	if (format_data != NULL &&
	    field_def_array_decode(&p, &fields, &field_count, region,
				   /*names_only=*/names_only) != 0)
		goto error;
	struct tuple_dictionary *dict =
		tuple_dictionary_new(fields, field_count);
	if (dict == NULL)
		goto error;
	if (names_only) {
		fields = NULL;
		field_count = 0;
	}
	struct tuple_format *format =
		tuple_format_new(/*vtab=*/&tuple_format_runtime_vtab,
				 /*engine=*/NULL, /*keys=*/NULL,
				 /*key_count=*/0, /*space_fields=*/fields,
				 /*space_field_count=*/field_count,
				 /*exact_field_count=*/0,  /*dict=*/dict,
				 /*is_temporary=*/false, /*is_reusable=*/true,
				 /*constraint_def=*/NULL,
				 /*constraint_count=*/0,
				 /*format_data=*/format_data,
				 /*format_data_len=*/format_data_len);
	region_truncate(region, region_svp);
	/*
	 * Since dictionary reference counter is 1 from the
	 * beginning and after creation of the tuple_format
	 * increases by one, we must decrease it once.
	 */
	tuple_dictionary_unref(dict);
	return format;
error:
	region_truncate(region, region_svp);
	return NULL;
}

int
tuple_init(field_name_hash_f hash)
{
	tuple_format_init();
	field_name_hash = hash;
	/*
	 * Create a format for runtime tuples
	 */
	tuple_format_runtime = runtime_tuple_format_new(NULL, 0,
							/*names_only=*/false);
	if (tuple_format_runtime == NULL)
		return -1;
	if (tuple_format_runtime->id != 0)
		panic("tuple_format_runtime must have id == 0");

	/* Make sure this one stays around. */
	tuple_format_ref(tuple_format_runtime);

	float actual_alloc_factor;
	small_alloc_create(&runtime_alloc, &cord()->slabc, OBJSIZE_MIN,
			   sizeof(intptr_t), ALLOC_FACTOR,
			   &actual_alloc_factor);

	mempool_create(&tuple_iterator_pool, &cord()->slabc,
		       sizeof(struct tuple_iterator));

	box_tuple_last = NULL;

	tuple_uploaded_refs = mh_tuple_uploaded_refs_new();

	coll_id_cache_init();
	return 0;
}

void
tuple_arena_create(struct slab_arena *arena, struct quota *quota,
		   uint64_t arena_max_size, uint32_t slab_size,
		   bool dontdump, const char *arena_name)
{
	/*
	 * Ensure that quota is a multiple of slab_size, to
	 * have accurate value of quota_used_ratio.
	 */
	size_t prealloc = small_align(arena_max_size, slab_size);

        /*
         * Skip from coredump if requested.
         */
        int flags = SLAB_ARENA_PRIVATE;
        if (dontdump)
                flags |= SLAB_ARENA_DONTDUMP;

	say_info("mapping %zu bytes for %s tuple arena...", prealloc,
		 arena_name);

	if (slab_arena_create(arena, quota, prealloc, slab_size, flags) != 0) {
		if (errno == ENOMEM) {
			panic("failed to preallocate %zu bytes: Cannot "\
			      "allocate memory, check option '%s_memory' in box.cfg(..)", prealloc,
			      arena_name);
		} else {
			panic_syserror("failed to preallocate %zu bytes for %s"\
				       " tuple arena", prealloc, arena_name);
		}
	}
}

void
tuple_arena_destroy(struct slab_arena *arena)
{
	slab_arena_destroy(arena);
}

void
tuple_free(void)
{
	/* Unref last tuple returned by public C API */
	if (box_tuple_last != NULL) {
		tuple_unref(box_tuple_last);
		box_tuple_last = NULL;
	}

	mempool_destroy(&tuple_iterator_pool);
	small_alloc_destroy(&runtime_alloc);

	tuple_format_free();

	coll_id_cache_destroy();

	mh_tuple_uploaded_refs_delete(tuple_uploaded_refs);
}

static int
small_stats_noop_cb(const void *stats, void *cb_ctx)
{
	(void)stats;
	(void)cb_ctx;
	return 0;
}

size_t
tuple_runtime_memory_used(void)
{
	struct small_stats data_stats;
	small_stats(&runtime_alloc, &data_stats, small_stats_noop_cb, NULL);
	return data_stats.used;
}

void *
runtime_memory_alloc(size_t size)
{
	return smalloc(&runtime_alloc, size);
}

void
runtime_memory_free(void *ptr, size_t size)
{
	smfree(&runtime_alloc, ptr, size);
}

/* {{{ tuple_field_* getters */

int
tuple_field_go_to_index(const char **field, uint64_t index)
{
	enum mp_type type = mp_typeof(**field);
	if (type == MP_ARRAY) {
		uint32_t count = mp_decode_array(field);
		if (index >= count)
			return -1;
		for (; index > 0; --index)
			mp_next(field);
		return 0;
	} else if (type == MP_MAP) {
		index += TUPLE_INDEX_BASE;
		uint64_t count = mp_decode_map(field);
		for (; count > 0; --count) {
			type = mp_typeof(**field);
			if (type == MP_UINT) {
				uint64_t value = mp_decode_uint(field);
				if (value == index)
					return 0;
			} else if (type == MP_INT) {
				int64_t value = mp_decode_int(field);
				if (value >= 0 && (uint64_t)value == index)
					return 0;
			} else {
				/* Skip key. */
				mp_next(field);
			}
			/* Skip value. */
			mp_next(field);
		}
	}
	return -1;
}

int
tuple_field_go_to_key(const char **field, const char *key, int len)
{
	enum mp_type type = mp_typeof(**field);
	if (type != MP_MAP)
		return -1;
	uint64_t count = mp_decode_map(field);
	for (; count > 0; --count) {
		type = mp_typeof(**field);
		if (type == MP_STR) {
			uint32_t value_len;
			const char *value = mp_decode_str(field, &value_len);
			if (value_len == (uint)len &&
			    memcmp(value, key, len) == 0)
				return 0;
		} else {
			/* Skip key. */
			mp_next(field);
		}
		/* Skip value. */
		mp_next(field);
	}
	return -1;
}

int
tuple_go_to_path(const char **data, const char *path, uint32_t path_len,
		 int index_base, int multikey_idx)
{
	int rc;
	struct json_lexer lexer;
	struct json_token token;
	json_lexer_create(&lexer, path, path_len, index_base);
	while ((rc = json_lexer_next_token(&lexer, &token)) == 0) {
		switch (token.type) {
		case JSON_TOKEN_ANY:
			if (multikey_idx == MULTIKEY_NONE)
				return -1;
			token.num = multikey_idx;
			FALLTHROUGH;
		case JSON_TOKEN_NUM:
			rc = tuple_field_go_to_index(data, token.num);
			break;
		case JSON_TOKEN_STR:
			rc = tuple_field_go_to_key(data, token.str, token.len);
			break;
		default:
			assert(token.type == JSON_TOKEN_END);
			return 0;
		}
		if (rc != 0) {
			*data = NULL;
			return 0;
		}
	}
	return rc != 0 ? -1 : 0;
}

const char *
tuple_field_raw_by_full_path(struct tuple_format *format, const char *tuple,
			     const uint32_t *field_map, const char *path,
			     uint32_t path_len, uint32_t path_hash,
			     int index_base)
{
	assert(path_len > 0);
	uint32_t fieldno;
	/*
	 * It is possible, that a field has a name as
	 * well-formatted JSON. For example 'a.b.c.d' or '[1]' can
	 * be field name. To save compatibility at first try to
	 * use the path as a field name.
	 */
	if (tuple_fieldno_by_name(format->dict, path, path_len, path_hash,
				  &fieldno) == 0)
		return tuple_field_raw(format, tuple, field_map, fieldno);
	struct json_lexer lexer;
	struct json_token token;
	json_lexer_create(&lexer, path, path_len, index_base);
	if (json_lexer_next_token(&lexer, &token) != 0)
		return NULL;
	switch(token.type) {
	case JSON_TOKEN_NUM: {
		fieldno = token.num;
		break;
	}
	case JSON_TOKEN_STR: {
		/* First part of a path is a field name. */
		uint32_t name_hash;
		if (path_len == (uint32_t) token.len) {
			name_hash = path_hash;
		} else {
			/*
			 * If a string is "field....", then its
			 * precalculated juajit hash can not be
			 * used. A tuple dictionary hashes only
			 * name, not path.
			 */
			name_hash = field_name_hash(token.str, token.len);
		}
		if (tuple_fieldno_by_name(format->dict, token.str, token.len,
					  name_hash, &fieldno) != 0)
			return NULL;
		break;
	}
	default:
		assert(token.type == JSON_TOKEN_END ||
		       token.type == JSON_TOKEN_ANY);
		return NULL;
	}
	return tuple_field_raw_by_path(format, tuple, field_map, fieldno,
				       path + lexer.offset,
				       path_len - lexer.offset,
				       index_base, NULL, MULTIKEY_NONE);
}

uint32_t
tuple_raw_multikey_count(struct tuple_format *format, const char *data,
			       const uint32_t *field_map,
			       struct key_def *key_def)
{
	assert(key_def->is_multikey);
	const char *array_raw =
		tuple_field_raw_by_path(format, data, field_map,
					key_def->multikey_fieldno,
					key_def->multikey_path,
					key_def->multikey_path_len,
					TUPLE_INDEX_BASE,
					NULL, MULTIKEY_NONE);
	if (array_raw == NULL)
		return 0;
	enum mp_type type = mp_typeof(*array_raw);
	if (type == MP_NIL)
		return 0;
	assert(type == MP_ARRAY);
	return mp_decode_array(&array_raw);
}

/* }}} tuple_field_* getters */

/* {{{ box_tuple_* */

box_tuple_format_t *
box_tuple_format_default(void)
{
	return tuple_format_runtime;
}

box_tuple_format_t *
box_tuple_format_new(struct key_def **keys, uint16_t key_count)
{
	box_tuple_format_t *format =
		simple_tuple_format_new(&tuple_format_runtime_vtab,
					NULL, keys, key_count);
	if (format != NULL)
		tuple_format_ref(format);
	return format;
}

int
box_tuple_ref(box_tuple_t *tuple)
{
	assert(tuple != NULL);
	tuple_ref(tuple);
	return 0;
}

void
box_tuple_unref(box_tuple_t *tuple)
{
	assert(tuple != NULL);
	return tuple_unref(tuple);
}

uint32_t
box_tuple_field_count(box_tuple_t *tuple)
{
	assert(tuple != NULL);
	return tuple_field_count(tuple);
}

size_t
box_tuple_bsize(box_tuple_t *tuple)
{
	assert(tuple != NULL);
	return tuple_bsize(tuple);
}

ssize_t
tuple_to_buf(struct tuple *tuple, char *buf, size_t size)
{
	uint32_t bsize;
	const char *data = tuple_data_range(tuple, &bsize);
	if (likely(bsize <= size)) {
		memcpy(buf, data, bsize);
	}
	return bsize;
}

ssize_t
box_tuple_to_buf(box_tuple_t *tuple, char *buf, size_t size)
{
	assert(tuple != NULL);
	return tuple_to_buf(tuple, buf, size);
}

box_tuple_format_t *
box_tuple_format(box_tuple_t *tuple)
{
	assert(tuple != NULL);
	return tuple_format(tuple);
}

const char *
box_tuple_field(box_tuple_t *tuple, uint32_t fieldno)
{
	assert(tuple != NULL);
	return tuple_field(tuple, fieldno);
}

const char *
box_tuple_field_by_path(box_tuple_t *tuple, const char *path,
			uint32_t path_len, int index_base)
{
	assert(tuple != NULL);
	assert(path != NULL);

	if (path_len == 0)
		return NULL;

	uint32_t path_hash = field_name_hash(path, path_len);
	return tuple_field_raw_by_full_path(tuple_format(tuple),
					    tuple_data(tuple),
					    tuple_field_map(tuple),
					    path, path_len, path_hash,
					    index_base);
}

typedef struct tuple_iterator box_tuple_iterator_t;

box_tuple_iterator_t *
box_tuple_iterator(box_tuple_t *tuple)
{
	assert(tuple != NULL);
	struct tuple_iterator *it = (struct tuple_iterator *)
		mempool_alloc(&tuple_iterator_pool);
	if (it == NULL) {
		diag_set(OutOfMemory, tuple_iterator_pool.objsize,
			 "mempool", "new slab");
		return NULL;
	}
	tuple_ref(tuple);
	tuple_rewind(it, tuple);
	return it;
}

void
box_tuple_iterator_free(box_tuple_iterator_t *it)
{
	tuple_unref(it->tuple);
	mempool_free(&tuple_iterator_pool, it);
}

uint32_t
box_tuple_position(box_tuple_iterator_t *it)
{
	return it->fieldno;
}

void
box_tuple_rewind(box_tuple_iterator_t *it)
{
	tuple_rewind(it, it->tuple);
}

const char *
box_tuple_seek(box_tuple_iterator_t *it, uint32_t fieldno)
{
	return tuple_seek(it, fieldno);
}

const char *
box_tuple_next(box_tuple_iterator_t *it)
{
	return tuple_next(it);
}

box_tuple_t *
box_tuple_update(box_tuple_t *tuple, const char *expr, const char *expr_end)
{
	uint32_t new_size = 0, bsize;
	const char *old_data = tuple_data_range(tuple, &bsize);
	struct region *region = &fiber()->gc;
	size_t used = region_used(region);
	struct tuple_format *format = tuple_format(tuple);
	const char *new_data =
		xrow_update_execute(expr, expr_end, old_data, old_data + bsize,
				    format, &new_size, 1, NULL);
	if (new_data == NULL)
		return NULL;
	struct tuple *ret = tuple_new(format, new_data, new_data + new_size);
	region_truncate(region, used);
	if (ret != NULL)
		return tuple_bless(ret);
	return NULL;
}

box_tuple_t *
box_tuple_upsert(box_tuple_t *tuple, const char *expr, const char *expr_end)
{
	uint32_t new_size = 0, bsize;
	const char *old_data = tuple_data_range(tuple, &bsize);
	struct region *region = &fiber()->gc;
	size_t used = region_used(region);
	struct tuple_format *format = tuple_format(tuple);
	const char *new_data =
		xrow_upsert_execute(expr, expr_end, old_data, old_data + bsize,
				    format, &new_size, 1, false, NULL);
	if (new_data == NULL)
		return NULL;

	struct tuple *ret = tuple_new(format, new_data, new_data + new_size);
	region_truncate(region, used);
	if (ret != NULL)
		return tuple_bless(ret);
	return NULL;
}

box_tuple_t *
box_tuple_new(box_tuple_format_t *format, const char *data, const char *end)
{
	struct tuple *ret = tuple_new(format, data, end);
	if (ret == NULL)
		return NULL;
	return tuple_bless(ret);
}

int
box_tuple_validate(box_tuple_t *tuple, box_tuple_format_t *format)
{
	return tuple_validate(format, tuple);
}

/* }}} box_tuple_* */

int
tuple_snprint(char *buf, int size, struct tuple *tuple)
{
	int total = 0;
	if (tuple == NULL) {
		SNPRINT(total, snprintf, buf, size, "<NULL>");
		return total;
	}
	SNPRINT(total, mp_snprint, buf, size, tuple_data(tuple));
	return total;
}

const char *
tuple_str(struct tuple *tuple)
{
	char *buf = tt_static_buf();
	if (tuple_snprint(buf, TT_STATIC_BUF_LEN, tuple) < 0)
		return "<failed to format tuple>";
	return buf;
}

const char *
mp_str(const char *data)
{
	char *buf = tt_static_buf();
	if (mp_snprint(buf, TT_STATIC_BUF_LEN, data) < 0)
		return "<failed to format message pack>";
	return buf;
}
