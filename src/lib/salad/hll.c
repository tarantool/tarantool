/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "hll.h"
#include "hll_empirical.h"
#include "trivia/util.h"
#include "say.h"

#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <limits.h>
#include <math.h>
#include <assert.h>

enum HLL_CONSTANTS {
	/*
	 * 6 bits are needed to store the number of
	 * leading zeros of 64 bit hash.
	 */
	HLL_RANK_BITS = 6,

	/* The maximum value that can be stored in HLL_RANK_BITS bits. */
	HLL_RANK_MAX = (1 << HLL_RANK_BITS) - 1,

	/* Number of bits stored in a register bucket. */
	HLL_BUCKET_BITS = 24,
	/* Number of bits stored in a register bucket. */
	HLL_BUCKET_BYTES = HLL_BUCKET_BITS / CHAR_BIT,

	/*
	 * Grow coefficient for the sparse representation. 2 allows to grow
	 * to exact limit of the sparse representation
	 * (equals to the memory usage of the dense representation).
	 */
	HLL_SPARSE_GROW_COEF = 2,

	/*
	 * The smallest precision of HyperLogLog that can start with the sparse
	 * representation. Sparse representation for smaller precision can't
	 * store more than 100 pairs so there is no need in this representation.
	 */
	HLL_SPARSE_MIN_PRECISION = 10,

	/*
	 * Initial size of sparsely represented HyperLogLog.
	 * The initial size must accommodate at least a pairs header.
	 */
	HLL_SPARSE_INITIAL_BSIZE = 48,
};

/* Check if the precision is correct. */
static int MAYBE_UNUSED
hll_is_valid_precision(uint8_t prec)
{
	return hll_is_supported_precision(prec) || prec == HLL_SPARSE_PRECISION;
}

/* Get a number whose first n bits are equal to ones. */
static uint64_t
hll_ones(uint8_t n)
{
	assert(n <= 64);
	return ((UINT64_C(1) << n) - 1);
}

/* Check whether the cached value stores the valid value of the estimation. */
static int
hll_is_valid_cache(const struct hll *hll)
{
	return hll->cached_estimation >= 0;
}

/* Mark the cached value as invalid. */
static void
hll_invalidate_cache(struct hll *hll)
{
	hll->cached_estimation = -1.f;
}

/*
 * The highest precision bits of the hash are interpreted as a register index.
 */
static uint32_t
hll_hash_register_idx(uint64_t hash, uint8_t precision)
{
	assert(hll_is_valid_precision(precision));
	return hash >> (64 - precision);
}

/*
 * Return the number of leading zeros of the first
 * (64 - precision) hash bits plus one.
 */
static uint8_t
hll_hash_rank(uint64_t hash, uint8_t precision)
{
	assert(hll_is_valid_precision(precision));
	hash |= hll_ones(precision) << (64 - precision);
	uint8_t zero_count = 0;
	uint64_t bit = 0x1;
	while ((hash & bit) == 0) {
		++zero_count;
		bit <<= 1;
	}
	uint8_t rank = zero_count + 1;
	assert(rank <= HLL_RANK_MAX);
	return rank;
}

/* Calculate the number of registers for this presision. */
static uint64_t
hll_n_registers(uint8_t precision)
{
	assert(hll_is_valid_precision(precision));
	return UINT64_C(1) << precision;
}

double
hll_error(uint8_t precision)
{
	assert(hll_is_valid_precision(precision));
	uint64_t n_counters = hll_n_registers(precision);
	return 1.04 / sqrt(n_counters);
}

/* Alpha constant that HyperLogLog uses in the estimation formula. */
static double
hll_alpha(uint8_t precision)
{
	return 0.7213 / (1.0 + 1.079 / hll_n_registers(precision));
}

/* Estimate the cardinality using the LinearCounting algorithm. */
static double
linear_counting(size_t counters, size_t empty_counters)
{
	return counters * log((double)counters / empty_counters);
}

/* Initialize an uninitialized hll object. */
static void
hll_create(struct hll *hll, uint8_t precision,
	   enum HLL_REPRESENTATION representation);

/* Releaze resourses of the HyperLogLog estimator. */
static void
hll_destroy(struct hll *hll);

