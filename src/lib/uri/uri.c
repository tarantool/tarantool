
#line 1 "src/uri.rl"
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
#include "uri.h"
#include <trivia/util.h> /* SNPRINT */
#include <string.h>
#include <stdio.h> /* snprintf */
int
uri_parse(struct uri *uri, const char *p)
{
	const char *pe = p + strlen(p);
	const char *eof = pe;
	int cs;
	memset(uri, 0, sizeof(*uri));

	if (p == pe)
		return -1;

	const char *s = NULL, *login = NULL, *scheme = NULL;
	size_t login_len = 0, scheme_len = 0;

	
#line 53 "src/uri.c"
static const int uri_start = 134;
static const int uri_first_final = 134;
static const int uri_error = 0;

static const int uri_en_main = 134;


#line 61 "src/uri.c"
	{
	cs = uri_start;
	}

#line 66 "src/uri.c"
	{
	if ( p == pe )
		goto _test_eof;
	switch ( cs )
	{
case 134:
	switch( (*p) ) {
		case 33: goto tr140;
		case 35: goto tr141;
		case 37: goto tr142;
		case 47: goto tr143;
		case 59: goto tr140;
		case 61: goto tr140;
		case 63: goto tr145;
		case 64: goto st194;
		case 91: goto st38;
		case 95: goto tr140;
		case 117: goto tr148;
		case 126: goto tr140;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto tr140;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto tr147;
		} else if ( (*p) >= 65 )
			goto tr147;
	} else
		goto tr144;
	goto st0;
st0:
cs = 0;
	goto _out;
tr140:
#line 144 "src/uri.rl"
	{ s = p; }
#line 100 "src/uri.rl"
	{ s = p; }
	goto st135;
st135:
	if ( ++p == pe )
		goto _test_eof135;
case 135:
#line 112 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st135;
		case 35: goto tr149;
		case 37: goto st6;
		case 47: goto tr151;
		case 58: goto tr152;
		case 61: goto st135;
		case 63: goto tr153;
		case 64: goto tr154;
		case 95: goto st135;
		case 126: goto st135;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st135;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st135;
	} else
		goto st135;
	goto st0;
tr141:
#line 173 "src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 193 "src/uri.rl"
	{ s = p; }
	goto st136;
tr149:
#line 101 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 169 "src/uri.rl"
	{ s = p; }
#line 173 "src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 193 "src/uri.rl"
	{ s = p; }
	goto st136;
tr160:
#line 71 "src/uri.rl"
	{ s = p; }
#line 72 "src/uri.rl"
	{ uri->query = s; uri->query_len = p - s; }
#line 193 "src/uri.rl"
	{ s = p; }
	goto st136;
tr162:
#line 72 "src/uri.rl"
	{ uri->query = s; uri->query_len = p - s; }
#line 193 "src/uri.rl"
	{ s = p; }
	goto st136;
tr165:
#line 138 "src/uri.rl"
	{ s = p; }
#line 139 "src/uri.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 169 "src/uri.rl"
	{ s = p; }
#line 173 "src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 193 "src/uri.rl"
	{ s = p; }
	goto st136;
tr176:
#line 139 "src/uri.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 169 "src/uri.rl"
	{ s = p; }
#line 173 "src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 193 "src/uri.rl"
	{ s = p; }
	goto st136;
tr191:
#line 108 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;
			   uri->host_hint = 1; }
#line 101 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 169 "src/uri.rl"
	{ s = p; }
#line 173 "src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 193 "src/uri.rl"
	{ s = p; }
	goto st136;
tr200:
#line 169 "src/uri.rl"
	{ s = p; }
#line 173 "src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 193 "src/uri.rl"
	{ s = p; }
	goto st136;
tr213:
#line 119 "src/uri.rl"
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
#line 168 "src/uri.rl"
	{ s = p; }
#line 173 "src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 193 "src/uri.rl"
	{ s = p; }
	goto st136;
tr307:
#line 173 "src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 119 "src/uri.rl"
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
#line 193 "src/uri.rl"
	{ s = p; }
	goto st136;
st136:
	if ( ++p == pe )
		goto _test_eof136;
case 136:
#line 257 "src/uri.c"
	switch( (*p) ) {
		case 33: goto tr155;
		case 37: goto tr156;
		case 61: goto tr155;
		case 95: goto tr155;
		case 124: goto tr155;
		case 126: goto tr155;
	}
	if ( (*p) < 63 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto tr155;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr155;
	} else
		goto tr155;
	goto st0;
tr155:
#line 75 "src/uri.rl"
	{ s = p; }
	goto st137;
st137:
	if ( ++p == pe )
		goto _test_eof137;
case 137:
#line 283 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st137;
		case 37: goto st1;
		case 61: goto st137;
		case 95: goto st137;
		case 124: goto st137;
		case 126: goto st137;
	}
	if ( (*p) < 63 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st137;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st137;
	} else
		goto st137;
	goto st0;
tr156:
#line 75 "src/uri.rl"
	{ s = p; }
	goto st1;
st1:
	if ( ++p == pe )
		goto _test_eof1;
case 1:
#line 309 "src/uri.c"
	switch( (*p) ) {
		case 37: goto st137;
		case 117: goto st2;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st137;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st137;
	} else
		goto st137;
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
			goto st137;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st137;
	} else
		goto st137;
	goto st0;
tr142:
#line 144 "src/uri.rl"
	{ s = p; }
#line 100 "src/uri.rl"
	{ s = p; }
	goto st6;
st6:
	if ( ++p == pe )
		goto _test_eof6;
case 6:
#line 385 "src/uri.c"
	switch( (*p) ) {
		case 37: goto st135;
		case 117: goto st7;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st135;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st135;
	} else
		goto st135;
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
			goto st135;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st135;
	} else
		goto st135;
	goto st0;
tr151:
#line 101 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 169 "src/uri.rl"
	{ s = p; }
	goto st138;
tr167:
#line 138 "src/uri.rl"
	{ s = p; }
#line 139 "src/uri.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 169 "src/uri.rl"
	{ s = p; }
	goto st138;
tr177:
#line 139 "src/uri.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 169 "src/uri.rl"
	{ s = p; }
	goto st138;
tr192:
#line 108 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;
			   uri->host_hint = 1; }
#line 101 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 169 "src/uri.rl"
	{ s = p; }
	goto st138;
tr201:
#line 169 "src/uri.rl"
	{ s = p; }
	goto st138;
st138:
	if ( ++p == pe )
		goto _test_eof138;
case 138:
#line 488 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st138;
		case 35: goto tr141;
		case 37: goto st11;
		case 61: goto st138;
		case 63: goto tr145;
		case 95: goto st138;
		case 124: goto st138;
		case 126: goto st138;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st138;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st138;
	} else
		goto st138;
	goto st0;
st11:
	if ( ++p == pe )
		goto _test_eof11;
case 11:
	switch( (*p) ) {
		case 37: goto st138;
		case 117: goto st12;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st138;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st138;
	} else
		goto st138;
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
			goto st138;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st138;
	} else
		goto st138;
	goto st0;
tr145:
#line 173 "src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 193 "src/uri.rl"
	{ s = p; }
	goto st139;
tr153:
#line 101 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 169 "src/uri.rl"
	{ s = p; }
#line 173 "src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 193 "src/uri.rl"
	{ s = p; }
	goto st139;
tr169:
#line 138 "src/uri.rl"
	{ s = p; }
#line 139 "src/uri.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 169 "src/uri.rl"
	{ s = p; }
#line 173 "src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 193 "src/uri.rl"
	{ s = p; }
	goto st139;
tr179:
#line 139 "src/uri.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 169 "src/uri.rl"
	{ s = p; }
#line 173 "src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 193 "src/uri.rl"
	{ s = p; }
	goto st139;
tr195:
#line 108 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;
			   uri->host_hint = 1; }
#line 101 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 169 "src/uri.rl"
	{ s = p; }
#line 173 "src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 193 "src/uri.rl"
	{ s = p; }
	goto st139;
tr203:
#line 169 "src/uri.rl"
	{ s = p; }
#line 173 "src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 193 "src/uri.rl"
	{ s = p; }
	goto st139;
tr215:
#line 119 "src/uri.rl"
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
#line 168 "src/uri.rl"
	{ s = p; }
#line 173 "src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 193 "src/uri.rl"
	{ s = p; }
	goto st139;
tr308:
#line 173 "src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 119 "src/uri.rl"
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
#line 193 "src/uri.rl"
	{ s = p; }
	goto st139;
st139:
	if ( ++p == pe )
		goto _test_eof139;
case 139:
#line 686 "src/uri.c"
	switch( (*p) ) {
		case 33: goto tr159;
		case 35: goto tr160;
		case 37: goto tr161;
		case 61: goto tr159;
		case 95: goto tr159;
		case 124: goto tr159;
		case 126: goto tr159;
	}
	if ( (*p) < 63 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto tr159;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr159;
	} else
		goto tr159;
	goto st0;
tr159:
#line 71 "src/uri.rl"
	{ s = p; }
	goto st140;
st140:
	if ( ++p == pe )
		goto _test_eof140;
case 140:
#line 713 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st140;
		case 35: goto tr162;
		case 37: goto st16;
		case 61: goto st140;
		case 95: goto st140;
		case 124: goto st140;
		case 126: goto st140;
	}
	if ( (*p) < 63 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st140;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st140;
	} else
		goto st140;
	goto st0;
tr161:
#line 71 "src/uri.rl"
	{ s = p; }
	goto st16;
st16:
	if ( ++p == pe )
		goto _test_eof16;
case 16:
#line 740 "src/uri.c"
	switch( (*p) ) {
		case 37: goto st140;
		case 117: goto st17;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st140;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st140;
	} else
		goto st140;
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
			goto st140;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st140;
	} else
		goto st140;
	goto st0;
tr152:
#line 145 "src/uri.rl"
	{ login = s; login_len = p - s; }
#line 101 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st141;
tr229:
#line 145 "src/uri.rl"
	{ login = s; login_len = p - s; }
#line 108 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;
			   uri->host_hint = 1; }
#line 101 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st141;
st141:
	if ( ++p == pe )
		goto _test_eof141;
case 141:
#line 825 "src/uri.c"
	switch( (*p) ) {
		case 33: goto tr164;
		case 35: goto tr165;
		case 37: goto tr166;
		case 47: goto tr167;
		case 59: goto tr164;
		case 61: goto tr164;
		case 63: goto tr169;
		case 64: goto tr170;
		case 95: goto tr164;
		case 126: goto tr164;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto tr164;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto tr171;
		} else if ( (*p) >= 65 )
			goto tr171;
	} else
		goto tr168;
	goto st0;
tr164:
#line 148 "src/uri.rl"
	{ s = p; }
	goto st21;
st21:
	if ( ++p == pe )
		goto _test_eof21;
case 21:
#line 858 "src/uri.c"
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
tr166:
#line 148 "src/uri.rl"
	{ s = p; }
	goto st22;
st22:
	if ( ++p == pe )
		goto _test_eof22;
case 22:
#line 888 "src/uri.c"
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
#line 149 "src/uri.rl"
	{ uri->password = s; uri->password_len = p - s; }
#line 153 "src/uri.rl"
	{ uri->login = login; uri->login_len = login_len; }
	goto st27;
tr154:
#line 145 "src/uri.rl"
	{ login = s; login_len = p - s; }
#line 153 "src/uri.rl"
	{ uri->login = login; uri->login_len = login_len; }
	goto st27;
tr170:
#line 148 "src/uri.rl"
	{ s = p; }
#line 149 "src/uri.rl"
	{ uri->password = s; uri->password_len = p - s; }
#line 153 "src/uri.rl"
	{ uri->login = login; uri->login_len = login_len; }
	goto st27;
st27:
	if ( ++p == pe )
		goto _test_eof27;
case 27:
#line 978 "src/uri.c"
	switch( (*p) ) {
		case 33: goto tr28;
		case 37: goto tr29;
		case 47: goto tr30;
		case 59: goto tr28;
		case 61: goto tr28;
		case 91: goto st38;
		case 95: goto tr28;
		case 117: goto tr33;
		case 126: goto tr28;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto tr28;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto tr28;
		} else if ( (*p) >= 65 )
			goto tr28;
	} else
		goto tr31;
	goto st0;
tr28:
#line 100 "src/uri.rl"
	{ s = p; }
	goto st142;
st142:
	if ( ++p == pe )
		goto _test_eof142;
case 142:
#line 1010 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st142;
		case 35: goto tr149;
		case 37: goto st28;
		case 47: goto tr151;
		case 58: goto tr173;
		case 61: goto st142;
		case 63: goto tr153;
		case 95: goto st142;
		case 126: goto st142;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st142;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st142;
	} else
		goto st142;
	goto st0;
tr29:
#line 100 "src/uri.rl"
	{ s = p; }
	goto st28;
st28:
	if ( ++p == pe )
		goto _test_eof28;
case 28:
#line 1039 "src/uri.c"
	switch( (*p) ) {
		case 37: goto st142;
		case 117: goto st29;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st142;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st142;
	} else
		goto st142;
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
			goto st142;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st142;
	} else
		goto st142;
	goto st0;
tr173:
#line 101 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st143;
tr194:
#line 108 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;
			   uri->host_hint = 1; }
#line 101 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st143;
st143:
	if ( ++p == pe )
		goto _test_eof143;
case 143:
#line 1120 "src/uri.c"
	switch( (*p) ) {
		case 35: goto tr165;
		case 47: goto tr167;
		case 63: goto tr169;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr174;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr175;
	} else
		goto tr175;
	goto st0;
