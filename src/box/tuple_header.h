/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2025, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/util.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * An atom of Tarantool storage. Represents MsgPack Array.
 * Tuple has the following structure:
 *                           uint32       uint32     bsize
 *                          +-------------------+-------------+
 * tuple_begin, ..., raw =  | offN | ... | off1 | MessagePack |
 * |                        +-------------------+-------------+
 * |                                            ^
 * +---------------------------------------data_offset
 *
 * Each 'off_i' is the offset to the i-th indexed field.
 */
struct PACKED tuple
{
	/**
	 * Local reference counter. After hitting the limit a part of this
	 * counter is uploaded to external storage that is acquired back
	 * when the counter falls back to zero.
	 * Is always nonzero in normal reference counted tuples.
	 * Must not be accessed directly, use @sa tuple_create,
	 * tuple_ref_init, tuple_ref, tuple_unref instead.
	 */
	uint8_t local_refs;
	/** Tuple flags (e.g. see TUPLE_HAS_UPLOADED_REFS). */
	uint8_t flags;
	/** Format identifier. */
	uint16_t format_id;
	/**
	 * A pair of fields for storing data offset and data bsize:
	 * Data offset - to the MessagePack from the begin of the tuple.
	 * Data bsize - length of the MessagePack data in raw part of the tuple.
	 * These two values can be stored in bulky and compact modes.
	 * In bulky mode offset is simple stored in data_offset_bsize_raw
	 * member while bsize is stored in bsize_bulky.
	 * In compact mode bsize_bulky is not used and both values are combined
	 * in data_offset_bsize_raw: offset in high byte, bsize in low byte,
	 * and the highest bit is set to 1 as a flag of compact mode.
	 * They're for internal use only, must be set in tuple_create and
	 * accessed by tuple_data_offset, tuple_bsize etc.
	 */
	uint16_t data_offset_bsize_raw;
	/** See above. */
	uint32_t bsize_bulky;
	/**
	 * Engine specific fields and offsets array concatenated
	 * with MessagePack fields array.
	 * char raw[0];
	 * Note that in compact mode bsize_bulky is no used and dynamic data
	 * can be written starting from bsize_bulky member.
	 */
};

static_assert(sizeof(struct tuple) == 10, "Just to be sure");

/**
 * Test whether the tuple can be stored compactly.
 * @param data_offset - see member description in struct tuple.
 * @param bsize - see member description in struct tuple.
 * @return
 */
static inline bool
tuple_can_be_compact(uint16_t data_offset, uint32_t bsize)
{
	/*
	 * Compact mode requires data_offset to be 7 bits max and bsize
	 * to be 8 bits max; see tuple::data_offset_bsize_raw for explanation.
	 */
	return data_offset <= INT8_MAX && bsize <= UINT8_MAX;
}

/** Offset to the MessagePack from the beginning of the tuple. */
static inline uint16_t
tuple_data_offset(struct tuple *tuple)
{
	uint16_t res = tuple->data_offset_bsize_raw;
	/*
	 * We have two variants depending on high bit of res (res & 0x8000):
	 * 1) nonzero, compact mode, the result is in high byte, excluding
	 *  high bit: (res & 0x7fff) >> 8.
	 * 2) zero, bulky mode, the result is just res.
	 * We could make `if` statement here, but it would cost too much.
	 * We can make branchless code instead. We can rewrite the result:
	 * 1) In compact mode: (res & 0x7fff) >> 8
	 * 2) In bulky mode:   (res & 0x7fff) >> 0
	 * Or, simply:
	 * In any case mode: (res & 0x7fff) >> (8 * (is_compact ? 1 : 0)).
	 * On the other hand the compact 0/1 bit can be simply acquired
	 * by shifting res >> 15.
	 */
	uint16_t is_compact_bit = res >> 15;
	res = (res & 0x7fff) >> (is_compact_bit * 8);
#ifndef NDEBUG
	uint16_t simple = (tuple->data_offset_bsize_raw & 0x8000) ?
			  (tuple->data_offset_bsize_raw >> 8) & 0x7f :
			  tuple->data_offset_bsize_raw;
	assert(res == simple);
#endif
	return res;
}

/** Size of MessagePack data of the tuple. */
static inline uint32_t
tuple_bsize(struct tuple *tuple)
{
	if (tuple->data_offset_bsize_raw & 0x8000)
		return tuple->data_offset_bsize_raw & 0xff;
	else
		return tuple->bsize_bulky;
}

/**
 * Initializes `data_offset_bsize_raw' and `bsize_bulky' of a tuple.
 *
 * @param tuple - Tuple to initialize.
 * @param data_offset - see member description in struct tuple.
 * @param bsize - see member description in struct tuple.
 * @param make_compact - construct compact tuple, see description in tuple.
 */
static inline void
tuple_set_data_offset_bsize(struct tuple *tuple, uint16_t data_offset,
			    uint32_t bsize, bool make_compact)
{
	assert(data_offset <= INT16_MAX);
	if (make_compact) {
		assert(tuple_can_be_compact(data_offset, bsize));
		uint16_t combined = 0x8000;
		combined |= data_offset << 8;
		combined |= bsize;
		tuple->data_offset_bsize_raw = combined;
	} else {
		tuple->data_offset_bsize_raw = data_offset;
		tuple->bsize_bulky = bsize;
	}
}

#ifdef __cplusplus
} /* extern "C" */
#endif
