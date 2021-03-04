#ifndef TARANTOOL_BOX_KEY_DEF_H_INCLUDED
#define TARANTOOL_BOX_KEY_DEF_H_INCLUDED
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
#include "trivia/util.h"
#include "error.h"
#include "diag.h"
#include <msgpuck.h>
#include "field_def.h"
#include "coll_id.h"
#include "tuple_compare.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/* Sorting order of a part. */
extern const char *sort_order_strs[];

/** \cond ffi */

enum sort_order {
	SORT_ORDER_ASC = 0,
	SORT_ORDER_DESC,
	SORT_ORDER_UNDEF,
	sort_order_MAX
};

/** \endcond ffi */

struct key_part_def {
	/** Tuple field index for this part. */
	uint32_t fieldno;
	/** Type of the tuple field. */
	enum field_type type;
	/** Collation ID for string comparison. */
	uint32_t coll_id;
	/** True if a key part can store NULLs. */
	bool is_nullable;
	/** Action to perform if NULL constraint failed. */
	enum on_conflict_action nullable_action;
	/** Part sort order. */
	enum sort_order sort_order;
	/**
	 * JSON path to indexed data, relative to the field number,
	 * or NULL if this key part indexes a top-level field.
	 * This sting is 0-terminated.
	 */
	const char *path;
	/** True if nulls are ignored by index. */
	bool exclude_null;
};

extern const struct key_part_def key_part_def_default;

/** Descriptor of a single part in a multipart key. */
struct key_part {
	/** Tuple field index for this part */
	uint32_t fieldno;
	/** Type of the tuple field */
	enum field_type type;
	/** Collation ID for string comparison. */
	uint32_t coll_id;
	/** Collation definition for string comparison */
	struct coll *coll;
	/** Action to perform if NULL constraint failed. */
	enum on_conflict_action nullable_action;
	/** True if nulls are ignored by index. */
	bool exclude_null;
	/** Part sort order. */
	enum sort_order sort_order;
	/**
	 * JSON path to indexed data, relative to the field number,
	 * or NULL if this key part index a top-level field.
	 * This string is not 0-terminated. String memory is
	 * allocated at the end of key_def.
	 */
	char *path;
	/** The length of JSON path. */
	uint32_t path_len;
	/**
	 * Epoch of the tuple format the offset slot cached in
	 * this part is valid for, see tuple_format::epoch.
	 */
	uint64_t format_epoch;
	/**
	 * Cached value of the offset slot corresponding to
	 * the indexed field (tuple_field::offset_slot).
	 * Valid only if key_part::format_epoch equals the epoch
	 * of the tuple format. This value is updated in
	 * tuple_field_raw_by_part to always store the
	 * offset corresponding to the last used tuple format.
	 */
	int32_t offset_slot_cache;
};

struct key_def;
struct tuple;

/**
 * Get is_nullable property of key_part.
 * @param key_part for which attribute is being fetched
 *
 * @retval boolean nullability attribute
 */
static inline bool
key_part_is_nullable(const struct key_part *part)
{
	return part->nullable_action == ON_CONFLICT_ACTION_NONE;
}

/** @copydoc tuple_compare_with_key() */
typedef int (*tuple_compare_with_key_t)(struct tuple *tuple,
					hint_t tuple_hint,
					const char *key,
					uint32_t part_count,
					hint_t key_hint,
					struct key_def *key_def);
/** @copydoc tuple_compare() */
typedef int (*tuple_compare_t)(struct tuple *tuple_a,
			       hint_t tuple_a_hint,
			       struct tuple *tuple_b,
			       hint_t tuple_b_hint,
			       struct key_def *key_def);
/** @copydoc tuple_extract_key() */
typedef char *(*tuple_extract_key_t)(struct tuple *tuple,
				     struct key_def *key_def,
				     int multikey_idx,
				     uint32_t *key_size);
/** @copydoc tuple_extract_key_raw() */
typedef char *(*tuple_extract_key_raw_t)(const char *data,
					 const char *data_end,
					 struct key_def *key_def,
					 int multikey_idx,
					 uint32_t *key_size);
/** @copydoc tuple_hash() */
typedef uint32_t (*tuple_hash_t)(struct tuple *tuple,
				 struct key_def *key_def);
