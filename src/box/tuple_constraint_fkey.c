/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "tuple_constraint_fkey.h"

#include "engine.h"
#include "space.h"
#include "space_cache.h"
#include "tuple.h"
#include "tuple_format.h"
#include "tuple_constraint.h"
#include "tt_static.h"
#include "trivia/util.h"

/** Static buffer size for extraction of complex keys from tuples. */
enum {
	COMPLEX_KEY_BUFFER_SIZE = 4096,
};

/** Static buffer for extraction of complex keys from tuples. */
static char complex_key_buffer[COMPLEX_KEY_BUFFER_SIZE];

/**
 * Find field number in @a space by field def in constraint.
 * Return -1 if not found.
 */
static int32_t
find_field_no_by_def(struct space *space,
		     const struct tuple_constraint_field_id *field_def)
{
	/* Simple case. */
	if (field_def->name_len == 0)
		return field_def->id;
	/* A bit more complicated case - find by name. */
	uint32_t field_no;
	uint32_t hash = field_name_hash(field_def->name, field_def->name_len);
	if (tuple_fieldno_by_name(space->def->dict, field_def->name,
				  field_def->name_len, hash, &field_no) != 0)
		return -1;
	return field_no;
}

/**
 * Common version of fkey_update_foreign_index amd fkey_update_local_index.
 * Find and set foreign/local (@a is_foreign) index by set of foreign/local
 * fields in foreign key constraint. Save also order of foreign/local field
 * pairs in the index. Return that index number (-1 in not found).
 * Foreign version looks for unique index.
 */
static void
fkey_update_index_common(struct tuple_constraint *constr, bool is_foreign)
{
	struct space *space = is_foreign ? constr->space_cache_holder.space
					 : constr->space;
	int32_t *index = is_foreign ? &constr->fkey->foreign_index
				    : &constr->fkey->local_index;
	*index = -1;
	uint32_t field_count = constr->fkey->field_count;
	for (uint32_t i = 0; i < space->index_count; i++) {
		struct index *idx = space->index[i];
		if (idx->def->key_def->part_count != field_count)
			continue;
		if (is_foreign && !idx->def->opts.is_unique)
			continue;
		struct key_part *parts = idx->def->key_def->parts;

		/*
		 * Just check that sets parts[*].fieldno and
		 * constr->fkey->field[*].X_field are the same (except order).
		 */
		uint32_t j;
		for (j = 0; j < field_count; j++) {
			int32_t field_no = is_foreign ?
				constr->fkey->data[j].foreign_field_no :
				constr->fkey->data[j].local_field_no;
			assert(field_no >= 0);
			uint32_t k;
			for (k = 0; k < field_count; k++)
				if (parts[k].fieldno == (uint32_t)field_no)
					break;
			if (k == field_count)
				break; /* Not found. */
			int16_t *order;
			order = is_foreign ?
				&constr->fkey->data[k].foreign_index_order :
				&constr->fkey->data[k].local_index_order;
			*order = j;
		}
		if (j != field_count)
			continue; /* Not all found. */
		*index = i;
		return;
	}
}

/**
 * Find and set unique foreign index by set of foreign fields in foreign key
 * constraint. Save also order of foreign/local field pairs in the index.
 * Return that index number (-1 in not found).
 */
static void
fkey_update_foreign_index(struct tuple_constraint *constr)
{
	fkey_update_index_common(constr, true);
}

/**
 * Find and set local index by set of local fields in foreign key constraint.
 * Save also order of foreign/local field pairs in the index. Return that
 * index number (-1 in not found).
 * Return that index no (-1 in not found).
 */
static void
fkey_update_local_index(struct tuple_constraint *constr)
{
	fkey_update_index_common(constr, false);
}

/**
 * Set diag error ER_FIELD_FOREIGN_KEY_FAILED with given message.
 */
