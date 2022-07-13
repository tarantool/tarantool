#include "salad/hll.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stdarg.h>
#include <assert.h>

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

/*
 * Error measurement functions linearly divide the ranges by this number.
 * Increasing this value can critically increase the execution time.
 */
const size_t N_POINTS = 10;

/*
 * Number of randomly generated sets for every cardinality of the range
 * in error measurement functions.
 * Increasing this value can critically increase the execution time.
 */
const size_t SETS_PER_POINT = 15;

/*
 * Specify range that will be used to measure errors or dump the data
 * in the test for the dense representation.
 * The range starts from 0 and ends with RANGE * n_registers.
 */
enum RANGES {
	/*
	 * Range for general testing.
	 * The range of 10m is divided into 3 sections with different
	 * estimation approach, each of which must be tested:
	 * [0 - m] - LinearCounting algorithm;
	 * [1m - 5m] - HyperLogLog with bias correction;
	 * [5m - inf] - pure HyperLogLog algorithm;
	 * where m is the number of counters.
	 */
	GENERAL_RANGE = 8,
	/* Range that must be used to find linear counting thresholds. */
	LINEAR_COUNTING_RANGE = 3,
	/* Range that must be used to find bias correction curves. */
	BIAS_RANGE = 6,
};

/*
 * Range for measuring dense representation errors or dumping.
 * Use the constants from RANGES enum.
 * The range starts from 0 and ends with RANGE * n_registers.
 */
const size_t DENSE_REPR_MEASURING_RANGE = GENERAL_RANGE;

/*
 * Range for measuring sparse representation errors.
 * The sparse representation uses 4-byte pairs instead of 6-bit counters and
 * can reach the same amount of memory as the dense representation,
 * so the maximal number of pairs is (32/6 ~) 6 times less than number of
 * registers.
 * The range starts from 0 and ends with RANGE * n_registers.
 */
const double SPARSE_REPR_MEASURING_RANGE = 1.0 / 6.0;

/*
 * Files to dump the data that is used to measure errors.
 * Use NULL to avoid dumping.
 */
const char * const DENSE_OUTPUT_FILE_NAME = NULL;
const char * const SPARSE_OUTPUT_FILE_NAME = NULL;

/* Columns format of dumped data. */
const char *columns_format = "prec, card, avg_est, std_err";

double
average_of(double *arr, size_t n)
{
	double sum = 0;
	for (size_t i = 0; i < n; ++i)
		sum += arr[i];
	assert(n > 0);
	return sum / n;
}

double
max_of(double *arr, size_t n)
{
	double max = arr[0];
	for (size_t i = 0; i < n; ++i)
		if (max < arr[i])
			max = arr[i];
	return max;
}

double
dispersion_of(double *arr, double val, size_t n)
{
	double sqr_sum = 0;
	for (size_t i = 0; i < n; ++i) {
		sqr_sum += (val - arr[i]) * (val - arr[i]);
	}
	assert(n > 1);
	return sqrt(sqr_sum / (n - 1));
}

/* Get a random 64-bit value. */
uint64_t
rand64(void)
{
	uint64_t r1 = rand();
	uint64_t r2 = rand();
	uint64_t r3 = rand();
	uint64_t r4 = rand();
	return r1 ^ (r2 << 16) ^ (r3 << 32) ^ (r4 << 48);
}

/* Print to a file if it is not NULL. */
static inline void
fprintf_if_not_null(FILE *file, const char *format, ...)
{
	if (file != NULL && format != NULL) {
		va_list args;
		va_start(args, format);
		vfprintf(file, format, args);
		va_end(args);
	}
}

/* Return an opened file named file_name if it's not NULL, or NULL otherwise. */
static inline FILE *
fopen_if_not_null(const char *file_name, const char *mode)
{
	if (file_name != NULL && mode != NULL) {
		FILE *file = fopen(file_name, mode);
		assert(file != NULL && "Can't open a file.");
		return file;
	}
	return NULL;
}

static inline void
fclose_if_not_null(FILE *file)
{
	if (file != NULL)
		fclose(file);
}

/* Get the number of counters used by the algorithm for this precision. */
static inline uint64_t
n_registers(int prec)
{
	return UINT64_C(1) << prec;
}

/*
 * The error measure occurs as follows:
 * The range [0, max_card] is linearly divided by n_points.
 * For every cardinality from the range the error is calculated by using
 * estimations of sets_per_point randomly generated sets.
 * The resulting error is the average error of all cardinalities.
 * The error and intermidiate data will be dumped in the output file if it
 * is not NULL.
 */
