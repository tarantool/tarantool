/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "lib/ann/ann_backend.h"
#include "lib/ann/ann_usearch.h"
#include "lib/ann/ann_vector.h"
#include "lib/ann/ann_hnsw.h"
#include "box/ann_memory.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <usearch/basic/transactional_ram_index.hpp>

#include "trivia/util.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

/** Counting test owner with one deterministic allocation failure. */
struct owner {
	/** Total allocation attempts. */
	size_t calls;
	/** Attempt to fail, or zero. */
	size_t fail_at;
	/** Currently retained bytes. */
	size_t bytes;
	/** Mismatched owner or size on free. */
	size_t bad_free;
};

/** Test block retaining owner identity at maximal C alignment. */
union block {
	/** Payload alignment. */
	max_align_t alignment;
	/** Owner and size expected at release. */
	struct {
		/** Allocation owner. */
		struct owner *owner;
		/** Requested allocation size. */
		size_t size;
	} value;
};

static void *
owner_alloc(void *ctx, size_t size)
{
	struct owner *owner = (struct owner *)ctx;
	if (++owner->calls == owner->fail_at)
		return nullptr;
	union block *header =
		(union block *)realloc_with_free(NULL, sizeof(*header) + size);
	if (header == nullptr)
		return nullptr;
	header->value.owner = owner;
	header->value.size = size;
	owner->bytes += size;
	return header + 1;
}

static void
owner_free(void *ctx, void *ptr, size_t size)
{
	if (ptr == nullptr)
		return;
	union block *header = (union block *)ptr - 1;
	struct owner *owner = (struct owner *)ctx;
	if (header->value.owner != owner || header->value.size != size)
		++owner->bad_free;
	owner->bytes -= header->value.size;
	free(header);
}

static struct ann_memory
owner_memory(struct owner *owner)
{
	struct ann_memory memory = {owner, owner_alloc, owner_free};
	return memory;
}

static enum ann_status
insert(const struct ann_backend_ops *ops, struct ann_backend *backend,
       uint64_t label, const float *vector)
{
	struct ann_change *change = nullptr;
	enum ann_status status = ops->prepare(backend, ANN_INSERT, label,
					     vector, 2, &change);
	if (status != ANN_OK)
		return status;
	status = ops->apply(change, nullptr);
	if (status != ANN_OK)
		ops->rollback(change);
	ops->finish(change);
	return status;
}

static enum ann_status
search(const struct ann_backend_ops *ops, struct ann_backend *backend,
       const float *query, struct ann_candidate *out,
       uint32_t *count)
{
	struct ann_search_control control = {};
	control.work_limit = 100000;
	struct ann_search_opts opts = {};
	opts.candidate_limit = 2;
	opts.query_dimension = 2;
	opts.control = &control;
	return ops->search(backend, query, &opts, out, count);
}

/** Flat defines filtered top-k semantics independently of HNSW recall. */
static bool
accept_odd_label(uint64_t label, const struct ann_vector *vector, void *ctx)
{
	(void)vector;
	(void)ctx;
	return (label & 1) != 0;
}

static void
test_filtered_topk(const struct ann_backend_ops *ops)
{
	plan(3);
	struct owner owner = {};
	struct ann_memory memory = owner_memory(&owner);
	struct ann_config config = {2, ANN_L2, nullptr};
	struct ann_backend *backend = nullptr;
	is(ops->create(&config, &memory, &backend), ANN_OK,
	   "filtered reference backend creates");
	bool inserted = true;
	for (uint64_t label = 1; label <= 80; ++label) {
		float vector[] = {(float)label, 0};
		inserted &= insert(ops, backend, label, vector) == ANN_OK;
	}
	struct ann_filter filter = {accept_odd_label, nullptr};
	struct ann_search_control control = {};
	control.work_limit = 1000000;
	struct ann_hnsw_search_opts algorithm = {128, nullptr, nullptr};
	struct ann_search_opts opts = {};
	opts.candidate_limit = 10;
	opts.query_dimension = 2;
	opts.filter = &filter;
	opts.control = &control;
	if (ops == &ann_usearch_ops)
		opts.algorithm = &algorithm;
	float query[] = {0, 0};
	struct ann_candidate found[10];
	uint32_t count = 0;
	bool matches = inserted &&
		ops->search(backend, query, &opts, found, &count) == ANN_OK &&
		count == 10;
	for (uint32_t i = 0; matches && i < count; ++i)
		matches &= found[i].label == 2 * i + 1;
	ok(matches, "filter runs before limiting candidates");
	ops->destroy(backend);
	ok(owner.bytes == 0 && owner.bad_free == 0,
	   "filtered search releases all memory");
	check_plan();
}

/** Labels with the same tens digit represent one result identity. */
static int
compare_tied_labels(const struct ann_candidate *a,
		    const struct ann_candidate *b, void *ctx)
{
	(void)ctx;
	uint64_t left = a->label / 10, right = b->label / 10;
	return (left > right) - (left < right);
}

static void
test_ranked_topk(void)
{
	plan(6);
	struct owner owner = {};
	struct ann_memory memory = owner_memory(&owner);
	struct ann_config config = {2, ANN_L2, nullptr};
	struct ann_backend *backend = nullptr;
	const auto *ops = &ann_usearch_ops;
	is(ops->create(&config, &memory, &backend), ANN_OK, "create graph");
	float nearest[] = {0, 0}, next[] = {1, 0};
	fail_unless(insert(ops, backend, 92, nearest) == ANN_OK);
	fail_unless(insert(ops, backend, 22, next) == ANN_OK);
	fail_unless(insert(ops, backend, 11, nearest) == ANN_OK);
	fail_unless(insert(ops, backend, 12, nearest) == ANN_OK);
	struct ann_hnsw_search_opts algorithm = {
		64, compare_tied_labels, nullptr,
	};
	struct ann_search_control control = {};
	control.work_limit = 100000;
	struct ann_search_opts opts = {};
	opts.candidate_limit = 2;
	opts.query_dimension = 2;
	opts.control = &control;
	opts.algorithm = &algorithm;
	struct ann_candidate found[2];
	uint32_t count;
	is(ops->search(backend, nearest, &opts, found, &count), ANN_OK,
	   "search with an order for equal distances");
	ok(count == 2 && found[0].label / 10 == 1 &&
	   found[1].label / 10 == 9,
	   "tied order and duplicate identities are handled before the limit");
	ops->destroy(backend);
	struct ann_hnsw_config hnsw = {4, 4, 1};
	config.algorithm = &hnsw;
	fail_unless(ops->create(&config, &memory, &backend) == ANN_OK);
	float distant[] = {100, 0};
	fail_unless(insert(ops, backend, 11, nearest) == ANN_OK);
	fail_unless(insert(ops, backend, 23, distant) == ANN_OK);
	fail_unless(insert(ops, backend, 32, next) == ANN_OK);
	fail_unless(insert(ops, backend, 13, nearest) == ANN_OK);
	fail_unless(insert(ops, backend, 34, next) == ANN_OK);
	fail_unless(insert(ops, backend, 15, nearest) == ANN_OK);
	struct ann_backend *snapshot = nullptr;
	fail_unless(ops->snapshot(backend, &memory, &snapshot) == ANN_OK);
	struct ann_filter filter = {accept_odd_label, nullptr};
	opts.filter = &filter;
	algorithm.expansion = 1;
	is(ops->search(snapshot, nearest, &opts, found, &count), ANN_OK,
	   "search a frozen graph at minimum breadth");
	ok(count == 2 && found[0].label / 10 == 1 && found[1].label == 23,
	   "duplicate identities don't shorten the graph walk");
	ops->destroy(snapshot);
	ops->destroy(backend);
	ok(owner.bytes == 0 && owner.bad_free == 0, "release graph");
	check_plan();
}

