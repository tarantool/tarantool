/*
 * *No header guard*: the header is allowed to be included twice
 * with different sets of defines.
 */
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
 * Overhead per value stored in a hash table.
 * Must be adjusted if struct LIGHT(record) is modified.
 */
enum { LIGHT_RECORD_OVERHEAD = 8 };

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
		uint32_t empty_next;
		/* Round record size up to nearest power of two. */
		uint8_t padding[(1 << (32 - __builtin_clz(sizeof(LIGHT_DATA_TYPE) +
							LIGHT_RECORD_OVERHEAD - 1))) -
				LIGHT_RECORD_OVERHEAD];
	};
};

/* Number of records added while grow iteration */
enum { LIGHT_GROW_INCREMENT = 8 };

/**
 * Common fields used by both a hash table and a hash table view
 */
struct LIGHT(common) {
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

	/* additional parameter for data comparison */
	LIGHT_CMP_ARG_TYPE arg;

	/* dynamic storage for records */
	struct matras *mtable;
	/* version of matras memory for MVCC */
	struct matras_view *view;
};

/**
 * Main struct for holding hash table
 */
struct LIGHT(core) {
	/* hash table implementation */
	struct LIGHT(common) common;
	/* dynamic storage for records */
	struct matras mtable;
	/* head matras view */
	struct matras_view view;
};

/**
 * Hash table view - frozen snapshot of a hash table
 */
struct LIGHT(view) {
	/* hash table implementation */
	struct LIGHT(common) common;
	/* version of matras memory for MVCC */
	struct matras_view view;
};

/**
 * Iterator, for iterating all values in hash_table.
 * It also may be used for restoring one value by key.
 */
struct LIGHT(iterator) {
	/* Current position on table (ID of a current record) */
	uint32_t slotpos;
};

/**
 * Special result of light_find that means that nothing was found
 * Must be equal or greater than possible hash table size
 */
static const uint32_t LIGHT(end) = 0xFFFFFFFF;

/* Functions declaration */

/**
 * @brief Hash table construction. Fills struct light members.
 * @param ht - pointer to a hash table struct
 * @param arg - optional parameter to save for comparing function
 * @param extent_size - size of allocating memory blocks
 * @param extent_alloc_func - memory blocks allocation function
 * @param extent_free_func - memory blocks allocation function
 * @param alloc_ctx - argument passed to memory block allocator
 * @param alloc_stats - optional extent allocator statistics
 */
static inline void
LIGHT(create)(struct LIGHT(core) *ht, LIGHT_CMP_ARG_TYPE arg,
	      size_t extent_size, matras_alloc_func extent_alloc_func,
	      matras_free_func extent_free_func, void *alloc_ctx,
	      struct matras_stats *alloc_stats);

/**
 * @brief Hash table destruction. Frees all allocated memory
 * @param ht - pointer to a hash table struct
 */
static inline void
LIGHT(destroy)(struct LIGHT(core) *ht);

/**
 * @brief Hash table view construction.
 *  All following hash table updates will not apply to the view.
 * @param v - pointer to a hash table view struct
 * @param ht - pointer to a hash table struct
 */
static inline void
LIGHT(view_create)(struct LIGHT(view) *v, struct LIGHT(core) *ht);

/**
 * @brief Hash table view destruction.
 * @param v - pointer to a hash table view struct
 */
static inline void
LIGHT(view_destroy)(struct LIGHT(view) *v);

/**
 * @brief Number of records stored in hash table
 * @param ht - pointer to a hash table struct
 * @return number of records
 */
static inline uint32_t
LIGHT(count)(struct LIGHT(core) *ht);

/**
 * @brief Number of records stored in hash table view
 * @param v - pointer to a hash table view struct
 * @return number of records
 */
static inline uint32_t
LIGHT(view_count)(struct LIGHT(view) *v);

/**
 * @brief Find a record with given hash and value
 * @param ht - pointer to a hash table struct
 * @param hash - hash to find
 * @param data - value to find
 * @return integer ID of found record or light_end if nothing found
 */
static inline uint32_t
LIGHT(find)(const struct LIGHT(core) *ht, uint32_t hash, LIGHT_DATA_TYPE data);

/**
 * @brief Find a record with given hash and value
 * @param v - pointer to a hash table view struct
 * @param hash - hash to find
 * @param data - value to find
 * @return integer ID of found record or light_end if nothing found
 */
static inline uint32_t
LIGHT(view_find)(const struct LIGHT(view) *v, uint32_t hash,
		 LIGHT_DATA_TYPE data);

/**
 * @brief Find a record with given hash and key
 * @param ht - pointer to a hash table struct
 * @param hash - hash to find
 * @param data - key to find
 * @return integer ID of found record or light_end if nothing found
 */
static inline uint32_t
LIGHT(find_key)(const struct LIGHT(core) *ht, uint32_t hash, LIGHT_KEY_TYPE data);

