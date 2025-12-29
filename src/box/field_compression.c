/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2025, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "field_compression.h"

#include <assert.h>
#include <string.h>

int
field_compression_cmp(const struct field_compression *compr1,
		      const struct field_compression *compr2)
{
	return field_compression_def_cmp(&compr1->def, &compr2->def);
}

uint32_t
field_compression_hash_process(const struct field_compression *compr,
			       uint32_t *ph, uint32_t *pcarry)
{
	return field_compression_def_hash_process(&compr->def, ph, pcarry);
}

#if defined(ENABLE_TUPLE_COMPRESSION)
# include "field_compression_impl.c"
#else /* !defined(ENABLE_TUPLE_COMPRESSION) */

void
field_compression_from_def(const struct field_compression_def *def,
			   struct field_compression *compr)
{
	compr->def = *def;
	assert(def->type == COMPRESSION_TYPE_NONE);
	compr->opts = (struct compression_opts){.type = COMPRESSION_TYPE_NONE};
}

#endif
