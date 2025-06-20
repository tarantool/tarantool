#pragma once
/*
 * Copyright (c) 2025 VK Company Limited. All Rights Reserved.
 *
 * The information and source code contained herein is the exclusive property
 * of VK Company Limited and may not be disclosed, examined, or reproduced in
 * whole or in part without explicit written authorization from the Company.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "trivia/config.h"
#include "trivia/util.h"

#include <stdint.h>

#if defined(ENABLE_MEMCS_ENGINE)
# include "filters_impl.h"
#else /* !defined(ENABLE_MEMCS_ENGINE) */

struct filter_def;
struct filter_opts;
struct index;
struct space;
struct tuple;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Extract filters from tuple. An array of filter definitions is returned,
 * `count` is an output argument that will be set to the number of filters.
 */
static struct filter_def *
filters_from_tuple(struct tuple *tuple, uint32_t *count)
{
	(void)tuple;
	(void)count;
	diag_set(ClientError, ER_UNSUPPORTED, "Community edition",
		 "index filters");
	return NULL;
}

/**
 * Duplicate an array of filter definitions.
 */
static struct filter_def *
filters_dup(const struct filter_def *filters, uint32_t count)
{
	(void)filters;
	(void)count;
	unreachable();
	return NULL;
}

/**
 * Delete an array of filter definitions.
 */
static void
filters_delete(struct filter_def *filters, uint32_t count);
{
	(void)filters;
	(void)count;
	unreachable();
}

/**
 * Index method to set filters. After the function returns, the filters
 * should be created and ready for use.
 *
 * The index must handle rollback and commit by itself - after the function
 * successfully returns, the caller of the function will eventually call either
 * `index_set_filters_commit()` or `index_set_filters_rollback()`.
 * For the sake of simplicity, the index must forbid to set filters several
 * times without commit or rollback.
 *
 * Note that when the index is rebuilt, it should handle the filters by itself.
 * Silent drop of the filters is forbidden.
 *
 * NB: the function is not allowed to yield.
 */
static int
index_set_filters(struct index *index, struct filter_def *filters,
		  uint32_t filter_count, struct filter_opts *opts)
{
	(void)index;
	(void)filters;
	(void)filter_count;
	(void)opts;
	unreachable();
	return -1;
}

/**
 * Commit alter of filters, see `index_set_filters()`.
 * NB: the function is not allowed to yield.
 */
static void
index_set_filters_commit(struct index *index)
{
	(void)index;
	unreachable();
}

/**
 * Rollback alter of filters, see `index_set_filters()`.
 * NB: the function is not allowed to yield.
 */
static void
index_set_filters_rollback(struct index *index)
{
	(void)index;
	unreachable();
}

/** Whether the index has any filters. */
static bool
index_has_filters(struct index *index)
{
	(void)index;
	return false;
}

#ifdef __cplusplus
}
#endif

#endif /* !defined(ENABLE_MEMCS_ENGINE) */