tr174:
#line 138 "src/uri.rl"
	{ s = p; }
	goto st144;
st144:
	if ( ++p == pe )
		goto _test_eof144;
case 144:
#line 1143 "src/uri.c"
	switch( (*p) ) {
		case 35: goto tr176;
		case 47: goto tr177;
		case 63: goto tr179;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st144;
	goto st0;
tr175:
#line 138 "src/uri.rl"
	{ s = p; }
	goto st145;
st145:
	if ( ++p == pe )
		goto _test_eof145;
case 145:
#line 1160 "src/uri.c"
	switch( (*p) ) {
		case 35: goto tr176;
		case 47: goto tr177;
		case 63: goto tr179;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st145;
	} else if ( (*p) >= 65 )
		goto st145;
	goto st0;
tr30:
#line 190 "src/uri.rl"
	{ s = p; }
	goto st146;
st146:
	if ( ++p == pe )
		goto _test_eof146;
case 146:
#line 1180 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st147;
		case 37: goto st33;
		case 61: goto st147;
		case 95: goto st147;
		case 124: goto st147;
		case 126: goto st147;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st147;
	} else if ( (*p) > 59 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st147;
		} else if ( (*p) >= 64 )
			goto st147;
	} else
		goto st147;
	goto st0;
st147:
	if ( ++p == pe )
		goto _test_eof147;
case 147:
	switch( (*p) ) {
		case 33: goto st147;
		case 37: goto st33;
		case 61: goto st147;
		case 95: goto st147;
		case 124: goto st147;
		case 126: goto st147;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st147;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st147;
	} else
		goto st147;
	goto st0;
st33:
	if ( ++p == pe )
		goto _test_eof33;
case 33:
	switch( (*p) ) {
		case 37: goto st147;
		case 117: goto st34;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st147;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st147;
	} else
		goto st147;
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
			goto st147;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st147;
	} else
		goto st147;
	goto st0;
tr31:
#line 107 "src/uri.rl"
	{ s = p; }
#line 100 "src/uri.rl"
	{ s = p; }
	goto st148;
st148:
	if ( ++p == pe )
		goto _test_eof148;
case 148:
#line 1301 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st142;
		case 35: goto tr149;
		case 37: goto st28;
		case 46: goto st149;
		case 47: goto tr151;
		case 58: goto tr173;
		case 59: goto st142;
		case 61: goto st142;
		case 63: goto tr153;
		case 95: goto st142;
		case 126: goto st142;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st142;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st142;
		} else if ( (*p) >= 65 )
			goto st142;
	} else
		goto st161;
	goto st0;
st149:
	if ( ++p == pe )
		goto _test_eof149;
case 149:
	switch( (*p) ) {
		case 33: goto st142;
		case 35: goto tr149;
		case 37: goto st28;
		case 47: goto tr151;
		case 58: goto tr173;
		case 59: goto st142;
		case 61: goto st142;
		case 63: goto tr153;
		case 95: goto st142;
		case 126: goto st142;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st142;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st142;
		} else if ( (*p) >= 65 )
			goto st142;
	} else
		goto st150;
	goto st0;
st150:
	if ( ++p == pe )
		goto _test_eof150;
case 150:
	switch( (*p) ) {
		case 33: goto st142;
		case 35: goto tr149;
		case 37: goto st28;
		case 46: goto st151;
		case 47: goto tr151;
		case 58: goto tr173;
		case 59: goto st142;
		case 61: goto st142;
		case 63: goto tr153;
		case 95: goto st142;
		case 126: goto st142;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st142;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st142;
		} else if ( (*p) >= 65 )
			goto st142;
	} else
		goto st159;
	goto st0;
st151:
	if ( ++p == pe )
		goto _test_eof151;
case 151:
	switch( (*p) ) {
		case 33: goto st142;
		case 35: goto tr149;
		case 37: goto st28;
		case 47: goto tr151;
		case 58: goto tr173;
		case 59: goto st142;
		case 61: goto st142;
		case 63: goto tr153;
		case 95: goto st142;
		case 126: goto st142;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st142;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st142;
		} else if ( (*p) >= 65 )
			goto st142;
	} else
		goto st152;
	goto st0;
st152:
	if ( ++p == pe )
		goto _test_eof152;
case 152:
	switch( (*p) ) {
		case 33: goto st142;
		case 35: goto tr149;
		case 37: goto st28;
		case 46: goto st153;
		case 47: goto tr151;
		case 58: goto tr173;
		case 59: goto st142;
		case 61: goto st142;
		case 63: goto tr153;
		case 95: goto st142;
		case 126: goto st142;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st142;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st142;
		} else if ( (*p) >= 65 )
			goto st142;
	} else
		goto st157;
	goto st0;
st153:
	if ( ++p == pe )
		goto _test_eof153;
case 153:
	switch( (*p) ) {
		case 33: goto st142;
		case 35: goto tr149;
		case 37: goto st28;
		case 47: goto tr151;
		case 58: goto tr173;
		case 59: goto st142;
		case 61: goto st142;
		case 63: goto tr153;
		case 95: goto st142;
		case 126: goto st142;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st142;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st142;
		} else if ( (*p) >= 65 )
			goto st142;
	} else
		goto st154;
	goto st0;
st154:
	if ( ++p == pe )
		goto _test_eof154;
case 154:
	switch( (*p) ) {
		case 33: goto st142;
		case 35: goto tr191;
		case 37: goto st28;
		case 47: goto tr192;
		case 58: goto tr194;
		case 59: goto st142;
		case 61: goto st142;
		case 63: goto tr195;
		case 95: goto st142;
		case 126: goto st142;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st142;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st142;
		} else if ( (*p) >= 65 )
			goto st142;
	} else
		goto st155;
	goto st0;
st155:
	if ( ++p == pe )
		goto _test_eof155;
case 155:
	switch( (*p) ) {
		case 33: goto st142;
		case 35: goto tr191;
		case 37: goto st28;
		case 47: goto tr192;
		case 58: goto tr194;
		case 59: goto st142;
		case 61: goto st142;
		case 63: goto tr195;
		case 95: goto st142;
		case 126: goto st142;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st142;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st142;
		} else if ( (*p) >= 65 )
			goto st142;
	} else
		goto st156;
	goto st0;
st156:
	if ( ++p == pe )
		goto _test_eof156;
case 156:
	switch( (*p) ) {
		case 33: goto st142;
		case 35: goto tr191;
		case 37: goto st28;
		case 47: goto tr192;
		case 58: goto tr194;
		case 61: goto st142;
		case 63: goto tr195;
		case 95: goto st142;
		case 126: goto st142;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st142;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st142;
	} else
		goto st142;
	goto st0;
st157:
	if ( ++p == pe )
		goto _test_eof157;
case 157:
	switch( (*p) ) {
		case 33: goto st142;
		case 35: goto tr149;
		case 37: goto st28;
		case 46: goto st153;
		case 47: goto tr151;
		case 58: goto tr173;
		case 59: goto st142;
		case 61: goto st142;
		case 63: goto tr153;
		case 95: goto st142;
		case 126: goto st142;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st142;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st142;
		} else if ( (*p) >= 65 )
			goto st142;
	} else
		goto st158;
	goto st0;
st158:
	if ( ++p == pe )
		goto _test_eof158;
case 158:
	switch( (*p) ) {
		case 33: goto st142;
		case 35: goto tr149;
		case 37: goto st28;
		case 46: goto st153;
		case 47: goto tr151;
		case 58: goto tr173;
		case 61: goto st142;
		case 63: goto tr153;
		case 95: goto st142;
		case 126: goto st142;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st142;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st142;
	} else
		goto st142;
	goto st0;
st159:
	if ( ++p == pe )
		goto _test_eof159;
case 159:
	switch( (*p) ) {
		case 33: goto st142;
		case 35: goto tr149;
		case 37: goto st28;
		case 46: goto st151;
		case 47: goto tr151;
		case 58: goto tr173;
		case 59: goto st142;
		case 61: goto st142;
		case 63: goto tr153;
		case 95: goto st142;
		case 126: goto st142;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st142;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st142;
		} else if ( (*p) >= 65 )
			goto st142;
	} else
		goto st160;
	goto st0;
st160:
	if ( ++p == pe )
		goto _test_eof160;
case 160:
	switch( (*p) ) {
		case 33: goto st142;
		case 35: goto tr149;
		case 37: goto st28;
		case 46: goto st151;
		case 47: goto tr151;
		case 58: goto tr173;
		case 61: goto st142;
		case 63: goto tr153;
		case 95: goto st142;
		case 126: goto st142;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st142;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st142;
	} else
		goto st142;
	goto st0;
st161:
	if ( ++p == pe )
		goto _test_eof161;
case 161:
	switch( (*p) ) {
		case 33: goto st142;
		case 35: goto tr149;
		case 37: goto st28;
		case 46: goto st149;
		case 47: goto tr151;
		case 58: goto tr173;
		case 59: goto st142;
		case 61: goto st142;
		case 63: goto tr153;
		case 95: goto st142;
		case 126: goto st142;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st142;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st142;
		} else if ( (*p) >= 65 )
			goto st142;
	} else
		goto st162;
	goto st0;
st162:
	if ( ++p == pe )
		goto _test_eof162;
case 162:
	switch( (*p) ) {
		case 33: goto st142;
		case 35: goto tr149;
		case 37: goto st28;
		case 46: goto st149;
		case 47: goto tr151;
		case 58: goto tr173;
		case 61: goto st142;
		case 63: goto tr153;
		case 95: goto st142;
		case 126: goto st142;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st142;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st142;
	} else
		goto st142;
	goto st0;
st38:
	if ( ++p == pe )
		goto _test_eof38;
case 38:
	if ( (*p) == 58 )
		goto tr45;
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto tr44;
	} else if ( (*p) >= 48 )
		goto tr44;
	goto st0;
tr44:
#line 114 "src/uri.rl"
	{ s = p; }
	goto st39;
st39:
	if ( ++p == pe )
		goto _test_eof39;
case 39:
#line 1731 "src/uri.c"
	if ( (*p) == 58 )
		goto st43;
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st40;
	} else if ( (*p) >= 48 )
		goto st40;
	goto st0;
st40:
	if ( ++p == pe )
		goto _test_eof40;
case 40:
	if ( (*p) == 58 )
		goto st43;
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st41;
	} else if ( (*p) >= 48 )
		goto st41;
	goto st0;
st41:
	if ( ++p == pe )
		goto _test_eof41;
case 41:
	if ( (*p) == 58 )
		goto st43;
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st42;
	} else if ( (*p) >= 48 )
		goto st42;
	goto st0;
st42:
	if ( ++p == pe )
		goto _test_eof42;
case 42:
	if ( (*p) == 58 )
		goto st43;
	goto st0;
st43:
	if ( ++p == pe )
		goto _test_eof43;
case 43:
	switch( (*p) ) {
		case 58: goto st48;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st44;
	} else if ( (*p) >= 48 )
		goto st44;
	goto st0;
st44:
	if ( ++p == pe )
		goto _test_eof44;
case 44:
	switch( (*p) ) {
		case 58: goto st48;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st45;
	} else if ( (*p) >= 48 )
		goto st45;
	goto st0;
st45:
	if ( ++p == pe )
		goto _test_eof45;
case 45:
	switch( (*p) ) {
		case 58: goto st48;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st46;
	} else if ( (*p) >= 48 )
		goto st46;
	goto st0;
st46:
	if ( ++p == pe )
		goto _test_eof46;
case 46:
	switch( (*p) ) {
		case 58: goto st48;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st47;
	} else if ( (*p) >= 48 )
		goto st47;
	goto st0;
st47:
	if ( ++p == pe )
		goto _test_eof47;
case 47:
	switch( (*p) ) {
		case 58: goto st48;
		case 93: goto tr52;
	}
	goto st0;
st48:
	if ( ++p == pe )
		goto _test_eof48;
case 48:
	switch( (*p) ) {
		case 58: goto st53;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st49;
	} else if ( (*p) >= 48 )
		goto st49;
	goto st0;
st49:
	if ( ++p == pe )
		goto _test_eof49;
case 49:
	switch( (*p) ) {
		case 58: goto st53;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st50;
	} else if ( (*p) >= 48 )
		goto st50;
	goto st0;
st50:
	if ( ++p == pe )
		goto _test_eof50;
case 50:
	switch( (*p) ) {
		case 58: goto st53;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st51;
	} else if ( (*p) >= 48 )
		goto st51;
	goto st0;
st51:
	if ( ++p == pe )
		goto _test_eof51;
case 51:
	switch( (*p) ) {
		case 58: goto st53;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st52;
	} else if ( (*p) >= 48 )
		goto st52;
	goto st0;
st52:
	if ( ++p == pe )
		goto _test_eof52;
case 52:
	switch( (*p) ) {
		case 58: goto st53;
		case 93: goto tr52;
	}
	goto st0;
st53:
	if ( ++p == pe )
		goto _test_eof53;
case 53:
	switch( (*p) ) {
		case 58: goto st58;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st54;
	} else if ( (*p) >= 48 )
		goto st54;
	goto st0;
st54:
	if ( ++p == pe )
		goto _test_eof54;
case 54:
	switch( (*p) ) {
		case 58: goto st58;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st55;
	} else if ( (*p) >= 48 )
		goto st55;
	goto st0;
st55:
	if ( ++p == pe )
		goto _test_eof55;
case 55:
	switch( (*p) ) {
		case 58: goto st58;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st56;
	} else if ( (*p) >= 48 )
		goto st56;
	goto st0;
st56:
	if ( ++p == pe )
		goto _test_eof56;