/**
 * @brief Find a record with given hash and key
 * @param v - pointer to a hash table view struct
 * @param hash - hash to find
 * @param data - key to find
 * @return integer ID of found record or light_end if nothing found
 */
static inline uint32_t
LIGHT(view_find_key)(const struct LIGHT(view) *v, uint32_t hash,
		     LIGHT_KEY_TYPE data);

/**
 * @brief Insert a record with given hash and value
 * @param ht - pointer to a hash table struct
 * @param hash - hash to insert
 * @param data - value to insert
 * @return integer ID of inserted record or light_end if failed
 */
static inline uint32_t
LIGHT(insert)(struct LIGHT(core) *ht, uint32_t hash, LIGHT_DATA_TYPE data);

/**
 * @brief Replace a record with given hash and value
 * @param ht - pointer to a hash table struct
 * @param hash - hash to find
 * @param data - value to find and replace
 * @param replaced - pointer to a value that was stored in table before replace
 * @return integer ID of found record or light_end if nothing found
 */
static inline uint32_t
LIGHT(replace)(struct LIGHT(core) *ht, uint32_t hash,
	       LIGHT_DATA_TYPE data, LIGHT_DATA_TYPE *replaced);

/**
 * @brief Delete a record from a hash table by given record ID
 * @param ht - pointer to a hash table struct
 * @param slotpos - ID of an record. See LIGHT(find) for details.
 * @return 0 if ok, -1 on memory error (only with freezed iterators)
 */
static inline int
LIGHT(delete)(struct LIGHT(core) *ht, uint32_t slotpos);

/**
 * @brief Delete a record from a hash table by that value and its hash.
 * @param ht - pointer to a hash table struct
 * @param slotpos - ID of an record. See LIGHT(find) for details.
 * @return 0 if ok, 1 if not found or -1 on memory error
 * (only with freezed iterators)
 */
static inline int
LIGHT(delete_value)(struct LIGHT(core) *ht,
		    uint32_t hash, LIGHT_DATA_TYPE value);

/**
 * @brief Get a value from a desired position
 * @param ht - pointer to a hash table struct
 * @param slotpos - ID of an record
 *  ID must be vaild, check it by light_pos_valid (asserted).
 */
static inline LIGHT_DATA_TYPE
LIGHT(get)(struct LIGHT(core) *ht, uint32_t slotpos);

/**
 * @brief Get a value from a desired position
 * @param v - pointer to a hash table view struct
 * @param slotpos - ID of an record
 *  ID must be valid, check it by light_pos_valid (asserted).
 */
static inline LIGHT_DATA_TYPE
LIGHT(view_get)(struct LIGHT(view) *v, uint32_t slotpos);

/**
 * @brief Get a random record
 * @param ht - pointer to a hash table struct
 * @param rnd - some random value
 * @return integer ID of random record or light_end if table is empty
 */
static inline uint32_t
LIGHT(random)(const struct LIGHT(core) *ht, uint32_t rnd);

/**
 * @brief Set iterator to the beginning of hash table
 * @param ht - pointer to a hash table struct
 * @param itr - iterator to set
 */
static inline void
LIGHT(iterator_begin)(const struct LIGHT(core) *ht, struct LIGHT(iterator) *itr);

/**
 * @brief Set iterator to the beginning of hash table
 * @param v - pointer to a hash table view struct
 * @param itr - iterator to set
 */
static inline void
LIGHT(view_iterator_begin)(const struct LIGHT(view) *v,
			   struct LIGHT(iterator) *itr);

/**
 * @brief Set iterator to position determined by key
 * @param ht - pointer to a hash table struct
 * @param itr - iterator to set
 * @param hash - hash to find
 * @param data - key to find
 */
static inline void
LIGHT(iterator_key)(const struct LIGHT(core) *ht, struct LIGHT(iterator) *itr,
	            uint32_t hash, LIGHT_KEY_TYPE data);

/**
 * @brief Set iterator to position determined by key
 * @param v - pointer to a hash table view struct
 * @param itr - iterator to set
 * @param hash - hash to find
 * @param data - key to find
 */
static inline void
LIGHT(view_iterator_key)(const struct LIGHT(view) *v,
			 struct LIGHT(iterator) *itr, uint32_t hash,
			 LIGHT_KEY_TYPE data);

/**
 * @brief Get the value that iterator currently points to
 * @param ht - pointer to a hash table struct
 * @param itr - iterator to set
 * @return poiner to the value or NULL if iteration is complete
 */
static inline LIGHT_DATA_TYPE *
LIGHT(iterator_get_and_next)(const struct LIGHT(core) *ht,
			     struct LIGHT(iterator) *itr);

/**
 * @brief Get the value that iterator currently points to
 * @param v - pointer to a hash table view struct
 * @param itr - iterator to set
 * @return poiner to the value or NULL if iteration is complete
 */
static inline LIGHT_DATA_TYPE *
LIGHT(view_iterator_get_and_next)(const struct LIGHT(view) *v,
				  struct LIGHT(iterator) *itr);

/* Functions definition */

