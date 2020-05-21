/*
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "unit.h"
#include "crc32.h"

static void
test_alignment(void)
{
	header();
	plan(4);

	unsigned long buf[1024];
	char *str = (char *)buf;

	strcpy(str, "1234567891234567");
	uint32_t crc = crc32_calc(0, str, strlen(str));
	is(crc, 3333896965, "aligned crc32 buffer without a tail");

	strcpy(str, "12345678912345678");
	crc = crc32_calc(0, str, strlen(str));
	is(crc, 2400039513, "aligned crc32 buffer with a tail");

	crc = crc32_calc(0, str + 2, strlen(str) - 2);
	is(crc, 984331636, "not aligned crc32 buffer with a tail");

	strcpy(str, "1234");
	crc = crc32_calc(0, str + 2, strlen(str) - 2);
	is(crc, 2211472564, "not aligned buffer less than a word");

	check_plan();
	footer();
}

int
main(void)
{
	crc32_init();

	header();
	plan(1);
	test_alignment();
	int rc = check_plan();
	footer();
	return rc;
}