/** @copydoc key_hash() */
typedef uint32_t (*key_hash_t)(const char *key,
				struct key_def *key_def);
/** @copydoc tuple_hint() */
typedef hint_t (*tuple_hint_t)(struct tuple *tuple,
			       struct key_def *key_def);
/** @copydoc key_hint() */
typedef hint_t (*key_hint_t)(const char *key, uint32_t part_count,
			     struct key_def *key_def);

/* Definition of a multipart key. */
struct key_def {
	/** @see tuple_compare() */
	tuple_compare_t tuple_compare;
	/** @see tuple_compare_with_key() */
	tuple_compare_with_key_t tuple_compare_with_key;
	/** @see tuple_extract_key() */
	tuple_extract_key_t tuple_extract_key;
	/** @see tuple_extract_key_raw() */
	tuple_extract_key_raw_t tuple_extract_key_raw;
	/** @see tuple_hash() */
	tuple_hash_t tuple_hash;
	/** @see key_hash() */
	key_hash_t key_hash;
	/** @see tuple_hint() */
	tuple_hint_t tuple_hint;
	/** @see key_hint() */
	key_hint_t key_hint;
	/**
	 * Minimal part count which always is unique. For example,
	 * if a secondary index is unique, then
	 * unique_part_count == secondary index part count. But if
	 * a secondary index is not unique, then
	 * unique_part_count == part count of a merged key_def.
	 */
	uint32_t unique_part_count;
	/** True, if at least one part can store NULL. */
	bool is_nullable;
	/** True if some key part has exclude_null option */
	bool has_exclude_null;
	/** True if some key part has JSON path. */
	bool has_json_paths;
	/** True if it is a multikey index definition.
	 * XXX Not used for multikey functional indexes,
	 * please use func->def.is_multikey instead.
	 */
	bool is_multikey;
	/** True if it is a functional index key definition. */
	bool for_func_index;
	/**
	 * True, if some key parts can be absent in a tuple. These
	 * fields assumed to be MP_NIL.
	 */
	bool has_optional_parts;
	/** Key fields mask. @sa column_mask.h for details. */
	uint64_t column_mask;
	/**
	 * A pointer to a functional index function.
	 * Initially set to NULL and is initialized when the
	 * record in _func_index is handled by a respective trigger.
	 * The reason is that we may not yet have a defined
	 * function when a functional index is defined. E.g.
	 * during recovery, we recovery _index first, and _func
	 * second, so when recovering _index no func object is
	 * loaded in the cache and nothing can be assigned.
	 * Once a pointer is assigned its life cycle is guarded by
	 * a check in _func on_replace trigger in alter.cc which
	 * would not let anyone change a function until it is
	 * referenced by a functional index.
	 * In future, one will be able to update a function of
	 * a functional index by disabling the index, thus
	 * clearing this pointer, modifying the function, and
	 * enabling/rebuilding the index.
	 */
	struct func *func_index_func;
	/**
	 * In case of the multikey index, a pointer to the
	 * JSON path string, the path to the root node of
	 * multikey index that contains the array having
	 * index placeholder sign [*].
	 *
	 * This pointer duplicates the JSON path of some key_part.
	 * This path is not 0-terminated. Moreover, it is only
	 * JSON path subpath so key_def::multikey_path_len must
	 * be directly used in all cases.
	 *
	 * This field is not NULL iff this is multikey index
	 * key definition.
	 */
	const char *multikey_path;
	/**
	 * The length of the key_def::multikey_path.
	 * Valid when key_def->is_multikey is true,
	 * undefined otherwise.
	 */
	uint32_t multikey_path_len;
	/**
	 * The index of the root field of the multikey JSON
	 * path index key_def::multikey_path.
	 * Valid when key_def->is_multikey is true,
	 * undefined otherwise.
	*/
	uint32_t multikey_fieldno;
	/** The size of the 'parts' array. */
	uint32_t part_count;
	/** Description of parts of a multipart index. */
	struct key_part parts[];
};

/**
 * Duplicate key_def.
 * @param src Original key_def.
 *
 * @retval not NULL Duplicate of src.
 * @retval     NULL Memory error.
 */
struct key_def *
key_def_dup(const struct key_def *src);

/**
 * Copy content of key definition src to dest.
 * The two key definitions must have the same size.
 */
