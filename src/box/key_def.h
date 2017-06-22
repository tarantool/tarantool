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
#include "small/rlist.h"
#include "error.h"
#include "diag.h"
#include <msgpuck.h>
#define RB_COMPACT 1
#include "small/rb.h"
#include <limits.h>
#include <wchar.h>
#include <wctype.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

enum {
	BOX_ENGINE_MAX = 3, /* + 1 to the actual number of engines */
	BOX_SPACE_MAX = INT32_MAX,
	BOX_FUNCTION_MAX = 32000,
	BOX_INDEX_MAX = 128,
	BOX_NAME_MAX = 64,
	BOX_FIELD_MAX = INT32_MAX,
	BOX_USER_MAX = 32,
	/**
	 * A fairly arbitrary limit which is still necessary
	 * to keep tuple_format object small.
	 */
	BOX_INDEX_FIELD_MAX = INT16_MAX,
	/** Yet another arbitrary limit which simply needs to
	 * exist.
	 */
	BOX_INDEX_PART_MAX = UINT8_MAX
};

/*
 * Different objects which can be subject to access
 * control.
 *
 * Use 0 for unknown to use the same index consistently
 * even when there are more object types in the future.
 */
enum schema_object_type {
	SC_UNKNOWN = 0, SC_UNIVERSE = 1, SC_SPACE = 2, SC_FUNCTION = 3,
	SC_USER = 4, SC_ROLE = 5
};

enum schema_object_type
schema_object_type(const char *name);

const char *
schema_object_name(enum schema_object_type type);

/** \cond public */

/*
 * Possible field data types. Can't use STRS/ENUM macros for them,
 * since there is a mismatch between enum name (STRING) and type
 * name literal ("STR"). STR is already used as Objective C type.
 */
enum field_type {
	FIELD_TYPE_ANY = 0,
	FIELD_TYPE_UNSIGNED,
	FIELD_TYPE_STRING,
	FIELD_TYPE_ARRAY,
	FIELD_TYPE_NUMBER,
	FIELD_TYPE_INTEGER,
	FIELD_TYPE_SCALAR,
	field_type_MAX
};

/** \endcond public */

extern const char *field_type_strs[];

/* MsgPack type names */
extern const char *mp_type_strs[];

/**
 * The supported language of the stored function.
 */
enum func_language {
	FUNC_LANGUAGE_LUA,
	FUNC_LANGUAGE_C,
	func_language_MAX,
};
extern const char *func_language_strs[];

static inline uint32_t
field_type_maxlen(enum field_type type)
{
	static const uint32_t maxlen[] =
		{ UINT32_MAX, 8, UINT32_MAX, UINT32_MAX, UINT32_MAX };
	return maxlen[type];
}

enum field_type
field_type_by_name(const char *name);

enum index_type {
	HASH = 0, /* HASH Index */
	TREE,     /* TREE Index */
	BITSET,   /* BITSET Index */
	RTREE,    /* R-Tree Index */
	index_type_MAX,
};

extern const char *index_type_strs[];

enum opt_type {
	OPT_BOOL,	/* bool */
	OPT_INT,	/* int64_t */
	OPT_FLOAT,	/* double */
	OPT_STR,	/* char[] */
	opt_type_MAX,
};

extern const char *opt_type_strs[];

struct opt_def {
	const char *name;
	enum opt_type type;
	ptrdiff_t offset;
	uint32_t len;
};

#define OPT_DEF(key, type, opts, field) \
	{ key, type, offsetof(opts, field), sizeof(((opts *)0)->field) }

enum rtree_index_distance_type {
	 /* Euclid distance, sqrt(dx*dx + dy*dy) */
	RTREE_INDEX_DISTANCE_TYPE_EUCLID,
	/* Manhattan distance, fabs(dx) + fabs(dy) */
	RTREE_INDEX_DISTANCE_TYPE_MANHATTAN,
	rtree_index_distance_type_MAX
};
extern const char *rtree_index_distance_type_strs[];

/** Descriptor of a single part in a multipart key. */
struct key_part {
	uint32_t fieldno;
	enum field_type type;
};

