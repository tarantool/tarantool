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
 * light - Linear probing Incremental Growing Hash Table
 */


/**
 * Prefix for all names of struct and functions in this header file.
 * Mat be empty, but still have to be defined
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
#ifndef CONCAT
#define CONCAT_R(a, b) a##b
#define CONCAT(a, b) CONCAT_R(a, b)
#endif

#ifdef _
#error '_' must be undefinded!
#endif
#define LH(name) CONCAT(LIGHT_NAME, name)

/**
 * Main struct for holding hash table
 */
struct LH(light) {
	/* count of values in hash table */
	uint32_t count;
	/* size of hash table in clusters ( equal to mtable.size ) */
	uint32_t table_size;
	/*
	 * cover is power of two;
	 * if table_size is positive, then cover/2 < table_size <= cover
	 * cover_mask is cover - 1
	 */
	uint32_t cover_mask;
	/* additional parameter for data comparison */
	LIGHT_CMP_ARG_TYPE arg;
	/* dynamic storage for clusters */
	struct matras mtable;
};

/**
 * Type of functions for memory allocation and deallocation
 */
typedef void *(*LH(light_extent_alloc_t))();
typedef void (*LH(light_extent_free_t))(void *);

/**
 * @brief Hash table construction. Fills struct light members.
 * @param ht - pointer to a hash table struct
 * @param extent_size - size of allocating memory blocks
 * @param extent_alloc_func - memory blocks allocation function
 * @param extent_free_func - memory blocks allocation function
 * @param arg - optional parameter to save for comparing function
 */
void
LH(light_create)(struct LH(light) *ht, size_t extent_size,
	   LH(light_extent_alloc_t) extent_alloc_func,
	   LH(light_extent_free_t) extent_free_func,
	   LIGHT_CMP_ARG_TYPE arg);


/**
 * @brief Hash table destruction. Frees all allocated memory
 * @param ht - pointer to a hash table struct
 */
void
LH(light_destroy)(struct LH(light) *ht);

/**
 * @brief Find a record with given hash and value
 * @param ht - pointer to a hash table struct
 * @param hash - hash to find
 * @param data - value to find
 * @return integer ID of found record or light_end if nothing found
 */
uint32_t
LH(light_find)(const struct LH(light) *ht, uint32_t hash, LIGHT_DATA_TYPE data);

/**
 * @brief Find a record with given hash and key
 * @param ht - pointer to a hash table struct
 * @param hash - hash to find
 * @param data - key to find
 * @return integer ID of found record or light_end if nothing found
 */
uint32_t
LH(light_find_key)(const struct LH(light) *ht, uint32_t hash, LIGHT_KEY_TYPE data);

/**
 * @brief Insert a record with given hash and value
 * @param ht - pointer to a hash table struct
 * @param hash - hash to insert
 * @param data - value to insert
 * @return integer ID of inserted record or light_end if failed
 */
uint32_t
LH(light_insert)(struct LH(light) *ht, uint32_t hash, LIGHT_DATA_TYPE data);

/**
 * @brief Replace a record with given hash and value
 * @param ht - pointer to a hash table struct
 * @param hash - hash to find
 * @param data - value to find and replace
 * @param replaced - pointer to a value that was stored in table before replace
 * @return integer ID of found record or light_end if nothing found
 */
uint32_t
LH(light_replace)(struct LH(light) *ht, uint32_t hash, LIGHT_DATA_TYPE data, LIGHT_DATA_TYPE *replaced);

/**
 * @brief Delete a record from a hash table by given record ID
 * @param ht - pointer to a hash table struct
 * @param slotpos - ID of an record. See light_find for details.
 */
void
LH(light_delete)(struct LH(light) *ht, uint32_t slotpos);

/**
 * @brief Get a value from a desired position
 * @param ht - pointer to a hash table struct
 * @param slotpos - ID of an record
 */
LIGHT_DATA_TYPE
LH(light_get)(struct LH(light) *ht, uint32_t slotpos);

/**
 * @brief Determine if posision holds a value
 * @param ht - pointer to a hash table struct
 * @param slotpos - ID of an record
 */
bool
LH(light_pos_valid)(struct LH(light) *ht, uint32_t slotpos);

/**
 * Internal definitions
 */
enum { LIGHT_CLUSTER_SIZE = 64 };
static const uint32_t LH(light_end) = 0xFFFFFFFFu;

struct LH(light_cluster) {
	uint32_t flags;
	uint32_t hash[5];
	LIGHT_DATA_TYPE data[5];
};