/**
 * @brief Hash table construction. Fills struct light members.
 * @param htab - pointer to a hash table struct
 * @param arg - optional parameter to save for comparing function
 * @param extent_size - size of allocating memory blocks
 * @param extent_alloc_func - memory blocks allocation function
 * @param extent_free_func - memory blocks allocation function
 * @param alloc_ctx - argument passed to memory block allocator
 * @param alloc_stats - optional extent allocator statistics
 */
static inline void
LIGHT(create)(struct LIGHT(core) *htab, LIGHT_CMP_ARG_TYPE arg,
	      size_t extent_size, matras_alloc_func extent_alloc_func,
	      matras_free_func extent_free_func, void *alloc_ctx,
	      struct matras_stats *alloc_stats)
{
	struct LIGHT(common) *ht = &htab->common;
	assert((LIGHT_GROW_INCREMENT & (LIGHT_GROW_INCREMENT - 1)) == 0);
	assert(sizeof(LIGHT_DATA_TYPE) >= sizeof(uint32_t));
	ht->count = 0;
	ht->table_size = 0;
	ht->empty_slot = LIGHT(end);
	ht->arg = arg;
	matras_create(&htab->mtable,
		      extent_size, sizeof(struct LIGHT(record)),
		      extent_alloc_func, extent_free_func, alloc_ctx,
		      alloc_stats);
	matras_head_read_view(&htab->view);
	ht->mtable = &htab->mtable;
	ht->view = &htab->view;
}

/**
 * @brief Hash table destruction. Frees all allocated memory
 * @param ht - pointer to a hash table struct
 */
static inline void
LIGHT(destroy)(struct LIGHT(core) *ht)
{
	matras_destroy(&ht->mtable);
}

/**
 * @brief Hash table view construction.
 *  All following hash table updates will not apply to the view.
 * @param v - pointer to a hash table view struct
 * @param ht - pointer to a hash table struct
 */
static inline void
LIGHT(view_create)(struct LIGHT(view) *v, struct LIGHT(core) *ht)
{
	v->common = ht->common;
	v->common.view = &v->view;
	matras_create_read_view(v->common.mtable, &v->view);
}

/**
 * @brief Hash table view destruction.
 * @param v - pointer to a hash table view struct
 */
static inline void
LIGHT(view_destroy)(struct LIGHT(view) *v)
{
	matras_destroy_read_view(v->common.mtable, &v->view);
}

/**
 * @brief Number of records stored in hash table
 * @param ht - pointer to a hash table struct
 * @return number of records
 */
static inline uint32_t
LIGHT(count_impl)(struct LIGHT(common) *ht)
{
	return ht->count;
}

static inline uint32_t
LIGHT(count)(struct LIGHT(core) *ht)
{
	return LIGHT(count_impl)(&ht->common);
}

static inline uint32_t
LIGHT(view_count)(struct LIGHT(view) *v)
{
	return LIGHT(count_impl)(&v->common);
}

/**
 * Find a slot (index in the hash table), where an item with
 * given hash should be placed.
 */
static inline uint32_t
LIGHT(slot)(const struct LIGHT(common) *ht, uint32_t hash)
{
	uint32_t cover_mask = ht->cover_mask;
	uint32_t res = hash & cover_mask;
	uint32_t probe = (ht->table_size - res - 1) >> 31;
	uint32_t shift = __builtin_ctz(~(cover_mask >> 1));
	res ^= (probe << shift);
	return res;

}

/**
 * Get a record stored at the given hash table slot for read.
 */
static inline struct LIGHT(record) *
LIGHT(get_record)(const struct LIGHT(common) *ht, uint32_t slot)
{
	return (struct LIGHT(record) *)matras_view_get(ht->mtable,
						       ht->view, slot);
}

/**
 * Get a record stored at the given hash table slot for update.
 */
static inline struct LIGHT(record) *
LIGHT(touch_record)(struct LIGHT(common) *ht, uint32_t slot)
{
	assert(!matras_is_read_view_created(ht->view));
	return (struct LIGHT(record) *)matras_touch(ht->mtable, slot);
}

/**
 * @brief Find a record with given hash and value
 * @param ht - pointer to a hash table struct
 * @param hash - hash to find
 * @param data - value to find
 * @return integer ID of found record or light_end if nothing found
 */
static inline uint32_t
LIGHT(find_impl)(const struct LIGHT(common) *ht, uint32_t hash,
		 LIGHT_DATA_TYPE value)
{
	if (ht->count == 0)
		return LIGHT(end);
	uint32_t slot = LIGHT(slot)(ht, hash);
	struct LIGHT(record) *record = LIGHT(get_record)(ht, slot);
	if (record->next == slot)
		return LIGHT(end);
	while (1) {
		if (record->hash == hash
		    && LIGHT_EQUAL((record->value), (value), (ht->arg)))
			return slot;
		slot = record->next;
		if (slot == LIGHT(end))
			return LIGHT(end);
		record = LIGHT(get_record)(ht, slot);
	}
	/* unreachable */
	return LIGHT(end);
}