case 56:
	switch( (*p) ) {
		case 58: goto st58;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st57;
	} else if ( (*p) >= 48 )
		goto st57;
	goto st0;
st57:
	if ( ++p == pe )
		goto _test_eof57;
case 57:
	switch( (*p) ) {
		case 58: goto st58;
		case 93: goto tr52;
	}
	goto st0;
st58:
	if ( ++p == pe )
		goto _test_eof58;
case 58:
	switch( (*p) ) {
		case 58: goto st63;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st59;
	} else if ( (*p) >= 48 )
		goto st59;
	goto st0;
st59:
	if ( ++p == pe )
		goto _test_eof59;
case 59:
	switch( (*p) ) {
		case 58: goto st63;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st60;
	} else if ( (*p) >= 48 )
		goto st60;
	goto st0;
st60:
	if ( ++p == pe )
		goto _test_eof60;
case 60:
	switch( (*p) ) {
		case 58: goto st63;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st61;
	} else if ( (*p) >= 48 )
		goto st61;
	goto st0;
st61:
	if ( ++p == pe )
		goto _test_eof61;
case 61:
	switch( (*p) ) {
		case 58: goto st63;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st62;
	} else if ( (*p) >= 48 )
		goto st62;
	goto st0;
st62:
	if ( ++p == pe )
		goto _test_eof62;
case 62:
	switch( (*p) ) {
		case 58: goto st63;
		case 93: goto tr52;
	}
	goto st0;
st63:
	if ( ++p == pe )
		goto _test_eof63;
case 63:
	switch( (*p) ) {
		case 58: goto st68;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st64;
	} else if ( (*p) >= 48 )
		goto st64;
	goto st0;
st64:
	if ( ++p == pe )
		goto _test_eof64;
case 64:
	switch( (*p) ) {
		case 58: goto st68;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st65;
	} else if ( (*p) >= 48 )
		goto st65;
	goto st0;
st65:
	if ( ++p == pe )
		goto _test_eof65;
case 65:
	switch( (*p) ) {
		case 58: goto st68;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st66;
	} else if ( (*p) >= 48 )
		goto st66;
	goto st0;
st66:
	if ( ++p == pe )
		goto _test_eof66;
case 66:
	switch( (*p) ) {
		case 58: goto st68;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st67;
	} else if ( (*p) >= 48 )
		goto st67;
	goto st0;
st67:
	if ( ++p == pe )
		goto _test_eof67;
case 67:
	switch( (*p) ) {
		case 58: goto st68;
		case 93: goto tr52;
	}
	goto st0;
st68:
	if ( ++p == pe )
		goto _test_eof68;
case 68:
	switch( (*p) ) {
		case 58: goto st73;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st69;
	} else if ( (*p) >= 48 )
		goto st69;
	goto st0;
st69:
	if ( ++p == pe )
		goto _test_eof69;
case 69:
	switch( (*p) ) {
		case 58: goto st73;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st70;
	} else if ( (*p) >= 48 )
		goto st70;
	goto st0;
st70:
	if ( ++p == pe )
		goto _test_eof70;
case 70:
	switch( (*p) ) {
		case 58: goto st73;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st71;
	} else if ( (*p) >= 48 )
		goto st71;
	goto st0;
st71:
	if ( ++p == pe )
		goto _test_eof71;
case 71:
	switch( (*p) ) {
		case 58: goto st73;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st72;
	} else if ( (*p) >= 48 )
		goto st72;
	goto st0;
st72:
	if ( ++p == pe )
		goto _test_eof72;
case 72:
	switch( (*p) ) {
		case 58: goto st73;
		case 93: goto tr52;
	}
	goto st0;
st73:
	if ( ++p == pe )
		goto _test_eof73;
case 73:
	switch( (*p) ) {
		case 58: goto st78;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st74;
	} else if ( (*p) >= 48 )
		goto st74;
	goto st0;
st74:
	if ( ++p == pe )
		goto _test_eof74;
case 74:
	switch( (*p) ) {
		case 58: goto st78;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st75;
	} else if ( (*p) >= 48 )
		goto st75;
	goto st0;
st75:
	if ( ++p == pe )
		goto _test_eof75;
case 75:
	switch( (*p) ) {
		case 58: goto st78;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st76;
	} else if ( (*p) >= 48 )
		goto st76;
	goto st0;
st76:
	if ( ++p == pe )
		goto _test_eof76;
case 76:
	switch( (*p) ) {
		case 58: goto st78;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st77;
	} else if ( (*p) >= 48 )
		goto st77;
	goto st0;
st77:
	if ( ++p == pe )
		goto _test_eof77;
case 77:
	switch( (*p) ) {
		case 58: goto st78;
		case 93: goto tr52;
	}
	goto st0;
st78:
	if ( ++p == pe )
		goto _test_eof78;
case 78:
	if ( (*p) == 93 )
		goto tr52;
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st79;
	} else if ( (*p) >= 48 )
		goto st79;
	goto st0;
st79:
	if ( ++p == pe )
		goto _test_eof79;
case 79:
	if ( (*p) == 93 )
		goto tr52;
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st80;
	} else if ( (*p) >= 48 )
		goto st80;
	goto st0;
st80:
	if ( ++p == pe )
		goto _test_eof80;
case 80:
	if ( (*p) == 93 )
		goto tr52;
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st81;
	} else if ( (*p) >= 48 )
		goto st81;
	goto st0;
st81:
	if ( ++p == pe )
		goto _test_eof81;
case 81:
	if ( (*p) == 93 )
		goto tr52;
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st82;
	} else if ( (*p) >= 48 )
		goto st82;
	goto st0;
st82:
	if ( ++p == pe )
		goto _test_eof82;
case 82:
	if ( (*p) == 93 )
		goto tr52;
	goto st0;
tr52:
#line 115 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;
				   uri->host_hint = 2; }
	goto st163;
st163:
	if ( ++p == pe )
		goto _test_eof163;
case 163:
#line 2290 "src/uri.c"
	switch( (*p) ) {
		case 35: goto tr200;
		case 47: goto tr201;
		case 58: goto st143;
		case 63: goto tr203;
	}
	goto st0;
tr45:
#line 114 "src/uri.rl"
	{ s = p; }
	goto st83;
st83:
	if ( ++p == pe )
		goto _test_eof83;
case 83:
#line 2306 "src/uri.c"
	switch( (*p) ) {
		case 58: goto st84;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st44;
	} else if ( (*p) >= 48 )
		goto st44;
	goto st0;
st84:
	if ( ++p == pe )
		goto _test_eof84;
case 84:
	switch( (*p) ) {
		case 58: goto st53;
		case 93: goto tr52;
		case 102: goto st85;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 101 )
			goto st49;
	} else if ( (*p) >= 48 )
		goto st49;
	goto st0;
st85:
	if ( ++p == pe )
		goto _test_eof85;
case 85:
	switch( (*p) ) {
		case 58: goto st53;
		case 93: goto tr52;
		case 102: goto st86;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 101 )
			goto st50;
	} else if ( (*p) >= 48 )
		goto st50;
	goto st0;
st86:
	if ( ++p == pe )
		goto _test_eof86;
case 86:
	switch( (*p) ) {
		case 58: goto st53;
		case 93: goto tr52;
		case 102: goto st87;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 101 )
			goto st51;
	} else if ( (*p) >= 48 )
		goto st51;
	goto st0;
st87:
	if ( ++p == pe )
		goto _test_eof87;
case 87:
	switch( (*p) ) {
		case 58: goto st53;
		case 93: goto tr52;
		case 102: goto st88;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 101 )
			goto st52;
	} else if ( (*p) >= 48 )
		goto st52;
	goto st0;
st88:
	if ( ++p == pe )
		goto _test_eof88;
case 88:
	switch( (*p) ) {
		case 58: goto st89;
		case 93: goto tr52;
	}
	goto st0;
st89:
	if ( ++p == pe )
		goto _test_eof89;
case 89:
	switch( (*p) ) {
		case 58: goto st58;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st54;
	} else if ( (*p) >= 48 )
		goto st90;
	goto st0;
st90:
	if ( ++p == pe )
		goto _test_eof90;
case 90:
	switch( (*p) ) {
		case 46: goto st91;
		case 58: goto st58;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st55;
	} else if ( (*p) >= 48 )
		goto st102;
	goto st0;
st91:
	if ( ++p == pe )
		goto _test_eof91;
case 91:
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st92;
	goto st0;
st92:
	if ( ++p == pe )
		goto _test_eof92;
case 92:
	if ( (*p) == 46 )
		goto st93;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st100;
	goto st0;
st93:
	if ( ++p == pe )
		goto _test_eof93;
case 93:
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st94;
	goto st0;
st94:
	if ( ++p == pe )
		goto _test_eof94;
case 94:
	if ( (*p) == 46 )
		goto st95;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st98;
	goto st0;
st95:
	if ( ++p == pe )
		goto _test_eof95;
case 95:
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st96;
	goto st0;
st96:
	if ( ++p == pe )
		goto _test_eof96;
case 96:
	if ( (*p) == 93 )
		goto tr52;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st97;
	goto st0;
st97:
	if ( ++p == pe )
		goto _test_eof97;
case 97:
	if ( (*p) == 93 )
		goto tr52;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st82;
	goto st0;
st98:
	if ( ++p == pe )
		goto _test_eof98;
case 98:
	if ( (*p) == 46 )
		goto st95;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st99;
	goto st0;
st99:
	if ( ++p == pe )
		goto _test_eof99;
case 99:
	if ( (*p) == 46 )
		goto st95;
	goto st0;
st100:
	if ( ++p == pe )
		goto _test_eof100;
case 100:
	if ( (*p) == 46 )
		goto st93;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st101;
	goto st0;
st101:
	if ( ++p == pe )
		goto _test_eof101;
case 101:
	if ( (*p) == 46 )
		goto st93;
	goto st0;
st102:
	if ( ++p == pe )
		goto _test_eof102;
case 102:
	switch( (*p) ) {
		case 46: goto st91;
		case 58: goto st58;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st56;
	} else if ( (*p) >= 48 )
		goto st103;
	goto st0;
st103:
	if ( ++p == pe )
		goto _test_eof103;
case 103:
	switch( (*p) ) {
		case 46: goto st91;
		case 58: goto st58;
		case 93: goto tr52;
	}
	if ( (*p) > 57 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st57;
	} else if ( (*p) >= 48 )
		goto st57;
	goto st0;
tr33:
#line 100 "src/uri.rl"
	{ s = p; }
	goto st164;
st164:
	if ( ++p == pe )
		goto _test_eof164;
case 164:
#line 2542 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st142;
		case 35: goto tr149;
		case 37: goto st28;
		case 47: goto tr151;
		case 58: goto tr173;
		case 61: goto st142;
		case 63: goto tr153;
		case 95: goto st142;
		case 110: goto st165;
		case 126: goto st142;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st142;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st142;
	} else
		goto st142;
	goto st0;
st165:
	if ( ++p == pe )
		goto _test_eof165;
case 165:
	switch( (*p) ) {
		case 33: goto st142;
		case 35: goto tr149;
		case 37: goto st28;
		case 47: goto tr151;
		case 58: goto tr173;
		case 61: goto st142;
		case 63: goto tr153;
		case 95: goto st142;
		case 105: goto st166;
		case 126: goto st142;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st142;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st142;
	} else
		goto st142;
	goto st0;
st166:
	if ( ++p == pe )
		goto _test_eof166;
case 166:
	switch( (*p) ) {
		case 33: goto st142;
		case 35: goto tr149;
		case 37: goto st28;
		case 47: goto tr151;
		case 58: goto tr173;
		case 61: goto st142;
		case 63: goto tr153;
		case 95: goto st142;
		case 120: goto st167;
		case 126: goto st142;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st142;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st142;
	} else
		goto st142;
	goto st0;
st167:
	if ( ++p == pe )
		goto _test_eof167;
case 167:
	switch( (*p) ) {
		case 33: goto st142;
		case 35: goto tr149;
		case 37: goto st28;
		case 47: goto tr207;
		case 58: goto tr173;
		case 61: goto st142;
		case 63: goto tr153;
		case 95: goto st142;
		case 126: goto st142;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st142;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st142;
	} else
		goto st142;
	goto st0;
tr207:
#line 101 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 169 "src/uri.rl"
	{ s = p; }
	goto st168;
st168:
	if ( ++p == pe )
		goto _test_eof168;
case 168:
#line 2648 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st138;
		case 35: goto tr141;
		case 37: goto st11;
		case 58: goto st169;
		case 61: goto st138;
		case 63: goto tr145;
		case 95: goto st138;
		case 124: goto st138;
		case 126: goto st138;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st138;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st138;
	} else
		goto st138;
	goto st0;
st169:
	if ( ++p == pe )
		goto _test_eof169;
case 169:
	switch( (*p) ) {
		case 33: goto st138;
		case 35: goto tr141;
		case 37: goto st11;
		case 46: goto tr209;
		case 47: goto tr210;
		case 61: goto st138;
		case 63: goto tr145;
		case 95: goto st138;
		case 124: goto st138;
		case 126: goto st138;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st138;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st138;
	} else
		goto st138;
	goto st0;
tr209:
#line 135 "src/uri.rl"
	{ s = p;}
	goto st170;
st170:
	if ( ++p == pe )
		goto _test_eof170;
case 170:
#line 2702 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st138;
		case 35: goto tr141;
		case 37: goto st11;
		case 47: goto st171;
		case 61: goto st138;
		case 63: goto tr145;
		case 95: goto st138;
		case 124: goto st138;
		case 126: goto st138;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st138;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st138;
	} else
		goto st138;
	goto st0;