static void
test_lifecycle(const struct ann_backend_ops *ops)
{
	plan(25);
	struct owner owner = {};
	struct ann_memory memory = owner_memory(&owner);
	struct ann_config config = {2, ANN_L2, nullptr};
	struct ann_backend *backend = nullptr;
	is(ops->create(&config, &memory, &backend), ANN_OK,
	   "%s creates", ops->name);
	float first[] = {1, 0};
	float second[] = {10, 0};
	is(insert(ops, backend, 11, first), ANN_OK, "first insert");
	is(insert(ops, backend, 22, second), ANN_OK, "second insert");
	ok(ops->is_live(backend, 11), "inserted graph label is live");
	struct ann_backend_stats stats;
	ops->stat(backend, &stats);
	ok(stats.live == 2 && stats.resident_bytes == owner.bytes,
	   "all backend allocations appear in resident memory");
	struct ann_candidate found[2];
	uint32_t count = 0;
	is(search(ops, backend, first, found, &count), ANN_OK,
	   "search succeeds");
	ops->stat(backend, &stats);
	is(stats.resident_bytes, owner.bytes,
	   "search buffers appear in resident memory");
	is(count, 2, "two candidates found");
	is(found[0].label, 11, "nearest label is first");
	is(found[0].distance, 0, "nearest distance is zero");
	size_t alloc_before = owner.calls;
	is(ops->set_live(backend, 11, false), ANN_OK,
	   "existing label deactivates without preparation");
	ok(!ops->is_live(backend, 11), "deactivated graph label is not live");
	is(ops->set_live(backend, 11, true), ANN_OK,
	   "existing label reactivates without preparation");
	ok(ops->is_live(backend, 11), "reactivated graph label is live");
	is(ops->set_live(backend, 33, false), ANN_NOT_FOUND,
	   "unknown label cannot change visibility");
	ok(owner.calls == alloc_before,
	   "visibility compensation makes no allocations");
	struct ann_change *change = nullptr;
	is(ops->prepare(backend, ANN_RETIRE, 11, nullptr, 0, &change),
	   ANN_OK, "retire prepared");
	is(ops->apply(change, nullptr), ANN_OK, "retire applied");
	ops->finish(change);
	is(search(ops, backend, first, found, &count), ANN_OK,
	   "search after retire succeeds");
	ok(count == 1 && found[0].label == 22,
	   "retired label is excluded");
	is(ops->reclaim(backend, 11), ANN_OK,
	   "visibility GC releases the retired label");
	ok(!ops->is_live(backend, 11), "reclaimed graph label is not live");
	is(insert(ops, backend, 11, first), ANN_OK,
	   "released label can be assigned to a new version");
	ok(search(ops, backend, first, found, &count) == ANN_OK &&
	   count == 2 && found[0].label == 11,
	   "reclaimed graph node does not appear in results");
	ops->destroy(backend);
	ok(owner.bytes == 0 && owner.bad_free == 0,
	   "backend returns every allocation to its owner");
	check_plan();
}

static void
test_failed_insert(void)
{
	plan(6);
	struct owner baseline = {};
	struct ann_memory memory = owner_memory(&baseline);
	struct ann_config config = {2, ANN_L2, nullptr};
	struct ann_backend *backend = nullptr;
	float first[] = {1, 0};
	float second[] = {10, 0};
	is(ann_usearch_ops.create(&config, &memory, &backend), ANN_OK,
	   "failure baseline creates");
	is(insert(&ann_usearch_ops, backend, 11, first), ANN_OK,
	   "failure baseline first insert");
	size_t before = baseline.calls;
	is(insert(&ann_usearch_ops, backend, 22, second), ANN_OK,
	   "failure baseline second insert");
	size_t steps = baseline.calls - before;
	ann_usearch_ops.destroy(backend);
	is(baseline.bytes, 0, "failure baseline releases all memory");
	bool consistent = true;
	for (size_t point = 1; point <= steps; ++point) {
		struct owner owner = {};
		memory = owner_memory(&owner);
		backend = nullptr;
		enum ann_status created =
			ann_usearch_ops.create(&config, &memory, &backend);
		if (created != ANN_OK ||
		    insert(&ann_usearch_ops, backend, 11, first) != ANN_OK) {
			consistent = false;
			break;
		}
		owner.fail_at = owner.calls + point;
		enum ann_status status = insert(&ann_usearch_ops, backend,
						22, second);
		struct ann_candidate found[2];
		uint32_t count = 0;
		owner.fail_at = 0;
		if (status != ANN_OUT_OF_MEMORY ||
		    search(&ann_usearch_ops, backend, first,
			   found, &count) != ANN_OK ||
		    count != 1 || found[0].label != 11)
			consistent = false;
		ann_usearch_ops.destroy(backend);
		if (owner.bytes != 0 || owner.bad_free != 0)
			consistent = false;
		if (!consistent)
			break;
	}
	ok(steps != 0, "second insert exercises allocations");
	ok(consistent, "allocation failure preserves search and ownership");
	check_plan();
}

