#ifndef TARANTOOL_BOX_TUPLE_FORMAT_H_INCLUDED
#define TARANTOOL_BOX_TUPLE_FORMAT_H_INCLUDED
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
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
#include "field_def.h"
#include "errinj.h"
#include "json/json.h"
#include "tuple_dictionary.h"
#include "field_map.h"
#include "index.h"
#include "field_default_func.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Destroy tuple format subsystem and free resourses
 */
void
tuple_format_free();

enum { FORMAT_ID_MAX = UINT16_MAX };

/*
 * We don't pass TUPLE_INDEX_BASE around dynamically all the time,
 * at least hard code it so that in most cases it's a nice error
 * message
 */
enum { TUPLE_INDEX_BASE = 1 };
/*
 * A special value to indicate that tuple format doesn't store
 * an offset for a field_id.
 */
enum { TUPLE_OFFSET_SLOT_NIL = INT32_MAX };

struct tuple;
struct tuple_info;
struct tuple_format;
struct coll;
struct mpstream;

/** Engine-specific tuple format methods. */
struct tuple_format_vtab {
	/**
	 * Free allocated tuple using engine-specific
	 * memory allocator.
	 */
	void
	(*tuple_delete)(struct tuple_format *format, struct tuple *tuple);
	/**
	 * Allocates a new tuple on the same allocator
	 * and with the same format.
	 */
	struct tuple*
	(*tuple_new)(struct tuple_format *format, const char *data,
	             const char *end);
	/**
	 * Fill `tuple_info' with the engine-specific and allocator-specific
	 * information about the `tuple'.
	 */
	void
	(*tuple_info)(struct tuple_format *format, struct tuple *tuple,
		      struct tuple_info *tuple_info);
};

struct tuple_constraint;

/** Tuple field default value. */
struct field_default_value {
	/**
	 * MsgPack with the static default value, if func is NULL.
	 * Otherwise `data` contains an optional argument of func.
	 */
	char *data;
	/** Size of the data. */
	size_t size;
	/** Functional default value. */
	struct field_default_func func;
};

/** Tuple field meta information for tuple_format. */
struct tuple_field {
	/** Unique field identifier. */
	uint32_t id;
	/**
	 * Field type of an indexed field.
	 * If a field participates in at least one of space indexes
	 * then its type is stored in this member.
	 * If a field does not participate in an index
	 * then UNKNOWN is stored for it.
	 */
	enum field_type type;
	/**
	 * Offset slot in field map in tuple. Normally tuple
	 * stores field map - offsets of all fields participating
	 * in indexes. This allows quick access to most used
	 * fields without parsing entire mspack. This member
	 * stores position in the field map of tuple for current
	 * field. If the field does not participate in indexes
	 * then it has no offset in field map and INT_MAX is
	 * stored in this member. Due to specific field map in
	 * tuple (it is stored before tuple), the positions in
	 * field map is negative.
	 */
	int32_t offset_slot;
	/** True if this field is used by an index. */
	bool is_key_part;
	/** True if this field is used by multikey index. */
	bool is_multikey_part;
	/** Action to perform if NULL constraint failed. */
	enum on_conflict_action nullable_action;
	/** Collation definition for string comparison */
	struct coll *coll;
	/** Collation identifier. */
	uint32_t coll_id;
	/** Type of compression for this field. */
	enum compression_type compression_type;
	/**
	 * Bitmap of fields that must be present in a tuple
	 * conforming to the multikey subtree. Not NULL only
	 * when tuple_field:token.type == JSON_TOKEN_ANY.
	 * Indexed by tuple_field::id.
	 */
	void *multikey_required_fields;
	/** Link in tuple_format::fields. */
	struct json_token token;
	/**
	 * Array of constraints. Can be NULL if constraints_count == 0.
	 * Strings of constraints are allocated in the same memory block
	 * right after the array.
	 */
	struct tuple_constraint *constraint;
	/** Number of constraints. */
	uint32_t constraint_count;
	/** Tuple field default value. */
	struct field_default_value default_value;
};

/**
 * Get is_nullable property of tuple_field.
 * @param tuple_field for which attribute is being fetched
 *
 * @retval boolean nullability attribute
 */
