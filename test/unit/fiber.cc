#include "memory.h"
#include "fiber.h"
int main()
{
	memory_init();
	fiber_init();
	fiber_free();
	memory_free();
	return 0;
}
