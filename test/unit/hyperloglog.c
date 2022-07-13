#include "salad/hll.h"
#include "salad/hll_empirical.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

/*
 * measure_dense_hll_estimation_error linearly divides the range
 * from 0 to max_card by this number of points.
 * Increasing this value can critically increase the execution time.
 */
const size_t N_POINTS = 20;

/*
 * Number of randomly generated sets for every cardinality of the range
 * in measure_dense_hll_estimation_error.
 * Increasing this value can critically increase the execution time.
 */
const size_t SETS_PER_POINT = 20;

/*
 * File to dump the data that is used to measure the errors.
 * Use NULL to avoid dumping.
 */
char *OUTPUT_FILE_NAME = NULL;

double
average_sum_of(double *arr, size_t n)
{
	double sum = 0;
	for (size_t i = 0; i < n; ++i)
		sum += arr[i];
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
	assert(n > 1);
	double sqr_sum = 0;
	for (size_t i = 0; i < n; ++i) {
		sqr_sum += (val - arr[i]) * (val - arr[i]);
	}
	return sqrt(sqr_sum / (n - 1));
}

struct est_errors {
	/* Average standard error. */
	double std_err;
	/* Average max error. */
	double max_err;
};

/* Simple polynimal hash algorithm. */
uint64_t
phash(uint64_t val)
{
	unsigned char *bytes = (void *)&val;
	uint64_t p = 29791;
	uint64_t pi = p;
	uint64_t hash = 0;
	for (size_t i = 0; i < sizeof(val); ++i, pi *= p) {
		hash += pi * bytes[i];
	}
	return hash;
}

/* Double hashing helps to improve hash values distibution. */
uint64_t
hash(uint64_t val)
{
	return phash(phash(val));
}

uint64_t
big_rand(void)
{
	/*
	 * C standard rand() genarates random numbers in the range from 0 to
	 * RAND_MAX. RAND_MAX is at least 32767. This function helps to reduce
	 * the number of repeats if RAND_MAX is small.
	 */
#if RAND_MAX < (UINT64_C(1) << 16)
	uint64_t r1 = rand();
	uint64_t r2 = rand();
	uint64_t r3 = rand();
	uint64_t r4 = rand();
	return r1 * r2 * r3 * r4;
#elif RAND_MAX < (UINT64_C(1) << 32)
	uint64_t r1 = rand();
	uint64_t r2 = rand();
	uint64_t r3 = rand();
	return r1 * r2 * r3;
#else
	uint64_t r1 = rand();
	uint64_t r2 = rand();
	return r1 * r2;
#endif
}

#define MAYBE_PRINT(file, ...)			\
do {						\
	if ((file) != NULL) {			\
		fprintf((file), __VA_ARGS__);	\
	}					\
} while (0)

/* Get the number of counters used by the algorithm for this precision. */
static inline uint64_t
n_registers(int prec)
{
	return UINT64_C(1) << prec;
}

/*
 * The error measure occurs as follows:
 * The range [0, max_card] is linearly divided by n_points.
 * For every cardinality from the range sets_per_point HyperLogLog is created
 * and estimates the cardinality of randomly generated set of this cardinality.
 * Using the estimations, the error for this cardinality is calculated.
 * The error and imtermidiate data can be dumped in the output file.
 * The resulting error is the average error of all cardinalities.
 */
void
measure_hll_estimation_error(int prec, enum HLL_REPRESENTATION representation,
			     size_t max_card,
			     size_t n_points, size_t sets_per_point,
			     struct est_errors *res, FILE *output)
{
	double max_err_sum = 0;
	double std_err_sum = 0;
	const double card_step = 1.f * max_card / n_points;
	for (size_t n = 0; n < n_points; ++n) {
		size_t card = card_step * n;
		double error[sets_per_point];
		double est[sets_per_point];

		for (size_t i = 0; i < sets_per_point; ++i) {
			struct hll *hll = hll_new(prec, representation);

			for (size_t j = 0; j < card; ++j) {
				uint64_t val = big_rand();
				hll_add(hll, hash(val));
			}
			est[i] = hll_estimate(hll);
			error[i] = fabs(est[i] - card);

			hll_delete(hll);
		}

		double max_err = max_of(error, sets_per_point) / (card + 1);
		max_err_sum += max_err;

		double avg_est = average_sum_of(est, sets_per_point);
		double std_err = dispersion_of(est, card, sets_per_point) /
				(card + 1);
		std_err_sum += std_err;

		MAYBE_PRINT(output,
			    "%2d, %12zu, %12.2f, %12lg, %12lg\n",
			    prec, card, avg_est, std_err, max_err);
	}

	double avg_std_err = std_err_sum / n_points;
	double avg_max_err = max_err_sum / n_points;

	res->std_err = avg_std_err;
	res->max_err = avg_max_err;
}

void
measure_sparse_hll_estimation_error(int prec, size_t max_card,
				    size_t n_points, size_t sets_per_point,
				    struct est_errors *res, FILE *output)
{
	measure_hll_estimation_error(prec, HLL_SPARSE, max_card,
				     n_points, sets_per_point, res, output);
}

void
measure_dense_hll_estimation_error(int prec, size_t max_card,
				   size_t n_points, size_t sets_per_point,
				   struct est_errors *res, FILE *output)
{
	measure_hll_estimation_error(prec, HLL_DENSE, max_card,
				     n_points, sets_per_point, res, output);
}