static inline bool
tuple_field_is_nullable(const struct tuple_field *tuple_field)
{
	return tuple_field->nullable_action == ON_CONFLICT_ACTION_NONE;
}

/**
 * Return true if tuple_field has a functional default value, i.e. the value
 * is generated by a function call.
 */
static inline bool
tuple_field_has_default_func(const struct tuple_field *tuple_field)
{
	return tuple_field->default_value.func.id > 0;
}

/**
 * Return true if tuple_field has a default value.
 */
static inline bool
tuple_field_has_default(const struct tuple_field *tuple_field)
{
	return tuple_field_has_default_func(tuple_field) ||
	       tuple_field->default_value.data != NULL;
}

/**
 * Return true for fixed-size integer field `type'.
 */
static inline bool
tuple_field_type_is_fixed_int(enum field_type type)
{
	assert(type < field_type_MAX);
	return field_type_is_fixed_signed[type] ||
	       field_type_is_fixed_unsigned[type];
}

/**
 * Return path to a tuple field. Used for error reporting.
 */
const char *
tuple_field_path(const struct tuple_field *field,
		 const struct tuple_format *format);

/**
 * @brief Tuple format
 * Tuple format describes how tuple is stored and information about its fields
 */
struct tuple_format {
	/** Virtual function table */
	struct tuple_format_vtab vtab;
	/** Pointer to engine-specific data. */
	void *engine;
	/** Identifier */
	uint32_t id;
	/**
	 * Hash computed from this format. Does not include the
	 * tuple dictionary. Used only for sharing formats among
	 * ephemeral spaces.
	 */
	uint32_t hash;
	/**
	 * Counter that grows incrementally on space rebuild
	 * used for caching offset slot in key_part, for more
	 * details see key_part::offset_slot_cache.
	 */
	uint64_t epoch;
	/** Reference counter */
	int64_t refs;
	/**
	 * Tuples of this format belong to a data-temporary space and
	 * hence can be freed immediately while checkpointing is
	 * in progress.
	 */
	bool is_temporary;
	/**
	 * True if this format may be reused instead of creating a new format.
	 * Not all formats are reusable: a typical space format is mutable,
	 * because its dictionary may be updated by space alter, and therefore
	 * can't be reused. We can reuse formats of ephemeral spaces, because
	 * those are never altered. We can also reuse formats exported to Lua.
	 */
	bool is_reusable;
	/** True if tuples of this format may contain compressed fields. */
	bool is_compressed;
	/**
	 * Size of minimal field map of tuple where each indexed
	 * field has own offset slot (in bytes). The real tuple
	 * field_map may be bigger in case of multikey indexes.
	 * \sa struct field_map_builder
	 */
	uint16_t field_map_size;
	/**
	 * If not set (== 0), any tuple in the space can have any number of
	 * fields. If set, each tuple must have exactly this number of fields.
	 */
	uint32_t exact_field_count;
	/**
	 * The longest field array prefix in which the last
	 * element is used by an index.
	 */
	uint32_t index_field_count;
	/**
	 * The minimal field count that must be specified.
	 * index_field_count <= min_field_count <= field_count.
	 */
	uint32_t min_field_count;
	/**
	 * Total number of formatted fields, including JSON
	 * path fields. See also tuple_format::fields.
	 */
	uint32_t total_field_count;
	/**
	 * An upper bound for the number of fields with a default value.
	 * In other words, max fieldno with a default value + 1.
	 */
	uint32_t default_field_count;
	/**
	 * Bitmap of fields that must be present in a tuple
	 * conforming to the format. Indexed by tuple_field::id.
	 */
	void *required_fields;
	/**
	 * Shared names storage used by all formats of a space.
	 */
	struct tuple_dictionary *dict;
	/**
	 * A maximum depth of format::fields subtree.
	 */
	uint32_t fields_depth;
	/**
	 * Fields comprising the format, organized in a tree.
	 * First level nodes correspond to tuple fields.
	 * Deeper levels define indexed JSON paths within
	 * tuple fields. Nodes of the tree are linked by
	 * tuple_field::token.
	 */
	struct json_tree fields;
	/**
	 * Array of constraints. Can be NULL if constraints_count == 0.
	 * Strings of constraints are allocated in the same memory block
	 * right after the array.
	 */
	struct tuple_constraint *constraint;
	/** Number of constraints. */
	uint32_t constraint_count;
	/**
	 * Encoding of original (i.e., user-provided) format clause to MsgPack,
	 * allocated via malloc.
	 */
	char *data;
	/** Length of MsgPack encoding. */
	size_t data_len;
};

