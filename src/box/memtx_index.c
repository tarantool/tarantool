#include "memtx_index.h"

void
memtx_index_create(struct memtx_index *index, struct memtx_engine *memtx,
		   const struct index_vtab *vtab, struct index_def *def)
{
	index_create(&index->base, (struct engine *)memtx, vtab, def);
	index->built_presorted = false;
	index->build_presorted = NULL;
}