/*
 * ================================================================
 * Implementation of the dense representation.
 * Dense representation is a classical representation: there is alsways
 * allocated 2^precision counters so it may be wasteful for small caridnalities.
 * ================================================================
 */

/*
 * Dense register is represented by 6 bits so it can go out the range of one
 * byte but 4 registers occupy exactly a 3-byte bucket.
 * The registers array can always be separated to such buckets because its size
 * in bits is divided by 24 if precision is more than 2
 * (2^2 * 6 bits = 24 bits, other sizes differ by power of 2 times).
 *
 * The bucket structure:
 * +----------+----------+----------+----------+
 * |0 register|1 register|2 register|3 register|
 * +----------+----------+----------+----------+
 * |<---------6 bits * 4 = 24 bits------------>|
 */
struct reg_bucket {
	/* Pointer to the 3 byte bucket where the register is stored. */
	uint8_t *addr;
	/* Offset of the register in the bucket. */
	size_t offset;
};

static_assert(HLL_MIN_PRECISION >= 2,
	      "Bucket can go beyond the boundaries of registers array.");

/*
 * Init a register bucket structure that is used for
 * convenient work with registers.
 */
static void
reg_bucket_create(struct reg_bucket *bucket, uint8_t *regs, size_t reg_idx)
{
	/*
	 * regs		  1 byte	 2 byte	       3 byte	      4 byte
	 * |		  |		 |	       |	      |
	 * +----------+----------+----------+----------+----------+----------+--
	 * |0 register|1 register|2 register|3 register|4 register|5 register|..
	 * +----------+----------+----------+----------+----------+----------+--
	 * |	      6		 12	    18	       |          30	     32
	 * 0 bucket				       1 bucket
	 *
	 * For instance, the 5th register is stored in (5*6 / 24 = 1) the first
	 * bucket and its offset is equal to 5*6 % 24 = 6.
	 */
	size_t bucket_idx = reg_idx * HLL_RANK_BITS / HLL_BUCKET_BITS;
	size_t offset = reg_idx * HLL_RANK_BITS % HLL_BUCKET_BITS;
	assert(offset <= HLL_BUCKET_BITS - HLL_RANK_BITS);

	bucket->addr = regs + bucket_idx * HLL_BUCKET_BYTES;
	bucket->offset = offset;
}

/* Get an integer value of 3 bytes stored in the bucket. */
static uint32_t
reg_bucket_value(const struct reg_bucket *bucket)
{
	uint8_t *addr = bucket->addr;
	return addr[0] | (addr[1] << CHAR_BIT) | (addr[2] << 2 * CHAR_BIT);
}

/*
 * Get a mask that clears the register stored in the bucket and
 * saves the other registers in the bucket.
 */
static uint32_t
reg_bucket_boundary_mask(const struct reg_bucket *bucket)
{
	/*
	 * |000000000000000000111111|
	 * |------------regstr------|
	 */
	uint32_t ones = hll_ones(HLL_RANK_BITS);
	/*
	 * |000000000000111111000000|
	 * |------------regstr------|
	 */
	uint32_t register_mask = ones << bucket->offset;
	/*
	 * |111111111111000000111111|
	 * |------------regstr------|
	 */
	uint32_t boundary_mask = ~register_mask;
	return boundary_mask;
}

/* Get the value of the register stored in the bucket. */
static uint8_t
reg_bucket_register_value(const struct reg_bucket *bucket)
{
	uint32_t reg_mask = hll_ones(HLL_RANK_BITS);
	uint32_t bucket_value = reg_bucket_value(bucket);
	uint8_t register_value = (bucket_value >> bucket->offset) & reg_mask;
	return register_value;
}

/* Set a new value of the register stored in the bucket. */
static void
reg_bucket_set_register_value(struct reg_bucket *bucket, uint8_t value)
{
	uint32_t boundary_mask = reg_bucket_boundary_mask(bucket);
	uint32_t bucket_value = reg_bucket_value(bucket);
	union {
		uint32_t value;
		uint8_t bytes[HLL_BUCKET_BYTES];
	} modified_bucket;
	modified_bucket.value = (value << bucket->offset) |
				(bucket_value & boundary_mask);

	memcpy(bucket->addr, &modified_bucket.bytes, HLL_BUCKET_BYTES);
}