static void
test_interrupted_search(void)
{
	plan(8);
	struct owner owner = {};
	struct ann_memory memory = owner_memory(&owner);
	struct ann_config config = {2, ANN_L2, nullptr};
	struct ann_backend *backend = nullptr;
	is(ann_usearch_ops.create(&config, &memory, &backend), ANN_OK,
	   "interruption backend creates");
	float first[] = {1, 0};
	float second[] = {10, 0};
	is(insert(&ann_usearch_ops, backend, 11, first), ANN_OK,
	   "interruption first insert");
	is(insert(&ann_usearch_ops, backend, 22, second), ANN_OK,
	   "interruption second insert");
	struct ann_search_control control = {};
	control.work_limit = 2;
	struct ann_search_opts opts = {};
	opts.candidate_limit = 2;
	opts.query_dimension = 2;
	opts.control = &control;
	struct ann_candidate found[2];
	uint32_t count = 9;
	is(ann_usearch_ops.search(backend, first, &opts, found, &count),
	   ANN_WORK_LIMIT, "traversal returns work-limit error");
	is(count, 0, "interrupted search publishes no candidates");
	is(search(&ann_usearch_ops, backend, first, found, &count), ANN_OK,
	   "later search gets independent traversal scratch");
	ok(count == 2 && found[0].label == 11,
	   "later search retains correct results");
	ann_usearch_ops.destroy(backend);
	ok(owner.bytes == 0 && owner.bad_free == 0,
	   "interrupted backend releases every allocation");
	check_plan();
}

static enum ann_status
wide_search(struct ann_backend *backend, const float *query,
	    struct ann_candidate *out, uint32_t *count)
{
	struct ann_search_control control = {};
	control.work_limit = 100000;
	struct ann_hnsw_search_opts algorithm = {1024, nullptr, nullptr};
	struct ann_search_opts opts = {};
	opts.candidate_limit = 2;
	opts.query_dimension = 2;
	opts.control = &control;
	opts.algorithm = &algorithm;
	return ann_usearch_ops.search(backend, query, &opts, out, count);
}

static void
test_failed_search(void)
{
	plan(5);
	struct owner baseline = {};
	struct ann_memory memory = owner_memory(&baseline);
	struct ann_config config = {2, ANN_L2, nullptr};
	struct ann_backend *backend = nullptr;
	float first[] = {1, 0};
	float second[] = {10, 0};
	is(ann_usearch_ops.create(&config, &memory, &backend), ANN_OK,
	   "search failure baseline creates");
	is(insert(&ann_usearch_ops, backend, 11, first), ANN_OK,
	   "search failure baseline first insert");
	is(insert(&ann_usearch_ops, backend, 22, second), ANN_OK,
	   "search failure baseline second insert");
	size_t before = baseline.calls;
	struct ann_candidate found[2];
	uint32_t count = 0;
	bool baseline_ok = wide_search(backend, first, found, &count) == ANN_OK;
	size_t steps = baseline.calls - before;
	ann_usearch_ops.destroy(backend);
	ok(baseline_ok && steps != 0 && baseline.bytes == 0,
	   "wide search allocates and releases buffers");
	bool consistent = true;
	for (size_t point = 1; point <= steps; ++point) {
		struct owner owner = {};
		memory = owner_memory(&owner);
		backend = nullptr;
		enum ann_status created =
			ann_usearch_ops.create(&config, &memory, &backend);
		if (created != ANN_OK ||
		    insert(&ann_usearch_ops, backend, 11, first) != ANN_OK ||
		    insert(&ann_usearch_ops, backend, 22, second) != ANN_OK) {
			consistent = false;
			break;
		}
		owner.fail_at = owner.calls + point;
		count = 9;
		enum ann_status status = wide_search(backend, first,
						     found, &count);
		owner.fail_at = 0;
		if (status != ANN_OUT_OF_MEMORY || count != 0 ||
		    wide_search(backend, first, found, &count) != ANN_OK ||
		    count != 2 || found[0].label != 11)
			consistent = false;
		ann_usearch_ops.destroy(backend);
		if (owner.bytes != 0 || owner.bad_free != 0)
			consistent = false;
		if (!consistent)
			break;
	}
	ok(consistent, "every search allocation failure permits retry");
	check_plan();
}

static void
test_interrupted_insert(void)
{
	plan(3);
	struct ann_config config = {2, ANN_L2, nullptr};
	float first[] = {1, 0};
	float second[] = {10, 0};
	float third[] = {20, 0};
	bool consistent = true;
	bool stopped_late = false;
	bool reached_success = false;
	for (uint64_t budget = 1; budget <= 256; ++budget) {
		struct owner owner = {};
		struct ann_memory memory = owner_memory(&owner);
		struct ann_backend *backend = nullptr;
		enum ann_status created =
			ann_usearch_ops.create(&config, &memory, &backend);
		if (created != ANN_OK ||
		    insert(&ann_usearch_ops, backend, 11, first) != ANN_OK ||
		    insert(&ann_usearch_ops, backend, 22, second) != ANN_OK) {
			consistent = false;
			break;
		}
		struct ann_change *change = nullptr;
		if (ann_usearch_ops.prepare(backend, ANN_INSERT, 33,
					    third, 2, &change) != ANN_OK) {
			consistent = false;
			ann_usearch_ops.destroy(backend);
			break;
		}
		struct ann_search_control control = {};
		control.work_limit = budget;
		enum ann_status status = ann_usearch_ops.apply(change,
							   &control);
		if (status == ANN_OK) {
			reached_success = true;
			ann_usearch_ops.finish(change);
		} else {
			if (status != ANN_WORK_LIMIT)
				consistent = false;
			if (budget > 10)
				stopped_late = true;
			ann_usearch_ops.rollback(change);
			ann_usearch_ops.finish(change);
			struct ann_candidate found[2];
			uint32_t count = 0;
			if (search(&ann_usearch_ops, backend, first,
				   found, &count) != ANN_OK ||
			    count != 2 || found[0].label != 11 ||
			    insert(&ann_usearch_ops, backend, 33, third) != ANN_OK)
				consistent = false;
		}
		ann_usearch_ops.destroy(backend);
		if (owner.bytes != 0 || owner.bad_free != 0)
			consistent = false;
		if (reached_success || !consistent)
			break;
	}
	ok(stopped_late, "insertion stops after entering graph traversal");
	ok(reached_success, "sufficient write budget permits insertion");
	ok(consistent, "every interrupted insertion restores graph and owner");
	check_plan();
}

/** Reproduce cancellation at the last poll of a successful write. */
struct late_stop {
	/** Number of polls observed in this write. */
	unsigned count;
	/** Poll that requests cancellation, or zero to allow the write. */
	unsigned stop_at;
};

static bool
stop_at_poll(void *ctx)
{
	auto *stop = static_cast<struct late_stop *>(ctx);
	return ++stop->count == stop->stop_at;
}