void
key_def_copy(struct key_def *dest, const struct key_def *src);

/**
 * Delete @a key_def.
 * @param def Key_def to delete.
 */
void
key_def_delete(struct key_def *def);

typedef struct tuple box_tuple_t;

/* {{{ Module API */

/** \cond public */

typedef struct key_def box_key_def_t;

/** Key part definition flags. */
enum {
	BOX_KEY_PART_DEF_IS_NULLABLE = 1 << 0,
	BOX_KEY_PART_DEF_EXCLUDE_NULL = 1 << 1,
};

/**
 * It is recommended to verify size of <box_key_part_def_t>
 * against this constant on the module side at build time.
 * Example:
 *
 * | #if !defined(__cplusplus) && !defined(static_assert)
 * | #define static_assert _Static_assert
 * | #endif
 * |
 * | (slash)*
 * |  * Verify that <box_key_part_def_t> has the same size when
 * |  * compiled within tarantool and within the module.
 * |  *
 * |  * It is important, because the module allocates an array of key
 * |  * parts and passes it to <box_key_def_new_v2>() tarantool
 * |  * function.
 * |  *(slash)
 * | static_assert(sizeof(box_key_part_def_t) == BOX_KEY_PART_DEF_T_SIZE,
 * |               "sizeof(box_key_part_def_t)");
 *
 * This snippet is not part of module.h, because portability of
 * static_assert() / _Static_assert() is dubious. It should be
 * decision of a module author how portable its code should be.
 */
enum {
	BOX_KEY_PART_DEF_T_SIZE = 64,
};

/**
 * Public representation of a key part definition.
 *
 * Usage: Allocate an array of such key parts, initialize each
 * key part (call <box_key_part_def_create>() and set necessary
 * fields), pass the array into <box_key_def_new_v2>() function.
 *
 * Important: A module should call <box_key_part_def_create>()
 * to initialize the structure with default values. There is no
 * guarantee that all future default values for fields and flags
 * will be remain the same.
 *
 * The idea of separation from internal <struct key_part_def> is
 * to provide stable API and ABI for modules.
 *
 * New fields may be added into the end of the structure in later
 * tarantool versions. Also new flags may be introduced within
 * <flags> field. <collation> cannot be changed to a union (to
 * reuse for some other value), because it is verified even for
 * a non-string key part by <box_key_def_new_v2>().
 *
 * Fields that are unknown at given tarantool version are ignored
 * in general, but filled with zeros when initialized.
 */
typedef union PACKED {
	struct {
		/** Index of a tuple field (zero based). */
		uint32_t fieldno;
		/** Flags, e.g. nullability. */
		uint32_t flags;
		/** Type of the tuple field. */
		const char *field_type;
		/** Collation name for string comparisons. */
		const char *collation;
		/**
		 * JSON path to point a nested field.
		 *
		 * Example:
		 *
		 * tuple: [1, {"foo": "bar"}]
		 * key parts: [
		 *     {
		 *         "fieldno": 2,
		 *         "type": "string",
		 *         "path": "foo"
		 *     }
		 * ]
		 *
		 * => key: ["bar"]
		 *
		 * Note: When the path is given, <field_type>
		 * means type of the nested field.
		 */
		const char *path;
	};
	/**
	 * Padding to guarantee certain size across different
	 * tarantool versions.
	 */
	char padding[BOX_KEY_PART_DEF_T_SIZE];
} box_key_part_def_t;

/**
 * Create key definition with given field numbers and field types.
 *
 * May be used for tuple format creation and/or tuple comparison.
 *
 * \sa <box_key_def_new_v2>().
 *
 * \param fields array with key field identifiers
 * \param types array with key field types (see enum field_type)
 * \param part_count the number of key fields
 * \returns a new key definition object
 */
API_EXPORT box_key_def_t *
box_key_def_new(uint32_t *fields, uint32_t *types, uint32_t part_count);

