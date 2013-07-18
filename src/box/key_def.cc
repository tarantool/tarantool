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
#include <stdarg.h>

const char *field_type_strs[] = {"UNKNOWN", "NUM", "NUM64", "STR", "\0"};
STRS(index_type, INDEX_TYPE);

void
key_def_create_from_cfg(struct key_def *def, uint32_t id,
	       struct tarantool_cfg_space_index *cfg_index)
{
	def->id = id;
	def->type = STR2ENUM(index_type, cfg_index->type);

	if (def->type == index_type_MAX)
		tnt_raise(LoggedError, ER_INDEX_TYPE, cfg_index->type);

	def->is_unique = cfg_index->unique;
	def->part_count = 0;

	/* Find out key part count. */
	for (uint32_t k = 0; cfg_index->key_field[k] != NULL; ++k) {
		auto cfg_key = cfg_index->key_field[k];

		if (cfg_key->fieldno == -1) {
			/* last filled key reached */
			break;
		}
		def->part_count++;
	}

	/* Allocate and initialize key part array. */
	def->parts = (struct key_part *) malloc(sizeof(struct key_part) *
						def->part_count);
	for (uint32_t k = 0; k < def->part_count; k++) {
		auto cfg_key = cfg_index->key_field[k];

		def->parts[k].fieldno = cfg_key->fieldno;
		def->parts[k].type = STR2ENUM(field_type, cfg_key->type);
	}
}

struct key_def *
key_def_create(struct key_def *def,
	       uint32_t id, enum index_type type, bool is_unique,
	       uint32_t part_count, ...)
{
	va_list ap;
	va_start(ap, part_count);
	def->type = type;
	def->id = id;
	def->is_unique = is_unique;
	def->part_count = part_count;

	def->parts = (struct key_part *) malloc(sizeof(struct key_part) *
						def->part_count);

	for (struct key_part *part = def->parts;
	     part < def->parts + def->part_count;
	     part++) {

		part->fieldno = va_arg(ap, uint32_t);
		part->type = (enum field_type) va_arg(ap, int);
	}
	va_end(ap);
	return def;
}

/** Free a key definition. */
void
key_def_destroy(struct key_def *key_def)
{
	free(key_def->parts);
}

