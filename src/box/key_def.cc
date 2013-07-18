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
extern "C" {
#include <cfg/tarantool_box_cfg.h>
} /* extern "C" */
#include "exception.h"
#include <stddef.h>

const char *field_type_strs[] = {"UNKNOWN", "NUM", "NUM64", "STR", "\0"};
STRS(index_type, INDEX_TYPE);

void
key_def_create(struct key_def *def, uint32_t id,
	       struct tarantool_cfg_space_index *cfg_index)
{
	def->id = id;
	def->part_count = 0;

	def->type = STR2ENUM(index_type, cfg_index->type);
	if (def->type == index_type_MAX)
		tnt_raise(LoggedError, ER_INDEX_TYPE, cfg_index->type);

	/* Calculate key part count and maximal field number. */
	for (uint32_t k = 0; cfg_index->key_field[k] != NULL; ++k) {
		auto cfg_key = cfg_index->key_field[k];

		if (cfg_key->fieldno == -1) {
			/* last filled key reached */
			break;
		}

		def->part_count++;
	}

	/* init def array */
	def->parts = (struct key_part *) malloc(sizeof(struct key_part) *
						def->part_count);

	/* fill fields and compare order */
	for (uint32_t k = 0; cfg_index->key_field[k] != NULL; ++k) {
		auto cfg_key = cfg_index->key_field[k];

		if (cfg_key->fieldno == -1) {
			/* last filled key reached */
			break;
		}

		/* fill keys */
		def->parts[k].fieldno = cfg_key->fieldno;
		def->parts[k].type = STR2ENUM(field_type, cfg_key->type);
	}
	def->is_unique = cfg_index->unique;
}

/** Free a key definition. */
void
key_def_destroy(struct key_def *key_def)
{
	free(key_def->parts);
}

