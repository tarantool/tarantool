#include <string.h>
#include <stdlib.h>
#include "decimal.h"
#include "trivia/util.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char *buf = xcalloc(size + 1, sizeof(char));
	if (!buf)
		return 0;
	memcpy(buf, data, size);
	buf[size] = '\0';
	decimal_t d;
	decimal_from_string(&d, buf);
	free(buf);

	return 0;
}