static void
field_foreign_key_failed(const struct tuple_constraint *constr,
			 const struct tuple_field *field,
			 const char *message)
{
	struct error *err;
	const char *field_path = NULL;
	if (field != NULL) {
		field_path = tuple_field_path(field, constr->space->format);
		err = diag_set(ClientError, ER_FIELD_FOREIGN_KEY_FAILED,
			       constr->def.name, field_path, message);
	} else {
		err = diag_set(ClientError, ER_COMPLEX_FOREIGN_KEY_FAILED,
			       constr->def.name, message);
	}
	error_set_str(err, "name", constr->def.name);
	if (field != NULL) {
		error_set_str(err, "field_path", field_path);
		error_set_uint(err, "field_id", field->id);
	}
}

/**
 * Auxiliary data structure that is used for complex key extraction from tuple.
 */
struct key_info {
	/** Index of key part in key definition. */
	uint32_t index_order;
	/** Field number of key part. */
	uint32_t field_no;
	/** Msgpack data of that part in tuple. */
	const char *mp_data;
	/** Size of msgpack data of that part in tuple. */
	size_t mp_data_size;
};

/** Sort by index_order compare function. */
static int
key_info_by_order(const void *ptr1, const void *ptr2)
{
	const struct key_info *info1 = (const struct key_info *)ptr1;
	const struct key_info *info2 = (const struct key_info *)ptr2;
	return info1->index_order < info2->index_order ? -1 :
	       info1->index_order > info2->index_order;
}

/** Sort by field_no compare function. */
static int
key_info_by_field_no(const void *ptr1, const void *ptr2)
{
	const struct key_info *info1 = (const struct key_info *)ptr1;
	const struct key_info *info2 = (const struct key_info *)ptr2;
	return info1->field_no < info2->field_no ? -1 :
	       info1->field_no > info2->field_no;
}

/**
 * Get or extract key for foreign index from local tuple by given as @a mp_data.
 * Simply return mp_data for field foreign key - it is the field itself.
 * For complex foreign keys collect field in one contiguous buffer.
 * Try to place resulting key in @a *buffer, that must be a buffer of size
 * COMPLEX_KEY_BUFFER_SIZE. If there's not enough space - allocate needed
 * using xmalloc - that pointer is returned via @a buffer. Thus if the pointer
 * is changed - a user of that function must free() the buffer after usage.
 * In any case, the pointer to ready-to-use key is returned in @a key, or it is
 * set to NULL if all parts of the key are null.
 */
static int
get_or_extract_key_mp(const struct tuple_constraint *constr,
		      struct key_def *def, char **buffer, const char **key,
		      const char *mp_data)
{
	if (constr->def.fkey.field_mapping_size == 0) {
		*key = mp_data;
		return 0;
	}

	assert(def->part_count == constr->def.fkey.field_mapping_size);
	const uint32_t info_count = def->part_count;
	struct key_info info[info_count];

	/* Collect fields_no in index order. */
	for (uint32_t i = 0; i < info_count; ++i) {
		info[i].index_order = i;
		int16_t pair_no = constr->fkey->data[i].foreign_index_order;
		info[i].field_no = constr->fkey->data[pair_no].local_field_no;
	}

	/* Reorder by fields_no, traverse tuple and collect fields. */
	qsort(info, def->part_count, sizeof(info[0]), key_info_by_field_no);
	assert(mp_typeof(*mp_data) == MP_ARRAY);
	uint32_t tuple_size = mp_decode_array(&mp_data);
	uint32_t info_pos = 0;
	uint32_t null_count = 0;
	size_t total_size = 0;
	for (uint32_t i = 0; i < tuple_size; i++) {
		const char *mp_data_end = mp_data;
		mp_next(&mp_data_end);

		while (i == info[info_pos].field_no) {
			info[info_pos].mp_data = mp_data;
			info[info_pos].mp_data_size = mp_data_end - mp_data;
			total_size += info[info_pos].mp_data_size;
			if (mp_typeof(*mp_data) == MP_NIL)
				null_count++;
			info_pos++;
			if (info_pos == info_count)
				break;
		}

		if (info_pos == info_count)
			break;

		mp_data = mp_data_end;
	}

	if (info_pos == null_count) {
		/* All parts of the key are null. */
		*key = NULL;
		return 0;
	}

	if (info_pos != info_count)
		return -1; /* End of tuple reached unexpectedly. */

	/* Allocate of necessary. */
	if (total_size > COMPLEX_KEY_BUFFER_SIZE)
		*buffer = xmalloc(total_size);
	char *w_pos = *buffer;

	/* Reorder back to index order and join fields in one buffer. */
	qsort(info, def->part_count, sizeof(info[0]), key_info_by_order);
	for (uint32_t i = 0; i < info_count; ++i) {
		memcpy(w_pos, info[i].mp_data, info[i].mp_data_size);
		w_pos += info[i].mp_data_size;
	}

	*key = *buffer;
	return 0;
}

