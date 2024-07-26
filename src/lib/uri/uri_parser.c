
#line 1 "src/lib/uri/uri_parser.rl"
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include "uri_parser.h"
#include "uri.h"
#include <trivia/util.h> /* SNPRINT */
#include <string.h>
#include <stdio.h> /* snprintf */
int
uri_raw_parse(struct uri_raw *uri, const char *p)
{
	const char *pe = p + strlen(p);
	const char *eof = pe;
	int cs;
	memset(uri, 0, sizeof(*uri));

	if (p == pe)
		return -1;

	const char *s = NULL, *login = NULL, *scheme = NULL;
	size_t login_len = 0, scheme_len = 0;

	
#line 54 "src/lib/uri/uri_parser.c"
static const int uri_start = 149;
static const int uri_first_final = 149;
static const int uri_error = 0;

static const int uri_en_main = 149;


#line 62 "src/lib/uri/uri_parser.c"
	{
	cs = uri_start;
	}

#line 67 "src/lib/uri/uri_parser.c"
	{
	if ( p == pe )
		goto _test_eof;
	switch ( cs )
	{
case 149:
	switch( (*p) ) {
		case 33: goto tr156;
		case 35: goto tr157;
		case 37: goto tr158;
		case 46: goto tr159;
		case 47: goto tr160;
		case 59: goto tr156;
		case 61: goto tr156;
		case 63: goto tr162;
		case 64: goto st221;
		case 91: goto st53;
		case 95: goto tr156;
		case 117: goto tr165;
		case 126: goto tr156;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto tr156;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto tr164;
		} else if ( (*p) >= 65 )
			goto tr164;
	} else
		goto tr161;
	goto st0;
st0:
cs = 0;
	goto _out;
tr156:
#line 145 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 101 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st150;
st150:
	if ( ++p == pe )
		goto _test_eof150;
case 150:
#line 114 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st150;
		case 35: goto tr166;
		case 37: goto st6;
		case 47: goto tr168;
		case 58: goto tr169;
		case 61: goto st150;
		case 63: goto tr170;
		case 64: goto tr171;
		case 95: goto st150;
		case 126: goto st150;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st150;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st150;
	} else
		goto st150;
	goto st0;
tr157:
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 196 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st151;
tr166:
#line 102 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 196 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st151;
tr177:
#line 72 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 73 "src/lib/uri/uri_parser.rl"
	{ uri->query = s; uri->query_len = p - s; }
#line 196 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st151;
tr179:
#line 73 "src/lib/uri/uri_parser.rl"
	{ uri->query = s; uri->query_len = p - s; }
#line 196 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st151;
tr182:
#line 139 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 140 "src/lib/uri/uri_parser.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 196 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st151;
tr193:
#line 140 "src/lib/uri/uri_parser.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 196 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st151;
tr202:
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 196 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st151;
tr220:
#line 109 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;
			   uri->host_hint = 1; }
#line 102 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 196 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st151;
tr239:
#line 120 "src/lib/uri/uri_parser.rl"
	{
			/*
			 * This action is also called for path_* terms.
			 * I absolutely have no idea why.
			 */
			if (uri->host_hint != 3) {
				uri->host_hint = 3;
				uri->host = URI_HOST_UNIX;
				uri->host_len = strlen(URI_HOST_UNIX);
				uri->service = s; uri->service_len = p - s;
				/* a workaround for grammar limitations */
				uri->path = NULL;
				uri->path_len = 0;
			};
		}
#line 171 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 196 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st151;
tr245:
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 120 "src/lib/uri/uri_parser.rl"
	{
			/*
			 * This action is also called for path_* terms.
			 * I absolutely have no idea why.
			 */
			if (uri->host_hint != 3) {
				uri->host_hint = 3;
				uri->host = URI_HOST_UNIX;
				uri->host_len = strlen(URI_HOST_UNIX);
				uri->service = s; uri->service_len = p - s;
				/* a workaround for grammar limitations */
				uri->path = NULL;
				uri->path_len = 0;
			};
		}
#line 196 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st151;
st151:
	if ( ++p == pe )
		goto _test_eof151;
case 151:
#line 259 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto tr172;
		case 37: goto tr173;
		case 61: goto tr172;
		case 95: goto tr172;
		case 124: goto tr172;
		case 126: goto tr172;
	}
	if ( (*p) < 63 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto tr172;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr172;
	} else
		goto tr172;
	goto st0;
tr172:
#line 76 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st152;
st152:
	if ( ++p == pe )
		goto _test_eof152;
case 152:
#line 285 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st152;
		case 37: goto st1;
		case 61: goto st152;
		case 95: goto st152;
		case 124: goto st152;
		case 126: goto st152;
	}
	if ( (*p) < 63 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st152;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st152;
	} else
		goto st152;
	goto st0;
tr173:
#line 76 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st1;
st1:
	if ( ++p == pe )
		goto _test_eof1;
case 1:
#line 311 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 37: goto st152;
		case 117: goto st2;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st152;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st152;
	} else
		goto st152;
	goto st0;
st2:
	if ( ++p == pe )
		goto _test_eof2;
case 2:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st3;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st3;
	} else
		goto st3;
	goto st0;
st3:
	if ( ++p == pe )
		goto _test_eof3;
case 3:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st4;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st4;
	} else
		goto st4;
	goto st0;
st4:
	if ( ++p == pe )
		goto _test_eof4;
case 4:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st5;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st5;
	} else
		goto st5;
	goto st0;
st5:
	if ( ++p == pe )
		goto _test_eof5;
case 5:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st152;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st152;
	} else
		goto st152;
	goto st0;
tr158:
#line 145 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 101 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st6;
st6:
	if ( ++p == pe )
		goto _test_eof6;
case 6:
#line 387 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 37: goto st150;
		case 117: goto st7;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st150;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st150;
	} else
		goto st150;
	goto st0;
st7:
	if ( ++p == pe )
		goto _test_eof7;
case 7:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st8;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st8;
	} else
		goto st8;
	goto st0;
st8:
	if ( ++p == pe )
		goto _test_eof8;
case 8:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st9;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st9;
	} else
		goto st9;
	goto st0;
st9:
	if ( ++p == pe )
		goto _test_eof9;
case 9:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st10;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st10;
	} else
		goto st10;
	goto st0;
st10:
	if ( ++p == pe )
		goto _test_eof10;
case 10:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st150;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st150;
	} else
		goto st150;
	goto st0;
tr168:
#line 102 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st153;
tr184:
#line 139 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 140 "src/lib/uri/uri_parser.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st153;
tr194:
#line 140 "src/lib/uri/uri_parser.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st153;
tr203:
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st153;
tr221:
#line 109 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;
			   uri->host_hint = 1; }
#line 102 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st153;
st153:
	if ( ++p == pe )
		goto _test_eof153;
case 153:
#line 490 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st153;
		case 35: goto tr157;
		case 37: goto st11;
		case 61: goto st153;
		case 63: goto tr162;
		case 95: goto st153;
		case 124: goto st153;
		case 126: goto st153;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st153;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st153;
	} else
		goto st153;
	goto st0;
st11:
	if ( ++p == pe )
		goto _test_eof11;
case 11:
	switch( (*p) ) {
		case 37: goto st153;
		case 117: goto st12;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st153;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st153;
	} else
		goto st153;
	goto st0;
st12:
	if ( ++p == pe )
		goto _test_eof12;
case 12:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st13;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st13;
	} else
		goto st13;
	goto st0;
st13:
	if ( ++p == pe )
		goto _test_eof13;
case 13:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st14;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st14;
	} else
		goto st14;
	goto st0;
st14:
	if ( ++p == pe )
		goto _test_eof14;
case 14:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st15;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st15;
	} else
		goto st15;
	goto st0;
st15:
	if ( ++p == pe )
		goto _test_eof15;
case 15:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st153;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st153;
	} else
		goto st153;
	goto st0;
tr162:
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 196 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st154;
tr170:
#line 102 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 196 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st154;
tr186:
#line 139 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 140 "src/lib/uri/uri_parser.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 196 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st154;
tr196:
#line 140 "src/lib/uri/uri_parser.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 196 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st154;
tr204:
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 196 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st154;
tr224:
#line 109 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;
			   uri->host_hint = 1; }
#line 102 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 196 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st154;
tr240:
#line 120 "src/lib/uri/uri_parser.rl"
	{
			/*
			 * This action is also called for path_* terms.
			 * I absolutely have no idea why.
			 */
			if (uri->host_hint != 3) {
				uri->host_hint = 3;
				uri->host = URI_HOST_UNIX;
				uri->host_len = strlen(URI_HOST_UNIX);
				uri->service = s; uri->service_len = p - s;
				/* a workaround for grammar limitations */
				uri->path = NULL;
				uri->path_len = 0;
			};
		}
#line 171 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 196 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st154;
tr247:
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 120 "src/lib/uri/uri_parser.rl"
	{
			/*
			 * This action is also called for path_* terms.
			 * I absolutely have no idea why.
			 */
			if (uri->host_hint != 3) {
				uri->host_hint = 3;
				uri->host = URI_HOST_UNIX;
				uri->host_len = strlen(URI_HOST_UNIX);
				uri->service = s; uri->service_len = p - s;
				/* a workaround for grammar limitations */
				uri->path = NULL;
				uri->path_len = 0;
			};
		}
#line 196 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st154;
st154:
	if ( ++p == pe )
		goto _test_eof154;
case 154:
#line 688 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto tr176;
		case 35: goto tr177;
		case 37: goto tr178;
		case 61: goto tr176;
		case 95: goto tr176;
		case 124: goto tr176;
		case 126: goto tr176;
	}
	if ( (*p) < 63 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto tr176;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr176;
	} else
		goto tr176;
	goto st0;
tr176:
#line 72 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st155;
st155:
	if ( ++p == pe )
		goto _test_eof155;
case 155:
#line 715 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st155;
		case 35: goto tr179;
		case 37: goto st16;
		case 61: goto st155;
		case 95: goto st155;
		case 124: goto st155;
		case 126: goto st155;
	}
	if ( (*p) < 63 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st155;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st155;
	} else
		goto st155;
	goto st0;
tr178:
#line 72 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st16;
st16:
	if ( ++p == pe )
		goto _test_eof16;
case 16:
#line 742 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 37: goto st155;
		case 117: goto st17;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st155;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st155;
	} else
		goto st155;
	goto st0;
st17:
	if ( ++p == pe )
		goto _test_eof17;
case 17:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st18;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st18;
	} else
		goto st18;
	goto st0;
st18:
	if ( ++p == pe )
		goto _test_eof18;
case 18:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st19;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st19;
	} else
		goto st19;
	goto st0;
st19:
	if ( ++p == pe )
		goto _test_eof19;
case 19:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st20;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st20;
	} else
		goto st20;
	goto st0;
st20:
	if ( ++p == pe )
		goto _test_eof20;
case 20:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st155;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st155;
	} else
		goto st155;
	goto st0;
tr169:
#line 146 "src/lib/uri/uri_parser.rl"
	{ login = s; login_len = p - s; }
#line 102 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st156;
tr261:
#line 146 "src/lib/uri/uri_parser.rl"
	{ login = s; login_len = p - s; }
#line 109 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;
			   uri->host_hint = 1; }
#line 102 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st156;
st156:
	if ( ++p == pe )
		goto _test_eof156;
case 156:
#line 827 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto tr181;
		case 35: goto tr182;
		case 37: goto tr183;
		case 47: goto tr184;
		case 59: goto tr181;
		case 61: goto tr181;
		case 63: goto tr186;
		case 64: goto tr187;
		case 95: goto tr181;
		case 126: goto tr181;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto tr181;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto tr188;
		} else if ( (*p) >= 65 )
			goto tr188;
	} else
		goto tr185;
	goto st0;
tr181:
#line 149 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st21;
st21:
	if ( ++p == pe )
		goto _test_eof21;
case 21:
#line 860 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st21;
		case 37: goto st22;
		case 59: goto st21;
		case 61: goto st21;
		case 64: goto tr23;
		case 95: goto st21;
		case 126: goto st21;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st21;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st21;
		} else if ( (*p) >= 65 )
			goto st21;
	} else
		goto st21;
	goto st0;
tr183:
#line 149 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st22;
st22:
	if ( ++p == pe )
		goto _test_eof22;
case 22:
#line 890 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 37: goto st21;
		case 117: goto st23;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st21;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st21;
	} else
		goto st21;
	goto st0;
st23:
	if ( ++p == pe )
		goto _test_eof23;
case 23:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st24;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st24;
	} else
		goto st24;
	goto st0;
st24:
	if ( ++p == pe )
		goto _test_eof24;
case 24:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st25;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st25;
	} else
		goto st25;
	goto st0;
st25:
	if ( ++p == pe )
		goto _test_eof25;
case 25:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st26;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st26;
	} else
		goto st26;
	goto st0;
st26:
	if ( ++p == pe )
		goto _test_eof26;
case 26:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st21;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st21;
	} else
		goto st21;
	goto st0;
tr23:
#line 150 "src/lib/uri/uri_parser.rl"
	{ uri->password = s; uri->password_len = p - s; }
#line 154 "src/lib/uri/uri_parser.rl"
	{ uri->login = login; uri->login_len = login_len; }
	goto st27;
tr171:
#line 146 "src/lib/uri/uri_parser.rl"
	{ login = s; login_len = p - s; }
#line 154 "src/lib/uri/uri_parser.rl"
	{ uri->login = login; uri->login_len = login_len; }
	goto st27;
tr187:
#line 149 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 150 "src/lib/uri/uri_parser.rl"
	{ uri->password = s; uri->password_len = p - s; }
#line 154 "src/lib/uri/uri_parser.rl"
	{ uri->login = login; uri->login_len = login_len; }
	goto st27;
st27:
	if ( ++p == pe )
		goto _test_eof27;
case 27:
#line 980 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto tr28;
		case 37: goto tr29;
		case 46: goto tr30;
		case 47: goto tr31;
		case 59: goto tr28;
		case 61: goto tr28;
		case 91: goto st53;
		case 95: goto tr28;
		case 117: goto tr34;
		case 126: goto tr28;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto tr28;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto tr28;
		} else if ( (*p) >= 65 )
			goto tr28;
	} else
		goto tr32;
	goto st0;
tr28:
#line 101 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st157;
st157:
	if ( ++p == pe )
		goto _test_eof157;
case 157:
#line 1013 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st157;
		case 35: goto tr166;
		case 37: goto st28;
		case 47: goto tr168;
		case 58: goto tr190;
		case 61: goto st157;
		case 63: goto tr170;
		case 95: goto st157;
		case 126: goto st157;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st157;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st157;
	} else
		goto st157;
	goto st0;
tr29:
#line 101 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st28;
st28:
	if ( ++p == pe )
		goto _test_eof28;
case 28:
#line 1042 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 37: goto st157;
		case 117: goto st29;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st157;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st157;
	} else
		goto st157;
	goto st0;
st29:
	if ( ++p == pe )
		goto _test_eof29;
case 29:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st30;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st30;
	} else
		goto st30;
	goto st0;
st30:
	if ( ++p == pe )
		goto _test_eof30;
case 30:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st31;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st31;
	} else
		goto st31;
	goto st0;
st31:
	if ( ++p == pe )
		goto _test_eof31;
case 31:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st32;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st32;
	} else
		goto st32;
	goto st0;
st32:
	if ( ++p == pe )
		goto _test_eof32;
case 32:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st157;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st157;
	} else
		goto st157;
	goto st0;
tr190:
#line 102 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st158;
tr223:
#line 109 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;
			   uri->host_hint = 1; }
#line 102 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st158;
st158:
	if ( ++p == pe )
		goto _test_eof158;
case 158:
#line 1123 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 35: goto tr182;
		case 47: goto tr184;
		case 63: goto tr186;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr191;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr192;
	} else
		goto tr192;
	goto st0;
tr191:
#line 139 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st159;
st159:
	if ( ++p == pe )
		goto _test_eof159;
