/*
 * *No header guard*: the header is allowed to be included twice
 * with different sets of defines.
 */
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

#include "small/matras.h"

/**
 * Additional user defined name that appended to prefix 'light'
 *  for all names of structs and functions in this header file.
 * All names use pattern: light<LIGHT_NAME>_<name of func/struct>
 * May be empty, but still have to be defined (just #define LIGHT_NAME)
 * Example:
 * #define LIGHT_NAME _test
 * ...
 * struct light_test_core hash_table;
 * light_test_create(&hash_table, ...);
 */
#ifndef LIGHT_NAME
#error "LIGHT_NAME must be defined"
#endif

/**
 * Data type that hash table holds. Must be less tant 8 bytes.
 */
#ifndef LIGHT_DATA_TYPE
#error "LIGHT_DATA_TYPE must be defined"
#endif

/**
 * Data type that used to for finding values.
 */
#ifndef LIGHT_KEY_TYPE
#error "LIGHT_KEY_TYPE must be defined"
#endif

/**
 * Type of optional third parameter of comparing function.
 * If not needed, simply use #define LIGHT_CMP_ARG_TYPE int
 */
#ifndef LIGHT_CMP_ARG_TYPE
#error "LIGHT_CMP_ARG_TYPE must be defined"
#endif

/**
 * Data comparing function. Takes 3 parameters - value1, value2 and
 * optional value that stored in hash table struct.
 * Third parameter may be simply ignored like that:
 * #define LIGHT_EQUAL(a, b, garb) a == b
 */
#ifndef LIGHT_EQUAL
#error "LIGHT_EQUAL must be defined"
#endif

/**
 * Data comparing function. Takes 3 parameters - value, key and
 * optional value that stored in hash table struct.
 * Third parameter may be simply ignored like that:
 * #define LIGHT_EQUAL_KEY(a, b, garb) a == b
 */
#ifndef LIGHT_EQUAL_KEY
#error "LIGHT_EQUAL_KEY must be defined"
#endif

/**
 * Tools for name substitution:
 */
#ifndef CONCAT4
#define CONCAT4_R(a, b, c, d) a##b##c##d
#define CONCAT4(a, b, c, d) CONCAT4_R(a, b, c, d)
#endif

#ifdef _
#error '_' must be undefinded!
#endif
#define LIGHT(name) CONCAT4(light, LIGHT_NAME, _, name)

/**
 * Struct for one record of the hash table
 */
struct LIGHT(record) {
	/* hash of a value */
	uint32_t hash;
	/* slot of the next record in chain */
	uint32_t next;
	/* the value */
	union {
		LIGHT_DATA_TYPE value;
		uint64_t uint64_padding;
	};
};

/**
 * Main struct for holding hash table
 */
struct LIGHT(core) {
	/* Number of records added while grow iteration */
	enum { GROW_INCREMENT = 8 };
	/* count of values in hash table */
	uint32_t count;
	/* size of hash table ( equal to mtable.size ) */
	uint32_t table_size;
	/*
	 * cover is power of two;
	 * if count is positive, then cover/2 < count <= cover
	 * cover_mask is cover - 1
	 */
	uint32_t cover_mask;

	/*
	 * Start of chain of empty slots
	 */
	uint32_t empty_slot;
	struct LIGHT(record) *empty_record;

	/* additional parameter for data comparison */
	LIGHT_CMP_ARG_TYPE arg;

	/* dynamic storage for records */
	struct matras mtable;
};

/**
 * Type of functions for memory allocation and deallocation
 */
typedef void *(*LIGHT(extent_alloc_t))();
typedef void (*LIGHT(extent_free_t))(void *);

/**
 * Special result of light_find that means that nothing was found
 */
static const uint32_t LIGHT(end) = 0xFFFFFFFF;

/**
 * @brief Hash table construction. Fills struct light members.
 * @param ht - pointer to a hash table struct
 * @param extent_size - size of allocating memory blocks
 * @param extent_alloc_func - memory blocks allocation function
 * @param extent_free_func - memory blocks allocation function
 * @param arg - optional parameter to save for comparing function
 */
void
LIGHT(create)(struct LIGHT(core) *ht, size_t extent_size,
	      LIGHT(extent_alloc_t) extent_alloc_func,
	      LIGHT(extent_free_t) extent_free_func,
	      LIGHT_CMP_ARG_TYPE arg);

