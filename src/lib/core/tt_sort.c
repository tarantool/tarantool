/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "tt_sort.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clock.h"
#include "diag.h"
#include "fiber.h"
#include "qsort_arg.h"
#include "say.h"
#include "trivia/util.h"

/**
 * If size of a data to be sorted is less than NOSPAWN_SIZE_THESHOLD then
 * sorting will be done in calling thread without yielding. This helps to start
 * application faster if there is no much data in database (test cases in
 * particular). Otherwise sorting will be done in threads.
 */
#define NOSPAWN_SIZE_THESHOLD 1024

/** Sample sort algorithm data. */
struct sort_data {
	/** The data being sorted. */
	void *data;
	/** Number of elements in data. */
	size_t elem_count;
	/** Size of data element. */
	size_t elem_size;
	/** Function for comparing two elements. */
	tt_sort_compare_f cmp;
	/** Extra argument for `cmp` function. */
	void *cmp_arg;
	/**
	 * Number of threads to run sort on. It is equal to number of
	 * buckets we divide the data to.
	 */
	int thread_count;
	/**
	 * Array of elements that are used as boundaries between buckets.
	 * Array size is `thread_count - 1`, size of element is `elem_size`.
	 */
	void *splitters;
	/**
	 * Map of element to bucket. `elem_bucket[index]` is bucket for
	 * element data[index]`.
	 */
	unsigned char *elem_bucket;
	/**
	 * Extra space which is used to partition data into buckets. The
	 * size of space is same as `data` size and each element is of
	 * `elem_size` just as in `data`.
	 */
	void *buffer;
};

/** Data for a single sample sort worker thread. */
struct sort_worker {
	/** A reference to data shared between threads. */
	struct sort_data *sort;
	/** The worker cord. */
	struct cord cord;
	/** Begin index of data part processed by this thread. */
	size_t begin;
	/** End index of data part processed by this thread. */
	size_t end;
	/** Whether this thread data part is presorted. */
	bool presorted;
	/**
	 * Histogram of how much elements are placed in each bucket on
	 * partitioning. Array size is `thread_count`.
	 */
	size_t *bucket_hist;
	/**
	 * Offsets from the beginning of extra space at which this thread
	 * write elements for each bucket when partitioning. Array size
	 * is `thread_count`.
	 */
	size_t *bucket_offs;
	/** The thread bucket begin index in `buffer` on bucket sort phase. */
	size_t bucket_begin;
	/** This thread bucket size on bucket sort phase. */
	size_t bucket_size;
};

/**
 * Find bucket for element using binary search among sorted in ascending
 * order splitters.
 *
 * Return index of the bucket for the element.
 */
static int
find_bucket(struct sort_data *sort, void *elem)
{
	/*
	 * Bucket count is `thread_count`, thus bucket boundraries (splitters)
	 * count is `thread_count - 1` omitting most left and most right
	 * boundaries. Let's place most left and most right boundaries at
	 * imaginary indexes `-1` and `size of splitters` respectively.
	 */
	int b = -1;
	int e = sort->thread_count - 1;

	do {
		int m = (b + e) / 2;
		assert(m >= 0 && m < sort->thread_count - 1);
		if (sort->cmp(elem, sort->splitters + m * sort->elem_size,
			      sort->cmp_arg) < 0)
			e = m;
		else
			b = m;
	} while (e - b > 1);
	return b + 1;
}

/**
 * Calculate element to bucket map for data part assigned to a thread.
 * Additionally calculate bucket histogramm - how much elements are placed
 * in each bucket.
 */
static int
calc_elem_bucket(va_list ap)
{
	struct sort_worker *worker = va_arg(ap, typeof(worker));
	struct sort_data *sort = worker->sort;

	void *pos = sort->data + worker->begin * sort->elem_size;
	for (size_t i = worker->begin; i < worker->end; i++) {
		int b = find_bucket(sort, pos);
		assert(b >= 0 && b < sort->thread_count);
		sort->elem_bucket[i] = b;
		worker->bucket_hist[b]++;
		pos += sort->elem_size;
	}

	return 0;
}

/**
 * Distribute data part assigned to a thread to buckets. Each bucket
 * has a designated place for this thread.
 */
static int
split_to_buckets(va_list ap)
{
	struct sort_worker *worker = va_arg(ap, typeof(worker));
	struct sort_data *sort = worker->sort;

	void *pos = sort->data + worker->begin * sort->elem_size;
	for (size_t i = worker->begin; i < worker->end; i++) {
		int b = sort->elem_bucket[i];
		memcpy(sort->buffer + worker->bucket_offs[b], pos,
		       sort->elem_size);
		worker->bucket_offs[b] += sort->elem_size;
		pos += sort->elem_size;
	}

	return 0;
}

/**
 * Sort bucket assigned to a thread and copy sorted data back to the original
 * array.
 */