case 159:
#line 1146 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 35: goto tr193;
		case 47: goto tr194;
		case 63: goto tr196;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st159;
	goto st0;
tr192:
#line 139 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st160;
st160:
	if ( ++p == pe )
		goto _test_eof160;
case 160:
#line 1163 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 35: goto tr193;
		case 47: goto tr194;
		case 63: goto tr196;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st160;
	} else if ( (*p) >= 65 )
		goto st160;
	goto st0;
tr30:
#line 101 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 159 "src/lib/uri/uri_parser.rl"
	{ s = p;}
	goto st161;
st161:
	if ( ++p == pe )
		goto _test_eof161;
case 161:
#line 1185 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st157;
		case 35: goto tr166;
		case 37: goto st28;
		case 47: goto tr198;
		case 58: goto tr190;
		case 61: goto st157;
		case 63: goto tr170;
		case 95: goto st157;
		case 126: goto st157;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st157;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st157;
	} else
		goto st157;
	goto st0;
tr198:
#line 102 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st162;
tr274:
#line 159 "src/lib/uri/uri_parser.rl"
	{ s = p;}
	goto st162;
st162:
	if ( ++p == pe )
		goto _test_eof162;
case 162:
#line 1220 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st163;
		case 35: goto tr157;
		case 37: goto st33;
		case 47: goto st153;
		case 58: goto st153;
		case 61: goto st163;
		case 63: goto tr162;
		case 95: goto st163;
		case 124: goto st153;
		case 126: goto st163;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st163;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st163;
	} else
		goto st163;
	goto st0;
st163:
	if ( ++p == pe )
		goto _test_eof163;
case 163:
	switch( (*p) ) {
		case 33: goto st163;
		case 35: goto tr157;
		case 37: goto st33;
		case 47: goto st162;
		case 58: goto tr201;
		case 61: goto st163;
		case 63: goto tr162;
		case 95: goto st163;
		case 124: goto st153;
		case 126: goto st163;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st163;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st163;
	} else
		goto st163;
	goto st0;
st33:
	if ( ++p == pe )
		goto _test_eof33;
case 33:
	switch( (*p) ) {
		case 37: goto st163;
		case 117: goto st34;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st163;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st163;
	} else
		goto st163;
	goto st0;
st34:
	if ( ++p == pe )
		goto _test_eof34;
case 34:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st35;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st35;
	} else
		goto st35;
	goto st0;
st35:
	if ( ++p == pe )
		goto _test_eof35;
case 35:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st36;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st36;
	} else
		goto st36;
	goto st0;
st36:
	if ( ++p == pe )
		goto _test_eof36;
case 36:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st37;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st37;
	} else
		goto st37;
	goto st0;
st37:
	if ( ++p == pe )
		goto _test_eof37;
case 37:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st163;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st163;
	} else
		goto st163;
	goto st0;
tr201:
#line 120 "src/lib/uri/uri_parser.rl"
	{
			/*
			 * This action is also called for path_* terms.
			 * I absolutely have no idea why.
			 */
			if (uri->host_hint != 3) {
				uri->host_hint = 3;
				uri->host = URI_HOST_UNIX;
				uri->host_len = strlen(URI_HOST_UNIX);
				uri->service = s; uri->service_len = p - s;
				/* a workaround for grammar limitations */
				uri->path = NULL;
				uri->path_len = 0;
			};
		}
	goto st164;
st164:
	if ( ++p == pe )
		goto _test_eof164;
case 164:
#line 1358 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st153;
		case 35: goto tr202;
		case 37: goto st11;
		case 47: goto tr203;
		case 61: goto st153;
		case 63: goto tr204;
		case 95: goto st153;
		case 124: goto st153;
		case 126: goto st153;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st153;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st153;
	} else
		goto st153;
	goto st0;
tr31:
#line 159 "src/lib/uri/uri_parser.rl"
	{ s = p;}
#line 193 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st165;
st165:
	if ( ++p == pe )
		goto _test_eof165;
case 165:
#line 1389 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st166;
		case 37: goto st38;
		case 58: goto st168;
		case 61: goto st166;
		case 95: goto st166;
		case 124: goto st168;
		case 126: goto st166;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st166;
	} else if ( (*p) > 59 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st166;
		} else if ( (*p) >= 64 )
			goto st166;
	} else
		goto st166;
	goto st0;
st166:
	if ( ++p == pe )
		goto _test_eof166;
case 166:
	switch( (*p) ) {
		case 33: goto st166;
		case 37: goto st38;
		case 47: goto st167;
		case 58: goto tr207;
		case 61: goto st166;
		case 95: goto st166;
		case 124: goto st168;
		case 126: goto st166;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st166;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st166;
	} else
		goto st166;
	goto st0;
st38:
	if ( ++p == pe )
		goto _test_eof38;
case 38:
	switch( (*p) ) {
		case 37: goto st166;
		case 117: goto st39;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st166;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st166;
	} else
		goto st166;
	goto st0;
st39:
	if ( ++p == pe )
		goto _test_eof39;
case 39:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st40;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st40;
	} else
		goto st40;
	goto st0;
st40:
	if ( ++p == pe )
		goto _test_eof40;
case 40:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st41;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st41;
	} else
		goto st41;
	goto st0;
st41:
	if ( ++p == pe )
		goto _test_eof41;
case 41:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st42;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st42;
	} else
		goto st42;
	goto st0;
st42:
	if ( ++p == pe )
		goto _test_eof42;
case 42:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st166;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st166;
	} else
		goto st166;
	goto st0;
st167:
	if ( ++p == pe )
		goto _test_eof167;
case 167:
	switch( (*p) ) {
		case 33: goto st166;
		case 37: goto st38;
		case 47: goto st168;
		case 58: goto st168;
		case 61: goto st166;
		case 95: goto st166;
		case 124: goto st168;
		case 126: goto st166;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st166;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st166;
	} else
		goto st166;
	goto st0;
st168:
	if ( ++p == pe )
		goto _test_eof168;
case 168:
	switch( (*p) ) {
		case 33: goto st168;
		case 37: goto st43;
		case 61: goto st168;
		case 95: goto st168;
		case 124: goto st168;
		case 126: goto st168;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st168;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st168;
	} else
		goto st168;
	goto st0;
st43:
	if ( ++p == pe )
		goto _test_eof43;
case 43:
	switch( (*p) ) {
		case 37: goto st168;
		case 117: goto st44;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st168;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st168;
	} else
		goto st168;
	goto st0;
st44:
	if ( ++p == pe )
		goto _test_eof44;
case 44:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st45;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st45;
	} else
		goto st45;
	goto st0;
st45:
	if ( ++p == pe )
		goto _test_eof45;
case 45:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st46;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st46;
	} else
		goto st46;
	goto st0;
st46:
	if ( ++p == pe )
		goto _test_eof46;
case 46:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st47;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st47;
	} else
		goto st47;
	goto st0;
st47:
	if ( ++p == pe )
		goto _test_eof47;
case 47:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st168;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st168;
	} else
		goto st168;
	goto st0;
tr207:
#line 120 "src/lib/uri/uri_parser.rl"
	{
			/*
			 * This action is also called for path_* terms.
			 * I absolutely have no idea why.
			 */
			if (uri->host_hint != 3) {
				uri->host_hint = 3;
				uri->host = URI_HOST_UNIX;
				uri->host_len = strlen(URI_HOST_UNIX);
				uri->service = s; uri->service_len = p - s;
				/* a workaround for grammar limitations */
				uri->path = NULL;
				uri->path_len = 0;
			};
		}
	goto st169;
st169:
	if ( ++p == pe )
		goto _test_eof169;
case 169:
#line 1638 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st168;
		case 35: goto tr202;
		case 37: goto st43;
		case 47: goto tr209;
		case 61: goto st168;
		case 63: goto tr204;
		case 95: goto st168;
		case 124: goto st168;
		case 126: goto st168;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st168;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st168;
	} else
		goto st168;
	goto st0;
tr209:
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st170;
st170:
	if ( ++p == pe )
		goto _test_eof170;
case 170:
#line 1667 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st170;
		case 35: goto tr157;
		case 37: goto st48;
		case 61: goto st170;
		case 63: goto tr162;
		case 95: goto st170;
		case 124: goto st170;
		case 126: goto st170;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st170;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st170;
	} else
		goto st170;
	goto st0;
st48:
	if ( ++p == pe )
		goto _test_eof48;
case 48:
	switch( (*p) ) {
		case 37: goto st170;
		case 117: goto st49;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st170;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st170;
	} else
		goto st170;
	goto st0;
st49:
	if ( ++p == pe )
		goto _test_eof49;
case 49:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st50;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st50;
	} else
		goto st50;
	goto st0;
st50:
	if ( ++p == pe )
		goto _test_eof50;
case 50:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st51;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st51;
	} else
		goto st51;
	goto st0;
st51:
	if ( ++p == pe )
		goto _test_eof51;
case 51:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st52;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st52;
	} else
		goto st52;
	goto st0;
st52:
	if ( ++p == pe )
		goto _test_eof52;
case 52:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st170;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st170;
	} else
		goto st170;
	goto st0;
tr32:
#line 108 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 101 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st171;
st171:
	if ( ++p == pe )
		goto _test_eof171;
case 171:
#line 1766 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st157;
		case 35: goto tr166;
		case 37: goto st28;
		case 46: goto st172;
		case 47: goto tr168;
		case 58: goto tr190;
		case 59: goto st157;
		case 61: goto st157;
		case 63: goto tr170;
		case 95: goto st157;
		case 126: goto st157;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st157;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st157;
		} else if ( (*p) >= 65 )
			goto st157;
	} else
		goto st184;
	goto st0;
st172:
	if ( ++p == pe )
		goto _test_eof172;
case 172:
	switch( (*p) ) {
		case 33: goto st157;
		case 35: goto tr166;
		case 37: goto st28;
		case 47: goto tr168;
		case 58: goto tr190;
		case 59: goto st157;
		case 61: goto st157;
		case 63: goto tr170;
		case 95: goto st157;
		case 126: goto st157;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st157;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st157;
		} else if ( (*p) >= 65 )
			goto st157;
	} else
		goto st173;
	goto st0;
st173:
	if ( ++p == pe )
		goto _test_eof173;
case 173:
	switch( (*p) ) {
		case 33: goto st157;
		case 35: goto tr166;
		case 37: goto st28;
		case 46: goto st174;
		case 47: goto tr168;
		case 58: goto tr190;
		case 59: goto st157;
		case 61: goto st157;
		case 63: goto tr170;
		case 95: goto st157;
		case 126: goto st157;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st157;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st157;
		} else if ( (*p) >= 65 )
			goto st157;
	} else
		goto st182;
	goto st0;
st174:
	if ( ++p == pe )
		goto _test_eof174;
case 174:
	switch( (*p) ) {
		case 33: goto st157;
		case 35: goto tr166;
		case 37: goto st28;
		case 47: goto tr168;
		case 58: goto tr190;
		case 59: goto st157;
		case 61: goto st157;
		case 63: goto tr170;
		case 95: goto st157;
		case 126: goto st157;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st157;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st157;
		} else if ( (*p) >= 65 )
			goto st157;
	} else
		goto st175;
	goto st0;
st175:
	if ( ++p == pe )
		goto _test_eof175;
case 175:
	switch( (*p) ) {
		case 33: goto st157;
		case 35: goto tr166;
		case 37: goto st28;
		case 46: goto st176;
		case 47: goto tr168;
		case 58: goto tr190;
		case 59: goto st157;
		case 61: goto st157;
		case 63: goto tr170;
		case 95: goto st157;
		case 126: goto st157;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st157;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st157;
		} else if ( (*p) >= 65 )
			goto st157;
	} else
		goto st180;
	goto st0;
st176:
	if ( ++p == pe )
		goto _test_eof176;
case 176:
	switch( (*p) ) {
		case 33: goto st157;
		case 35: goto tr166;
		case 37: goto st28;
		case 47: goto tr168;
		case 58: goto tr190;
		case 59: goto st157;
		case 61: goto st157;
		case 63: goto tr170;
		case 95: goto st157;
		case 126: goto st157;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st157;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st157;
		} else if ( (*p) >= 65 )
			goto st157;
	} else
		goto st177;
	goto st0;
st177:
	if ( ++p == pe )
		goto _test_eof177;
case 177:
	switch( (*p) ) {
		case 33: goto st157;
		case 35: goto tr220;
		case 37: goto st28;
		case 47: goto tr221;
		case 58: goto tr223;
		case 59: goto st157;
		case 61: goto st157;
		case 63: goto tr224;
		case 95: goto st157;
		case 126: goto st157;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st157;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st157;
		} else if ( (*p) >= 65 )
			goto st157;
	} else
		goto st178;
	goto st0;
st178:
	if ( ++p == pe )
		goto _test_eof178;
case 178:
	switch( (*p) ) {
		case 33: goto st157;
		case 35: goto tr220;
		case 37: goto st28;
		case 47: goto tr221;
		case 58: goto tr223;
		case 59: goto st157;
		case 61: goto st157;
		case 63: goto tr224;
		case 95: goto st157;
		case 126: goto st157;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st157;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st157;
		} else if ( (*p) >= 65 )
			goto st157;
	} else
		goto st179;
	goto st0;
st179:
	if ( ++p == pe )
		goto _test_eof179;
case 179:
	switch( (*p) ) {
		case 33: goto st157;
		case 35: goto tr220;
		case 37: goto st28;
		case 47: goto tr221;
		case 58: goto tr223;
		case 61: goto st157;
		case 63: goto tr224;
		case 95: goto st157;
		case 126: goto st157;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st157;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st157;
	} else
		goto st157;
	goto st0;
st180:
	if ( ++p == pe )
		goto _test_eof180;
case 180:
	switch( (*p) ) {
		case 33: goto st157;
		case 35: goto tr166;
		case 37: goto st28;
		case 46: goto st176;
		case 47: goto tr168;
		case 58: goto tr190;
		case 59: goto st157;
		case 61: goto st157;
		case 63: goto tr170;
		case 95: goto st157;
		case 126: goto st157;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st157;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st157;
		} else if ( (*p) >= 65 )
			goto st157;
	} else
		goto st181;
	goto st0;
st181:
	if ( ++p == pe )
		goto _test_eof181;
case 181:
	switch( (*p) ) {
		case 33: goto st157;
		case 35: goto tr166;
		case 37: goto st28;
		case 46: goto st176;
		case 47: goto tr168;
		case 58: goto tr190;
		case 61: goto st157;
		case 63: goto tr170;
		case 95: goto st157;
		case 126: goto st157;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st157;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st157;
	} else
		goto st157;
	goto st0;
st182:
	if ( ++p == pe )
		goto _test_eof182;
case 182:
	switch( (*p) ) {
		case 33: goto st157;
		case 35: goto tr166;
		case 37: goto st28;
		case 46: goto st174;
		case 47: goto tr168;
		case 58: goto tr190;
		case 59: goto st157;
		case 61: goto st157;
		case 63: goto tr170;
		case 95: goto st157;
		case 126: goto st157;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st157;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st157;
		} else if ( (*p) >= 65 )
			goto st157;
	} else
		goto st183;
	goto st0;
st183:
	if ( ++p == pe )
		goto _test_eof183;
case 183:
	switch( (*p) ) {
		case 33: goto st157;
		case 35: goto tr166;
		case 37: goto st28;
		case 46: goto st174;
		case 47: goto tr168;
		case 58: goto tr190;
		case 61: goto st157;
		case 63: goto tr170;
		case 95: goto st157;
		case 126: goto st157;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st157;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st157;
	} else
		goto st157;
	goto st0;
st184:
	if ( ++p == pe )
		goto _test_eof184;
case 184:
	switch( (*p) ) {
		case 33: goto st157;
		case 35: goto tr166;
		case 37: goto st28;
		case 46: goto st172;
		case 47: goto tr168;
		case 58: goto tr190;
		case 59: goto st157;
		case 61: goto st157;
		case 63: goto tr170;
		case 95: goto st157;
		case 126: goto st157;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st157;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st157;
		} else if ( (*p) >= 65 )
			goto st157;
	} else
		goto st185;
	goto st0;