/**
 * Foreign key check function: return 0 if foreign tuple exists.
 */
static int
tuple_constraint_fkey_check(const struct tuple_constraint *constr,
			    const char *mp_data, const char *mp_data_end,
			    const struct tuple_field *field)
{
	(void)mp_data_end;
	assert((constr->def.fkey.field_mapping_size == 0) == (field != NULL));
	struct space *foreign_space = constr->space_cache_holder.space;

	if (recovery_state <= FINAL_RECOVERY) {
		/*
		 * During initial recovery from snapshot it is normal that
		 * there is no foreign tuple because the foreign space is not
		 * loaded yet.
		 * Even more, during final recovery from xlog it is normal
		 * that a secondary index in foreign space is not built yet.
		 * So the best idea is to skip the check during recovery.
		 */
		return 0;
	}

	if (constr->fkey->foreign_index < 0) {
		field_foreign_key_failed(constr, field,
					 "foreign index was not found");
		return -1;
	}
	for (uint32_t i = 0; i < constr->fkey->field_count; i++) {
		if (constr->fkey->data[i].local_field_no < 0) {
			field_foreign_key_failed(constr, field,
						 "wrong local field name");
			return -1;
		}
	}
	struct index *index = foreign_space->index[constr->fkey->foreign_index];
	struct key_def *key_def = index->def->key_def;
	uint32_t part_count = constr->fkey->field_count;
	assert(constr->fkey->field_count == key_def->part_count);

	char *key_buffer = complex_key_buffer;
	const char *key;
	int key_rc = get_or_extract_key_mp(constr, key_def, &key_buffer, &key,
					   mp_data);
	if (key_rc == -1) {
		field_foreign_key_failed(constr, field, "extract key failed");
		return -1;
	}

	if (key == NULL)
		return 0; /* No need to validate all-null key. */

	int rc = -1;
	const char *unused;
	if (key_validate_parts(key_def, key, part_count, false, &unused) != 0) {
		field_foreign_key_failed(constr, field, "wrong key type");
		goto done;
	}
	struct tuple *tuple = NULL;
	if (index_get(index, key, part_count, &tuple) != 0) {
		field_foreign_key_failed(constr, field, "index get failed");
		goto done;
	}
	if (tuple == NULL) {
		field_foreign_key_failed(constr, field,
					 "foreign tuple was not found");
		goto done;
	}
	rc = 0;
done:
	if (key_buffer != complex_key_buffer)
		free(key_buffer);
	return rc;
}

/**
 * Set diag error ER_FOREIGN_KEY_INTEGRITY with given message.
 */
static void
foreign_key_integrity_failed(const struct tuple_constraint *constr,
			     const char *message)
{
	struct error *err = diag_set(ClientError, ER_FOREIGN_KEY_INTEGRITY,
				     constr->def.name, message);
	error_set_str(err, "name", constr->def.name);
}

/**
 * Get of extract key for local index from foreign @a tuple.
 * For field foreign key - return pointer to the field inside of tuple.
 * For complex foreign keys collect field in one contiguous buffer.
 * Try to place resulting key in @a *buffer, that must be a buffer of size
 * COMPLEX_KEY_BUFFER_SIZE. If there's not enough space - allocate needed
 * using xmalloc - that pointer is returned via @a buffer. Thus if the pointer
 * is changed - a user of that function must free() the buffer after usage.
 * Return pointer to ready-to-use key in any case.
 */