/**
 * Return the number of top-level tuple fields defined by
 * a given format.
 */
static inline uint32_t
tuple_format_field_count(struct tuple_format *format)
{
	const struct json_token *root = &format->fields.root;
	return root->children != NULL ? root->max_child_idx + 1 : 0;
}

/**
 * Return meta information of a tuple field given a format,
 * field index and path.
 */
static inline struct tuple_field *
tuple_format_field_by_path(struct tuple_format *format, uint32_t fieldno,
			   const char *path, uint32_t path_len, int index_base)
{
	assert(fieldno < tuple_format_field_count(format));
	struct json_token token;
	token.type = JSON_TOKEN_NUM;
	token.num = fieldno;
	struct tuple_field *root =
		json_tree_lookup_entry(&format->fields, &format->fields.root,
				       &token, struct tuple_field, token);
	assert(root != NULL);
	if (path == NULL)
		return root;
	return json_tree_lookup_path_entry(&format->fields, &root->token,
					   path, path_len, index_base,
					   struct tuple_field, token);
}

/**
 * Return meta information of a top-level tuple field given
 * a format and a field index.
 */
static inline struct tuple_field *
tuple_format_field(struct tuple_format *format, uint32_t fieldno)
{
	return tuple_format_field_by_path(format, fieldno, NULL, 0, 0);
}

extern struct tuple_format *tuple_formats[];

static inline uint32_t
tuple_format_id(struct tuple_format *format)
{
	assert(tuple_formats[format->id] == format);
	return format->id;
}

static inline struct tuple_format *
tuple_format_by_id(uint32_t tuple_format_id)
{
	return tuple_formats[tuple_format_id];
}

/** Delete a format with zero ref count. */
void
tuple_format_delete(struct tuple_format *format);

static inline void
tuple_format_ref(struct tuple_format *format)
{
	format->refs++;
}

static inline void
tuple_format_unref(struct tuple_format *format)
{
	assert(format->refs >= 1);
	if (--format->refs == 0)
		tuple_format_delete(format);
}

/**
 * Allocate, construct and register a new in-memory tuple format.
 * @param vtab Virtual function table for specific engines.
 * @param engine Pointer to storage engine.
 * @param keys Array of key_defs of a space.
 * @param key_count The number of keys in @a keys array.
 * @param space_fields Array of fields, defined in a space format.
 * @param space_field_count Length of @a space_fields.
 * @param exact_field_count Exact field count for format.
 * @param is_temporary Set if format belongs to data-temporary space.
 * @param is_reusable Set if format may be reused.
 * @param constraint_def - Array of constraint definitions.
 * @param constraint_count - Number of constraints above.
 * @param format_data Original format clause encoded to Msgpack (may be NULL).
 * @param format_data_len Length of MsgPack encoded format clause (may be 0).
 *
 * @retval not NULL Tuple format.
 * @retval     NULL Memory error.
 */
struct tuple_format *
tuple_format_new(struct tuple_format_vtab *vtab, void *engine,
		 struct key_def * const *keys, uint16_t key_count,
		 const struct field_def *space_fields,
		 uint32_t space_field_count, uint32_t exact_field_count,
		 struct tuple_dictionary *dict, bool is_temporary,
		 bool is_reusable, struct tuple_constraint_def *constraint_def,
		 uint32_t constraint_count, const char *format_data,
		 size_t format_data_len);

/**
 * Check, if tuple @a format is compatible with @a key_def.
 * This function return false and set diag in case when tuple
 * @a format is not compatible with @a key_def.
 */
bool
tuple_format_is_compatible_with_key_def(struct tuple_format *format,
					struct key_def *key_def);

/**
 * Simple form of @sa tuple_format_create without the most of arguments.
 * Omitted arguments are treated as 0/NULL/false depending on type.
 */
static inline struct tuple_format *
simple_tuple_format_new(struct tuple_format_vtab *vtab, void *engine,
			struct key_def * const *keys, uint16_t key_count)
{
	return tuple_format_new(vtab, engine, keys, key_count,
				NULL, 0, 0, NULL, false, false, NULL, 0, NULL,
				0);
}

