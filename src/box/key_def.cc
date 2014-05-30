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
#include "space.h"
#include "schema.h"
#include <stdlib.h>
#include <stdio.h>
#include "exception.h"

const char *field_type_strs[] = {"UNKNOWN", "NUM", "STR", "\0"};
STRS(index_type, ENUM_INDEX_TYPE);

const uint32_t key_mp_type[] = {
	/* [UNKNOWN] = */ UINT32_MAX,
	/* [NUM]     = */ 1U << MP_UINT,
	/* [_STR]    = */ 1U << MP_STR
};

enum schema_object_type
schema_object_type(const char *name)
{
	static const char *strs[] = {
		"unknown", "universe", "space", "function" };
	int index = strindex(strs, name, 4);
	return (enum schema_object_type) (index == 4 ? 0 : index);
}

struct key_def *
key_def_new(uint32_t space_id, uint32_t iid, const char *name,
	    enum index_type type, bool is_unique, uint32_t part_count)
{
	uint32_t parts_size = sizeof(struct key_part) * part_count;
	size_t sz = parts_size + sizeof(struct key_def);
	struct key_def *def = (struct key_def *) malloc(sz);
	if (def == NULL) {
		tnt_raise(LoggedError, ER_MEMORY_ISSUE,
			  sz, "struct key_def", "malloc");
	}
	int n = snprintf(def->name, sizeof(def->name), "%s", name);
	if (n >= sizeof(def->name)) {
		free(def);
		tnt_raise(LoggedError, ER_MODIFY_INDEX,
			  (unsigned) iid, (unsigned) space_id,
			  "index name is too long");
	}
	if (!identifier_is_valid(def->name)) {
		free(def);
		tnt_raise(ClientError, ER_IDENTIFIER, def->name);
	}
	def->type = type;
	def->space_id = space_id;
	def->iid = iid;
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
	if (key1->iid != key2->iid)
		return key1->iid < key2->iid ? -1 : 1;
	if (strcmp(key1->name, key2->name))
		return strcmp(key1->name, key2->name);
	if (key1->type != key2->type)
		return (int) key1->type < (int) key2->type ? -1 : 1;
	if (key1->is_unique != key2->is_unique)
		return (int) key1->is_unique < (int) key2->is_unique ? -1 : 1;

	return key_part_cmp(key1->parts, key1->part_count,
			    key2->parts, key2->part_count);
}

void
key_list_del_key(struct rlist *key_list, uint32_t iid)
{
	struct key_def *key;
	rlist_foreach_entry(key, key_list, link) {
		if (key->iid == iid) {
			rlist_del_entry(key, link);
			return;
		}
	}
	assert(false);
}

void
key_def_check(struct key_def *key_def)
{
	if (key_def->iid >= BOX_INDEX_MAX) {
		tnt_raise(ClientError, ER_MODIFY_INDEX,
			  (unsigned) key_def->iid,
			  (unsigned) key_def->space_id,
			  "index id too big");
	}
	if (key_def->iid == 0 && key_def->is_unique == false) {
		tnt_raise(ClientError, ER_MODIFY_INDEX,
			  (unsigned) key_def->iid,
			  (unsigned) key_def->space_id,
			  "primary key must be unique");
	}
	if (key_def->part_count == 0) {
		tnt_raise(ClientError, ER_MODIFY_INDEX,
			  (unsigned) key_def->iid,
			  (unsigned) key_def->space_id,
			  "part count must be positive");
	}
	if (key_def->part_count > BOX_INDEX_PART_MAX) {
		tnt_raise(ClientError, ER_MODIFY_INDEX,
			  (unsigned) key_def->iid,
			  (unsigned) key_def->space_id,
			  "too many key parts");
	}
	for (uint32_t i = 0; i < key_def->part_count; i++) {
		if (key_def->parts[i].type == field_type_MAX) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  (unsigned) key_def->iid,
				  (unsigned) key_def->space_id,
				  "unknown field type");
		}
		if (key_def->parts[i].fieldno > BOX_INDEX_FIELD_MAX) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  (unsigned) key_def->iid,
				  (unsigned) key_def->space_id,
				  "field no is too big");
		}
		for (uint32_t j = 0; j < i; j++) {
			/*
			 * Courtesy to a user who could have made
			 * a typo.
			 */
			if (key_def->parts[i].fieldno ==
			    key_def->parts[j].fieldno) {
				tnt_raise(ClientError, ER_MODIFY_INDEX,
					  (unsigned) key_def->iid,
					  (unsigned) key_def->space_id,
					  "same key part is indexed twice");
			}
		}
	}
	struct space *space =
		space_cache_find(key_def->space_id);

	/* validate key_def->type */
	space->engine->factory->keydefCheck(key_def);
}

void
space_def_check(struct space_def *def, uint32_t namelen, uint32_t engine_namelen,
                int32_t errcode)
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
	identifier_check(def->name);
	if (engine_namelen >= sizeof(def->engine_name)) {
		tnt_raise(ClientError, errcode,
			  (unsigned) def->id,
			  "space engine name is too long");
	}
	identifier_check(def->engine_name);
}

bool
identifier_is_valid(const char *str)
{
	mbstate_t state;
	memset(&state, 0, sizeof(state));
	wchar_t w;
	ssize_t len = mbrtowc(&w, str, MB_CUR_MAX, &state);
	if (len <= 0)
		return false; /* invalid character or zero-length string */
	if (!iswalpha(w) && w != L'_')
		return false; /* fail to match [a-zA-Z_] */

	while ((len = mbrtowc(&w, str, MB_CUR_MAX, &state)) > 0) {
		if (!iswalnum(w) && w != L'_')
			return false; /* fail to match [a-zA-Z0-9_]* */
		str += len;
	}

	if (len < 0)
		return false; /* invalid character  */

	return true;
}

void
identifier_check(const char *str)
{
	if (! identifier_is_valid(str))
		tnt_raise(ClientError, ER_IDENTIFIER, str);
}