st185:
	if ( ++p == pe )
		goto _test_eof185;
case 185:
	switch( (*p) ) {
		case 33: goto st157;
		case 35: goto tr166;
		case 37: goto st28;
		case 46: goto st172;
		case 47: goto tr168;
		case 58: goto tr190;
		case 61: goto st157;
		case 63: goto tr170;
		case 95: goto st157;
		case 126: goto st157;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st157;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st157;
	} else
		goto st157;
	goto st0;
st53:
	if ( ++p == pe )
		goto _test_eof53;
case 53:
	if ( (*p) == 58 )
		goto tr61;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr60;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto tr60;
	} else
		goto tr60;
	goto st0;
tr60:
#line 115 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st54;
st54:
	if ( ++p == pe )
		goto _test_eof54;
case 54:
#line 2199 "src/lib/uri/uri_parser.c"
	if ( (*p) == 58 )
		goto st58;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st55;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st55;
	} else
		goto st55;
	goto st0;
st55:
	if ( ++p == pe )
		goto _test_eof55;
case 55:
	if ( (*p) == 58 )
		goto st58;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st56;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st56;
	} else
		goto st56;
	goto st0;
st56:
	if ( ++p == pe )
		goto _test_eof56;
case 56:
	if ( (*p) == 58 )
		goto st58;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st57;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st57;
	} else
		goto st57;
	goto st0;
st57:
	if ( ++p == pe )
		goto _test_eof57;
case 57:
	if ( (*p) == 58 )
		goto st58;
	goto st0;
st58:
	if ( ++p == pe )
		goto _test_eof58;
case 58:
	switch( (*p) ) {
		case 58: goto st63;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st59;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st59;
	} else
		goto st59;
	goto st0;
st59:
	if ( ++p == pe )
		goto _test_eof59;
case 59:
	switch( (*p) ) {
		case 58: goto st63;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st60;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st60;
	} else
		goto st60;
	goto st0;
st60:
	if ( ++p == pe )
		goto _test_eof60;
case 60:
	switch( (*p) ) {
		case 58: goto st63;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st61;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st61;
	} else
		goto st61;
	goto st0;
st61:
	if ( ++p == pe )
		goto _test_eof61;
case 61:
	switch( (*p) ) {
		case 58: goto st63;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st62;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st62;
	} else
		goto st62;
	goto st0;
st62:
	if ( ++p == pe )
		goto _test_eof62;
case 62:
	switch( (*p) ) {
		case 58: goto st63;
		case 93: goto tr68;
	}
	goto st0;
st63:
	if ( ++p == pe )
		goto _test_eof63;
case 63:
	switch( (*p) ) {
		case 58: goto st68;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st64;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st64;
	} else
		goto st64;
	goto st0;
st64:
	if ( ++p == pe )
		goto _test_eof64;
case 64:
	switch( (*p) ) {
		case 58: goto st68;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st65;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st65;
	} else
		goto st65;
	goto st0;
st65:
	if ( ++p == pe )
		goto _test_eof65;
case 65:
	switch( (*p) ) {
		case 58: goto st68;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st66;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st66;
	} else
		goto st66;
	goto st0;
st66:
	if ( ++p == pe )
		goto _test_eof66;
case 66:
	switch( (*p) ) {
		case 58: goto st68;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st67;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st67;
	} else
		goto st67;
	goto st0;
st67:
	if ( ++p == pe )
		goto _test_eof67;
case 67:
	switch( (*p) ) {
		case 58: goto st68;
		case 93: goto tr68;
	}
	goto st0;
st68:
	if ( ++p == pe )
		goto _test_eof68;
case 68:
	switch( (*p) ) {
		case 58: goto st73;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st69;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st69;
	} else
		goto st69;
	goto st0;
st69:
	if ( ++p == pe )
		goto _test_eof69;
case 69:
	switch( (*p) ) {
		case 58: goto st73;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st70;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st70;
	} else
		goto st70;
	goto st0;
st70:
	if ( ++p == pe )
		goto _test_eof70;
case 70:
	switch( (*p) ) {
		case 58: goto st73;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st71;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st71;
	} else
		goto st71;
	goto st0;
st71:
	if ( ++p == pe )
		goto _test_eof71;
case 71:
	switch( (*p) ) {
		case 58: goto st73;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st72;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st72;
	} else
		goto st72;
	goto st0;
st72:
	if ( ++p == pe )
		goto _test_eof72;
case 72:
	switch( (*p) ) {
		case 58: goto st73;
		case 93: goto tr68;
	}
	goto st0;
st73:
	if ( ++p == pe )
		goto _test_eof73;
case 73:
	switch( (*p) ) {
		case 58: goto st78;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st74;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st74;
	} else
		goto st74;
	goto st0;
st74:
	if ( ++p == pe )
		goto _test_eof74;
case 74:
	switch( (*p) ) {
		case 58: goto st78;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st75;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st75;
	} else
		goto st75;
	goto st0;
st75:
	if ( ++p == pe )
		goto _test_eof75;
case 75:
	switch( (*p) ) {
		case 58: goto st78;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st76;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st76;
	} else
		goto st76;
	goto st0;
st76:
	if ( ++p == pe )
		goto _test_eof76;
case 76:
	switch( (*p) ) {
		case 58: goto st78;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st77;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st77;
	} else
		goto st77;
	goto st0;
st77:
	if ( ++p == pe )
		goto _test_eof77;
case 77:
	switch( (*p) ) {
		case 58: goto st78;
		case 93: goto tr68;
	}
	goto st0;
st78:
	if ( ++p == pe )
		goto _test_eof78;
case 78:
	switch( (*p) ) {
		case 58: goto st83;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st79;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st79;
	} else
		goto st79;
	goto st0;
st79:
	if ( ++p == pe )
		goto _test_eof79;
case 79:
	switch( (*p) ) {
		case 58: goto st83;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st80;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st80;
	} else
		goto st80;
	goto st0;
st80:
	if ( ++p == pe )
		goto _test_eof80;
case 80:
	switch( (*p) ) {
		case 58: goto st83;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st81;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st81;
	} else
		goto st81;
	goto st0;
st81:
	if ( ++p == pe )
		goto _test_eof81;
case 81:
	switch( (*p) ) {
		case 58: goto st83;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st82;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st82;
	} else
		goto st82;
	goto st0;
st82:
	if ( ++p == pe )
		goto _test_eof82;
case 82:
	switch( (*p) ) {
		case 58: goto st83;
		case 93: goto tr68;
	}
	goto st0;
st83:
	if ( ++p == pe )
		goto _test_eof83;
case 83:
	switch( (*p) ) {
		case 58: goto st88;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st84;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st84;
	} else
		goto st84;
	goto st0;
st84:
	if ( ++p == pe )
		goto _test_eof84;
case 84:
	switch( (*p) ) {
		case 58: goto st88;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st85;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st85;
	} else
		goto st85;
	goto st0;
st85:
	if ( ++p == pe )
		goto _test_eof85;
case 85:
	switch( (*p) ) {
		case 58: goto st88;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st86;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st86;
	} else
		goto st86;
	goto st0;
st86:
	if ( ++p == pe )
		goto _test_eof86;
case 86:
	switch( (*p) ) {
		case 58: goto st88;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st87;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st87;
	} else
		goto st87;
	goto st0;
st87:
	if ( ++p == pe )
		goto _test_eof87;
case 87:
	switch( (*p) ) {
		case 58: goto st88;
		case 93: goto tr68;
	}
	goto st0;
st88:
	if ( ++p == pe )
		goto _test_eof88;
case 88:
	switch( (*p) ) {
		case 58: goto st93;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st89;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st89;
	} else
		goto st89;
	goto st0;
st89:
	if ( ++p == pe )
		goto _test_eof89;
case 89:
	switch( (*p) ) {
		case 58: goto st93;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st90;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st90;
	} else
		goto st90;
	goto st0;
st90:
	if ( ++p == pe )
		goto _test_eof90;
case 90:
	switch( (*p) ) {
		case 58: goto st93;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st91;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st91;
	} else
		goto st91;
	goto st0;
st91:
	if ( ++p == pe )
		goto _test_eof91;
case 91:
	switch( (*p) ) {
		case 58: goto st93;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st92;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st92;
	} else
		goto st92;
	goto st0;
st92:
	if ( ++p == pe )
		goto _test_eof92;
case 92:
	switch( (*p) ) {
		case 58: goto st93;
		case 93: goto tr68;
	}
	goto st0;
st93:
	if ( ++p == pe )
		goto _test_eof93;
case 93:
	if ( (*p) == 93 )
		goto tr68;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st94;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st94;
	} else
		goto st94;
	goto st0;
st94:
	if ( ++p == pe )
		goto _test_eof94;
case 94:
	if ( (*p) == 93 )
		goto tr68;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st95;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st95;
	} else
		goto st95;
	goto st0;
st95:
	if ( ++p == pe )
		goto _test_eof95;
case 95:
	if ( (*p) == 93 )
		goto tr68;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st96;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st96;
	} else
		goto st96;
	goto st0;
st96:
	if ( ++p == pe )
		goto _test_eof96;
case 96:
	if ( (*p) == 93 )
		goto tr68;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st97;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st97;
	} else
		goto st97;
	goto st0;
st97:
	if ( ++p == pe )
		goto _test_eof97;
case 97:
	if ( (*p) == 93 )
		goto tr68;
	goto st0;
tr68:
#line 116 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;
				   uri->host_hint = 2; }
	goto st186;
st186:
	if ( ++p == pe )
		goto _test_eof186;
case 186:
#line 2863 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 35: goto tr202;
		case 47: goto tr203;
		case 58: goto st158;
		case 63: goto tr204;
	}
	goto st0;
tr61:
#line 115 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st98;
st98:
	if ( ++p == pe )
		goto _test_eof98;
case 98:
#line 2879 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 58: goto st99;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st59;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st59;
	} else
		goto st59;
	goto st0;
st99:
	if ( ++p == pe )
		goto _test_eof99;
case 99:
	switch( (*p) ) {
		case 58: goto st68;
		case 70: goto st100;
		case 93: goto tr68;
		case 102: goto st100;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st64;
	} else if ( (*p) > 69 ) {
		if ( 97 <= (*p) && (*p) <= 101 )
			goto st64;
	} else
		goto st64;
	goto st0;
st100:
	if ( ++p == pe )
		goto _test_eof100;
case 100:
	switch( (*p) ) {
		case 58: goto st68;
		case 70: goto st101;
		case 93: goto tr68;
		case 102: goto st101;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st65;
	} else if ( (*p) > 69 ) {
		if ( 97 <= (*p) && (*p) <= 101 )
			goto st65;
	} else
		goto st65;
	goto st0;
st101:
	if ( ++p == pe )
		goto _test_eof101;
case 101:
	switch( (*p) ) {
		case 58: goto st68;
		case 70: goto st102;
		case 93: goto tr68;
		case 102: goto st102;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st66;
	} else if ( (*p) > 69 ) {
		if ( 97 <= (*p) && (*p) <= 101 )
			goto st66;
	} else
		goto st66;
	goto st0;
st102:
	if ( ++p == pe )
		goto _test_eof102;
case 102:
	switch( (*p) ) {
		case 58: goto st68;
		case 70: goto st103;
		case 93: goto tr68;
		case 102: goto st103;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st67;
	} else if ( (*p) > 69 ) {
		if ( 97 <= (*p) && (*p) <= 101 )
			goto st67;
	} else
		goto st67;
	goto st0;
st103:
	if ( ++p == pe )
		goto _test_eof103;
case 103:
	switch( (*p) ) {
		case 58: goto st104;
		case 93: goto tr68;
	}
	goto st0;
st104:
	if ( ++p == pe )
		goto _test_eof104;
case 104:
	switch( (*p) ) {
		case 58: goto st73;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st105;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st69;
	} else
		goto st69;
	goto st0;
st105:
	if ( ++p == pe )
		goto _test_eof105;
case 105:
	switch( (*p) ) {
		case 46: goto st106;
		case 58: goto st73;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st117;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st70;
	} else
		goto st70;
	goto st0;
st106:
	if ( ++p == pe )
		goto _test_eof106;
case 106:
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st107;
	goto st0;
st107:
	if ( ++p == pe )
		goto _test_eof107;
case 107:
	if ( (*p) == 46 )
		goto st108;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st115;
	goto st0;
st108:
	if ( ++p == pe )
		goto _test_eof108;
case 108:
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st109;
	goto st0;
st109:
	if ( ++p == pe )
		goto _test_eof109;
case 109:
	if ( (*p) == 46 )
		goto st110;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st113;
	goto st0;
st110:
	if ( ++p == pe )
		goto _test_eof110;
case 110:
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st111;
	goto st0;
st111:
	if ( ++p == pe )
		goto _test_eof111;
case 111:
	if ( (*p) == 93 )
		goto tr68;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st112;
	goto st0;
st112:
	if ( ++p == pe )
		goto _test_eof112;
case 112:
	if ( (*p) == 93 )
		goto tr68;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st97;
	goto st0;
st113:
	if ( ++p == pe )
		goto _test_eof113;
case 113:
	if ( (*p) == 46 )
		goto st110;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st114;
	goto st0;
st114:
	if ( ++p == pe )
		goto _test_eof114;
case 114:
	if ( (*p) == 46 )
		goto st110;
	goto st0;
st115:
	if ( ++p == pe )
		goto _test_eof115;
case 115:
	if ( (*p) == 46 )
		goto st108;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st116;
	goto st0;
st116:
	if ( ++p == pe )
		goto _test_eof116;
case 116:
	if ( (*p) == 46 )
		goto st108;
	goto st0;
st117:
	if ( ++p == pe )
		goto _test_eof117;
case 117:
	switch( (*p) ) {
		case 46: goto st106;
		case 58: goto st73;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st118;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st71;
	} else
		goto st71;
	goto st0;
st118:
	if ( ++p == pe )
		goto _test_eof118;
case 118:
	switch( (*p) ) {
		case 46: goto st106;
		case 58: goto st73;
		case 93: goto tr68;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st72;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st72;
	} else
		goto st72;
	goto st0;
tr34:
#line 101 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st187;
st187:
	if ( ++p == pe )
		goto _test_eof187;
case 187:
#line 3146 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st157;
		case 35: goto tr166;
		case 37: goto st28;
		case 47: goto tr168;
		case 58: goto tr190;
		case 61: goto st157;
		case 63: goto tr170;
		case 95: goto st157;
		case 110: goto st188;
		case 126: goto st157;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st157;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st157;
	} else
		goto st157;
	goto st0;
st188:
	if ( ++p == pe )
		goto _test_eof188;
case 188:
	switch( (*p) ) {
		case 33: goto st157;
		case 35: goto tr166;
		case 37: goto st28;
		case 47: goto tr168;
		case 58: goto tr190;
		case 61: goto st157;
		case 63: goto tr170;
		case 95: goto st157;
		case 105: goto st189;
		case 126: goto st157;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st157;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st157;
	} else
		goto st157;
	goto st0;
st189:
	if ( ++p == pe )
		goto _test_eof189;
case 189:
	switch( (*p) ) {
		case 33: goto st157;
		case 35: goto tr166;
		case 37: goto st28;
		case 47: goto tr168;
		case 58: goto tr190;
		case 61: goto st157;
		case 63: goto tr170;
		case 95: goto st157;
		case 120: goto st190;
		case 126: goto st157;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st157;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st157;
	} else
		goto st157;
	goto st0;
st190:
	if ( ++p == pe )
		goto _test_eof190;
case 190:
	switch( (*p) ) {
		case 33: goto st157;
		case 35: goto tr166;
		case 37: goto st28;
		case 47: goto tr233;
		case 58: goto tr190;
		case 61: goto st157;
		case 63: goto tr170;
		case 95: goto st157;
		case 126: goto st157;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st157;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st157;
	} else
		goto st157;
	goto st0;
