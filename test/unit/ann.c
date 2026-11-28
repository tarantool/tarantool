/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "lib/ann/ann_backend.h"
#include "lib/ann/ann_numeric.h"
#include "lib/ann/ann_vector.h"
#include "trivia/util.h"

#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

/** Allocation owner used to verify context and failure handling. */
struct owner {
	/** Number of attempted allocations. */
	int allocations;
	/** Attempt number that should fail, or zero. */
	int fail_at;
	/** Bytes currently owned by this context. */
	size_t live;
	/** Deallocations with a mismatched owner or size. */
	int wrong_free;
};

/** Aligned header preceding every test allocation. */
struct block {
	/** Context that created this allocation. */
	struct owner *owner;
	/** Number of charged payload bytes. */
	size_t size;
	/** Ensure the following payload is maximally aligned. */
	max_align_t align;
};

static void *
owner_alloc(void *ctx, size_t size)
{
	struct owner *owner = ctx;
	if (++owner->allocations == owner->fail_at)
		return NULL;
	struct block *block = realloc_with_free(NULL, sizeof(*block) + size);
	if (block == NULL)
		return NULL;
	block->owner = owner;
	block->size = size;
	owner->live += size;
	return block + 1;
}

static void
owner_free(void *ctx, void *ptr, size_t size)
{
	struct owner *owner = ctx;
	struct block *block = (struct block *)ptr - 1;
	if (block->owner != owner || block->size != size)
		++owner->wrong_free;
	owner->live -= block->size;
	free(block);
}

static struct ann_memory
owner_memory(struct owner *owner)
{
	struct ann_memory memory = {owner, owner_alloc, owner_free};
	return memory;
}

static struct ann_search_control
search_control(uint64_t work_limit)
{
	struct ann_search_control control = {0};
	control.work_limit = work_limit;
	return control;
}

static enum ann_status
insert(const struct ann_backend_ops *ops, struct ann_backend *backend,
       uint64_t label, const float *vector)
{
	struct ann_change *change = NULL;
	enum ann_status status = ops->prepare(backend, ANN_INSERT, label,
					     vector, 2, &change);
	if (status != ANN_OK)
		return status;
	status = ops->apply(change, NULL);
	ops->finish(change);
	return status;
}

static bool
accept_label(uint64_t label, const struct ann_vector *vector, void *ctx)
{
	(void)vector;
	return label == *(uint64_t *)ctx;
}

static uint64_t
clock_now(void *ctx)
{
	return *(uint64_t *)ctx;
}

static bool
always_stop(void *ctx)
{
	(void)ctx;
	return true;
}

static long double
oracle_distance(const float *query, const float *vector, uint32_t dimension,
		enum ann_metric metric, long double *scale)
{
	long double dot = 0, query_norm2 = 0, vector_norm2 = 0;
	long double l2 = 0, absolute_sum = 0;
	for (uint32_t i = 0; i < dimension; ++i) {
		long double q = query[i], x = vector[i];
		long double delta = q - x;
		l2 += delta * delta;
		dot += q * x;
		absolute_sum += fabsl(q * x);
		query_norm2 += q * q;
		vector_norm2 += x * x;
	}
	if (metric == ANN_L2) {
		*scale = 1 + l2;
		return l2;
	}
	if (metric == ANN_IP) {
		*scale = 1 + absolute_sum;
		return 1 - dot;
	}
	long double norm = sqrtl(query_norm2) * sqrtl(vector_norm2);
	*scale = 1 + absolute_sum / norm;
	return 1 - dot / norm;
}