/**
 * Light implementation
 */
inline void
LH(light_create)(struct LH(light) *ht, size_t extent_size,
	   LH(light_extent_alloc_t) extent_alloc_func,
	   LH(light_extent_free_t) extent_free_func,
	   LIGHT_CMP_ARG_TYPE arg)
{
	/* could be less than LIGHT_CLUSTER_SIZE on 32bit system*/
	assert(sizeof(struct LH(light_cluster)) <= LIGHT_CLUSTER_SIZE);
	/* builtins (__builtin_ctz for example) must take uint32_t */
	assert(sizeof(unsigned) == sizeof(uint32_t));
	ht->count = 0;
	ht->table_size = 0;
	ht->cover_mask = 0;
	ht->arg = arg;
	matras_create(&ht->mtable,
		      extent_size, LIGHT_CLUSTER_SIZE,
		      extent_alloc_func, extent_free_func);
}

inline void
LH(light_destroy)(struct LH(light) *ht)
{
	matras_destroy(&ht->mtable);
}

inline uint32_t
LH(light_slot)(const struct LH(light) *ht, uint32_t hash)
{
	uint32_t high_hash = hash / 5u;
	uint32_t cover_mask = ht->cover_mask;
	uint32_t res = high_hash & cover_mask;
	uint32_t probe = (ht->table_size - res - 1) >> 31;
	uint32_t shift = __builtin_ctz(~(cover_mask >> 1));
	res ^= (probe << shift);
	return res;

}

inline struct LH(light_cluster) *
LH(light_cluster)(const struct LH(light) *ht, uint32_t slot)
{
	return (struct LH(light_cluster) *)
		matras_get(&ht->mtable, slot);
}

inline uint32_t
LH(light_find_key)(const struct LH(light) *ht, uint32_t hash, LIGHT_KEY_TYPE data)
{
	if (ht->table_size == 0)
		return LH(light_end);
	uint32_t slot = LH(light_slot)(ht, hash);
	while (1) {
		struct LH(light_cluster) *cluster = LH(light_cluster)(ht, slot);
		__builtin_prefetch(cluster, 0);
		uint32_t search_mask = ~(((hash & 017) | 060) * 01010101);
		uint32_t mask = cluster->flags & 03737373737;
		uint32_t found_mask = ((mask ^ search_mask) + 0101010101) & 04040404040;
		while (found_mask) {
			uint32_t bit = __builtin_ctz(found_mask);
			found_mask ^= (1u << bit);
			uint32_t pos = bit / 6;
			if (cluster->hash[pos] == hash &&
			    (LIGHT_EQUAL_KEY((cluster->data[pos]), (data), (ht->arg))))
				return slot * 5 + pos;
		}

		if (!(cluster->flags >> 31))
			return LH(light_end);
		slot++;
		if (slot >= ht->table_size)
			slot = 0;
	}
	/* unreachable */
	return LH(light_end);
}

inline uint32_t
LH(light_find)(const struct LH(light) *ht, uint32_t hash, LIGHT_DATA_TYPE data)
{
	if (ht->table_size == 0)
		return LH(light_end);
	uint32_t slot = LH(light_slot)(ht, hash);
	while (1) {
		struct LH(light_cluster) *cluster = LH(light_cluster)(ht, slot);
		__builtin_prefetch(cluster, 0);
		uint32_t search_mask = ~(((hash & 017) | 060) * 01010101);
		uint32_t mask = cluster->flags & 03737373737;
		uint32_t found_mask = ((mask ^ search_mask) + 0101010101) & 04040404040;
		while (found_mask) {
			uint32_t bit = __builtin_ctz(found_mask);
			found_mask ^= (1u << bit);
			uint32_t pos = bit / 6;
			if (cluster->hash[pos] == hash &&
			    (LIGHT_EQUAL((cluster->data[pos]), (data), (ht->arg))))
				return slot * 5 + pos;
		}

		if (!(cluster->flags >> 31))
			return LH(light_end);
		slot++;
		if (slot >= ht->table_size)
			slot = 0;
	}
	/* unreachable */
	return LH(light_end);
}