tr233:
#line 102 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st191;
st191:
	if ( ++p == pe )
		goto _test_eof191;
case 191:
#line 3252 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st153;
		case 35: goto tr157;
		case 37: goto st11;
		case 58: goto st192;
		case 61: goto st153;
		case 63: goto tr162;
		case 95: goto st153;
		case 124: goto st153;
		case 126: goto st153;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st153;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st153;
	} else
		goto st153;
	goto st0;
st192:
	if ( ++p == pe )
		goto _test_eof192;
case 192:
	switch( (*p) ) {
		case 33: goto st153;
		case 35: goto tr157;
		case 37: goto st11;
		case 46: goto tr235;
		case 47: goto tr236;
		case 61: goto st153;
		case 63: goto tr162;
		case 95: goto st153;
		case 124: goto st153;
		case 126: goto st153;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st153;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st153;
	} else
		goto st153;
	goto st0;
tr235:
#line 136 "src/lib/uri/uri_parser.rl"
	{ s = p;}
	goto st193;
st193:
	if ( ++p == pe )
		goto _test_eof193;
case 193:
#line 3306 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st153;
		case 35: goto tr157;
		case 37: goto st11;
		case 47: goto st194;
		case 61: goto st153;
		case 63: goto tr162;
		case 95: goto st153;
		case 124: goto st153;
		case 126: goto st153;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st153;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st153;
	} else
		goto st153;
	goto st0;
tr236:
#line 136 "src/lib/uri/uri_parser.rl"
	{ s = p;}
	goto st194;
st194:
	if ( ++p == pe )
		goto _test_eof194;
case 194:
#line 3335 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st195;
		case 35: goto tr157;
		case 37: goto st119;
		case 47: goto st153;
		case 58: goto st153;
		case 61: goto st195;
		case 63: goto tr162;
		case 95: goto st195;
		case 124: goto st153;
		case 126: goto st195;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st195;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st195;
	} else
		goto st195;
	goto st0;
st195:
	if ( ++p == pe )
		goto _test_eof195;
case 195:
	switch( (*p) ) {
		case 33: goto st195;
		case 35: goto tr239;
		case 37: goto st119;
		case 47: goto st194;
		case 58: goto tr201;
		case 61: goto st195;
		case 63: goto tr240;
		case 95: goto st195;
		case 124: goto st153;
		case 126: goto st195;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st195;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st195;
	} else
		goto st195;
	goto st0;
st119:
	if ( ++p == pe )
		goto _test_eof119;
case 119:
	switch( (*p) ) {
		case 37: goto st195;
		case 117: goto st120;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st195;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st195;
	} else
		goto st195;
	goto st0;
st120:
	if ( ++p == pe )
		goto _test_eof120;
case 120:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st121;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st121;
	} else
		goto st121;
	goto st0;
st121:
	if ( ++p == pe )
		goto _test_eof121;
case 121:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st122;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st122;
	} else
		goto st122;
	goto st0;
st122:
	if ( ++p == pe )
		goto _test_eof122;
case 122:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st123;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st123;
	} else
		goto st123;
	goto st0;
st123:
	if ( ++p == pe )
		goto _test_eof123;
case 123:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st195;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st195;
	} else
		goto st195;
	goto st0;
tr185:
#line 149 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 139 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st196;
st196:
	if ( ++p == pe )
		goto _test_eof196;
case 196:
#line 3461 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st21;
		case 35: goto tr193;
		case 37: goto st22;
		case 47: goto tr194;
		case 59: goto st21;
		case 61: goto st21;
		case 63: goto tr196;
		case 64: goto tr23;
		case 95: goto st21;
		case 126: goto st21;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st21;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st21;
		} else if ( (*p) >= 65 )
			goto st21;
	} else
		goto st196;
	goto st0;
tr188:
#line 149 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 139 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st197;
st197:
	if ( ++p == pe )
		goto _test_eof197;
case 197:
#line 3496 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st21;
		case 35: goto tr193;
		case 37: goto st22;
		case 47: goto tr194;
		case 59: goto st21;
		case 61: goto st21;
		case 63: goto tr196;
		case 64: goto tr23;
		case 95: goto st21;
		case 126: goto st21;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 57 )
			goto st21;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st197;
	} else
		goto st197;
	goto st0;
tr159:
#line 145 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 101 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 159 "src/lib/uri/uri_parser.rl"
	{ s = p;}
	goto st198;
st198:
	if ( ++p == pe )
		goto _test_eof198;
case 198:
#line 3530 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st150;
		case 35: goto tr166;
		case 37: goto st6;
		case 47: goto tr243;
		case 58: goto tr169;
		case 61: goto st150;
		case 63: goto tr170;
		case 64: goto tr171;
		case 95: goto st150;
		case 126: goto st150;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st150;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st150;
	} else
		goto st150;
	goto st0;
tr243:
#line 102 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st199;
tr339:
#line 136 "src/lib/uri/uri_parser.rl"
	{ s = p;}
	goto st199;
st199:
	if ( ++p == pe )
		goto _test_eof199;
case 199:
#line 3566 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st200;
		case 35: goto tr157;
		case 37: goto st124;
		case 47: goto st153;
		case 58: goto st153;
		case 61: goto st200;
		case 63: goto tr162;
		case 95: goto st200;
		case 124: goto st153;
		case 126: goto st200;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st200;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st200;
	} else
		goto st200;
	goto st0;
st200:
	if ( ++p == pe )
		goto _test_eof200;
case 200:
	switch( (*p) ) {
		case 33: goto st200;
		case 35: goto tr245;
		case 37: goto st124;
		case 47: goto st199;
		case 58: goto tr201;
		case 61: goto st200;
		case 63: goto tr247;
		case 95: goto st200;
		case 124: goto st153;
		case 126: goto st200;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st200;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st200;
	} else
		goto st200;
	goto st0;
st124:
	if ( ++p == pe )
		goto _test_eof124;
case 124:
	switch( (*p) ) {
		case 37: goto st200;
		case 117: goto st125;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st200;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st200;
	} else
		goto st200;
	goto st0;
st125:
	if ( ++p == pe )
		goto _test_eof125;
case 125:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st126;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st126;
	} else
		goto st126;
	goto st0;
st126:
	if ( ++p == pe )
		goto _test_eof126;
case 126:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st127;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st127;
	} else
		goto st127;
	goto st0;
st127:
	if ( ++p == pe )
		goto _test_eof127;
case 127:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st128;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st128;
	} else
		goto st128;
	goto st0;
st128:
	if ( ++p == pe )
		goto _test_eof128;
case 128:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st200;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st200;
	} else
		goto st200;
	goto st0;
tr160:
#line 159 "src/lib/uri/uri_parser.rl"
	{ s = p;}
#line 193 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st201;
st201:
	if ( ++p == pe )
		goto _test_eof201;
case 201:
#line 3692 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st202;
		case 35: goto tr157;
		case 37: goto st129;
		case 58: goto st170;
		case 61: goto st202;
		case 63: goto tr162;
		case 95: goto st202;
		case 124: goto st170;
		case 126: goto st202;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st202;
	} else if ( (*p) > 59 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st202;
		} else if ( (*p) >= 64 )
			goto st202;
	} else
		goto st202;
	goto st0;
st202:
	if ( ++p == pe )
		goto _test_eof202;
case 202:
	switch( (*p) ) {
		case 33: goto st202;
		case 35: goto tr245;
		case 37: goto st129;
		case 47: goto st203;
		case 58: goto tr250;
		case 61: goto st202;
		case 63: goto tr247;
		case 95: goto st202;
		case 124: goto st170;
		case 126: goto st202;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st202;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st202;
	} else
		goto st202;
	goto st0;
st129:
	if ( ++p == pe )
		goto _test_eof129;
case 129:
	switch( (*p) ) {
		case 37: goto st202;
		case 117: goto st130;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st202;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st202;
	} else
		goto st202;
	goto st0;
st130:
	if ( ++p == pe )
		goto _test_eof130;
case 130:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st131;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st131;
	} else
		goto st131;
	goto st0;
st131:
	if ( ++p == pe )
		goto _test_eof131;
case 131:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st132;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st132;
	} else
		goto st132;
	goto st0;
st132:
	if ( ++p == pe )
		goto _test_eof132;
case 132:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st133;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st133;
	} else
		goto st133;
	goto st0;
st133:
	if ( ++p == pe )
		goto _test_eof133;
case 133:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st202;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st202;
	} else
		goto st202;
	goto st0;
st203:
	if ( ++p == pe )
		goto _test_eof203;
case 203:
	switch( (*p) ) {
		case 33: goto st202;
		case 35: goto tr157;
		case 37: goto st129;
		case 47: goto st170;
		case 58: goto st170;
		case 61: goto st202;
		case 63: goto tr162;
		case 95: goto st202;
		case 124: goto st170;
		case 126: goto st202;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st202;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st202;
	} else
		goto st202;
	goto st0;
tr250:
#line 120 "src/lib/uri/uri_parser.rl"
	{
			/*
			 * This action is also called for path_* terms.
			 * I absolutely have no idea why.
			 */
			if (uri->host_hint != 3) {
				uri->host_hint = 3;
				uri->host = URI_HOST_UNIX;
				uri->host_len = strlen(URI_HOST_UNIX);
				uri->service = s; uri->service_len = p - s;
				/* a workaround for grammar limitations */
				uri->path = NULL;
				uri->path_len = 0;
			};
		}
	goto st204;
st204:
	if ( ++p == pe )
		goto _test_eof204;
case 204:
#line 3857 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st170;
		case 35: goto tr202;
		case 37: goto st48;
		case 47: goto tr209;
		case 61: goto st170;
		case 63: goto tr204;
		case 95: goto st170;
		case 124: goto st170;
		case 126: goto st170;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st170;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st170;
	} else
		goto st170;
	goto st0;
tr161:
#line 145 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 108 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 101 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 189 "src/lib/uri/uri_parser.rl"
	{ uri->service = p; }
	goto st205;
st205:
	if ( ++p == pe )
		goto _test_eof205;
case 205:
#line 3892 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st150;
		case 35: goto tr166;
		case 37: goto st6;
		case 46: goto st206;
		case 47: goto tr168;
		case 58: goto tr169;
		case 59: goto st150;
		case 61: goto st150;
		case 63: goto tr170;
		case 64: goto tr171;
		case 95: goto st150;
		case 126: goto st150;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st150;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st150;
		} else if ( (*p) >= 65 )
			goto st150;
	} else
		goto st218;
	goto st0;
st206:
	if ( ++p == pe )
		goto _test_eof206;
case 206:
	switch( (*p) ) {
		case 33: goto st150;
		case 35: goto tr166;
		case 37: goto st6;
		case 47: goto tr168;
		case 58: goto tr169;
		case 59: goto st150;
		case 61: goto st150;
		case 63: goto tr170;
		case 64: goto tr171;
		case 95: goto st150;
		case 126: goto st150;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st150;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st150;
		} else if ( (*p) >= 65 )
			goto st150;
	} else
		goto st207;
	goto st0;
st207:
	if ( ++p == pe )
		goto _test_eof207;
case 207:
	switch( (*p) ) {
		case 33: goto st150;
		case 35: goto tr166;
		case 37: goto st6;
		case 46: goto st208;
		case 47: goto tr168;
		case 58: goto tr169;
		case 59: goto st150;
		case 61: goto st150;
		case 63: goto tr170;
		case 64: goto tr171;
		case 95: goto st150;
		case 126: goto st150;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st150;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st150;
		} else if ( (*p) >= 65 )
			goto st150;
	} else
		goto st216;
	goto st0;
st208:
	if ( ++p == pe )
		goto _test_eof208;
case 208:
	switch( (*p) ) {
		case 33: goto st150;
		case 35: goto tr166;
		case 37: goto st6;
		case 47: goto tr168;
		case 58: goto tr169;
		case 59: goto st150;
		case 61: goto st150;
		case 63: goto tr170;
		case 64: goto tr171;
		case 95: goto st150;
		case 126: goto st150;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st150;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st150;
		} else if ( (*p) >= 65 )
			goto st150;
	} else
		goto st209;
	goto st0;
st209:
	if ( ++p == pe )
		goto _test_eof209;
case 209:
	switch( (*p) ) {
		case 33: goto st150;
		case 35: goto tr166;
		case 37: goto st6;
		case 46: goto st210;
		case 47: goto tr168;
		case 58: goto tr169;
		case 59: goto st150;
		case 61: goto st150;
		case 63: goto tr170;
		case 64: goto tr171;
		case 95: goto st150;
		case 126: goto st150;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st150;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st150;
		} else if ( (*p) >= 65 )
			goto st150;
	} else
		goto st214;
	goto st0;
st210:
	if ( ++p == pe )
		goto _test_eof210;
case 210:
	switch( (*p) ) {
		case 33: goto st150;
		case 35: goto tr166;
		case 37: goto st6;
		case 47: goto tr168;
		case 58: goto tr169;
		case 59: goto st150;
		case 61: goto st150;
		case 63: goto tr170;
		case 64: goto tr171;
		case 95: goto st150;
		case 126: goto st150;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st150;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st150;
		} else if ( (*p) >= 65 )
			goto st150;
	} else
		goto st211;
	goto st0;
st211:
	if ( ++p == pe )
		goto _test_eof211;
case 211:
	switch( (*p) ) {
		case 33: goto st150;
		case 35: goto tr220;
		case 37: goto st6;
		case 47: goto tr221;
		case 58: goto tr261;
		case 59: goto st150;
		case 61: goto st150;
		case 63: goto tr224;
		case 64: goto tr171;
		case 95: goto st150;
		case 126: goto st150;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st150;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st150;
		} else if ( (*p) >= 65 )
			goto st150;
	} else
		goto st212;
	goto st0;
st212:
	if ( ++p == pe )
		goto _test_eof212;
case 212:
	switch( (*p) ) {
		case 33: goto st150;
		case 35: goto tr220;
		case 37: goto st6;
		case 47: goto tr221;
		case 58: goto tr261;
		case 59: goto st150;
		case 61: goto st150;
		case 63: goto tr224;
		case 64: goto tr171;
		case 95: goto st150;
		case 126: goto st150;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st150;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st150;
		} else if ( (*p) >= 65 )
			goto st150;
	} else
		goto st213;
	goto st0;
st213:
	if ( ++p == pe )
		goto _test_eof213;
case 213:
	switch( (*p) ) {
		case 33: goto st150;
		case 35: goto tr220;
		case 37: goto st6;
		case 47: goto tr221;
		case 58: goto tr261;
		case 61: goto st150;
		case 63: goto tr224;
		case 64: goto tr171;
		case 95: goto st150;
		case 126: goto st150;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st150;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st150;
	} else
		goto st150;
	goto st0;
st214:
	if ( ++p == pe )
		goto _test_eof214;
case 214:
	switch( (*p) ) {
		case 33: goto st150;
		case 35: goto tr166;
		case 37: goto st6;
		case 46: goto st210;
		case 47: goto tr168;
		case 58: goto tr169;
		case 59: goto st150;
		case 61: goto st150;
		case 63: goto tr170;
		case 64: goto tr171;
		case 95: goto st150;
		case 126: goto st150;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st150;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st150;
		} else if ( (*p) >= 65 )
			goto st150;
	} else
		goto st215;
	goto st0;
st215:
	if ( ++p == pe )
		goto _test_eof215;
case 215:
	switch( (*p) ) {
		case 33: goto st150;
		case 35: goto tr166;
		case 37: goto st6;
		case 46: goto st210;
		case 47: goto tr168;
		case 58: goto tr169;
		case 61: goto st150;
		case 63: goto tr170;
		case 64: goto tr171;
		case 95: goto st150;
		case 126: goto st150;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st150;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st150;
	} else
		goto st150;
	goto st0;
st216:
	if ( ++p == pe )
		goto _test_eof216;