/*
 * Calculate the amount of memory reqired to store
 * the registers for the dense representation.
 */
static size_t
hll_dense_bsize(uint8_t precision)
{
	size_t n_registers = hll_n_registers(precision);
	return n_registers * HLL_RANK_BITS / CHAR_BIT;
}

/* Create a densely represented HyperLogLog estimator. */
void
hll_dense_create(struct hll *hll, uint8_t precision)
{
	hll->representation = HLL_DENSE;
	hll->dprecision = precision;
	hll->cached_estimation = 0;
	/* For the dense representation data interpreted as registers. */
	const size_t registers_bsize = hll_dense_bsize(precision);
	hll->data = xcalloc(registers_bsize, 1);
}

/* Get the value of the register with the idx index. */
static uint8_t
hll_dense_register_rank(const struct hll *hll, size_t idx)
{
	struct reg_bucket bucket;
	reg_bucket_create(&bucket, hll->data, idx);
	uint8_t rank = reg_bucket_register_value(&bucket);
	assert(rank <= HLL_RANK_MAX);
	return rank;
}

/* Set a new rank for the register with the idx index. */
static void
hll_dense_set_register_rank(struct hll *hll, size_t idx, uint8_t rank)
{
	struct reg_bucket bucket;
	reg_bucket_create(&bucket, hll->data, idx);
	assert(rank <= HLL_RANK_MAX);
	reg_bucket_set_register_value(&bucket, rank);
}

/* Add a new rank to the register with the idx index. */
static void
hll_dense_add_to_register(struct hll *hll, uint32_t idx, uint8_t new_rank)
{
	uint8_t reg_rank = hll_dense_register_rank(hll, idx);
	if (reg_rank < new_rank) {
		hll_dense_set_register_rank(hll, idx, new_rank);
		hll_invalidate_cache(hll);
	}
}

/* Add hash to the densely represented HyperLogLog estimator. */
static void
hll_dense_add(struct hll *hll, uint64_t hash)
{
	uint8_t precision = hll->dprecision;
	size_t idx = hll_hash_register_idx(hash, precision);
	uint8_t hash_rank = hll_hash_rank(hash, precision);
	hll_dense_add_to_register(hll, idx, hash_rank);
}

/*
 * Estimate the cardinality of the densely represented HyperLogLog using the
 * estimation formula. Raw estimation can have a large relative error
 * for small cardinalities.
 */
static double
hll_dense_raw_estimate(const struct hll *hll)
{
	double sum = 0;
	const size_t n_registers = hll_n_registers(hll->dprecision);
	for (size_t i = 0; i < n_registers; ++i) {
		sum += pow(2, -hll_dense_register_rank(hll, i));
	}

	const double alpha = hll_alpha(hll->dprecision);
	return alpha * n_registers * n_registers / sum;
}

/* Count the number of registers that are zero. */
static size_t
hll_dense_count_zero_registers(const struct hll *hll)
{
	size_t count = 0;
	const size_t n_registers = hll_n_registers(hll->dprecision);
	for (size_t i = 0; i < n_registers; ++i) {
		if (hll_dense_register_rank(hll, i) == 0)
			++count;
	}
	return count;
}

/* Estimate the caridnality of the densely represented HyperLogLog */
static uint64_t
hll_dense_estimate(struct hll *hll)
{
	if (hll_is_valid_cache(hll)) {
		return hll->cached_estimation;
	}

	const uint8_t prec = hll->dprecision;
	const size_t n_registers = hll_n_registers(prec);

	size_t zero_count = hll_dense_count_zero_registers(hll);
	uint64_t threshold = hll_empirical_estimation_threshold(prec);
	if (zero_count != 0) {
		double lc_estimation = linear_counting(n_registers, zero_count);
		if (lc_estimation < threshold)
			return hll->cached_estimation = lc_estimation;
	}

	double raw_estimation = hll_dense_raw_estimate(hll);

	double hll_estimation = raw_estimation;
	hll_estimation -= hll_empirical_bias_correction(prec, raw_estimation);

	return hll->cached_estimation = hll_estimation;
}