/**
 * Check, if @a format1 can store any tuples of @a format2. For
 * example, if a field is not nullable in format1 and the same
 * field is nullable in format2, or the field type is integer
 * in format1 and unsigned in format2, then format1 can not store
 * format2 tuples.
 * @param format1 tuple format to check for compatibility of
 * @param format2 tuple format to check compatibility with
 *
 * @retval True, if @a format1 can store any tuples of @a format2.
 */
bool
tuple_format1_can_store_format2_tuples(struct tuple_format *format1,
				       struct tuple_format *format2);

/**
 * Calculate minimal field count of tuples with specified keys and
 * space format.
 * @param keys Array of key definitions of indexes.
 * @param key_count Length of @a keys.
 * @param space_fields Array of fields from a space format.
 * @param space_field_count Length of @a space_fields.
 *
 * @retval Minimal field count.
 */
uint32_t
tuple_format_min_field_count(struct key_def * const *keys, uint16_t key_count,
			     const struct field_def *space_fields,
			     uint32_t space_field_count);

/**
 * Return true if format has at least one field with a default value.
 */
static inline bool
tuple_format_has_defaults(const struct tuple_format *format)
{
	return format->default_field_count > 0;
}

/**
 * Return true if format has at least one field with a functional default value.
 */
static inline bool
tuple_format_has_default_funcs(struct tuple_format *format)
{
	for (size_t i = 0; i < format->default_field_count; ++i) {
		if (tuple_field_has_default_func(tuple_format_field(format, i)))
			return true;
	}
	return false;
}

typedef struct tuple_format box_tuple_format_t;

/** \cond public */

/**
 * Return new in-memory tuple format based on passed key definitions.
 *
 * \param keys array of keys defined for the format
 * \param key_count count of keys
 * \retval new tuple format if success
 * \retval NULL for error
 */
box_tuple_format_t *
box_tuple_format_new(struct key_def **keys, uint16_t key_count);

/**
 * Increment tuple format ref count.
 *
 * \param format the tuple format to ref
 */
void
box_tuple_format_ref(box_tuple_format_t *format);

/**
 * Decrement tuple format ref count.
 *
 * \param format the tuple format to unref
 */
void
box_tuple_format_unref(box_tuple_format_t *format);

/** \endcond public */

/**
 * Allocate a field map for the given tuple on the region.
 *
 * @param format    Tuple format.
 * @param tuple     MessagePack array.
 * @param validate  If set, validate the tuple against the format.
 * @param builder[out] The pointer to field map builder object to
 *                     be prepared.
 *
 * @retval  0 Success.
 * @retval -1 Format error.
 *            +-------------------+
 * Result:    | offN | ... | off1 |
 *            +-------------------+
 *                                ^
 *                             field_map
 * tuple + off_i = indexed_field_i;
 */
int
tuple_field_map_create(struct tuple_format *format, const char *tuple,
		       bool validate, struct field_map_builder *builder);

/**
 * Initialize tuple format subsystem.
 */
void
tuple_format_init();


/** Tuple format iterator flags to configure parse mode. */
enum {
	/**
	 * This flag is set for iterator that should perform tuple
	 * validation to conform the specified format.
	 */
	TUPLE_FORMAT_ITERATOR_VALIDATE		= 1 << 0,
	/**
	 * This flag is set for iterator that should skip the
	 * tuple fields that are not marked as key_parts in
	 * format::fields tree.
	 */
	TUPLE_FORMAT_ITERATOR_KEY_PARTS_ONLY 	= 1 << 1,
};

/**
 * A tuple msgpack iterator that decodes the tuple and returns
 * only fields that are described in the tuple_format.
 */