tr210:
#line 135 "src/uri.rl"
	{ s = p;}
	goto st171;
st171:
	if ( ++p == pe )
		goto _test_eof171;
case 171:
#line 2731 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st172;
		case 35: goto tr141;
		case 37: goto st104;
		case 47: goto st138;
		case 58: goto st138;
		case 61: goto st172;
		case 63: goto tr145;
		case 95: goto st172;
		case 124: goto st138;
		case 126: goto st172;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st172;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st172;
	} else
		goto st172;
	goto st0;
st172:
	if ( ++p == pe )
		goto _test_eof172;
case 172:
	switch( (*p) ) {
		case 33: goto st172;
		case 35: goto tr213;
		case 37: goto st104;
		case 47: goto st171;
		case 58: goto tr214;
		case 61: goto st172;
		case 63: goto tr215;
		case 95: goto st172;
		case 124: goto st138;
		case 126: goto st172;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st172;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st172;
	} else
		goto st172;
	goto st0;
st104:
	if ( ++p == pe )
		goto _test_eof104;
case 104:
	switch( (*p) ) {
		case 37: goto st172;
		case 117: goto st105;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st172;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st172;
	} else
		goto st172;
	goto st0;
st105:
	if ( ++p == pe )
		goto _test_eof105;
case 105:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st106;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st106;
	} else
		goto st106;
	goto st0;
st106:
	if ( ++p == pe )
		goto _test_eof106;
case 106:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st107;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st107;
	} else
		goto st107;
	goto st0;
st107:
	if ( ++p == pe )
		goto _test_eof107;
case 107:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st108;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st108;
	} else
		goto st108;
	goto st0;
st108:
	if ( ++p == pe )
		goto _test_eof108;
case 108:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st172;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st172;
	} else
		goto st172;
	goto st0;
tr214:
#line 119 "src/uri.rl"
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
	goto st173;
st173:
	if ( ++p == pe )
		goto _test_eof173;
case 173:
#line 2869 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st138;
		case 35: goto tr200;
		case 37: goto st11;
		case 47: goto tr201;
		case 61: goto st138;
		case 63: goto tr203;
		case 95: goto st138;
		case 124: goto st138;
		case 126: goto st138;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st138;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st138;
	} else
		goto st138;
	goto st0;
tr168:
#line 148 "src/uri.rl"
	{ s = p; }
#line 138 "src/uri.rl"
	{ s = p; }
	goto st174;
st174:
	if ( ++p == pe )
		goto _test_eof174;
case 174:
#line 2900 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st21;
		case 35: goto tr176;
		case 37: goto st22;
		case 47: goto tr177;
		case 59: goto st21;
		case 61: goto st21;
		case 63: goto tr179;
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
		goto st174;
	goto st0;
tr171:
#line 148 "src/uri.rl"
	{ s = p; }
#line 138 "src/uri.rl"
	{ s = p; }
	goto st175;
st175:
	if ( ++p == pe )
		goto _test_eof175;
case 175:
#line 2935 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st21;
		case 35: goto tr176;
		case 37: goto st22;
		case 47: goto tr177;
		case 59: goto st21;
		case 61: goto st21;
		case 63: goto tr179;
		case 64: goto tr23;
		case 95: goto st21;
		case 126: goto st21;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 57 )
			goto st21;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st175;
	} else
		goto st175;
	goto st0;
tr143:
#line 190 "src/uri.rl"
	{ s = p; }
	goto st176;
st176:
	if ( ++p == pe )
		goto _test_eof176;
case 176:
#line 2965 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st177;
		case 35: goto tr141;
		case 37: goto st109;
		case 61: goto st177;
		case 63: goto tr145;
		case 95: goto st177;
		case 124: goto st177;
		case 126: goto st177;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st177;
	} else if ( (*p) > 59 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st177;
		} else if ( (*p) >= 64 )
			goto st177;
	} else
		goto st177;
	goto st0;
st177:
	if ( ++p == pe )
		goto _test_eof177;
case 177:
	switch( (*p) ) {
		case 33: goto st177;
		case 35: goto tr141;
		case 37: goto st109;
		case 61: goto st177;
		case 63: goto tr145;
		case 95: goto st177;
		case 124: goto st177;
		case 126: goto st177;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st177;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st177;
	} else
		goto st177;
	goto st0;
st109:
	if ( ++p == pe )
		goto _test_eof109;
case 109:
	switch( (*p) ) {
		case 37: goto st177;
		case 117: goto st110;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st177;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st177;
	} else
		goto st177;
	goto st0;
st110:
	if ( ++p == pe )
		goto _test_eof110;
case 110:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st111;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st111;
	} else
		goto st111;
	goto st0;
st111:
	if ( ++p == pe )
		goto _test_eof111;
case 111:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st112;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st112;
	} else
		goto st112;
	goto st0;
st112:
	if ( ++p == pe )
		goto _test_eof112;
case 112:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st113;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st113;
	} else
		goto st113;
	goto st0;
st113:
	if ( ++p == pe )
		goto _test_eof113;
case 113:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st177;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st177;
	} else
		goto st177;
	goto st0;
tr144:
#line 144 "src/uri.rl"
	{ s = p; }
#line 107 "src/uri.rl"
	{ s = p; }
#line 100 "src/uri.rl"
	{ s = p; }
#line 186 "src/uri.rl"
	{ uri->service = p; }
	goto st178;
st178:
	if ( ++p == pe )
		goto _test_eof178;
case 178:
#line 3094 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st135;
		case 35: goto tr149;
		case 37: goto st6;
		case 46: goto st179;
		case 47: goto tr151;
		case 58: goto tr152;
		case 59: goto st135;
		case 61: goto st135;
		case 63: goto tr153;
		case 64: goto tr154;
		case 95: goto st135;
		case 126: goto st135;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st135;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st135;
		} else if ( (*p) >= 65 )
			goto st135;
	} else
		goto st191;
	goto st0;
st179:
	if ( ++p == pe )
		goto _test_eof179;
case 179:
	switch( (*p) ) {
		case 33: goto st135;
		case 35: goto tr149;
		case 37: goto st6;
		case 47: goto tr151;
		case 58: goto tr152;
		case 59: goto st135;
		case 61: goto st135;
		case 63: goto tr153;
		case 64: goto tr154;
		case 95: goto st135;
		case 126: goto st135;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st135;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st135;
		} else if ( (*p) >= 65 )
			goto st135;
	} else
		goto st180;
	goto st0;
st180:
	if ( ++p == pe )
		goto _test_eof180;
case 180:
	switch( (*p) ) {
		case 33: goto st135;
		case 35: goto tr149;
		case 37: goto st6;
		case 46: goto st181;
		case 47: goto tr151;
		case 58: goto tr152;
		case 59: goto st135;
		case 61: goto st135;
		case 63: goto tr153;
		case 64: goto tr154;
		case 95: goto st135;
		case 126: goto st135;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st135;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st135;
		} else if ( (*p) >= 65 )
			goto st135;
	} else
		goto st189;
	goto st0;
st181:
	if ( ++p == pe )
		goto _test_eof181;
case 181:
	switch( (*p) ) {
		case 33: goto st135;
		case 35: goto tr149;
		case 37: goto st6;
		case 47: goto tr151;
		case 58: goto tr152;
		case 59: goto st135;
		case 61: goto st135;
		case 63: goto tr153;
		case 64: goto tr154;
		case 95: goto st135;
		case 126: goto st135;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st135;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st135;
		} else if ( (*p) >= 65 )
			goto st135;
	} else
		goto st182;
	goto st0;
st182:
	if ( ++p == pe )
		goto _test_eof182;
case 182:
	switch( (*p) ) {
		case 33: goto st135;
		case 35: goto tr149;
		case 37: goto st6;
		case 46: goto st183;
		case 47: goto tr151;
		case 58: goto tr152;
		case 59: goto st135;
		case 61: goto st135;
		case 63: goto tr153;
		case 64: goto tr154;
		case 95: goto st135;
		case 126: goto st135;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st135;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st135;
		} else if ( (*p) >= 65 )
			goto st135;
	} else
		goto st187;
	goto st0;
st183:
	if ( ++p == pe )
		goto _test_eof183;
case 183:
	switch( (*p) ) {
		case 33: goto st135;
		case 35: goto tr149;
		case 37: goto st6;
		case 47: goto tr151;
		case 58: goto tr152;
		case 59: goto st135;
		case 61: goto st135;
		case 63: goto tr153;
		case 64: goto tr154;
		case 95: goto st135;
		case 126: goto st135;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st135;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st135;
		} else if ( (*p) >= 65 )
			goto st135;
	} else
		goto st184;
	goto st0;
st184:
	if ( ++p == pe )
		goto _test_eof184;
case 184:
	switch( (*p) ) {
		case 33: goto st135;
		case 35: goto tr191;
		case 37: goto st6;
		case 47: goto tr192;
		case 58: goto tr229;
		case 59: goto st135;
		case 61: goto st135;
		case 63: goto tr195;
		case 64: goto tr154;
		case 95: goto st135;
		case 126: goto st135;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st135;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st135;
		} else if ( (*p) >= 65 )
			goto st135;
	} else
		goto st185;
	goto st0;
st185:
	if ( ++p == pe )
		goto _test_eof185;
case 185:
	switch( (*p) ) {
		case 33: goto st135;
		case 35: goto tr191;
		case 37: goto st6;
		case 47: goto tr192;
		case 58: goto tr229;
		case 59: goto st135;
		case 61: goto st135;
		case 63: goto tr195;
		case 64: goto tr154;
		case 95: goto st135;
		case 126: goto st135;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st135;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st135;
		} else if ( (*p) >= 65 )
			goto st135;
	} else
		goto st186;
	goto st0;
st186:
	if ( ++p == pe )
		goto _test_eof186;
case 186:
	switch( (*p) ) {
		case 33: goto st135;
		case 35: goto tr191;
		case 37: goto st6;
		case 47: goto tr192;
		case 58: goto tr229;
		case 61: goto st135;
		case 63: goto tr195;
		case 64: goto tr154;
		case 95: goto st135;
		case 126: goto st135;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st135;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st135;
	} else
		goto st135;
	goto st0;
st187:
	if ( ++p == pe )
		goto _test_eof187;
case 187:
	switch( (*p) ) {
		case 33: goto st135;
		case 35: goto tr149;
		case 37: goto st6;
		case 46: goto st183;
		case 47: goto tr151;
		case 58: goto tr152;
		case 59: goto st135;
		case 61: goto st135;
		case 63: goto tr153;
		case 64: goto tr154;
		case 95: goto st135;
		case 126: goto st135;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st135;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st135;
		} else if ( (*p) >= 65 )
			goto st135;
	} else
		goto st188;
	goto st0;
st188:
	if ( ++p == pe )
		goto _test_eof188;
case 188:
	switch( (*p) ) {
		case 33: goto st135;
		case 35: goto tr149;
		case 37: goto st6;
		case 46: goto st183;
		case 47: goto tr151;
		case 58: goto tr152;
		case 61: goto st135;
		case 63: goto tr153;
		case 64: goto tr154;
		case 95: goto st135;
		case 126: goto st135;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st135;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st135;
	} else
		goto st135;
	goto st0;
st189:
	if ( ++p == pe )
		goto _test_eof189;
case 189:
	switch( (*p) ) {
		case 33: goto st135;
		case 35: goto tr149;
		case 37: goto st6;
		case 46: goto st181;
		case 47: goto tr151;
		case 58: goto tr152;
		case 59: goto st135;
		case 61: goto st135;
		case 63: goto tr153;
		case 64: goto tr154;
		case 95: goto st135;
		case 126: goto st135;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st135;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st135;
		} else if ( (*p) >= 65 )
			goto st135;
	} else
		goto st190;
	goto st0;
st190:
	if ( ++p == pe )
		goto _test_eof190;
case 190:
	switch( (*p) ) {
		case 33: goto st135;
		case 35: goto tr149;
		case 37: goto st6;
		case 46: goto st181;
		case 47: goto tr151;
		case 58: goto tr152;
		case 61: goto st135;
		case 63: goto tr153;
		case 64: goto tr154;
		case 95: goto st135;
		case 126: goto st135;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st135;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st135;
	} else
		goto st135;
	goto st0;
st191:
	if ( ++p == pe )
		goto _test_eof191;
case 191:
	switch( (*p) ) {
		case 33: goto st135;
		case 35: goto tr149;
		case 37: goto st6;
		case 46: goto st179;
		case 47: goto tr151;
		case 58: goto tr152;
		case 59: goto st135;
		case 61: goto st135;
		case 63: goto tr153;
		case 64: goto tr154;
		case 95: goto st135;
		case 126: goto st135;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st135;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st135;
		} else if ( (*p) >= 65 )
			goto st135;
	} else
		goto st192;
	goto st0;
st192:
	if ( ++p == pe )
		goto _test_eof192;
case 192:
	switch( (*p) ) {
		case 33: goto st135;
		case 35: goto tr149;
		case 37: goto st6;
		case 46: goto st179;
		case 47: goto tr151;
		case 58: goto tr152;
		case 59: goto st135;
		case 61: goto st135;
		case 63: goto tr153;
		case 64: goto tr154;
		case 95: goto st135;
		case 126: goto st135;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st135;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st135;
		} else if ( (*p) >= 65 )
			goto st135;
	} else
		goto st193;
	goto st0;
st193:
	if ( ++p == pe )
		goto _test_eof193;