double
measure_hll_estimation_error(int prec, enum HLL_REPRESENTATION repr,
			     size_t max_card, size_t n_points,
			     size_t sets_per_point, FILE *output)
{
	double max_err_sum = 0;
	double std_err_sum = 0;
	const double card_step = 1.0 * max_card / n_points;
	for (size_t n = 0; n < n_points; ++n) {
		size_t card = card_step * n;
		double errors[sets_per_point];
		double estimations[sets_per_point];

		for (size_t i = 0; i < sets_per_point; ++i) {
			struct hll *hll = hll_new_concrete(prec, repr);

			for (size_t j = 0; j < card; ++j)
				hll_add(hll, rand64());

			estimations[i] = hll_count_distinct(hll);
			errors[i] = fabs(estimations[i] - card);

			hll_delete(hll);
		}

		double avg_est = average_of(estimations, sets_per_point);
		double std_err =
			dispersion_of(estimations, card, sets_per_point) /
				(card + 1);
		std_err_sum += std_err;

		fprintf_if_not_null(output, "%2d, %12zu, %12.2f, %12lg\n",
				    prec, card, avg_est, std_err);
	}

	double avg_std_err = std_err_sum / n_points;

	return avg_std_err;
}

double
measure_sparse_hll_estimation_error(int prec, FILE *output)
{
	size_t max_card = SPARSE_REPR_MEASURING_RANGE * n_registers(prec);
	return measure_hll_estimation_error(prec, HLL_SPARSE, max_card,
					    N_POINTS, SETS_PER_POINT, output);
}

double
measure_dense_hll_estimation_error(int prec, FILE *output)
{
	size_t max_card = DENSE_REPR_MEASURING_RANGE * n_registers(prec);
	return measure_hll_estimation_error(prec, HLL_DENSE, max_card,
					    N_POINTS, SETS_PER_POINT, output);
}

void
test_basic_functionality(void)
{
	header();
	plan(13);

	int prec = 14;
	struct hll *hll = hll_new(prec);
	is(hll_count_distinct(hll), 0, "Initial estimation is zero.");
	is(hll_precision(hll), HLL_SPARSE_PRECISION, "Right precision.");

	uint64_t h1 = rand64();
	uint64_t h2 = rand64();
	hll_add(hll, h1);
	is(hll_count_distinct(hll), 1, "Added one hash.");
	hll_add(hll, h1);
	is(hll_count_distinct(hll), 1, "Still only one hash.");

	hll_add(hll, h2);
	is(hll_count_distinct(hll), 2, "Added another hash.");
	hll_add(hll, h1);
	is(hll_count_distinct(hll), 2, "Still only two hashes.");
	hll_add(hll, h2);
	is(hll_count_distinct(hll), 2, "Still only two hashes.");

	struct hll *another_hll = hll_new(prec - 1);
	int rc = hll_merge(hll, another_hll);
	isnt(rc, 0, "Different precisions");
	is(hll_count_distinct(hll), 2, "Still only two hashes.");
	hll_delete(another_hll);

	another_hll = hll_new(prec);
	hll_add(another_hll, h1 ^ h2);
	is(hll_count_distinct(another_hll), 1, "Added one hash.");
	rc = hll_merge(hll, another_hll);
	is(rc, 0, "No error.");
	is(hll_count_distinct(hll), 3, "Added another hash.");
	is(hll_count_distinct(another_hll), 1, "Still only one hash.");

	hll_delete(another_hll);
	hll_delete(hll);

	check_plan();
	footer();
}

/*
 * This test can dump the data that is used to measure
 * the estimation error. These data can be used for further
 * analysis and empirical based improvements of the algorithm.
 */
void
test_dense_hyperloglog_error(void)
{
	header();
	plan(HLL_N_PRECISIONS);

	double errors[HLL_MAX_PRECISION + 1];

	FILE *output = fopen_if_not_null(DENSE_OUTPUT_FILE_NAME, "w");
	fprintf_if_not_null(output, "%s\n", columns_format);

	for (int prec = HLL_MIN_PRECISION;
	     prec <= HLL_MAX_PRECISION; ++prec) {
		errors[prec] = measure_dense_hll_estimation_error(prec, output);
		/*
		 * The error of HyperLogLog is close to 1/sqrt(n_counters),
		 * but for small cardinalities LinearCounting is used because
		 * it has better accuracy, so the resulting error must be
		 * smaller than the HyperLogLog theoretical error.
		 */
		ok(errors[prec] < hll_error(prec),
		   "The actual error doesn't exceed the expected value.");
	}

	for (int prec = HLL_MIN_PRECISION;
	     prec <= HLL_MAX_PRECISION; ++prec) {
		fprintf_if_not_null(output,
				    "prec:%d, std_err:%lg, exp_err: %lg\n",
				    prec, errors[prec], hll_error(prec));
	}

	fclose_if_not_null(output);

	check_plan();
	footer();
}