void
test_basic_functionality(void)
{
	header();
	plan(13);

	int prec = 14;
	struct hll *hll = hll_new(prec, HLL_DENSE);
	is(hll_estimate(hll), 0, "Initial estimation is zero.");
	is(hll_precision(hll), prec, "Right precision.");

	uint64_t h1 = hash(big_rand());
	uint64_t h2 = hash(big_rand());
	hll_add(hll, h1);
	is(hll_estimate(hll), 1, "Added one hash.");
	hll_add(hll, h1);
	is(hll_estimate(hll), 1, "Still only one hash.");

	hll_add(hll, h2);
	is(hll_estimate(hll), 2, "Added another hash.");
	hll_add(hll, h1);
	is(hll_estimate(hll), 2, "Still only two hashes.");
	hll_add(hll, h2);
	is(hll_estimate(hll), 2, "Still only two hashes.");

	struct hll *another_hll = hll_new(6, HLL_DENSE);
	int rc = hll_merge(hll, another_hll);
	isnt(rc, 0, "Different precisions");
	is(hll_estimate(hll), 2, "Still only two hashes.");
	hll_delete(another_hll);

	another_hll = hll_new(prec, HLL_DENSE);
	hll_add(another_hll, h1 ^ h2);
	is(hll_estimate(another_hll), 1, "Added one hash.");
	rc = hll_merge(hll, another_hll);
	is(rc, 0, "No error.");
	is(hll_estimate(hll), 3, "Added another hash.");
	is(hll_estimate(another_hll), 1, "Still only one hash.");

	hll_delete(another_hll);
	hll_delete(hll);

	check_plan();
	footer();
}

/*
 * This test can dump the data that is used to measure
 * the estimation error. These data can be used for further
 * analysis and empirical based impovements of the algorithm.
 */
void
test_dense_hyperloglog_error(void)
{
	header();
	plan(HLL_N_PRECISIONS);

	struct est_errors errors[HLL_MAX_PRECISION + 1];

	FILE *output = NULL;
	if (OUTPUT_FILE_NAME != NULL) {
		output = fopen(OUTPUT_FILE_NAME, "w");
		assert(output && "Can't open the output file.");
	}

	MAYBE_PRINT(output, "prec, card, avg_est, std_err, max_err\n");

	for (int prec = HLL_MIN_PRECISION;
	     prec <= HLL_MAX_PRECISION; ++prec) {
		/*
		 * ** let m = n_registers **
		 * The range of 10m is divided into 3 sections with different
		 * estimation approach, each of which must be tested:
		 * [0 - m] - LinearCounting algorithm.
		 * [1m - 5m] - HyperLogLog with bias correction.
		 * [5m - 10m] - pure HyperLogLog algorithm.
		 */
		size_t max_card = 10 * n_registers(prec);
		measure_dense_hll_estimation_error(prec, max_card,
						   N_POINTS, SETS_PER_POINT,
						   errors + prec, output);
		/*
		 * The error of HyperLogLog is close to 1/sqrt(n_counters),
		 * but for small cardinalities LinearCounting is used because
		 * it has better accuracy, so the resulting error must be
		 * smaller than the HyperLogLog theoretical error.
		 */
		ok(errors[prec].std_err < hll_error(prec),
		   "The actual error doesn't exceed the expected value.");
	}

	if (output)
		fflush(output);

	for (int prec = HLL_MIN_PRECISION;
	     prec <= HLL_MAX_PRECISION; ++prec) {
		MAYBE_PRINT(output,
			    "prec:%d, std_err:%lg, max_err:%lg, exp_err: %lg\n",
			    prec, errors[prec].std_err, errors[prec].max_err,
			    hll_error(prec));
	}

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

	int dprec = HLL_MAX_PRECISION;
	struct est_errors errors;
	/*
	 * Sparse representation stores 4-byte pairs instead of 6-bit counters,
	 * so 32/6 < 6 times fewer pairs can be stored in the same amount of
	 * memory.
	 */
	uint64_t max_card = n_registers(dprec) / 6;
	measure_sparse_hll_estimation_error(dprec, max_card, N_POINTS,
					    SETS_PER_POINT, &errors, NULL);
	/*
	 * Since sparse rerpesentation uses the LinearCounting algorithm the
	 * error must be less than the error of the HyperLogLog algorithm.
	 */
	ok(errors.std_err < hll_error(HLL_SPARSE_PRECISION),
	   "The actual error doesn't exceed the expected value.");

	check_plan();
	footer();
}

void
test_sparse_to_dense_convertion(void)
{
	header();
	plan(HLL_N_PRECISIONS);

	for (int prec = HLL_MIN_PRECISION;
	     prec <= HLL_MAX_PRECISION; ++prec) {
		struct hll *sparse_hll = hll_new(prec, HLL_SPARSE);
		struct hll *dense_hll = hll_new(prec, HLL_DENSE);

		while (sparse_hll->representation == HLL_SPARSE) {
			uint64_t val = big_rand();
			uint64_t h = hash(val);
			/* Double add must not affect the estimation. */
			hll_add(sparse_hll, h);
			hll_add(sparse_hll, h);
			hll_add(dense_hll, h);
		}

		uint64_t sparse_est = hll_estimate(sparse_hll);
		uint64_t dense_est = hll_estimate(dense_hll);

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
			struct hll *ref_hll = hll_new(prec, HLL_DENSE);
			struct hll *hll_1 = hll_new(prec, HLL_SPARSE);
			struct hll *hll_2 = hll_new(prec, HLL_SPARSE);

			for (size_t i = 0; i < card; ++i) {
				uint64_t h = hash(big_rand());
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
			size_t ref_est = hll_estimate(ref_hll);
			size_t merged_est = hll_estimate(hll_1);
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
	test_sparse_to_dense_convertion();
	test_merge();

	footer();
	return check_plan();
}