case 193:
	switch( (*p) ) {
		case 33: goto st135;
		case 35: goto tr149;
		case 37: goto st6;
		case 47: goto tr151;
		case 58: goto tr152;
		case 59: goto st135;
		case 61: goto st135;
		case 63: goto tr153;
		case 64: goto tr154;
		case 95: goto st135;
		case 126: goto st135;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st135;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st135;
		} else if ( (*p) >= 65 )
			goto st135;
	} else
		goto st193;
	goto st0;
st194:
	if ( ++p == pe )
		goto _test_eof194;
case 194:
	switch( (*p) ) {
		case 35: goto tr141;
		case 47: goto st138;
		case 63: goto tr145;
	}
	goto st0;
tr147:
#line 158 "src/uri.rl"
	{ s = p; }
#line 144 "src/uri.rl"
	{ s = p; }
#line 100 "src/uri.rl"
	{ s = p; }
	goto st195;
st195:
	if ( ++p == pe )
		goto _test_eof195;
case 195:
#line 3574 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st135;
		case 35: goto tr149;
		case 37: goto st6;
		case 43: goto st195;
		case 47: goto tr151;
		case 58: goto tr236;
		case 59: goto st135;
		case 61: goto st135;
		case 63: goto tr153;
		case 64: goto tr154;
		case 95: goto st135;
		case 126: goto st135;
	}
	if ( (*p) < 45 ) {
		if ( 36 <= (*p) && (*p) <= 44 )
			goto st135;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st195;
		} else if ( (*p) >= 65 )
			goto st195;
	} else
		goto st195;
	goto st0;
tr236:
#line 160 "src/uri.rl"
	{scheme = s; scheme_len = p - s; }
#line 145 "src/uri.rl"
	{ login = s; login_len = p - s; }
#line 101 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st196;
st196:
	if ( ++p == pe )
		goto _test_eof196;
case 196:
#line 3613 "src/uri.c"
	switch( (*p) ) {
		case 33: goto tr164;
		case 35: goto tr165;
		case 37: goto tr166;
		case 47: goto tr237;
		case 59: goto tr164;
		case 61: goto tr164;
		case 63: goto tr169;
		case 64: goto tr170;
		case 95: goto tr164;
		case 126: goto tr164;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto tr164;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto tr171;
		} else if ( (*p) >= 65 )
			goto tr171;
	} else
		goto tr168;
	goto st0;
tr237:
#line 177 "src/uri.rl"
	{ uri->scheme = scheme; uri->scheme_len = scheme_len;}
#line 138 "src/uri.rl"
	{ s = p; }
#line 139 "src/uri.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 169 "src/uri.rl"
	{ s = p; }
	goto st197;
st197:
	if ( ++p == pe )
		goto _test_eof197;
case 197:
#line 3652 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st138;
		case 35: goto tr141;
		case 37: goto st11;
		case 47: goto st198;
		case 61: goto st138;
		case 63: goto tr145;
		case 95: goto st138;
		case 124: goto st138;
		case 126: goto st138;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st138;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st138;
	} else
		goto st138;
	goto st0;
st198:
	if ( ++p == pe )
		goto _test_eof198;
case 198:
	switch( (*p) ) {
		case 33: goto tr239;
		case 35: goto tr141;
		case 37: goto tr240;
		case 47: goto st138;
		case 58: goto st138;
		case 59: goto tr239;
		case 61: goto tr239;
		case 63: goto tr145;
		case 64: goto st138;
		case 91: goto st38;
		case 95: goto tr239;
		case 117: goto tr242;
		case 124: goto st138;
		case 126: goto tr239;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto tr239;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto tr239;
		} else if ( (*p) >= 65 )
			goto tr239;
	} else
		goto tr241;
	goto st0;
tr239:
#line 144 "src/uri.rl"
	{ s = p; }
#line 100 "src/uri.rl"
	{ s = p; }
	goto st199;
st199:
	if ( ++p == pe )
		goto _test_eof199;
case 199:
#line 3715 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st199;
		case 35: goto tr149;
		case 37: goto st114;
		case 47: goto tr151;
		case 58: goto tr244;
		case 61: goto st199;
		case 63: goto tr153;
		case 64: goto tr245;
		case 95: goto st199;
		case 124: goto st138;
		case 126: goto st199;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st199;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st199;
	} else
		goto st199;
	goto st0;
tr240:
#line 144 "src/uri.rl"
	{ s = p; }
#line 100 "src/uri.rl"
	{ s = p; }
	goto st114;
st114:
	if ( ++p == pe )
		goto _test_eof114;
case 114:
#line 3748 "src/uri.c"
	switch( (*p) ) {
		case 37: goto st199;
		case 117: goto st115;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st199;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st199;
	} else
		goto st199;
	goto st0;
st115:
	if ( ++p == pe )
		goto _test_eof115;
case 115:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st116;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st116;
	} else
		goto st116;
	goto st0;
st116:
	if ( ++p == pe )
		goto _test_eof116;
case 116:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st117;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st117;
	} else
		goto st117;
	goto st0;
st117:
	if ( ++p == pe )
		goto _test_eof117;
case 117:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st118;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st118;
	} else
		goto st118;
	goto st0;
st118:
	if ( ++p == pe )
		goto _test_eof118;
case 118:
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st199;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st199;
	} else
		goto st199;
	goto st0;
tr244:
#line 145 "src/uri.rl"
	{ login = s; login_len = p - s; }
#line 101 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st200;
tr293:
#line 145 "src/uri.rl"
	{ login = s; login_len = p - s; }
#line 108 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;
			   uri->host_hint = 1; }
#line 101 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st200;
st200:
	if ( ++p == pe )
		goto _test_eof200;
case 200:
#line 3833 "src/uri.c"
	switch( (*p) ) {
		case 33: goto tr246;
		case 35: goto tr165;
		case 37: goto tr247;
		case 47: goto tr167;
		case 58: goto st138;
		case 59: goto tr246;
		case 61: goto tr246;
		case 63: goto tr169;
		case 64: goto tr249;
		case 95: goto tr246;
		case 124: goto st138;
		case 126: goto tr246;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto tr246;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto tr250;
		} else if ( (*p) >= 65 )
			goto tr250;
	} else
		goto tr248;
	goto st0;
tr246:
#line 148 "src/uri.rl"
	{ s = p; }
	goto st201;
st201:
	if ( ++p == pe )
		goto _test_eof201;
case 201:
#line 3868 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st201;
		case 35: goto tr141;
		case 37: goto st119;
		case 47: goto st138;
		case 58: goto st138;
		case 61: goto st201;
		case 63: goto tr145;
		case 64: goto tr252;
		case 95: goto st201;
		case 124: goto st138;
		case 126: goto st201;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st201;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st201;
	} else
		goto st201;
	goto st0;
tr247:
#line 148 "src/uri.rl"
	{ s = p; }
	goto st119;
st119:
	if ( ++p == pe )
		goto _test_eof119;
case 119:
#line 3899 "src/uri.c"
	switch( (*p) ) {
		case 37: goto st201;
		case 117: goto st120;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st201;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st201;
	} else
		goto st201;
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
			goto st201;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st201;
	} else
		goto st201;
	goto st0;
tr252:
#line 149 "src/uri.rl"
	{ uri->password = s; uri->password_len = p - s; }
#line 153 "src/uri.rl"
	{ uri->login = login; uri->login_len = login_len; }
	goto st202;
tr245:
#line 145 "src/uri.rl"
	{ login = s; login_len = p - s; }
#line 153 "src/uri.rl"
	{ uri->login = login; uri->login_len = login_len; }
	goto st202;
tr249:
#line 148 "src/uri.rl"
	{ s = p; }
#line 149 "src/uri.rl"
	{ uri->password = s; uri->password_len = p - s; }
#line 153 "src/uri.rl"
	{ uri->login = login; uri->login_len = login_len; }
	goto st202;
st202:
	if ( ++p == pe )
		goto _test_eof202;
case 202:
#line 3989 "src/uri.c"
	switch( (*p) ) {
		case 33: goto tr253;
		case 35: goto tr141;
		case 37: goto tr254;
		case 47: goto st138;
		case 58: goto st138;
		case 59: goto tr253;
		case 61: goto tr253;
		case 63: goto tr145;
		case 64: goto st138;
		case 91: goto st38;
		case 95: goto tr253;
		case 117: goto tr256;
		case 124: goto st138;
		case 126: goto tr253;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto tr253;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto tr253;
		} else if ( (*p) >= 65 )
			goto tr253;
	} else
		goto tr255;
	goto st0;
tr253:
#line 100 "src/uri.rl"
	{ s = p; }
	goto st203;
st203:
	if ( ++p == pe )
		goto _test_eof203;
case 203:
#line 4026 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st203;
		case 35: goto tr149;
		case 37: goto st124;
		case 47: goto tr151;
		case 58: goto tr258;
		case 61: goto st203;
		case 63: goto tr153;
		case 64: goto st138;
		case 95: goto st203;
		case 124: goto st138;
		case 126: goto st203;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st203;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st203;
	} else
		goto st203;
	goto st0;
tr254:
#line 100 "src/uri.rl"
	{ s = p; }
	goto st124;
st124:
	if ( ++p == pe )
		goto _test_eof124;
case 124:
#line 4057 "src/uri.c"
	switch( (*p) ) {
		case 37: goto st203;
		case 117: goto st125;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st203;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st203;
	} else
		goto st203;
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
			goto st203;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st203;
	} else
		goto st203;
	goto st0;
tr258:
#line 101 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st204;
tr273:
#line 108 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;
			   uri->host_hint = 1; }
#line 101 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st204;
st204:
	if ( ++p == pe )
		goto _test_eof204;
case 204:
#line 4138 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st138;
		case 35: goto tr165;
		case 37: goto st11;
		case 47: goto tr167;
		case 61: goto st138;
		case 63: goto tr169;
		case 64: goto st138;
		case 95: goto st138;
		case 124: goto st138;
		case 126: goto st138;
	}
	if ( (*p) < 58 ) {
		if ( (*p) > 46 ) {
			if ( 48 <= (*p) && (*p) <= 57 )
				goto tr259;
		} else if ( (*p) >= 36 )
			goto st138;
	} else if ( (*p) > 59 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto tr260;
		} else if ( (*p) >= 65 )
			goto tr260;
	} else
		goto st138;
	goto st0;
tr259:
#line 138 "src/uri.rl"
	{ s = p; }
	goto st205;
st205:
	if ( ++p == pe )
		goto _test_eof205;
case 205:
#line 4174 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st138;
		case 35: goto tr176;
		case 37: goto st11;
		case 47: goto tr177;
		case 61: goto st138;
		case 63: goto tr179;
		case 95: goto st138;
		case 124: goto st138;
		case 126: goto st138;
	}
	if ( (*p) < 58 ) {
		if ( (*p) > 46 ) {
			if ( 48 <= (*p) && (*p) <= 57 )
				goto st205;
		} else if ( (*p) >= 36 )
			goto st138;
	} else if ( (*p) > 59 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st138;
		} else if ( (*p) >= 64 )
			goto st138;
	} else
		goto st138;
	goto st0;
tr260:
#line 138 "src/uri.rl"
	{ s = p; }
	goto st206;
st206:
	if ( ++p == pe )
		goto _test_eof206;
case 206:
#line 4209 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st138;
		case 35: goto tr176;
		case 37: goto st11;
		case 47: goto tr177;
		case 61: goto st138;
		case 63: goto tr179;
		case 64: goto st138;
		case 95: goto st138;
		case 124: goto st138;
		case 126: goto st138;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st138;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st206;
	} else
		goto st206;
	goto st0;
tr255:
#line 107 "src/uri.rl"
	{ s = p; }
#line 100 "src/uri.rl"
	{ s = p; }
	goto st207;
st207:
	if ( ++p == pe )
		goto _test_eof207;
case 207:
#line 4241 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st203;
		case 35: goto tr149;
		case 37: goto st124;
		case 46: goto st208;
		case 47: goto tr151;
		case 58: goto tr258;
		case 59: goto st203;
		case 61: goto st203;
		case 63: goto tr153;
		case 64: goto st138;
		case 95: goto st203;
		case 124: goto st138;
		case 126: goto st203;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st203;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st203;
		} else if ( (*p) >= 65 )
			goto st203;
	} else
		goto st220;
	goto st0;
st208:
	if ( ++p == pe )
		goto _test_eof208;
case 208:
	switch( (*p) ) {
		case 33: goto st203;
		case 35: goto tr149;
		case 37: goto st124;
		case 47: goto tr151;
		case 58: goto tr258;
		case 59: goto st203;
		case 61: goto st203;
		case 63: goto tr153;
		case 64: goto st138;
		case 95: goto st203;
		case 124: goto st138;
		case 126: goto st203;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st203;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st203;
		} else if ( (*p) >= 65 )
			goto st203;
	} else
		goto st209;
	goto st0;
st209:
	if ( ++p == pe )
		goto _test_eof209;
case 209:
	switch( (*p) ) {
		case 33: goto st203;
		case 35: goto tr149;
		case 37: goto st124;
		case 46: goto st210;
		case 47: goto tr151;
		case 58: goto tr258;
		case 59: goto st203;
		case 61: goto st203;
		case 63: goto tr153;
		case 64: goto st138;
		case 95: goto st203;
		case 124: goto st138;
		case 126: goto st203;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st203;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st203;
		} else if ( (*p) >= 65 )
			goto st203;
	} else
		goto st218;
	goto st0;
st210:
	if ( ++p == pe )
		goto _test_eof210;