static inline uint32_t
LIGHT(find)(const struct LIGHT(core) *ht, uint32_t hash,
	    LIGHT_DATA_TYPE value)
{
	return LIGHT(find_impl)(&ht->common, hash, value);
}

static inline uint32_t
LIGHT(view_find)(const struct LIGHT(view) *v, uint32_t hash,
		 LIGHT_DATA_TYPE value)
{
	return LIGHT(find_impl)(&v->common, hash, value);
}

/**
 * @brief Find a record with given hash and key
 * @param ht - pointer to a hash table struct
 * @param hash - hash to find
 * @param data - key to find
 * @return integer ID of found record or light_end if nothing found
 */
static inline uint32_t
LIGHT(find_key_impl)(const struct LIGHT(common) *ht, uint32_t hash,
		     LIGHT_KEY_TYPE key)
{
	if (ht->count == 0)
		return LIGHT(end);
	uint32_t slot = LIGHT(slot)(ht, hash);
	struct LIGHT(record) *record = LIGHT(get_record)(ht, slot);
	if (record->next == slot)
		return LIGHT(end);
	while (1) {
		if (record->hash == hash &&
		    LIGHT_EQUAL_KEY((record->value), (key), (ht->arg)))
			return slot;
		slot = record->next;
		if (slot == LIGHT(end))
			return LIGHT(end);
		record = LIGHT(get_record)(ht, slot);
	}
	/* unreachable */
	return LIGHT(end);
}

static inline uint32_t
LIGHT(find_key)(const struct LIGHT(core) *ht, uint32_t hash, LIGHT_KEY_TYPE key)
{
	return LIGHT(find_key_impl)(&ht->common, hash, key);
}

static inline uint32_t
LIGHT(view_find_key)(const struct LIGHT(view) *v, uint32_t hash,
		     LIGHT_KEY_TYPE key)
{
	return LIGHT(find_key_impl)(&v->common, hash, key);
}

/**
 * @brief Replace a record with given hash and value
 * @param htab - pointer to a hash table struct
 * @param hash - hash to find
 * @param data - value to find and replace
 * @param replaced - pointer to a value that was stored in table before replace
 * @return integer ID of found record or light_end if nothing found
 */
static inline uint32_t
LIGHT(replace)(struct LIGHT(core) *htab, uint32_t hash,
	       LIGHT_DATA_TYPE value, LIGHT_DATA_TYPE *replaced)
{
	struct LIGHT(common) *ht = &htab->common;
	if (ht->count == 0)
		return LIGHT(end);
	uint32_t slot = LIGHT(slot)(ht, hash);
	struct LIGHT(record) *record = LIGHT(get_record)(ht, slot);
	if (record->next == slot)
		return LIGHT(end);
	while (1) {
		if (record->hash == hash
		    && LIGHT_EQUAL((record->value), (value), (ht->arg))) {
			record = LIGHT(touch_record)(ht, slot);
			if (!record)
				return LIGHT(end);
			*replaced = record->value;
			record->value = value;
			return slot;
		}
		slot = record->next;
		if (slot == LIGHT(end))
			return LIGHT(end);
		record = LIGHT(get_record)(ht, slot);
	}
	/* unreachable */
	return LIGHT(end);
}

/*
 * Empty records (that do not store value) are linked into doubly linked list.
 * Get an slot of the previous record in that list.
 */
static inline uint32_t
LIGHT(get_empty_prev)(struct LIGHT(record) *record)
{
	return record->hash;
}

/*
 * Empty records (that do not store value) are linked into doubly linked list.
 * Set an slot of the previous record in that list.
 */
static inline void
LIGHT(set_empty_prev)(struct LIGHT(record) *record, uint32_t pos)
{
	record->hash = pos;
}

/*
 * Empty records (that do not store value) are linked into doubly linked list.
 * Get an slot of the next record in that list.
 */
static inline uint32_t
LIGHT(get_empty_next)(struct LIGHT(record) *record)
{
	return record->empty_next;
}

/*
 * Empty records (that do not store value) are linked into doubly linked list.
 * Get an slot of the next record in that list.
 */
static inline void
LIGHT(set_empty_next)(struct LIGHT(record) *record, uint32_t pos)
{
	record->empty_next = pos;
}

/*
 * Empty records (that do not store value) are linked into doubly linked list.
 * Add given record with given slot to that list.
 * Touches matras of the record
 */
static inline int
LIGHT(enqueue_empty)(struct LIGHT(common) *ht, uint32_t slot,
		     struct LIGHT(record) *record)
{
	record->next = slot;
	if (ht->empty_slot != LIGHT(end)) {
		struct LIGHT(record) *empty_record =
			LIGHT(touch_record)(ht, ht->empty_slot);
		if (!empty_record)
			return -1;
		LIGHT(set_empty_prev)(empty_record, slot);
	}
	LIGHT(set_empty_prev)(record, LIGHT(end));
	LIGHT(set_empty_next)(record, ht->empty_slot);
	ht->empty_slot = slot;
	return 0;
}