case 216:
	switch( (*p) ) {
		case 33: goto st150;
		case 35: goto tr166;
		case 37: goto st6;
		case 46: goto st208;
		case 47: goto tr168;
		case 58: goto tr169;
		case 59: goto st150;
		case 61: goto st150;
		case 63: goto tr170;
		case 64: goto tr171;
		case 95: goto st150;
		case 126: goto st150;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st150;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st150;
		} else if ( (*p) >= 65 )
			goto st150;
	} else
		goto st217;
	goto st0;
st217:
	if ( ++p == pe )
		goto _test_eof217;
case 217:
	switch( (*p) ) {
		case 33: goto st150;
		case 35: goto tr166;
		case 37: goto st6;
		case 46: goto st208;
		case 47: goto tr168;
		case 58: goto tr169;
		case 61: goto st150;
		case 63: goto tr170;
		case 64: goto tr171;
		case 95: goto st150;
		case 126: goto st150;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st150;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st150;
	} else
		goto st150;
	goto st0;
st218:
	if ( ++p == pe )
		goto _test_eof218;
case 218:
	switch( (*p) ) {
		case 33: goto st150;
		case 35: goto tr166;
		case 37: goto st6;
		case 46: goto st206;
		case 47: goto tr168;
		case 58: goto tr169;
		case 59: goto st150;
		case 61: goto st150;
		case 63: goto tr170;
		case 64: goto tr171;
		case 95: goto st150;
		case 126: goto st150;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st150;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st150;
		} else if ( (*p) >= 65 )
			goto st150;
	} else
		goto st219;
	goto st0;
st219:
	if ( ++p == pe )
		goto _test_eof219;
case 219:
	switch( (*p) ) {
		case 33: goto st150;
		case 35: goto tr166;
		case 37: goto st6;
		case 46: goto st206;
		case 47: goto tr168;
		case 58: goto tr169;
		case 59: goto st150;
		case 61: goto st150;
		case 63: goto tr170;
		case 64: goto tr171;
		case 95: goto st150;
		case 126: goto st150;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st150;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st150;
		} else if ( (*p) >= 65 )
			goto st150;
	} else
		goto st220;
	goto st0;
st220:
	if ( ++p == pe )
		goto _test_eof220;
case 220:
	switch( (*p) ) {
		case 33: goto st150;
		case 35: goto tr166;
		case 37: goto st6;
		case 47: goto tr168;
		case 58: goto tr169;
		case 59: goto st150;
		case 61: goto st150;
		case 63: goto tr170;
		case 64: goto tr171;
		case 95: goto st150;
		case 126: goto st150;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st150;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st150;
		} else if ( (*p) >= 65 )
			goto st150;
	} else
		goto st220;
	goto st0;
st221:
	if ( ++p == pe )
		goto _test_eof221;
case 221:
	switch( (*p) ) {
		case 35: goto tr157;
		case 47: goto st153;
		case 63: goto tr162;
	}
	goto st0;
tr164:
#line 161 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 145 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 101 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st222;
st222:
	if ( ++p == pe )
		goto _test_eof222;
case 222:
#line 4372 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st150;
		case 35: goto tr166;
		case 37: goto st6;
		case 43: goto st222;
		case 47: goto tr168;
		case 58: goto tr268;
		case 59: goto st150;
		case 61: goto st150;
		case 63: goto tr170;
		case 64: goto tr171;
		case 95: goto st150;
		case 126: goto st150;
	}
	if ( (*p) < 45 ) {
		if ( 36 <= (*p) && (*p) <= 44 )
			goto st150;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st222;
		} else if ( (*p) >= 65 )
			goto st222;
	} else
		goto st222;
	goto st0;
tr268:
#line 163 "src/lib/uri/uri_parser.rl"
	{scheme = s; scheme_len = p - s; }
#line 146 "src/lib/uri/uri_parser.rl"
	{ login = s; login_len = p - s; }
#line 102 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st223;
st223:
	if ( ++p == pe )
		goto _test_eof223;
case 223:
#line 4411 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto tr181;
		case 35: goto tr182;
		case 37: goto tr183;
		case 47: goto tr269;
		case 59: goto tr181;
		case 61: goto tr181;
		case 63: goto tr186;
		case 64: goto tr187;
		case 95: goto tr181;
		case 126: goto tr181;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto tr181;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto tr188;
		} else if ( (*p) >= 65 )
			goto tr188;
	} else
		goto tr185;
	goto st0;
tr269:
#line 180 "src/lib/uri/uri_parser.rl"
	{ uri->scheme = scheme; uri->scheme_len = scheme_len;}
#line 139 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 140 "src/lib/uri/uri_parser.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st224;
st224:
	if ( ++p == pe )
		goto _test_eof224;
case 224:
#line 4450 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st153;
		case 35: goto tr157;
		case 37: goto st11;
		case 47: goto st225;
		case 61: goto st153;
		case 63: goto tr162;
		case 95: goto st153;
		case 124: goto st153;
		case 126: goto st153;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st153;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st153;
	} else
		goto st153;
	goto st0;
st225:
	if ( ++p == pe )
		goto _test_eof225;
case 225:
	switch( (*p) ) {
		case 33: goto tr271;
		case 35: goto tr157;
		case 37: goto tr272;
		case 46: goto tr273;
		case 47: goto tr274;
		case 58: goto st153;
		case 59: goto tr271;
		case 61: goto tr271;
		case 63: goto tr162;
		case 64: goto st153;
		case 91: goto st53;
		case 95: goto tr271;
		case 117: goto tr276;
		case 124: goto st153;
		case 126: goto tr271;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto tr271;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto tr271;
		} else if ( (*p) >= 65 )
			goto tr271;
	} else
		goto tr275;
	goto st0;
tr271:
#line 145 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 101 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st226;
st226:
	if ( ++p == pe )
		goto _test_eof226;
case 226:
#line 4514 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st226;
		case 35: goto tr166;
		case 37: goto st134;
		case 47: goto tr168;
		case 58: goto tr278;
		case 61: goto st226;
		case 63: goto tr170;
		case 64: goto tr279;
		case 95: goto st226;
		case 124: goto st153;
		case 126: goto st226;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st226;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st226;
	} else
		goto st226;
	goto st0;
tr272:
#line 145 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 101 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st134;
st134:
	if ( ++p == pe )
		goto _test_eof134;
case 134:
#line 4547 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 37: goto st226;
		case 117: goto st135;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st226;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st226;
	} else
		goto st226;
	goto st0;
st135:
	if ( ++p == pe )
		goto _test_eof135;
case 135:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st136;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st136;
	} else
		goto st136;
	goto st0;
st136:
	if ( ++p == pe )
		goto _test_eof136;
case 136:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st137;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st137;
	} else
		goto st137;
	goto st0;
st137:
	if ( ++p == pe )
		goto _test_eof137;
case 137:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st138;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st138;
	} else
		goto st138;
	goto st0;
st138:
	if ( ++p == pe )
		goto _test_eof138;
case 138:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st226;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st226;
	} else
		goto st226;
	goto st0;
tr278:
#line 146 "src/lib/uri/uri_parser.rl"
	{ login = s; login_len = p - s; }
#line 102 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st227;
tr328:
#line 146 "src/lib/uri/uri_parser.rl"
	{ login = s; login_len = p - s; }
#line 109 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;
			   uri->host_hint = 1; }
#line 102 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st227;
st227:
	if ( ++p == pe )
		goto _test_eof227;
case 227:
#line 4632 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto tr280;
		case 35: goto tr182;
		case 37: goto tr281;
		case 47: goto tr184;
		case 58: goto st153;
		case 59: goto tr280;
		case 61: goto tr280;
		case 63: goto tr186;
		case 64: goto tr283;
		case 95: goto tr280;
		case 124: goto st153;
		case 126: goto tr280;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto tr280;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto tr284;
		} else if ( (*p) >= 65 )
			goto tr284;
	} else
		goto tr282;
	goto st0;
tr280:
#line 149 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st228;
st228:
	if ( ++p == pe )
		goto _test_eof228;
case 228:
#line 4667 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st228;
		case 35: goto tr157;
		case 37: goto st139;
		case 47: goto st153;
		case 58: goto st153;
		case 61: goto st228;
		case 63: goto tr162;
		case 64: goto tr286;
		case 95: goto st228;
		case 124: goto st153;
		case 126: goto st228;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st228;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st228;
	} else
		goto st228;
	goto st0;
tr281:
#line 149 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st139;
st139:
	if ( ++p == pe )
		goto _test_eof139;
case 139:
#line 4698 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 37: goto st228;
		case 117: goto st140;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st228;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st228;
	} else
		goto st228;
	goto st0;
st140:
	if ( ++p == pe )
		goto _test_eof140;
case 140:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st141;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st141;
	} else
		goto st141;
	goto st0;
st141:
	if ( ++p == pe )
		goto _test_eof141;
case 141:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st142;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st142;
	} else
		goto st142;
	goto st0;
st142:
	if ( ++p == pe )
		goto _test_eof142;
case 142:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st143;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st143;
	} else
		goto st143;
	goto st0;
st143:
	if ( ++p == pe )
		goto _test_eof143;
case 143:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st228;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st228;
	} else
		goto st228;
	goto st0;
tr286:
#line 150 "src/lib/uri/uri_parser.rl"
	{ uri->password = s; uri->password_len = p - s; }
#line 154 "src/lib/uri/uri_parser.rl"
	{ uri->login = login; uri->login_len = login_len; }
	goto st229;
tr279:
#line 146 "src/lib/uri/uri_parser.rl"
	{ login = s; login_len = p - s; }
#line 154 "src/lib/uri/uri_parser.rl"
	{ uri->login = login; uri->login_len = login_len; }
	goto st229;
tr283:
#line 149 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 150 "src/lib/uri/uri_parser.rl"
	{ uri->password = s; uri->password_len = p - s; }
#line 154 "src/lib/uri/uri_parser.rl"
	{ uri->login = login; uri->login_len = login_len; }
	goto st229;
st229:
	if ( ++p == pe )
		goto _test_eof229;
case 229:
#line 4788 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto tr287;
		case 35: goto tr157;
		case 37: goto tr288;
		case 46: goto tr289;
		case 47: goto tr274;
		case 58: goto st153;
		case 59: goto tr287;
		case 61: goto tr287;
		case 63: goto tr162;
		case 64: goto st153;
		case 91: goto st53;
		case 95: goto tr287;
		case 117: goto tr291;
		case 124: goto st153;
		case 126: goto tr287;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto tr287;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto tr287;
		} else if ( (*p) >= 65 )
			goto tr287;
	} else
		goto tr290;
	goto st0;
tr287:
#line 101 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st230;
st230:
	if ( ++p == pe )
		goto _test_eof230;
case 230:
#line 4826 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st230;
		case 35: goto tr166;
		case 37: goto st144;
		case 47: goto tr168;
		case 58: goto tr293;
		case 61: goto st230;
		case 63: goto tr170;
		case 64: goto st153;
		case 95: goto st230;
		case 124: goto st153;
		case 126: goto st230;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st230;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st230;
	} else
		goto st230;
	goto st0;
tr288:
#line 101 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st144;
st144:
	if ( ++p == pe )
		goto _test_eof144;
case 144:
#line 4857 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 37: goto st230;
		case 117: goto st145;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st230;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st230;
	} else
		goto st230;
	goto st0;
st145:
	if ( ++p == pe )
		goto _test_eof145;
case 145:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st146;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st146;
	} else
		goto st146;
	goto st0;
st146:
	if ( ++p == pe )
		goto _test_eof146;
case 146:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st147;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st147;
	} else
		goto st147;
	goto st0;
st147:
	if ( ++p == pe )
		goto _test_eof147;
case 147:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st148;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st148;
	} else
		goto st148;
	goto st0;
st148:
	if ( ++p == pe )
		goto _test_eof148;
case 148:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st230;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st230;
	} else
		goto st230;
	goto st0;
tr293:
#line 102 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st231;
tr308:
#line 109 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;
			   uri->host_hint = 1; }
#line 102 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st231;
st231:
	if ( ++p == pe )
		goto _test_eof231;
case 231:
#line 4938 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st153;
		case 35: goto tr182;
		case 37: goto st11;
		case 47: goto tr184;
		case 61: goto st153;
		case 63: goto tr186;
		case 64: goto st153;
		case 95: goto st153;
		case 124: goto st153;
		case 126: goto st153;
	}
	if ( (*p) < 58 ) {
		if ( (*p) > 46 ) {
			if ( 48 <= (*p) && (*p) <= 57 )
				goto tr294;
		} else if ( (*p) >= 36 )
			goto st153;
	} else if ( (*p) > 59 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto tr295;
		} else if ( (*p) >= 65 )
			goto tr295;
	} else
		goto st153;
	goto st0;
tr294:
#line 139 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st232;
st232:
	if ( ++p == pe )
		goto _test_eof232;
case 232:
#line 4974 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st153;
		case 35: goto tr193;
		case 37: goto st11;
		case 47: goto tr194;
		case 61: goto st153;
		case 63: goto tr196;
		case 95: goto st153;
		case 124: goto st153;
		case 126: goto st153;
	}
	if ( (*p) < 58 ) {
		if ( (*p) > 46 ) {
			if ( 48 <= (*p) && (*p) <= 57 )
				goto st232;
		} else if ( (*p) >= 36 )
			goto st153;
	} else if ( (*p) > 59 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st153;
		} else if ( (*p) >= 64 )
			goto st153;
	} else
		goto st153;
	goto st0;
tr295:
#line 139 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st233;
st233:
	if ( ++p == pe )
		goto _test_eof233;
case 233:
#line 5009 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st153;
		case 35: goto tr193;
		case 37: goto st11;
		case 47: goto tr194;
		case 61: goto st153;
		case 63: goto tr196;
		case 64: goto st153;
		case 95: goto st153;
		case 124: goto st153;
		case 126: goto st153;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st153;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st233;
	} else
		goto st233;
	goto st0;
tr289:
#line 101 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 159 "src/lib/uri/uri_parser.rl"
	{ s = p;}
	goto st234;
st234:
	if ( ++p == pe )
		goto _test_eof234;
case 234:
#line 5041 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st230;
		case 35: goto tr166;
		case 37: goto st144;
		case 47: goto tr198;
		case 58: goto tr293;
		case 61: goto st230;
		case 63: goto tr170;
		case 64: goto st153;
		case 95: goto st230;
		case 124: goto st153;
		case 126: goto st230;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st230;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st230;
	} else
		goto st230;
	goto st0;
tr290:
#line 108 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 101 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st235;
st235:
	if ( ++p == pe )
		goto _test_eof235;
case 235:
#line 5074 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st230;
		case 35: goto tr166;
		case 37: goto st144;
		case 46: goto st236;
		case 47: goto tr168;
		case 58: goto tr293;
		case 59: goto st230;
		case 61: goto st230;
		case 63: goto tr170;
		case 64: goto st153;
		case 95: goto st230;
		case 124: goto st153;
		case 126: goto st230;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st230;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st230;
		} else if ( (*p) >= 65 )
			goto st230;
	} else
		goto st248;
	goto st0;
st236:
	if ( ++p == pe )
		goto _test_eof236;
case 236:
	switch( (*p) ) {
		case 33: goto st230;
		case 35: goto tr166;
		case 37: goto st144;
		case 47: goto tr168;
		case 58: goto tr293;
		case 59: goto st230;
		case 61: goto st230;
		case 63: goto tr170;
		case 64: goto st153;
		case 95: goto st230;
		case 124: goto st153;
		case 126: goto st230;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st230;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st230;
		} else if ( (*p) >= 65 )
			goto st230;
	} else
		goto st237;
	goto st0;
st237:
	if ( ++p == pe )
		goto _test_eof237;
case 237:
	switch( (*p) ) {
		case 33: goto st230;
		case 35: goto tr166;
		case 37: goto st144;
		case 46: goto st238;
		case 47: goto tr168;
		case 58: goto tr293;
		case 59: goto st230;
		case 61: goto st230;
		case 63: goto tr170;
		case 64: goto st153;
		case 95: goto st230;
		case 124: goto st153;
		case 126: goto st230;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st230;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st230;
		} else if ( (*p) >= 65 )
			goto st230;
	} else
		goto st246;
	goto st0;