case 210:
	switch( (*p) ) {
		case 33: goto st203;
		case 35: goto tr149;
		case 37: goto st124;
		case 47: goto tr151;
		case 58: goto tr258;
		case 59: goto st203;
		case 61: goto st203;
		case 63: goto tr153;
		case 64: goto st138;
		case 95: goto st203;
		case 124: goto st138;
		case 126: goto st203;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st203;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st203;
		} else if ( (*p) >= 65 )
			goto st203;
	} else
		goto st211;
	goto st0;
st211:
	if ( ++p == pe )
		goto _test_eof211;
case 211:
	switch( (*p) ) {
		case 33: goto st203;
		case 35: goto tr149;
		case 37: goto st124;
		case 46: goto st212;
		case 47: goto tr151;
		case 58: goto tr258;
		case 59: goto st203;
		case 61: goto st203;
		case 63: goto tr153;
		case 64: goto st138;
		case 95: goto st203;
		case 124: goto st138;
		case 126: goto st203;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st203;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st203;
		} else if ( (*p) >= 65 )
			goto st203;
	} else
		goto st216;
	goto st0;
st212:
	if ( ++p == pe )
		goto _test_eof212;
case 212:
	switch( (*p) ) {
		case 33: goto st203;
		case 35: goto tr149;
		case 37: goto st124;
		case 47: goto tr151;
		case 58: goto tr258;
		case 59: goto st203;
		case 61: goto st203;
		case 63: goto tr153;
		case 64: goto st138;
		case 95: goto st203;
		case 124: goto st138;
		case 126: goto st203;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st203;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st203;
		} else if ( (*p) >= 65 )
			goto st203;
	} else
		goto st213;
	goto st0;
st213:
	if ( ++p == pe )
		goto _test_eof213;
case 213:
	switch( (*p) ) {
		case 33: goto st203;
		case 35: goto tr191;
		case 37: goto st124;
		case 47: goto tr192;
		case 58: goto tr273;
		case 59: goto st203;
		case 61: goto st203;
		case 63: goto tr195;
		case 64: goto st138;
		case 95: goto st203;
		case 124: goto st138;
		case 126: goto st203;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st203;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st203;
		} else if ( (*p) >= 65 )
			goto st203;
	} else
		goto st214;
	goto st0;
st214:
	if ( ++p == pe )
		goto _test_eof214;
case 214:
	switch( (*p) ) {
		case 33: goto st203;
		case 35: goto tr191;
		case 37: goto st124;
		case 47: goto tr192;
		case 58: goto tr273;
		case 59: goto st203;
		case 61: goto st203;
		case 63: goto tr195;
		case 64: goto st138;
		case 95: goto st203;
		case 124: goto st138;
		case 126: goto st203;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st203;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st203;
		} else if ( (*p) >= 65 )
			goto st203;
	} else
		goto st215;
	goto st0;
st215:
	if ( ++p == pe )
		goto _test_eof215;
case 215:
	switch( (*p) ) {
		case 33: goto st203;
		case 35: goto tr191;
		case 37: goto st124;
		case 47: goto tr192;
		case 58: goto tr273;
		case 61: goto st203;
		case 63: goto tr195;
		case 64: goto st138;
		case 95: goto st203;
		case 124: goto st138;
		case 126: goto st203;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st203;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st203;
	} else
		goto st203;
	goto st0;
st216:
	if ( ++p == pe )
		goto _test_eof216;
case 216:
	switch( (*p) ) {
		case 33: goto st203;
		case 35: goto tr149;
		case 37: goto st124;
		case 46: goto st212;
		case 47: goto tr151;
		case 58: goto tr258;
		case 59: goto st203;
		case 61: goto st203;
		case 63: goto tr153;
		case 64: goto st138;
		case 95: goto st203;
		case 124: goto st138;
		case 126: goto st203;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st203;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st203;
		} else if ( (*p) >= 65 )
			goto st203;
	} else
		goto st217;
	goto st0;
st217:
	if ( ++p == pe )
		goto _test_eof217;
case 217:
	switch( (*p) ) {
		case 33: goto st203;
		case 35: goto tr149;
		case 37: goto st124;
		case 46: goto st212;
		case 47: goto tr151;
		case 58: goto tr258;
		case 61: goto st203;
		case 63: goto tr153;
		case 64: goto st138;
		case 95: goto st203;
		case 124: goto st138;
		case 126: goto st203;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st203;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st203;
	} else
		goto st203;
	goto st0;
st218:
	if ( ++p == pe )
		goto _test_eof218;
case 218:
	switch( (*p) ) {
		case 33: goto st203;
		case 35: goto tr149;
		case 37: goto st124;
		case 46: goto st210;
		case 47: goto tr151;
		case 58: goto tr258;
		case 59: goto st203;
		case 61: goto st203;
		case 63: goto tr153;
		case 64: goto st138;
		case 95: goto st203;
		case 124: goto st138;
		case 126: goto st203;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st203;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st203;
		} else if ( (*p) >= 65 )
			goto st203;
	} else
		goto st219;
	goto st0;
st219:
	if ( ++p == pe )
		goto _test_eof219;
case 219:
	switch( (*p) ) {
		case 33: goto st203;
		case 35: goto tr149;
		case 37: goto st124;
		case 46: goto st210;
		case 47: goto tr151;
		case 58: goto tr258;
		case 61: goto st203;
		case 63: goto tr153;
		case 64: goto st138;
		case 95: goto st203;
		case 124: goto st138;
		case 126: goto st203;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st203;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st203;
	} else
		goto st203;
	goto st0;
st220:
	if ( ++p == pe )
		goto _test_eof220;
case 220:
	switch( (*p) ) {
		case 33: goto st203;
		case 35: goto tr149;
		case 37: goto st124;
		case 46: goto st208;
		case 47: goto tr151;
		case 58: goto tr258;
		case 59: goto st203;
		case 61: goto st203;
		case 63: goto tr153;
		case 64: goto st138;
		case 95: goto st203;
		case 124: goto st138;
		case 126: goto st203;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st203;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st203;
		} else if ( (*p) >= 65 )
			goto st203;
	} else
		goto st221;
	goto st0;
st221:
	if ( ++p == pe )
		goto _test_eof221;
case 221:
	switch( (*p) ) {
		case 33: goto st203;
		case 35: goto tr149;
		case 37: goto st124;
		case 46: goto st208;
		case 47: goto tr151;
		case 58: goto tr258;
		case 61: goto st203;
		case 63: goto tr153;
		case 64: goto st138;
		case 95: goto st203;
		case 124: goto st138;
		case 126: goto st203;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st203;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st203;
	} else
		goto st203;
	goto st0;
tr256:
#line 100 "src/uri.rl"
	{ s = p; }
	goto st222;
st222:
	if ( ++p == pe )
		goto _test_eof222;
case 222:
#line 4689 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st203;
		case 35: goto tr149;
		case 37: goto st124;
		case 47: goto tr151;
		case 58: goto tr258;
		case 61: goto st203;
		case 63: goto tr153;
		case 64: goto st138;
		case 95: goto st203;
		case 110: goto st223;
		case 124: goto st138;
		case 126: goto st203;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st203;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st203;
	} else
		goto st203;
	goto st0;
st223:
	if ( ++p == pe )
		goto _test_eof223;
case 223:
	switch( (*p) ) {
		case 33: goto st203;
		case 35: goto tr149;
		case 37: goto st124;
		case 47: goto tr151;
		case 58: goto tr258;
		case 61: goto st203;
		case 63: goto tr153;
		case 64: goto st138;
		case 95: goto st203;
		case 105: goto st224;
		case 124: goto st138;
		case 126: goto st203;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st203;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st203;
	} else
		goto st203;
	goto st0;
st224:
	if ( ++p == pe )
		goto _test_eof224;
case 224:
	switch( (*p) ) {
		case 33: goto st203;
		case 35: goto tr149;
		case 37: goto st124;
		case 47: goto tr151;
		case 58: goto tr258;
		case 61: goto st203;
		case 63: goto tr153;
		case 64: goto st138;
		case 95: goto st203;
		case 120: goto st225;
		case 124: goto st138;
		case 126: goto st203;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st203;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st203;
	} else
		goto st203;
	goto st0;
st225:
	if ( ++p == pe )
		goto _test_eof225;
case 225:
	switch( (*p) ) {
		case 33: goto st203;
		case 35: goto tr149;
		case 37: goto st124;
		case 47: goto tr207;
		case 58: goto tr258;
		case 61: goto st203;
		case 63: goto tr153;
		case 64: goto st138;
		case 95: goto st203;
		case 124: goto st138;
		case 126: goto st203;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st203;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st203;
	} else
		goto st203;
	goto st0;
tr248:
#line 148 "src/uri.rl"
	{ s = p; }
#line 138 "src/uri.rl"
	{ s = p; }
	goto st226;
st226:
	if ( ++p == pe )
		goto _test_eof226;
case 226:
#line 4803 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st201;
		case 35: goto tr176;
		case 37: goto st119;
		case 47: goto tr177;
		case 58: goto st138;
		case 59: goto st201;
		case 61: goto st201;
		case 63: goto tr179;
		case 64: goto tr252;
		case 95: goto st201;
		case 124: goto st138;
		case 126: goto st201;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st201;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st201;
		} else if ( (*p) >= 65 )
			goto st201;
	} else
		goto st226;
	goto st0;
tr250:
#line 148 "src/uri.rl"
	{ s = p; }
#line 138 "src/uri.rl"
	{ s = p; }
	goto st227;
st227:
	if ( ++p == pe )
		goto _test_eof227;
case 227:
#line 4840 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st201;
		case 35: goto tr176;
		case 37: goto st119;
		case 47: goto tr177;
		case 58: goto st138;
		case 61: goto st201;
		case 63: goto tr179;
		case 64: goto tr252;
		case 95: goto st201;
		case 124: goto st138;
		case 126: goto st201;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st201;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st227;
	} else
		goto st227;
	goto st0;
tr241:
#line 144 "src/uri.rl"
	{ s = p; }
#line 107 "src/uri.rl"
	{ s = p; }
#line 100 "src/uri.rl"
	{ s = p; }
	goto st228;
st228:
	if ( ++p == pe )
		goto _test_eof228;
case 228:
#line 4875 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st199;
		case 35: goto tr149;
		case 37: goto st114;
		case 46: goto st229;
		case 47: goto tr151;
		case 58: goto tr244;
		case 59: goto st199;
		case 61: goto st199;
		case 63: goto tr153;
		case 64: goto tr245;
		case 95: goto st199;
		case 124: goto st138;
		case 126: goto st199;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st199;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st199;
		} else if ( (*p) >= 65 )
			goto st199;
	} else
		goto st241;
	goto st0;
st229:
	if ( ++p == pe )
		goto _test_eof229;
case 229:
	switch( (*p) ) {
		case 33: goto st199;
		case 35: goto tr149;
		case 37: goto st114;
		case 47: goto tr151;
		case 58: goto tr244;
		case 59: goto st199;
		case 61: goto st199;
		case 63: goto tr153;
		case 64: goto tr245;
		case 95: goto st199;
		case 124: goto st138;
		case 126: goto st199;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st199;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st199;
		} else if ( (*p) >= 65 )
			goto st199;
	} else
		goto st230;
	goto st0;
st230:
	if ( ++p == pe )
		goto _test_eof230;
case 230:
	switch( (*p) ) {
		case 33: goto st199;
		case 35: goto tr149;
		case 37: goto st114;
		case 46: goto st231;
		case 47: goto tr151;
		case 58: goto tr244;
		case 59: goto st199;
		case 61: goto st199;
		case 63: goto tr153;
		case 64: goto tr245;
		case 95: goto st199;
		case 124: goto st138;
		case 126: goto st199;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st199;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st199;
		} else if ( (*p) >= 65 )
			goto st199;
	} else
		goto st239;
	goto st0;
st231:
	if ( ++p == pe )
		goto _test_eof231;
case 231:
	switch( (*p) ) {
		case 33: goto st199;
		case 35: goto tr149;
		case 37: goto st114;
		case 47: goto tr151;
		case 58: goto tr244;
		case 59: goto st199;
		case 61: goto st199;
		case 63: goto tr153;
		case 64: goto tr245;
		case 95: goto st199;
		case 124: goto st138;
		case 126: goto st199;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st199;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st199;
		} else if ( (*p) >= 65 )
			goto st199;
	} else
		goto st232;
	goto st0;
st232:
	if ( ++p == pe )
		goto _test_eof232;
case 232:
	switch( (*p) ) {
		case 33: goto st199;
		case 35: goto tr149;
		case 37: goto st114;
		case 46: goto st233;
		case 47: goto tr151;
		case 58: goto tr244;
		case 59: goto st199;
		case 61: goto st199;
		case 63: goto tr153;
		case 64: goto tr245;
		case 95: goto st199;
		case 124: goto st138;
		case 126: goto st199;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st199;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st199;
		} else if ( (*p) >= 65 )
			goto st199;
	} else
		goto st237;
	goto st0;
st233:
	if ( ++p == pe )
		goto _test_eof233;
case 233:
	switch( (*p) ) {
		case 33: goto st199;
		case 35: goto tr149;
		case 37: goto st114;
		case 47: goto tr151;
		case 58: goto tr244;
		case 59: goto st199;
		case 61: goto st199;
		case 63: goto tr153;
		case 64: goto tr245;
		case 95: goto st199;
		case 124: goto st138;
		case 126: goto st199;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st199;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st199;
		} else if ( (*p) >= 65 )
			goto st199;
	} else
		goto st234;
	goto st0;
st234:
	if ( ++p == pe )
		goto _test_eof234;