static void
test_numeric(void)
{
	plan(23);
	float value = 0;
	ok(ann_f32_from_double(FLT_MAX, &value) == ANN_OK &&
	   value == FLT_MAX, "largest finite float32");
	is(ann_f32_from_double((double)FLT_MAX * 2, &value), ANN_INVALID,
	   "out-of-range float32 rejected");
	is(ann_f32_from_double(NAN, &value), ANN_INVALID, "NaN rejected");
	is(ann_f32_from_double(INFINITY, &value), ANN_INVALID,
	   "infinity rejected");
	int old_round = fegetround();
	float next = nextafterf(1.0f, 2.0f);
	double halfway = (1.0 + (double)next) / 2;
	fesetround(FE_UPWARD);
	ok(ann_f32_from_double(halfway, &value) == ANN_OK && value == 1,
	   "ties-to-even independent of caller rounding mode");
	is(fegetround(), FE_UPWARD, "caller rounding mode restored");
	fesetround(old_round);
	ok(ann_f32_from_double((double)FLT_TRUE_MIN / 4, &value) == ANN_OK &&
	   value == 0, "underflow rounds to zero");
	double tiny[] = {(double)FLT_TRUE_MIN / 4};
	float canonical[1];
	is(ann_vector_from_double(tiny, 1, ANN_COSINE, canonical), ANN_INVALID,
	   "cosine zero after conversion rejected");
	float huge_a[] = {1e20f};
	float huge_b[] = {-1e20f};
	double distance = 0;
	is(ann_distance(huge_a, huge_b, 1, ANN_L2, NULL, &distance),
	   ANN_OK, "large L2 computed");
	long double delta = (long double)huge_a[0] - huge_b[0];
	long double oracle = delta * delta;
	ok(fabsl((long double)distance - oracle) <=
	   8 * DBL_EPSILON * oracle, "L2 uses float64 arithmetic");
	float ip_a[] = {2, 3};
	float ip_b[] = {4, 5};
	is(ann_distance(ip_a, ip_b, 2, ANN_IP, NULL, &distance), ANN_OK,
	   "IP computed");
	is(distance, -22, "IP distance may be negative");
	is(ann_distance(ip_a, ip_a, 2, ANN_COSINE, NULL, &distance),
	   ANN_OK, "cosine computed");
	ok(distance >= 0 && distance < 1e-14,
	   "cosine is clamped at one");
	float zero[] = {0, 0};
	is(ann_distance(zero, ip_a, 2, ANN_COSINE, NULL, &distance),
	   ANN_INVALID, "zero cosine query rejected");
	is(ann_distance(ip_a, ip_b, 0, ANN_L2, NULL, &distance),
	   ANN_INVALID, "zero dimension rejected");
	float bad[] = {NAN, 1};
	is(ann_distance(bad, ip_b, 2, ANN_L2, NULL, &distance),
	   ANN_INVALID, "non-finite canonical component rejected");
	is(ann_vector_validate_f32(ip_a, 2, (enum ann_metric)-1),
	   ANN_INVALID, "unknown distance rejected");
	float subnormal[] = {FLT_TRUE_MIN};
	is(ann_distance(subnormal, subnormal, 1, ANN_COSINE,
			NULL, &distance), ANN_OK,
	   "nonzero float32 subnormal keeps a cosine norm");
	is(distance, 0, "zero cosine distance for equal subnormal vectors");
	float oracle_query[] = {10000, 10000, 1, -3};
	float oracle_vector[] = {10000, -10000, 1, 5};
	for (enum ann_metric metric = ANN_L2; metric <= ANN_IP; ++metric) {
		long double scale;
		long double expected = oracle_distance(oracle_query,
			oracle_vector, 4, metric, &scale);
		enum ann_status status = ann_distance(oracle_query,
						      oracle_vector, 4,
						      metric, NULL, &distance);
		ok(status == ANN_OK &&
		   fabsl((long double)distance - expected) <=
		   64 * DBL_EPSILON * 4 * scale,
		   "metric %d agrees with independent scaled oracle", metric);
	}
	check_plan();
}

