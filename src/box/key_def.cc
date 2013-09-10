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
#include "key_def.h"
#include <stdlib.h>
#include "exception.h"

const char *field_type_strs[] = {"UNKNOWN", "NUM", "NUM64", "STR", "\0"};
STRS(index_type, ENUM_INDEX_TYPE);

struct key_def *
key_def_new(uint32_t id, enum index_type type, bool is_unique,
	    uint32_t part_count)
{
	uint32_t parts_size = sizeof(struct key_part) * part_count;
	size_t sz = parts_size + sizeof(struct key_def);
	struct key_def *def = (struct key_def *) malloc(sz);
	if (def == NULL)
		tnt_raise(LoggedError, ER_MEMORY_ISSUE,
			  sz, "struct key_def", "malloc");
	def->type = type;
	def->id = id;
	def->is_unique = is_unique;
	def->part_count = part_count;

	memset(def->parts, 0, parts_size);
	return def;
}

/** Free a key definition. */
void
key_def_delete(struct key_def *key_def)
{
	free(key_def);
}

int
key_part_cmp(const struct key_part *parts1, uint32_t part_count1,
	     const struct key_part *parts2, uint32_t part_count2)
{
	const struct key_part *part1 = parts1;
	const struct key_part *part2 = parts2;
	uint32_t part_count = MIN(part_count1, part_count2);
	const struct key_part *end = parts1 + part_count;
	for (; part1 != end; part1++, part2++) {
		if (part1->fieldno != part2->fieldno)
			return part1->fieldno < part2->fieldno ? -1 : 1;
		if ((int) part1->type != (int) part2->type)
			return (int) part1->type < (int) part2->type ? -1 : 1;
	}
	return part_count1 < part_count2 ? -1 : part_count1 > part_count2;
}

int
key_def_cmp(const struct key_def *key1, const struct key_def *key2)
{
	if (key1->id != key2->id)
		return key1->id < key2->id ? -1 : 1;
	if (key1->type != key2->type)
		return (int) key1->type < (int) key2->type ? -1 : 1;
	if (key1->is_unique != key2->is_unique)
		return (int) key1->is_unique < (int) key2->is_unique ? -1 : 1;

	return key_part_cmp(key1->parts, key1->part_count,
			    key2->parts, key2->part_count);
}

void
key_list_del_key(struct rlist *key_list, uint32_t id)
{
	struct key_def *key;
	rlist_foreach_entry(key, key_list, link) {
		if (key->id == id) {
			rlist_del_entry(key, link);
			return;
		}
	}
	assert(false);
}

void
key_def_check(uint32_t id, struct key_def *key_def)
{
	if (key_def->id > BOX_INDEX_MAX) {
		tnt_raise(ClientError, ER_MODIFY_INDEX,
			  (unsigned) key_def->id, (unsigned) id,
			  "index id too big");
	}
	if (key_def->part_count == 0) {
		tnt_raise(ClientError, ER_MODIFY_INDEX,
			  (unsigned) key_def->id, (unsigned) id,
			  "part count must be positive");
	}
	for (uint32_t i = 0; i < key_def->part_count; i++) {
		if (key_def->parts[i].type == field_type_MAX) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  (unsigned) key_def->id, (unsigned) id,
				  "unknown field type");
		}
		if (key_def->parts[i].fieldno > BOX_FIELD_MAX) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  (unsigned) key_def->id, (unsigned) id,
				  "field no is too big");
		}
	}
	switch (key_def->type) {
	case HASH:
		if (! key_def->is_unique) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  (unsigned) key_def->id, (unsigned) id,
				  "HASH index must be unique");
		}
		break;
	case TREE:
		/* TREE index has no limitations. */
		break;
	case BITSET:
		if (key_def->part_count != 1) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  (unsigned) key_def->id, (unsigned) id,
				    "BITSET index key can not be multipart");
		}
		if (key_def->is_unique) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  (unsigned) key_def->id, (unsigned) id,
				  "BITSET can not be unique");
		}
		break;
	default:
		tnt_raise(ClientError, ER_INDEX_TYPE,
			  (unsigned) key_def->id, (unsigned) id);
		break;
	}
}

void
space_def_check(struct space_def *def, uint32_t namelen, uint32_t errcode)
{
	if (def->id > BOX_SPACE_MAX) {
		tnt_raise(ClientError, errcode,
			  (unsigned) def->id,
			  "space id is too big");
	}
	if (namelen >= sizeof(def->name)) {
		tnt_raise(ClientError, errcode,
			  (unsigned) def->id,
			  "space name is too long");
	}
}
