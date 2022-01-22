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
			uint32_t field_no = is_foreign ?
				constr->fkey->data[j].foreign_field_no :
				constr->fkey->data[j].local_field_no;
			uint32_t k;
			for (k = 0; k < field_count; k++)
				if (parts[k].fieldno == field_no)
					break;
			if (k == field_count)
				break; /* Not found. */
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
	const char *field_path = tuple_field_path(field, constr->space->format);
	struct error *err = diag_set(ClientError, ER_FIELD_FOREIGN_KEY_FAILED,
				     constr->def.name, field_path, message);
	error_set_str(err, "name", constr->def.name);
	error_set_str(err, "field_path", field_path);
	error_set_uint(err, "field_id", field->id);
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
	assert(field != NULL);
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
	struct index *index = foreign_space->index[constr->fkey->foreign_index];
	struct key_def *key_def = index->def->key_def;
	uint32_t part_count = constr->fkey->field_count;
	assert(constr->fkey->field_count == key_def->part_count);

	const char *key = mp_data;

	const char *unused;
	if (key_validate_parts(key_def, key, part_count, false, &unused) != 0) {
		field_foreign_key_failed(constr, field, "wrong key type");
		return -1;
	}
	struct tuple *tuple = NULL;
	if (index_get(index, key, part_count, &tuple) != 0) {
		field_foreign_key_failed(constr, field, "index get failed");
		return -1;
	}
	if (tuple == NULL) {
		field_foreign_key_failed(constr, field,
					 "foreign tuple was not found");
		return -1;
	}
	return 0;
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

int
tuple_constraint_fkey_check_delete(const struct tuple_constraint *constr,
				   struct tuple *deleted_tuple,
				   struct tuple *replaced_with_tuple)
{
	assert(deleted_tuple != NULL);
	int32_t foreign_field_no = constr->fkey->data->foreign_field_no;
	if (foreign_field_no < 0) {
		foreign_key_integrity_failed(constr,
					     "wrong foreign field name");
		return -1;
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

	const char *key = tuple_field(deleted_tuple, foreign_field_no);
	if (key == NULL || mp_typeof(*key) == MP_NIL) {
		/* No field - nobody can be bound to it.*/
		return 0;
	}

	const char *unused;
	if (key_validate_parts(key_def, key, part_count, false, &unused) != 0) {
		foreign_key_integrity_failed(constr, "wrong key type");
		return -1;
	}

	struct tuple *found_tuple;
	if (index->def->opts.is_unique ?
	    index_get(index, key, part_count, &found_tuple) :
	    index_min(index, key, part_count, &found_tuple) != 0)
		return -1;
	if (found_tuple != NULL) {
		foreign_key_integrity_failed(constr, "tuple is referenced");
		return -1;
	}
	return 0;
}

/**
 * Destructor that unpins space from space_cache.
 */
static void
tuple_constraint_fkey_unpin(struct tuple_constraint *constr)
{
	assert(constr->destroy == tuple_constraint_fkey_unpin);
	space_cache_unpin(&constr->space_cache_holder);
	constr->check = tuple_constraint_noop_check;
	constr->destroy = tuple_constraint_noop_destructor;
	constr->space = NULL;
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
	assert(constraint->fkey->field_count == 1);
	constraint->fkey->data[0].foreign_field_no =
		find_field_no_by_def(space, &constraint->def.fkey.field);
	if (constraint->fkey->data[0].foreign_field_no >= 0)
		fkey_update_foreign_index(constraint);
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

int
tuple_constraint_fkey_init(struct tuple_constraint *constr,
			   struct space *space, int32_t field_no)
{
	assert(constr->def.type == CONSTR_FKEY);
	constr->space = space;
	constr->fkey->data[0].local_field_no = field_no;
	fkey_update_local_index(constr);

	struct space *foreign_space;
	foreign_space = space_by_id(constr->def.fkey.space_id);
	enum space_cache_holder_type type = SPACE_HOLDER_FOREIGN_KEY;
	if (foreign_space != NULL) {
		/* Space was found, use it. */
		space_cache_pin(foreign_space, &constr->space_cache_holder,
				tuple_constraint_fkey_space_cache_on_replace,
				type);
		tuple_constraint_fkey_update_foreign(constr);
		constr->check = tuple_constraint_fkey_check;
		constr->destroy = tuple_constraint_fkey_unpin;
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
