#include "memtx_index_read_view.h"

void
memtx_index_read_view_create(struct memtx_index_read_view *rv,
			     const struct index_read_view_vtab *vtab,
			     struct index_def *def)
{
	index_read_view_create(&rv->base, vtab, def);
	rv->dump_sort_data = NULL;
}