/** Index options */
struct index_opts {
	/**
	 * Is this index unique or not - relevant to HASH/TREE
	 * index
	 */
	bool is_unique;
	/**
	 * RTREE index dimension.
	 */
	int64_t dimension;
	/**
	 * RTREE distance type.
	 */
	char distancebuf[16];
	enum rtree_index_distance_type distance;
	/**
	 * Vinyl index options.
	 */
	int64_t range_size;
	int64_t page_size;
	/**
	 * Maximal number of runs that can be created in a level
	 * of the LSM tree before triggering compaction.
	 */
	int64_t run_count_per_level;
	/**
	 * The LSM tree multiplier. Each subsequent level of
	 * the LSM tree is run_size_ratio times larger than
	 * previous one.
	 */
	double run_size_ratio;
	/* Bloom filter false positive rate. */
	double bloom_fpr;
	/**
	 * LSN from the time of index creation.
	 */
	int64_t lsn;
};

extern const struct index_opts index_opts_default;
extern const struct opt_def index_opts_reg[];

static inline int
index_opts_cmp(const struct index_opts *o1, const struct index_opts *o2)
{
	if (o1->is_unique != o2->is_unique)
		return o1->is_unique < o2->is_unique ? -1 : 1;
	if (o1->dimension != o2->dimension)
		return o1->dimension < o2->dimension ? -1 : 1;
	if (o1->distance != o2->distance)
		return o1->distance < o2->distance ? -1 : 1;
	if (o1->range_size != o2->range_size)
		return o1->range_size < o2->range_size ? -1 : 1;
	if (o1->page_size != o2->page_size)
		return o1->page_size < o2->page_size ? -1 : 1;
	if (o1->run_count_per_level != o2->run_count_per_level)
		return o1->run_count_per_level < o2->run_count_per_level ?
		       -1 : 1;
	if (o1->run_size_ratio != o2->run_size_ratio)
		return o1->run_size_ratio < o2->run_size_ratio ? -1 : 1;
	if (o1->bloom_fpr != o2->bloom_fpr)
		return o1->bloom_fpr < o2->bloom_fpr ? -1 : 1;
	return 0;
}

struct key_def;
struct tuple;

/** @copydoc tuple_compare_with_key() */
typedef int (*tuple_compare_with_key_t)(const struct tuple *tuple_a,
					const char *key,
					uint32_t part_count,
					const struct key_def *key_def);
/** @copydoc tuple_compare() */
typedef int (*tuple_compare_t)(const struct tuple *tuple_a,
			       const struct tuple *tuple_b,
			       const struct key_def *key_def);
/** @copydoc tuple_extract_key() */
typedef char *(*tuple_extract_key_t)(const struct tuple *tuple,
				     const struct key_def *key_def,
				     uint32_t *key_size);
/** @copydoc tuple_extract_key_raw() */
typedef char *(*tuple_extract_key_raw_t)(const char *data,
					 const char *data_end,
					 const struct key_def *key_def,
					 uint32_t *key_size);
/** @copydoc tuple_hash() */
typedef uint32_t (*tuple_hash_t)(const struct tuple *tuple,
				 const struct key_def *key_def);
/** @copydoc key_hash() */
typedef uint32_t (*key_hash_t)(const char *key,
				const struct key_def *key_def);

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
	/** Key fields mask. @sa column_mask.h for details. */
	uint64_t column_mask;
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

/** \cond public */

typedef struct key_def box_key_def_t;

/**
 * Create key definition with key fields with passed typed on passed positions.
 * May be used for tuple format creation and/or tuple comparation.
 *
 * \param key_def key definition to create
 * \param fields array with key field identifiers
 * \param types array with key field types (see enum field_type)
 * \param part_count the number of key fields
 */
box_key_def_t *
box_key_def_new(uint32_t *fields, uint32_t *types, uint32_t part_count);

/**
 * Delete key definition
 *
 * \param key_def key definition to delete
 */
void
box_key_def_delete(box_key_def_t *key_def);

/** \endcond public */

/* Definition of an index. */
struct index_def {
	/* A link in key list. */
	struct rlist link;
	/** Ordinal index number in the index array. */
	uint32_t iid;
	/* Space id. */
	uint32_t space_id;
	/** Index name. */
	char *name;
	/** Index type. */
	enum index_type type;
	struct index_opts opts;
	struct key_def key_def;
};

struct index_def *
index_def_dup(const struct index_def *def);

/* Destroy and free an index_def. */
void
index_def_delete(struct index_def *def);

/**
 * True, if the index change by alter requires an index rebuild.
 *
 * Some changes, such as a new page size or bloom_fpr do not
 * take effect immediately, so do not require a rebuild.
 *
 * Others, such as index name change, do not change the data, only
 * metadata, so do not require a rebuild either.
 *
 * Finally, changing index type or number of parts always requires
 * a rebuild.
 */
bool
index_def_change_requires_rebuild(struct index_def *old_index_def,
				  struct index_def *new_index_def);

