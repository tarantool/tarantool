#include "memory.h"

struct slab_cache slabc_runtime;

void
memory_init()
{
	slab_cache_create(&slabc_runtime);
}

void
memory_free()
{
	slab_cache_destroy(&slabc_runtime);
}
