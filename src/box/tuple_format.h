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

#include "key_def.h" /* for enum field_type */

enum { FORMAT_ID_MAX = UINT16_MAX - 1, FORMAT_ID_NIL = UINT16_MAX };
enum { FORMAT_REF_MAX = INT32_MAX};

/*
 * We don't pass INDEX_OFFSET around dynamically all the time,
 * at least hard code it so that in most cases it's a nice error
 * message
 */
enum { INDEX_OFFSET = 1 };


/**
 * @brief Tuple field format
 * Support structure for struct tuple_format.
 * Contains information of one field.
 */
struct tuple_field_format {
	/**
	 * Field type of an indexed field.
	 * If a field participates in at least one of space indexes
	 * then its type is stored in this member.
	 * If a field does not participate in an index
	 * then UNKNOWN is stored for it.
	 */
	enum field_type type;
	/**
	 * Offset slot in field map in tuple.
	 * Normally tuple stores field map - offsets of all fields
	 * participating in indexes. This allows quick access to most
	 * used fields without parsing entire mspack.
	 * This member stores position in the field map of tuple
	 * for current field.
	 * If the field does not participate in indexes then it has
	 * no offset in field map and INT_MAX is stored in this member.
	 * Due to specific field map in tuple (it is stored before tuple),
	 * the positions in field map is negative.
	 * Thus if this member is negative, smth like
	 * tuple->data[((uint32_t *)tuple)[format->offset_slot[fieldno]]]
	 * gives the start of the field
	 */
	int32_t offset_slot;
};

/**
 * @brief Tuple format
 * Tuple format describes how tuple is stored and information about its fields
 */
struct tuple_format {
	uint16_t id;
	/* Format objects are reference counted. */
	int refs;
	/* Length of 'fields' array. */
	uint32_t field_count;
	/**
	 * Size of field map of tuple in bytes.
	 * See tuple_field_format::ofset for details//
	 */
	uint32_t field_map_size;

	/* Formats of the fields */
	struct tuple_field_format fields[];
};

/**
 * Default format for a tuple which does not belong
 * to any space and is stored in memory.
 */
extern struct tuple_format *tuple_format_default;

extern struct tuple_format **tuple_formats;

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

/**
 * @brief Allocate, construct and register a new in-memory tuple
 *	 format.
 * @param space description
 *
 * @return tuple format or raise an exception on error
 */
struct tuple_format *
tuple_format_new(struct rlist *key_list);

/** Delete a format with zero ref count. */
void
tuple_format_delete(struct tuple_format *format);

static inline void
tuple_format_ref(struct tuple_format *format, int count)
{
	assert(format->refs + count >= 0);
	assert((uint64_t)format->refs + count <= FORMAT_REF_MAX);

	format->refs += count;
	if (format->refs == 0)
		tuple_format_delete(format);

};

void
tuple_format_init();

/** Destroy tuple format subsystem and free resourses */
void
tuple_format_free();

#endif /* #ifndef TARANTOOL_BOX_TUPLE_FORMAT_H_INCLUDED */