inline uint32_t
LH(light_replace)(struct LH(light) *ht, uint32_t hash, LIGHT_DATA_TYPE data, LIGHT_DATA_TYPE *replaced)
{
	if (ht->table_size == 0)
		return LH(light_end);
	uint32_t slot = LH(light_slot)(ht, hash);
	while (1) {
		struct LH(light_cluster) *cluster = LH(light_cluster)(ht, slot);
		__builtin_prefetch(cluster, 0);
		uint32_t search_mask = ~(((hash & 017) | 060) * 01010101);
		uint32_t mask = cluster->flags & 03737373737;
		uint32_t found_mask = ((mask ^ search_mask) + 0101010101) & 04040404040;
		while (found_mask) {
			uint32_t bit = __builtin_ctz(found_mask);
			found_mask ^= (1u << bit);
			uint32_t pos = bit / 6;
			if (cluster->hash[pos] == hash &&
			    (LIGHT_EQUAL((cluster->data[pos]), (data), (ht->arg)))) {
				*replaced = cluster->data[pos];
				cluster->data[pos] = data;
				return slot * 5 + pos;
			}
		}

		if (!(cluster->flags >> 31))
			return LH(light_end);
		slot++;
		if (slot >= ht->table_size)
			slot = 0;
	}
	/* unreachable */
	return LH(light_end);
}

inline void
LH(light_set_value)(struct LH(light_cluster) *cluster, uint32_t pos,
	uint32_t hash_flags, uint32_t hash, LIGHT_DATA_TYPE data)
{
	uint32_t shift = pos * 6;
	cluster->flags |= (((hash & 017) | hash_flags) << shift);
	cluster->hash[pos] = hash;
	cluster->data[pos] = data;
}

inline void
LH(light_clr_value)(struct LH(light_cluster) *cluster, uint32_t pos)
{
	uint32_t shift = pos * 6;
	cluster->flags &= ~(077 << shift);
}

inline void
LH(light_grow)(struct LH(light) *ht)
{
	uint32_t to_flags = 0;
	if (ht->table_size > 1) {
		struct LH(light_cluster) *cluster = LH(light_cluster)(ht, ht->table_size - 1);
		to_flags = cluster->flags & 0x80000000;
	}
	uint32_t to_slot;
	struct LH(light_cluster) *to_cluster = (struct LH(light_cluster) *)
		matras_alloc(&ht->mtable, &to_slot);
	__builtin_prefetch(to_cluster, 1);
	if (ht->cover_mask < ht->table_size)
		ht->cover_mask = (ht->cover_mask << 1) | 1;
	ht->table_size++;
	hash_t split_slot = to_slot & (ht->cover_mask >> 1);
	struct LH(light_cluster) *split_cluster = LH(light_cluster)(ht, split_slot);
	__builtin_prefetch(split_cluster, 1);
	hash_t split_diff_shift = __builtin_ctz(~(ht->cover_mask >> 1));
	hash_t mask = 0;
	for (int i = 0; i < 5; i++) {
		uint32_t match = (split_cluster->hash[i] / 5) >> split_diff_shift;
		uint32_t chain = split_cluster->flags >> (i * 6 + 5);
		mask |= (match & (~chain) & 1) << (i * 6);
	}
	mask *= 077;

	*to_cluster = *split_cluster;
	split_cluster->flags &= ~mask;
	to_cluster->flags &= mask;
	to_cluster->flags |= to_flags;

	uint32_t dst_slot = to_slot;
	uint32_t hash_flags = 020;
	while (split_cluster->flags >> 31) {
		split_slot++;
		if (split_slot == dst_slot)
			break;
		split_cluster = LH(light_cluster)(ht, split_slot);
		uint32_t test = (split_cluster->flags & 02020202020) & ((split_cluster->flags & 04040404040) >> 1) ;
		while (test) {
			uint32_t bit = __builtin_ctz(test);
			test ^= (1u << bit);
			uint32_t pos = bit / 6;
			if (LH(light_slot)(ht, split_cluster->hash[pos]) == dst_slot) {
				LH(light_clr_value)(split_cluster, pos);

				uint32_t slot = split_slot;
				struct LH(light_cluster) *cluster = split_cluster;
				while (!(cluster->flags & 024040404040)) {
					if (slot == 0)
						slot = ht->table_size;
					slot--;
					cluster = LH(light_cluster)(ht, slot);
					if (!(cluster->flags >> 31))
						break;
					cluster->flags &= 0x7FFFFFFF;
				}

				while ((to_cluster->flags & 02020202020) == 02020202020) {
					to_cluster->flags |= (1 << 31);
					hash_flags = 060;
					to_slot++;
					if (to_slot >= ht->table_size)
						to_slot = 0;
					to_cluster = (struct LH(light_cluster) *)
							matras_get(&ht->mtable, to_slot);
				}
				uint32_t to_pos = __builtin_ctz(((~to_cluster->flags) & 02020202020)) / 6;
				LH(light_set_value)(to_cluster, to_pos, hash_flags, split_cluster->hash[pos], split_cluster->data[pos]);
			}
		}
	}
}

