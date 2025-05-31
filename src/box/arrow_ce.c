/*
 * Copyright (c) 2025 VK Company Limited. All Rights Reserved.
 *
 * The information and source code contained herein is the exclusive property
 * of VK Company Limited and may not be disclosed, examined, or reproduced in
 * whole or in part without explicit written authorization from the Company.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "arrow_ce.h"

#include "diag.h"
#include "errcode.h"
#include "fiber.h"
#include "tuple.h"
#include "tuple_format.h"
#include "key_def.h"
#include "space_cache.h"
#include "bit/bit.h"
#include "small/region.h"
#include "trivia/util.h"

#ifndef ROUND_UP
# define ROUND_UP(n, d) (DIV_ROUND_UP(n, d) * (d))
#endif

static const char *const field_type_to_arrow_format[] = {
	[FIELD_TYPE_ANY] = NULL,
	[FIELD_TYPE_UNSIGNED] = "L",
	[FIELD_TYPE_STRING] = "u",
	[FIELD_TYPE_NUMBER] = NULL,
	[FIELD_TYPE_DOUBLE] = "g",
	[FIELD_TYPE_INTEGER] = "l",
	[FIELD_TYPE_BOOLEAN] = "C",
	[FIELD_TYPE_VARBINARY] = NULL,
	[FIELD_TYPE_SCALAR] = NULL,
	[FIELD_TYPE_DECIMAL] = NULL,
	[FIELD_TYPE_UUID] = NULL,
	[FIELD_TYPE_DATETIME] = NULL,
	[FIELD_TYPE_INTERVAL] = NULL,
	[FIELD_TYPE_ARRAY] = NULL,
	[FIELD_TYPE_MAP] = NULL,
	[FIELD_TYPE_INT8] = "c",
	[FIELD_TYPE_UINT8] = "C",
	[FIELD_TYPE_INT16] = "s",
	[FIELD_TYPE_UINT16] = "S",
	[FIELD_TYPE_INT32] = "i",
	[FIELD_TYPE_UINT32] = "I",
	[FIELD_TYPE_INT64] = "l",
	[FIELD_TYPE_UINT64] = "L",
	[FIELD_TYPE_FLOAT32] = "f",
	[FIELD_TYPE_FLOAT64] = "g",
	[FIELD_TYPE_DECIMAL32] = NULL,
	[FIELD_TYPE_DECIMAL64] = NULL,
	[FIELD_TYPE_DECIMAL128] = NULL,
	[FIELD_TYPE_DECIMAL256] = NULL,
};

static_assert(lengthof(field_type_to_arrow_format) == field_type_MAX,
	      "Each field type must be present in field_type_to_arrow_format");

const char *
field_type_to_arrow_type(enum field_type field_type, bool use_view_layout)
{
	assert(field_type < field_type_MAX);
	if (field_type == FIELD_TYPE_STRING && use_view_layout)
		return "vu";
	return field_type_to_arrow_format[field_type];
}

int
arrow_check_field_type_supported(enum field_type field_type)
{
	assert(field_type < field_type_MAX);
	if (field_type_to_arrow_format[field_type] == NULL) {
		diag_set(ClientError, ER_UNSUPPORTED, "Arrow stream",
			 tt_sprintf("field type '%s'",
				    field_type_strs[field_type]));
		return -1;
	}
	return 0;
}

/**
 * True if arrow `array' contains at least one NULL value.
 */
static bool
arrow_array_has_nulls(struct ArrowArray *array)
{
	if (array->null_count > 0)
		return true;
	if (array->null_count == 0)
		return false;
	/*
	 * The number of nulls in the array wasn't calculated by the producer
	 * (null_count == -1), check the validity bitmap.
	 */
	const void *validity = array->buffers[0];
	assert(validity != NULL);
	size_t validity_size = DIV_ROUND_UP(array->length, CHAR_BIT);

	struct bit_iterator it;
	bit_iterator_init(&it, validity, validity_size, false);
	return bit_iterator_next(&it) != SIZE_MAX;
}

