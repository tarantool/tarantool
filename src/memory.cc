#include "memory.h"

__thread struct slab_cache *slabc_runtime;

void
memory_init()
{
	static struct slab_cache slab_cache;
	slabc_runtime = &slab_cache;
	slab_cache_create(slabc_runtime);
}

void
memory_free()
{
	slab_cache_destroy(slabc_runtime);
}