/**
 * Encapsulates privileges of a user on an object.
 * I.e. "space" object has an instance of this
 * structure for each user.
 */
struct access {
	/**
	 * Granted access has been given to a user explicitly
	 * via some form of a grant.
	 */
	uint8_t granted;
	/**
	 * Effective access is a sum of granted access and
	 * all privileges inherited by a user on this object
	 * via some role. Since roles may be granted to other
	 * roles, this may include indirect grants.
	 */
	uint8_t effective;
};

/**
 * Effective session user. A cache of user data
 * and access stored in session and fiber local storage.
 * Differs from the authenticated user when executing
 * setuid functions.
 */
struct credentials {
	/** A look up key to quickly find session user. */
	uint8_t auth_token;
	/**
	 * Cached global grants, to avoid an extra look up
	 * when checking global grants.
	 */
	uint8_t universal_access;
	/** User id of the authenticated user. */
	uint32_t uid;
};

/**
 * Definition of a function. Function body is not stored
 * or replicated (yet).
 */
struct func_def {
	/** Function id. */
	uint32_t fid;
	/** Owner of the function. */
	uint32_t uid;
	/**
	 * True if the function requires change of user id before
	 * invocation.
	 */
	bool setuid;
	/**
	 * The language of the stored function.
	 */
	enum func_language language;
	/** Function name. */
	char name[0];
};

/**
 * @param name_length length of func_def->name
 * @returns size in bytes needed to allocate for struct func_def
 * for a function of length @a a name_length.
 */
static inline size_t
func_def_sizeof(uint32_t name_length)
{
	/* +1 for '\0' name terminating. */
	return sizeof(struct func_def) + name_length + 1;
}

/**
 * Definition of a privilege
 */
struct priv_def {
	/** Who grants the privilege. */
	uint32_t grantor_id;
	/** Whom the privilege is granted. */
	uint32_t grantee_id;
	/* Object id - is only defined for object type */
	uint32_t object_id;
	/* Object type - function, space, universe */
	enum schema_object_type object_type;
	/**
	 * What is being granted, has been granted, or is being
	 * revoked.
	 */
	uint8_t access;
	/** To maintain a set of effective privileges. */
	rb_node(struct priv_def) link;
};

/** Space options */
struct space_opts {
        /**
	 * The space is a temporary:
	 * - it is empty at server start
	 * - changes are not written to WAL
	 * - changes are not part of a snapshot
	 */
	bool temporary;
};

extern const struct space_opts space_opts_default;
extern const struct opt_def space_opts_reg[];

/** Space metadata. */
struct space_def {
	/** Space id. */
	uint32_t id;
	/** User id of the creator of the space */
	uint32_t uid;
	/**
	 * If not set (is 0), any tuple in the
	 * space can have any number of fields.
	 * If set, each tuple
	 * must have exactly this many fields.
	 */
	uint32_t exact_field_count;
	char name[BOX_NAME_MAX + 1];
	char engine_name[BOX_NAME_MAX + 1];
	struct space_opts opts;
};

/**
 * API of C stored function.
 */
typedef struct box_function_ctx box_function_ctx_t;
typedef int (*box_function_f)(box_function_ctx_t *ctx,
	     const char *args, const char *args_end);

static inline size_t
key_def_sizeof(uint32_t part_count)
{
	return sizeof(struct key_def) + sizeof(struct key_part) * part_count;
}

static inline size_t
index_def_sizeof(uint32_t part_count, uint32_t name_length)
{
	return sizeof(struct index_def) +
	       sizeof(struct key_part) * (part_count + 1) + name_length + 1;
}

/**
 * Allocate a new key definition.
 * @retval not NULL Success.
 * @retval NULL     Memory error.
 */
struct index_def *
index_def_new(uint32_t space_id, const char *space_name, uint32_t iid,
	      const char *name, uint32_t name_length, enum index_type type,
	      const struct index_opts *opts, uint32_t part_count);

/**
 * Set a single key part in a key def.
 * @pre part_no < part_count
 */
void
key_def_set_part(struct key_def *def, uint32_t part_no,
		 uint32_t fieldno, enum field_type type);

/**
 * An snprint-style function to print a key definition.
 */
int
key_def_snprint(char *buf, int size, const struct key_def *key_def);

/**
 * Return size of key parts array when encoded in MsgPack.
 * See also key_def_encode_parts().
 */
size_t
key_def_sizeof_parts(const struct key_def *key_def);

/**
 * Encode key parts array in MsgPack and return a pointer following
 * the end of encoded data.
 */
