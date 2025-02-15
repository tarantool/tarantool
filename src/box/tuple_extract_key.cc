#include "tuple_extract_key.h"
#include "tuple.h"
#include "fiber.h"

enum { MSGPACK_NULL = 0xc0 };

/** True if key part i and i+1 are sequential. */
template <bool has_json_paths>
static inline bool
key_def_parts_are_sequential(const struct key_def *def, int i)
{
	const struct key_part *part1 = &def->parts[i];
	const struct key_part *part2 = &def->parts[i + 1];
	if (!has_json_paths) {
		return part1->fieldno + 1 == part2->fieldno;
	} else {
		return part1->fieldno + 1 == part2->fieldno &&
		       part1->path == NULL && part2->path == NULL;
	}
}

/** True, if a key con contain two or more parts in sequence. */
static bool
key_def_contains_sequential_parts(const struct key_def *def)
{
	for (uint32_t i = 0; i < def->part_count - 1; ++i) {
		if (key_def_parts_are_sequential<true>(def, i))
			return true;
	}
	return false;
}

/**
 * Optimized version of tuple_extract_key_raw() for sequential key defs
 * @copydoc tuple_extract_key_raw()
 */
template <bool has_optional_parts>
static char *
tuple_extract_key_sequential_raw(const char *data, const char *data_end,
				 struct key_def *key_def, int multikey_idx,
				 uint32_t *key_size, struct region *region)
{
	(void)multikey_idx;
	assert(!has_optional_parts || key_def->is_nullable);
	assert(key_def_is_sequential(key_def));
	assert(has_optional_parts == key_def->has_optional_parts);
	assert(data_end != NULL);
	assert(mp_sizeof_nil() == 1);
	const char *field_start = data;
	uint32_t bsize = mp_sizeof_array(key_def->part_count);
	uint32_t field_count = mp_decode_array(&field_start);
	const char *field_end = field_start;
	uint32_t null_count;
	if (!has_optional_parts || field_count > key_def->part_count) {
		for (uint32_t i = 0; i < key_def->part_count; i++)
			mp_next(&field_end);
		null_count = 0;
	} else {
		assert(key_def->is_nullable);
		null_count = key_def->part_count - field_count;
		field_end = data_end;
		bsize += null_count * mp_sizeof_nil();
	}
	assert(field_end - field_start <= data_end - data);
	bsize += field_end - field_start;

	char *key = (char *)region_alloc(region, bsize);
	if (key == NULL) {
		diag_set(OutOfMemory, bsize, "region",
			"tuple_extract_key_raw_sequential");
		return NULL;
	}
	char *key_buf = mp_encode_array(key, key_def->part_count);
	memcpy(key_buf, field_start, field_end - field_start);
	if (has_optional_parts && null_count > 0) {
		key_buf += field_end - field_start;
		memset(key_buf, MSGPACK_NULL, null_count);
	}

	if (key_size != NULL)
		*key_size = bsize;
	return key;
}

/**
 * Optimized version of tuple_extract_key() for sequential key defs
 * @copydoc tuple_extract_key()
 */
template <bool has_optional_parts>
static inline char *
tuple_extract_key_sequential(struct tuple *tuple, struct key_def *key_def,
			     int multikey_idx, uint32_t *key_size,
			     struct region *region)
{
	assert(key_def_is_sequential(key_def));
	assert(!has_optional_parts || key_def->is_nullable);
	assert(has_optional_parts == key_def->has_optional_parts);
	const char *data = tuple_data(tuple);
	const char *data_end = data + tuple_bsize(tuple);
	return tuple_extract_key_sequential_raw<has_optional_parts>(data,
								    data_end,
								    key_def,
								    multikey_idx,
								    key_size,
								    region);
}

/**
 * General-purpose implementation of tuple_extract_key()
 * @copydoc tuple_extract_key()
 */