/**
 * Returns a tuple field from the `space' format, that corresponds to the name
 * in the `schema'. Also tuple field number is returned in `ret_fieldno'.
 * If not found, NULL is returned and diag is set.
 */
static struct tuple_field *
arrow_schema_get_tuple_field(struct ArrowSchema *schema, struct space *space,
			     uint32_t *ret_fieldno)
{
	assert(schema->n_children == 0);
	struct tuple_format *format = space->format;
	struct tuple_dictionary *dict = format->dict;
	const char *name = schema->name;
	size_t name_len = strlen(name);
	uint32_t name_hash = field_name_hash(name, name_len);
	if (tuple_fieldno_by_name(dict, name, name_len, name_hash,
				  ret_fieldno) != 0) {
		diag_set(ClientError, ER_NO_SUCH_FIELD_NAME_IN_SPACE,
			 name, space_name(space));
		return NULL;
	}
	return tuple_format_field(format, *ret_fieldno);
}

/**
 * Checks that a column (represented by `array' and `schema') contains values
 * that match `field' type and nullability.
 * Returns 0 on success, -1 on failure (diag is set).
 */
static int
arrow_validate_column(struct ArrowArray *array, struct ArrowSchema *schema,
		      struct space *space, struct tuple_field *field)
{
	assert(schema->format != NULL);
	assert(schema->n_children == 0);
	struct tuple_format *format = space->format;
	if (arrow_check_field_type_supported(field->type) != 0)
		return -1;
	const char *arrow_type = field_type_to_arrow_type(field->type, false);
	if (strcmp(schema->format, arrow_type) != 0) {
		diag_set(ClientError, ER_FIELD_TYPE,
			 tuple_field_path(field, format),
			 tt_sprintf("%s (%s)", arrow_type,
				    field_type_strs[field->type]),
			 schema->format);
		return -1;
	}
	if (!tuple_field_is_nullable(field) && arrow_array_has_nulls(array)) {
		diag_set(ClientError, ER_FIELD_TYPE,
			 tuple_field_path(field, format),
			 field_type_strs[field->type], "nil");
		return -1;
	}
	return 0;
}

int
arrow_validate_batch(struct ArrowArray *array, struct ArrowSchema *schema,
		     struct space *space, uint32_t *fields)
{
	if (schema->n_children == 0)
		return 0;
	int rc = -1;
	struct tuple_format *format = space->format;
	struct region *gc = &fiber()->gc;
	size_t gc_svp = region_used(gc);
	void *field_mask = xregion_alloc(gc, BITMAP_SIZE(schema->n_children));
	memset(field_mask, 0, BITMAP_SIZE(schema->n_children));
	for (uint32_t i = 0; i < schema->n_children; i++) {
		struct tuple_field *field;
		uint32_t fieldno;
		field = arrow_schema_get_tuple_field(schema->children[i], space,
						     &fieldno);
		if (field == NULL)
			goto out;
		if (arrow_validate_column(array->children[i],
					  schema->children[i],
					  space, field) != 0)
			goto out;
		if (bit_test(field_mask, fieldno)) {
			diag_set(ClientError, ER_SPACE_FIELD_IS_DUPLICATE,
				 tuple_field_path(field, format));
			goto out;
		}
		bit_set(field_mask, fieldno);
		fields[i] = fieldno;
	}
	for (uint32_t i = 0; i < format->exact_field_count; i++) {
		struct tuple_field *field = tuple_format_field(format, i);
		if (!bit_test(field_mask, i) &&
		    !tuple_field_is_nullable(field)) {
			diag_set(ClientError, ER_FIELD_MISSING,
				 tuple_field_path(field, format));
			goto out;
		}
	}
	rc = 0;
out:
	region_truncate(gc, gc_svp);
	return rc;
}