/**
 * Initialize a key part with default values.
 *
 *  | Field       | Default value   | Details |
 *  | ----------- | --------------- | ------- |
 *  | fieldno     | 0               |         |
 *  | flags       | <default flags> |         |
 *  | field_type  | NULL            | [^1]    |
 *  | collation   | NULL            |         |
 *  | path        | NULL            |         |
 *
 * Default flag values are the following:
 *
 *  | Flag                          | Default value |
 *  | ----------------------------- | ------------- |
 *  | BOX_KEY_PART_DEF_IS_NULLABLE  | 0 (unset)     |
 *  | BOX_KEY_PART_DEF_EXCLUDE_NULL | 0 (unset)     |
 *
 * Default values of fields and flags are permitted to be changed
 * in future tarantool versions. However we should be VERY
 * conservative here and consider any meaningful usage scenarios,
 * when doing so. At least new defaults should be consistent with
 * how tarantool itself doing key_def related operations:
 * validation, key extraction, comparisons and so on.
 *
 * All trailing padding bytes are set to zero. The same for
 * unknown <flags> bits.
 *
 * [^1]: <box_key_def_new_v2>() does not accept NULL as a
 *       <field_type>, so it should be filled explicitly.
 */
API_EXPORT void
box_key_part_def_create(box_key_part_def_t *part);

/**
 * Create a key_def from given key parts.
 *
 * Unlike <box_key_def_new>() this function allows to define
 * nullability, collation and other options for each key part.
 *
 * <box_key_part_def_t> fields that are unknown at given tarantool
 * version are ignored. The same for unknown <flags> bits.
 *
 * In case of an error set a diag and return NULL.
 * @sa <box_error_last>().
 */
API_EXPORT box_key_def_t *
box_key_def_new_v2(box_key_part_def_t *parts, uint32_t part_count);

/**
 * Duplicate key_def.
 * @param key_def Original key_def.
 *
 * @retval not NULL Duplicate of src.
 * @retval     NULL Memory error.
 */
API_EXPORT box_key_def_t *
box_key_def_dup(const box_key_def_t *key_def);

/**
 * Delete key definition
 *
 * \param key_def key definition to delete
 */
API_EXPORT void
box_key_def_delete(box_key_def_t *key_def);

/**
 * Dump key part definitions of given key_def.
 *
 * The function allocates key parts and storage for pointer fields
 * (e.g. collation names) on the box region.
 * @sa <box_region_truncate>().
 *
 * <box_key_part_def_t> fields that are unknown at given tarantool
 * version are set to zero. The same for unknown <flags> bits.
 *
 * In case of an error set a diag and return NULL.
 * @sa <box_error_last>().
 */
API_EXPORT box_key_part_def_t *
box_key_def_dump_parts(const box_key_def_t *key_def, uint32_t *part_count_ptr);

/**
 * Check that tuple fields match with given key definition.
 *
 * @param key_def  Key definition.
 * @param tuple    Tuple to validate.
 *
 * @retval 0   The tuple is valid.
 * @retval -1  The tuple is invalid.
 *
 * In case of an invalid tuple set a diag and return -1.
 * @sa <box_error_last>().
 */
API_EXPORT int
box_key_def_validate_tuple(box_key_def_t *key_def, box_tuple_t *tuple);

/**
 * Compare tuples using the key definition.
 * @param tuple_a first tuple
 * @param tuple_b second tuple
 * @param key_def key definition
 * @retval 0  if key_fields(tuple_a) == key_fields(tuple_b)
 * @retval <0 if key_fields(tuple_a) < key_fields(tuple_b)
 * @retval >0 if key_fields(tuple_a) > key_fields(tuple_b)
 */
API_EXPORT int
box_tuple_compare(box_tuple_t *tuple_a, box_tuple_t *tuple_b,
		  box_key_def_t *key_def);

/**
 * @brief Compare tuple with key using the key definition.
 * @param tuple tuple
 * @param key key with MessagePack array header
 * @param key_def key definition
 *
 * @retval 0  if key_fields(tuple) == parts(key)
 * @retval <0 if key_fields(tuple) < parts(key)
 * @retval >0 if key_fields(tuple) > parts(key)
 */

API_EXPORT int
box_tuple_compare_with_key(box_tuple_t *tuple_a, const char *key_b,
			   box_key_def_t *key_def);

/**
 * Allocate a new key_def with a set union of key parts from
 * first and second key defs.
 *
 * Parts of the new key_def consist of the first key_def's parts
 * and those parts of the second key_def that were not among the
 * first parts.
 *
 * @retval not NULL  Ok.
 * @retval NULL      Memory error.
 *
 * In case of an error set a diag and return NULL.
 * @sa <box_error_last>().
 */