static const char *
get_or_extract_key_tuple(const struct tuple_constraint *constr,
			 struct key_def *def, char **buffer,
			 struct tuple *tuple)
{
	if (constr->def.fkey.field_mapping_size == 0) {
		assert(constr->fkey->field_count == 1);
		return tuple_field(tuple,
				   constr->fkey->data[0].foreign_field_no);
	}

	assert(def->part_count == constr->def.fkey.field_mapping_size);
	const uint32_t info_count = def->part_count;
	struct key_info info[info_count];

	/* Traverse fields and calculate total size. */
	size_t total_size = 0;
	for (uint32_t i = 0; i < info_count; ++i) {
		int16_t pair_no = constr->fkey->data[i].local_index_order;
		int32_t field_no = constr->fkey->data[pair_no].foreign_field_no;
		const char *field = tuple_field(tuple, field_no);
		if (field == NULL || *field == MP_NIL)
			return NULL;
		info[i].mp_data = field;
		mp_next(&field);
		info[i].mp_data_size = field - info[i].mp_data;
		total_size += info[i].mp_data_size;
	}

	/* Allocate of necessary. */
	if (total_size > COMPLEX_KEY_BUFFER_SIZE)
		*buffer = xmalloc(total_size);
	char *key = *buffer;
	char *w_pos = key;

	/* Join fields in one buffer. */
	for (uint32_t i = 0; i < info_count; ++i) {
		memcpy(w_pos, info[i].mp_data, info[i].mp_data_size);
		w_pos += info[i].mp_data_size;
	}

	return key;
}

int
tuple_constraint_fkey_check_delete(const struct tuple_constraint *constr,
				   struct tuple *deleted_tuple,
				   struct tuple *replaced_with_tuple)
{
	assert(deleted_tuple != NULL);
	for (uint32_t i = 0; i < constr->fkey->field_count; i++) {
		if (constr->fkey->data[i].foreign_field_no < 0) {
			foreign_key_integrity_failed(constr,
						     "wrong foreign "
						     "field name");
			return -1;
		}
	}
	if (replaced_with_tuple != NULL) {
		/*
		 * We have to check whether the tuple is replaced by equal in
		 * terms of that foreign key constraint - if it is so then the
		 * integrity cannot be broken and no more checks are needed.
		 * Note that if there are no foreign index - the foreign key
		 * constraint is already is in uncheckable state and perhaps
		 * we may not check replaces tuple too.
		 */
		struct space *fspace = constr->space_cache_holder.space;
		if (constr->fkey->foreign_index == 0) {
			/*
			 * The index in the foreign space is primary.
			 * replaced_with_tuple is equal to deleted_tuple in
			 * terms of the primary index.
			 */
			return 0;
		} else if (constr->fkey->foreign_index > 0) {
			/*
			 * Just compare using foreign index's def.
			 */
			int32_t findex_no = constr->fkey->foreign_index;
			struct index *findex = fspace->index[findex_no];
			struct key_def *fkey_def = findex->def->key_def;
			if (tuple_compare(deleted_tuple, HINT_NONE,
					  replaced_with_tuple, HINT_NONE,
					  fkey_def) == 0)
				return 0;
		}
	}

	if (constr->fkey->local_index < 0) {
		foreign_key_integrity_failed(constr, "index was not found");
		return -1;
	}

	struct index *index = constr->space->index[constr->fkey->local_index];
	struct key_def *key_def = index->def->key_def;
	uint32_t part_count = constr->fkey->field_count;
	assert(constr->fkey->field_count == key_def->part_count);

	char *key_buffer = complex_key_buffer;
	const char *key = get_or_extract_key_tuple(constr, key_def,
						   &key_buffer, deleted_tuple);

	if (key == NULL || mp_typeof(*key) == MP_NIL) {
		/* No field(s) - nobody can be bound to them.*/
		return 0;
	}
	int rc = -1;

	const char *unused;
	if (key_validate_parts(key_def, key, part_count, false, &unused) != 0) {
		foreign_key_integrity_failed(constr, "wrong key type");
		goto done;
	}