static void
test_lifecycle(void)
{
	plan(38);
	const struct ann_backend_ops *ops = ann_backend_find("flat");
	ok(ops != NULL && ann_backend_find("hnsw") != NULL,
	   "reference and production algorithms are registered");
	struct owner first = {0}, second = {0};
	struct ann_memory first_mem = owner_memory(&first);
	struct ann_memory second_mem = owner_memory(&second);
	struct ann_config config = {2, ANN_L2, NULL};
	struct ann_backend *backend = NULL, *other = NULL;
	is(ops->create(&config, &first_mem, &backend), ANN_OK,
	   "first owner creates a backend");
	is(ops->create(&config, &second_mem, &other), ANN_OK,
	   "second owner creates a backend");
	float one[] = {1, 0}, two[] = {-1, 0}, three[] = {2, 0};
	is(insert(ops, backend, 2, two), ANN_OK, "insert label two");
	is(insert(ops, backend, 1, one), ANN_OK, "insert label one");
	is(insert(ops, backend, 3, three), ANN_OK, "insert label three");
	ok(ops->is_live(backend, 2), "inserted label is live");
	is(insert(ops, other, 9, one), ANN_OK, "second owner inserts");
	struct ann_backend_stats stats;
	ops->stat(backend, &stats);
	ok(stats.live == 3 && stats.retired == 0 && stats.capacity >= 3,
	   "stats describe retained entries");
	struct ann_search_control control = search_control(100);
	struct ann_search_opts opts = {3, 2, NULL, &control, NULL, NULL, NULL};
	struct ann_candidate out[3];
	uint32_t count = UINT32_MAX;
	float query[] = {0, 0};
	is(ops->search(backend, query, &opts, out, &count), ANN_OK,
	   "search succeeds");
	ok(count == 3 && out[0].label == 1 && out[1].label == 2 &&
	   out[2].label == 3, "exact order breaks equal distances by label");
	ok(out[0].distance == 1 && out[2].distance == 4,
	   "caller owns copied distance results");
	struct ann_candidate saved = out[0];
	ops->stat(backend, &stats);
	ok(out[0].label == saved.label &&
	   out[0].distance == saved.distance,
	   "caller buffer survives the next backend operation");
	uint64_t selected = 2;
	struct ann_filter filter = {accept_label, &selected};
	opts.filter = &filter;
	control = search_control(100);
	is(ops->search(backend, query, &opts, out, &count), ANN_OK,
	   "predicate search succeeds");
	ok(count == 1 && out[0].label == 2,
	   "predicate filters before candidate limit");
	struct ann_change *change = NULL;
	is(ops->prepare(backend, ANN_RETIRE, 2, NULL, 0, &change), ANN_OK,
	   "retire prepared");
	is(ops->apply(change, NULL), ANN_OK, "retire applied");
	is(ops->reclaim(backend, 2), ANN_BUSY,
	   "GC waits for a pending undo record");
	ops->rollback(change);
	ops->finish(change);
	opts.filter = NULL;
	control = search_control(100);
	is(ops->search(backend, query, &opts, out, &count), ANN_OK,
	   "search after rollback succeeds");
	ok(count == 3, "rollback restores retired version");
	is(ops->prepare(backend, ANN_RETIRE, 2, NULL, 0, &change), ANN_OK,
	   "retire prepared again");
	is(ops->apply(change, NULL), ANN_OK, "retire committed");
	ops->finish(change);
	control = search_control(100);
	is(ops->search(backend, query, &opts, out, &count), ANN_OK,
	   "current-view search succeeds");
	ok(count == 2 && out[0].label == 1,
	   "retired version hidden by default");
	ok(!ops->is_live(backend, 2), "retired label is not live");
	opts.filter = &filter;
	control = search_control(100);
	is(ops->search(backend, query, &opts, out, &count), ANN_OK,
	   "historical-view predicate succeeds");
	ok(count == 1 && out[0].label == 2,
	   "retired version available to older visibility view");
	is(ops->reclaim(backend, 2), ANN_OK, "retired version reclaimed");
	ok(!ops->is_live(backend, 2), "reclaimed label is not live");
	ops->stat(backend, &stats);
	ok(stats.live == 2 && stats.retired == 0,
	   "GC updates retained-version counters");
	is(ops->prepare(backend, ANN_INSERT, 4, one, 1, &change),
	   ANN_INVALID, "insert dimension mismatch rejected");
	opts.query_dimension = 1;
	is(ops->search(backend, query, &opts, out, &count), ANN_INVALID,
	   "query dimension mismatch rejected");
	config.metric = (enum ann_metric)-1;
	struct ann_backend *invalid = (struct ann_backend *)1;
	is(ops->create(&config, &first_mem, &invalid), ANN_INVALID,
	   "invalid backend metric rejected");
	is(invalid, NULL, "invalid create clears output pointer");
	int allocations;
	config.metric = ANN_L2;
	is(ops->prepare(backend, ANN_INSERT, 4, one, 2, &change),
	   ANN_OK, "rollback insert prepared");
	is(ops->apply(change, NULL), ANN_OK, "rollback insert applied");
	allocations = first.allocations;
	first.fail_at = allocations + 1;
	ops->rollback(change);
	ops->finish(change);
	ok(first.allocations == allocations,
	   "rollback and finish allocate no memory");
	ops->destroy(backend);
	ops->destroy(other);
	ok(first.live == 0 && second.live == 0 &&
	   first.wrong_free == 0 && second.wrong_free == 0,
	   "both allocation contexts fully released");
	check_plan();
}