API_EXPORT box_key_def_t *
box_key_def_merge(const box_key_def_t *first, const box_key_def_t *second);

/**
 * Extract key from tuple by given key definition and return
 * buffer allocated on the box region with this key.
 * @sa <box_region_truncate>().
 *
 * This function has O(n) complexity, where n is the number of key
 * parts.
 *
 * @param key_def       Definition of key that need to extract.
 * @param tuple         Tuple from which need to extract key.
 * @param multikey_idx  Multikey index hint or -1.
 * @param key_size_ptr  Here will be size of extracted key.
 *
 * @retval not NULL  Success.
 * @retval NULL      Memory allocation error.
 *
 * In case of an error set a diag and return NULL.
 * @sa <box_error_last>().
 */
API_EXPORT char *
box_key_def_extract_key(box_key_def_t *key_def, box_tuple_t *tuple,
			int multikey_idx, uint32_t *key_size_ptr);

/**
 * Check a key against given key definition.
 *
 * Verifies key parts against given key_def's field types with
 * respect to nullability.
 *
 * A partial key (with less part than defined in @a key_def) is
 * verified by given key parts, the omitted tail is not verified
 * anyhow.
 *
 * Note: nil is accepted for nullable fields, but only for them.
 *
 * @param key_def       Key definition.
 * @param key           MessagePack'ed data for matching.
 * @param key_size_ptr  Here will be size of the validated key.
 *
 * @retval 0   The key is valid.
 * @retval -1  The key is invalid.
 *
 * In case of an invalid key set a diag and return -1.
 * @sa <box_error_last>().
 */
API_EXPORT int
box_key_def_validate_key(const box_key_def_t *key_def, const char *key,
			 uint32_t *key_size_ptr);

/**
 * Check a full key against given key definition.
 *
 * Verifies key parts against given key_def's field types with
 * respect to nullability.
 *
 * Imposes the same parts count in @a key as in @a key_def.
 * Absense of trailing key parts fails the check.
 *
 * Note: nil is accepted for nullable fields, but only for them.
 *
 * @param key_def       Key definition.
 * @param key           MessagePack'ed data for matching.
 * @param key_size_ptr  Here will be size of the validated key.
 *
 * @retval 0   The key is valid.
 * @retval -1  The key is invalid.
 *
 * In case of an invalid key set a diag and return -1.
 * @sa <box_error_last>().
 */
API_EXPORT int
box_key_def_validate_full_key(const box_key_def_t *key_def, const char *key,
			      uint32_t *key_size_ptr);

/** \endcond public */

/*
 * Size of the structure should remain the same across all
 * tarantool versions in order to allow to allocate an array of
 * them.
 */
static_assert(sizeof(box_key_part_def_t) == BOX_KEY_PART_DEF_T_SIZE,
	      "sizeof(box_key_part_def_t)");

/* }}} Module API */

static inline size_t
key_def_sizeof(uint32_t part_count, uint32_t path_pool_size)
{
	return sizeof(struct key_def) + sizeof(struct key_part) * part_count +
	       path_pool_size;
}

/**
 * Allocate a new key_def with the given part count
 * and initialize its parts.
 */
struct key_def *
key_def_new(const struct key_part_def *parts, uint32_t part_count,
	    bool for_func_index);

/**
 * Dump part definitions of the given key def.
 * The region is used for allocating JSON paths, if any.
 * Return -1 on memory allocation error, 0 on success.
 */
int
key_def_dump_parts(const struct key_def *def, struct key_part_def *parts,
		   struct region *region);

/**
 * Update 'has_optional_parts' of @a key_def with correspondence
 * to @a min_field_count.
 * @param def Key definition to update.
 * @param min_field_count Minimal field count. All parts out of
 *        this value are optional.
 */
void
key_def_update_optionality(struct key_def *def, uint32_t min_field_count);

/**
 * An snprint-style function to print a key definition.
 */
int
key_def_snprint_parts(char *buf, int size, const struct key_part_def *parts,
		      uint32_t part_count);

/**
 * Return size of key parts array when encoded in MsgPack.
 * See also key_def_encode_parts().
 */
