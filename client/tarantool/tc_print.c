
/*
 * Copyright (C) 2012 Mail.RU
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <ctype.h>

#include <connector/c/include/tarantool/tnt.h>

#include "client/tarantool/tc_print.h"

void tc_print_tuple(struct tnt_tuple *tu)
{
	struct tnt_iter ifl;
	tnt_iter(&ifl, tu);
	printf("[");
	while (tnt_next(&ifl)) {
		if (TNT_IFIELD_IDX(&ifl) != 0)
			printf(", ");
		char *data = TNT_IFIELD_DATA(&ifl);
		uint32_t size = TNT_IFIELD_SIZE(&ifl);
		if (!isprint(data[0]) && (size == 4 || size == 8)) {
			if (size == 4) {
				uint32_t i = *((uint32_t*)data);
				printf("%"PRIu32, i);
			} else {
				uint64_t i = *((uint64_t*)data);
				printf("%"PRIu64, i);
			}
		} else {
			printf("'%-.*s'", size, data);
		}
	}
	if (ifl.status == TNT_ITER_FAIL)
		printf("<parsing error>");
	printf("]\n");
	tnt_iter_free(&ifl);
}

void tc_print_list(struct tnt_list *l)
{
	struct tnt_iter it;
	tnt_iter_list(&it, l);
	while (tnt_next(&it)) {
		struct tnt_tuple *tu = TNT_ILIST_TUPLE(&it);
		tc_print_tuple(tu);
	}
	tnt_iter_free(&it);
}