static void
test_control(const char *algorithm)
{
	plan(14);
	const struct ann_backend_ops *ops = ann_backend_find(algorithm);
	struct owner owner = {0};
	struct ann_memory memory = owner_memory(&owner);
	struct ann_config config = {2, ANN_L2, NULL};
	struct ann_backend *backend = NULL;
	is(ops->create(&config, &memory, &backend), ANN_OK,
	   "control test backend created");
	float vector[] = {1, 0}, query[] = {0, 0};
	is(insert(ops, backend, 1, vector), ANN_OK, "control test inserted");
	struct ann_candidate out[1];
	uint32_t count = 9;
	struct ann_search_control control = search_control(1);
	struct ann_search_opts opts = {1, 2, NULL, &control, NULL, NULL, NULL};
	is(ops->search(backend, query, &opts, out, &count), ANN_WORK_LIMIT,
	   "query validation obeys work budget");
	is(count, 0, "work error has no successful result");
	control = search_control(3);
	is(ops->search(backend, query, &opts, out, &count), ANN_WORK_LIMIT,
	   "candidate and distance work share one budget");
	is(count, 0, "distance work error has no result");
	uint64_t absent = 99;
	struct ann_filter filter = {accept_label, &absent};
	opts.filter = &filter;
	control = search_control(2);
	is(ops->search(backend, query, &opts, out, &count), ANN_WORK_LIMIT,
	   "filtered-out candidate still consumes work");
	is(count, 0, "filtered scan has no partial result");
	opts.filter = NULL;
	uint64_t now = 10;
	control = search_control(100);
	control.deadline_ns = 10;
	control.now_ns = clock_now;
	control.ctx = &now;
	is(ops->search(backend, query, &opts, out, &count), ANN_TIMEOUT,
	   "expired monotonic deadline stops search");
	control = search_control(100);
	control.should_stop = always_stop;
	is(ops->search(backend, query, &opts, out, &count), ANN_STOPPED,
	   "caller stop callback stops search");
	control = search_control(100);
	opts.candidate_limit = 0;
	is(ops->search(backend, query, &opts, NULL, &count), ANN_OK,
	   "zero candidate limit validates and succeeds");
	is(count, 0, "zero candidate limit is empty");
	float invalid[] = {NAN, 1};
	is(ops->search(backend, invalid, &opts, NULL, &count), ANN_INVALID,
	   "zero limit still validates query");
	ops->destroy(backend);
	is(owner.live, 0, "control test memory released");
	check_plan();
}