static int
sort_bucket(va_list ap)
{
	struct sort_worker *worker = va_arg(ap, typeof(worker));
	struct sort_data *sort = worker->sort;

	/* Sort this worker bucket. */
	qsort_arg_st(sort->buffer + worker->bucket_begin * sort->elem_size,
		     worker->bucket_size, sort->elem_size,
		     sort->cmp, sort->cmp_arg);

	/* Move sorted data back from temporary space. */
	memcpy(sort->data + worker->bucket_begin * sort->elem_size,
	       sort->buffer + worker->bucket_begin * sort->elem_size,
	       worker->bucket_size * sort->elem_size);

	return 0;
}

/**
 * Run function in several threads. Yields while waiting threads to
 * finish.
 *
 * Arguments:
 *  func         - a function to run in threads
 *  workers      - array of function arguments. An element of this array will
 *                 be passed to as an argument to the function `func` for each
 *                 thread. So the array should have `thread_count` elements
 *  thread_count - number of threads
 */
static void
sort_run_mt(fiber_func func, struct sort_worker *workers, int thread_count)
{
	for (int i = 0; i < thread_count; i++) {
		char name[FIBER_NAME_MAX];

		snprintf(name, sizeof(name), "sort.worker.%d", i);
		if (cord_costart(&workers[i].cord, name, func,
				 &workers[i]) != 0) {
			diag_log();
			panic("cord_start failed");
		}
	}

	for (int i = 0; i < thread_count; i++) {
		if (cord_cojoin(&workers[i].cord) != 0) {
			diag_log();
			panic("cord_cojoin failed");
		}
	}
}

/**
 * As we first split data to buckets and then sort each bucket in single
 * thread the algorithm performance critically depends on even distribution
 * data among buckets. According to estimation given in [1] with oversample
 * factor of 100*log2(elem_count) the probability that no bucket size deviates
 * from even distribution more than 10% is larger than 1 - 1/elem_count.
 *
 * We also do not use random sampling as periodic sample should also work
 * most of the time.
 *
 * [1] https://en.wikipedia.org/wiki/Samplesort
 */
static void
find_splitters(struct sort_data *sort)
{
	int log2_n = 0;
	size_t n = sort->elem_count;

	/* Calculate log2(elem_count). */
	while (n > 1) {
		n >>= 1;
		log2_n++;
	}

	/* Take samples with oversampling. */
	int oversample = 100 * log2_n;
	int samples_num = sort->thread_count * oversample - 1;
	void *samples = xmalloc(samples_num * sort->elem_size);
	size_t sample_step = sort->elem_count / samples_num;
	for (int i = 0; i < samples_num; i++)
		memcpy(samples + i * sort->elem_size,
		       sort->data + i * sample_step * sort->elem_size,
		       sort->elem_size);

	qsort_arg_st(samples, samples_num, sort->elem_size, sort->cmp,
		     sort->cmp_arg);

	/* Take splitters from samples. */
	for (int i = 0; i < sort->thread_count - 1; i++) {
		size_t si = oversample - 1 + i * oversample;
		memcpy(sort->splitters + i * sort->elem_size,
		       samples + si * sort->elem_size, sort->elem_size);
	}

	free(samples);
}

/** Check whether data part assigned to a thread is presorted. */
static int
check_presorted(va_list ap)
{
	struct sort_worker *worker = va_arg(ap, typeof(worker));
	struct sort_data *sort = worker->sort;
	worker->presorted = true;

	void *pos = sort->data + worker->begin * sort->elem_size;
	void *limit = sort->data + (worker->end - 1) * sort->elem_size;
	for (; pos < limit; pos += sort->elem_size) {
		if (sort->cmp(pos, pos + sort->elem_size, sort->cmp_arg) > 0) {
			worker->presorted = false;
			break;
		}
	}

	return 0;
}

/** Sort all the data. */
static int
sort_all(va_list ap)
{
	struct sort_data *sort = va_arg(ap, typeof(sort));

	qsort_arg_st(sort->data, sort->elem_count, sort->elem_size, sort->cmp,
		     sort->cmp_arg);

	return 0;
}

/** Sort all the data in a new thread. Yields while waiting. */
static void
sort_single_thread(struct sort_data *sort)
{
	struct cord cord;
	if (cord_costart(&cord, "sort.worker.0", sort_all, sort) != 0) {
		diag_log();
		panic("cord_start failed");
	}
	if (cord_cojoin(&cord) != 0) {
		diag_log();
		panic("cord_cojoin failed");
	}
}

