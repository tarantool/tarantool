/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "ann_numeric.h"

#include "mp_embedding.h"
#include <math.h>
#include <string.h>

enum ann_status
ann_search_control_poll(struct ann_search_control *control)
{
	if (control == NULL || control->work_limit == 0 ||
	    (control->deadline_ns != 0 && control->now_ns == NULL))
		return ANN_INVALID;
	if (control->deadline_ns != 0 &&
	    control->now_ns(control->ctx) >= control->deadline_ns)
		return ANN_TIMEOUT;
	if (control->should_stop != NULL &&
	    control->should_stop(control->ctx))
		return ANN_STOPPED;
	return ANN_OK;
}

enum ann_status
ann_search_control_step(struct ann_search_control *control)
{
	enum ann_status status = ann_search_control_poll(control);
	if (status != ANN_OK)
		return status;
	if (control->work_done >= control->work_limit)
		return ANN_WORK_LIMIT;
	++control->work_done;
	return ANN_OK;
}

enum ann_status
ann_f32_from_double(double value, float *out)
{
	return embedding_f32_from_double(value, out) == 0 ?
		ANN_OK : ANN_INVALID;
}

enum ann_status
ann_vector_validate_f32(const float *vector, uint32_t dimension,
			enum ann_metric metric)
{
	if (vector == NULL || dimension == 0 || (unsigned)metric > ANN_IP)
		return ANN_INVALID;
	double norm2 = 0;
	for (uint32_t i = 0; i < dimension; ++i) {
		if (!isfinite(vector[i]))
			return ANN_INVALID;
		if (metric == ANN_COSINE) {
			double x = vector[i];
			norm2 += x * x;
		}
	}
	if (metric == ANN_COSINE && norm2 == 0)
		return ANN_INVALID;
	return ANN_OK;
}

enum ann_status
ann_vector_from_double(const double *input, uint32_t dimension,
		       enum ann_metric metric, float *out)
{
	if (input == NULL || out == NULL || dimension == 0 ||
	    (unsigned)metric > ANN_IP)
		return ANN_INVALID;
	for (uint32_t i = 0; i < dimension; ++i) {
		enum ann_status status = ann_f32_from_double(input[i], &out[i]);
		if (status != ANN_OK)
			return status;
	}
	return ann_vector_validate_f32(out, dimension, metric);
}

float
ann_f32le_load(const void *data, uint32_t index)
{
	return embedding_f32le_load(data, index);
}

enum ann_status
ann_vector_validate_le(const void *data, uint32_t dimension,
		       enum ann_metric metric)
{
	if (data == NULL || dimension == 0 || (unsigned)metric > ANN_IP)
		return ANN_INVALID;
	double norm2 = 0;
	for (uint32_t i = 0; i < dimension; ++i) {
		double x = ann_f32le_load(data, i);
		if (!isfinite(x))
			return ANN_INVALID;
		norm2 += x * x;
	}
	return metric == ANN_COSINE && norm2 == 0 ? ANN_INVALID : ANN_OK;
}

enum ann_status
ann_distance(const float *query, const float *vector, uint32_t dimension,
	     enum ann_metric metric, struct ann_search_control *control,
	     double *out)
{
	return ann_distance_raw(query, false, vector, false, dimension,
				metric, control, out);
}

enum ann_status
ann_distance_raw(const void *query, bool query_le, const void *vector,
		 bool vector_le, uint32_t dimension, enum ann_metric metric,
		 struct ann_search_control *control, double *out)
{
	if (out == NULL || query == NULL || vector == NULL || dimension == 0 ||
	    (unsigned)metric > ANN_IP)
		return ANN_INVALID;
	double dot = 0;
	double query_norm2 = 0;
	double vector_norm2 = 0;
	double l2 = 0;
	for (uint32_t i = 0; i < dimension; ++i) {
		if (control != NULL) {
			enum ann_status status =
				ann_search_control_step(control);
			if (status != ANN_OK)
				return status;
		}
		double q = query_le ? ann_f32le_load(query, i) :
			((const float *)query)[i];
		double x = vector_le ? ann_f32le_load(vector, i) :
			((const float *)vector)[i];
		if (!isfinite(q) || !isfinite(x))
			return ANN_INVALID;
		if (metric == ANN_L2) {
			double delta = q - x;
			l2 += delta * delta;
		} else {
			dot += q * x;
			if (metric == ANN_COSINE) {
				query_norm2 += q * q;
				vector_norm2 += x * x;
			}
		}
	}
	if (metric == ANN_L2) {
		*out = l2;
	} else if (metric == ANN_IP) {
		*out = 1 - dot;
	} else {
		if (query_norm2 == 0 || vector_norm2 == 0)
			return ANN_INVALID;
		double similarity = dot / sqrt(query_norm2 * vector_norm2);
		if (similarity > 1)
			similarity = 1;
		if (similarity < -1)
			similarity = -1;
		*out = 1 - similarity;
	}
	return isfinite(*out) ? ANN_OK : ANN_INVALID;
}