st238:
	if ( ++p == pe )
		goto _test_eof238;
case 238:
	switch( (*p) ) {
		case 33: goto st230;
		case 35: goto tr166;
		case 37: goto st144;
		case 47: goto tr168;
		case 58: goto tr293;
		case 59: goto st230;
		case 61: goto st230;
		case 63: goto tr170;
		case 64: goto st153;
		case 95: goto st230;
		case 124: goto st153;
		case 126: goto st230;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st230;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st230;
		} else if ( (*p) >= 65 )
			goto st230;
	} else
		goto st239;
	goto st0;
st239:
	if ( ++p == pe )
		goto _test_eof239;
case 239:
	switch( (*p) ) {
		case 33: goto st230;
		case 35: goto tr166;
		case 37: goto st144;
		case 46: goto st240;
		case 47: goto tr168;
		case 58: goto tr293;
		case 59: goto st230;
		case 61: goto st230;
		case 63: goto tr170;
		case 64: goto st153;
		case 95: goto st230;
		case 124: goto st153;
		case 126: goto st230;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st230;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st230;
		} else if ( (*p) >= 65 )
			goto st230;
	} else
		goto st244;
	goto st0;
st240:
	if ( ++p == pe )
		goto _test_eof240;
case 240:
	switch( (*p) ) {
		case 33: goto st230;
		case 35: goto tr166;
		case 37: goto st144;
		case 47: goto tr168;
		case 58: goto tr293;
		case 59: goto st230;
		case 61: goto st230;
		case 63: goto tr170;
		case 64: goto st153;
		case 95: goto st230;
		case 124: goto st153;
		case 126: goto st230;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st230;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st230;
		} else if ( (*p) >= 65 )
			goto st230;
	} else
		goto st241;
	goto st0;
st241:
	if ( ++p == pe )
		goto _test_eof241;
case 241:
	switch( (*p) ) {
		case 33: goto st230;
		case 35: goto tr220;
		case 37: goto st144;
		case 47: goto tr221;
		case 58: goto tr308;
		case 59: goto st230;
		case 61: goto st230;
		case 63: goto tr224;
		case 64: goto st153;
		case 95: goto st230;
		case 124: goto st153;
		case 126: goto st230;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st230;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st230;
		} else if ( (*p) >= 65 )
			goto st230;
	} else
		goto st242;
	goto st0;
st242:
	if ( ++p == pe )
		goto _test_eof242;
case 242:
	switch( (*p) ) {
		case 33: goto st230;
		case 35: goto tr220;
		case 37: goto st144;
		case 47: goto tr221;
		case 58: goto tr308;
		case 59: goto st230;
		case 61: goto st230;
		case 63: goto tr224;
		case 64: goto st153;
		case 95: goto st230;
		case 124: goto st153;
		case 126: goto st230;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st230;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st230;
		} else if ( (*p) >= 65 )
			goto st230;
	} else
		goto st243;
	goto st0;
st243:
	if ( ++p == pe )
		goto _test_eof243;
case 243:
	switch( (*p) ) {
		case 33: goto st230;
		case 35: goto tr220;
		case 37: goto st144;
		case 47: goto tr221;
		case 58: goto tr308;
		case 61: goto st230;
		case 63: goto tr224;
		case 64: goto st153;
		case 95: goto st230;
		case 124: goto st153;
		case 126: goto st230;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st230;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st230;
	} else
		goto st230;
	goto st0;
st244:
	if ( ++p == pe )
		goto _test_eof244;
case 244:
	switch( (*p) ) {
		case 33: goto st230;
		case 35: goto tr166;
		case 37: goto st144;
		case 46: goto st240;
		case 47: goto tr168;
		case 58: goto tr293;
		case 59: goto st230;
		case 61: goto st230;
		case 63: goto tr170;
		case 64: goto st153;
		case 95: goto st230;
		case 124: goto st153;
		case 126: goto st230;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st230;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st230;
		} else if ( (*p) >= 65 )
			goto st230;
	} else
		goto st245;
	goto st0;
st245:
	if ( ++p == pe )
		goto _test_eof245;
case 245:
	switch( (*p) ) {
		case 33: goto st230;
		case 35: goto tr166;
		case 37: goto st144;
		case 46: goto st240;
		case 47: goto tr168;
		case 58: goto tr293;
		case 61: goto st230;
		case 63: goto tr170;
		case 64: goto st153;
		case 95: goto st230;
		case 124: goto st153;
		case 126: goto st230;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st230;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st230;
	} else
		goto st230;
	goto st0;
st246:
	if ( ++p == pe )
		goto _test_eof246;
case 246:
	switch( (*p) ) {
		case 33: goto st230;
		case 35: goto tr166;
		case 37: goto st144;
		case 46: goto st238;
		case 47: goto tr168;
		case 58: goto tr293;
		case 59: goto st230;
		case 61: goto st230;
		case 63: goto tr170;
		case 64: goto st153;
		case 95: goto st230;
		case 124: goto st153;
		case 126: goto st230;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st230;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st230;
		} else if ( (*p) >= 65 )
			goto st230;
	} else
		goto st247;
	goto st0;
st247:
	if ( ++p == pe )
		goto _test_eof247;
case 247:
	switch( (*p) ) {
		case 33: goto st230;
		case 35: goto tr166;
		case 37: goto st144;
		case 46: goto st238;
		case 47: goto tr168;
		case 58: goto tr293;
		case 61: goto st230;
		case 63: goto tr170;
		case 64: goto st153;
		case 95: goto st230;
		case 124: goto st153;
		case 126: goto st230;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st230;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st230;
	} else
		goto st230;
	goto st0;
st248:
	if ( ++p == pe )
		goto _test_eof248;
case 248:
	switch( (*p) ) {
		case 33: goto st230;
		case 35: goto tr166;
		case 37: goto st144;
		case 46: goto st236;
		case 47: goto tr168;
		case 58: goto tr293;
		case 59: goto st230;
		case 61: goto st230;
		case 63: goto tr170;
		case 64: goto st153;
		case 95: goto st230;
		case 124: goto st153;
		case 126: goto st230;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st230;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st230;
		} else if ( (*p) >= 65 )
			goto st230;
	} else
		goto st249;
	goto st0;
st249:
	if ( ++p == pe )
		goto _test_eof249;
case 249:
	switch( (*p) ) {
		case 33: goto st230;
		case 35: goto tr166;
		case 37: goto st144;
		case 46: goto st236;
		case 47: goto tr168;
		case 58: goto tr293;
		case 61: goto st230;
		case 63: goto tr170;
		case 64: goto st153;
		case 95: goto st230;
		case 124: goto st153;
		case 126: goto st230;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st230;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st230;
	} else
		goto st230;
	goto st0;
tr291:
#line 101 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st250;
st250:
	if ( ++p == pe )
		goto _test_eof250;
case 250:
#line 5522 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st230;
		case 35: goto tr166;
		case 37: goto st144;
		case 47: goto tr168;
		case 58: goto tr293;
		case 61: goto st230;
		case 63: goto tr170;
		case 64: goto st153;
		case 95: goto st230;
		case 110: goto st251;
		case 124: goto st153;
		case 126: goto st230;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st230;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st230;
	} else
		goto st230;
	goto st0;
st251:
	if ( ++p == pe )
		goto _test_eof251;
case 251:
	switch( (*p) ) {
		case 33: goto st230;
		case 35: goto tr166;
		case 37: goto st144;
		case 47: goto tr168;
		case 58: goto tr293;
		case 61: goto st230;
		case 63: goto tr170;
		case 64: goto st153;
		case 95: goto st230;
		case 105: goto st252;
		case 124: goto st153;
		case 126: goto st230;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st230;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st230;
	} else
		goto st230;
	goto st0;
st252:
	if ( ++p == pe )
		goto _test_eof252;
case 252:
	switch( (*p) ) {
		case 33: goto st230;
		case 35: goto tr166;
		case 37: goto st144;
		case 47: goto tr168;
		case 58: goto tr293;
		case 61: goto st230;
		case 63: goto tr170;
		case 64: goto st153;
		case 95: goto st230;
		case 120: goto st253;
		case 124: goto st153;
		case 126: goto st230;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st230;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st230;
	} else
		goto st230;
	goto st0;
st253:
	if ( ++p == pe )
		goto _test_eof253;
case 253:
	switch( (*p) ) {
		case 33: goto st230;
		case 35: goto tr166;
		case 37: goto st144;
		case 47: goto tr233;
		case 58: goto tr293;
		case 61: goto st230;
		case 63: goto tr170;
		case 64: goto st153;
		case 95: goto st230;
		case 124: goto st153;
		case 126: goto st230;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st230;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st230;
	} else
		goto st230;
	goto st0;
tr282:
#line 149 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 139 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st254;
st254:
	if ( ++p == pe )
		goto _test_eof254;
case 254:
#line 5636 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st228;
		case 35: goto tr193;
		case 37: goto st139;
		case 47: goto tr194;
		case 58: goto st153;
		case 59: goto st228;
		case 61: goto st228;
		case 63: goto tr196;
		case 64: goto tr286;
		case 95: goto st228;
		case 124: goto st153;
		case 126: goto st228;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st228;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st228;
		} else if ( (*p) >= 65 )
			goto st228;
	} else
		goto st254;
	goto st0;
tr284:
#line 149 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 139 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st255;
st255:
	if ( ++p == pe )
		goto _test_eof255;
case 255:
#line 5673 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st228;
		case 35: goto tr193;
		case 37: goto st139;
		case 47: goto tr194;
		case 58: goto st153;
		case 61: goto st228;
		case 63: goto tr196;
		case 64: goto tr286;
		case 95: goto st228;
		case 124: goto st153;
		case 126: goto st228;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st228;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st255;
	} else
		goto st255;
	goto st0;
tr273:
#line 145 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 101 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 159 "src/lib/uri/uri_parser.rl"
	{ s = p;}
	goto st256;
st256:
	if ( ++p == pe )
		goto _test_eof256;
case 256:
#line 5708 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st226;
		case 35: goto tr166;
		case 37: goto st134;
		case 47: goto tr198;
		case 58: goto tr278;
		case 61: goto st226;
		case 63: goto tr170;
		case 64: goto tr279;
		case 95: goto st226;
		case 124: goto st153;
		case 126: goto st226;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st226;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st226;
	} else
		goto st226;
	goto st0;
tr275:
#line 145 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 108 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 101 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st257;
st257:
	if ( ++p == pe )
		goto _test_eof257;
case 257:
#line 5743 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st226;
		case 35: goto tr166;
		case 37: goto st134;
		case 46: goto st258;
		case 47: goto tr168;
		case 58: goto tr278;
		case 59: goto st226;
		case 61: goto st226;
		case 63: goto tr170;
		case 64: goto tr279;
		case 95: goto st226;
		case 124: goto st153;
		case 126: goto st226;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st226;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st226;
		} else if ( (*p) >= 65 )
			goto st226;
	} else
		goto st270;
	goto st0;
st258:
	if ( ++p == pe )
		goto _test_eof258;
case 258:
	switch( (*p) ) {
		case 33: goto st226;
		case 35: goto tr166;
		case 37: goto st134;
		case 47: goto tr168;
		case 58: goto tr278;
		case 59: goto st226;
		case 61: goto st226;
		case 63: goto tr170;
		case 64: goto tr279;
		case 95: goto st226;
		case 124: goto st153;
		case 126: goto st226;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st226;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st226;
		} else if ( (*p) >= 65 )
			goto st226;
	} else
		goto st259;
	goto st0;
st259:
	if ( ++p == pe )
		goto _test_eof259;
case 259:
	switch( (*p) ) {
		case 33: goto st226;
		case 35: goto tr166;
		case 37: goto st134;
		case 46: goto st260;
		case 47: goto tr168;
		case 58: goto tr278;
		case 59: goto st226;
		case 61: goto st226;
		case 63: goto tr170;
		case 64: goto tr279;
		case 95: goto st226;
		case 124: goto st153;
		case 126: goto st226;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st226;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st226;
		} else if ( (*p) >= 65 )
			goto st226;
	} else
		goto st268;
	goto st0;
st260:
	if ( ++p == pe )
		goto _test_eof260;
case 260:
	switch( (*p) ) {
		case 33: goto st226;
		case 35: goto tr166;
		case 37: goto st134;
		case 47: goto tr168;
		case 58: goto tr278;
		case 59: goto st226;
		case 61: goto st226;
		case 63: goto tr170;
		case 64: goto tr279;
		case 95: goto st226;
		case 124: goto st153;
		case 126: goto st226;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st226;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st226;
		} else if ( (*p) >= 65 )
			goto st226;
	} else
		goto st261;
	goto st0;
st261:
	if ( ++p == pe )
		goto _test_eof261;
case 261:
	switch( (*p) ) {
		case 33: goto st226;
		case 35: goto tr166;
		case 37: goto st134;
		case 46: goto st262;
		case 47: goto tr168;
		case 58: goto tr278;
		case 59: goto st226;
		case 61: goto st226;
		case 63: goto tr170;
		case 64: goto tr279;
		case 95: goto st226;
		case 124: goto st153;
		case 126: goto st226;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st226;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st226;
		} else if ( (*p) >= 65 )
			goto st226;
	} else
		goto st266;
	goto st0;
st262:
	if ( ++p == pe )
		goto _test_eof262;
case 262:
	switch( (*p) ) {
		case 33: goto st226;
		case 35: goto tr166;
		case 37: goto st134;
		case 47: goto tr168;
		case 58: goto tr278;
		case 59: goto st226;
		case 61: goto st226;
		case 63: goto tr170;
		case 64: goto tr279;
		case 95: goto st226;
		case 124: goto st153;
		case 126: goto st226;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st226;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st226;
		} else if ( (*p) >= 65 )
			goto st226;
	} else
		goto st263;
	goto st0;
st263:
	if ( ++p == pe )
		goto _test_eof263;
case 263:
	switch( (*p) ) {
		case 33: goto st226;
		case 35: goto tr220;
		case 37: goto st134;
		case 47: goto tr221;
		case 58: goto tr328;
		case 59: goto st226;
		case 61: goto st226;
		case 63: goto tr224;
		case 64: goto tr279;
		case 95: goto st226;
		case 124: goto st153;
		case 126: goto st226;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st226;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st226;
		} else if ( (*p) >= 65 )
			goto st226;
	} else
		goto st264;
	goto st0;
st264:
	if ( ++p == pe )
		goto _test_eof264;
case 264:
	switch( (*p) ) {
		case 33: goto st226;
		case 35: goto tr220;
		case 37: goto st134;
		case 47: goto tr221;
		case 58: goto tr328;
		case 59: goto st226;
		case 61: goto st226;
		case 63: goto tr224;
		case 64: goto tr279;
		case 95: goto st226;
		case 124: goto st153;
		case 126: goto st226;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st226;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st226;
		} else if ( (*p) >= 65 )
			goto st226;
	} else
		goto st265;
	goto st0;
st265:
	if ( ++p == pe )
		goto _test_eof265;
case 265:
	switch( (*p) ) {
		case 33: goto st226;
		case 35: goto tr220;
		case 37: goto st134;
		case 47: goto tr221;
		case 58: goto tr328;
		case 61: goto st226;
		case 63: goto tr224;
		case 64: goto tr279;
		case 95: goto st226;
		case 124: goto st153;
		case 126: goto st226;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st226;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st226;
	} else
		goto st226;
	goto st0;
st266:
	if ( ++p == pe )
		goto _test_eof266;
case 266:
	switch( (*p) ) {
		case 33: goto st226;
		case 35: goto tr166;
		case 37: goto st134;
		case 46: goto st262;
		case 47: goto tr168;
		case 58: goto tr278;
		case 59: goto st226;
		case 61: goto st226;
		case 63: goto tr170;
		case 64: goto tr279;
		case 95: goto st226;
		case 124: goto st153;
		case 126: goto st226;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st226;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st226;
		} else if ( (*p) >= 65 )
			goto st226;
	} else
		goto st267;
	goto st0;
