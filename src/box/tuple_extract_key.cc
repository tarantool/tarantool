#include "tuple_extract_key.h"
#include "tuple.h"
#include "fiber.h"

/**
 * Optimized version of tuple_extract_key_raw() for sequential key defs
 * @copydoc tuple_extract_key_raw()
 */
static char *
tuple_extract_key_sequential_raw(const char *data, const char *data_end,
				 const struct key_def *key_def,
				 uint32_t *key_size)
{
	assert(key_def_is_sequential(key_def));
	const char *field_start = data;
	uint32_t bsize = mp_sizeof_array(key_def->part_count);

	mp_decode_array(&field_start);
	const char *field_end = field_start;

	for (uint32_t i = 0; i < key_def->part_count; i++)
		mp_next(&field_end);
	bsize += field_end - field_start;

	assert(!data_end || (field_end - field_start <= data_end - data));
	(void) data_end;

	char *key = (char *) region_alloc(&fiber()->gc, bsize);
	if (key == NULL) {
		diag_set(OutOfMemory, bsize, "region",
			"tuple_extract_key_raw_sequential");
		return NULL;
	}
	char *key_buf = mp_encode_array(key, key_def->part_count);
	memcpy(key_buf, field_start, field_end - field_start);

	if (key_size != NULL)
		*key_size = bsize;
	return key;
}

/**
 * Optimized version of tuple_extract_key() for sequential key defs
 * @copydoc tuple_extract_key()
 */
static inline char *
tuple_extract_key_sequential(const struct tuple *tuple,
			     const struct key_def *key_def,
			     uint32_t *key_size)
{
	assert(key_def_is_sequential(key_def));
	const char *data = tuple_data(tuple);
	return tuple_extract_key_sequential_raw(data, NULL, key_def, key_size);
}

/**
 * General-purpose implementation of tuple_extract_key()
 * @copydoc tuple_extract_key()
 */
template <bool contains_sequential_parts>
static char *
tuple_extract_key_slowpath(const struct tuple *tuple,
			   const struct key_def *key_def, uint32_t *key_size)
{
	const char *data = tuple_data(tuple);
	uint32_t part_count = key_def->part_count;
	uint32_t bsize = mp_sizeof_array(part_count);
	const struct tuple_format *format = tuple_format(tuple);
	const uint32_t *field_map = tuple_field_map(tuple);

	/* Calculate the key size. */
	for (uint32_t i = 0; i < part_count; ++i) {
		const char *field =
			tuple_field_raw(format, data, field_map,
					key_def->parts[i].fieldno);
		const char *end = field;
		if (contains_sequential_parts) {
			/*
			 * Skip sequential part in order to
			 * minimize tuple_field_raw() calls.
			 */
			for (; i < key_def->part_count - 1; i++) {
				if (key_def->parts[i].fieldno + 1 !=
					key_def->parts[i + 1].fieldno) {
					/*
					 * End of sequential part.
					 */
					break;
				}
				mp_next(&end);
			}
		}
		mp_next(&end);
		bsize += end - field;
	}

	char *key = (char *) region_alloc(&fiber()->gc, bsize);
	if (key == NULL) {
		diag_set(OutOfMemory, bsize, "region", "tuple_extract_key");
		return NULL;
	}
	char *key_buf = mp_encode_array(key, part_count);
	for (uint32_t i = 0; i < part_count; ++i) {
		const char *field =
			tuple_field_raw(format, data, field_map,
					key_def->parts[i].fieldno);
		const char *end = field;
		if (contains_sequential_parts) {
			/*
			 * Skip sequential part in order to
			 * minimize tuple_field_raw() calls.
			 */
			for (; i < key_def->part_count - 1; i++) {
				if (key_def->parts[i].fieldno + 1 !=
					key_def->parts[i + 1].fieldno) {
					/*
					 * End of sequential part.
					 */
					break;
				}
				mp_next(&end);
			}
		}
		mp_next(&end);
		bsize = end - field;
		memcpy(key_buf, field, bsize);
		key_buf += bsize;
	}
	if (key_size != NULL)
		*key_size = key_buf - key;
	return key;
}

/**
 * General-purpose version of tuple_extract_key_raw()
 * @copydoc tuple_extract_key_raw()
 */
static char *
tuple_extract_key_slowpath_raw(const char *data, const char *data_end,
			       const struct key_def *key_def,
			       uint32_t *key_size)
{
	/* allocate buffer with maximal possible size */
	char *key = (char *) region_alloc(&fiber()->gc, data_end - data);
	if (key == NULL) {
		diag_set(OutOfMemory, data_end - data, "region",
			 "tuple_extract_key_raw");
		return NULL;
	}
	char *key_buf = mp_encode_array(key, key_def->part_count);
	const char *field0 = data;
	mp_decode_array(&field0);
	const char *field0_end = field0;
	mp_next(&field0_end);
	const char *field = field0;
	const char *field_end = field0_end;
	uint32_t current_fieldno = 0;
	for (uint32_t i = 0; i < key_def->part_count; i++) {
		uint32_t fieldno = key_def->parts[i].fieldno;
		for (; i < key_def->part_count - 1; i++) {
			if (key_def->parts[i].fieldno + 1 !=
			    key_def->parts[i + 1].fieldno)
				break;
		}
		if (fieldno < current_fieldno) {
			/* Rewind. */
			field = field0;
			field_end = field0_end;
			current_fieldno = 0;
		}

		while (current_fieldno < fieldno) {
			/* search first field of key in tuple raw data */
			field = field_end;
			mp_next(&field_end);
			current_fieldno++;
		}

		while (current_fieldno < key_def->parts[i].fieldno) {
			/* search the last field in subsequence */
			mp_next(&field_end);
			current_fieldno++;
		}
		memcpy(key_buf, field, field_end - field);
		key_buf += field_end - field;
		assert(key_buf - key <= data_end - data);
	}
	if (key_size != NULL)
		*key_size = (uint32_t)(key_buf - key);
	return key;
}

/** True, if a key con contain two or more parts in sequence. */
static bool
key_def_contains_sequential_parts(struct key_def *def)
{
	for (uint32_t i = 0; i < def->part_count - 1; ++i) {
		if (def->parts[i].fieldno + 1 == def->parts[i + 1].fieldno)
			return true;
	}
	return false;
}

/**
 * Initialize tuple_extract_key() and tuple_extract_key_raw()
 */
void
tuple_extract_key_set(struct key_def *key_def)
{
	if (key_def_is_sequential(key_def)) {
		key_def->tuple_extract_key = tuple_extract_key_sequential;
		key_def->tuple_extract_key_raw = tuple_extract_key_sequential_raw;
	} else {
		if (key_def_contains_sequential_parts(key_def)) {
			key_def->tuple_extract_key =
				tuple_extract_key_slowpath<true>;
		} else {
			key_def->tuple_extract_key =
				tuple_extract_key_slowpath<false>;
		}
		key_def->tuple_extract_key_raw = tuple_extract_key_slowpath_raw;
	}
}
