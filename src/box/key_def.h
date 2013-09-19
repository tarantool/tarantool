#ifndef TARANTOOL_BOX_KEY_DEF_H_INCLUDED
#define TARANTOOL_BOX_KEY_DEF_H_INCLUDED
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
#include "tarantool/util.h"
#include "rlist.h"

enum {
	BOX_SPACE_MAX = INT32_MAX,
	BOX_INDEX_MAX = 10,
	BOX_NAME_MAX = 32,
	BOX_FIELD_MAX = INT32_MAX,
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
 * Possible field data types. Can't use STRS/ENUM macros for them,
 * since there is a mismatch between enum name (STRING) and type
 * name literal ("STR"). STR is already used as Objective C type.
 */
enum field_type { UNKNOWN = 0, NUM, NUM64, STRING, field_type_MAX };
extern const char *field_type_strs[];

static inline uint32_t
field_type_maxlen(enum field_type type)
{
	static const uint32_t maxlen[] =
		{ UINT32_MAX, 4, 8, UINT32_MAX, UINT32_MAX };
	return maxlen[type];
}

#define ENUM_INDEX_TYPE(_)                                          \
	_(HASH,    0)       /* HASH Index  */                       \
	_(TREE,    1)       /* TREE Index  */                       \
	_(BITSET,  2)       /* BITSET Index  */                     \

ENUM(index_type, ENUM_INDEX_TYPE);
extern const char *index_type_strs[];

/** Descriptor of a single part in a multipart key. */
struct key_part {
	uint32_t fieldno;
	enum field_type type;
};

/* Descriptor of a multipart key. */
struct key_def {
	/* A link in key list. */
	struct rlist link;
	/** Ordinal index number in the index array. */
	uint32_t iid;
	/* Space id. */
	uint32_t space_id;
	/** Index name. */
	char name[BOX_NAME_MAX + 1];
	/** The size of the 'parts' array. */
	uint32_t part_count;
	/** Index type. */
	enum index_type type;
	/** Is this key unique. */
	bool is_unique;
	/** Description of parts of a multipart index. */
	struct key_part parts[];
};

/** Initialize a pre-allocated key_def. */
struct key_def *
key_def_new(uint32_t space_id, uint32_t iid, const char *name,
	    enum index_type type, bool is_unique, uint32_t part_count);

static inline struct key_def *
key_def_dup(struct key_def *def)
{
	struct key_def *dup = key_def_new(def->space_id, def->iid, def->name,
					  def->type, def->is_unique,
					  def->part_count);
	if (dup) {
		memcpy(dup->parts, def->parts,
		       def->part_count * sizeof(*def->parts));
	}
	return dup;
}

/**
 * Set a single key part in a key def.
 * @pre part_no < part_count
 */
static inline void
key_def_set_part(struct key_def *def, uint32_t part_no,
		 uint32_t fieldno, enum field_type type)
{
	assert(part_no < def->part_count);
	def->parts[part_no].fieldno = fieldno;
	def->parts[part_no].type = type;
}

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
 * - given the same fieldno, NUM < NUM64 < STRING
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
key_def_cmp(const struct key_def *key1, const struct key_def *key2);

/* Destroy and free a key_def. */
void
key_def_delete(struct key_def *def);

/** Add a key to the list of keys. */
static inline  void
key_list_add_key(struct rlist *key_list, struct key_def *key)
{
	rlist_add_entry(key_list, key, link);
}

/** Remove a key from the list of keys. */
void
key_list_del_key(struct rlist *key_list, uint32_t id);

/**
 * Check a key definition for violation of various limits.
 *
 * @param id        space id
 * @param key_def   key_def
 * @param type_str  type name (to produce a nice error)
 */
void
key_def_check(struct key_def *key_def);

/** Space metadata. */
struct space_def {
	/** Space id. */
	uint32_t id;
	/**
	 * If not set (is 0), any tuple in the
	 * space can have any number of fields.
	 * If set, each tuple
	 * must have exactly this many fields.
	 */
	uint32_t arity;
	char name[BOX_NAME_MAX + 1];
        /**
	 * The space is a temporary:
	 * - it is empty at server start
	 * - changes are not written to WAL
	 * - changes are not part of a snapshot
	 */
	bool temporary;
};

/** Check space definition structure for errors. */
void
space_def_check(struct space_def *def, uint32_t namelen, uint32_t errcode);

#endif /* TARANTOOL_BOX_KEY_DEF_H_INCLUDED */