/*
 * =====================================================================
 * Implementation of the sparse representation.
 * The sparse representation works with pairs consisting of the rank and the
 * index of the added hashes. It requires less memory for small cardinalities
 * than the dense representation and provides better accuracy. The sparse
 * representation swithces to the dense representation if it statrs to require
 * more amount of memory that is needed for the dense representation.
 * =====================================================================
 */

/*
 * Instead of registers The sparse representation works with pairs consisting
 * of the rank and the index of the added hashes.
 * The pairs for the sparse representation have the following structure:
 * +--------------------------------+----------+
 * |		   index	    |   rank   |
 * +--------------------------------+----------+
 * |<-----------26 bits------------>|<-6 bits->|
 */
typedef uint32_t pair_t;

/* Make a pair with specified index and rank. */
static pair_t
hll_sparse_new_pair(size_t idx, uint8_t rank)
{
	return rank | (idx << HLL_RANK_BITS);
}

/* Get the index of the pair. */
static uint32_t
hll_sparse_pair_idx(pair_t pair)
{
	return pair >> HLL_RANK_BITS;
}

/* Get the rank of the pair. */
static uint8_t
hll_sparse_pair_rank(pair_t pair)
{
	return pair & hll_ones(HLL_RANK_BITS);
}

/* Get the index that would be if the dense representation was used. */
static uint32_t
hll_sparse_pair_dense_idx(pair_t pair, uint8_t precision)
{
	uint32_t idx = hll_sparse_pair_idx(pair);
	/*
	 * Since the sparse precision is more than any dense precisions
	 * the hash can alway be restored by discarding the extra bits.
	 * |101110101010010010010011...1011| : hash
	 * |<-------idx(26)------->|
	 * |101110101010010010010011...1011| : hash
	 * |<---idx(prec)--->|
	 */
	return idx >> (HLL_SPARSE_PRECISION - precision);
}

/* Get the rank that would be if the dense representation was used. */
static uint8_t
hll_sparse_pair_dense_rank(pair_t pair)
{
	/*
	 * I make an assumption that rank for both representations is the same.
	 * But in fact, the rank of the hash with sparse precision may differ by
	 * rank with the dense precision but the probability of this is
	 * less than 1 / 2 ^ (64 - 26) ~ 3.6e-12 (38 leading zeros)
	 * so such assumption will not make a big mistake.
	 *
	 * The formula explanation:
	 * |101110101010010010010011101110101010010010010011001110111010| : hash
	 * |<--------------same_bits----------->|<-------idx(26)------->|
	 * |<--------------same_bits----------->|<#####>|<---idx(18)--->|
	 *
	 * #### - are not included in rank(26), but may be included in rank(18),
	 * but only if there are more than 64 - 26 leading zeros, otherwise the
	 * rank will be calculated using only same_bits.
	 */
	return hll_sparse_pair_rank(pair);
}

/* Add a new pair to the densely represented HyperLogLog. */
static void
hll_dense_add_pair(struct hll *hll, pair_t pair)
{
	uint32_t idx = hll_sparse_pair_dense_idx(pair, hll->dprecision);
	uint8_t new_rank = hll_sparse_pair_dense_rank(pair);
	hll_dense_add_to_register(hll, idx, new_rank);
}

/* Add an array of pairs to the densely represented HyperLogLog. */
static void
hll_dense_add_pairs(struct hll *hll, pair_t *beg, pair_t *end)
{
	for (pair_t *pair = beg; pair != end; ++pair)
		hll_dense_add_pair(hll, *pair);
}

/*
 * Header for the pairs stored in sparsely represented HyperLogLog. The pairs
 * are stored in the data field of the hll structure.
 * The first bytes are used for the header.
 * After the header starts an array of pairs. This array is divided
 * into 2 parst: a sorted list of pairs and a temporary buffer for recently
 * added pairs. The list starts right after the header and grows by increasing
 * indexes and the buffer starst at the end of the pairs array and grows by
 * decreasing indexes. The list is kept sorted for efficient merging with a
 * buffer of recently added pairs, the buffer is sorted before the merging so
 * the merging takes linear time.
 *
 * Sparsely represented HyperLogLog structure:
 *
 *		list_idx------\ ->        <- /-----buff_idx
 * +------------+-------------------   ----------------+
 * |   HEADER   |  PAIRS LIST | ==> ... <== |  BUFFER  |
 * +------------+-------------------   ----------------+
 * |<---------------------bsize----------------------->|
 */