static void
test_late_cancel(void)
{
	plan(5);
	struct owner owner = {};
	struct ann_memory memory = owner_memory(&owner);
	struct ann_config config = {2, ANN_L2, nullptr};
	struct ann_backend *backend = nullptr;
	fail_unless(ann_usearch_ops.create(&config, &memory, &backend) ==
		    ANN_OK);
	float first[] = {1, 0}, second[] = {10, 0};
	fail_unless(insert(&ann_usearch_ops, backend, 11, first) == ANN_OK);
	struct late_stop stop = {};
	struct ann_search_control control = {};
	control.work_limit = 100000;
	control.should_stop = stop_at_poll;
	control.ctx = &stop;
	struct ann_change *change = nullptr;
	fail_unless(ann_usearch_ops.prepare(backend, ANN_INSERT, 22, second, 2,
					    &change) == ANN_OK);
	is(ann_usearch_ops.apply(change, &control), ANN_OK,
	   "successful graph write records every poll");
	ann_usearch_ops.rollback(change);
	ann_usearch_ops.finish(change);
	unsigned last_poll = stop.count;
	ok(last_poll > 5, "the final poll follows graph construction");

	stop = {0, last_poll};
	control.work_done = 0;
	fail_unless(ann_usearch_ops.prepare(backend, ANN_INSERT, 22, second, 2,
					    &change) == ANN_OK);
	is(ann_usearch_ops.apply(change, &control), ANN_STOPPED,
	   "late cancellation restores the graph before returning");
	size_t calls = owner.calls;
	ann_usearch_ops.rollback(change);
	ann_usearch_ops.finish(change);
	ok(owner.calls == calls && !ann_usearch_ops.contains(backend, 22),
	   "late cancellation leaves allocation-free rollback and no new label");

	struct ann_candidate found[2];
	uint32_t count = 0;
	bool restored = search(&ann_usearch_ops, backend, first, found,
			       &count) == ANN_OK &&
		count == 1 && found[0].label == 11;
	ann_usearch_ops.destroy(backend);
	ok(restored && owner.bytes == 0 && owner.bad_free == 0,
	   "search sees the original root and all allocations are released");
	check_plan();
}

/** Exact scalar metric for exercising the native physical-slot ceiling. */
struct slot_metric {
	/** Query in the current round. */
	double query;
	/** Distance for the staged candidate. */
	double distance;
	/** Whether the current one-entry round has been filled. */
	bool staged;

	void reset(double value) noexcept
	{
		query = value;
		staged = false;
	}

	unum::usearch::basic::metric_staging_t push(double value) noexcept
	{
		using unum::usearch::basic::metric_staging_t;
		if (staged)
			return metric_staging_t::refused_k;
		double delta = value - query;
		distance = delta * delta;
		staged = true;
		return metric_staging_t::staged_k;
	}

	void drain(std::span<double> result, double) noexcept
	{
		result.front() = distance;
		staged = false;
	}
};

static void
test_native_slot_limit(void)
{
	plan(6);
	using namespace unum::usearch::basic;
	using graph_t = transactional_ram_index<double, uint64_t, uint32_t,
						double,
						std::allocator<std::byte>>;
	graph_t graph(graph_t::allocator_t{}, {4, 2});
	graph_t::traversal_context_t scratch;
	struct slot_metric metric = {};
	ok(graph.insert(10, 1.0, metric, scratch).status ==
	   status_t::success_k, "first node occupies the first slot");
	ok(graph.insert(20, 2.0, metric, scratch).status ==
	   status_t::success_k, "second node reaches the ceiling");
	auto old = graph.snapshot();
	ok(graph.insert(30, 3.0, metric, scratch).status ==
	   status_t::overflow_k && graph.size() == 2,
	   "append at the limit leaves both physical slots intact");
	ok(graph.replace(0, 30, 3.0, metric, scratch) == status_t::success_k &&
	   graph.size() == 2, "replacing a slot succeeds at the ceiling");
	auto current = graph.snapshot();
	ok(*old.at(0).value.key == 10 && *current.at(0).value.key == 30,
	   "the old root retains the previous slot identity");
	ok(graph.restore(old) == status_t::success_k &&
	   *graph.snapshot().at(0).value.key == 10,
	   "restore rewinds slot identity at the ceiling");
	check_plan();
}

static void
test_rollback_no_alloc(void)
{
	plan(6);
	struct owner owner = {};
	struct ann_memory memory = owner_memory(&owner);
	struct ann_config config = {2, ANN_L2, nullptr};
	struct ann_backend *backend = nullptr;
	is(ann_usearch_ops.create(&config, &memory, &backend), ANN_OK,
	   "rollback backend creates");
	float first[] = {1, 0};
	float second[] = {10, 0};
	is(insert(&ann_usearch_ops, backend, 11, first), ANN_OK,
	   "rollback baseline insert");
	struct ann_change *change = nullptr;
	is(ann_usearch_ops.prepare(backend, ANN_INSERT, 22,
				   second, 2, &change), ANN_OK,
	   "rollback insertion prepares");
	is(ann_usearch_ops.apply(change, nullptr), ANN_OK,
	   "rollback insertion mutates graph");
	size_t calls = owner.calls;
	owner.fail_at = calls + 1;
	ann_usearch_ops.rollback(change);
	ann_usearch_ops.finish(change);
	is(owner.calls, calls, "rollback and finish allocate nothing");
	ann_usearch_ops.destroy(backend);
	ok(owner.bytes == 0 && owner.bad_free == 0,
	   "rolled-back graph releases all memory");
	check_plan();
}

static void
test_growth_and_retire(void)
{
	plan(6);
	struct owner owner = {};
	struct ann_memory memory = owner_memory(&owner);
	struct ann_hnsw_config algorithm = {8, 32, 64};
	struct ann_config config = {2, ANN_L2, &algorithm};
	struct ann_backend *backend = nullptr;
	is(ann_usearch_ops.create(&config, &memory, &backend), ANN_OK,
	   "growth backend creates with HNSW options");
	bool inserted = true;
	for (uint64_t i = 0; i < 128; ++i) {
		float vector[] = {(float)i, (float)(i % 3)};
		if (insert(&ann_usearch_ops, backend, i + 1, vector) !=
		    ANN_OK) {
			inserted = false;
			break;
		}
	}
	ok(inserted, "128 vectors grow graph and lookup");
	bool retired = true;
	for (uint64_t label = 2; label <= 128; label += 2) {
		struct ann_change *change = nullptr;
		if (ann_usearch_ops.prepare(backend, ANN_RETIRE, label,
					    nullptr, 0, &change) != ANN_OK ||
		    ann_usearch_ops.apply(change, nullptr) != ANN_OK) {
			retired = false;
			break;
		}
		ann_usearch_ops.finish(change);
	}
	ok(retired, "64 versions retire after graph growth");
	struct ann_backend_stats stats;
	ann_usearch_ops.stat(backend, &stats);
	ok(stats.live == 64 && stats.retired == 64 &&
	   stats.resident_bytes == owner.bytes,
	   "growth and tombstones remain charged");
	float query[] = {64, 1};
	struct ann_candidate found[2];
	uint32_t count = 0;
	ok(search(&ann_usearch_ops, backend, query, found, &count) == ANN_OK &&
	   count > 0 && found[0].label == 65 && found[0].distance == 0,
	   "search finds exact live vector through retired nodes");
	ann_usearch_ops.destroy(backend);
	ok(owner.bytes == 0 && owner.bad_free == 0,
	   "grown graph releases every allocation");
	check_plan();
}

