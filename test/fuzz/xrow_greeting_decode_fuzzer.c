#include <stdint.h>

#include "box/xrow.h"
#include "box/iproto_constants.h"
#include "trivia/util.h"

void
cord_on_yield(void) {}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	const char *d = (const char *)data;
	const char *end = (const char *)data + size;
	if (mp_check(&d, end) != 0)
		return -1;

	if (size < IPROTO_GREETING_SIZE + 1)
		return -1;

	char *greetingbuf = xcalloc(size + 1, sizeof(char));
	if (greetingbuf == NULL)
		return 0;
	memcpy(greetingbuf, data, size);
	greetingbuf[size] = '\0';

	struct greeting greeting = {0};
	greeting_decode(greetingbuf, &greeting);

	free(greetingbuf);

	return 0;
}