/*
 * Empty records (that do not store value) are linked into doubly linked list.
 * Remove from list first record of that list and return that record
 * Touches matras of result and all changing records
 */
static inline struct LIGHT(record) *
LIGHT(detach_first_empty)(struct LIGHT(common) *ht)
{
	assert(ht->empty_slot != LIGHT(end));
	struct LIGHT(record) *empty_record =
		LIGHT(touch_record)(ht, ht->empty_slot);
	if (!empty_record)
		return 0;
	assert(empty_record == LIGHT(get_record)(ht, ht->empty_slot));
	assert(empty_record->next == ht->empty_slot);
	uint32_t new_empty_slot = LIGHT(get_empty_next)(empty_record);
	if (new_empty_slot != LIGHT(end)) {
		struct LIGHT(record) *new_empty_record =
			LIGHT(touch_record)(ht, new_empty_slot);
		if (!new_empty_record)
			return 0;
		LIGHT(set_empty_prev)(new_empty_record, LIGHT(end));
	}
	ht->empty_slot = new_empty_slot;
	return empty_record;
}

/*
 * Empty records (that do not store value) are linked into doubly linked list.
 * Remove from list the record by given slot and return that record
 * Touches matras of result and all changing records
 */
static inline struct LIGHT(record) *
LIGHT(detach_empty)(struct LIGHT(common) *ht, uint32_t slot)
{
	struct LIGHT(record) *record = LIGHT(touch_record)(ht, slot);
	if (!record)
		return 0;
	uint32_t prev_slot = LIGHT(get_empty_prev)(record);
	uint32_t next_slot = LIGHT(get_empty_next)(record);
	struct LIGHT(record) *prev_record = 0;
	if (prev_slot != LIGHT(end)) {
		prev_record = LIGHT(touch_record)(ht, prev_slot);
		if (!prev_record)
			return 0;
	}
	struct LIGHT(record) *next_record = 0;
	if (next_slot != LIGHT(end)) {
		next_record = LIGHT(touch_record)(ht, next_slot);
		if (!next_record)
			return 0;
	}
	if (prev_slot != LIGHT(end)) {
		LIGHT(set_empty_next)(prev_record, next_slot);
	} else {
		ht->empty_slot = next_slot;
	}
	if (next_slot != LIGHT(end)) {
		LIGHT(set_empty_prev)(next_record, prev_slot);
	}
	return record;
}

/*
 * Allocate memory and initialize empty list to get ready for first insertion
 */
static inline int
LIGHT(prepare_first_insert)(struct LIGHT(common) *ht)
{
	assert(ht->count == 0);
	assert(ht->table_size == 0);
	assert(ht->mtable->head.block_count == 0);
	assert(!matras_is_read_view_created(ht->view));

	uint32_t slot;
	struct LIGHT(record) *record = (struct LIGHT(record) *)
		matras_alloc_range(ht->mtable, &slot, LIGHT_GROW_INCREMENT);
	if (!record)
		return -1;
	assert(slot == 0);
	ht->table_size = LIGHT_GROW_INCREMENT;
	ht->cover_mask = LIGHT_GROW_INCREMENT - 1;
	ht->empty_slot = 0;
	for (int i = 0; i < LIGHT_GROW_INCREMENT; i++) {
		record[i].next = i;
		LIGHT(set_empty_prev)(record + i, i - 1);
		LIGHT(set_empty_next)(record + i, i + 1);
	}
	LIGHT(set_empty_prev)(record, LIGHT(end));
	LIGHT(set_empty_next)(record + LIGHT_GROW_INCREMENT - 1, LIGHT(end));
	return 0;
}

/*
 * Enlarge hash table to store more values
 */