case 234:
	switch( (*p) ) {
		case 33: goto st199;
		case 35: goto tr191;
		case 37: goto st114;
		case 47: goto tr192;
		case 58: goto tr293;
		case 59: goto st199;
		case 61: goto st199;
		case 63: goto tr195;
		case 64: goto tr245;
		case 95: goto st199;
		case 124: goto st138;
		case 126: goto st199;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st199;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st199;
		} else if ( (*p) >= 65 )
			goto st199;
	} else
		goto st235;
	goto st0;
st235:
	if ( ++p == pe )
		goto _test_eof235;
case 235:
	switch( (*p) ) {
		case 33: goto st199;
		case 35: goto tr191;
		case 37: goto st114;
		case 47: goto tr192;
		case 58: goto tr293;
		case 59: goto st199;
		case 61: goto st199;
		case 63: goto tr195;
		case 64: goto tr245;
		case 95: goto st199;
		case 124: goto st138;
		case 126: goto st199;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st199;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st199;
		} else if ( (*p) >= 65 )
			goto st199;
	} else
		goto st236;
	goto st0;
st236:
	if ( ++p == pe )
		goto _test_eof236;
case 236:
	switch( (*p) ) {
		case 33: goto st199;
		case 35: goto tr191;
		case 37: goto st114;
		case 47: goto tr192;
		case 58: goto tr293;
		case 61: goto st199;
		case 63: goto tr195;
		case 64: goto tr245;
		case 95: goto st199;
		case 124: goto st138;
		case 126: goto st199;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st199;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st199;
	} else
		goto st199;
	goto st0;
st237:
	if ( ++p == pe )
		goto _test_eof237;
case 237:
	switch( (*p) ) {
		case 33: goto st199;
		case 35: goto tr149;
		case 37: goto st114;
		case 46: goto st233;
		case 47: goto tr151;
		case 58: goto tr244;
		case 59: goto st199;
		case 61: goto st199;
		case 63: goto tr153;
		case 64: goto tr245;
		case 95: goto st199;
		case 124: goto st138;
		case 126: goto st199;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st199;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st199;
		} else if ( (*p) >= 65 )
			goto st199;
	} else
		goto st238;
	goto st0;
st238:
	if ( ++p == pe )
		goto _test_eof238;
case 238:
	switch( (*p) ) {
		case 33: goto st199;
		case 35: goto tr149;
		case 37: goto st114;
		case 46: goto st233;
		case 47: goto tr151;
		case 58: goto tr244;
		case 61: goto st199;
		case 63: goto tr153;
		case 64: goto tr245;
		case 95: goto st199;
		case 124: goto st138;
		case 126: goto st199;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st199;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st199;
	} else
		goto st199;
	goto st0;
st239:
	if ( ++p == pe )
		goto _test_eof239;
case 239:
	switch( (*p) ) {
		case 33: goto st199;
		case 35: goto tr149;
		case 37: goto st114;
		case 46: goto st231;
		case 47: goto tr151;
		case 58: goto tr244;
		case 59: goto st199;
		case 61: goto st199;
		case 63: goto tr153;
		case 64: goto tr245;
		case 95: goto st199;
		case 124: goto st138;
		case 126: goto st199;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st199;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st199;
		} else if ( (*p) >= 65 )
			goto st199;
	} else
		goto st240;
	goto st0;
st240:
	if ( ++p == pe )
		goto _test_eof240;
case 240:
	switch( (*p) ) {
		case 33: goto st199;
		case 35: goto tr149;
		case 37: goto st114;
		case 46: goto st231;
		case 47: goto tr151;
		case 58: goto tr244;
		case 61: goto st199;
		case 63: goto tr153;
		case 64: goto tr245;
		case 95: goto st199;
		case 124: goto st138;
		case 126: goto st199;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st199;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st199;
	} else
		goto st199;
	goto st0;
st241:
	if ( ++p == pe )
		goto _test_eof241;
case 241:
	switch( (*p) ) {
		case 33: goto st199;
		case 35: goto tr149;
		case 37: goto st114;
		case 46: goto st229;
		case 47: goto tr151;
		case 58: goto tr244;
		case 59: goto st199;
		case 61: goto st199;
		case 63: goto tr153;
		case 64: goto tr245;
		case 95: goto st199;
		case 124: goto st138;
		case 126: goto st199;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st199;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st199;
		} else if ( (*p) >= 65 )
			goto st199;
	} else
		goto st242;
	goto st0;
st242:
	if ( ++p == pe )
		goto _test_eof242;
case 242:
	switch( (*p) ) {
		case 33: goto st199;
		case 35: goto tr149;
		case 37: goto st114;
		case 46: goto st229;
		case 47: goto tr151;
		case 58: goto tr244;
		case 61: goto st199;
		case 63: goto tr153;
		case 64: goto tr245;
		case 95: goto st199;
		case 124: goto st138;
		case 126: goto st199;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st199;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st199;
	} else
		goto st199;
	goto st0;
tr242:
#line 144 "src/uri.rl"
	{ s = p; }
#line 100 "src/uri.rl"
	{ s = p; }
	goto st243;
st243:
	if ( ++p == pe )
		goto _test_eof243;
case 243:
#line 5325 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st199;
		case 35: goto tr149;
		case 37: goto st114;
		case 47: goto tr151;
		case 58: goto tr244;
		case 61: goto st199;
		case 63: goto tr153;
		case 64: goto tr245;
		case 95: goto st199;
		case 110: goto st244;
		case 124: goto st138;
		case 126: goto st199;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st199;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st199;
	} else
		goto st199;
	goto st0;
st244:
	if ( ++p == pe )
		goto _test_eof244;
case 244:
	switch( (*p) ) {
		case 33: goto st199;
		case 35: goto tr149;
		case 37: goto st114;
		case 47: goto tr151;
		case 58: goto tr244;
		case 61: goto st199;
		case 63: goto tr153;
		case 64: goto tr245;
		case 95: goto st199;
		case 105: goto st245;
		case 124: goto st138;
		case 126: goto st199;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st199;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st199;
	} else
		goto st199;
	goto st0;
st245:
	if ( ++p == pe )
		goto _test_eof245;
case 245:
	switch( (*p) ) {
		case 33: goto st199;
		case 35: goto tr149;
		case 37: goto st114;
		case 47: goto tr151;
		case 58: goto tr244;
		case 61: goto st199;
		case 63: goto tr153;
		case 64: goto tr245;
		case 95: goto st199;
		case 120: goto st246;
		case 124: goto st138;
		case 126: goto st199;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st199;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st199;
	} else
		goto st199;
	goto st0;
st246:
	if ( ++p == pe )
		goto _test_eof246;
case 246:
	switch( (*p) ) {
		case 33: goto st199;
		case 35: goto tr149;
		case 37: goto st114;
		case 47: goto tr301;
		case 58: goto tr244;
		case 61: goto st199;
		case 63: goto tr153;
		case 64: goto tr245;
		case 95: goto st199;
		case 124: goto st138;
		case 126: goto st199;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st199;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st199;
	} else
		goto st199;
	goto st0;
tr301:
#line 101 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 169 "src/uri.rl"
	{ s = p; }
	goto st247;
st247:
	if ( ++p == pe )
		goto _test_eof247;
case 247:
#line 5439 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st138;
		case 35: goto tr141;
		case 37: goto st11;
		case 58: goto st248;
		case 61: goto st138;
		case 63: goto tr145;
		case 95: goto st138;
		case 124: goto st138;
		case 126: goto st138;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st138;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st138;
	} else
		goto st138;
	goto st0;
st248:
	if ( ++p == pe )
		goto _test_eof248;
case 248:
	switch( (*p) ) {
		case 33: goto st138;
		case 35: goto tr141;
		case 37: goto st11;
		case 46: goto tr303;
		case 47: goto tr304;
		case 61: goto st138;
		case 63: goto tr145;
		case 95: goto st138;
		case 124: goto st138;
		case 126: goto st138;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st138;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st138;
	} else
		goto st138;
	goto st0;
tr303:
#line 135 "src/uri.rl"
	{ s = p;}
	goto st249;
st249:
	if ( ++p == pe )
		goto _test_eof249;
case 249:
#line 5493 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st138;
		case 35: goto tr141;
		case 37: goto st11;
		case 47: goto st250;
		case 61: goto st138;
		case 63: goto tr145;
		case 95: goto st138;
		case 124: goto st138;
		case 126: goto st138;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st138;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st138;
	} else
		goto st138;
	goto st0;
tr304:
#line 135 "src/uri.rl"
	{ s = p;}
	goto st250;
st250:
	if ( ++p == pe )
		goto _test_eof250;
case 250:
#line 5522 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st251;
		case 35: goto tr141;
		case 37: goto st129;
		case 47: goto st138;
		case 58: goto st138;
		case 61: goto st251;
		case 63: goto tr145;
		case 95: goto st251;
		case 124: goto st138;
		case 126: goto st251;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st251;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st251;
	} else
		goto st251;
	goto st0;
st251:
	if ( ++p == pe )
		goto _test_eof251;
case 251:
	switch( (*p) ) {
		case 33: goto st251;
		case 35: goto tr307;
		case 37: goto st129;
		case 47: goto st250;
		case 58: goto tr214;
		case 61: goto st251;
		case 63: goto tr308;
		case 95: goto st251;
		case 124: goto st138;
		case 126: goto st251;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st251;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st251;
	} else
		goto st251;
	goto st0;
st129:
	if ( ++p == pe )
		goto _test_eof129;
case 129:
	switch( (*p) ) {
		case 37: goto st251;
		case 117: goto st130;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st251;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st251;
	} else
		goto st251;
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
			goto st251;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st251;
	} else
		goto st251;
	goto st0;
tr148:
#line 158 "src/uri.rl"
	{ s = p; }
#line 144 "src/uri.rl"
	{ s = p; }
#line 100 "src/uri.rl"
	{ s = p; }
	goto st252;
st252:
	if ( ++p == pe )
		goto _test_eof252;
case 252:
#line 5650 "src/uri.c"
	switch( (*p) ) {
		case 33: goto st135;
		case 35: goto tr149;
		case 37: goto st6;
		case 43: goto st195;
		case 47: goto tr151;
		case 58: goto tr236;
		case 59: goto st135;
		case 61: goto st135;
		case 63: goto tr153;
		case 64: goto tr154;
		case 95: goto st135;
		case 110: goto st253;
		case 126: goto st135;
	}
	if ( (*p) < 45 ) {
		if ( 36 <= (*p) && (*p) <= 44 )
			goto st135;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st195;
		} else if ( (*p) >= 65 )
			goto st195;
	} else
		goto st195;
	goto st0;
st253:
	if ( ++p == pe )
		goto _test_eof253;
case 253:
	switch( (*p) ) {
		case 33: goto st135;
		case 35: goto tr149;
		case 37: goto st6;
		case 43: goto st195;
		case 47: goto tr151;
		case 58: goto tr236;
		case 59: goto st135;
		case 61: goto st135;
		case 63: goto tr153;
		case 64: goto tr154;
		case 95: goto st135;
		case 105: goto st254;
		case 126: goto st135;
	}
	if ( (*p) < 45 ) {
		if ( 36 <= (*p) && (*p) <= 44 )
			goto st135;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st195;
		} else if ( (*p) >= 65 )
			goto st195;
	} else
		goto st195;
	goto st0;
st254:
	if ( ++p == pe )
		goto _test_eof254;
case 254:
	switch( (*p) ) {
		case 33: goto st135;
		case 35: goto tr149;
		case 37: goto st6;
		case 43: goto st195;
		case 47: goto tr151;
		case 58: goto tr236;
		case 59: goto st135;
		case 61: goto st135;
		case 63: goto tr153;
		case 64: goto tr154;
		case 95: goto st135;
		case 120: goto st255;
		case 126: goto st135;
	}
	if ( (*p) < 45 ) {
		if ( 36 <= (*p) && (*p) <= 44 )
			goto st135;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st195;
		} else if ( (*p) >= 65 )
			goto st195;
	} else
		goto st195;
	goto st0;
st255:
	if ( ++p == pe )
		goto _test_eof255;