static void
test_two_owners(void)
{
	plan(8);
	struct owner first_owner = {}, second_owner = {};
	struct ann_memory first_memory = owner_memory(&first_owner);
	struct ann_memory second_memory = owner_memory(&second_owner);
	struct ann_config config = {2, ANN_L2, nullptr};
	struct ann_backend *first = nullptr, *second = nullptr;
	is(ann_usearch_ops.create(&config, &first_memory, &first), ANN_OK,
	   "first owner creates backend");
	is(ann_usearch_ops.create(&config, &second_memory, &second), ANN_OK,
	   "second owner creates backend");
	float a[] = {1, 0};
	float b[] = {2, 0};
	is(insert(&ann_usearch_ops, first, 11, a), ANN_OK,
	   "first owner inserts");
	is(insert(&ann_usearch_ops, second, 22, b), ANN_OK,
	   "second owner inserts");
	ann_usearch_ops.destroy(first);
	is(first_owner.bytes, 0, "first owner releases only its backend");
	struct ann_candidate found[2];
	uint32_t count = 0;
	ok(search(&ann_usearch_ops, second, b, found, &count) == ANN_OK &&
	   count == 1 && found[0].label == 22,
	   "second backend remains searchable");
	ann_usearch_ops.destroy(second);
	is(second_owner.bytes, 0, "second owner releases its backend");
	ok(first_owner.bad_free == 0 && second_owner.bad_free == 0,
	   "no allocation crosses owner boundary");
	check_plan();
}

static void
test_shared_memtx_quota(void)
{
	plan(8);
	struct quota quota;
	quota_init(&quota, 1024 * 1024);
	struct ann_quota_memory older_owner, newer_owner;
	ann_quota_memory_create(&older_owner, &quota);
	ann_quota_memory_create(&newer_owner, &quota);
	struct ann_memory older_memory = ann_quota_memory_bind(&older_owner);
	struct ann_memory newer_memory = ann_quota_memory_bind(&newer_owner);
	struct ann_config config = {2, ANN_L2, nullptr};
	struct ann_backend *older = nullptr, *newer = nullptr;
	is(ann_usearch_ops.create(&config, &older_memory, &older), ANN_OK,
	   "older graph creates under memtx quota");
	is(ann_usearch_ops.create(&config, &newer_memory, &newer), ANN_OK,
	   "newer graph creates under same quota");
	float vector[] = {1, 2};
	is(insert(&ann_usearch_ops, older, 11, vector), ANN_OK,
	   "older graph grows under quota");
	is(insert(&ann_usearch_ops, newer, 22, vector), ANN_OK,
	   "newer graph grows under quota");
	ok(quota_used(&quota) > 0 && older_owner.blocks > 0 &&
	   newer_owner.blocks > 0, "both owners charge shared quota");
	ann_usearch_ops.destroy(older);
	struct ann_candidate found[2];
	uint32_t count = 0;
	ok(older_owner.blocks == 0 &&
	   search(&ann_usearch_ops, newer, vector, found, &count) == ANN_OK &&
	   count == 1 && found[0].label == 22,
	   "old graph releases charge while newer remains searchable");
	ann_usearch_ops.destroy(newer);
	ok(older_owner.blocks == 0 && newer_owner.blocks == 0,
	   "both graph owners release every allocation");
	is(quota_used(&quota), 0, "memtx quota returns to zero");
	check_plan();
}

static void
test_quota_exhaustion(void)
{
	plan(9);
	struct quota quota;
	quota_init(&quota, 1024 * 1024);
	struct ann_quota_memory owner;
	ann_quota_memory_create(&owner, &quota);
	struct ann_memory memory = ann_quota_memory_bind(&owner);
	struct ann_config config = {2, ANN_L2, nullptr};
	struct ann_backend *backend = nullptr;
	is(ann_usearch_ops.create(&config, &memory, &backend), ANN_OK,
	   "quota-limited graph creates");
	float first[] = {1, 0};
	float second[] = {2, 0};
	is(insert(&ann_usearch_ops, backend, 11, first), ANN_OK,
	   "baseline insertion succeeds");
	size_t charged = quota_used(&quota);
	is(quota_set(&quota, charged), (ssize_t)charged,
	   "quota shrinks to current charge");
	is(insert(&ann_usearch_ops, backend, 22, second), ANN_OUT_OF_MEMORY,
	   "next insertion fails at quota boundary");
	is(quota_used(&quota), charged,
	   "failed insertion returns temporary charges");
	is(quota_set(&quota, 1024 * 1024), 1024 * 1024,
	   "quota limit is restored");
	struct ann_candidate found[2];
	uint32_t count = 0;
	ok(search(&ann_usearch_ops, backend, first, found, &count) == ANN_OK &&
	   count == 1 && found[0].label == 11,
	   "failed insertion leaves baseline graph intact");
	is(insert(&ann_usearch_ops, backend, 22, second), ANN_OK,
	   "insertion succeeds after quota restoration");
	ann_usearch_ops.destroy(backend);
	ok(owner.blocks == 0 && quota_used(&quota) == 0,
	   "destruction returns every quota charge");
	check_plan();
}

