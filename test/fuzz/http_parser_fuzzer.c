#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include "http_parser/http_parser.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct http_parser parser;
	char *buf = (char *)data;
	http_parser_create(&parser);
	parser.hdr_name = (char *)calloc(size, sizeof(char));
	if (parser.hdr_name == NULL)
		return 0;
	char *end_buf = buf + size;
	http_parse_header_line(&parser, &buf, end_buf, size);
	free(parser.hdr_name);

	return 0;
}
