#ifndef TARANTOOL_BOX_COLUMN_MASK_H_INCLUDED
#define TARANTOOL_BOX_COLUMN_MASK_H_INCLUDED
/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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

#include <stdint.h>
#include <stdbool.h>

/**
 * Column mask is a bitmask of update operations for one tuple.
 * Column mask bit 'n' is set if in the corresponding tuple
 * field 'n' could be changed by an update operation.
 * This mask is used for update and upsert optimizations, when,
 * for example, it is necessary to check whether the operation
 * has changed an indexed field.
 *
 * The last bit of the mask stands for fields in range [63, +inf).
 * If an update operation updates field #63 and greater, then this
 * last bit of the mask is set. If an
 * update operations changes many fields ('#' or '!'), then all
 * fields after and including the target field could be changed -
 * in such case we set not one bit, but a range of bits.
 */

#define COLUMN_MASK_FULL UINT64_MAX

/**
 * Set a bit in the bitmask corresponding to a
 * single changed column.
 * @param column_mask Mask to update.
 * @param fieldno     Updated fieldno (index base must be 0).
 */
static inline void
column_mask_set_fieldno(uint64_t *column_mask, uint32_t fieldno)
{
	if (fieldno >= 63)
		/*
		 * @sa column_mask key_def declaration for
		 * details.
		 */
		*column_mask |= ((uint64_t) 1) << 63;
	else
		*column_mask |= ((uint64_t) 1) << fieldno;
}

/**
 * Set bits in a bitmask for a range of changed columns.
 * @param column_mask Mask to update.
 * @param first_fieldno_in_range First fieldno of the updated
 *        range.
 */
static inline void
column_mask_set_range(uint64_t *column_mask, uint32_t first_fieldno_in_range)
{
	if (first_fieldno_in_range < 63) {
		/*
		 * Set all bits by default via COLUMN_MASK_FULL
		 * and then unset bits preceding the operation
		 * field number. Fields corresponding to these
		 * bits will definitely not be changed.
		 */
		*column_mask |= COLUMN_MASK_FULL << first_fieldno_in_range;
	} else {
		/* A range outside "short" range. */
		*column_mask |= ((uint64_t) 1) << 63;
	}
}

/**
 * True if the update operation does not change the key.
 * @param key_mask Key mask.
 * @param update_mask Column mask of the update operation.
 *
 * @retval true, if the key is not updated.
 * @retval false, if a key field is possibly updated or column
 *         mask optimization is not applicable.
 */
static inline bool
key_update_can_be_skipped(uint64_t key_mask, uint64_t update_mask)
{
	return (key_mask & update_mask) == 0;
}

/**
 * Test a bit in the bitmask corresponding to a column fieldno.
 * @param column_mask Mask to test.
 * @param fieldno Fieldno number to test (index base must be 0).
 * @retval true If bit corresponding to a column fieldno.
 * @retval false if bit is not set or fieldno > 64.
 */
static inline bool
column_mask_fieldno_is_set(uint64_t column_mask, uint32_t fieldno)
{
	uint64_t mask = (uint64_t) 1 << (fieldno < 63 ? fieldno : 63);
	return (column_mask & mask) != 0;
}

/**
 * Set a bit with given index @a bitno in a given @a bitmask.
 * Do nothing when @a bitno is greater than size of bitmask.
 * @param bitmask Bitmask to test.
 * @param bitno Bit number to test (must be 0-based).
 */
static inline void
bitmask64_set_bit(uint64_t *bitmask, uint32_t bitno)
{
	if (bitno < 64)
		*bitmask |= ((uint64_t)1 << bitno);
}

/**
 * Test a bit in the bitmask corresponding to @a bitno.
 * @param bitmask Bitmask to test.
 * @param bitno Bit number to test (must be 0-based).
 * @return Whether given bit is set in a given bitmask.
 *         When bitno is greater than size of bitmask returns
 *         false.
 */
static inline bool
bitmask64_is_bit_set(uint64_t bitmask, uint32_t bitno)
{
	return bitno < 64 && (bitmask & ((uint64_t)1 << bitno)) != 0;
}

#endif