/**
 * @brief Hash table destruction. Frees all allocated memory
 * @param ht - pointer to a hash table struct
 */
void
LIGHT(destroy)(struct LIGHT(core) *ht);

/**
 * @brief Find a record with given hash and value
 * @param ht - pointer to a hash table struct
 * @param hash - hash to find
 * @param data - value to find
 * @return integer ID of found record or light_end if nothing found
 */
uint32_t
LIGHT(find)(const struct LIGHT(core) *ht, uint32_t hash, LIGHT_DATA_TYPE data);

/**
 * @brief Find a record with given hash and key
 * @param ht - pointer to a hash table struct
 * @param hash - hash to find
 * @param data - key to find
 * @return integer ID of found record or light_end if nothing found
 */
uint32_t
LIGHT(find_key)(const struct LIGHT(core) *ht, uint32_t hash, LIGHT_KEY_TYPE data);

/**
 * @brief Insert a record with given hash and value
 * @param ht - pointer to a hash table struct
 * @param hash - hash to insert
 * @param data - value to insert
 * @return integer ID of inserted record or light_end if failed
 */
uint32_t
LIGHT(insert)(struct LIGHT(core) *ht, uint32_t hash, LIGHT_DATA_TYPE data);

/**
 * @brief Replace a record with given hash and value
 * @param ht - pointer to a hash table struct
 * @param hash - hash to find
 * @param data - value to find and replace
 * @param replaced - pointer to a value that was stored in table before replace
 * @return integer ID of found record or light_end if nothing found
 */
uint32_t
LIGHT(replace)(struct LIGHT(core) *ht, uint32_t hash,
	       LIGHT_DATA_TYPE data, LIGHT_DATA_TYPE *replaced);

/**
 * @brief Delete a record from a hash table by given record ID
 * @param ht - pointer to a hash table struct
 * @param slotpos - ID of an record. See LIGHT(find) for details.
 */
void
LIGHT(delete)(struct LIGHT(core) *ht, uint32_t slotpos);

/**
 * @brief Get a value from a desired position
 * @param ht - pointer to a hash table struct
 * @param slotpos - ID of an record
 *  ID must be vaild, check it by light_pos_valid (asserted).
 */
LIGHT_DATA_TYPE
LIGHT(get)(struct LIGHT(core) *ht, uint32_t slotpos);

/**
 * @brief Determine if posision holds a value
 * @param ht - pointer to a hash table struct
 * @param slotpos - ID of an record
 *  ID must be in valid range [0, ht->table_size) (asserted).
 */
bool
LIGHT(pos_valid)(struct LIGHT(core) *ht, uint32_t slotpos);



inline void
LIGHT(create)(struct LIGHT(core) *ht, size_t extent_size,
	      LIGHT(extent_alloc_t) extent_alloc_func,
	      LIGHT(extent_free_t) extent_free_func,
	      LIGHT_CMP_ARG_TYPE arg)
{
	assert((ht->GROW_INCREMENT & (ht->GROW_INCREMENT - 1)) == 0);
	assert(sizeof(LIGHT_DATA_TYPE) >= sizeof(uint32_t));
	ht->count = 0;
	ht->table_size = 0;
	ht->empty_slot = LIGHT(end);
	ht->empty_record = 0;
	ht->arg = arg;
	matras_create(&ht->mtable,
		      extent_size, sizeof(struct LIGHT(record)),
		      extent_alloc_func, extent_free_func);
}

inline void
LIGHT(destroy)(struct LIGHT(core) *ht)
{
	matras_destroy(&ht->mtable);
}

inline uint32_t
LIGHT(slot)(const struct LIGHT(core) *ht, uint32_t hash)
{
	uint32_t cover_mask = ht->cover_mask;
	uint32_t res = hash & cover_mask;
	uint32_t probe = (ht->table_size - res - 1) >> 31;
	uint32_t shift = __builtin_ctz(~(cover_mask >> 1));
	res ^= (probe << shift);
	return res;

}