size_t
key_def_sizeof_parts(const struct key_part_def *parts, uint32_t part_count);

/**
 * Encode key parts array in MsgPack and return a pointer following
 * the end of encoded data.
 */
char *
key_def_encode_parts(char *data, const struct key_part_def *parts,
		     uint32_t part_count);

/**
 * Decode parts array from tuple field and write'em to index_def structure.
 * Throws a nice error about invalid types, but does not check ranges of
 *  resulting values field_no and field_type
 * Parts expected to be a sequence of <part_count> arrays like this:
 *  [NUM, STR, ..][NUM, STR, ..]..,
 *  OR
 *  {field=NUM, type=STR, ..}{field=NUM, type=STR, ..}..,
 * The region is used for allocating JSON paths, if any.
 */
int
key_def_decode_parts(struct key_part_def *parts, uint32_t part_count,
		     const char **data, const struct field_def *fields,
		     uint32_t field_count, struct region *region);

/**
 * Returns the part in index_def->parts for the specified fieldno.
 * If fieldno is not in index_def->parts returns NULL.
 */
const struct key_part *
key_def_find_by_fieldno(const struct key_def *key_def, uint32_t fieldno);

/**
 * Returns the part in index_def->parts for the specified key part.
 * If to_find is not in index_def->parts returns NULL.
 */
const struct key_part *
key_def_find(const struct key_def *key_def, const struct key_part *to_find);

/**
 * Check if key definition @a first contains all parts of
 * key definition @a second.
 * @retval true if @a first is a superset of @a second
 * @retval false otherwise
 */
bool
key_def_contains(const struct key_def *first, const struct key_def *second);

/**
 * Allocate a new key_def with a set union of key parts from
 * first and second key defs. Parts of the new key_def consist
 * of the first key_def's parts and those parts of the second
 * key_def that were not among the first parts.
 * @retval not NULL Ok.
 * @retval NULL     Memory error.
 */
struct key_def *
key_def_merge(const struct key_def *first, const struct key_def *second);

/**
 * Create a key definition suitable for extracting primary key
 * parts from an extended secondary key.
 * @param cmp_def   Extended secondary key definition
 *                  (must include primary key parts).
 * @param pk_def    Primary key definition.
 * @param region    Region used for temporary allocations.
 * @retval not NULL Pointer to the extracted key definition.
 * @retval NULL     Memory allocation error.
 */
struct key_def *
key_def_find_pk_in_cmp_def(const struct key_def *cmp_def,
			   const struct key_def *pk_def,
			   struct region *region);

/*
 * Check that parts of the key match with the key definition.
 * @param key_def Key definition.
 * @param key MessagePack'ed data for matching.
 * @param part_count Field count in the key.
 * @param allow_nullable True if nullable parts are allowed.
 * @param key_end[out] The end of the validated key.
 *
 * @retval 0  The key is valid.
 * @retval -1 The key is invalid.
 */
int
key_validate_parts(const struct key_def *key_def, const char *key,
		   uint32_t part_count, bool allow_nullable,
		   const char **key_end);

/**
 * Return true if @a index_def defines a sequential key without
 * holes starting from the first field. In other words, for all
 * key parts index_def->parts[part_id].fieldno == part_id.
 * @param index_def index_def
 * @retval true index_def is sequential
 * @retval false otherwise
 */
static inline bool
key_def_is_sequential(const struct key_def *key_def)
{
	if (key_def->has_json_paths)
		return false;
	for (uint32_t part_id = 0; part_id < key_def->part_count; part_id++) {
		if (key_def->parts[part_id].fieldno != part_id)
			return false;
	}
	return true;
}

/**
 * Return true if @a key_def defines has fields that requires
 * special collation comparison.
 * @param key_def key_def
 * @retval true if the key_def has collation fields
 * @retval false otherwise
 */
static inline bool
key_def_has_collation(const struct key_def *key_def)
{
	for (uint32_t part_id = 0; part_id < key_def->part_count; part_id++) {
		if (key_def->parts[part_id].coll != NULL)
			return true;
	}
	return false;
}

/**
 * Return the first field type which can't be compared if @a key_def
 * has such. Otherwise return field_type_MAX value.
 */