struct tuple_format_iterator {
	/** The current read position in msgpack. */
	const char *pos;
	/**
	 * Tuple format is used to perform field lookups in
	 * format::fields JSON tree.
	 */
	struct tuple_format *format;
	/** The combination of tuple format iterator flags. */
	uint8_t flags;
	/**
	 * Traversal stack of msgpack frames is used to determine
	 * when the parsing of the current composite mp structure
	 * (array or map) is completed to update to the parent
	 * pointer accordingly.
	 *
	 * Stack has the size equal to the maximum depth of the
	 * indexed field in the format::fields tree
	 * (format::fields_depth).
	 */
	struct mp_stack stack;
	/**
	 * The pointer to the stack frame representing an array
	 * filed that has JSON_TOKEN_ANY child, i.e. the root
	 * of the multikey index.
	 */
	struct mp_frame *multikey_frame;
	/**
	 * The pointer to the parent node in the format::fields
	 * JSON tree. Is required for relative lookup for the
	 * next field.
	 */
	struct json_token *parent;
	/**
	 * The size of validation bitmasks required_fields and
	 * multikey_required_fields.
	 */
	uint32_t required_fields_sz;
	/**
	 * Field bitmap that is used for checking that all
	 * mandatory fields are present.
	 * Not NULL iff validate == true.
	 */
	void *required_fields;
	/**
	 * Field bitmap that is used for checking that all
	 * mandatory fields of multikey subtree are present.
	 * Not NULL iff validate == true.
	 */
	void *multikey_required_fields;
};

/** Tuple format iterator next method's returning entry. */
struct tuple_format_iterator_entry {
	/** Pointer to the tuple field data. NULL if EOF. */
	const char *data;
	/**
	 * Pointer to the end of tuple field data.
	 * Is defined only for leaf fields
	 * (json_token_is_leaf(&field->token) == true).
	 */
	const char *data_end;
	/**
	 * Format field metadata that represents the data field.
	 * May be NULL if the field isn't present in the format.
	 *
	 * (All child entries of an array are returned present in
	 * the format, no matter formatted or not)
	 */
	struct tuple_field *field;
	/**
	 * Format parent field metadata. NULL for top-level
	 * fields.
	 */
	struct tuple_field *parent;
	/**
	 * Number of child entries of the analyzed field that has
	 * container type. Is defined for intermediate fields.
	 * (field->type in FIELD_TYPE_ARRAY, FIELD_TYPE_MAP).
	 */
	int count;
	/**
	 * Number of multikey items. Is defined when iterator is
	 * positioned on tuple field is related to format's
	 * multikey subtree (there was a parent field1 that
	 * json_token_is_multikey(&field1->token) == true)
	 */
	int multikey_count;
	/**
	 * Index of the key in the multikey array. Is defined
	 * when multikey_count is defined.
	 */
	int multikey_idx;
};

/**
 * Initialize tuple decode iterator with tuple format and tuple
 * data pointer.
 *
 * Function uses the region to allocate the traversal stack
 * and required fields bitmasks.
 *
 * Returns 0 on success. In case of error sets diag message and
 * returns -1.
 */
int
tuple_format_iterator_create(struct tuple_format_iterator *it,
			     struct tuple_format *format, const char *tuple,
			     uint8_t flags, uint32_t *field_count,
			     struct region *region);

/**
 * Perform tuple decode step and update iterator state.
 * Update entry pointer with actual format parse context.
 *
 * Returns 0 on success. In case of error sets diag message and
 * returns -1.
 */
int
tuple_format_iterator_next(struct tuple_format_iterator *it,
			   struct tuple_format_iterator_entry *entry);

/**
 * Replace null (or absent) fields of msgpack with the default values from the
 * format. The input msgpack is located at [*data .. *data_end).
 * Return true if at least one field is changed, in that case data and data_end
 * are updated to point to the new buffer with the modified msgpack. The buffer
 * is allocated on current fiber's region.
 *
 * Returns 0 on success. On error, sets diag and returns -1.
 */
int
tuple_format_apply_defaults(struct tuple_format *format, const char **data,
			    const char **data_end);

/**
 * Serialize a tuple format to a MsgPack stream.
 */
void
tuple_format_to_mpstream(struct tuple_format *format, struct mpstream *stream);

/**
 * Checks that `mp_data' is compatible with the `field' type defined in format,
 * checks that integer value is within an allowed range, also checks field
 * constraints, etc.
 *
 * Returns 0 on success. On error, sets diag and returns -1.
 */
int
tuple_field_validate(struct tuple_format *format, struct tuple_field *field,
		     const char *mp_data, const char *mp_data_end);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* #ifndef TARANTOOL_BOX_TUPLE_FORMAT_H_INCLUDED */