static void
test_metric_search(void)
{
	plan(12);
	const struct ann_backend_ops *ops = ann_backend_find("flat");
	struct owner owner = {0};
	struct ann_memory memory = owner_memory(&owner);
	struct ann_config config = {2, ANN_COSINE, NULL};
	struct ann_backend *backend = NULL;
	is(ops->create(&config, &memory, &backend), ANN_OK,
	   "cosine backend created");
	float forward[] = {1, 0}, backward[] = {-1, 0};
	float zero[] = {0, 0};
	is(insert(ops, backend, 1, forward), ANN_OK,
	   "cosine forward inserted");
	is(insert(ops, backend, 2, backward), ANN_OK,
	   "cosine backward inserted");
	struct ann_search_control control = search_control(100);
	struct ann_search_opts opts = {2, 2, NULL, &control, NULL, NULL, NULL};
	struct ann_candidate out[2];
	uint32_t count = 0;
	is(ops->search(backend, forward, &opts, out, &count), ANN_OK,
	   "cosine Flat search succeeds");
	ok(count == 2 && out[0].label == 1 && out[0].distance == 0 &&
	   out[1].label == 2 && out[1].distance == 2,
	   "cosine Flat returns exact order and distance");
	struct ann_change *change = NULL;
	is(ops->prepare(backend, ANN_INSERT, 3, zero, 2, &change),
	   ANN_INVALID, "cosine Flat rejects zero norm");
	ops->destroy(backend);
	config.metric = ANN_IP;
	is(ops->create(&config, &memory, &backend), ANN_OK,
	   "IP backend created");
	float stronger[] = {2, 0};
	is(insert(ops, backend, 10, stronger), ANN_OK,
	   "stronger IP vector inserted");
	is(insert(ops, backend, 11, forward), ANN_OK,
	   "weaker IP vector inserted");
	control = search_control(100);
	is(ops->search(backend, forward, &opts, out, &count), ANN_OK,
	   "IP Flat search succeeds");
	ok(count == 2 && out[0].label == 10 && out[0].distance == -1 &&
	   out[1].label == 11 && out[1].distance == 0,
	   "IP Flat returns negative distance in exact order");
	ops->destroy(backend);
	ok(owner.live == 0 && owner.wrong_free == 0,
	   "metric backends release all owner memory");
	check_plan();
}

static void
test_compound_undo(void)
{
	plan(15);
	const struct ann_backend_ops *ops = ann_backend_find("flat");
	struct owner owner = {0};
	struct ann_memory memory = owner_memory(&owner);
	struct ann_config config = {2, ANN_L2, NULL};
	struct ann_backend *backend = NULL;
	is(ops->create(&config, &memory, &backend), ANN_OK,
	   "compound backend created");
	float old_vector[] = {1, 0}, new_vector[] = {2, 0};
	is(insert(ops, backend, 1, old_vector), ANN_OK,
	   "old version inserted");
	struct ann_change *old_change = NULL, *new_change = NULL;
	is(ops->prepare(backend, ANN_RETIRE, 1, NULL, 0, &old_change),
	   ANN_OK, "old version retire prepared");
	is(ops->apply(old_change, NULL), ANN_OK, "old version retired");
	owner.fail_at = owner.allocations + 1;
	is(ops->prepare(backend, ANN_INSERT, 2, new_vector, 2, &new_change),
	   ANN_OUT_OF_MEMORY, "new version OOM after old retirement");
	is(new_change, NULL, "failed second change has no undo record");
	owner.fail_at = 0;
	ops->rollback(old_change);
	ops->finish(old_change);
	struct ann_backend_stats stats;
	ops->stat(backend, &stats);
	ok(stats.live == 1 && stats.retired == 0,
	   "old version survives failed second preparation");
	is(ops->prepare(backend, ANN_RETIRE, 1, NULL, 0, &old_change),
	   ANN_OK, "old retire prepared again");
	is(ops->apply(old_change, NULL), ANN_OK, "old retire applied again");
	is(ops->prepare(backend, ANN_INSERT, 2, new_vector, 2, &new_change),
	   ANN_OK, "second change prepared while first undo is held");
	is(ops->apply(new_change, NULL), ANN_OK, "new version published");
	ops->stat(backend, &stats);
	ok(stats.live == 1 && stats.retired == 1,
	   "both versions coexist before resolution");
	int allocations = owner.allocations;
	owner.fail_at = allocations + 1;
	ops->rollback(new_change);
	ops->finish(new_change);
	ops->rollback(old_change);
	ops->finish(old_change);
	ok(owner.allocations == allocations,
	   "compound rollback and finish allocate nothing");
	ops->stat(backend, &stats);
	ok(stats.live == 1 && stats.retired == 0,
	   "compound rollback restores original version");
	ops->destroy(backend);
	ok(owner.live == 0 && owner.wrong_free == 0,
	   "compound undo releases all owner memory");
	check_plan();
}