static inline enum field_type
key_def_incomparable_type(const struct key_def *key_def)
{
	for (uint32_t i = 0; i < key_def->part_count; ++i) {
		if (key_def->parts[i].type == FIELD_TYPE_ANY ||
		    key_def->parts[i].type == FIELD_TYPE_ARRAY ||
		    key_def->parts[i].type == FIELD_TYPE_MAP) {
			/* Tuple comparators don't support these types. */
			return key_def->parts[i].type;
		}
	}
	return field_type_MAX;
}

/**
 * @brief Checks if \a field_type (MsgPack) is compatible \a type (KeyDef).
 * @param type KeyDef type
 * @param key Pointer to MsgPack field to be tested.
 * @param field_no - a field number (is used to store an error message)
 *
 * @retval 0  mp_type is valid.
 * @retval -1 mp_type is invalid.
 */
static inline int
key_part_validate(enum field_type key_type, const char *key,
		  uint32_t field_no, bool is_nullable)
{
	if (unlikely(!field_mp_type_is_compatible(key_type, key, is_nullable))) {
		diag_set(ClientError, ER_KEY_PART_TYPE, field_no,
			 field_type_strs[key_type]);
		return -1;
	}
	return 0;
}

/**
 * Compare two key part arrays.
 *
 * One key part is considered to be greater than the other if:
 * - its fieldno is greater
 * - given the same fieldno, NUM < STRING
 *
 * A key part array is considered greater than the other if all
 * its key parts are greater, or, all common key parts are equal
 * but there are additional parts in the bigger array.
 */
int
key_part_cmp(const struct key_part *parts1, uint32_t part_count1,
	     const struct key_part *parts2, uint32_t part_count2);

/**
 * Check if a key of @a tuple contains NULL.
 * @param tuple Tuple to check.
 * @param def Key def to check by.
 * @param multikey_idx Multikey index hint.
 * @retval Does the key contain NULL or not?
 */
bool
tuple_key_contains_null(struct tuple *tuple, struct key_def *def,
			int multikey_idx);

/**
 * Check that tuple fields match with given key definition
 * key_def.
 * @param key_def Key definition.
 * @param tuple Tuple to validate.
 *
 * @retval 0  The tuple is valid.
 * @retval -1 The tuple is invalid.
 */
int
tuple_validate_key_parts(struct key_def *key_def, struct tuple *tuple);

/**
 * Extract key from tuple by given key definition and return
 * buffer allocated on box_txn_alloc with this key. This function
 * has O(n) complexity, where n is the number of key parts.
 * @param tuple - tuple from which need to extract key
 * @param key_def - definition of key that need to extract
 * @param multikey_idx - multikey index hint
 * @param key_size - here will be size of extracted key
 *
 * @retval not NULL Success
 * @retval NULL     Memory allocation error
 */
static inline char *
tuple_extract_key(struct tuple *tuple, struct key_def *key_def,
		  int multikey_idx, uint32_t *key_size)
{
	return key_def->tuple_extract_key(tuple, key_def, multikey_idx,
					  key_size);
}

/**
 * Extract key from raw msgpuck by given key definition and return
 * buffer allocated on box_txn_alloc with this key.
 * This function has O(n*m) complexity, where n is the number of key parts
 * and m is the tuple size.
 * @param data - msgpuck data from which need to extract key
 * @param data_end - pointer at the end of data
 * @param key_def - definition of key that need to extract
 * @param multikey_idx - multikey index hint
 * @param key_size - here will be size of extracted key
 *
 * @retval not NULL Success
 * @retval NULL     Memory allocation error
 */
static inline char *
tuple_extract_key_raw(const char *data, const char *data_end,
		      struct key_def *key_def, int multikey_idx,
		      uint32_t *key_size)
{
	return key_def->tuple_extract_key_raw(data, data_end, key_def,
					      multikey_idx, key_size);
}

/**
 * Compare keys using the key definition and comparison hints.
 * @param key_a key parts with MessagePack array header
 * @param key_a_hint comparison hint of @a key_a
 * @param key_b key_parts with MessagePack array header
 * @param key_b_hint comparison hint of @a key_b
 * @param key_def key definition
 *
 * @retval 0  if key_a == key_b
 * @retval <0 if key_a < key_b
 * @retval >0 if key_a > key_b
 */