inline uint32_t
LIGHT(find)(const struct LIGHT(core) *ht, uint32_t hash, LIGHT_DATA_TYPE value)
{
	if (ht->count == 0)
		return LIGHT(end);
	uint32_t slot = LIGHT(slot)(ht, hash);
	struct LIGHT(record) *record = (struct LIGHT(record) *)
		matras_get(&ht->mtable, slot);
	if (record->next == slot)
		return LIGHT(end);
	while (1) {
		if (record->hash == hash && LIGHT_EQUAL((record->value), (value), (ht->arg)))
			return slot;
		slot = record->next;
		if (slot == LIGHT(end))
			return LIGHT(end);
		record = (struct LIGHT(record) *)
			matras_get(&ht->mtable, slot);
	}
	/* unreachable */
	return LIGHT(end);
}

inline uint32_t
LIGHT(replace)(struct LIGHT(core) *ht, uint32_t hash, LIGHT_DATA_TYPE value, LIGHT_DATA_TYPE *replaced)
{
	if (ht->count == 0)
		return LIGHT(end);
	uint32_t slot = LIGHT(slot)(ht, hash);
	struct LIGHT(record) *record = (struct LIGHT(record) *)
		matras_get(&ht->mtable, slot);
	if (record->next == slot)
		return LIGHT(end);
	while (1) {
		if (record->hash == hash && LIGHT_EQUAL((record->value), (value), (ht->arg))) {
			*replaced = record->value;
			record->value = value;
			return slot;
		}
		slot = record->next;
		if (slot == LIGHT(end))
			return LIGHT(end);
		record = (struct LIGHT(record) *)
			matras_get(&ht->mtable, slot);
	}
	/* unreachable */
	return LIGHT(end);
}



inline uint32_t
LIGHT(find_key)(const struct LIGHT(core) *ht, uint32_t hash, LIGHT_KEY_TYPE key)
{
	if (ht->count == 0)
		return LIGHT(end);
	uint32_t slot = LIGHT(slot)(ht, hash);
	struct LIGHT(record) *record = (struct LIGHT(record) *)
		matras_get(&ht->mtable, slot);
	if (record->next == slot)
		return LIGHT(end);
	while (1) {
		if (record->hash == hash && LIGHT_EQUAL_KEY((record->value), (key), (ht->arg)))
			return slot;
		slot = record->next;
		if (slot == LIGHT(end))
			return LIGHT(end);
		record = (struct LIGHT(record) *)
			matras_get(&ht->mtable, slot);
	}
	/* unreachable */
	return LIGHT(end);
}

inline uint32_t
LIGHT(get_empty_prev)(struct LIGHT(record) *record)
{
	return record->hash;
}

inline void
LIGHT(set_empty_prev)(struct LIGHT(record) *record, uint32_t pos)
{
	record->hash = pos;
}

inline uint32_t
LIGHT(get_empty_next)(struct LIGHT(record) *record)
{
	return (uint32_t)(uint64_t)record->value;
}

inline void
LIGHT(set_empty_next)(struct LIGHT(record) *record, uint32_t pos)
{
	record->value = (LIGHT_DATA_TYPE)(int64_t)pos;
}

inline void
LIGHT(enqueue_empty)(struct LIGHT(core) *ht, uint32_t slot, struct LIGHT(record) *record)
{
	record->next = slot;
	if (ht->empty_record)
		LIGHT(set_empty_prev)(ht->empty_record, slot);
	LIGHT(set_empty_prev)(record, LIGHT(end));
	LIGHT(set_empty_next)(record, ht->empty_slot);
	ht->empty_record = record;
	ht->empty_slot = slot;
}

inline void
LIGHT(detach_first_empty)(struct LIGHT(core) *ht)
{
	assert(ht->empty_record);
	ht->empty_slot = LIGHT(get_empty_next)(ht->empty_record);
	if (ht->empty_slot == LIGHT(end)) {
		ht->empty_record = 0;
	} else {
		ht->empty_record = (struct LIGHT(record) *)
			matras_get(&ht->mtable, ht->empty_slot);
		LIGHT(set_empty_prev)(ht->empty_record, LIGHT(end));
	}
}

inline void
LIGHT(detach_empty)(struct LIGHT(core) *ht, struct LIGHT(record) *record)
{
	uint32_t prev_slot = LIGHT(get_empty_prev)(record);
	uint32_t next_slot = LIGHT(get_empty_next)(record);
	if (prev_slot == LIGHT(end)) {
		ht->empty_slot = next_slot;
		if (next_slot == LIGHT(end))
			ht->empty_record = 0;
		else
			ht->empty_record = (struct LIGHT(record) *)
				matras_get(&ht->mtable, next_slot);
	} else {
		struct LIGHT(record) *prev_record = (struct LIGHT(record) *)
			matras_get(&ht->mtable, prev_slot);
		LIGHT(set_empty_next)(prev_record, next_slot);
	}
	if (next_slot != LIGHT(end)) {
		struct LIGHT(record) *next_record = (struct LIGHT(record) *)
			matras_get(&ht->mtable, next_slot);
		LIGHT(set_empty_prev)(next_record, prev_slot);
	}
}