	struct tuple *found_tuple;
	if (index->def->opts.is_unique ?
	    index_get(index, key, part_count, &found_tuple) :
	    index_min(index, key, part_count, &found_tuple) != 0)
		goto done;
	if (found_tuple != NULL) {
		foreign_key_integrity_failed(constr, "tuple is referenced");
		goto done;
	}
	rc = 0;

done:
	if (key_buffer != complex_key_buffer)
		free(key_buffer);
	return rc;
}

/**
 * Find and set foreign_field_no amd foreign_index fkey member of @a constraint.
 * If something was not found - foreign_index is set to -1.
 */
static void
tuple_constraint_fkey_update_foreign(struct tuple_constraint *constraint)
{
	struct space *space = constraint->space_cache_holder.space;
	constraint->fkey->foreign_index = -1;
	uint32_t field_mapping_size = constraint->def.fkey.field_mapping_size;
	if (field_mapping_size == 0) {
		assert(constraint->fkey->field_count == 1);
		constraint->fkey->data[0].foreign_field_no =
			find_field_no_by_def(space,
					     &constraint->def.fkey.field);
		if (constraint->fkey->data[0].foreign_field_no >= 0)
			fkey_update_foreign_index(constraint);
		return;
	}
	for (uint32_t i = 0; i < field_mapping_size; i++) {
		struct tuple_constraint_field_id *f =
			&constraint->def.fkey.field_mapping[i].foreign_field;
		int32_t field_no = find_field_no_by_def(space, f);
		constraint->fkey->data[i].foreign_field_no = field_no;
		if (field_no < 0)
			return;
	}
	fkey_update_foreign_index(constraint);
}

/**
 * Find and set local_field_no amd local_index fkey member of @a constraint.
 * If something was not found - local_index is set to -1.
 */
static void
tuple_constraint_fkey_update_local(struct tuple_constraint *constraint,
				   int32_t field_no)
{
	struct space *space = constraint->space;
	constraint->fkey->local_index = -1;
	uint32_t field_mapping_size = constraint->def.fkey.field_mapping_size;
	if (field_mapping_size == 0) {
		assert(constraint->fkey->field_count == 1);
		constraint->fkey->data[0].local_field_no = field_no;
		assert(field_no >= 0);
		fkey_update_local_index(constraint);
		return;
	}
	for (uint32_t i = 0; i < field_mapping_size; i++) {
		struct tuple_constraint_field_id *f =
			&constraint->def.fkey.field_mapping[i].local_field;
		field_no = find_field_no_by_def(space, f);
		constraint->fkey->data[i].local_field_no = field_no;
		if (field_no < 0)
			return;
	}
	fkey_update_local_index(constraint);
}

/**
 * Callback that is called when a space that is pinned by a constraint is
 * replaced in space cache.
 */
static void
tuple_constraint_fkey_space_cache_on_replace(struct space_cache_holder *holder,
					     struct space *old_space)
{
	(void)old_space;
	struct tuple_constraint *constr =
		container_of(holder, struct tuple_constraint,
			     space_cache_holder);
	tuple_constraint_fkey_update_foreign(constr);
}

/**
 * Unpin space from space_cache, remove check.
 */
static void
tuple_constraint_fkey_detach(struct tuple_constraint *constr)
{
	assert(constr->detach == tuple_constraint_fkey_detach);
	/* Check that constraint has not been detached yet. */
	assert(constr->check != tuple_constraint_noop_check);
	space_cache_unpin(&constr->space_cache_holder);
	constr->check = tuple_constraint_noop_check;
}

/**
 * Put space back to space_cache, put check back.
 */
