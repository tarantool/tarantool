#include <stdlib.h>
#include <string.h>
#include "datetime.h"
#include "trivia/util.h"

void
cord_on_yield(void) {}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char *buf = xcalloc(size + 1, sizeof(char));
	if (buf == NULL)
		return 0;
	memcpy(buf, data, size);
	buf[size] = '\0';
	struct datetime date_expected;
	datetime_parse_full(&date_expected, buf, size);
	free(buf);

	return 0;
}