struct pairs_header {
	/* Index of the first pair after the list. */
	uint32_t list_idx;
	/* Index of the last pair added to the buffer. */
	uint32_t buff_idx;
	/*
	 * Amount of memory that is used to store the HyperLogLog
	 * (incluning the header).
	 */
	uint32_t bsize;
};

static_assert(sizeof(struct pairs_header) <= HLL_SPARSE_INITIAL_BSIZE,
	      "Pairs header gets out of the initial size bounds.");

/* Get the header of a sparsely represented HyperLogLog. */
static struct pairs_header *
hll_sparse_pairs_header(const uint8_t *pairs_data)
{
	return (struct pairs_header *)pairs_data;
}

/* Get the total size of a sparsely represented HyperLogLog. */
static uint32_t
hll_sparse_bsize(const uint8_t *pairs_data)
{
	struct pairs_header *header = hll_sparse_pairs_header(pairs_data);
	return header->bsize;
}

/* Get the maximum number of pairs for the current size. */
static uint32_t
hll_sparse_pairs_max_size(const uint8_t *pairs_data)
{
	uint32_t hll_bsize = hll_sparse_bsize(pairs_data);
	uint32_t header_bsize = sizeof(struct pairs_header);
	uint32_t pairs_bsize = hll_bsize - header_bsize;
	uint32_t max_size = pairs_bsize / sizeof(pair_t);
	return max_size;
}

/* Get the pairs stored in HyperLogLog. */
static pair_t *
hll_sparse_pairs(const uint8_t *pairs_data)
{
	return (pair_t *)(pairs_data + sizeof(struct pairs_header));
}

/*
 * Get a pointer to the first pair of the HyperLogLog list.
 * Increase for iteration.
 */
static pair_t *
hll_sparse_pairs_list_begin(const uint8_t *pairs_data)
{
	return hll_sparse_pairs(pairs_data);
}

/* Get the number of pairs stored in the HyperLogLog list. */
static uint32_t
hll_sparse_pairs_list_size(const uint8_t *pairs_data)
{
	struct pairs_header *header = hll_sparse_pairs_header(pairs_data);
	return header->list_idx;
}

/* Get a pointer to the first pair after the HyperLogLog list. */
static pair_t *
hll_sparse_pairs_list_end(const uint8_t *pairs_data)
{
	pair_t *begin = hll_sparse_pairs_list_begin(pairs_data);
	uint32_t size = hll_sparse_pairs_list_size(pairs_data);
	return begin + size;
}

/*
 * Get a pointer to the first pair of the HyperLogLog buffer.
 * Increase for iteration.
 */
static pair_t *
hll_sparse_pairs_buffer_begin(const uint8_t *pairs_data)
{
	struct pairs_header *header = hll_sparse_pairs_header(pairs_data);
	pair_t *pairs = hll_sparse_pairs(pairs_data);
	return pairs + header->buff_idx;
}

/* Get a pointer to the first pair after the HyperLogLog buffer. */
static pair_t *
hll_sparse_pairs_buffer_end(const uint8_t *pairs_data)
{
	pair_t *pairs = hll_sparse_pairs(pairs_data);
	uint32_t max_size = hll_sparse_pairs_max_size(pairs_data);
	return pairs + max_size;
}

/* Get the number of pairs stored in the HyperLogLog buffer. */
static uint32_t
hll_sparse_pairs_buffer_size(const uint8_t *pairs_data)
{
	pair_t *begin = hll_sparse_pairs_buffer_begin(pairs_data);
	pair_t *end = hll_sparse_pairs_buffer_end(pairs_data);
	return end - begin;
}

