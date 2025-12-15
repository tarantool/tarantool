#include <stddef.h>

int mprotect(void *addr, size_t len, int prot)
{
	return -1;
}