static inline int
LIGHT(grow)(struct LIGHT(common) *ht)
{
	assert(!matras_is_read_view_created(ht->view));
	assert(ht->empty_slot == LIGHT(end));
	uint32_t new_slot;
	struct LIGHT(record) *new_record = (struct LIGHT(record) *)
		matras_alloc_range(ht->mtable, &new_slot, LIGHT_GROW_INCREMENT);
	if (!new_record) /* memory failure */
		return -1;
	new_record = LIGHT(touch_record)(ht, new_slot);
	if (!new_record) { /* memory failure */
		matras_dealloc_range(ht->mtable, LIGHT_GROW_INCREMENT);
		return -1;
	}
	uint32_t save_cover_mask = ht->cover_mask;
	ht->table_size += LIGHT_GROW_INCREMENT;
	if (ht->cover_mask < ht->table_size - 1)
		ht->cover_mask = (ht->cover_mask << 1) | (uint32_t)1;

	uint32_t split_comm_mask = (ht->cover_mask >> 1);
	uint32_t split_diff_mask = ht->cover_mask ^ split_comm_mask;

	uint32_t susp_slot = new_slot & split_comm_mask;
	struct LIGHT(record) *susp_record = LIGHT(touch_record)(ht, susp_slot);
	if (!susp_record) {
		matras_dealloc_range(ht->mtable, LIGHT_GROW_INCREMENT);
		ht->cover_mask = save_cover_mask;
		ht->table_size -= LIGHT_GROW_INCREMENT;
		return -1;
	}

	for (int i = 0; i < LIGHT_GROW_INCREMENT;
	     i++, susp_slot++, susp_record++, new_slot++, new_record++) {
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
		uint32_t prev_flag = 0;
		struct LIGHT(record) *test_record = susp_record;
		uint32_t test_slot = susp_slot;
		struct LIGHT(record) *prev_record = 0;
		uint32_t prev_slot = LIGHT(end);
		while (1) {
			uint32_t test_flag = (test_record->hash >> shift)
					     & ((uint32_t)1);
			if (test_flag != prev_flag) {
				if (prev_slot != LIGHT(end))
					prev_record = LIGHT(touch_record)(
								ht, prev_slot);
					/* TODO: check the result */
				chain_tail[prev_flag] = prev_record;
				if (chain_tail[test_flag]) {
					chain_tail[test_flag]->next = test_slot;
				} else {
					*chain_head[test_flag] = *test_record;
					last_empty_slot = test_slot;
					test_slot = chain_head_slot[test_flag];
				}
				prev_flag = test_flag;
			}
			prev_slot = test_slot;
			test_slot = test_record->next;
			if (test_slot == LIGHT(end))
				break;
			test_record = LIGHT(get_record)(ht, test_slot);
		}
		prev_flag = prev_flag ^ ((uint32_t)1);
		if (chain_tail[prev_flag])
			chain_tail[prev_flag]->next = LIGHT(end);

		struct LIGHT(record) *last_empty_record =
			LIGHT(touch_record)(ht, last_empty_slot);
		LIGHT(enqueue_empty)(ht, last_empty_slot, last_empty_record);
	}
	return 0;
}

/**
 * @brief Insert a record with given hash and value
 * @param htab - pointer to a hash table struct
 * @param hash - hash to insert
 * @param data - value to insert
 * @return integer ID of inserted record or light_end if failed
 */
static inline uint32_t
LIGHT(insert)(struct LIGHT(core) *htab, uint32_t hash, LIGHT_DATA_TYPE value)
{
	struct LIGHT(common) *ht = &htab->common;
	if (ht->table_size == 0)
		if (LIGHT(prepare_first_insert)(ht))
			return LIGHT(end);
	if (ht->empty_slot == LIGHT(end))
		if (LIGHT(grow)(ht))
			return LIGHT(end);
	assert(ht->table_size == ht->mtable->head.block_count);

	ht->count++;
	uint32_t slot = LIGHT(slot)(ht, hash);
	struct LIGHT(record) *record = LIGHT(touch_record)(ht, slot);
	if (!record)
		return LIGHT(end);

	if (record->next == slot) {
		/* Inserting to an empty slot */
		record = LIGHT(detach_empty)(ht, slot);
		if (!record)
			return LIGHT(end);
		record->value = value;
		record->hash = hash;
		record->next = LIGHT(end);
		return slot;
	}

	uint32_t chain_slot = LIGHT(slot)(ht, record->hash);
	struct LIGHT(record) *chain = 0;
	if (chain_slot != slot) {
		chain = LIGHT(get_record)(ht, chain_slot);
		while (chain->next != slot) {
			chain_slot = chain->next;
			chain = LIGHT(get_record)(ht, chain_slot);
		}
		chain = LIGHT(touch_record)(ht, chain_slot);
		if (!chain)
			return LIGHT(end);
	}

	uint32_t empty_slot = ht->empty_slot;
	struct LIGHT(record) *empty_record = LIGHT(detach_first_empty)(ht);
	if (!empty_record)
		return LIGHT(end);

	if (chain_slot == slot) {
		/* add to existing chain */
		empty_record->value = value;
		empty_record->hash = hash;
		empty_record->next = record->next;
		record->next = empty_slot;
		return empty_slot;
	} else {
		/* create new chain */
		*empty_record = *record;
		chain->next = empty_slot;
		record->value = value;
		record->hash = hash;
		record->next = LIGHT(end);
		return slot;
	}
}

/**
 * @brief Delete a record from a hash table by given record ID
 * @param htab - pointer to a hash table struct
 * @param slotpos - ID of an record. See LIGHT(find) for details.
 * @return 0 if ok, -1 on memory error (only with freezed iterators)
 */