inline bool
LIGHT(prepare_first_insert)(struct LIGHT(core) *ht)
{
	assert(ht->count == 0);
	assert(ht->table_size == 0);
	assert(ht->mtable.block_counts[0] == 0);

	uint32_t slot;
	struct LIGHT(record) *record = (struct LIGHT(record) *)
		matras_alloc_range(&ht->mtable, &slot, ht->GROW_INCREMENT);
	if (!record)
		return false;
	assert(slot == 0);
	ht->table_size = ht->GROW_INCREMENT;
	ht->cover_mask = ht->GROW_INCREMENT - 1;
	ht->empty_slot = 0;
	ht->empty_record = record;
	for (int i = 0; i < ht->GROW_INCREMENT; i++) {
		record[i].next = i;
		LIGHT(set_empty_prev)(record + i, i - 1);
		LIGHT(set_empty_next)(record + i, i + 1);
	}
	LIGHT(set_empty_prev)(record, LIGHT(end));
	LIGHT(set_empty_next)(record + ht->GROW_INCREMENT - 1, LIGHT(end));
	return true;
}

inline bool
LIGHT(grow)(struct LIGHT(core) *ht)
{
	uint32_t new_slot;
	struct LIGHT(record) *new_record = (struct LIGHT(record) *)
		matras_alloc_range(&ht->mtable, &new_slot, ht->GROW_INCREMENT);
	if (!new_record) /* memory failure */
		return false;
	ht->table_size += ht->GROW_INCREMENT;

	if (ht->cover_mask < ht->table_size - 1)
		ht->cover_mask = (ht->cover_mask << 1) | (uint32_t)1;

	uint32_t split_comm_mask = (ht->cover_mask >> 1);
	uint32_t split_diff_mask = ht->cover_mask ^ split_comm_mask;

	uint32_t susp_slot = new_slot & split_comm_mask;
	struct LIGHT(record) *susp_record = (struct LIGHT(record) *)
		matras_get(&ht->mtable, susp_slot);

	for (int i = 0; i < ht->GROW_INCREMENT; i++, susp_slot++, susp_record++, new_slot++, new_record++) {
		if (susp_record->next == susp_slot) {
			/* Suspicious slot is empty, nothing to split */
			LIGHT(enqueue_empty)(ht, new_slot, new_record);
			continue;
		}
		if ((susp_record->hash & split_comm_mask) != susp_slot) {
			/* Another chain in suspicious slot, nothing to split */
			LIGHT(enqueue_empty)(ht, new_slot, new_record);
			continue;
		}

		uint32_t chain_head_slot[2] = {susp_slot, new_slot};
		struct LIGHT(record) *chain_head[2] = {susp_record, new_record};
		struct LIGHT(record) *chain_tail[2] = {0, 0};
		uint32_t shift = __builtin_ctz(split_diff_mask);
		assert(split_diff_mask == (((uint32_t)1) << shift));

		uint32_t last_empty_slot = new_slot;
		struct LIGHT(record) *last_empty_record = new_record;
		uint32_t prev_flag = 0;
		struct LIGHT(record) *test_record = susp_record;
		uint32_t test_slot = susp_slot;
		struct LIGHT(record) *prev_record = 0;
		while (1) {
			uint32_t test_flag = (test_record->hash >> shift) & ((uint32_t)1);
			if (test_flag  != prev_flag) {
				chain_tail[prev_flag] = prev_record;
				if (chain_tail[test_flag]) {
					chain_tail[test_flag]->next = test_slot;
				} else {
					*chain_head[test_flag] = *test_record;
					last_empty_slot = test_slot;
					last_empty_record = test_record;
					test_record = chain_head[test_flag];
					test_slot = chain_head_slot[test_flag];
				}
				prev_flag = test_flag;
			}
			test_slot = test_record->next;
			if (test_slot == LIGHT(end))
				break;
			prev_record = test_record;
			test_record = (struct LIGHT(record) *)
				matras_get(&ht->mtable, test_slot);
		}
		prev_flag = prev_flag ^ ((uint32_t)1);
		if (chain_tail[prev_flag])
			chain_tail[prev_flag]->next = LIGHT(end);

		LIGHT(enqueue_empty)(ht, last_empty_slot, last_empty_record);
	}
	return true;
}