static void
tuple_constraint_fkey_reattach(struct tuple_constraint *constr)
{
	assert(constr->reattach == tuple_constraint_fkey_reattach);
	/* Check that constraint has been detached. */
	assert(constr->check == tuple_constraint_noop_check);
	struct space *space = constr->space;
	bool fkey_same_space = constr->def.fkey.space_id == 0 ||
			       constr->def.fkey.space_id == space->def->id;
	uint32_t space_id = fkey_same_space ? space->def->id :
			    constr->def.fkey.space_id;
	struct space *foreign_space = space_by_id(space_id);
	enum space_cache_holder_type type = SPACE_HOLDER_FOREIGN_KEY;
	space_cache_pin(foreign_space, &constr->space_cache_holder,
			tuple_constraint_fkey_space_cache_on_replace,
			type, fkey_same_space);
	constr->check = tuple_constraint_fkey_check;
}

/**
 * Destructor. Detaches constraint if it has not been detached before and
 * deinitializes its fields.
 */
static void
tuple_constraint_fkey_destroy(struct tuple_constraint *constr)
{
	assert(constr->destroy == tuple_constraint_fkey_destroy);
	/** Detach constraint if it has not been detached before. */
	if (constr->check != tuple_constraint_noop_check)
		tuple_constraint_fkey_detach(constr);
	constr->detach = tuple_constraint_noop_alter;
	constr->reattach = tuple_constraint_noop_alter;
	constr->destroy = tuple_constraint_noop_alter;
	constr->space = NULL;
}

/**
 * Check that spaces @a space and @a foreign_space are compatible with
 * the foreign key constraint @a constr.
 * Return 0 on success, return -1 and set diag in case of error.
 */
static int
tuple_constraint_fkey_check_spaces(struct tuple_constraint *constr,
				   struct space *space,
				   struct space *foreign_space)
{
	if (space_is_data_temporary(foreign_space) &&
	    !space_is_data_temporary(space)) {
		diag_set(ClientError, ER_CREATE_FOREIGN_KEY,
			 constr->def.name, constr->space->def->name,
			 "foreign key from non-data-temporary space"
			 " can't refer to data-temporary space");
		return -1;
	}
	if (space_is_local(foreign_space) && !space_is_local(space)) {
		diag_set(ClientError, ER_CREATE_FOREIGN_KEY,
			 constr->def.name, constr->space->def->name,
			 "foreign key from non-local space"
			 " can't refer to local space");
		return -1;
	}
	return 0;
}

int
tuple_constraint_fkey_init(struct tuple_constraint *constr,
			   struct space *space, int32_t field_no)
{
	assert(constr->def.type == CONSTR_FKEY);
	constr->space = space;
	tuple_constraint_fkey_update_local(constr, field_no);

	bool fkey_same_space = constr->def.fkey.space_id == 0 ||
			       constr->def.fkey.space_id == space->def->id;
	uint32_t space_id = fkey_same_space ? space->def->id :
			    constr->def.fkey.space_id;
	struct space *foreign_space = space_by_id(space_id);
	if (fkey_same_space && foreign_space == NULL)
		foreign_space = space;
	enum space_cache_holder_type type = SPACE_HOLDER_FOREIGN_KEY;
	if (foreign_space != NULL) {
		/* Space was found, use it. */
		if (tuple_constraint_fkey_check_spaces(constr, space,
						       foreign_space) != 0)
			return -1;
		space_cache_pin(foreign_space, &constr->space_cache_holder,
				tuple_constraint_fkey_space_cache_on_replace,
				type, fkey_same_space);
		tuple_constraint_fkey_update_foreign(constr);
		constr->check = tuple_constraint_fkey_check;
		constr->destroy = tuple_constraint_fkey_destroy;
		constr->detach = tuple_constraint_fkey_detach;
		constr->reattach = tuple_constraint_fkey_reattach;
		return 0;
	}
	if (recovery_state >= FINAL_RECOVERY) {
		/* Space was not found, error. */
		const char *error =
			tt_sprintf("foreign space '%u' was not found by id",
				   constr->def.fkey.space_id);
		diag_set(ClientError, ER_CREATE_FOREIGN_KEY,
			 constr->def.name, constr->space->def->name, error);
		return -1;
	}
	/*
	 * Space was not found, but it's OK in during initial recovery. We
	 * will find it later, after initial recovery.
	 */
	assert(constr->check == tuple_constraint_noop_check);
	return 0;
}