char *
key_def_encode_parts(char *data, const struct key_def *key_def);

/**
 * 1.6.6+
 * Decode parts array from tuple field and write'em to index_def structure.
 * Throws a nice error about invalid types, but does not check ranges of
 *  resulting values field_no and field_type
 * Parts expected to be a sequence of <part_count> arrays like this:
 *  [NUM, STR, ..][NUM, STR, ..]..,
 */
int
key_def_decode_parts(struct key_def *key_def, const char **data);

/**
 * 1.6.5-
 * TODO: Remove it in newer version, find all 1.6.5-
 * Decode parts array from tuple fieldw and write'em to index_def structure.
 * Does not check anything since tuple must be validated before
 * Parts expected to be a sequence of <part_count> 2 * arrays values this:
 *  NUM, STR, NUM, STR, ..,
 */
int
key_def_decode_parts_165(struct key_def *key_def, const char **data);

/**
 * Returns the part in index_def->parts for the specified fieldno.
 * If fieldno is not in index_def->parts returns NULL.
 */
const struct key_part *
key_def_find(const struct key_def *key_def, uint32_t fieldno);

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

/*
 * Check that parts of the key match with the key definition.
 * @param key_def Key definition.
 * @param key MessagePack'ed data for matching.
 * @param part_count Field count in the key.
 *
 * @retval 0  The key is valid.
 * @retval -1 The key is invalid.
 */
int
key_validate_parts(struct key_def *key_def, const char *key,
		   uint32_t part_count);

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
	for (uint32_t part_id = 0; part_id < key_def->part_count; part_id++) {
		if (key_def->parts[part_id].fieldno != part_id)
			return false;
	}
	return true;
}

/** A helper table for key_mp_type_validate */
extern const uint32_t key_mp_type[];

/**
 * @brief Checks if \a field_type (MsgPack) is compatible \a type (KeyDef).
 * @param type KeyDef type
 * @param field_type MsgPack type
 * @param field_no - a field number (is used to store an error message)
 *
 * @retval 0  mp_type is valid.
 * @retval -1 mp_type is invalid.
 */
static inline int
key_mp_type_validate(enum field_type key_type, enum mp_type mp_type,
	       int err, uint32_t field_no)
{
	assert(key_type < field_type_MAX);
	assert((size_t) mp_type < CHAR_BIT * sizeof(*key_mp_type));
	if (unlikely((key_mp_type[key_type] & (1U << mp_type)) == 0)) {
		diag_set(ClientError, err, field_no, field_type_strs[key_type]);
		return -1;
	}
	return 0;
}

#if defined(__cplusplus)
} /* extern "C" */

/** Compare two key part arrays.
 *
 * This function is used to find out whether alteration
 * of an index has changed it substantially enough to warrant
 * a rebuild or not. For example, change of index id is
 * not a substantial change, whereas change of index type
 * or key parts requires a rebuild.
 *
 * One key part is considered to be greater than the other if:
 * - its fieldno is greater
 * - given the same fieldno, NUM < STRING
 *   (coarsely speaking, based on field_type_maxlen()).
 *
 * A key part array is considered greater than the other if all
 * its key parts are greater, or, all common key parts are equal
 * but there are additional parts in the bigger array.
 */
int
key_part_cmp(const struct key_part *parts1, uint32_t part_count1,
	     const struct key_part *parts2, uint32_t part_count2);

/**
 * One key definition is greater than the other if it's id is
 * greater, it's name is greater,  it's index type is greater
 * (HASH < TREE < BITSET) or its key part array is greater.
 */
int
index_def_cmp(const struct index_def *key1, const struct index_def *key2);

/**
 * Check a key definition for violation of various limits.
 *
 * @param index_def   index definition
 * @param old_space   space definition
 */
void
index_def_check(struct index_def *index_def, const char *space_name);

/** Check space definition structure for errors. */
void
space_def_check(struct space_def *def, uint32_t namelen,
                uint32_t engine_namelen,
                int32_t errcode);

/**
 * Check object identifier for invalid symbols.
 * The function checks \a str for matching [a-zA-Z_][a-zA-Z0-9_]* expression.
 * Result is locale-dependent.
 */
bool
identifier_is_valid(const char *str);

/**
 * Throw an error if identifier is not valid.
 */
void
identifier_check(const char *str);

static inline struct index_def *
index_def_dup_xc(const struct index_def *def)
{
	struct index_def *ret = index_def_dup(def);
	if (ret == NULL)
		diag_raise();
	return ret;
}

#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_KEY_DEF_H_INCLUDED */