static void
test_generation_rebuild(void)
{
	plan(10);
	struct quota quota;
	quota_init(&quota, 8 * 1024 * 1024);
	struct ann_quota_memory old_owner, new_owner;
	ann_quota_memory_create(&old_owner, &quota);
	ann_quota_memory_create(&new_owner, &quota);
	struct ann_memory old_memory = ann_quota_memory_bind(&old_owner);
	struct ann_memory new_memory = ann_quota_memory_bind(&new_owner);
	struct ann_config config = {2, ANN_L2, nullptr};
	struct ann_backend *old_graph = nullptr, *new_graph = nullptr;
	is(ann_usearch_ops.create(&config, &old_memory, &old_graph), ANN_OK,
	   "old generation creates");
	bool inserted = true;
	for (uint64_t label = 1; label <= 32; ++label) {
		float vector[] = {(float)label, 1};
		inserted &= insert(&ann_usearch_ops, old_graph, label,
				   vector) == ANN_OK;
	}
	ok(inserted, "old generation grows to 32 entries");
	bool retired = true;
	for (uint64_t label = 2; label <= 32; label += 2) {
		struct ann_change *change = nullptr;
		retired &= ann_usearch_ops.prepare(old_graph, ANN_RETIRE,
				label, nullptr, 0, &change) == ANN_OK;
		if (change == nullptr)
			break;
		retired &= ann_usearch_ops.apply(change, nullptr) == ANN_OK;
		ann_usearch_ops.finish(change);
		retired &= ann_usearch_ops.reclaim(old_graph, label) == ANN_OK;
	}
	ok(retired, "half the old labels retire and reclaim");
	is(ann_usearch_ops.reserve(old_graph, 64), ANN_OK,
	   "capacity growth preserves the reclaimed-slot queue");
	float extra[] = {33, 1};
	is(insert(&ann_usearch_ops, old_graph, 33, extra), ANN_OK,
	   "insertion reuses a reclaimed slot after reserve");
	struct ann_backend_stats stats;
	ann_usearch_ops.stat(old_graph, &stats);
	ok(stats.live == 17 && stats.retired == 15 &&
	   stats.reclaimable == 15,
	   "successful reuse consumes exactly one reclaimed slot");
	is(ann_usearch_ops.create(&config, &new_memory, &new_graph), ANN_OK,
	   "replacement generation creates under shared quota");
	bool replayed = true;
	for (uint64_t label = 1; label <= 33; label += 2) {
		float vector[] = {(float)label, 1};
		replayed &= insert(&ann_usearch_ops, new_graph, label,
				   vector) == ANN_OK;
	}
	ok(replayed, "live versions replay into replacement generation");
	ann_usearch_ops.destroy(old_graph);
	float query[] = {31, 1};
	struct ann_candidate found[2];
	uint32_t count = 0;
	ok(old_owner.blocks == 0 &&
	   search(&ann_usearch_ops, new_graph, query, found, &count) ==
	   ANN_OK && count > 0 && found[0].label == 31,
	   "old tombstones release while replacement stays searchable");
	ann_usearch_ops.destroy(new_graph);
	ok(new_owner.blocks == 0 && quota_used(&quota) == 0,
	   "both generations return their full quota charge");
	check_plan();
}

static void
test_shared_coordinates(void)
{
	plan(11);
	struct owner coordinates = {}, graphs = {};
	struct ann_memory shared_memory = owner_memory(&coordinates);
	struct ann_memory graph_memory = owner_memory(&graphs);
	float data[] = {1, 2};
	struct ann_vector *vector =
		ann_vector_new(&shared_memory, data, 2);
	ok(vector != nullptr, "shared vector allocates once");
	size_t bytes = coordinates.bytes;
	struct ann_config config = {2, ANN_L2, nullptr};
	struct ann_backend *old = nullptr, *fresh = nullptr;
	is(ann_usearch_ops.create(&config, &graph_memory, &old), ANN_OK,
	   "old graph creates");
	is(ann_usearch_ops.create(&config, &graph_memory, &fresh), ANN_OK,
	   "new graph creates");
	struct ann_backend *backends[] = {old, fresh};
	for (auto *graph : backends) {
		struct ann_change *change = nullptr;
		enum ann_status status = ann_usearch_ops.prepare_shared(
			graph, 1, vector, &change);
		is(status, ANN_OK,
		   "shared insertion prepares without copying coordinates");
		is(ann_usearch_ops.apply(change, nullptr), ANN_OK,
		   "shared insertion applies");
		ann_usearch_ops.finish(change);
	}
	uint64_t size = 0;
	ok(ann_usearch_ops.reclaimable(fresh, &size) == 0 && size == 1,
	   "constant-time maintenance counters describe the graph");
	ann_vector_unref(vector);
	ann_usearch_ops.destroy(old);
	ok(coordinates.bytes == bytes, "new graph retains the same vector");
	struct ann_candidate found[2];
	uint32_t count = 0;
	ok(search(&ann_usearch_ops, fresh, data, found, &count) == ANN_OK &&
	   count == 1 && found[0].label == 1, "replacement remains searchable");
	ann_usearch_ops.destroy(fresh);
	ok(coordinates.bytes == 0 && graphs.bytes == 0 &&
	   coordinates.bad_free == 0 && graphs.bad_free == 0,
	   "last graph releases coordinates through their original owner");
	check_plan();
}

static void
release_borrowed(void *ctx)
{
	++*(unsigned *)ctx;
}

static void
test_borrowed_coordinates(void)
{
	plan(9);
	struct owner coordinates = {}, graphs = {};
	struct ann_memory memory = owner_memory(&coordinates);
	struct ann_memory graph_memory = owner_memory(&graphs);
	unsigned released = 0;
	unsigned char packed[] = {0xff, 0, 0, 128, 63, 0, 0, 0, 64};
	struct ann_vector *vector = ann_vector_borrow(
		&memory, packed + 1, 2, &released, release_borrowed);
	ok(vector != nullptr && ann_vector_is_borrowed(vector),
	   "unaligned packed coordinates can be borrowed");
	is(ann_vector_data(vector), packed + 1,
	   "coordinate pointer is not copied");
	is(coordinates.bytes, ann_vector_overhead(),
	   "only a lifetime header is allocated");
	struct ann_config config = {2, ANN_L2, nullptr};
	struct ann_backend *backend = nullptr;
	is(ann_usearch_ops.create(&config, &graph_memory, &backend), ANN_OK,
	   "packed graph creates");
	struct ann_change *change = nullptr;
	is(ann_usearch_ops.prepare_shared(backend, 1, vector, &change), ANN_OK,
	   "packed insertion prepares");
	is(ann_usearch_ops.apply(change, nullptr), ANN_OK,
	   "packed insertion applies");
	ann_usearch_ops.finish(change);
	ann_vector_unref(vector);
	is(released, 0, "graph retains the external owner");
	float query[] = {1, 2};
	struct ann_candidate found[2];
	uint32_t count = 0;
	ok(search(&ann_usearch_ops, backend, query, found, &count) == ANN_OK &&
	   count == 1 && found[0].distance == 0,
	   "search reads unaligned little-endian coordinates");
	ann_usearch_ops.destroy(backend);
	ok(released == 1 && coordinates.bytes == 0 && graphs.bytes == 0,
	   "last graph releases external storage exactly once");
	check_plan();
}

/** External GC grants reuse only after a label leaves every visible view. */
static bool
reclaim_label(struct ann_backend *backend, uint64_t label)
{
	return ann_usearch_ops.set_live(backend, label, false) == ANN_OK &&
		ann_usearch_ops.reclaim(backend, label) == ANN_OK;
}