/* Create a sparsely represented HyperLogLog estimator. */
static void
hll_sparse_create(struct hll *hll, uint8_t precision)
{
	hll->representation = HLL_SPARSE;
	hll->dprecision = precision;
	/*
	 * For sparse representation data field stores
	 * pairs of the list and the buffer.
	 */
	hll->data = xcalloc(HLL_SPARSE_INITIAL_BSIZE, 1);
	struct pairs_header *header = hll_sparse_pairs_header(hll->data);
	header->bsize = HLL_SPARSE_INITIAL_BSIZE;
	header->list_idx = 0;
	header->buff_idx = hll_sparse_pairs_max_size(hll->data);
}

/* The HyperLogLog is full if the buffer reaches the list. */
static int
hll_sparse_is_full(const struct hll *hll)
{
	struct pairs_header *header = hll_sparse_pairs_header(hll->data);
	assert(header->list_idx <= header->buff_idx);
	return header->list_idx == header->buff_idx;
}

/*
 * Check if a size after growing is not more than
 * the size of the dense representation.
 */
static int
hll_sparse_can_grow(const struct hll *hll)
{
	uint32_t current_bsize = hll_sparse_bsize(hll->data);
	uint32_t new_bsize = HLL_SPARSE_GROW_COEF * current_bsize;
	uint32_t max_bsize = hll_dense_bsize(hll->dprecision);
	assert(current_bsize <= max_bsize);
	return new_bsize <= max_bsize;
}

/*
 * Increase the size of HyperLogLog by HLL_SPARSE_GROW_COEF times.
 * The buffer is cleared after the growing so in order not to lose pairs
 * from the buffer, they must be pre-merged with the list.
 */
static void
hll_sparse_grow_after_merge(struct hll *hll)
{
	assert(hll_sparse_pairs_buffer_size(hll->data) == 0);
	uint32_t bsize = hll_sparse_bsize(hll->data);
	hll->data = xrealloc(hll->data, HLL_SPARSE_GROW_COEF * bsize);
	struct pairs_header *header = hll_sparse_pairs_header(hll->data);
	header->bsize *= HLL_SPARSE_GROW_COEF;
	assert(header->bsize <= hll_dense_bsize(hll->dprecision));
	header->buff_idx = hll_sparse_pairs_max_size(hll->data);
}

/*
 * Insert a new pair into the buffer.
 * There must be a free space for a new pair.
 */
static void
hll_sparse_buff_add(struct hll *hll, pair_t pair)
{
	struct pairs_header *header = hll_sparse_pairs_header(hll->data);
	pair_t *pairs = hll_sparse_pairs(hll->data);
	assert(!hll_sparse_is_full(hll));
	pairs[--header->buff_idx] = pair;
}

/*
 * Comparator for pair_t type. One pair is bigger than another if it has
 * larger index value, or if indexes are equal, larger rank value.
 */
static int
pair_cmp(const void *p1_, const void *p2_)
{
	/*
	 * Since the last bits are used by index you can simply compare
	 * the pairs as integer values.
	 */
	const pair_t *p1 = p1_;
	const pair_t *p2 = p2_;
	return (*p1 > *p2) - (*p2 > *p1);
}

/*
 * Sort buffer pairs using the pairs comparator.
 * Sorting is needed for effective merging with the list.
 */
static void
hll_sparse_sort_buff(struct hll *hll)
{
	pair_t *buffer = hll_sparse_pairs_buffer_begin(hll->data);
	size_t buff_size = hll_sparse_pairs_buffer_size(hll->data);
	qsort(buffer, buff_size, sizeof(pair_t), pair_cmp);
}

/*
 * Merge the list with the pre-sorted buffer.
 * Return the number of pairs added to the resulting array.
 */
static uint32_t
merge_sorted_pairs_arrays(pair_t *beg1, pair_t *end1,
			  pair_t *beg2, pair_t *end2,
			  pair_t *res)
{
	uint32_t n = 0;
	pair_t *arr1 = beg1;
	pair_t *arr2 = beg2;
	while (arr1 != end1 && arr2 != end2) {
		if (*arr1 < *arr2)
			res[n++] = *arr1++;
		else
			res[n++] = *arr2++;
	}

	while (arr1 != end1)
		res[n++] = *arr1++;
	while (arr2 != end2)
		res[n++] = *arr2++;

	return n;
}