template <bool contains_sequential_parts, bool has_optional_parts,
	  bool has_json_paths, bool is_multikey>
static char *
tuple_extract_key_slowpath(struct tuple *tuple, struct key_def *key_def,
			   int multikey_idx, uint32_t *key_size,
			   struct region *region)
{
	assert(has_json_paths == key_def->has_json_paths);
	assert(!has_optional_parts || key_def->is_nullable);
	assert(has_optional_parts == key_def->has_optional_parts);
	assert(contains_sequential_parts ==
	       key_def_contains_sequential_parts(key_def));
	assert(is_multikey == key_def->is_multikey);
	assert(!key_def->is_multikey || multikey_idx != MULTIKEY_NONE);
	assert(!key_def->for_func_index);
	assert(mp_sizeof_nil() == 1);
	const char *data = tuple_data(tuple);
	uint32_t part_count = key_def->part_count;
	uint32_t bsize = mp_sizeof_array(part_count);
	struct tuple_format *format = tuple_format(tuple);
	const char *field_map = tuple_field_map(tuple);
	const char *tuple_end = data + tuple_bsize(tuple);

	/* Calculate the key size. */
	for (uint32_t i = 0; i < part_count; ++i) {
		const char *field;
		if (!has_json_paths) {
			field = tuple_field_raw(format, data, field_map,
						key_def->parts[i].fieldno);
		} else if (!is_multikey) {
			field = tuple_field_raw_by_part(format, data, field_map,
							&key_def->parts[i],
							MULTIKEY_NONE);
		} else {
			field = tuple_field_raw_by_part(format, data, field_map,
							&key_def->parts[i],
							multikey_idx);
		}
		if (has_optional_parts && field == NULL) {
			bsize += mp_sizeof_nil();
			continue;
		}
		assert(field != NULL);
		const char *end = field;
		if (contains_sequential_parts) {
			/*
			 * Skip sequential part in order to
			 * minimize tuple_field_raw() calls.
			 */
			for (; i < part_count - 1; i++) {
				if (!key_def_parts_are_sequential
						<has_json_paths>(key_def, i)) {
					/*
					 * End of sequential part.
					 */
					break;
				}
				if (!has_optional_parts || end < tuple_end)
					mp_next(&end);
				else
					bsize += mp_sizeof_nil();
			}
		}
		if (!has_optional_parts || end < tuple_end)
			mp_next(&end);
		else
			bsize += mp_sizeof_nil();
		bsize += end - field;
	}

	char *key = (char *)region_alloc(region, bsize);
	if (key == NULL) {
		diag_set(OutOfMemory, bsize, "region", "tuple_extract_key");
		return NULL;
	}
	char *key_buf = mp_encode_array(key, part_count);
	for (uint32_t i = 0; i < part_count; ++i) {
		const char *field;
		if (!has_json_paths) {
			field = tuple_field_raw(format, data, field_map,
						key_def->parts[i].fieldno);
		} else if (!is_multikey) {
			field = tuple_field_raw_by_part(format, data, field_map,
							&key_def->parts[i],
							MULTIKEY_NONE);
		} else {
			field = tuple_field_raw_by_part(format, data, field_map,
							&key_def->parts[i],
							multikey_idx);
		}
		if (has_optional_parts && field == NULL) {
			key_buf = mp_encode_nil(key_buf);
			continue;
		}
		const char *end = field;
		uint32_t null_count = 0;
		if (contains_sequential_parts) {
			/*
			 * Skip sequential part in order to
			 * minimize tuple_field_raw() calls.
			 */
			for (; i < part_count - 1; i++) {
				if (!key_def_parts_are_sequential
						<has_json_paths>(key_def, i)) {
					/*
					 * End of sequential part.
					 */
					break;
				}
				if (!has_optional_parts || end < tuple_end)
					mp_next(&end);
				else
					++null_count;
			}
		}
		if (!has_optional_parts || end < tuple_end)
			mp_next(&end);
		else
			++null_count;
		bsize = end - field;
		memcpy(key_buf, field, bsize);
		key_buf += bsize;
		if (has_optional_parts && null_count != 0) {
			memset(key_buf, MSGPACK_NULL, null_count);
			key_buf += null_count * mp_sizeof_nil();
		}
	}
	if (key_size != NULL)
		*key_size = key_buf - key;
	return key;
}