inline uint32_t
LIGHT(insert)(struct LIGHT(core) *ht, uint32_t hash, LIGHT_DATA_TYPE value)
{
	if (ht->table_size == 0)
		if (!LIGHT(prepare_first_insert)(ht))
			return LIGHT(end);
	if (!ht->empty_record)
		if (!LIGHT(grow)(ht))
			return LIGHT(end);

	ht->count++;
	uint32_t slot = LIGHT(slot)(ht, hash);
	struct LIGHT(record) *record = (struct LIGHT(record) *)
		matras_get(&ht->mtable, slot);

	if (record->next == slot) {
		/* Inserting to an empty slot */
		LIGHT(detach_empty)(ht, record);
		record->value = value;
		record->hash = hash;
		record->next = LIGHT(end);
		return slot;
	}

	uint32_t empty_slot = ht->empty_slot;
	struct LIGHT(record) *empty_record = ht->empty_record;
	LIGHT(detach_first_empty)(ht);

	uint32_t chain_slot = LIGHT(slot)(ht, record->hash);
	if (chain_slot == slot) {
		/* add to existing chain */
		empty_record->value = value;
		empty_record->hash = hash;
		empty_record->next = record->next;
		record->next = empty_slot;
		return empty_slot;
	} else {
		/* create new chain */
		struct LIGHT(record) *chain = (struct LIGHT(record) *)
			matras_get(&ht->mtable, chain_slot);
		while (chain->next != slot) {
			chain_slot = chain->next;
			chain = (struct LIGHT(record) *)
				matras_get(&ht->mtable, chain_slot);
		}
		*empty_record = *record;
		chain->next = empty_slot;
		record->value = value;
		record->hash = hash;
		record->next = LIGHT(end);
		return slot;
	}
}

inline void
LIGHT(delete)(struct LIGHT(core) *ht, uint32_t slot)
{
	assert(slot < ht->table_size);
	uint32_t empty_slot;
	struct LIGHT(record) *empty_record;
	struct LIGHT(record) *record = (struct LIGHT(record) *)
		matras_get(&ht->mtable, slot);
	assert(record->next != slot);
	if (record->next != LIGHT(end)) {
		empty_slot = record->next;
		empty_record = (struct LIGHT(record) *)
			matras_get(&ht->mtable, empty_slot);
		*record = *empty_record;
	} else {
		empty_slot = slot;
		empty_record = record;
		uint32_t chain_slot = LIGHT(slot)(ht, record->hash);
		if (chain_slot != slot) {
			/* deleting a last record of chain */
			struct LIGHT(record) *chain = (struct LIGHT(record) *)
				matras_get(&ht->mtable, chain_slot);
			uint32_t chain_next_slot = chain->next;
			assert(chain_next_slot != LIGHT(end));
			while (chain_next_slot != slot) {
				chain_slot = chain_next_slot;
				chain = (struct LIGHT(record) *)
					matras_get(&ht->mtable, chain_next_slot);
				chain_next_slot = chain->next;
				assert(chain_next_slot != LIGHT(end));
			}
			chain->next = LIGHT(end);
		}
	}
	LIGHT(enqueue_empty)(ht, empty_slot, empty_record);
	ht->count--;
}

inline void
LIGHT(delete_value)(struct LIGHT(core) *ht, uint32_t hash, LIGHT_DATA_TYPE value)
{
	if (ht->count == 0)
		return;
	uint32_t slot = LIGHT(slot)(ht, hash);
	struct LIGHT(record) *record = (struct LIGHT(record) *)
		matras_get(&ht->mtable, slot);
	if (record->next == slot)
		return;
	struct LIGHT(record) *prev_record = 0;
	while (1) {
		if (record->hash == hash && LIGHT_EQUAL((record->value), (value), (ht->arg)))
			break;
		slot = record->next;
		if (slot == LIGHT(end))
			return;
		prev_record = record;
		record = (struct LIGHT(record) *)
			matras_get(&ht->mtable, slot);
	}
	ht->count--;
	if (prev_record) {
		prev_record->next = record->next;
		LIGHT(enqueue_empty)(ht, slot, record);
		return;
	}
	if (record->next == LIGHT(end)) {
		LIGHT(enqueue_empty)(ht, slot, record);
		return;
	}
	uint32_t next_slot = record->next;
	struct LIGHT(record) *next_record = (struct LIGHT(record) *)
		matras_get(&ht->mtable, next_slot);
	*record = *next_record;
	LIGHT(enqueue_empty)(ht, next_slot, next_record);
}