/*
 * Leave only pairs with unique indexes or
 * with the highest rank for duplicate indexes.
 */
static uint32_t
merge_pairs_with_duplicate_indexes(pair_t *pairs, uint32_t n_pairs)
{
	if (n_pairs == 0)
		return 0;

	uint32_t unique_idx = 0;
	uint32_t last_pair_idx = hll_sparse_pair_idx(pairs[0]);
	for (uint32_t i = 1; i < n_pairs; ++i) {
		if (last_pair_idx != hll_sparse_pair_idx(pairs[i])) {
			last_pair_idx = hll_sparse_pair_idx(pairs[i]);
			++unique_idx;
		}
		pairs[unique_idx] = pairs[i];
	}

	return unique_idx + 1;
}

/*
 * Merge the list with the buffer.
 * It takes o(buffer_size*log(buffer_size) to sort the buffer before the
 * merging, o(buffer_size + list_size) to merge and o(buffer_size + list_size)
 * to remove duplicate indexes.
 */
static void
hll_sparse_merge_list_with_buffer(struct hll *hll)
{
	if (hll_sparse_pairs_buffer_size(hll->data) == 0)
		return;

	hll_sparse_sort_buff(hll);

	pair_t *list = hll_sparse_pairs_list_begin(hll->data);
	pair_t *list_end = hll_sparse_pairs_list_end(hll->data);
	pair_t *buff = hll_sparse_pairs_buffer_begin(hll->data);
	pair_t *buff_end = hll_sparse_pairs_buffer_end(hll->data);
	assert(list_end <= buff);

	uint8_t *new_pairs = xcalloc(hll_sparse_bsize(hll->data), 1);
	pair_t *new_list = hll_sparse_pairs_list_begin(new_pairs);
	uint32_t new_list_size = merge_sorted_pairs_arrays(list, list_end,
							   buff, buff_end,
							   new_list);
	new_list_size =
		merge_pairs_with_duplicate_indexes(new_list, new_list_size);

	struct pairs_header *new_header = hll_sparse_pairs_header(new_pairs);
	new_header->bsize = hll_sparse_bsize(hll->data);
	new_header->list_idx = new_list_size;
	new_header->buff_idx = hll_sparse_pairs_max_size(hll->data);
	free(hll->data);
	hll->data = new_pairs;
}

/*
 * Convert a sparsely represented HyperLogLog to a densely represented
 * HyperLogLog. The sparsely represented HyperLogLog is freed after converting.
 */
static void
hll_sparse_to_dense(struct hll *sparse_hll)
{
	assert(sparse_hll->representation == HLL_SPARSE);

	uint8_t precision = sparse_hll->dprecision;
	struct hll tmp;
	struct hll *dense_hll = &tmp;
	hll_create(dense_hll, precision, HLL_DENSE);

	pair_t *list_beg = hll_sparse_pairs_list_begin(sparse_hll->data);
	pair_t *list_end = hll_sparse_pairs_list_end(sparse_hll->data);
	hll_dense_add_pairs(dense_hll, list_beg, list_end);
	pair_t *buff_beg = hll_sparse_pairs_buffer_begin(sparse_hll->data);
	pair_t *buff_end = hll_sparse_pairs_buffer_end(sparse_hll->data);
	hll_dense_add_pairs(dense_hll, buff_beg, buff_end);

	hll_destroy(sparse_hll);
	memcpy(sparse_hll, dense_hll, sizeof(*sparse_hll));
}

/*
 * Add hash to the sparsely represented HyperLogLog estimator.
 * The representation may be changed to dense after the call.
 */
static void
hll_sparse_add(struct hll *hll, uint64_t hash)
{
	if (hll_sparse_is_full(hll))
		hll_sparse_merge_list_with_buffer(hll);

	if (hll_sparse_is_full(hll)) {
		if (hll_sparse_can_grow(hll)) {
			hll_sparse_grow_after_merge(hll);
		} else {
			hll_sparse_to_dense(hll);
			hll_dense_add(hll, hash);
			return;
		}
	}

	uint32_t idx = hll_hash_register_idx(hash, HLL_SPARSE_PRECISION);
	uint32_t rank = hll_hash_rank(hash, HLL_SPARSE_PRECISION);
	pair_t new_pair = hll_sparse_new_pair(idx, rank);
	hll_sparse_buff_add(hll, new_pair);
}