static inline int
LIGHT(delete)(struct LIGHT(core) *htab, uint32_t slot)
{
	struct LIGHT(common) *ht = &htab->common;
	assert(slot < ht->table_size);
	uint32_t empty_slot;
	struct LIGHT(record) *empty_record;
	struct LIGHT(record) *record = LIGHT(touch_record)(ht, slot);
	if (!record)
		return -1;
	assert(record->next != slot);
	if (ht->empty_slot != LIGHT(end)) {
		if (!LIGHT(touch_record)(ht, ht->empty_slot))
			return -1;
	}
	if (record->next != LIGHT(end)) {
		empty_slot = record->next;
		empty_record = LIGHT(touch_record)(ht, empty_slot);
		if (!empty_record)
			return -1;
		*record = *empty_record;
	} else {
		empty_slot = slot;
		empty_record = record;
		uint32_t chain_slot = LIGHT(slot)(ht, record->hash);
		if (chain_slot != slot) {
			/* deleting a last record of chain */
			struct LIGHT(record) *chain =
				LIGHT(get_record)(ht, chain_slot);
			uint32_t chain_next_slot = chain->next;
			assert(chain_next_slot != LIGHT(end));
			while (chain_next_slot != slot) {
				chain_slot = chain_next_slot;
				chain = LIGHT(get_record)(ht, chain_slot);
				chain_next_slot = chain->next;
				assert(chain_next_slot != LIGHT(end));
			}
			chain = LIGHT(touch_record)(ht, chain_slot);
			if (!chain)
				return -1;
			chain->next = LIGHT(end);
		}
	}
	LIGHT(enqueue_empty)(ht, empty_slot, empty_record);
	ht->count--;
	return 0;
}

/**
 * @brief Delete a record from a hash table by that value and its hash.
 * @param htab - pointer to a hash table struct
 * @param slotpos - ID of an record. See LIGHT(find) for details.
 * @return 0 if ok, 1 if not found or -1 on memory error
 * (only with freezed iterators)
 */
static inline int
LIGHT(delete_value)(struct LIGHT(core) *htab, uint32_t hash,
		    LIGHT_DATA_TYPE value)
{
	struct LIGHT(common) *ht = &htab->common;
	if (ht->count == 0)
		return 1; /* not found */
	uint32_t slot = LIGHT(slot)(ht, hash);
	struct LIGHT(record) *record = LIGHT(get_record)(ht, slot);
	if (record->next == slot)
		return 1; /* not found */
	uint32_t prev_slot = LIGHT(end);
	struct LIGHT(record) *prev_record = 0;
	while (1) {
		if (record->hash == hash
		    && LIGHT_EQUAL((record->value), (value), (ht->arg)))
			break;
		prev_slot = slot;
		slot = record->next;
		if (slot == LIGHT(end))
			return 1; /* not found */
		prev_record = record;
		record = LIGHT(get_record)(ht, slot);
	}
	record = LIGHT(touch_record)(ht, slot);
	if (!record) {
		return -1; /* mem fail */
	}
	if (ht->empty_slot != LIGHT(end)) {
		if (!LIGHT(touch_record)(ht, ht->empty_slot))
			return -1; /* mem fail */
	}
	if (prev_record) {
		prev_record = LIGHT(touch_record)(ht, prev_slot);
		if (!prev_record)
			return -1; /* mem fail */
		prev_record->next = record->next;
		LIGHT(enqueue_empty)(ht, slot, record);
		ht->count--;
		return 0;
	}
	if (record->next == LIGHT(end)) {
		LIGHT(enqueue_empty)(ht, slot, record);
		ht->count--;
		return 0;
	}
	uint32_t next_slot = record->next;
	struct LIGHT(record) *next_record = LIGHT(touch_record)(ht, next_slot);
	if (!next_record)
		return -1; /* mem fail */
	*record = *next_record;
	LIGHT(enqueue_empty)(ht, next_slot, next_record);
	ht->count--;
	return 0;
}

/**
 * @brief Get a value from a desired position
 * @param ht - pointer to a hash table struct
 * @param slotpos - ID of an record
 *  ID must be vaild, check it by light_pos_valid (asserted).
 */
static inline LIGHT_DATA_TYPE
LIGHT(get_impl)(struct LIGHT(common) *ht, uint32_t slotpos)
{
	assert(slotpos < ht->table_size);
	struct LIGHT(record) *record = LIGHT(get_record)(ht, slotpos);
	assert(record->next != slotpos);
	return record->value;
}

static inline LIGHT_DATA_TYPE
LIGHT(get)(struct LIGHT(core) *ht, uint32_t slotpos)
{
	return LIGHT(get_impl)(&ht->common, slotpos);
}

static inline LIGHT_DATA_TYPE
LIGHT(view_get)(struct LIGHT(view) *v, uint32_t slotpos)
{
	return LIGHT(get_impl)(&v->common, slotpos);
}

/**
 * @brief Get a random record
 * @param htab - pointer to a hash table struct
 * @param rnd - some random value
 * @return integer ID of random record or light_end if table is empty
 */
static inline uint32_t
LIGHT(random)(const struct LIGHT(core) *htab, uint32_t rnd)
{
	const struct LIGHT(common) *ht = &htab->common;
	if (ht->count == 0)
		return LIGHT(end);
	rnd %= (ht->table_size);
	while (true) {
		struct LIGHT(record) *record = LIGHT(get_record)(ht, rnd);
		if (record->next != rnd)
			break;
		rnd++;
		rnd %= (ht->table_size);
	}
	return rnd;
}

