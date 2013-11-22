/*
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
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <ctype.h>
#include <wctype.h>

#include <unistd.h>
#include <errno.h>

#if 0
#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_xlog.h>
#include <connector/c/include/tarantool/tnt_rpl.h>
#endif

#include "client/tarantool/tc_opt.h"
#include "client/tarantool/tc_admin.h"
#include "client/tarantool/tc.h"
#include "client/tarantool/tc_print.h"
#include "client/tarantool/tc_query.h"

extern struct tc tc;

/*##################### Base printing functions #####################*/
void tc_print_buf(char *buf, size_t size) {
	printf("%-.*s", (int)size, buf);
	fflush(stdout);
}

void tc_printf(char *fmt, ...) {
	char *str;
	va_list args;
	va_start(args, fmt);
	ssize_t str_len = vasprintf(&str, fmt, args);
	if (str_len == -1)
		tc_error("Error in vasprintf - %d", errno);
	ssize_t stat = write(tc.pager_fd, str, str_len);
	if (stat == -1)
		tc_error("Can't write into pager - %d", errno);
	va_end(args);
	return;
}

static int tc_str_valid(char *data, uint32_t size) {
	int length;
	wchar_t dest;

	mbtowc(NULL, NULL, 0);
	while ((length = mbtowc(&dest, data, size)) > -1 && size > 0) {
		if (length == 0)
			++length;
		data += length;
		size -= length;
	}
	if (length == -1)
		return 0;
	return 1;
}

void tc_print_string(char *data, uint32_t size, char lua)
{
	if (tc_str_valid(data, size)) {
		wchar_t dest;
		int length;
		mbtowc (NULL, NULL, 0);
		while ((length = mbtowc(&dest, data, size)) > -1 && size > 0) {
			if (dest >= 0x20) {
				if (lua)
					switch (dest) {
					case '\'':
						tc_printf("\\\'");
						break;
					case '\\':
						tc_printf("\\\\");
						break;
					default:
						tc_printf ("%lc", dest);
					}
				else
					tc_printf ("%lc", dest);
			}
			else {
				switch (dest) {
				case 0x00:
					tc_printf("\\0");
					length++;
					/* Cause of mbtowc returns 0 when \0 */
					break;
				case 0x07:
					tc_printf("\\a");
					break;
				case 0x08:
					tc_printf("\\b");
					break;
				case 0x09:
					tc_printf("\\t");
					break;
				case 0x0A:
					tc_printf("\\n");
					break;
				case 0x0B:
					tc_printf("\\v");
					break;
				case 0x0C:
					tc_printf("\\f");
					break;
				case 0x0D:
					tc_printf("\\r");
					break;
				default:
					tc_printf("\\x%02lX",
						(unsigned long int)dest);
					break;
				}
			}
			size -= length;
			data += length;
		}
	}
	else {
		while (size-- > 0) {
			tc_printf("\\x%02X", (unsigned char)*data);
			data++;
		}
	}
}

#if 0

void tc_print_fields(struct tnt_tuple *tu)
{
	struct tnt_iter ifl;
	tnt_iter(&ifl, tu);
	while (tnt_next(&ifl)) {
		if (TNT_IFIELD_IDX(&ifl) != 0)
			tc_printf(", ");
		char *data = TNT_IFIELD_DATA(&ifl);
		uint32_t size = TNT_IFIELD_SIZE(&ifl);
		switch (size) {
		case 4:
			tc_printf("%"PRIu32, *((uint32_t*)data));
			break;
		case 8:
			tc_printf("%"PRIu64, *((uint64_t*)data));
			break;
		default:
			tc_printf("'");
			tc_print_string(data, size, 0);
			tc_printf("'");
		}
	}
	if (ifl.status == TNT_ITER_FAIL)
		tc_printf("<parsing error>");
	tnt_iter_free(&ifl);
}

void tc_print_tuple(struct tnt_tuple *tu)
{
	tc_printf("[");
	tc_print_fields(tu);
	tc_printf("]\n");
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

void tc_print_lua_field(char *data, uint32_t size, char string)
{
	if (string)
		goto _string;
	switch (size){
	case 4:
		tc_printf("%"PRIu32, *((uint32_t*)data));
		break;
	case 8:
		tc_printf("%"PRIu64"LL", *((uint64_t*)data));
		break;
	default:
_string:
		tc_printf("\'");
		tc_print_string(data, size, 1);
		tc_printf("\'");
	}
}

void tc_print_lua_fields(struct tnt_tuple *tu)
{
	struct tnt_iter ifl;
	tnt_iter(&ifl, tu);
	while (tnt_next(&ifl)) {
		if ((TNT_IFIELD_IDX(&ifl)) != 0)
			tc_printf(", ");
		char *data = TNT_IFIELD_DATA(&ifl);
		uint32_t size = TNT_IFIELD_SIZE(&ifl);
		tc_print_lua_field(data, size, tc.opt.str_instead_int);
	}
	if (ifl.status == TNT_ITER_FAIL)
		tc_printf("<parsing error>");
	tnt_iter_free(&ifl);
}

void tc_print_lua_tuple(struct tnt_tuple *tu)
{
	tc_printf("{");
	tc_print_lua_fields(tu);
	tc_printf("}");
}
#endif