/* Estimate the cardinality of the sparsely represented HyperLogLog. */
static uint64_t
hll_sparse_estimate(struct hll *hll)
{
	/*
	 * Since the number of pairs is low compared to linear counting
	 * estimation threshold linear counting is always used.
	 */
	hll_sparse_merge_list_with_buffer(hll);
	size_t n_counters = hll_n_registers(HLL_SPARSE_PRECISION);
	size_t n_pairs = hll_sparse_pairs_list_size(hll->data);
	return linear_counting(n_counters, n_counters - n_pairs);
}

static void
hll_create(struct hll *hll, uint8_t precision,
	   enum HLL_REPRESENTATION representation)
{
	if (representation == HLL_SPARSE &&
	    precision >= HLL_SPARSE_MIN_PRECISION) {
		hll_sparse_create(hll, precision);
	} else {
		hll_dense_create(hll, precision);
	}
}

struct hll *
hll_new(uint8_t precision, enum HLL_REPRESENTATION representation)
{
	assert(hll_is_supported_precision(precision));
	struct hll *hll = xcalloc(1, sizeof(*hll));
	hll_create(hll, precision, representation);
	return hll;
}

void
hll_add(struct hll *hll, uint64_t hash)
{
	switch (hll->representation) {
	case HLL_SPARSE:
		hll_sparse_add(hll, hash);
		break;
	case HLL_DENSE:
		hll_dense_add(hll, hash);
		break;
	default:
		unreachable();
		panic("Unreachable branch.");
	}
}

/* Check if it is possible to merge the dst estimator with the src. */
static int MAYBE_UNUSED
hll_is_mergable(const struct hll *dst, const struct hll *src)
{
	return dst->dprecision == src->dprecision;
}

/* Merge a dense estimator with a dense estimator. */
static void
hll_merge_dense_with_dense(struct hll *dst, const struct hll *src)
{
	const size_t n_registers = hll_n_registers(dst->dprecision);
	for (size_t idx = 0; idx < n_registers; ++idx) {
		uint8_t new_rank = hll_dense_register_rank(src, idx);
		hll_dense_add_to_register(dst, idx, new_rank);
	}
}

/* Merge a dense estimator with a sparse estimator. */
static void
hll_merge_dense_with_sparse(struct hll *dst, const struct hll *src)
{
	pair_t *list_beg = hll_sparse_pairs_list_begin(src->data);
	pair_t *list_end = hll_sparse_pairs_list_end(src->data);
	hll_dense_add_pairs(dst, list_beg, list_end);
	pair_t *buff_beg = hll_sparse_pairs_buffer_begin(src->data);
	pair_t *buff_end = hll_sparse_pairs_buffer_end(src->data);
	hll_dense_add_pairs(dst, buff_beg, buff_end);
}

int
hll_merge(struct hll *dst, const struct hll *src)
{
	if (!hll_is_mergable(dst, src))
		return -1;

	if (dst->representation == HLL_SPARSE)
		hll_sparse_to_dense(dst);

	switch (src->representation) {
	case HLL_SPARSE:
		hll_merge_dense_with_sparse(dst, src);
		return 0;
	case HLL_DENSE:
		hll_merge_dense_with_dense(dst, src);
		return 0;
	default:
		unreachable();
		panic("Unreachable branch.");
	}
}

uint64_t
hll_estimate(struct hll *hll)
{
	switch (hll->representation) {
	case HLL_SPARSE:
		return hll_sparse_estimate(hll);
	case HLL_DENSE:
		return hll_dense_estimate(hll);
	default:
		unreachable();
		panic("Unreachable branch.");
	}
}

/* Releaze resourses of the HyperLogLog estimator. */
static void
hll_destroy(struct hll *hll)
{
	free(hll->data);
}

void
hll_delete(struct hll *hll)
{
	hll_destroy(hll);
	free(hll);
}