static void
test_failure(void)
{
	plan(15);
	const struct ann_backend_ops *ops = ann_backend_find("flat");
	struct ann_config config = {2, ANN_L2, NULL};
	float vector[] = {1, 0};
	struct owner failed_owner = {.fail_at = 1};
	struct ann_memory failed_memory = owner_memory(&failed_owner);
	struct ann_backend *failed_backend = (struct ann_backend *)1;
	is(ops->create(&config, &failed_memory, &failed_backend),
	   ANN_OUT_OF_MEMORY, "create reports allocation failure");
	is(failed_backend, NULL, "failed create clears output pointer");
	is(failed_owner.live, 0, "failed create retains no memory");
	for (int point = 1; point <= 3; ++point) {
		struct owner owner = {0};
		struct ann_memory memory = owner_memory(&owner);
		struct ann_backend *backend = NULL;
		fail_unless(ops->create(&config, &memory, &backend) == ANN_OK);
		owner.fail_at = owner.allocations + point;
		struct ann_change *change = NULL;
		is(ops->prepare(backend, ANN_INSERT, 1, vector, 2, &change),
		   ANN_OUT_OF_MEMORY, "prepare fails at allocation %d", point);
		is(change, NULL, "failed prepare returns no undo record");
		struct ann_backend_stats stats;
		ops->stat(backend, &stats);
		is(stats.live, 0, "failed prepare leaves logical state empty");
		ops->destroy(backend);
		ok(owner.live == 0 && owner.wrong_free == 0,
		   "failed prepare releases owner %d", point);
	}
	check_plan();
}

static void
release_borrowed(void *ctx)
{
	++*(int *)ctx;
}

static bool
accept_any_label(uint64_t label, const struct ann_vector *vector, void *ctx)
{
	(void)vector;
	(void)label;
	(void)ctx;
	return true;
}

static bool
stop_search(void *ctx)
{
	(void)ctx;
	return true;
}