int
key_compare(const char *key_a, hint_t key_a_hint,
	    const char *key_b, hint_t key_b_hint,
	    struct key_def *key_def);

/**
 * Compare tuples using the key definition and comparison hints.
 * @param tuple_a first tuple
 * @param tuple_a_hint comparison hint of @a tuple_a
 * @param tuple_b second tuple
 * @param tuple_b_hint comparison hint of @a tuple_b
 * @param key_def key definition
 * @retval 0  if key_fields(tuple_a) == key_fields(tuple_b)
 * @retval <0 if key_fields(tuple_a) < key_fields(tuple_b)
 * @retval >0 if key_fields(tuple_a) > key_fields(tuple_b)
 */
static inline int
tuple_compare(struct tuple *tuple_a, hint_t tuple_a_hint,
	      struct tuple *tuple_b, hint_t tuple_b_hint,
	      struct key_def *key_def)
{
	assert(key_def->tuple_compare != NULL);
	return key_def->tuple_compare(tuple_a, tuple_a_hint,
				      tuple_b, tuple_b_hint, key_def);
}

/**
 * Compare tuple with key using the key definition and
 * comparison hints
 * @param tuple tuple
 * @param tuple_hint comparison hint of @a tuple
 * @param key key parts without MessagePack array header
 * @param part_count the number of parts in @a key
 * @param key_hint comparison hint of @a key
 * @param key_def key definition
 * @retval 0  if key_fields(tuple) == parts(key)
 * @retval <0 if key_fields(tuple) < parts(key)
 * @retval >0 if key_fields(tuple) > parts(key)
 */
static inline int
tuple_compare_with_key(struct tuple *tuple, hint_t tuple_hint,
		       const char *key, uint32_t part_count,
		       hint_t key_hint, struct key_def *key_def)
{
	assert(key_def->tuple_compare_with_key != NULL);
	return key_def->tuple_compare_with_key(tuple, tuple_hint, key,
					       part_count, key_hint, key_def);
}

/**
 * Compute hash of a tuple field.
 * @param ph1 - pointer to running hash
 * @param pcarry - pointer to carry
 * @param field - pointer to field data
 * @param coll - collation to use for hashing strings or NULL
 * @return size of processed data
 *
 * This function updates @ph1 and @pcarry and advances @field
 * by the number of processed bytes.
 */
uint32_t
tuple_hash_field(uint32_t *ph1, uint32_t *pcarry, const char **field,
		 struct coll *coll);

/**
 * Compute hash of a key part.
 * @param ph1 - pointer to running hash
 * @param pcarry - pointer to carry
 * @param tuple - tuple to hash
 * @param part - key part
 * @param multikey_idx - multikey index hint
 * @return size of processed data
 *
 * This function updates @ph1 and @pcarry.
 */
uint32_t
tuple_hash_key_part(uint32_t *ph1, uint32_t *pcarry, struct tuple *tuple,
		    struct key_part *part, int multikey_idx);

/**
 * Calculates a common hash value for a tuple
 * @param tuple - a tuple
 * @param key_def - key_def for field description
 * @return - hash value
 */
static inline uint32_t
tuple_hash(struct tuple *tuple, struct key_def *key_def)
{
	return key_def->tuple_hash(tuple, key_def);
}

/**
 * Calculate a common hash value for a key
 * @param key - full key (msgpack fields w/o array marker)
 * @param key_def - key_def for field description
 * @return - hash value
 */
static inline uint32_t
key_hash(const char *key, struct key_def *key_def)
{
	return key_def->key_hash(key, key_def);
}

 /*
 * Get comparison hint for a tuple.
 * @param tuple - tuple to compute the hint for
 * @param key_def - key_def used for tuple comparison
 * @return - hint value
 */
static inline hint_t
tuple_hint(struct tuple *tuple, struct key_def *key_def)
{
	return key_def->tuple_hint(tuple, key_def);
}

/**
 * Get a comparison hint of a key.
 * @param key - key to compute the hint for
 * @param key_def - key_def used for tuple comparison
 * @return - hint value
 */
static inline hint_t
key_hint(const char *key, uint32_t part_count, struct key_def *key_def)
{
	return key_def->key_hint(key, part_count, key_def);
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_KEY_DEF_H_INCLUDED */
