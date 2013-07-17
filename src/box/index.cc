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
#include "index.h"
#include "hash_index.h"
#include "tree_index.h"
#include "bitset_index.h"
#include "tuple.h"
#include "say.h"
#include "exception.h"

STRS(iterator_type, ITERATOR_TYPE);

/* {{{ Utilities. **********************************************/

static inline void
key_validate_parts(struct key_def *key_def,
		   const char *key, uint32_t part_count)
{
	for (uint32_t part = 0; part < part_count; part++) {
		uint32_t part_size = load_varint32(&key);

		enum field_type part_type = key_def->parts[part].type;

		if (part_type == NUM && part_size != sizeof(uint32_t))
			tnt_raise(ClientError, ER_KEY_FIELD_TYPE, "u32");

		if (part_type == NUM64 && part_size != sizeof(uint64_t) &&
		    part_size != sizeof(uint32_t))
			tnt_raise(ClientError, ER_KEY_FIELD_TYPE, "u64");

		key += part_size;
	}
}

void
key_validate(struct key_def *key_def, enum iterator_type type, const char *key,
	     uint32_t part_count)
{
	if (part_count == 0) {
		assert(key == NULL);
		/*
		 * Zero key parts are allowed:
		 * - for TREE index, all iterator types,
		 * - ITERA_ALL iterator type, all index types
		 * - ITER_GE iterator in HASH index (legacy)
		 */
		if (key_def->type == TREE || type == ITER_ALL ||
		    (key_def->type == HASH && type == ITER_GE))
			return;
		/* Fall through. */
	}

	if (part_count > key_def->part_count)
		tnt_raise(ClientError, ER_KEY_PART_COUNT,
			  key_def->part_count, part_count);

	/* Partial keys are allowed only for TREE index type. */
	if (key_def->type != TREE && part_count < key_def->part_count) {
		tnt_raise(ClientError, ER_EXACT_MATCH,
			  key_def->part_count, part_count);
	}
	key_validate_parts(key_def, key, part_count);
}

void
primary_key_validate(struct key_def *key_def, const char *key,
		     uint32_t part_count)
{
	if (key_def->part_count != part_count) {
		tnt_raise(ClientError, ER_EXACT_MATCH,
			  key_def->part_count, part_count);
	}
	key_validate_parts(key_def, key, part_count);
}

/* }}} */

/* {{{ Index -- base class for all indexes. ********************/

Index *
Index::factory(struct key_def *key_def)
{
	switch (key_def->type) {
	case HASH:
		return new HashIndex(key_def);
	case TREE:
		return new TreeIndex(key_def);
	case BITSET:
		return new BitsetIndex(key_def);
	default:
		assert(false);
	}

	return NULL;
}

Index::Index(struct key_def *key_def)
{
	this->key_def = *key_def;
	m_position = NULL;
}

Index::~Index()
{
	if (m_position != NULL)
		m_position->free(m_position);
	key_def_destroy(&key_def);
}

struct tuple *
Index::findByTuple(struct tuple *tuple) const
{
	(void) tuple;
	tnt_raise(ClientError, ER_UNSUPPORTED, "Index", "findByTuple()");
	return NULL;
}
/* }}} */