/** Both backends obey the same contract without implementation extensions. */
static void
test_backend_contract(const char *algorithm)
{
	plan(36);
	const struct ann_backend_ops *ops = ann_backend_find(algorithm);
	fail_unless(ops != NULL);
	struct owner owner = {0};
	struct ann_memory memory = owner_memory(&owner);
	struct ann_config config = {2, ANN_L2, NULL};
	struct ann_backend *backend = NULL;
	is(ops->create(&config, &memory, &backend), ANN_OK,
	   "%s creates through the common factory", algorithm);
	is(ops->reserve(backend, 8), ANN_OK, "reserve before mutation");
	is(ops->prepare_shared(backend, 17, NULL, NULL), ANN_INVALID,
	   "invalid shared payload is rejected");
	is(ann_vector_new(&memory, NULL, 2), NULL,
	   "copied owner rejects missing coordinates");
	is(ann_vector_borrow(&memory, NULL, 0, NULL, NULL), NULL,
	   "borrowed owner rejects invalid lifetime and dimension");
	/* Tuple coordinates may be unaligned and have an external owner. */
	const unsigned char packed[] = {0, 0, 0, 128, 63, 0, 0, 0, 64};
	int released = 0;
	struct ann_vector *vector =
		ann_vector_borrow(&memory, packed + 1, 2, &released,
				  release_borrowed);
	fail_unless(vector != NULL);
	is(ann_vector_dimension(vector), 2, "owner preserves dimension");
	ok(ann_vector_data(vector) == packed + 1,
	   "borrowed owner preserves the caller's coordinate address");
	struct ann_change *change = NULL;
	ok(!ops->contains(backend, 17), "unpublished label is absent");
	is(ops->prepare_shared(backend, 17, vector, &change), ANN_OK,
	   "prepare borrowed coordinates");
	ok(!ops->contains(backend, 17), "prepare does not publish the label");
	struct ann_search_control control = search_control(1000000);
	is(ops->apply(change, &control), ANN_OK, "publish borrowed vector");
	ops->finish(change);
	ann_vector_unref(vector);
	is(released, 0, "backend retains the external owner");
	ok(ops->contains(backend, 17), "published label is present");
	float query[] = {0, 0};
	struct ann_candidate found[2];
	uint32_t count = 0;
	struct ann_search_opts opts = {2, 2, NULL, &control, NULL, NULL, NULL};
	is(ops->search(backend, query, &opts, found, &count), ANN_OK,
	   "search borrowed coordinates");
	ok(count == 1 && found[0].label == 17 && found[0].distance == 5,
	   "shared numeric contract for unaligned little-endian values");
	struct ann_backend_stats stats;
	ops->stat(backend, &stats);
	is(stats.vector_bytes, 0, "shared payload is not charged twice");
	is(ops->set_live(backend, 17, false), ANN_OK, "retire the label");
	ok(!ops->is_live(backend, 17) && ops->contains(backend, 17),
	   "retired label remains available to older views");
	is(ops->search(backend, query, &opts, found, &count), ANN_OK,
	   "search current view");
	is(count, 0, "retired label is absent from the current view");
	struct ann_filter filter = {accept_any_label, NULL};
	opts.filter = &filter;
	is(ops->search(backend, query, &opts, found, &count), ANN_OK,
	   "search an external visibility view");
	is(count, 1, "external predicate can expose a retained version");
	is(ops->reclaim(backend, 17), ANN_OK, "GC releases the label");
	ok(!ops->contains(backend, 17), "reclaimed label is absent");
	uint64_t size = 0;
	uint64_t reusable = ops->reclaimable(backend, &size);
	ok(reusable <= size, "maintenance reports physical slot usage");
	float native[] = {3, 4};
	vector = ann_vector_new(&memory, native, 2);
	fail_unless(vector != NULL);
	owner.fail_at = owner.allocations + 1;
	is(ops->prepare_shared(backend, 21, vector, &change), ANN_OUT_OF_MEMORY,
	   "failed prepare preserves the caller's shared reference");
	owner.fail_at = 0;
	is(ops->prepare_shared(backend, 21, vector, &change), ANN_OK,
	   "prepare after allocation failure");
	control.should_stop = stop_search;
	is(ops->apply(change, &control), ANN_STOPPED,
	   "stopped mutation remains rollbackable");
	ops->rollback(change);
	ops->finish(change);
	ok(!ops->contains(backend, 21), "rollback keeps the label absent");
	control.should_stop = NULL;
	is(ops->prepare_shared(backend, 21, vector, &change), ANN_OK,
	   "prepare after rollback");
	is(ops->apply(change, &control), ANN_OK, "publish copied coordinates");
	ops->finish(change);
	ann_vector_unref(vector);
	opts.filter = NULL;
	is(ops->search(backend, query, &opts, found, &count), ANN_OK,
	   "search copied coordinates");
	ok(count == 1 && found[0].label == 21 && found[0].distance == 25,
	   "native coordinate ownership has the same search contract");
	ops->destroy(backend);
	is(released, 1, "external owner released exactly once");
	ok(owner.live == 0 && owner.wrong_free == 0,
	   "all shared and backend allocations released through their owner");
	ok(ann_backend_find(NULL) == NULL &&
	   ann_backend_find("missing") == NULL,
	   "unknown algorithms are not silently substituted");
	check_plan();
}