st267:
	if ( ++p == pe )
		goto _test_eof267;
case 267:
	switch( (*p) ) {
		case 33: goto st226;
		case 35: goto tr166;
		case 37: goto st134;
		case 46: goto st262;
		case 47: goto tr168;
		case 58: goto tr278;
		case 61: goto st226;
		case 63: goto tr170;
		case 64: goto tr279;
		case 95: goto st226;
		case 124: goto st153;
		case 126: goto st226;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st226;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st226;
	} else
		goto st226;
	goto st0;
st268:
	if ( ++p == pe )
		goto _test_eof268;
case 268:
	switch( (*p) ) {
		case 33: goto st226;
		case 35: goto tr166;
		case 37: goto st134;
		case 46: goto st260;
		case 47: goto tr168;
		case 58: goto tr278;
		case 59: goto st226;
		case 61: goto st226;
		case 63: goto tr170;
		case 64: goto tr279;
		case 95: goto st226;
		case 124: goto st153;
		case 126: goto st226;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st226;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st226;
		} else if ( (*p) >= 65 )
			goto st226;
	} else
		goto st269;
	goto st0;
st269:
	if ( ++p == pe )
		goto _test_eof269;
case 269:
	switch( (*p) ) {
		case 33: goto st226;
		case 35: goto tr166;
		case 37: goto st134;
		case 46: goto st260;
		case 47: goto tr168;
		case 58: goto tr278;
		case 61: goto st226;
		case 63: goto tr170;
		case 64: goto tr279;
		case 95: goto st226;
		case 124: goto st153;
		case 126: goto st226;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st226;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st226;
	} else
		goto st226;
	goto st0;
st270:
	if ( ++p == pe )
		goto _test_eof270;
case 270:
	switch( (*p) ) {
		case 33: goto st226;
		case 35: goto tr166;
		case 37: goto st134;
		case 46: goto st258;
		case 47: goto tr168;
		case 58: goto tr278;
		case 59: goto st226;
		case 61: goto st226;
		case 63: goto tr170;
		case 64: goto tr279;
		case 95: goto st226;
		case 124: goto st153;
		case 126: goto st226;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st226;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st226;
		} else if ( (*p) >= 65 )
			goto st226;
	} else
		goto st271;
	goto st0;
st271:
	if ( ++p == pe )
		goto _test_eof271;
case 271:
	switch( (*p) ) {
		case 33: goto st226;
		case 35: goto tr166;
		case 37: goto st134;
		case 46: goto st258;
		case 47: goto tr168;
		case 58: goto tr278;
		case 61: goto st226;
		case 63: goto tr170;
		case 64: goto tr279;
		case 95: goto st226;
		case 124: goto st153;
		case 126: goto st226;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st226;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st226;
	} else
		goto st226;
	goto st0;
tr276:
#line 145 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 101 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st272;
st272:
	if ( ++p == pe )
		goto _test_eof272;
case 272:
#line 6193 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st226;
		case 35: goto tr166;
		case 37: goto st134;
		case 47: goto tr168;
		case 58: goto tr278;
		case 61: goto st226;
		case 63: goto tr170;
		case 64: goto tr279;
		case 95: goto st226;
		case 110: goto st273;
		case 124: goto st153;
		case 126: goto st226;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st226;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st226;
	} else
		goto st226;
	goto st0;
st273:
	if ( ++p == pe )
		goto _test_eof273;
case 273:
	switch( (*p) ) {
		case 33: goto st226;
		case 35: goto tr166;
		case 37: goto st134;
		case 47: goto tr168;
		case 58: goto tr278;
		case 61: goto st226;
		case 63: goto tr170;
		case 64: goto tr279;
		case 95: goto st226;
		case 105: goto st274;
		case 124: goto st153;
		case 126: goto st226;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st226;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st226;
	} else
		goto st226;
	goto st0;
st274:
	if ( ++p == pe )
		goto _test_eof274;
case 274:
	switch( (*p) ) {
		case 33: goto st226;
		case 35: goto tr166;
		case 37: goto st134;
		case 47: goto tr168;
		case 58: goto tr278;
		case 61: goto st226;
		case 63: goto tr170;
		case 64: goto tr279;
		case 95: goto st226;
		case 120: goto st275;
		case 124: goto st153;
		case 126: goto st226;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st226;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st226;
	} else
		goto st226;
	goto st0;
st275:
	if ( ++p == pe )
		goto _test_eof275;
case 275:
	switch( (*p) ) {
		case 33: goto st226;
		case 35: goto tr166;
		case 37: goto st134;
		case 47: goto tr336;
		case 58: goto tr278;
		case 61: goto st226;
		case 63: goto tr170;
		case 64: goto tr279;
		case 95: goto st226;
		case 124: goto st153;
		case 126: goto st226;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st226;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st226;
	} else
		goto st226;
	goto st0;
tr336:
#line 102 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st276;
st276:
	if ( ++p == pe )
		goto _test_eof276;
case 276:
#line 6307 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st153;
		case 35: goto tr157;
		case 37: goto st11;
		case 58: goto st277;
		case 61: goto st153;
		case 63: goto tr162;
		case 95: goto st153;
		case 124: goto st153;
		case 126: goto st153;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st153;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st153;
	} else
		goto st153;
	goto st0;
st277:
	if ( ++p == pe )
		goto _test_eof277;
case 277:
	switch( (*p) ) {
		case 33: goto st153;
		case 35: goto tr157;
		case 37: goto st11;
		case 46: goto tr338;
		case 47: goto tr339;
		case 61: goto st153;
		case 63: goto tr162;
		case 95: goto st153;
		case 124: goto st153;
		case 126: goto st153;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st153;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st153;
	} else
		goto st153;
	goto st0;
tr338:
#line 136 "src/lib/uri/uri_parser.rl"
	{ s = p;}
	goto st278;
st278:
	if ( ++p == pe )
		goto _test_eof278;
case 278:
#line 6361 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st153;
		case 35: goto tr157;
		case 37: goto st11;
		case 47: goto st199;
		case 61: goto st153;
		case 63: goto tr162;
		case 95: goto st153;
		case 124: goto st153;
		case 126: goto st153;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st153;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st153;
	} else
		goto st153;
	goto st0;
tr165:
#line 161 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 145 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 101 "src/lib/uri/uri_parser.rl"
	{ s = p; }
	goto st279;
st279:
	if ( ++p == pe )
		goto _test_eof279;
case 279:
#line 6394 "src/lib/uri/uri_parser.c"
	switch( (*p) ) {
		case 33: goto st150;
		case 35: goto tr166;
		case 37: goto st6;
		case 43: goto st222;
		case 47: goto tr168;
		case 58: goto tr268;
		case 59: goto st150;
		case 61: goto st150;
		case 63: goto tr170;
		case 64: goto tr171;
		case 95: goto st150;
		case 110: goto st280;
		case 126: goto st150;
	}
	if ( (*p) < 45 ) {
		if ( 36 <= (*p) && (*p) <= 44 )
			goto st150;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st222;
		} else if ( (*p) >= 65 )
			goto st222;
	} else
		goto st222;
	goto st0;
st280:
	if ( ++p == pe )
		goto _test_eof280;
case 280:
	switch( (*p) ) {
		case 33: goto st150;
		case 35: goto tr166;
		case 37: goto st6;
		case 43: goto st222;
		case 47: goto tr168;
		case 58: goto tr268;
		case 59: goto st150;
		case 61: goto st150;
		case 63: goto tr170;
		case 64: goto tr171;
		case 95: goto st150;
		case 105: goto st281;
		case 126: goto st150;
	}
	if ( (*p) < 45 ) {
		if ( 36 <= (*p) && (*p) <= 44 )
			goto st150;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st222;
		} else if ( (*p) >= 65 )
			goto st222;
	} else
		goto st222;
	goto st0;
st281:
	if ( ++p == pe )
		goto _test_eof281;
case 281:
	switch( (*p) ) {
		case 33: goto st150;
		case 35: goto tr166;
		case 37: goto st6;
		case 43: goto st222;
		case 47: goto tr168;
		case 58: goto tr268;
		case 59: goto st150;
		case 61: goto st150;
		case 63: goto tr170;
		case 64: goto tr171;
		case 95: goto st150;
		case 120: goto st282;
		case 126: goto st150;
	}
	if ( (*p) < 45 ) {
		if ( 36 <= (*p) && (*p) <= 44 )
			goto st150;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st222;
		} else if ( (*p) >= 65 )
			goto st222;
	} else
		goto st222;
	goto st0;
st282:
	if ( ++p == pe )
		goto _test_eof282;
