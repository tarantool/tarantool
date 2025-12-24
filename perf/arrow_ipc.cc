#include <benchmark/benchmark.h>

#include "arrow_ipc.h"

#include "fiber.h"
#include "memory.h"

static void
arrow_schema_destroy(struct ArrowSchema *schema)
{
	for (int i = 0; i < schema->n_children; i++) {
		struct ArrowSchema *child = schema->children[i];
		if (child->release != NULL)
			child->release(child);
		free(child);
	}
	free(schema->children);
	schema->release = NULL;
}

static void
arrow_array_destroy(struct ArrowArray *array)
{
	for (int i = 0; i < array->n_children; i++) {
		struct ArrowArray *child = array->children[i];
		if (child->release != NULL)
			child->release(child);
	}
	free(array->children);
	for (int i = 0; i < array->n_buffers; i++)
		free((void *)array->buffers[i]);
	free(array->buffers);
	array->release = NULL;
}

static const int bench_data_count = 16384;

static void
bench_data(struct ArrowArray *array, struct ArrowSchema *schema)
{
	*schema = {
		.format = "+s",
		.name = NULL,
		.metadata = NULL,
		.flags = 0,
		.n_children = 1,
		.children = (struct ArrowSchema **)
				xcalloc(1, sizeof(struct ArrowSchema *)),
		.dictionary = NULL,
		.release = arrow_schema_destroy,
		.private_data = NULL,
	};
	schema->children[0] =
		(struct ArrowSchema *)xmalloc(sizeof(struct ArrowSchema));
	*schema->children[0] = {
		.format = "L",
		.name = "test",
		.metadata = NULL,
		.flags = 0,
		.n_children = 0,
		.children = NULL,
		.dictionary = NULL,
		.release = arrow_schema_destroy,
		.private_data = NULL,
	};

	*array = {
		.length = bench_data_count,
		.null_count = 0,
		.offset = 0,
		.n_buffers = 1,
		.n_children = 1,
		.buffers = (const void **)xcalloc(1, sizeof(void *)),
		.children = (struct ArrowArray **)
				xcalloc(1, sizeof(struct ArrowArray *)),
		.dictionary = NULL,
		.release = arrow_array_destroy,
		.private_data = NULL,
	};
	array->children[0] =
		(struct ArrowArray *)xmalloc(sizeof(struct ArrowArray));
	*array->children[0] = {
		.length = bench_data_count,
		.null_count = 0,
		.offset = 0,
		.n_buffers = 2,
		.n_children = 0,
		.buffers = (const void **)xcalloc(2, sizeof(void *)),
		.children = NULL,
		.dictionary = NULL,
		.release = arrow_array_destroy,
		.private_data = NULL,
	};

	uint64_t *data = (uint64_t *)xcalloc(bench_data_count, sizeof(*data));
	for (int i = 0; i < bench_data_count; i++)
		data[i] = i;
	array->children[0]->buffers[1] = (const void *)data;
}

static void
bench_encode(benchmark::State &state)
{
	struct ArrowArray array;
	struct ArrowSchema schema;
	struct region *gc = &fiber()->gc;

	bench_data(&array, &schema);

	for (auto _ : state) {
		const char *data;
		const char *data_end;

		RegionGuard region_guard(gc);
		if (arrow_ipc_encode(&array, &schema, gc, &data,
				     &data_end) != 0) {
			diag_log();
			panic("failed to encode");
		}
	}

	state.SetItemsProcessed(state.iterations() * bench_data_count);
	array.release(&array);
	schema.release(&schema);
}

static void
bench_decode(benchmark::State &state)
{
	struct ArrowArray array;
	struct ArrowSchema schema;
	const char *data;
	const char *data_end;
	struct region *gc = &fiber()->gc;

	bench_data(&array, &schema);
	RegionGuard region_guard(gc);
	if (arrow_ipc_encode(&array, &schema, gc, &data,
			     &data_end) != 0) {
		diag_log();
		panic("failed to encode");
	}

	for (auto _ : state) {
		struct ArrowArray array;
		struct ArrowSchema schema;

		if (arrow_ipc_decode(&array, &schema, data, data_end) != 0) {
			diag_log();
			panic("failed to decode");
		}
		array.release(&array);
		schema.release(&schema);
	}

	state.SetItemsProcessed(state.iterations() * bench_data_count);
	array.release(&array);
	schema.release(&schema);
}

BENCHMARK(bench_decode);
BENCHMARK(bench_encode);

int
main(int argc, char **argv)
{
	memory_init();
	fiber_init(fiber_c_invoke);

	::benchmark::Initialize(&argc, argv);
	::benchmark::RunSpecifiedBenchmarks();

	fiber_free();
	memory_free();
	return 0;
}