inline LIGHT_DATA_TYPE
LIGHT(get)(struct LIGHT(core) *ht, uint32_t slotpos)
{
	assert(slotpos < ht->table_size);
	struct LIGHT(record) *record = (struct LIGHT(record) *)
		matras_get(&ht->mtable, slotpos);
	assert(record->next != slotpos);
	return record->value;
}

/**
 * @brief Determine if posision holds a value
 * @param ht - pointer to a hash table struct
 * @param slotpos - ID of an record
 */
inline bool
LIGHT(pos_valid)(LIGHT(core) *ht, uint32_t slotpos)
{
	assert(slotpos < ht->table_size);
	struct LIGHT(record) *record = (struct LIGHT(record) *)
		matras_get(&ht->mtable, slotpos);
	return record->next != slotpos;
}


inline int
LIGHT(selfcheck)(const struct LIGHT(core) *ht)
{
	int res = 0;
	if (ht->table_size != ht->mtable.block_counts[0])
		res |= 64;
	uint32_t empty_slot = ht->empty_slot;
	if (empty_slot == LIGHT(end)) {
		if (ht->empty_record)
			res |= 512;
	} else {
		struct LIGHT(record) *should_be = (struct LIGHT(record) *)
			matras_get(&ht->mtable, empty_slot);
		if (ht->empty_record != should_be)
			res |= 1024;
	}
	uint32_t prev_empty_slot = LIGHT(end);
	while (empty_slot != LIGHT(end)) {
		struct LIGHT(record) *empty_record = (struct LIGHT(record) *)
			matras_get(&ht->mtable, empty_slot);
		if (empty_record->next != empty_slot)
			res |= 2048;
		if (LIGHT(get_empty_prev)(empty_record) != prev_empty_slot)
			res |= 4096;
		prev_empty_slot = empty_slot;
		empty_slot = LIGHT(get_empty_next)(empty_record);
	}
	for (uint32_t i = 0; i < ht->table_size; i++) {
		struct LIGHT(record) *record = (struct LIGHT(record) *)
			matras_get(&ht->mtable, i);
		if (record->next == i) {
			uint32_t empty_slot = ht->empty_slot;
			while (empty_slot != LIGHT(end) && empty_slot != i) {
				struct LIGHT(record) *empty_record = (struct LIGHT(record) *)
					matras_get(&ht->mtable, empty_slot);
				empty_slot = LIGHT(get_empty_next)(empty_record);
			}
			if (empty_slot != i)
				res |= 256;
			continue;
		}
		uint32_t slot = LIGHT(slot)(ht, record->hash);
		if (slot != i) {
			bool found = false;
			uint32_t chain_slot = slot;
			uint32_t chain_start_slot = slot;
			do {
				struct LIGHT(record) *chain_record = (struct LIGHT(record) *)
					matras_get(&ht->mtable, chain_slot);
				chain_slot = chain_record->next;
				if (chain_slot >= ht->table_size) {
					res |= 16; /* out of bounds (1) */
					break;
				}
				if (chain_slot == i) {
					found = true;
					break;
				}
				if (chain_slot == chain_start_slot) {
					res |= 4; /* cycles in chain (1) */
					break;
				}
			} while (chain_slot != LIGHT(end));
			if (!found)
				res |= 1; /* slot is out of chain */
		} else {
			do {
				struct LIGHT(record) *record = (struct LIGHT(record) *)
					matras_get(&ht->mtable, slot);
				if (LIGHT(slot)(ht, record->hash) != i)
					res |= 2; /* wrong value in chain */
				slot = record->next;
				if (slot != LIGHT(end) && slot >= ht->table_size) {
					res |= 32; /* out of bounds (2) */
					break;
				}
				if (slot == i) {
					res |= 8; /* cycles in chain (2) */
					break;
				}
			} while (slot != LIGHT(end));
		}
	}
	return res;
}