/** Snapshots preserve nodes across reuse, undo and generation retirement. */
static void
test_backend_snapshot(void)
{
	plan(20);
	const struct ann_backend_ops *ops = ann_backend_find("hnsw");
	struct owner owner = {0}, views = {0};
	struct ann_memory memory = owner_memory(&owner);
	struct ann_memory view_memory = owner_memory(&views);
	struct ann_config config = {2, ANN_L2, NULL};
	struct ann_backend *backend = NULL, *before = NULL, *during = NULL;
	is(ops->create(&config, &memory, &backend), ANN_OK,
	   "create CoW source");
	float original[] = {1, 0}, replacement[] = {9, 0}, query[] = {0, 0};
	is(insert(ops, backend, 1, original), ANN_OK, "insert original vector");
	int calls = owner.allocations;
	is(ops->snapshot(backend, &view_memory, &before), ANN_OK,
	   "freeze an immutable root");
	is(owner.allocations, calls,
	   "snapshot does not walk or copy source nodes");
	struct ann_change *change = NULL;
	bool retired = ops->prepare(backend, ANN_RETIRE, 1, NULL, 0, &change) ==
		ANN_OK && ops->apply(change, NULL) == ANN_OK;
	ops->finish(change);
	ok(retired && ops->reclaim(backend, 1) == ANN_OK, "reclaim live slot");
	is(ops->prepare(backend, ANN_INSERT, 2, replacement, 2, &change),
	   ANN_OK,
	   "prepare physical slot reuse");
	is(ops->apply(change, NULL), ANN_OK, "publish the reused row");
	is(ops->snapshot(backend, &view_memory, &during), ANN_OK,
	   "freeze applied but unresolved topology");
	calls = owner.allocations;
	ops->rollback(change);
	ops->finish(change);
	is(owner.allocations, calls, "pointer undo allocates nothing");
	struct ann_candidate found[2];
	uint32_t count;
	struct ann_search_control control = search_control(1000000);
	struct ann_filter filter = {accept_any_label, NULL};
	struct ann_search_opts opts = {
		2, 2, &filter, &control, NULL, NULL, NULL,
	};
	is(ops->search(before, query, &opts, found, &count), ANN_OK,
	   "search the original frozen topology");
	ok(count == 1 && found[0].label == 1 && found[0].distance == 1,
	   "reuse and rollback preserve original coordinates and label");
	is(ops->search(during, query, &opts, found, &count), ANN_OK,
	   "search a snapshot made before rollback");
	ok(count == 1 && found[0].label == 2 && found[0].distance == 81,
	   "undo does not write through the newer snapshot");
	ops->destroy(backend);
	is(ops->search(before, query, &opts, found, &count), ANN_OK,
	   "snapshot outlives source graph destruction");
	ops->destroy(during);
	ops->destroy(before);
	ok(owner.live == 0 && views.live == 0 && owner.wrong_free == 0 &&
	   views.wrong_free == 0, "shared storage uses its original allocator");
	bool failures = true, unchanged = true, clean = true;
	unsigned failed = 0;
	for (int nth = 1; nth <= 80; ++nth) {
		owner = (struct owner){0};
		views = (struct owner){0};
		ops->create(&config, &memory, &backend);
		insert(ops, backend, 1, original);
		ops->snapshot(backend, &view_memory, &before);
		ops->prepare(backend, ANN_INSERT, 2, replacement, 2, &change);
		owner.fail_at = owner.allocations + nth;
		enum ann_status status = ops->apply(change, NULL);
		if (status != ANN_OK) {
			++failed;
			failures &= status == ANN_OUT_OF_MEMORY;
			calls = owner.allocations;
			ops->rollback(change);
			failures &= owner.allocations == calls;
		}
		ops->finish(change);
		owner.fail_at = 0;
		control = search_control(1000000);
		unchanged &= ops->search(before, query, &opts, found, &count) ==
			ANN_OK && count == 1 && found[0].label == 1;
		ops->destroy(backend);
		ops->destroy(before);
		clean &= owner.live == 0 && views.live == 0 &&
			 owner.wrong_free == 0 && views.wrong_free == 0;
	}
	ok(failed > 10, "exercise entry, row, directory and scratch failures");
	ok(failures, "every failed write has allocation-free undo");
	ok(unchanged, "every injected failure preserves the frozen graph");
	ok(clean, "every failure releases all snapshot and source storage");
	views.fail_at = views.allocations + 1;
	ops->create(&config, &memory, &backend);
	is(ops->snapshot(backend, &view_memory, &before), ANN_OUT_OF_MEMORY,
	   "snapshot creation reports OOM");
	ops->destroy(backend);
	check_plan();
}

int
main(void)
{
	plan(10);
	test_numeric();
	test_lifecycle();
	test_control("flat");
	test_control("hnsw");
	test_metric_search();
	test_compound_undo();
	test_failure();
	test_backend_contract("flat");
	test_backend_contract("hnsw");
	test_backend_snapshot();
	return check_plan();
}
