/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "tuple_constraint.h"

#include "salad/grp_alloc.h"
#include "trivia/util.h"

int
tuple_constraint_noop_check(const struct tuple_constraint *constr,
			    const char *mp_data, const char *mp_data_end,
			    const struct tuple_field *field)
{
	(void)constr;
	(void)mp_data;
	(void)mp_data_end;
	(void)field;
	return 0;
}

void
tuple_constraint_noop_alter(struct tuple_constraint *constr)
{
	(void)constr;
}

int
tuple_constraint_cmp(const struct tuple_constraint *constr1,
		     const struct tuple_constraint *constr2,
		     bool ignore_name)
{
	return tuple_constraint_def_cmp(&constr1->def, &constr2->def,
					ignore_name);
}

uint32_t
tuple_constraint_hash_process(const struct tuple_constraint *constr,
			      uint32_t *ph, uint32_t *pcarry)
{
	return tuple_constraint_def_hash_process(&constr->def, ph, pcarry);
}

struct tuple_constraint *
tuple_constraint_array_new(const struct tuple_constraint_def *defs,
			   size_t count)
{
	/** Data bank for structs tuple_constraint_fkey_data. */
	struct grp_alloc all = grp_alloc_initializer();
	for (size_t i = 0; i < count; i++) {
		if (defs[i].type != CONSTR_FKEY)
			continue;
		uint32_t field_count = defs[i].fkey.field_mapping_size;
		if (field_count == 0)
			field_count = 1; /* field foreign key */
		size_t size = offsetof(struct tuple_constraint_fkey_data,
				       data[field_count]);
		grp_alloc_reserve_data(&all, size);
	}
	struct tuple_constraint *res =
		tuple_constraint_def_array_dup_raw(defs, count, sizeof(*res),
						   grp_alloc_size(&all));
	/* Initialize uninitialized part. */
	grp_alloc_use(&all, res + count);
	for (size_t i = 0; i < count; i++) {
		res[i].check = tuple_constraint_noop_check;
		res[i].destroy = tuple_constraint_noop_alter;
		res[i].detach = tuple_constraint_noop_alter;
		res[i].reattach = tuple_constraint_noop_alter;
		if (defs[i].type != CONSTR_FKEY) {
			res[i].fkey = NULL;
			continue;
		}
		uint32_t field_count = defs[i].fkey.field_mapping_size;
		if (field_count == 0)
			field_count = 1; /* field foreign key */
		size_t size = offsetof(struct tuple_constraint_fkey_data,
				       data[field_count]);
		res[i].fkey = grp_alloc_create_data(&all, size);
		res[i].fkey->field_count = field_count;
	}

	assert(grp_alloc_size(&all) == 0);
	return res;
}