case 255:
	switch( (*p) ) {
		case 33: goto st135;
		case 35: goto tr149;
		case 37: goto st6;
		case 43: goto st195;
		case 47: goto tr301;
		case 58: goto tr236;
		case 59: goto st135;
		case 61: goto st135;
		case 63: goto tr153;
		case 64: goto tr154;
		case 95: goto st135;
		case 126: goto st135;
	}
	if ( (*p) < 45 ) {
		if ( 36 <= (*p) && (*p) <= 44 )
			goto st135;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st195;
		} else if ( (*p) >= 65 )
			goto st195;
	} else
		goto st195;
	goto st0;
	}
	_test_eof135: cs = 135; goto _test_eof; 
	_test_eof136: cs = 136; goto _test_eof; 
	_test_eof137: cs = 137; goto _test_eof; 
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
	_test_eof138: cs = 138; goto _test_eof; 
	_test_eof11: cs = 11; goto _test_eof; 
	_test_eof12: cs = 12; goto _test_eof; 
	_test_eof13: cs = 13; goto _test_eof; 
	_test_eof14: cs = 14; goto _test_eof; 
	_test_eof15: cs = 15; goto _test_eof; 
	_test_eof139: cs = 139; goto _test_eof; 
	_test_eof140: cs = 140; goto _test_eof; 
	_test_eof16: cs = 16; goto _test_eof; 
	_test_eof17: cs = 17; goto _test_eof; 
	_test_eof18: cs = 18; goto _test_eof; 
	_test_eof19: cs = 19; goto _test_eof; 
	_test_eof20: cs = 20; goto _test_eof; 
	_test_eof141: cs = 141; goto _test_eof; 
	_test_eof21: cs = 21; goto _test_eof; 
	_test_eof22: cs = 22; goto _test_eof; 
	_test_eof23: cs = 23; goto _test_eof; 
	_test_eof24: cs = 24; goto _test_eof; 
	_test_eof25: cs = 25; goto _test_eof; 
	_test_eof26: cs = 26; goto _test_eof; 
	_test_eof27: cs = 27; goto _test_eof; 
	_test_eof142: cs = 142; goto _test_eof; 
	_test_eof28: cs = 28; goto _test_eof; 
	_test_eof29: cs = 29; goto _test_eof; 
	_test_eof30: cs = 30; goto _test_eof; 
	_test_eof31: cs = 31; goto _test_eof; 
	_test_eof32: cs = 32; goto _test_eof; 
	_test_eof143: cs = 143; goto _test_eof; 
	_test_eof144: cs = 144; goto _test_eof; 
	_test_eof145: cs = 145; goto _test_eof; 
	_test_eof146: cs = 146; goto _test_eof; 
	_test_eof147: cs = 147; goto _test_eof; 
	_test_eof33: cs = 33; goto _test_eof; 
	_test_eof34: cs = 34; goto _test_eof; 
	_test_eof35: cs = 35; goto _test_eof; 
	_test_eof36: cs = 36; goto _test_eof; 
	_test_eof37: cs = 37; goto _test_eof; 
	_test_eof148: cs = 148; goto _test_eof; 
	_test_eof149: cs = 149; goto _test_eof; 
	_test_eof150: cs = 150; goto _test_eof; 
	_test_eof151: cs = 151; goto _test_eof; 
	_test_eof152: cs = 152; goto _test_eof; 
	_test_eof153: cs = 153; goto _test_eof; 
	_test_eof154: cs = 154; goto _test_eof; 
	_test_eof155: cs = 155; goto _test_eof; 
	_test_eof156: cs = 156; goto _test_eof; 
	_test_eof157: cs = 157; goto _test_eof; 
	_test_eof158: cs = 158; goto _test_eof; 
	_test_eof159: cs = 159; goto _test_eof; 
	_test_eof160: cs = 160; goto _test_eof; 
	_test_eof161: cs = 161; goto _test_eof; 
	_test_eof162: cs = 162; goto _test_eof; 
	_test_eof38: cs = 38; goto _test_eof; 
	_test_eof39: cs = 39; goto _test_eof; 
	_test_eof40: cs = 40; goto _test_eof; 
	_test_eof41: cs = 41; goto _test_eof; 
	_test_eof42: cs = 42; goto _test_eof; 
	_test_eof43: cs = 43; goto _test_eof; 
	_test_eof44: cs = 44; goto _test_eof; 
	_test_eof45: cs = 45; goto _test_eof; 
	_test_eof46: cs = 46; goto _test_eof; 
	_test_eof47: cs = 47; goto _test_eof; 
	_test_eof48: cs = 48; goto _test_eof; 
	_test_eof49: cs = 49; goto _test_eof; 
	_test_eof50: cs = 50; goto _test_eof; 
	_test_eof51: cs = 51; goto _test_eof; 
	_test_eof52: cs = 52; goto _test_eof; 
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
	_test_eof163: cs = 163; goto _test_eof; 
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
	_test_eof98: cs = 98; goto _test_eof; 
	_test_eof99: cs = 99; goto _test_eof; 
	_test_eof100: cs = 100; goto _test_eof; 
	_test_eof101: cs = 101; goto _test_eof; 
	_test_eof102: cs = 102; goto _test_eof; 
	_test_eof103: cs = 103; goto _test_eof; 
	_test_eof164: cs = 164; goto _test_eof; 
	_test_eof165: cs = 165; goto _test_eof; 
	_test_eof166: cs = 166; goto _test_eof; 
	_test_eof167: cs = 167; goto _test_eof; 
	_test_eof168: cs = 168; goto _test_eof; 
	_test_eof169: cs = 169; goto _test_eof; 
	_test_eof170: cs = 170; goto _test_eof; 
	_test_eof171: cs = 171; goto _test_eof; 
	_test_eof172: cs = 172; goto _test_eof; 
	_test_eof104: cs = 104; goto _test_eof; 
	_test_eof105: cs = 105; goto _test_eof; 
	_test_eof106: cs = 106; goto _test_eof; 
	_test_eof107: cs = 107; goto _test_eof; 
	_test_eof108: cs = 108; goto _test_eof; 
	_test_eof173: cs = 173; goto _test_eof; 
	_test_eof174: cs = 174; goto _test_eof; 
	_test_eof175: cs = 175; goto _test_eof; 
	_test_eof176: cs = 176; goto _test_eof; 
	_test_eof177: cs = 177; goto _test_eof; 
	_test_eof109: cs = 109; goto _test_eof; 
	_test_eof110: cs = 110; goto _test_eof; 
	_test_eof111: cs = 111; goto _test_eof; 
	_test_eof112: cs = 112; goto _test_eof; 
	_test_eof113: cs = 113; goto _test_eof; 
	_test_eof178: cs = 178; goto _test_eof; 
	_test_eof179: cs = 179; goto _test_eof; 
	_test_eof180: cs = 180; goto _test_eof; 
	_test_eof181: cs = 181; goto _test_eof; 
	_test_eof182: cs = 182; goto _test_eof; 
	_test_eof183: cs = 183; goto _test_eof; 
	_test_eof184: cs = 184; goto _test_eof; 
	_test_eof185: cs = 185; goto _test_eof; 
	_test_eof186: cs = 186; goto _test_eof; 
	_test_eof187: cs = 187; goto _test_eof; 
	_test_eof188: cs = 188; goto _test_eof; 
	_test_eof189: cs = 189; goto _test_eof; 
	_test_eof190: cs = 190; goto _test_eof; 
	_test_eof191: cs = 191; goto _test_eof; 
	_test_eof192: cs = 192; goto _test_eof; 
	_test_eof193: cs = 193; goto _test_eof; 
	_test_eof194: cs = 194; goto _test_eof; 
	_test_eof195: cs = 195; goto _test_eof; 
	_test_eof196: cs = 196; goto _test_eof; 
	_test_eof197: cs = 197; goto _test_eof; 
	_test_eof198: cs = 198; goto _test_eof; 
	_test_eof199: cs = 199; goto _test_eof; 
	_test_eof114: cs = 114; goto _test_eof; 
	_test_eof115: cs = 115; goto _test_eof; 
	_test_eof116: cs = 116; goto _test_eof; 
	_test_eof117: cs = 117; goto _test_eof; 
	_test_eof118: cs = 118; goto _test_eof; 
	_test_eof200: cs = 200; goto _test_eof; 
	_test_eof201: cs = 201; goto _test_eof; 
	_test_eof119: cs = 119; goto _test_eof; 
	_test_eof120: cs = 120; goto _test_eof; 
	_test_eof121: cs = 121; goto _test_eof; 
	_test_eof122: cs = 122; goto _test_eof; 
	_test_eof123: cs = 123; goto _test_eof; 
	_test_eof202: cs = 202; goto _test_eof; 
	_test_eof203: cs = 203; goto _test_eof; 
	_test_eof124: cs = 124; goto _test_eof; 
	_test_eof125: cs = 125; goto _test_eof; 
	_test_eof126: cs = 126; goto _test_eof; 
	_test_eof127: cs = 127; goto _test_eof; 
	_test_eof128: cs = 128; goto _test_eof; 
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
	_test_eof227: cs = 227; goto _test_eof; 
	_test_eof228: cs = 228; goto _test_eof; 
	_test_eof229: cs = 229; goto _test_eof; 
	_test_eof230: cs = 230; goto _test_eof; 
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
	_test_eof129: cs = 129; goto _test_eof; 
	_test_eof130: cs = 130; goto _test_eof; 
	_test_eof131: cs = 131; goto _test_eof; 
	_test_eof132: cs = 132; goto _test_eof; 
	_test_eof133: cs = 133; goto _test_eof; 
	_test_eof252: cs = 252; goto _test_eof; 
	_test_eof253: cs = 253; goto _test_eof; 
	_test_eof254: cs = 254; goto _test_eof; 
	_test_eof255: cs = 255; goto _test_eof; 

	_test_eof: {}
	if ( p == eof )
	{
	switch ( cs ) {
	case 140: 
#line 72 "src/uri.rl"
	{ uri->query = s; uri->query_len = p - s; }
	break;
	case 137: 
#line 76 "src/uri.rl"
	{ uri->fragment = s; uri->fragment_len = p - s; }
	break;
	case 146: 
	case 147: 
#line 119 "src/uri.rl"
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
	case 134: 
	case 138: 
	case 168: 
	case 169: 
	case 170: 
	case 171: 
	case 194: 
	case 197: 
	case 198: 
	case 201: 
	case 202: 
	case 247: 
	case 248: 
	case 249: 
	case 250: 
#line 173 "src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
	break;
	case 139: 
#line 71 "src/uri.rl"
	{ s = p; }
#line 72 "src/uri.rl"
	{ uri->query = s; uri->query_len = p - s; }
	break;
	case 136: 
#line 75 "src/uri.rl"
	{ s = p; }
#line 76 "src/uri.rl"
	{ uri->fragment = s; uri->fragment_len = p - s; }
	break;
	case 163: 
	case 173: 
#line 169 "src/uri.rl"
	{ s = p; }
#line 173 "src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
	break;
	case 176: 
	case 177: 
	case 251: 
#line 173 "src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 119 "src/uri.rl"
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
	case 135: 
	case 142: 
	case 148: 
	case 149: 
	case 150: 
	case 151: 
	case 152: 
	case 153: 
	case 157: 
	case 158: 
	case 159: 
	case 160: 
	case 161: 
	case 162: 
	case 164: 
	case 165: 
	case 166: 
	case 167: 
	case 179: 
	case 180: 
	case 181: 
	case 182: 
	case 183: 
	case 187: 
	case 188: 
	case 189: 
	case 190: 
	case 195: 
	case 199: 
	case 203: 
	case 207: 
	case 208: 
	case 209: 
	case 210: 
	case 211: 
	case 212: 
	case 216: 
	case 217: 
	case 218: 
	case 219: 
	case 220: 
	case 221: 
	case 222: 
	case 223: 
	case 224: 
	case 225: 
	case 228: 
	case 229: 
	case 230: 
	case 231: 
	case 232: 
	case 233: 
	case 237: 
	case 238: 
	case 239: 
	case 240: 
	case 241: 
	case 242: 
	case 243: 
	case 244: 
	case 245: 
	case 246: 
	case 252: 
	case 253: 
	case 254: 
	case 255: 
#line 101 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 169 "src/uri.rl"
	{ s = p; }
#line 173 "src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
	break;
	case 172: 
#line 119 "src/uri.rl"
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
#line 168 "src/uri.rl"
	{ s = p; }
#line 173 "src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
	break;
	case 144: 
	case 145: 
	case 174: 
	case 175: 
	case 205: 
	case 206: 
	case 226: 
	case 227: 
#line 139 "src/uri.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 169 "src/uri.rl"
	{ s = p; }
#line 173 "src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
	break;
	case 178: 
	case 191: 
	case 192: 
	case 193: 
#line 101 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 169 "src/uri.rl"
	{ s = p; }
#line 173 "src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 187 "src/uri.rl"
	{ uri->service_len = p - uri->service;
			   uri->host = NULL; uri->host_len = 0; }
	break;
	case 154: 
	case 155: 
	case 156: 
	case 184: 
	case 185: 
	case 186: 
	case 213: 
	case 214: 
	case 215: 
	case 234: 
	case 235: 
	case 236: 
#line 108 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;
			   uri->host_hint = 1; }
#line 101 "src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 169 "src/uri.rl"
	{ s = p; }
#line 173 "src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
	break;
	case 141: 
	case 143: 
	case 196: 
	case 200: 
	case 204: 
#line 138 "src/uri.rl"
	{ s = p; }
#line 139 "src/uri.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 169 "src/uri.rl"
	{ s = p; }
#line 173 "src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
	break;
#line 6276 "src/uri.c"
	}
	}

	_out: {}
	}

#line 200 "src/uri.rl"


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

int
uri_format(char *str, int len, const struct uri *uri, bool write_password)
{
	int total = 0;
	if (uri->scheme_len > 0) {
		SNPRINT(total, snprintf, str, len, "%.*s://",
			 (int)uri->scheme_len, uri->scheme);
	}
	if (uri->host_len > 0) {
		if (uri->login_len > 0) {
			SNPRINT(total, snprintf, str, len, "%.*s",
				(int)uri->login_len, uri->login);
			if (uri->password_len > 0 && write_password) {
				SNPRINT(total, snprintf, str, len, ":%.*s",
				        (int)uri->password_len,
					uri->password);
			}
			SNPRINT(total, snprintf, str, len, "@");
		}
		SNPRINT(total, snprintf, str, len, "%.*s",
			 (int)uri->host_len, uri->host);
		if (uri->service_len > 0) {
			SNPRINT(total, snprintf, str, len, ":%.*s",
				(int)uri->service_len, uri->service);
		}
	}
	if (uri->path_len > 0) {
		SNPRINT(total, snprintf, str, len, "%.*s",
			(int)uri->path_len, uri->path);
	}
	if (uri->query_len > 0) {
		SNPRINT(total, snprintf, str, len, "?%.*s",
			(int)uri->query_len, uri->query);
	}
	if (uri->fragment_len > 0) {
		SNPRINT(total, snprintf, str, len, "#%.*s",
			(int)uri->fragment_len, uri->fragment);
	}
	return total;
}

/* vim: set ft=ragel: */