void
test_sparse_hyperloglog_error(void)
{
	header();
	/*
	 * Since the precision parameter defines only the maximal size of the
	 * sparse representation there is no need in tests for all available
	 * precision values.
	 */
	plan(1);

	FILE *output = fopen_if_not_null(SPARSE_OUTPUT_FILE_NAME, "w");
	fprintf_if_not_null(output, "%s\n", columns_format);

	int dprec = HLL_MAX_PRECISION;
	double error = measure_sparse_hll_estimation_error(dprec, output);
	/*
	 * Since sparse representation uses the LinearCounting algorithm the
	 * error must be less than the error of the HyperLogLog algorithm.
	 */
	ok(error < hll_error(HLL_SPARSE_PRECISION),
	   "The actual error doesn't exceed the expected value.");

	fprintf_if_not_null(output, "std_err:%lg, exp_err: %lg\n",
			    error, hll_error(HLL_SPARSE_PRECISION));
	fclose_if_not_null(output);

	check_plan();
	footer();
}

void
test_sparse_to_dense_conversion(void)
{
	header();
	plan(HLL_N_PRECISIONS);

	for (int prec = HLL_MIN_PRECISION;
	     prec <= HLL_MAX_PRECISION; ++prec) {
		struct hll *sparse_hll = hll_new_concrete(prec, HLL_SPARSE);
		struct hll *dense_hll = hll_new_concrete(prec, HLL_DENSE);

		/*
		 * The sparse representation can't store more items than a
		 * number of counters in the dense representation, because each
		 * item in the sparse representation requires more amount of
		 * memory than the same item in the dense representation.
		 */
		uint64_t max_card = n_registers(prec);
		for (uint64_t i = 0; i < max_card; ++i) {
			uint64_t h = rand64();
			/* Double add must not affect the estimation. */
			hll_add(sparse_hll, h);
			hll_add(sparse_hll, h);
			hll_add(dense_hll, h);
		}

		uint64_t sparse_est = hll_count_distinct(sparse_hll);
		uint64_t dense_est = hll_count_distinct(dense_hll);

		ok(sparse_est == dense_est,
		   "Converted estimator is equivalent to the reference one.");

		hll_delete(sparse_hll);
		hll_delete(dense_hll);
	}

	check_plan();
	footer();
}

void
test_merge(void)
{
	header();
	const size_t card_steps = 8;
	plan(HLL_N_PRECISIONS * card_steps);
	for (int prec = HLL_MIN_PRECISION;
	     prec <= HLL_MAX_PRECISION; ++prec) {
		/*
		 * The range of 1m is divided into 3 sections with different
		 * merging types, each of which must be tested:
		 * [0 - m/6] - merge sparse with sparse.
		 * [m/6 - m/2] - merge dense with sparse.
		 * [m/2 - m] - merge dense with dense.
		 */
		const size_t max_card = n_registers(prec);
		const size_t card_step = max_card / card_steps;
		for (size_t card = 0; card < max_card; card += card_step) {
			struct hll *ref_hll = hll_new_concrete(prec, HLL_DENSE);
			struct hll *hll_1 = hll_new_concrete(prec, HLL_SPARSE);
			struct hll *hll_2 = hll_new_concrete(prec, HLL_SPARSE);

			for (size_t i = 0; i < card; ++i) {
				uint64_t h = rand64();
				hll_add(ref_hll, h);
				if (i % 2 == 0) {
					hll_add(hll_1, h);
				} else if (i % 3 == 0) {
					hll_add(hll_2, h);
				} else {
					hll_add(hll_1, h);
					hll_add(hll_2, h);
				}
			}

			hll_merge(hll_1, hll_2);
			size_t ref_est = hll_count_distinct(ref_hll);
			size_t merged_est = hll_count_distinct(hll_1);
			ok(ref_est == merged_est,
			   "Merged estimator is equivalent to "
			   "the reference one.");

			hll_delete(ref_hll);
			hll_delete(hll_1);
			hll_delete(hll_2);
		}
	}
	check_plan();
	footer();
}

int
main(int argc, char *argv[])
{
	header();
	plan(5);

	test_basic_functionality();
	test_dense_hyperloglog_error();
	test_sparse_hyperloglog_error();
	test_sparse_to_dense_conversion();
	test_merge();

	footer();
	return check_plan();
}