/**
 * General-purpose version of tuple_extract_key_raw()
 * @copydoc tuple_extract_key_raw()
 */
template <bool has_optional_parts, bool has_json_paths>
static char *
tuple_extract_key_slowpath_raw(const char *data, const char *data_end,
			       struct key_def *key_def, int multikey_idx,
			       uint32_t *key_size, struct region *region)
{
	assert(has_json_paths == key_def->has_json_paths);
	assert(!has_optional_parts || key_def->is_nullable);
	assert(has_optional_parts == key_def->has_optional_parts);
	assert(!key_def->is_multikey || multikey_idx != MULTIKEY_NONE);
	assert(!key_def->for_func_index);
	assert(mp_sizeof_nil() == 1);
	/* allocate buffer with maximal possible size */
	uint32_t potential_null_count = key_def->is_nullable ?
		key_def->part_count : 0;
	uint32_t alloc_size = (uint32_t)(data_end - data) +
		potential_null_count * mp_sizeof_nil();
	char *key = (char *)region_alloc(region, alloc_size);
	if (key == NULL) {
		diag_set(OutOfMemory, alloc_size, "region",
			 "tuple_extract_key_raw");
		return NULL;
	}
	char *key_buf = mp_encode_array(key, key_def->part_count);
	const char *field0 = data;
	uint32_t field_count = mp_decode_array(&field0);
	/*
	 * A tuple can not be empty - at least a pk always exists.
	 */
	assert(field_count > 0);
	(void) field_count;
	const char *field0_end = field0;
	mp_next(&field0_end);
	const char *field = field0;
	const char *field_end = field0_end;
	uint32_t current_fieldno = 0;
	for (uint32_t i = 0; i < key_def->part_count; i++) {
		uint32_t fieldno = key_def->parts[i].fieldno;
		uint32_t null_count = 0;
		for (; i < key_def->part_count - 1; i++) {
			if (!key_def_parts_are_sequential
					<has_json_paths>(key_def, i))
				break;
		}
		const struct key_part *part = &key_def->parts[i];
		uint32_t end_fieldno = part->fieldno;

		if (fieldno < current_fieldno) {
			/* Rewind. */
			field = field0;
			field_end = field0_end;
			current_fieldno = 0;
		}

		/*
		 * First fieldno in a key columns can be out of
		 * tuple size for nullable indexes because of
		 * absense of indexed fields. Treat such fields
		 * as NULLs.
		 */
		if (has_optional_parts && fieldno >= field_count) {
			/* Nullify entire columns range. */
			null_count = end_fieldno - fieldno + 1;
			memset(key_buf, MSGPACK_NULL, null_count);
			key_buf += null_count * mp_sizeof_nil();
			continue;
		}
		while (current_fieldno < fieldno) {
			/* search first field of key in tuple raw data */
			field = field_end;
			mp_next(&field_end);
			current_fieldno++;
		}

		/*
		 * If the last fieldno is out of tuple size, then
		 * fill rest of columns with NULLs.
		 */
		if (has_optional_parts && end_fieldno >= field_count) {
			null_count = end_fieldno - field_count + 1;
			field_end = data_end;
		} else {
			while (current_fieldno < end_fieldno) {
				mp_next(&field_end);
				current_fieldno++;
			}
		}
		const char *src = field;
		const char *src_end = field_end;
		if (has_json_paths && part->path != NULL) {
			if (tuple_go_to_path(&src, part->path, part->path_len,
					     TUPLE_INDEX_BASE,
					     multikey_idx) != 0) {
				/*
				 * The path must be correct as
				 * it has already been validated
				 * in key_def_decode_parts.
				 */
				unreachable();
			}
			assert(src != NULL || has_optional_parts);
			if (has_optional_parts && src == NULL) {
				null_count += 1;
				src = src_end;
			} else {
				src_end = src;
				mp_next(&src_end);
			}
		}
		memcpy(key_buf, src, src_end - src);
		key_buf += src_end - src;
		if (has_optional_parts && null_count != 0) {
			memset(key_buf, MSGPACK_NULL, null_count);
			key_buf += null_count * mp_sizeof_nil();
		}
	}
	assert(key_buf - key <= alloc_size);
	if (key_size != NULL)
		*key_size = (uint32_t)(key_buf - key);
	return key;
}