case 282:
	switch( (*p) ) {
		case 33: goto st150;
		case 35: goto tr166;
		case 37: goto st6;
		case 43: goto st222;
		case 47: goto tr336;
		case 58: goto tr268;
		case 59: goto st150;
		case 61: goto st150;
		case 63: goto tr170;
		case 64: goto tr171;
		case 95: goto st150;
		case 126: goto st150;
	}
	if ( (*p) < 45 ) {
		if ( 36 <= (*p) && (*p) <= 44 )
			goto st150;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st222;
		} else if ( (*p) >= 65 )
			goto st222;
	} else
		goto st222;
	goto st0;
	}
	_test_eof150: cs = 150; goto _test_eof; 
	_test_eof151: cs = 151; goto _test_eof; 
	_test_eof152: cs = 152; goto _test_eof; 
	_test_eof1: cs = 1; goto _test_eof; 
	_test_eof2: cs = 2; goto _test_eof; 
	_test_eof3: cs = 3; goto _test_eof; 
	_test_eof4: cs = 4; goto _test_eof; 
	_test_eof5: cs = 5; goto _test_eof; 
	_test_eof6: cs = 6; goto _test_eof; 
	_test_eof7: cs = 7; goto _test_eof; 
	_test_eof8: cs = 8; goto _test_eof; 
	_test_eof9: cs = 9; goto _test_eof; 
	_test_eof10: cs = 10; goto _test_eof; 
	_test_eof153: cs = 153; goto _test_eof; 
	_test_eof11: cs = 11; goto _test_eof; 
	_test_eof12: cs = 12; goto _test_eof; 
	_test_eof13: cs = 13; goto _test_eof; 
	_test_eof14: cs = 14; goto _test_eof; 
	_test_eof15: cs = 15; goto _test_eof; 
	_test_eof154: cs = 154; goto _test_eof; 
	_test_eof155: cs = 155; goto _test_eof; 
	_test_eof16: cs = 16; goto _test_eof; 
	_test_eof17: cs = 17; goto _test_eof; 
	_test_eof18: cs = 18; goto _test_eof; 
	_test_eof19: cs = 19; goto _test_eof; 
	_test_eof20: cs = 20; goto _test_eof; 
	_test_eof156: cs = 156; goto _test_eof; 
	_test_eof21: cs = 21; goto _test_eof; 
	_test_eof22: cs = 22; goto _test_eof; 
	_test_eof23: cs = 23; goto _test_eof; 
	_test_eof24: cs = 24; goto _test_eof; 
	_test_eof25: cs = 25; goto _test_eof; 
	_test_eof26: cs = 26; goto _test_eof; 
	_test_eof27: cs = 27; goto _test_eof; 
	_test_eof157: cs = 157; goto _test_eof; 
	_test_eof28: cs = 28; goto _test_eof; 
	_test_eof29: cs = 29; goto _test_eof; 
	_test_eof30: cs = 30; goto _test_eof; 
	_test_eof31: cs = 31; goto _test_eof; 
	_test_eof32: cs = 32; goto _test_eof; 
	_test_eof158: cs = 158; goto _test_eof; 
	_test_eof159: cs = 159; goto _test_eof; 
	_test_eof160: cs = 160; goto _test_eof; 
	_test_eof161: cs = 161; goto _test_eof; 
	_test_eof162: cs = 162; goto _test_eof; 
	_test_eof163: cs = 163; goto _test_eof; 
	_test_eof33: cs = 33; goto _test_eof; 
	_test_eof34: cs = 34; goto _test_eof; 
	_test_eof35: cs = 35; goto _test_eof; 
	_test_eof36: cs = 36; goto _test_eof; 
	_test_eof37: cs = 37; goto _test_eof; 
	_test_eof164: cs = 164; goto _test_eof; 
	_test_eof165: cs = 165; goto _test_eof; 
	_test_eof166: cs = 166; goto _test_eof; 
	_test_eof38: cs = 38; goto _test_eof; 
	_test_eof39: cs = 39; goto _test_eof; 
	_test_eof40: cs = 40; goto _test_eof; 
	_test_eof41: cs = 41; goto _test_eof; 
	_test_eof42: cs = 42; goto _test_eof; 
	_test_eof167: cs = 167; goto _test_eof; 
	_test_eof168: cs = 168; goto _test_eof; 
	_test_eof43: cs = 43; goto _test_eof; 
	_test_eof44: cs = 44; goto _test_eof; 
	_test_eof45: cs = 45; goto _test_eof; 
	_test_eof46: cs = 46; goto _test_eof; 
	_test_eof47: cs = 47; goto _test_eof; 
	_test_eof169: cs = 169; goto _test_eof; 
	_test_eof170: cs = 170; goto _test_eof; 
	_test_eof48: cs = 48; goto _test_eof; 
	_test_eof49: cs = 49; goto _test_eof; 
	_test_eof50: cs = 50; goto _test_eof; 
	_test_eof51: cs = 51; goto _test_eof; 
	_test_eof52: cs = 52; goto _test_eof; 
	_test_eof171: cs = 171; goto _test_eof; 
	_test_eof172: cs = 172; goto _test_eof; 
	_test_eof173: cs = 173; goto _test_eof; 
	_test_eof174: cs = 174; goto _test_eof; 
	_test_eof175: cs = 175; goto _test_eof; 
	_test_eof176: cs = 176; goto _test_eof; 
	_test_eof177: cs = 177; goto _test_eof; 
	_test_eof178: cs = 178; goto _test_eof; 
	_test_eof179: cs = 179; goto _test_eof; 
	_test_eof180: cs = 180; goto _test_eof; 
	_test_eof181: cs = 181; goto _test_eof; 
	_test_eof182: cs = 182; goto _test_eof; 
	_test_eof183: cs = 183; goto _test_eof; 
	_test_eof184: cs = 184; goto _test_eof; 
	_test_eof185: cs = 185; goto _test_eof; 
	_test_eof53: cs = 53; goto _test_eof; 
	_test_eof54: cs = 54; goto _test_eof; 
	_test_eof55: cs = 55; goto _test_eof; 
	_test_eof56: cs = 56; goto _test_eof; 
	_test_eof57: cs = 57; goto _test_eof; 
	_test_eof58: cs = 58; goto _test_eof; 
	_test_eof59: cs = 59; goto _test_eof; 
	_test_eof60: cs = 60; goto _test_eof; 
	_test_eof61: cs = 61; goto _test_eof; 
	_test_eof62: cs = 62; goto _test_eof; 
	_test_eof63: cs = 63; goto _test_eof; 
	_test_eof64: cs = 64; goto _test_eof; 
	_test_eof65: cs = 65; goto _test_eof; 
	_test_eof66: cs = 66; goto _test_eof; 
	_test_eof67: cs = 67; goto _test_eof; 
	_test_eof68: cs = 68; goto _test_eof; 
	_test_eof69: cs = 69; goto _test_eof; 
	_test_eof70: cs = 70; goto _test_eof; 
	_test_eof71: cs = 71; goto _test_eof; 
	_test_eof72: cs = 72; goto _test_eof; 
	_test_eof73: cs = 73; goto _test_eof; 
	_test_eof74: cs = 74; goto _test_eof; 
	_test_eof75: cs = 75; goto _test_eof; 
	_test_eof76: cs = 76; goto _test_eof; 
	_test_eof77: cs = 77; goto _test_eof; 
	_test_eof78: cs = 78; goto _test_eof; 
	_test_eof79: cs = 79; goto _test_eof; 
	_test_eof80: cs = 80; goto _test_eof; 
	_test_eof81: cs = 81; goto _test_eof; 
	_test_eof82: cs = 82; goto _test_eof; 
	_test_eof83: cs = 83; goto _test_eof; 
	_test_eof84: cs = 84; goto _test_eof; 
	_test_eof85: cs = 85; goto _test_eof; 
	_test_eof86: cs = 86; goto _test_eof; 
	_test_eof87: cs = 87; goto _test_eof; 
	_test_eof88: cs = 88; goto _test_eof; 
	_test_eof89: cs = 89; goto _test_eof; 
	_test_eof90: cs = 90; goto _test_eof; 
	_test_eof91: cs = 91; goto _test_eof; 
	_test_eof92: cs = 92; goto _test_eof; 
	_test_eof93: cs = 93; goto _test_eof; 
	_test_eof94: cs = 94; goto _test_eof; 
	_test_eof95: cs = 95; goto _test_eof; 
	_test_eof96: cs = 96; goto _test_eof; 
	_test_eof97: cs = 97; goto _test_eof; 
	_test_eof186: cs = 186; goto _test_eof; 
	_test_eof98: cs = 98; goto _test_eof; 
	_test_eof99: cs = 99; goto _test_eof; 
	_test_eof100: cs = 100; goto _test_eof; 
	_test_eof101: cs = 101; goto _test_eof; 
	_test_eof102: cs = 102; goto _test_eof; 
	_test_eof103: cs = 103; goto _test_eof; 
	_test_eof104: cs = 104; goto _test_eof; 
	_test_eof105: cs = 105; goto _test_eof; 
	_test_eof106: cs = 106; goto _test_eof; 
	_test_eof107: cs = 107; goto _test_eof; 
	_test_eof108: cs = 108; goto _test_eof; 
	_test_eof109: cs = 109; goto _test_eof; 
	_test_eof110: cs = 110; goto _test_eof; 
	_test_eof111: cs = 111; goto _test_eof; 
	_test_eof112: cs = 112; goto _test_eof; 
	_test_eof113: cs = 113; goto _test_eof; 
	_test_eof114: cs = 114; goto _test_eof; 
	_test_eof115: cs = 115; goto _test_eof; 
	_test_eof116: cs = 116; goto _test_eof; 
	_test_eof117: cs = 117; goto _test_eof; 
	_test_eof118: cs = 118; goto _test_eof; 
	_test_eof187: cs = 187; goto _test_eof; 
	_test_eof188: cs = 188; goto _test_eof; 
	_test_eof189: cs = 189; goto _test_eof; 
	_test_eof190: cs = 190; goto _test_eof; 
	_test_eof191: cs = 191; goto _test_eof; 
	_test_eof192: cs = 192; goto _test_eof; 
	_test_eof193: cs = 193; goto _test_eof; 
	_test_eof194: cs = 194; goto _test_eof; 
	_test_eof195: cs = 195; goto _test_eof; 
	_test_eof119: cs = 119; goto _test_eof; 
	_test_eof120: cs = 120; goto _test_eof; 
	_test_eof121: cs = 121; goto _test_eof; 
	_test_eof122: cs = 122; goto _test_eof; 
	_test_eof123: cs = 123; goto _test_eof; 
	_test_eof196: cs = 196; goto _test_eof; 
	_test_eof197: cs = 197; goto _test_eof; 
	_test_eof198: cs = 198; goto _test_eof; 
	_test_eof199: cs = 199; goto _test_eof; 
	_test_eof200: cs = 200; goto _test_eof; 
	_test_eof124: cs = 124; goto _test_eof; 
	_test_eof125: cs = 125; goto _test_eof; 
	_test_eof126: cs = 126; goto _test_eof; 
	_test_eof127: cs = 127; goto _test_eof; 
	_test_eof128: cs = 128; goto _test_eof; 
	_test_eof201: cs = 201; goto _test_eof; 
	_test_eof202: cs = 202; goto _test_eof; 
	_test_eof129: cs = 129; goto _test_eof; 
	_test_eof130: cs = 130; goto _test_eof; 
	_test_eof131: cs = 131; goto _test_eof; 
	_test_eof132: cs = 132; goto _test_eof; 
	_test_eof133: cs = 133; goto _test_eof; 
	_test_eof203: cs = 203; goto _test_eof; 
	_test_eof204: cs = 204; goto _test_eof; 
	_test_eof205: cs = 205; goto _test_eof; 
	_test_eof206: cs = 206; goto _test_eof; 
	_test_eof207: cs = 207; goto _test_eof; 
	_test_eof208: cs = 208; goto _test_eof; 
	_test_eof209: cs = 209; goto _test_eof; 
	_test_eof210: cs = 210; goto _test_eof; 
	_test_eof211: cs = 211; goto _test_eof; 
	_test_eof212: cs = 212; goto _test_eof; 
	_test_eof213: cs = 213; goto _test_eof; 
	_test_eof214: cs = 214; goto _test_eof; 
	_test_eof215: cs = 215; goto _test_eof; 
	_test_eof216: cs = 216; goto _test_eof; 
	_test_eof217: cs = 217; goto _test_eof; 
	_test_eof218: cs = 218; goto _test_eof; 
	_test_eof219: cs = 219; goto _test_eof; 
	_test_eof220: cs = 220; goto _test_eof; 
	_test_eof221: cs = 221; goto _test_eof; 
	_test_eof222: cs = 222; goto _test_eof; 
	_test_eof223: cs = 223; goto _test_eof; 
	_test_eof224: cs = 224; goto _test_eof; 
	_test_eof225: cs = 225; goto _test_eof; 
	_test_eof226: cs = 226; goto _test_eof; 
	_test_eof134: cs = 134; goto _test_eof; 
	_test_eof135: cs = 135; goto _test_eof; 
	_test_eof136: cs = 136; goto _test_eof; 
	_test_eof137: cs = 137; goto _test_eof; 
	_test_eof138: cs = 138; goto _test_eof; 
	_test_eof227: cs = 227; goto _test_eof; 
	_test_eof228: cs = 228; goto _test_eof; 
	_test_eof139: cs = 139; goto _test_eof; 
	_test_eof140: cs = 140; goto _test_eof; 
	_test_eof141: cs = 141; goto _test_eof; 
	_test_eof142: cs = 142; goto _test_eof; 
	_test_eof143: cs = 143; goto _test_eof; 
	_test_eof229: cs = 229; goto _test_eof; 
	_test_eof230: cs = 230; goto _test_eof; 
	_test_eof144: cs = 144; goto _test_eof; 
	_test_eof145: cs = 145; goto _test_eof; 
	_test_eof146: cs = 146; goto _test_eof; 
	_test_eof147: cs = 147; goto _test_eof; 
	_test_eof148: cs = 148; goto _test_eof; 
	_test_eof231: cs = 231; goto _test_eof; 
	_test_eof232: cs = 232; goto _test_eof; 
	_test_eof233: cs = 233; goto _test_eof; 
	_test_eof234: cs = 234; goto _test_eof; 
	_test_eof235: cs = 235; goto _test_eof; 
	_test_eof236: cs = 236; goto _test_eof; 
	_test_eof237: cs = 237; goto _test_eof; 
	_test_eof238: cs = 238; goto _test_eof; 
	_test_eof239: cs = 239; goto _test_eof; 
	_test_eof240: cs = 240; goto _test_eof; 
	_test_eof241: cs = 241; goto _test_eof; 
	_test_eof242: cs = 242; goto _test_eof; 
	_test_eof243: cs = 243; goto _test_eof; 
	_test_eof244: cs = 244; goto _test_eof; 
	_test_eof245: cs = 245; goto _test_eof; 
	_test_eof246: cs = 246; goto _test_eof; 
	_test_eof247: cs = 247; goto _test_eof; 
	_test_eof248: cs = 248; goto _test_eof; 
	_test_eof249: cs = 249; goto _test_eof; 
	_test_eof250: cs = 250; goto _test_eof; 
	_test_eof251: cs = 251; goto _test_eof; 
	_test_eof252: cs = 252; goto _test_eof; 
	_test_eof253: cs = 253; goto _test_eof; 
	_test_eof254: cs = 254; goto _test_eof; 
	_test_eof255: cs = 255; goto _test_eof; 
	_test_eof256: cs = 256; goto _test_eof; 
	_test_eof257: cs = 257; goto _test_eof; 
	_test_eof258: cs = 258; goto _test_eof; 
	_test_eof259: cs = 259; goto _test_eof; 
	_test_eof260: cs = 260; goto _test_eof; 
	_test_eof261: cs = 261; goto _test_eof; 
	_test_eof262: cs = 262; goto _test_eof; 
	_test_eof263: cs = 263; goto _test_eof; 
	_test_eof264: cs = 264; goto _test_eof; 
	_test_eof265: cs = 265; goto _test_eof; 
	_test_eof266: cs = 266; goto _test_eof; 
	_test_eof267: cs = 267; goto _test_eof; 
	_test_eof268: cs = 268; goto _test_eof; 
	_test_eof269: cs = 269; goto _test_eof; 
	_test_eof270: cs = 270; goto _test_eof; 
	_test_eof271: cs = 271; goto _test_eof; 
	_test_eof272: cs = 272; goto _test_eof; 
	_test_eof273: cs = 273; goto _test_eof; 
	_test_eof274: cs = 274; goto _test_eof; 
	_test_eof275: cs = 275; goto _test_eof; 
	_test_eof276: cs = 276; goto _test_eof; 
	_test_eof277: cs = 277; goto _test_eof; 
	_test_eof278: cs = 278; goto _test_eof; 
	_test_eof279: cs = 279; goto _test_eof; 
	_test_eof280: cs = 280; goto _test_eof; 
	_test_eof281: cs = 281; goto _test_eof; 
	_test_eof282: cs = 282; goto _test_eof; 

	_test_eof: {}
	if ( p == eof )
	{
	switch ( cs ) {
	case 155: 
#line 73 "src/lib/uri/uri_parser.rl"
	{ uri->query = s; uri->query_len = p - s; }
	break;
	case 152: 
#line 77 "src/lib/uri/uri_parser.rl"
	{ uri->fragment = s; uri->fragment_len = p - s; }
	break;
	case 165: 
	case 166: 
	case 167: 
	case 168: 
#line 120 "src/lib/uri/uri_parser.rl"
	{
			/*
			 * This action is also called for path_* terms.
			 * I absolutely have no idea why.
			 */
			if (uri->host_hint != 3) {
				uri->host_hint = 3;
				uri->host = URI_HOST_UNIX;
				uri->host_len = strlen(URI_HOST_UNIX);
				uri->service = s; uri->service_len = p - s;
				/* a workaround for grammar limitations */
				uri->path = NULL;
				uri->path_len = 0;
			};
		}
	break;
	case 149: 
	case 153: 
	case 162: 
	case 163: 
	case 191: 
	case 192: 
	case 193: 
	case 194: 
	case 199: 
	case 221: 
	case 224: 
	case 225: 
	case 228: 
	case 229: 
	case 276: 
	case 277: 
	case 278: 
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
	break;
	case 154: 
#line 72 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 73 "src/lib/uri/uri_parser.rl"
	{ uri->query = s; uri->query_len = p - s; }
	break;
	case 151: 
#line 76 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 77 "src/lib/uri/uri_parser.rl"
	{ uri->fragment = s; uri->fragment_len = p - s; }
	break;
	case 164: 
	case 186: 
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
	break;
	case 170: 
	case 200: 
	case 201: 
	case 202: 
	case 203: 
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 120 "src/lib/uri/uri_parser.rl"
	{
			/*
			 * This action is also called for path_* terms.
			 * I absolutely have no idea why.
			 */
			if (uri->host_hint != 3) {
				uri->host_hint = 3;
				uri->host = URI_HOST_UNIX;
				uri->host_len = strlen(URI_HOST_UNIX);
				uri->service = s; uri->service_len = p - s;
				/* a workaround for grammar limitations */
				uri->path = NULL;
				uri->path_len = 0;
			};
		}
	break;
	case 150: 
	case 157: 
	case 161: 
	case 171: 
	case 172: 
	case 173: 
	case 174: 
	case 175: 
	case 176: 
	case 180: 
	case 181: 
	case 182: 
	case 183: 
	case 184: 
	case 185: 
	case 187: 
	case 188: 
	case 189: 
	case 190: 
	case 198: 
	case 206: 
	case 207: 
	case 208: 
	case 209: 
	case 210: 
	case 214: 
	case 215: 
	case 216: 
	case 217: 
	case 222: 
	case 226: 
	case 230: 
	case 234: 
	case 235: 
	case 236: 
	case 237: 
	case 238: 
	case 239: 
	case 240: 
	case 244: 
	case 245: 
	case 246: 
	case 247: 
	case 248: 
	case 249: 
	case 250: 
	case 251: 
	case 252: 
	case 253: 
	case 256: 
	case 257: 
	case 258: 
	case 259: 
	case 260: 
	case 261: 
	case 262: 
	case 266: 
	case 267: 
	case 268: 
	case 269: 
	case 270: 
	case 271: 
	case 272: 
	case 273: 
	case 274: 
	case 275: 
	case 279: 
	case 280: 
	case 281: 
	case 282: 
#line 102 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
	break;
	case 195: 
#line 120 "src/lib/uri/uri_parser.rl"
	{
			/*
			 * This action is also called for path_* terms.
			 * I absolutely have no idea why.
			 */
			if (uri->host_hint != 3) {
				uri->host_hint = 3;
				uri->host = URI_HOST_UNIX;
				uri->host_len = strlen(URI_HOST_UNIX);
				uri->service = s; uri->service_len = p - s;
				/* a workaround for grammar limitations */
				uri->path = NULL;
				uri->path_len = 0;
			};
		}
#line 171 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
	break;
	case 159: 
	case 160: 
	case 196: 
	case 197: 
	case 232: 
	case 233: 
	case 254: 
	case 255: 
#line 140 "src/lib/uri/uri_parser.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
	break;
	case 169: 
	case 204: 
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 120 "src/lib/uri/uri_parser.rl"
	{
			/*
			 * This action is also called for path_* terms.
			 * I absolutely have no idea why.
			 */
			if (uri->host_hint != 3) {
				uri->host_hint = 3;
				uri->host = URI_HOST_UNIX;
				uri->host_len = strlen(URI_HOST_UNIX);
				uri->service = s; uri->service_len = p - s;
				/* a workaround for grammar limitations */
				uri->path = NULL;
				uri->path_len = 0;
			};
		}
	break;
	case 205: 
	case 218: 
	case 219: 
	case 220: 
#line 102 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 190 "src/lib/uri/uri_parser.rl"
	{ uri->service_len = p - uri->service;
			   uri->host = NULL; uri->host_len = 0; }
	break;
	case 177: 
	case 178: 
	case 179: 
	case 211: 
	case 212: 
	case 213: 
	case 241: 
	case 242: 
	case 243: 
	case 263: 
	case 264: 
	case 265: 
#line 109 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;
			   uri->host_hint = 1; }
#line 102 "src/lib/uri/uri_parser.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
	break;
	case 156: 
	case 158: 
	case 223: 
	case 227: 
	case 231: 
#line 139 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 140 "src/lib/uri/uri_parser.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 172 "src/lib/uri/uri_parser.rl"
	{ s = p; }
#line 176 "src/lib/uri/uri_parser.rl"
	{ uri->path = s; uri->path_len = p - s; }
	break;
#line 7080 "src/lib/uri/uri_parser.c"
	}
	}

	_out: {}
	}

#line 203 "src/lib/uri/uri_parser.rl"


	if (uri->path_len == 0)
		uri->path = NULL;
	if (uri->service_len == 0)
		uri->service = NULL;
	if (uri->service_len >= URI_MAXSERVICE)
		return -1;
	if (uri->host_len >= URI_MAXHOST)
		return -1;

	(void)uri_first_final;
	(void)uri_error;
	(void)uri_en_main;
	(void)eof;

	return cs >= uri_first_final ? 0 : -1;
}

/* vim: set ft=ragel: */
