/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "hll.h"
#include "trivia/util.h"
#include "say.h"

#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <limits.h>
#include <math.h>
#include <assert.h>

/*
 * The implementation of the algorithm is mainly based on the article
 * HyperLogLog in Practice: Algorithmic Engineering of a State of The Art
 * Cardinality Estimation Algorithm (https://research.google/pubs/pub40671/).
 */

enum HLL_CONSTANTS {
	/*
	 * 6 bits are needed to store the number of
	 * trailing zeros of 64 bit hash.
	 */
	HLL_RANK_BITS = 6,

	/* The maximum value that can be stored in HLL_RANK_BITS bits. */
	HLL_RANK_MAX = (1 << HLL_RANK_BITS) - 1,

	/* Number of bits stored in a register bucket. */
	HLL_BUCKET_BITS = 24,
	/* Number of bytes stored in a register bucket. */
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

/**
 * Estimator that is used for the HyperLogLog algorithm.
 * The algorithm allows to estimate number of distinct items using fixed amount
 * of memory or even less. Memory requirements and estimation accuracy are
 * determined by the algorithm precision parameter.
 * For the dense representation the relative error is 1.04/sqrt(m) and the
 * memory capacity is m*6 bits where m is number of counters which
 * equals to 2^precision.
 * For the sparse representation the memory usage is proportional to the number
 * of distinct elements that has added until it reaches the memory usage of the
 * dense representation, and then it switches to the dense representation with
 * fixed memory requirements. The sparse representation has the best accuracy.
 */
struct hll {
	/** See the comment to HLL_REPRESENTATION enum. */
	enum HLL_REPRESENTATION representation;
	/**
	 * Interpretation of the data depends on the representation.
	 * For dense representation it's an array of registers of size
	 * 2^precision * 6 bits. Registers store the maximum added rank of set
	 * of hashes which last precision bits are equal to register index.
	 * For the sparse representation it's a sparsely represented
	 * HyperLogLog. Instead of registers the sparse representation works
	 * with pairs consisting of the rank and the index of the added hashes.
	 */
	uint8_t *data;
	/**
	 * Precision is equal to the number of bits that are interpreted as
	 * a register index. Available values are from HLL_MIN_PRECISION to
	 * HLL_MAX_PRECISION.
	 * The larger value leads to less estimation error but larger
	 * memory requirement (2^precision * 6 bits).
	 * This value is only used by the dense representation and for switching
	 * from the sparse representation.
	 */
	uint8_t dprecision;
	/**
	 * Cached value of the last estimation.
	 */
	double cached_estimation;
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
	if (likely(n < 64))
		return (UINT64_C(1) << n) - 1;
	return UINT64_C(-1);
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
	hll->cached_estimation = -1.0;
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
 * Return the number of trailing zeros bits in hash plus one.
 * Register bits are interpreted as ones.
 */
static uint8_t
hll_hash_rank(uint64_t hash, uint8_t precision)
{
	assert(hll_is_valid_precision(precision));
	hash |= hll_ones(precision) << (64 - precision);
	uint8_t zero_count = __builtin_ctzll(hash);
	uint8_t rank = zero_count + 1;
	assert(rank <= HLL_RANK_MAX);
	return rank;
}

/* Calculate the number of registers for this precision. */
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

/* Alpha constant that is used in the HyperLogLog estimation formula. */
static double
hll_alpha(uint8_t precision)
{
	assert(hll_is_valid_precision(precision));
	switch (precision) {
	case 4:
		return 0.673;
	case 5:
		return 0.697;
	case 6:
		return 0.709;
	default:
		return 0.7213 / (1.0 + 1.079 / hll_n_registers(precision));
	}
}

/* Estimate the number of distinct items using the LinearCounting algorithm. */
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
 * Dense representation is a classical representation: there is always
 * allocated 2^precision counters so it may be wasteful for small cardinalities.
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
 * Calculate the amount of memory required to store
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
 * Precomputed array of powers of 2 from -HLL_RANK_MAX to 0.
 * Using this array instead of the pow function speeds up the calculation
 * of the raw estimation by ~33%.
 */
static const double pow_2_minus[] = {
	1, 0.5, 0.25, 0.125, 0.0625, 0.03125, 0.015625, 0.0078125, 0.00390625,
	0.001953125, 0.0009765625, 0.00048828125, 0.000244140625,
	0.0001220703125, 6.103515625e-05, 3.0517578125e-05, 1.52587890625e-05,
	7.62939453125e-06, 3.814697265625e-06, 1.9073486328125e-06,
	9.5367431640625e-07, 4.76837158203125e-07, 2.384185791015625e-07,
	1.1920928955078125e-07, 5.960464477539063e-08, 2.9802322387695312e-08,
	1.4901161193847656e-08, 7.450580596923828e-09, 3.725290298461914e-09,
	1.862645149230957e-09, 9.313225746154785e-10, 4.656612873077393e-10,
	2.3283064365386963e-10, 1.1641532182693481e-10, 5.820766091346741e-11,
	2.9103830456733704e-11, 1.4551915228366852e-11, 7.275957614183426e-12,
	3.637978807091713e-12, 1.8189894035458565e-12, 9.094947017729282e-13,
	4.547473508864641e-13, 2.2737367544323206e-13, 1.1368683772161603e-13,
	5.684341886080802e-14, 2.842170943040401e-14, 1.4210854715202004e-14,
	7.105427357601002e-15, 3.552713678800501e-15, 1.7763568394002505e-15,
	8.881784197001252e-16, 4.440892098500626e-16, 2.220446049250313e-16,
	1.1102230246251565e-16, 5.551115123125783e-17, 2.7755575615628914e-17,
	1.3877787807814457e-17, 6.938893903907228e-18, 3.469446951953614e-18,
	1.734723475976807e-18, 8.673617379884035e-19, 4.336808689942018e-19,
	2.168404344971009e-19, 1.0842021724855044e-19,
};

static_assert(lengthof(pow_2_minus) == HLL_RANK_MAX + 1,
	      "Array must contain HLL_RANK_MAX + 1 powers.");

/*
 * Estimate the number of distinct hashes added to the densely represented
 * HyperLogLog using the estimation formula. Raw estimation can have a large
 * relative error for small counts.
 */
static double
hll_dense_raw_estimate(const struct hll *hll)
{
	double sum = 0;
	const size_t n_registers = hll_n_registers(hll->dprecision);
	for (size_t i = 0; i < n_registers; ++i)
		sum += pow_2_minus[hll_dense_register_rank(hll, i)];

	const double alpha = hll_alpha(hll->dprecision);
	return alpha * n_registers * n_registers / sum;
}

/**
 * Return the bias correction for the raw_estimation.
 */
double
hll_empirical_bias_correction(uint8_t precision, double raw_estimation);

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

/*
 * Return the threshold below which linear counting algorithm has a smaller
 * error than the HyperLogLog algorithm.
 */
uint64_t
hll_empirical_estimation_threshold(uint8_t precision);

/*
 * Estimate the number of distinct hashes added to the densely represented
 * HyperLogLog.
 */
static uint64_t
hll_dense_count_distinct(struct hll *hll)
{
	if (hll_is_valid_cache(hll)) {
		return hll->cached_estimation;
	}

	const uint8_t prec = hll->dprecision;
	const size_t n_registers = hll_n_registers(prec);

	size_t zero_count = hll_dense_count_zero_registers(hll);
	if (zero_count != 0) {
		double lc_estimation = linear_counting(n_registers, zero_count);
		uint64_t threshold = hll_empirical_estimation_threshold(prec);
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
 * representation switches to the dense representation if it starts to require
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
	 * less than 1 / 2 ^ (64 - 26) ~ 3.6e-12 (38 trailing zeros)
	 * so such assumption will not make a big mistake.
	 *
	 * The formula explanation:
	 * |101110101010010010010011101110101010010010010011001110111010| : hash
	 * |<-------idx(26)------->|<--------------same_bits----------->|
	 * |<---idx(18)--->|<#####>|<--------------same_bits----------->|
	 *
	 * #### - are not included in rank(26), but may be included in rank(18),
	 * but only if there are more than 64 - 26 trailing zeros, otherwise the
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
 * into 2 parts: a sorted list of pairs and a temporary buffer for recently
 * added pairs. The list starts right after the header and grows by increasing
 * indexes and the buffer starts at the end of the pairs array and grows by
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
	 * (including the header).
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

/* Allocate pairs for sparsely represented HyperLogLog. */
static uint8_t *
hll_sparse_pairs_new(uint32_t bsize)
{
	assert(bsize >= sizeof(struct pairs_header));
	uint8_t *pairs_data = xcalloc(bsize, 1);
	struct pairs_header *header = hll_sparse_pairs_header(pairs_data);
	header->bsize = bsize;
	header->list_idx = 0;
	header->buff_idx = hll_sparse_pairs_max_size(pairs_data);
	return pairs_data;
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
	hll->data = hll_sparse_pairs_new(HLL_SPARSE_INITIAL_BSIZE);
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
 * Sort the pairs buffer using the pairs comparator.
 * Sorting is needed for effective merging with the list.
 */
static void
hll_sparse_pairs_sort_buff(uint8_t *pairs_data)
{
	pair_t *buffer = hll_sparse_pairs_buffer_begin(pairs_data);
	size_t buff_size = hll_sparse_pairs_buffer_size(pairs_data);
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
 * Leave only pairs with unique indexes or with the highest rank for duplicate
 * indexes. Pairs are expected to be sorted.
 * Return the new number of pairs after merging.
 */
static uint32_t
pairs_merge_duplicate_indexes(pair_t *pairs, uint32_t n_pairs)
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
 * Merge the list with the buffer into the returned HyperLogLog.
 */
static uint8_t *
hll_sparse_pairs_merge_list_with_sorted_buffer(const uint8_t *pairs_data)
{
	pair_t *list = hll_sparse_pairs_list_begin(pairs_data);
	pair_t *list_end = hll_sparse_pairs_list_end(pairs_data);
	pair_t *buff = hll_sparse_pairs_buffer_begin(pairs_data);
	pair_t *buff_end = hll_sparse_pairs_buffer_end(pairs_data);
	assert(list_end <= buff);

	uint32_t bsize = hll_sparse_bsize(pairs_data);
	uint8_t *new_pairs_data = hll_sparse_pairs_new(bsize);
	pair_t *new_pairs = hll_sparse_pairs(new_pairs_data);
	uint32_t n_pairs = merge_sorted_pairs_arrays(list, list_end,
						     buff, buff_end,
						     new_pairs);
	n_pairs = pairs_merge_duplicate_indexes(new_pairs, n_pairs);
	hll_sparse_pairs_header(new_pairs_data)->list_idx = n_pairs;
	return new_pairs_data;
}

/*
 * Merge the list with the buffer.
 */
static void
hll_sparse_merge_list_with_buffer(struct hll *hll)
{
	if (hll_sparse_pairs_buffer_size(hll->data) == 0)
		return;

	hll_sparse_pairs_sort_buff(hll->data);
	uint8_t *merged_pairs_data =
		hll_sparse_pairs_merge_list_with_sorted_buffer(hll->data);

	free(hll->data);
	hll->data = merged_pairs_data;
}

/*
 * Convert a sparsely represented HyperLogLog to a densely represented
 * HyperLogLog. The sparsely represented HyperLogLog is freed after converting.
 */
static void
hll_sparse_to_dense(struct hll *hll)
{
	assert(hll->representation == HLL_SPARSE);
	uint8_t precision = hll->dprecision;
	struct hll tmp;
	struct hll *dense_hll = &tmp;
	hll_create(dense_hll, precision, HLL_DENSE);

	pair_t *list_beg = hll_sparse_pairs_list_begin(hll->data);
	pair_t *list_end = hll_sparse_pairs_list_end(hll->data);
	hll_dense_add_pairs(dense_hll, list_beg, list_end);
	pair_t *buff_beg = hll_sparse_pairs_buffer_begin(hll->data);
	pair_t *buff_end = hll_sparse_pairs_buffer_end(hll->data);
	hll_dense_add_pairs(dense_hll, buff_beg, buff_end);

	hll_destroy(hll);
	memcpy(hll, dense_hll, sizeof(*hll));
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

/*
 * Estimate the number of distinct hashes added to the sparsely
 * represented HyperLogLog.
 */
static uint64_t
hll_sparse_count_distinct(struct hll *hll)
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
hll_new(uint8_t precision)
{
	return hll_new_concrete(precision, HLL_SPARSE);
}

struct hll *
hll_new_concrete(uint8_t precision, enum HLL_REPRESENTATION representation)
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
hll_is_mergeable(const struct hll *dst, const struct hll *src)
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
	if (dst == src)
		return 0;

	if (!hll_is_mergeable(dst, src))
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
hll_count_distinct(struct hll *hll)
{
	switch (hll->representation) {
	case HLL_SPARSE:
		return hll_sparse_count_distinct(hll);
	case HLL_DENSE:
		return hll_dense_count_distinct(hll);
	default:
		unreachable();
		panic("Unreachable branch.");
	}
}

/* Release resources of the HyperLogLog estimator. */
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

/** Get the estimator precision parameter. */
uint8_t
hll_precision(struct hll *hll)
{
	return hll->representation == HLL_DENSE ? hll->dprecision :
						  HLL_SPARSE_PRECISION;
}

/*
 * ==========================================================================
 * Section with empirical based optimizations.
 *
 * If the algorithm changed, this data may become invalid.
 * If a new precision value added, it is necessary to find the corresponding
 * bias correction curve and the linear counting algorithm threshold
 * using scripts from tool/.
 * ==========================================================================
 */

enum {
	/*
	 * Multiply this constant with the number of counters of the HyperLogLog
	 * algorithm to get the cardinality above which there is no any bias.
	 * This constant was found approximately as a point on the bias graph,
	 * where the bias is zero.
	 */
	BIAS_RANGE = 5,
	/* The highest degree of the approximation polynomial. */
	APPROXIMATION_CURVE_DEGREE = 5,
};

/*
 * Array of coefficients of bias correction curves that is used to avoid the
 * bias of the raw estimation of the HyperLogLog algorithm.
 * The curves at 0 have a value of the order of m and then decreases linearly to
 * 0 before reaching point of 5m. (m is the number of counters)
 * To find this curves, I found biases for many cardinalities from 0 to 6m and
 * then I got the bias curves by approximating these biases.
 */
static const double bias_correction_curves[][APPROXIMATION_CURVE_DEGREE + 1] = {
/* precision 6 */
{
	-1.560253831322616e-11,
	4.2918991518443075e-08,
	-3.250051480954982e-05,
	0.010658687018936542,
	-1.6361474946615935,
	98.0999748892133,
},
/* precision 7 */
{
	-2.6348819804723807e-12,
	8.638406724535363e-09,
	-1.0520599302100548e-05,
	0.006128048918021137,
	-1.7594170030710843,
	205.2872174568368,
},
/* precision 8 */
{
	-2.5160746064504215e-13,
	1.4014951481090302e-09,
	-3.0709340331627186e-06,
	0.0033447668143924297,
	-1.8445455137150777,
	422.06152806585067,
},
/* precision 9 */
{
	-2.1248565963465066e-14,
	2.209328461903022e-10,
	-9.062985969933961e-07,
	0.0018599604101881286,
	-1.955620041226081,
	869.3141721067316,
},
/* precision 10 */
{
	-1.1738467691849587e-15,
	2.5234861825039117e-11,
	-2.1387208279704244e-07,
	0.0009023359586847869,
	-1.9335234021733647,
	1736.224269908643,
},
/* precision 11 */
{
	-7.776926793401511e-17,
	3.2640458828416577e-12,
	-5.428048779309705e-08,
	0.0004524759210201062,
	-1.9297041172484268,
	3466.8665413900717,
},
/* precision 12 */
{
	-5.263504634476788e-18,
	4.328854403341179e-13,
	-1.4134166124776926e-08,
	0.0002319981337483276,
	-1.956233468802009,
	6980.089443005847,
},
/* precision 13 */
{
	-3.0953932973940563e-19,
	5.153467644092651e-14,
	-3.4077463515510344e-09,
	0.00011324850437847658,
	-1.9298578807007847,
	13872.610979620697,
},
/* precision 14 */
{
	-1.9660372917435063e-20,
	6.556831384307935e-15,
	-8.663378641196496e-10,
	5.7384181647964165e-05,
	-1.9464220405203823,
	27868.19348200696,
},
/* precision 15 */
{
	-1.3172381163841186e-21,
	8.644553586371329e-16,
	-2.249580395078218e-10,
	2.9399282179207515e-05,
	-1.9729745088347135,
	56089.97266216575,
},
/* precision 16 */
{
	-8.116581067854268e-23,
	1.0696804865228364e-16,
	-5.586285006018088e-11,
	1.4640444825651528e-05,
	-1.9688351935818378,
	112081.55873324239,
},
/* precision 17 */
{
	-4.9858577224116555e-24,
	1.3175656218134596e-17,
	-1.3804847009520501e-11,
	7.261162372440564e-06,
	-1.9594659887911166,
	223653.9794755037,
},
/* precision 18 */
{
	-3.1412699911414233e-25,
	1.6579394491113172e-18,
	-3.468977319968413e-12,
	3.6435853608631795e-06,
	-1.9637079952913725,
	447784.39774089074,
},
};

static_assert(lengthof(bias_correction_curves) == HLL_N_PRECISIONS,
	      "Size of thresholds_data doesn't correspond to the hll "
	      "precision bounds.");

/* Get the threshold above which there is no need in the bias correction. */
static uint64_t
bias_correction_threshold(uint8_t precision)
{
	size_t n_registers = UINT64_C(1) << precision;
	return BIAS_RANGE * n_registers;
}

double
hll_empirical_bias_correction(uint8_t precision, double raw_estimation)
{
	assert(hll_is_supported_precision(precision));
	uint64_t threshold = bias_correction_threshold(precision);
	if (threshold < raw_estimation) {
		return 0;
	}
	double x1 = raw_estimation;
	double x2 = x1 * raw_estimation;
	double x3 = x2 * raw_estimation;
	double x4 = x3 * raw_estimation;
	double x5 = x4 * raw_estimation;
	int idx = precision - HLL_MIN_PRECISION;
	const double *coefs = bias_correction_curves[idx];
	return x5 * coefs[0] + x4 * coefs[1] + x3 * coefs[2] +
		x2 * coefs[3] + x1 * coefs[4] + coefs[5];
}

/*
 * Thresholds below which the linear counting algorithm should be used.
 * The linear counting algorithm is used for small cardinalities because it has
 * better accuracy compared to the HyperLogLog algorithm. In this thresholds the
 * accuracy of the linear counting algorithm is equal to the accuracy of the
 * HyperLogLog algorithm.
 * To find these thresholds, I calculated the errors of the LinearCounting
 * algorithm for many points from 0 to 4m. Then I approximated these points
 * and got an error curve. Finally, I got the threshold as the point at which
 * the value of the approximated curve is equal to the error of the HyperLogLog
 * algorithm.
 */
static const uint64_t linear_counting_thresholds[] = {
	/* precision 6 */
	111,
	/* precision 7 */
	234,
	/* precision 8 */
	473,
	/* precision 9 */
	939,
	/* precision 10 */
	1944,
	/* precision 11 */
	3814,
	/* precision 12 */
	7791,
	/* precision 13 */
	15875,
	/* precision 14 */
	31668,
	/* precision 15 */
	62355,
	/* precision 16 */
	124568,
	/* precision 17 */
	260089,
	/* precision 18 */
	516293,
};

static_assert(lengthof(linear_counting_thresholds) == HLL_N_PRECISIONS,
	      "Size of linear_counting_thresholds doesn't correspond to the "
	      "hll precision bounds.");

uint64_t
hll_empirical_estimation_threshold(uint8_t precision)
{
	assert(hll_is_supported_precision(precision));
	return linear_counting_thresholds[precision - HLL_MIN_PRECISION];
}