/**
 * Initialize tuple_extract_key() and tuple_extract_key_raw()
 */
template<bool contains_sequential_parts, bool has_optional_parts>
static void
key_def_set_extract_func_plain(struct key_def *def)
{
	assert(!def->has_json_paths);
	assert(!def->is_multikey);
	assert(!def->for_func_index);
	if (key_def_is_sequential(def)) {
		assert(contains_sequential_parts || def->part_count == 1);
		def->tuple_extract_key = tuple_extract_key_sequential
					<has_optional_parts>;
		def->tuple_extract_key_raw = tuple_extract_key_sequential_raw
					<has_optional_parts>;
	} else {
		def->tuple_extract_key = tuple_extract_key_slowpath
					<contains_sequential_parts,
					 has_optional_parts, false, false>;
		def->tuple_extract_key_raw = tuple_extract_key_slowpath_raw
					<has_optional_parts, false>;
	}
}

template<bool contains_sequential_parts, bool has_optional_parts>
static void
key_def_set_extract_func_json(struct key_def *def)
{
	assert(def->has_json_paths);
	assert(!def->for_func_index);
	if (def->is_multikey) {
		def->tuple_extract_key = tuple_extract_key_slowpath
					<contains_sequential_parts,
					 has_optional_parts, true, true>;
	} else {
		def->tuple_extract_key = tuple_extract_key_slowpath
					<contains_sequential_parts,
					 has_optional_parts, true, false>;
	}
	def->tuple_extract_key_raw = tuple_extract_key_slowpath_raw
					<has_optional_parts, true>;
}

static char *
tuple_extract_key_stub(struct tuple *tuple, struct key_def *key_def,
		       int multikey_idx, uint32_t *key_size,
		       struct region *region)
{
	(void)tuple; (void)key_def; (void)multikey_idx; (void)key_size;
	(void)region;
	unreachable();
	return NULL;
}

static char *
tuple_extract_key_raw_stub(const char *data, const char *data_end,
			   struct key_def *key_def, int multikey_idx,
			   uint32_t *key_size, struct region *region)
{
	(void)data; (void)data_end;
	(void)key_def; (void)multikey_idx; (void)key_size;
	(void)region;
	unreachable();
	return NULL;
}

void
key_def_set_extract_func(struct key_def *key_def)
{
	bool contains_sequential_parts =
		key_def_contains_sequential_parts(key_def);
	bool has_optional_parts = key_def->has_optional_parts;
	if (key_def->for_func_index) {
		key_def->tuple_extract_key = tuple_extract_key_stub;
		key_def->tuple_extract_key_raw = tuple_extract_key_raw_stub;
	} else if (!key_def->has_json_paths) {
		if (!contains_sequential_parts && !has_optional_parts) {
			key_def_set_extract_func_plain<false, false>(key_def);
		} else if (!contains_sequential_parts && has_optional_parts) {
			key_def_set_extract_func_plain<false, true>(key_def);
		} else if (contains_sequential_parts && !has_optional_parts) {
			key_def_set_extract_func_plain<true, false>(key_def);
		} else {
			assert(contains_sequential_parts && has_optional_parts);
			key_def_set_extract_func_plain<true, true>(key_def);
		}
	} else {
		if (!contains_sequential_parts && !has_optional_parts) {
			key_def_set_extract_func_json<false, false>(key_def);
		} else if (!contains_sequential_parts && has_optional_parts) {
			key_def_set_extract_func_json<false, true>(key_def);
		} else if (contains_sequential_parts && !has_optional_parts) {
			key_def_set_extract_func_json<true, false>(key_def);
		} else {
			assert(contains_sequential_parts && has_optional_parts);
			key_def_set_extract_func_json<true, true>(key_def);
		}
	}
}