static void
test_slot_reuse(void)
{
	plan(10);
	struct owner owner = {};
	struct ann_memory memory = owner_memory(&owner);
	struct ann_config config = {2, ANN_L2, nullptr};
	struct ann_backend *backend = nullptr;
	is(ann_usearch_ops.create(&config, &memory, &backend), ANN_OK,
	   "reuse backend creates");
	bool inserted = true;
	for (uint64_t label = 1; label <= 16; ++label) {
		float vector[] = {(float)label, 0};
		inserted &= insert(&ann_usearch_ops, backend, label, vector) ==
			ANN_OK;
	}
	ok(inserted, "initial graph contains sixteen distinct nodes");
	ok(reclaim_label(backend, 16), "GC grants reuse of one retired node");
	uint64_t size = 0;
	ok(ann_usearch_ops.reclaimable(backend, &size) == 1 && size == 16,
	   "one navigation slot is available without graph growth");
	float vector[] = {17, 0};
	struct ann_change *change = nullptr;
	is(ann_usearch_ops.prepare(backend, ANN_INSERT, 17, vector, 2,
				   &change), ANN_OK, "reuse reserves a slot");
	ok(ann_usearch_ops.reclaimable(backend, &size) == 0 && size == 16,
	   "prepared reuse exclusively reserves the slot");
	ann_usearch_ops.finish(change);
	ok(ann_usearch_ops.reclaimable(backend, &size) == 1 && size == 16,
	   "discarding an unapplied change returns the slot");
	bool reused = true;
	for (uint64_t label = 17; label <= 1040; ++label) {
		float next[] = {(float)label, (float)(label % 3)};
		reused &= insert(&ann_usearch_ops, backend, label, next) ==
			ANN_OK;
		struct ann_candidate found[2];
		uint32_t count = 0;
		reused &= search(&ann_usearch_ops, backend, next,
				 found, &count) == ANN_OK && count != 0 &&
			found[0].label == label &&
			found[0].distance == 0;
		reused &= reclaim_label(backend, label);
		for (uint64_t retained = 1; retained < 16; ++retained)
			reused &= ann_usearch_ops.contains(backend, retained);
		if (!reused)
			break;
	}
	ok(reused, "repeated reuse keeps new values reachable");
	ok(ann_usearch_ops.reclaimable(backend, &size) == 1 && size == 16,
	   "a thousand replacements do not accumulate graph nodes");
	ann_usearch_ops.destroy(backend);
	ok(owner.bytes == 0 && owner.bad_free == 0,
	   "reused graph returns all allocation charges");
	check_plan();
}

/** Probe failures at each allocation or traversal budget of a reused node. */
static void
test_reuse_failures(bool allocation)
{
	plan(4);
	bool consistent = true, completed = false, stopped_late = false;
	unsigned failed = 0;
	uint64_t limit = allocation ? 128 : 8192;
	for (uint64_t fail = 1; fail <= limit; ++fail) {
		struct owner owner = {};
		struct ann_memory memory = owner_memory(&owner);
		struct ann_config config = {2, ANN_L2, nullptr};
		struct ann_backend *backend = nullptr;
		if (ann_usearch_ops.create(&config, &memory, &backend) !=
		    ANN_OK) {
			consistent = false;
			break;
		}
		for (uint64_t label = 1; label <= 16; ++label) {
			float vector[] = {(float)label, 0};
			consistent &= insert(&ann_usearch_ops, backend, label,
					     vector) == ANN_OK;
		}
		consistent &= reclaim_label(backend, 8);
		if (allocation)
			owner.fail_at = owner.calls + fail;
		float vector[] = {100, 0};
		struct ann_change *change = nullptr;
		enum ann_status status = ann_usearch_ops.prepare(
			backend, ANN_INSERT, 100, vector, 2, &change);
		struct ann_search_control control = {};
		control.work_limit = fail;
		if (status == ANN_OK)
			status = ann_usearch_ops.apply(change,
				allocation ? nullptr : &control);
		if (status != ANN_OK) {
			++failed;
			enum ann_status expected = allocation ?
				ANN_OUT_OF_MEMORY : ANN_WORK_LIMIT;
			consistent &= status == expected;
			stopped_late |= allocation ? fail >= 3 : fail > 100;
			size_t calls = owner.calls;
			ann_usearch_ops.rollback(change);
			ann_usearch_ops.finish(change);
			consistent &= owner.calls == calls;
			uint64_t size = 0;
			uint64_t reusable =
				ann_usearch_ops.reclaimable(backend, &size);
			consistent &= reusable == 1 && size == 16 &&
				!ann_usearch_ops.contains(backend, 100) &&
				!ann_usearch_ops.contains(backend, 8);
			owner.fail_at = 0;
			for (uint64_t label = 1; label <= 16; ++label) {
				if (label == 8)
					continue;
				float query[] = {(float)label, 0};
				struct ann_candidate found[2];
				uint32_t count = 0;
				consistent &= search(&ann_usearch_ops, backend,
					query, found, &count) == ANN_OK &&
					count != 0 && found[0].label == label;
			}
			consistent &= insert(&ann_usearch_ops, backend, 100,
					     vector) == ANN_OK;
		} else {
			ann_usearch_ops.finish(change);
			completed = true;
		}
		ann_usearch_ops.destroy(backend);
		consistent &= owner.bytes == 0 && owner.bad_free == 0;
		if (completed || !consistent)
			break;
	}
	ok(failed != 0, "%s can interrupt node reuse",
	   allocation ? "allocation failure" : "work limit");
	ok(stopped_late, "reuse is interrupted beyond its initial preparation");
	ok(completed, "sufficient resources let reused-node update finish");
	ok(consistent, "failed reuse restores slots, labels and ownership");
	check_plan();
}

