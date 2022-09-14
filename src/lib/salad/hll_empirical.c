/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "hll_empirical.h"
#include "hll.h"
#include "trivia/util.h"

#include <assert.h>

enum {
	/*
	 * Mupliply this constant with the number of counters of the HyperLogLog
	 * algorithm to get the cardinality above wich there is no any bias.
	 * This constant was found approxiamtely as a point on the bias graph,
	 * where the bias is zero.
	 */
	BIAS_RANGE = 5,
	/* The highest degree of the interpolation polynomial. */
	INTEROLATION_CURVE_DEGREE = 5,
};

/*
 * Array of coefficients of bias correction curves that is used to avoid the
 * bias of the raw estimation of the HyperLogLog algorithm.
 * The curves at 0 have a value of the order of m and then decreases linearly to
 * 0 before reaching point 5m. (m is the number of counters)
 * To find this curves, I found biases for many cardinalities from 0 to 6m and
 * then I got the bias curves by interpolating these biases.
 */
static const double bias_correction_curves[][INTEROLATION_CURVE_DEGREE + 1] = {
/* precision 6 */
{
	3.656778322121117e-11,
	-1.0157654721345629e-08,
	-1.2678085836096431e-05,
	0.0073197083790388804,
	-1.3865312935901248,
	91.67915712401428,
},
/* precision 7 */
{
	-4.294706535648134e-12,
	1.2143129345506375e-08,
	-1.3237403630160139e-05,
	0.007077604571987292,
	-1.9057390227389954,
	213.07458394034362,
},
/* precision 8 */
{
	-2.1838355333321183e-13,
	1.2785388976239e-09,
	-2.89354990414446e-06,
	0.0032166298071382416,
	-1.7989553517444152,
	416.3431487120903,
},
/* precision 9 */
{
	-2.1533198424775934e-14,
	2.222491331486239e-10,
	-9.081508548489511e-07,
	0.0018607825542265144,
	-1.9563564567469394,
	869.7116873924126,
},
/* precision 10 */
{
	-1.2979062142028944e-15,
	2.6901652770982905e-11,
	-2.2139271890053545e-07,
	0.0009155873194520684,
	-1.941923854013174,
	1737.2708188034546,
},
/* precision 11 */
{
	-7.769167546879811e-17,
	3.279195555750273e-12,
	-5.466939665233934e-08,
	0.0004554455468829395,
	-1.93755705183653,
	3473.167066706684,
},
/* precision 12 */
{
	-5.353871485179038e-18,
	4.39873788240566e-13,
	-1.4336858862855843e-08,
	0.00023466542395948685,
	-1.9713733520477046,
	7008.169798218187,
},
/* precision 13 */
{
	-3.2801438934547973e-19,
	5.4180601958541315e-14,
	-3.5464275595807208e-09,
	0.00011649057733135296,
	-1.9627617049556283,
	13987.357751628395,
},
/* precision 14 */
{
	-2.021466972895038e-20,
	6.6872533350709424e-15,
	-8.772627113647181e-10,
	5.778927552117441e-05,
	-1.9530867205289826,
	27907.023018390384,
},
/* precision 15 */
{
	-1.2738662947564625e-21,
	8.425818106867201e-16,
	-2.208388998887066e-10,
	2.9045024365459863e-05,
	-1.9594229366487512,
	55909.83618421248,
},
/* precision 16 */
{
	-8.119492540899859e-23,
	1.0691738276277167e-16,
	-5.5808288516743276e-11,
	1.4624960824201993e-05,
	-1.9673329967504534,
	112036.22750347921,
},
/* precision 17 */
{
	-5.014298363062619e-24,
	1.3236229138860697e-17,
	-1.3852283277123174e-11,
	7.277881451434703e-06,
	-1.9620448354982154,
	223786.7298929903,
},
/* precision 18 */
{
	-3.1708209046151947e-25,
	1.6702915235550548e-18,
	-3.4880067933507438e-12,
	3.6568738821316675e-06,
	-1.9677706205642147,
	448213.5537071222,
},
};

static_assert(lengthof(bias_correction_curves) == HLL_N_PRECISIONS,
	      "Size of thresholds_data doesn't correspond to the hll precsion bounds.");

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
 * better accuracy compared to the HyperLogLog algrithm. In this thresolds the
 * accuracy of the linear counting algorithm is equal to the accuracy of the
 * HyperLogLog algrithm.
 * To find these thresholds, I calculated the errors of the LinearCounting
 * algorithm for many points from 0 to 4m. Then I interpolated these points
 * and got an error curve. Finally, I got the threshold as the point at which
 * the value of the interpolated curve is equal to the error of the HyperLogLog
 * algorithm.
 */
static const uint64_t linear_counting_thresholds[] = {
	/* precision 6 */
	109,
	/* precision 7 */
	223,
	/* precision 8 */
	477,
	/* precision 9 */
	967,
	/* precision 10 */
	1913,
	/* precision 11 */
	3933,
	/* precision 12 */
	7937,
	/* precision 13 */
	15974,
	/* precision 14 */
	32379,
	/* precision 15 */
	62892,
	/* precision 16 */
	126517,
	/* precision 17 */
	253856,
	/* precision 18 */
	511081,
};

static_assert(lengthof(linear_counting_thresholds) == HLL_N_PRECISIONS,
	      "Size of linear_counting_thresholds doesn't correspond to the hll precsion bounds.");

uint64_t
hll_empirical_estimation_threshold(uint8_t precision)
{
	assert(hll_is_supported_precision(precision));
	return linear_counting_thresholds[precision - HLL_MIN_PRECISION];
}
