/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "tuple_constraint.h"

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
tuple_constraint_noop_destructor(struct tuple_constraint *constr)
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

struct tuple_constraint *
tuple_constraint_array_new(const struct tuple_constraint_def *defs,
			   size_t count)
{
	struct tuple_constraint *res =
		tuple_constraint_def_array_dup_raw(defs, count,
						   sizeof(*res), 0);
	/* Initialize uninitialized part. */
	for (size_t i = 0; i < count; i++) {
		res[i].check = tuple_constraint_noop_check;
		res[i].destroy = tuple_constraint_noop_destructor;
	}
	return res;
}
