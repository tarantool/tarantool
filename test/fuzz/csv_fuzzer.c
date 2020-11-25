#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "csv/csv.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct csv csv;
	csv_create(&csv);
	char *buf = calloc(size + 1, sizeof(char));
	if (buf == NULL)
		return 0;
	memcpy(buf, data, size);
	buf[size] = '\0';
	char *end = buf + size;
	csv_parse_chunk(&csv, buf, end);
	csv_finish_parsing(&csv);
	csv_destroy(&csv);
	free(buf);

	return 0;
}