/**
 * @brief Set iterator to the beginning of hash table
 * @param htab - pointer to a hash table struct
 * @param itr - iterator to set
 */
static inline void
LIGHT(iterator_begin_impl)(const struct LIGHT(common) *ht,
			   struct LIGHT(iterator) *itr)
{
	(void)ht;
	itr->slotpos = 0;
}

static inline void
LIGHT(iterator_begin)(const struct LIGHT(core) *ht, struct LIGHT(iterator) *itr)
{
	LIGHT(iterator_begin_impl)(&ht->common, itr);
}

static inline void
LIGHT(view_iterator_begin)(const struct LIGHT(view) *v,
			   struct LIGHT(iterator) *itr)
{
	LIGHT(iterator_begin_impl)(&v->common, itr);
}

/**
 * @brief Set iterator to position determined by key
 * @param ht - pointer to a hash table struct
 * @param itr - iterator to set
 * @param hash - hash to find
 * @param data - key to find
 */
static inline void
LIGHT(iterator_key_impl)(const struct LIGHT(common) *ht,
			 struct LIGHT(iterator) *itr, uint32_t hash,
			 LIGHT_KEY_TYPE data)
{
	itr->slotpos = LIGHT(find_key_impl)(ht, hash, data);
}

static inline void
LIGHT(iterator_key)(const struct LIGHT(core) *ht, struct LIGHT(iterator) *itr,
		    uint32_t hash, LIGHT_KEY_TYPE data)
{
	LIGHT(iterator_key_impl)(&ht->common, itr, hash, data);
}

static inline void
LIGHT(view_iterator_key)(const struct LIGHT(view) *v,
			 struct LIGHT(iterator) *itr, uint32_t hash,
			 LIGHT_KEY_TYPE data)
{
	LIGHT(iterator_key_impl)(&v->common, itr, hash, data);
}

/**
 * @brief Get the value that iterator currently points to
 * @param ht - pointer to a hash table struct
 * @param itr - iterator to set
 * @return poiner to the value or NULL if iteration is complete
 */
static inline LIGHT_DATA_TYPE *
LIGHT(iterator_get_and_next_impl)(const struct LIGHT(common) *ht,
				  struct LIGHT(iterator) *itr)
{
	while (itr->slotpos < ht->table_size) {
		uint32_t slotpos = itr->slotpos;
		struct LIGHT(record) *record = LIGHT(get_record)(ht, slotpos);
		itr->slotpos++;
		if (record->next != slotpos)
			return &record->value;
	}
	return 0;
}

static inline LIGHT_DATA_TYPE *
LIGHT(iterator_get_and_next)(const struct LIGHT(core) *ht,
			     struct LIGHT(iterator) *itr)
{
	return LIGHT(iterator_get_and_next_impl)(&ht->common, itr);
}

static inline LIGHT_DATA_TYPE *
LIGHT(view_iterator_get_and_next)(const struct LIGHT(view) *v,
				  struct LIGHT(iterator) *itr)
{
	return LIGHT(iterator_get_and_next_impl)(&v->common, itr);
}

/*
 * Selfcheck of the internal state of hash table. Used only for debugging.
 * That means that you should not use this function.
 * If return not zero, something went terribly wrong.
 */
static inline int
LIGHT(selfcheck)(const struct LIGHT(core) *htab)
{
	const struct LIGHT(common) *ht = &htab->common;
	int res = 0;
	if (ht->table_size != ht->mtable->head.block_count)
		res |= 64;
	uint32_t empty_slot = ht->empty_slot;
	uint32_t prev_empty_slot = LIGHT(end);
	while (empty_slot != LIGHT(end)) {
		struct LIGHT(record) *empty_record =
			LIGHT(get_record)(ht, empty_slot);
		if (empty_record->next != empty_slot)
			res |= 2048;
		if (LIGHT(get_empty_prev)(empty_record) != prev_empty_slot)
			res |= 4096;
		prev_empty_slot = empty_slot;
		empty_slot = LIGHT(get_empty_next)(empty_record);
	}
	for (uint32_t i = 0; i < ht->table_size; i++) {
		struct LIGHT(record) *record = LIGHT(get_record)(ht, i);
		if (record->next == i) {
			uint32_t empty_slot = ht->empty_slot;
			while (empty_slot != LIGHT(end) && empty_slot != i) {
				struct LIGHT(record) *empty_record =
					LIGHT(get_record)(ht, empty_slot);
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
				struct LIGHT(record) *chain_record =
					LIGHT(get_record)(ht, chain_slot);
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
				struct LIGHT(record) *record =
					LIGHT(get_record)(ht, slot);
				if (LIGHT(slot)(ht, record->hash) != i)
					res |= 2; /* wrong value in chain */
				slot = record->next;
				if (slot != LIGHT(end)
				    && slot >= ht->table_size) {
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