static void
test_reuse_borrowed_owner(void)
{
	plan(14);
	struct owner coordinates = {}, graphs = {};
	struct ann_memory memory = owner_memory(&coordinates);
	struct ann_memory graph_memory = owner_memory(&graphs);
	unsigned released = 0;
	unsigned char packed[] = {0, 0, 128, 63, 0, 0, 0, 64};
	struct ann_vector *vector = ann_vector_borrow(
		&memory, packed, 2, &released, release_borrowed);
	struct ann_config config = {2, ANN_L2, nullptr};
	struct ann_backend *backend = nullptr;
	is(ann_usearch_ops.create(&config, &graph_memory, &backend), ANN_OK,
	   "borrowed reuse backend creates");
	struct ann_change *change = nullptr;
	is(ann_usearch_ops.prepare_shared(backend, 1, vector, &change), ANN_OK,
	   "borrowed navigation vector prepares");
	is(ann_usearch_ops.apply(change, nullptr), ANN_OK,
	   "borrowed navigation vector applies");
	ann_usearch_ops.finish(change);
	ann_vector_unref(vector);
	ok(reclaim_label(backend, 1) && released == 0,
	   "reclaimed navigation still pins its external owner");
	float replacement[] = {4, 5};
	is(ann_usearch_ops.prepare(backend, ANN_INSERT, 2, replacement, 2,
				   &change), ANN_OK, "borrowed reuse prepares");
	is(ann_usearch_ops.apply(change, nullptr), ANN_OK,
	   "single entrypoint node can be reused");
	is(released, 0, "undo retains the old external owner until finish");
	size_t calls = graphs.calls;
	ann_usearch_ops.rollback(change);
	ann_usearch_ops.finish(change);
	ok(released == 0 && graphs.calls == calls,
	   "allocation-free rollback retains old navigation coordinates");
	ok(insert(&ann_usearch_ops, backend, 2, replacement) == ANN_OK &&
	   released == 1 && coordinates.bytes == 0,
	   "successful reuse releases the old external owner exactly once");
	ann_usearch_ops.destroy(backend);
	ok(graphs.bytes == 0 && released == 1,
	   "reused owner cannot be released twice during destruction");
	vector = ann_vector_borrow(&memory, packed, 2, &released,
				   release_borrowed);
	bool ready = ann_usearch_ops.create(&config, &graph_memory, &backend) ==
		ANN_OK && ann_usearch_ops.prepare_shared(backend, 1, vector,
						     &change) == ANN_OK &&
		ann_usearch_ops.apply(change, nullptr) == ANN_OK;
	ok(ready, "a second backend holds borrowed navigation coordinates");
	ann_usearch_ops.finish(change);
	ann_vector_unref(vector);
	ready = reclaim_label(backend, 1) &&
		ann_usearch_ops.prepare(backend, ANN_INSERT, 2, replacement, 2,
					  &change) == ANN_OK &&
		ann_usearch_ops.apply(change, nullptr) == ANN_OK;
	ok(ready, "reuse can remain applied until backend destruction");
	is(released, 1, "unfinished reuse still holds its undo owner");
	ann_usearch_ops.destroy(backend);
	ok(released == 2 && coordinates.bytes == 0 && graphs.bytes == 0,
	   "destruction releases both the applied entry and its undo owner");
	check_plan();
}

/** Reader-owned state; scratch allocation never touches the writer's owner. */
struct snapshot_reader {
	/** Frozen graph shared with the owner cord. */
	struct ann_backend *view;
	/** Search and allocator invariants checked after joining. */
	bool success;
};

/** A frozen graph is traversable while its source reuses physical slots. */
static void *
snapshot_reader_f(void *opaque)
{
	auto *reader = (struct snapshot_reader *)opaque;
	struct owner scratch = {};
	struct ann_memory memory = owner_memory(&scratch);
	struct ann_filter filter = {accept_odd_label, nullptr};
	float query[] = {0, 0};
	for (unsigned i = 0; i < 64; ++i) {
		struct ann_search_control control = {};
		control.work_limit = 1000000;
		struct ann_search_opts opts = {};
		opts.candidate_limit = 2;
		opts.query_dimension = 2;
		opts.filter = &filter;
		opts.control = &control;
		opts.memory = &memory;
		struct ann_candidate found[2];
		uint32_t count = 0;
		reader->success &= ann_usearch_ops.search(reader->view, query,
			&opts, found, &count) == ANN_OK && count == 2 &&
			found[0].label == 1 && found[0].distance == 0 &&
			found[1].label == 3 && found[1].distance == 4;
		reader->success &= scratch.bytes == 0 && scratch.bad_free == 0;
	}
	return nullptr;
}

static void
test_concurrent_snapshot(void)
{
	plan(6);
	struct owner owner = {}, views = {};
	struct ann_memory memory = owner_memory(&owner);
	struct ann_memory view_memory = owner_memory(&views);
	struct ann_config config = {2, ANN_L2, nullptr};
	struct ann_backend *backend = nullptr, *view = nullptr;
	bool ready = ann_usearch_ops.create(&config, &memory, &backend) ==
		ANN_OK;
	for (unsigned i = 0; i < 512 && ready; ++i) {
		float vector[] = {(float)i, 0};
		ready = insert(&ann_usearch_ops, backend, i + 1, vector) ==
			ANN_OK;
	}
	ok(ready, "source graph spans multiple directory pages");
	size_t calls = owner.calls;
	ok(ann_usearch_ops.snapshot(backend, &view_memory, &view) == ANN_OK &&
	   owner.calls == calls, "large snapshot does not walk graph nodes");
	struct snapshot_reader reader = {view, true};
	pthread_t thread;
	int rc = pthread_create(&thread, nullptr, snapshot_reader_f, &reader);
	is(rc, 0, "reader starts on a separate thread");
	bool changed = true;
	uint64_t label = 1;
	for (unsigned i = 0; i < 128; ++i) {
		float vector[] = {1000 + (float)i, 0};
		changed &= reclaim_label(backend, label);
		label = 10000 + i;
		changed &= insert(&ann_usearch_ops, backend, label, vector) ==
			ANN_OK;
	}
	ann_usearch_ops.destroy(backend);
	if (rc == 0)
		pthread_join(thread, nullptr);
	ok(changed, "writer reuses slots and destroys its graph");
	ok(reader.success, "reader keeps frozen results and private scratch");
	ann_usearch_ops.destroy(view);
	ok(owner.bytes == 0 && views.bytes == 0 && owner.bad_free == 0 &&
	   views.bad_free == 0, "concurrent snapshot releases both owners");
	check_plan();
}

int
main(void)
{
	plan(24);
	test_lifecycle(ann_backend_find("flat"));
	test_lifecycle(&ann_usearch_ops);
	test_filtered_topk(ann_backend_find("flat"));
	test_filtered_topk(&ann_usearch_ops);
	test_ranked_topk();
	test_failed_insert();
	test_interrupted_search();
	test_failed_search();
	test_interrupted_insert();
	test_late_cancel();
	test_native_slot_limit();
	test_rollback_no_alloc();
	test_growth_and_retire();
	test_two_owners();
	test_shared_memtx_quota();
	test_quota_exhaustion();
	test_generation_rebuild();
	test_shared_coordinates();
	test_borrowed_coordinates();
	test_slot_reuse();
	test_reuse_failures(true);
	test_reuse_failures(false);
	test_reuse_borrowed_owner();
	test_concurrent_snapshot();
	return check_plan();
}
