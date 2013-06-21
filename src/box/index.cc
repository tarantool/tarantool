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
#include "space.h"

const char *field_data_type_strs[] = {"UNKNOWN", "NUM", "NUM64", "STR", "\0"};
STRS(index_type, INDEX_TYPE);
STRS(iterator_type, ITERATOR_TYPE);

/* {{{ Utilities. **********************************************/

void
key_validate(struct key_def *key_def, enum iterator_type type, const char *key,
	     uint32_t part_count)
{
	if (part_count == 0 && (type == ITER_ALL || key_def->type == TREE ||
			 (key_def->type == HASH && type == ITER_GE))) {
		assert(key == NULL);
		return;
	}

	if (part_count > key_def->part_count)
		tnt_raise(ClientError, ER_KEY_PART_COUNT,
			  key_def->part_count, part_count);

	/* Check partial keys */
	if ((key_def->type != TREE) && part_count < key_def->part_count) {
		tnt_raise(ClientError, ER_EXACT_MATCH,
			  key_def->part_count, part_count);
	}

	const char *key_data = key;
	for (uint32_t part = 0; part < part_count; part++) {
		uint32_t part_size = load_varint32(&key_data);

		enum field_data_type part_type = key_def->parts[part].type;

		if (part_type == NUM && part_size != sizeof(uint32_t))
			tnt_raise(ClientError, ER_KEY_FIELD_TYPE, "u32");

		if (part_type == NUM64 && part_size != sizeof(uint64_t) &&
				part_size != sizeof(uint32_t))
			tnt_raise(ClientError, ER_KEY_FIELD_TYPE, "u64");

		key_data += part_size;
	}
}

/**
 * Check if replacement of an old tuple with a new one is
 * allowed.
 */
uint32_t
Index::replace_check_dup(struct tuple *old_tuple,
		  struct tuple *dup_tuple,
		  enum dup_replace_mode mode)
{
	if (dup_tuple == NULL) {
		if (mode == DUP_REPLACE) {
			/*
			 * dup_replace_mode is DUP_REPLACE, and
			 * a tuple with the same key is not found.
			 */
			return ER_TUPLE_NOT_FOUND;
		}
	} else { /* dup_tuple != NULL */
		if (dup_tuple != old_tuple &&
		    (old_tuple != NULL || mode == DUP_INSERT)) {
			/*
			 * There is a duplicate of new_tuple,
			 * and it's not old_tuple: we can't
			 * possibly delete more than one tuple
			 * at once.
			 */
			return ER_TUPLE_FOUND;
		}
	}
	return 0;
}

/* }}} */

/* {{{ Index -- base class for all indexes. ********************/

Index *
Index::factory(enum index_type type, struct key_def *key_def, struct space *space)
{
	switch (type) {
	case HASH:
		return HashIndex::factory(key_def, space);
	case TREE:
		return TreeIndex::factory(key_def, space);
	case BITSET:
		return new BitsetIndex(key_def, space);
	default:
		assert(false);
	}

	return NULL;
}

Index::Index(struct key_def *key_def, struct space *space)
{
	this->key_def = key_def;
	this->space = space;
	m_position = NULL;
}

Index::~Index()
{
	if (m_position != NULL)
		m_position->free(m_position);
}

/* }}} */