void
tt_sort(void *data, size_t elem_count, size_t elem_size,
	tt_sort_compare_f cmp, void *cmp_arg, int thread_count)
{
	struct sort_data sort;
	double time_start, time_finish;

	/*
	 * The algorithm idea is to split the data into buckets, each element
	 * in bucket `i` is greater than each element in bucket `i - 1`, and
	 * then sort the buckets. As a result we will sort original array.
	 *
	 * So the algo outline is next:
	 *
	 * 1. Find buckets boundaries (splitters).
	 * 2. For each element in data find to what bucket is belongs to.
	 * 3. Copy elements to their buckets.
	 * 4. Sort buckets using sequentional qsort_arg and then copy buckets
	 *    back to the original array.
	 *
	 * Steps 2, 3 and 4 are run in parallel on `thread_count` threads. Step
	 * 1 does not have high computation cost and is run single thread.
	 *
	 * Additionally we check if data is already sorted before applying main
	 * algo.
	 *
	 * See also:
	 * [1] https://en.wikipedia.org/wiki/Samplesort
	 * [2] Super Scalar Sample Sort, Peter Sanders and Sebastian Winkel
	 *
	 * Although this implementation does not use superscalar approach to
	 * map elements to buckets as in [2] and use usual binary search.
	 */

	say_verbose("start sort, data size: %zu, elem size: %zu, threads: %d",
		    elem_count, elem_size, thread_count);
	if (elem_count < NOSPAWN_SIZE_THESHOLD) {
		say_verbose("data size is less than threshold %d,"
			    " sort in caller thread", NOSPAWN_SIZE_THESHOLD);
		qsort_arg_st(data, elem_count, elem_size, cmp, cmp_arg);
		return;
	}

	/*
	 * Upper limit is because element to bucket map has unsigned char
	 * storage per element.
	 */
	assert(thread_count > 0 && thread_count <= TT_SORT_THREADS_MAX);

	sort.data = data;
	sort.elem_count = elem_count;
	sort.elem_size = elem_size;
	sort.cmp = cmp;
	sort.cmp_arg = cmp_arg;
	sort.thread_count = thread_count;

	if (thread_count == 1) {
		sort_single_thread(&sort);
		say_verbose("sorting thread number is 1, fallback to qsort");
		return;
	}

	sort.elem_bucket = xmalloc(elem_count);
	sort.buffer = xmalloc(elem_count * elem_size);
	sort.splitters = xmalloc((thread_count - 1) * elem_size);

	size_t part_size = elem_count / thread_count;
	struct sort_worker *workers = xmalloc(thread_count * sizeof(*workers));
	bool presorted = true;
	/* Required for presorted check on part borders. */
	assert(part_size > 0);
	for (int i = 0; i < thread_count; i++) {
		struct sort_worker *worker = &workers[i];

		worker->sort = &sort;
		worker->begin = i * part_size;
		/*
		 * Each thread takes equal share of data except for last
		 * thread which takes extra `elem_count % thread_count` elements
		 * if `elem_count` in not multiple of `thread_count`.
		 */
		if (i == thread_count - 1)
			worker->end = elem_count;
		else
			worker->end = worker->begin + part_size;

		worker->bucket_hist = xcalloc(thread_count,
					      sizeof(*worker->bucket_hist));
		worker->bucket_offs = xmalloc(thread_count *
					      sizeof(*worker->bucket_offs));

		if (presorted && i < thread_count - 1 &&
		    cmp(data + (worker->end - 1) * elem_size,
			data + worker->end * elem_size, cmp_arg) > 0)
			presorted = false;
	}

	if (presorted) {
		sort_run_mt(check_presorted, workers, thread_count);
		for (int i = 0; i < thread_count; i++) {
			if (!workers[i].presorted) {
				presorted = false;
				break;
			}
		}
		if (presorted) {
			say_verbose("data is presorted");
			goto cleanup;
		}
	}

	/* Step 1. Find buckets boundaries (splitters). */
	find_splitters(&sort);

	/* Step 2. For each element in data find to what bucket is belongs. */
	time_start = clock_monotonic();
	sort_run_mt(calc_elem_bucket, workers, thread_count);
	time_finish = clock_monotonic();
	say_verbose("calculating elements buckets, time spent: %.3f sec",
		    time_finish - time_start);

	/* Step 3. Copy elements to their buckets. */
	time_start = clock_monotonic();
	size_t offset = 0;
	for (int i = 0; i < thread_count; i++) {
		for (int j = 0; j < thread_count; j++) {
			workers[j].bucket_offs[i] = offset;
			offset += workers[j].bucket_hist[i] * elem_size;
		}
	}
	sort_run_mt(split_to_buckets, workers, thread_count);
	time_finish = clock_monotonic();
	say_verbose("splitting to buckets, time spent: %.3f sec",
		    time_finish - time_start);

	/*
	 * Step 4. Sort buckets using sequentional qsort_arg and then copy
	 * buckets back to the original array.
	 */
	time_start = clock_monotonic();
	size_t bucket_begin = 0;
	for (int i = 0; i < thread_count; i++) {
		struct sort_worker *worker = &workers[i];
		size_t bucket_size = 0;

		for (int j = 0; j < thread_count; j++)
			bucket_size += workers[j].bucket_hist[i];

		worker->bucket_begin = bucket_begin;
		worker->bucket_size = bucket_size;
		bucket_begin += bucket_size;

		say_verbose("bucket %d, size %f", i,
			    (double)worker->bucket_size / elem_count);
	}
	sort_run_mt(sort_bucket, workers, thread_count);
	time_finish = clock_monotonic();
	say_verbose("sorting buckets, time spent: %.3f sec",
		    time_finish - time_start);

cleanup:
	for (int i = 0; i < thread_count; i++) {
		struct sort_worker *worker = &workers[i];

		free(worker->bucket_hist);
		free(worker->bucket_offs);
	}
	free(workers);
	free(sort.elem_bucket);
	free(sort.buffer);
	free(sort.splitters);
}
