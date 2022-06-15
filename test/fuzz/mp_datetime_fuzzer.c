#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "msgpuck.h"
#include "mp_extension_types.h"
#include "mp_datetime.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct datetime ret;
	memset(&ret, 0, sizeof(ret));
	if (datetime_unpack((const char **)&data, size, &ret) == NULL)
		return 0;
	assert(datetime_validate(&ret));
	return 0;
}

void
cord_on_yield(void) {}