inline uint32_t
LH(light_insert)(struct LH(light) *ht, uint32_t hash, LIGHT_DATA_TYPE data)
{
	if (ht->table_size == 0) {
		ht->table_size = 1;
		uint32_t slot;
		struct LH(light_cluster) *cluster = (struct LH(light_cluster) *)
			matras_alloc(&ht->mtable, &slot);
		cluster->flags = 0;
	}
	if (ht->count >= 1 * ht->table_size)
		LH(light_grow)(ht);

	uint32_t slot = LH(light_slot)(ht, hash);
	struct LH(light_cluster) *cluster = LH(light_cluster)(ht, slot);
	uint32_t hash_flags = 020;
	while ((cluster->flags & 02020202020) == 02020202020) {
		cluster->flags |= (1 << 31);
		hash_flags = 060;
		slot++;
		if (slot >= ht->table_size)
			slot = 0;
		cluster = LH(light_cluster)(ht, slot);
	}
	uint32_t pos = __builtin_ctz(((~cluster->flags) & 02020202020)) / 6;
	LH(light_set_value)(cluster, pos, hash_flags, hash, data);
	ht->count++;
	return slot * 5 + pos;
}

inline void
LH(light_delete)(struct LH(light) *ht, uint32_t slotpos)
{
	uint32_t slot = slotpos / 5;
	uint32_t pos = slotpos % 5;
	struct LH(light_cluster) *cluster = LH(light_cluster)(ht, slot);
	uint32_t was_chain = cluster->flags & (040 << (pos * 6));
	LH(light_clr_value)(cluster, pos);
	ht->count--;

	if (was_chain) {
		while (!(cluster->flags & 024040404040)) {
			if (slot == 0)
				slot = ht->table_size;
			slot--;
			cluster = LH(light_cluster)(ht, slot);
			if (!(cluster->flags >> 31))
				break;
			cluster->flags &= 0x7FFFFFFF;
		}
	}
}

inline LIGHT_DATA_TYPE
LH(light_get)(struct LH(light) *ht, uint32_t slotpos)
{
	uint32_t slot = slotpos / 5;
	uint32_t pos = slotpos % 5;
	struct LH(light_cluster) *cluster = LH(light_cluster)(ht, slot);
	return cluster->data[pos];
}

/**
 * @brief Determine if posision holds a value
 * @param ht - pointer to a hash table struct
 * @param slotpos - ID of an record
 */
inline bool
LH(light_pos_valid)(struct LH(light) *ht, uint32_t slotpos)
{
	uint32_t slot = slotpos / 5;
	uint32_t pos = slotpos % 5;
	struct LH(light_cluster) *cluster = LH(light_cluster)(ht, slot);
	return cluster->flags & (020 << (pos * 6));
}


inline uint32_t
LH(light_selfcheck)(const struct LH(light) *ht)
{
	uint32_t res = 0;
	uint32_t total_count = 0;
	for (uint32_t slot = 0; slot < ht->table_size; slot++) {
		struct LH(light_cluster) *cluster = LH(light_cluster)(ht, slot);
		uint32_t flags = cluster->flags;
		total_count += __builtin_popcount(flags & 02020202020);
		for (uint32_t pos = 0; pos < 5u; pos++) {
			if (flags & (020 << (pos * 6))) {
				uint32_t hint1 = (flags >> (pos * 6)) & 15;
				uint32_t hint2 = cluster->hash[pos] & 15;
				if (hint1 != hint2)
					res |= 1;

				uint32_t from_slot = LH(light_slot)(ht, cluster->hash[pos]);
				bool here1 = from_slot == slot;
				bool here2 = !(flags & (040 << (6 * pos)));
				if (here1 != here2)
					res |= 2;

				while (from_slot != slot) {
					struct LH(light_cluster) *from_cluster = LH(light_cluster)(ht, from_slot);
					if (!(from_cluster->flags >> 31)) {
						res |= 4;
						break;
					}
					from_slot++;
					if (from_slot >= ht->table_size)
						from_slot = 0;
				}
			}
		}
	}
	if (ht->count != total_count)
		res |= 256;

	uint32_t cover = ht->cover_mask + 1;
	if (ht->cover_mask & cover)
		res |= 512;

	if (ht->table_size && cover < ht->table_size)
		res |= 1024;
	if (ht->table_size && cover / 2 >= ht->table_size)
		res |= 2048;
	return res;
}

#undef LH