bool
tuple_key_contains_null(struct tuple *tuple, struct key_def *def,
			int multikey_idx)
{
	struct tuple_format *format = tuple_format(tuple);
	const char *data = tuple_data(tuple);
	const char *field_map = tuple_field_map(tuple);
	for (struct key_part *part = def->parts, *end = part + def->part_count;
	     part < end; ++part) {
		const char *field = tuple_field_raw_by_part(format, data,
							    field_map, part,
							    multikey_idx);
		if (field == NULL || mp_typeof(*field) == MP_NIL)
			return true;
	}
	return false;
}

bool
tuple_key_is_excluded_slow(struct tuple *tuple, struct key_def *def,
			   int multikey_idx)
{
	assert(def->has_exclude_null);
	struct tuple_format *format = tuple_format(tuple);
	const char *data = tuple_data(tuple);
	const char *field_map = tuple_field_map(tuple);
	for (struct key_part *part = def->parts, *end = part + def->part_count;
	     part < end; ++part) {
		if (!part->exclude_null)
			continue;
		const char *field = tuple_field_raw_by_part(format, data,
							    field_map, part,
							    multikey_idx);
		if (field == NULL || mp_typeof(*field) == MP_NIL)
			return true;
	}
	return false;
}

/** Validate tuple field against key part. */
static int
tuple_validate_field(const char *field, struct key_part *part,
		     uint32_t field_no)
{
	if (field == NULL) {
		if (key_part_is_nullable(part))
			return 0;
		diag_set(ClientError, ER_FIELD_MISSING,
			 tt_sprintf("[%d]%.*s",
				    part->fieldno + TUPLE_INDEX_BASE,
				    part->path_len, part->path));
		return -1;
	}
	if (key_part_validate(part->type, field, field_no,
			      key_part_is_nullable(part)) != 0)
		return -1;
	return 0;
}

int
tuple_validate_key_parts(struct key_def *key_def, struct tuple *tuple)
{
	assert(!key_def->is_multikey);
	for (uint32_t idx = 0; idx < key_def->part_count; idx++) {
		struct key_part *part = &key_def->parts[idx];
		const char *field = tuple_field_by_part(tuple, part,
							MULTIKEY_NONE);
		if (tuple_validate_field(field, part, idx) != 0)
			return -1;
	}
	return 0;
}

int
tuple_validate_key_parts_raw(struct key_def *key_def, const char *tuple)
{
	assert(!key_def->is_multikey);
	struct key_part *part = NULL;
	const char *field = NULL;
	uint32_t field_count = mp_decode_array(&tuple);
	for (uint32_t idx = 0; idx < key_def->part_count; idx++) {
		part = &key_def->parts[idx];
		field = NULL;
		if (part->fieldno < field_count) {
			field = tuple;
			for (uint32_t k = 0; k < part->fieldno; k++)
				mp_next(&field);
			if (part->path != NULL &&
			    tuple_go_to_path(&field, part->path,
					     part->path_len, TUPLE_INDEX_BASE,
					     MULTIKEY_NONE) != 0)
				return -1;
		}
		if (tuple_validate_field(field, part, idx) != 0)
			return -1;
	}
	return 0;
}
