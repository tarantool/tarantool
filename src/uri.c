
#line 1 "../../src/uri.rl"
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
#include "uri.h"
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

	
#line 50 "../../src/uri.c"
static const int uri_start = 144;
static const int uri_first_final = 144;
static const int uri_error = 0;

static const int uri_en_main = 144;


#line 58 "../../src/uri.c"
	{
	cs = uri_start;
	}

#line 63 "../../src/uri.c"
	{
	if ( p == pe )
		goto _test_eof;
	switch ( cs )
	{
case 144:
	switch( (*p) ) {
		case 33: goto tr150;
		case 35: goto tr151;
		case 37: goto tr152;
		case 47: goto tr153;
		case 59: goto tr150;
		case 61: goto tr150;
		case 63: goto tr155;
		case 64: goto st204;
		case 91: goto st38;
		case 95: goto tr150;
		case 117: goto tr158;
		case 126: goto tr150;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto tr150;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto tr157;
		} else if ( (*p) >= 65 )
			goto tr157;
	} else
		goto tr154;
	goto st0;
st0:
cs = 0;
	goto _out;
tr150:
#line 137 "../../src/uri.rl"
	{ s = p; }
#line 92 "../../src/uri.rl"
	{ s = p; }
	goto st145;
st145:
	if ( ++p == pe )
		goto _test_eof145;
case 145:
#line 109 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st145;
		case 35: goto tr159;
		case 37: goto st6;
		case 47: goto tr161;
		case 58: goto tr162;
		case 61: goto st145;
		case 63: goto tr163;
		case 64: goto tr164;
		case 95: goto st145;
		case 126: goto st145;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st145;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st145;
	} else
		goto st145;
	goto st0;
tr151:
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 185 "../../src/uri.rl"
	{ s = p; }
	goto st146;
tr159:
#line 93 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 161 "../../src/uri.rl"
	{ s = p; }
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 185 "../../src/uri.rl"
	{ s = p; }
	goto st146;
tr170:
#line 68 "../../src/uri.rl"
	{ s = p; }
#line 69 "../../src/uri.rl"
	{ uri->query = s; uri->query_len = p - s; }
#line 185 "../../src/uri.rl"
	{ s = p; }
	goto st146;
tr172:
#line 69 "../../src/uri.rl"
	{ uri->query = s; uri->query_len = p - s; }
#line 185 "../../src/uri.rl"
	{ s = p; }
	goto st146;
tr175:
#line 131 "../../src/uri.rl"
	{ s = p; }
#line 132 "../../src/uri.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 161 "../../src/uri.rl"
	{ s = p; }
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 185 "../../src/uri.rl"
	{ s = p; }
	goto st146;
tr185:
#line 132 "../../src/uri.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 161 "../../src/uri.rl"
	{ s = p; }
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 185 "../../src/uri.rl"
	{ s = p; }
	goto st146;
tr200:
#line 100 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;
			   uri->host_hint = 1; }
#line 93 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 161 "../../src/uri.rl"
	{ s = p; }
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 185 "../../src/uri.rl"
	{ s = p; }
	goto st146;
tr209:
#line 161 "../../src/uri.rl"
	{ s = p; }
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 185 "../../src/uri.rl"
	{ s = p; }
	goto st146;
tr314:
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 128 "../../src/uri.rl"
	{ s = p;}
#line 111 "../../src/uri.rl"
	{
			/*
			 * This action is also called for path_* terminals.
			 * I absolute have no idea why. Please don't blame
			 * and fix grammar if you have a LOT of free time.
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
#line 185 "../../src/uri.rl"
	{ s = p; }
	goto st146;
tr318:
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 111 "../../src/uri.rl"
	{
			/*
			 * This action is also called for path_* terminals.
			 * I absolute have no idea why. Please don't blame
			 * and fix grammar if you have a LOT of free time.
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
#line 185 "../../src/uri.rl"
	{ s = p; }
	goto st146;
tr323:
#line 161 "../../src/uri.rl"
	{ s = p; }
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 111 "../../src/uri.rl"
	{
			/*
			 * This action is also called for path_* terminals.
			 * I absolute have no idea why. Please don't blame
			 * and fix grammar if you have a LOT of free time.
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
#line 185 "../../src/uri.rl"
	{ s = p; }
	goto st146;
st146:
	if ( ++p == pe )
		goto _test_eof146;
case 146:
#line 281 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto tr165;
		case 37: goto tr166;
		case 61: goto tr165;
		case 95: goto tr165;
		case 126: goto tr165;
	}
	if ( (*p) < 63 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto tr165;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr165;
	} else
		goto tr165;
	goto st0;
tr165:
#line 72 "../../src/uri.rl"
	{ s = p; }
	goto st147;
st147:
	if ( ++p == pe )
		goto _test_eof147;
case 147:
#line 306 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st147;
		case 37: goto st1;
		case 61: goto st147;
		case 95: goto st147;
		case 126: goto st147;
	}
	if ( (*p) < 63 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st147;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st147;
	} else
		goto st147;
	goto st0;
tr166:
#line 72 "../../src/uri.rl"
	{ s = p; }
	goto st1;
st1:
	if ( ++p == pe )
		goto _test_eof1;
case 1:
#line 331 "../../src/uri.c"
	switch( (*p) ) {
		case 37: goto st147;
		case 117: goto st2;
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
			goto st147;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st147;
	} else
		goto st147;
	goto st0;
tr152:
#line 137 "../../src/uri.rl"
	{ s = p; }
#line 92 "../../src/uri.rl"
	{ s = p; }
	goto st6;
st6:
	if ( ++p == pe )
		goto _test_eof6;
case 6:
#line 407 "../../src/uri.c"
	switch( (*p) ) {
		case 37: goto st145;
		case 117: goto st7;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st145;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st145;
	} else
		goto st145;
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
			goto st145;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st145;
	} else
		goto st145;
	goto st0;
tr161:
#line 93 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 161 "../../src/uri.rl"
	{ s = p; }
	goto st148;
tr177:
#line 131 "../../src/uri.rl"
	{ s = p; }
#line 132 "../../src/uri.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 161 "../../src/uri.rl"
	{ s = p; }
	goto st148;
tr186:
#line 132 "../../src/uri.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 161 "../../src/uri.rl"
	{ s = p; }
	goto st148;
tr201:
#line 100 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;
			   uri->host_hint = 1; }
#line 93 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 161 "../../src/uri.rl"
	{ s = p; }
	goto st148;
tr210:
#line 161 "../../src/uri.rl"
	{ s = p; }
	goto st148;
st148:
	if ( ++p == pe )
		goto _test_eof148;
case 148:
#line 510 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st148;
		case 35: goto tr151;
		case 37: goto st11;
		case 61: goto st148;
		case 63: goto tr155;
		case 95: goto st148;
		case 126: goto st148;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st148;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st148;
	} else
		goto st148;
	goto st0;
st11:
	if ( ++p == pe )
		goto _test_eof11;
case 11:
	switch( (*p) ) {
		case 37: goto st148;
		case 117: goto st12;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st148;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st148;
	} else
		goto st148;
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
			goto st148;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st148;
	} else
		goto st148;
	goto st0;
tr155:
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 185 "../../src/uri.rl"
	{ s = p; }
	goto st149;
tr163:
#line 93 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 161 "../../src/uri.rl"
	{ s = p; }
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 185 "../../src/uri.rl"
	{ s = p; }
	goto st149;
tr179:
#line 131 "../../src/uri.rl"
	{ s = p; }
#line 132 "../../src/uri.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 161 "../../src/uri.rl"
	{ s = p; }
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 185 "../../src/uri.rl"
	{ s = p; }
	goto st149;
tr188:
#line 132 "../../src/uri.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 161 "../../src/uri.rl"
	{ s = p; }
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 185 "../../src/uri.rl"
	{ s = p; }
	goto st149;
tr204:
#line 100 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;
			   uri->host_hint = 1; }
#line 93 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 161 "../../src/uri.rl"
	{ s = p; }
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 185 "../../src/uri.rl"
	{ s = p; }
	goto st149;
tr212:
#line 161 "../../src/uri.rl"
	{ s = p; }
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 185 "../../src/uri.rl"
	{ s = p; }
	goto st149;
tr317:
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 128 "../../src/uri.rl"
	{ s = p;}
#line 111 "../../src/uri.rl"
	{
			/*
			 * This action is also called for path_* terminals.
			 * I absolute have no idea why. Please don't blame
			 * and fix grammar if you have a LOT of free time.
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
#line 185 "../../src/uri.rl"
	{ s = p; }
	goto st149;
tr320:
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 111 "../../src/uri.rl"
	{
			/*
			 * This action is also called for path_* terminals.
			 * I absolute have no idea why. Please don't blame
			 * and fix grammar if you have a LOT of free time.
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
#line 185 "../../src/uri.rl"
	{ s = p; }
	goto st149;
tr325:
#line 161 "../../src/uri.rl"
	{ s = p; }
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 111 "../../src/uri.rl"
	{
			/*
			 * This action is also called for path_* terminals.
			 * I absolute have no idea why. Please don't blame
			 * and fix grammar if you have a LOT of free time.
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
#line 185 "../../src/uri.rl"
	{ s = p; }
	goto st149;
st149:
	if ( ++p == pe )
		goto _test_eof149;
case 149:
#line 734 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto tr169;
		case 35: goto tr170;
		case 37: goto tr171;
		case 61: goto tr169;
		case 95: goto tr169;
		case 126: goto tr169;
	}
	if ( (*p) < 63 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto tr169;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr169;
	} else
		goto tr169;
	goto st0;
tr169:
#line 68 "../../src/uri.rl"
	{ s = p; }
	goto st150;
st150:
	if ( ++p == pe )
		goto _test_eof150;
case 150:
#line 760 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st150;
		case 35: goto tr172;
		case 37: goto st16;
		case 61: goto st150;
		case 95: goto st150;
		case 126: goto st150;
	}
	if ( (*p) < 63 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st150;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st150;
	} else
		goto st150;
	goto st0;
tr171:
#line 68 "../../src/uri.rl"
	{ s = p; }
	goto st16;
st16:
	if ( ++p == pe )
		goto _test_eof16;
case 16:
#line 786 "../../src/uri.c"
	switch( (*p) ) {
		case 37: goto st150;
		case 117: goto st17;
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
			goto st150;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st150;
	} else
		goto st150;
	goto st0;
tr162:
#line 138 "../../src/uri.rl"
	{ login = s; login_len = p - s; }
#line 93 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st151;
tr240:
#line 138 "../../src/uri.rl"
	{ login = s; login_len = p - s; }
#line 100 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;
			   uri->host_hint = 1; }
#line 93 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st151;
st151:
	if ( ++p == pe )
		goto _test_eof151;
case 151:
#line 871 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto tr174;
		case 35: goto tr175;
		case 37: goto tr176;
		case 47: goto tr177;
		case 59: goto tr174;
		case 61: goto tr174;
		case 63: goto tr179;
		case 95: goto tr174;
		case 126: goto tr174;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto tr174;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto tr180;
		} else if ( (*p) >= 65 )
			goto tr180;
	} else
		goto tr178;
	goto st0;
tr174:
#line 141 "../../src/uri.rl"
	{ s = p; }
	goto st21;
st21:
	if ( ++p == pe )
		goto _test_eof21;
case 21:
#line 903 "../../src/uri.c"
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
tr176:
#line 141 "../../src/uri.rl"
	{ s = p; }
	goto st22;
st22:
	if ( ++p == pe )
		goto _test_eof22;
case 22:
#line 933 "../../src/uri.c"
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
#line 142 "../../src/uri.rl"
	{ uri->password = s; uri->password_len = p - s; }
#line 146 "../../src/uri.rl"
	{ uri->login = login; uri->login_len = login_len; }
	goto st27;
tr164:
#line 138 "../../src/uri.rl"
	{ login = s; login_len = p - s; }
#line 146 "../../src/uri.rl"
	{ uri->login = login; uri->login_len = login_len; }
	goto st27;
st27:
	if ( ++p == pe )
		goto _test_eof27;
case 27:
#line 1015 "../../src/uri.c"
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
#line 92 "../../src/uri.rl"
	{ s = p; }
	goto st152;
st152:
	if ( ++p == pe )
		goto _test_eof152;
case 152:
#line 1047 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st152;
		case 35: goto tr159;
		case 37: goto st28;
		case 47: goto tr161;
		case 58: goto tr182;
		case 61: goto st152;
		case 63: goto tr163;
		case 95: goto st152;
		case 126: goto st152;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st152;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st152;
	} else
		goto st152;
	goto st0;
tr29:
#line 92 "../../src/uri.rl"
	{ s = p; }
	goto st28;
st28:
	if ( ++p == pe )
		goto _test_eof28;
case 28:
#line 1076 "../../src/uri.c"
	switch( (*p) ) {
		case 37: goto st152;
		case 117: goto st29;
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
			goto st152;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st152;
	} else
		goto st152;
	goto st0;
tr182:
#line 93 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st153;
tr203:
#line 100 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;
			   uri->host_hint = 1; }
#line 93 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st153;
st153:
	if ( ++p == pe )
		goto _test_eof153;
case 153:
#line 1157 "../../src/uri.c"
	switch( (*p) ) {
		case 35: goto tr175;
		case 47: goto tr177;
		case 63: goto tr179;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr183;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr184;
	} else
		goto tr184;
	goto st0;
tr183:
#line 131 "../../src/uri.rl"
	{ s = p; }
	goto st154;
st154:
	if ( ++p == pe )
		goto _test_eof154;
case 154:
#line 1180 "../../src/uri.c"
	switch( (*p) ) {
		case 35: goto tr185;
		case 47: goto tr186;
		case 63: goto tr188;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st154;
	goto st0;
tr184:
#line 131 "../../src/uri.rl"
	{ s = p; }
	goto st155;
st155:
	if ( ++p == pe )
		goto _test_eof155;
case 155:
#line 1197 "../../src/uri.c"
	switch( (*p) ) {
		case 35: goto tr185;
		case 47: goto tr186;
		case 63: goto tr188;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st155;
	} else if ( (*p) >= 65 )
		goto st155;
	goto st0;
tr30:
#line 182 "../../src/uri.rl"
	{ s = p; }
	goto st156;
st156:
	if ( ++p == pe )
		goto _test_eof156;
case 156:
#line 1217 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st157;
		case 37: goto st33;
		case 61: goto st157;
		case 95: goto st157;
		case 126: goto st157;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st157;
	} else if ( (*p) > 59 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st157;
		} else if ( (*p) >= 64 )
			goto st157;
	} else
		goto st157;
	goto st0;
st157:
	if ( ++p == pe )
		goto _test_eof157;
case 157:
	switch( (*p) ) {
		case 33: goto st157;
		case 37: goto st33;
		case 61: goto st157;
		case 95: goto st157;
		case 126: goto st157;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st157;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st157;
	} else
		goto st157;
	goto st0;
st33:
	if ( ++p == pe )
		goto _test_eof33;
case 33:
	switch( (*p) ) {
		case 37: goto st157;
		case 117: goto st34;
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
			goto st157;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st157;
	} else
		goto st157;
	goto st0;
tr31:
#line 99 "../../src/uri.rl"
	{ s = p; }
#line 92 "../../src/uri.rl"
	{ s = p; }
	goto st158;
st158:
	if ( ++p == pe )
		goto _test_eof158;
case 158:
#line 1336 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st152;
		case 35: goto tr159;
		case 37: goto st28;
		case 46: goto st159;
		case 47: goto tr161;
		case 58: goto tr182;
		case 59: goto st152;
		case 61: goto st152;
		case 63: goto tr163;
		case 95: goto st152;
		case 126: goto st152;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st152;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st152;
		} else if ( (*p) >= 65 )
			goto st152;
	} else
		goto st171;
	goto st0;
st159:
	if ( ++p == pe )
		goto _test_eof159;
case 159:
	switch( (*p) ) {
		case 33: goto st152;
		case 35: goto tr159;
		case 37: goto st28;
		case 47: goto tr161;
		case 58: goto tr182;
		case 59: goto st152;
		case 61: goto st152;
		case 63: goto tr163;
		case 95: goto st152;
		case 126: goto st152;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st152;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st152;
		} else if ( (*p) >= 65 )
			goto st152;
	} else
		goto st160;
	goto st0;
st160:
	if ( ++p == pe )
		goto _test_eof160;
case 160:
	switch( (*p) ) {
		case 33: goto st152;
		case 35: goto tr159;
		case 37: goto st28;
		case 46: goto st161;
		case 47: goto tr161;
		case 58: goto tr182;
		case 59: goto st152;
		case 61: goto st152;
		case 63: goto tr163;
		case 95: goto st152;
		case 126: goto st152;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st152;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st152;
		} else if ( (*p) >= 65 )
			goto st152;
	} else
		goto st169;
	goto st0;
st161:
	if ( ++p == pe )
		goto _test_eof161;
case 161:
	switch( (*p) ) {
		case 33: goto st152;
		case 35: goto tr159;
		case 37: goto st28;
		case 47: goto tr161;
		case 58: goto tr182;
		case 59: goto st152;
		case 61: goto st152;
		case 63: goto tr163;
		case 95: goto st152;
		case 126: goto st152;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st152;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st152;
		} else if ( (*p) >= 65 )
			goto st152;
	} else
		goto st162;
	goto st0;
st162:
	if ( ++p == pe )
		goto _test_eof162;
case 162:
	switch( (*p) ) {
		case 33: goto st152;
		case 35: goto tr159;
		case 37: goto st28;
		case 46: goto st163;
		case 47: goto tr161;
		case 58: goto tr182;
		case 59: goto st152;
		case 61: goto st152;
		case 63: goto tr163;
		case 95: goto st152;
		case 126: goto st152;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st152;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st152;
		} else if ( (*p) >= 65 )
			goto st152;
	} else
		goto st167;
	goto st0;
st163:
	if ( ++p == pe )
		goto _test_eof163;
case 163:
	switch( (*p) ) {
		case 33: goto st152;
		case 35: goto tr159;
		case 37: goto st28;
		case 47: goto tr161;
		case 58: goto tr182;
		case 59: goto st152;
		case 61: goto st152;
		case 63: goto tr163;
		case 95: goto st152;
		case 126: goto st152;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st152;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st152;
		} else if ( (*p) >= 65 )
			goto st152;
	} else
		goto st164;
	goto st0;
st164:
	if ( ++p == pe )
		goto _test_eof164;
case 164:
	switch( (*p) ) {
		case 33: goto st152;
		case 35: goto tr200;
		case 37: goto st28;
		case 47: goto tr201;
		case 58: goto tr203;
		case 59: goto st152;
		case 61: goto st152;
		case 63: goto tr204;
		case 95: goto st152;
		case 126: goto st152;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st152;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st152;
		} else if ( (*p) >= 65 )
			goto st152;
	} else
		goto st165;
	goto st0;
st165:
	if ( ++p == pe )
		goto _test_eof165;
case 165:
	switch( (*p) ) {
		case 33: goto st152;
		case 35: goto tr200;
		case 37: goto st28;
		case 47: goto tr201;
		case 58: goto tr203;
		case 59: goto st152;
		case 61: goto st152;
		case 63: goto tr204;
		case 95: goto st152;
		case 126: goto st152;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st152;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st152;
		} else if ( (*p) >= 65 )
			goto st152;
	} else
		goto st166;
	goto st0;
st166:
	if ( ++p == pe )
		goto _test_eof166;
case 166:
	switch( (*p) ) {
		case 33: goto st152;
		case 35: goto tr200;
		case 37: goto st28;
		case 47: goto tr201;
		case 58: goto tr203;
		case 61: goto st152;
		case 63: goto tr204;
		case 95: goto st152;
		case 126: goto st152;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st152;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st152;
	} else
		goto st152;
	goto st0;
st167:
	if ( ++p == pe )
		goto _test_eof167;
case 167:
	switch( (*p) ) {
		case 33: goto st152;
		case 35: goto tr159;
		case 37: goto st28;
		case 46: goto st163;
		case 47: goto tr161;
		case 58: goto tr182;
		case 59: goto st152;
		case 61: goto st152;
		case 63: goto tr163;
		case 95: goto st152;
		case 126: goto st152;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st152;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st152;
		} else if ( (*p) >= 65 )
			goto st152;
	} else
		goto st168;
	goto st0;
st168:
	if ( ++p == pe )
		goto _test_eof168;
case 168:
	switch( (*p) ) {
		case 33: goto st152;
		case 35: goto tr159;
		case 37: goto st28;
		case 46: goto st163;
		case 47: goto tr161;
		case 58: goto tr182;
		case 61: goto st152;
		case 63: goto tr163;
		case 95: goto st152;
		case 126: goto st152;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st152;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st152;
	} else
		goto st152;
	goto st0;
st169:
	if ( ++p == pe )
		goto _test_eof169;
case 169:
	switch( (*p) ) {
		case 33: goto st152;
		case 35: goto tr159;
		case 37: goto st28;
		case 46: goto st161;
		case 47: goto tr161;
		case 58: goto tr182;
		case 59: goto st152;
		case 61: goto st152;
		case 63: goto tr163;
		case 95: goto st152;
		case 126: goto st152;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st152;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st152;
		} else if ( (*p) >= 65 )
			goto st152;
	} else
		goto st170;
	goto st0;
st170:
	if ( ++p == pe )
		goto _test_eof170;
case 170:
	switch( (*p) ) {
		case 33: goto st152;
		case 35: goto tr159;
		case 37: goto st28;
		case 46: goto st161;
		case 47: goto tr161;
		case 58: goto tr182;
		case 61: goto st152;
		case 63: goto tr163;
		case 95: goto st152;
		case 126: goto st152;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st152;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st152;
	} else
		goto st152;
	goto st0;
st171:
	if ( ++p == pe )
		goto _test_eof171;
case 171:
	switch( (*p) ) {
		case 33: goto st152;
		case 35: goto tr159;
		case 37: goto st28;
		case 46: goto st159;
		case 47: goto tr161;
		case 58: goto tr182;
		case 59: goto st152;
		case 61: goto st152;
		case 63: goto tr163;
		case 95: goto st152;
		case 126: goto st152;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st152;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st152;
		} else if ( (*p) >= 65 )
			goto st152;
	} else
		goto st172;
	goto st0;
st172:
	if ( ++p == pe )
		goto _test_eof172;
case 172:
	switch( (*p) ) {
		case 33: goto st152;
		case 35: goto tr159;
		case 37: goto st28;
		case 46: goto st159;
		case 47: goto tr161;
		case 58: goto tr182;
		case 61: goto st152;
		case 63: goto tr163;
		case 95: goto st152;
		case 126: goto st152;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st152;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st152;
	} else
		goto st152;
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
#line 106 "../../src/uri.rl"
	{ s = p; }
	goto st39;
st39:
	if ( ++p == pe )
		goto _test_eof39;
case 39:
#line 1766 "../../src/uri.c"
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
#line 107 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;
				   uri->host_hint = 2; }
	goto st173;
st173:
	if ( ++p == pe )
		goto _test_eof173;
case 173:
#line 2325 "../../src/uri.c"
	switch( (*p) ) {
		case 35: goto tr209;
		case 47: goto tr210;
		case 58: goto st153;
		case 63: goto tr212;
	}
	goto st0;
tr45:
#line 106 "../../src/uri.rl"
	{ s = p; }
	goto st83;
st83:
	if ( ++p == pe )
		goto _test_eof83;
case 83:
#line 2341 "../../src/uri.c"
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
#line 92 "../../src/uri.rl"
	{ s = p; }
	goto st174;
st174:
	if ( ++p == pe )
		goto _test_eof174;
case 174:
#line 2577 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st152;
		case 35: goto tr159;
		case 37: goto st28;
		case 47: goto tr161;
		case 58: goto tr182;
		case 61: goto st152;
		case 63: goto tr163;
		case 95: goto st152;
		case 110: goto st175;
		case 126: goto st152;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st152;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st152;
	} else
		goto st152;
	goto st0;
st175:
	if ( ++p == pe )
		goto _test_eof175;
case 175:
	switch( (*p) ) {
		case 33: goto st152;
		case 35: goto tr159;
		case 37: goto st28;
		case 47: goto tr161;
		case 58: goto tr182;
		case 61: goto st152;
		case 63: goto tr163;
		case 95: goto st152;
		case 105: goto st176;
		case 126: goto st152;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st152;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st152;
	} else
		goto st152;
	goto st0;
st176:
	if ( ++p == pe )
		goto _test_eof176;
case 176:
	switch( (*p) ) {
		case 33: goto st152;
		case 35: goto tr159;
		case 37: goto st28;
		case 47: goto tr161;
		case 58: goto tr182;
		case 61: goto st152;
		case 63: goto tr163;
		case 95: goto st152;
		case 120: goto st177;
		case 126: goto st152;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st152;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st152;
	} else
		goto st152;
	goto st0;
st177:
	if ( ++p == pe )
		goto _test_eof177;
case 177:
	switch( (*p) ) {
		case 33: goto st152;
		case 35: goto tr159;
		case 37: goto st28;
		case 47: goto tr216;
		case 58: goto tr182;
		case 61: goto st152;
		case 63: goto tr163;
		case 95: goto st152;
		case 126: goto st152;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st152;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st152;
	} else
		goto st152;
	goto st0;
tr216:
#line 93 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 161 "../../src/uri.rl"
	{ s = p; }
	goto st178;
st178:
	if ( ++p == pe )
		goto _test_eof178;
case 178:
#line 2683 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st148;
		case 35: goto tr151;
		case 37: goto st11;
		case 58: goto st179;
		case 61: goto st148;
		case 63: goto tr155;
		case 95: goto st148;
		case 126: goto st148;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st148;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st148;
	} else
		goto st148;
	goto st0;
st179:
	if ( ++p == pe )
		goto _test_eof179;
case 179:
	switch( (*p) ) {
		case 33: goto tr218;
		case 35: goto tr151;
		case 37: goto tr219;
		case 47: goto tr220;
		case 58: goto tr221;
		case 61: goto tr218;
		case 63: goto tr155;
		case 95: goto tr218;
		case 126: goto tr218;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto tr218;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr218;
	} else
		goto tr218;
	goto st0;
tr218:
#line 128 "../../src/uri.rl"
	{ s = p;}
	goto st180;
st180:
	if ( ++p == pe )
		goto _test_eof180;
case 180:
#line 2735 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st180;
		case 35: goto tr151;
		case 37: goto st104;
		case 47: goto st181;
		case 58: goto tr223;
		case 61: goto st180;
		case 63: goto tr155;
		case 95: goto st180;
		case 126: goto st180;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st180;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st180;
	} else
		goto st180;
	goto st0;
tr219:
#line 128 "../../src/uri.rl"
	{ s = p;}
	goto st104;
st104:
	if ( ++p == pe )
		goto _test_eof104;
case 104:
#line 2764 "../../src/uri.c"
	switch( (*p) ) {
		case 37: goto st180;
		case 117: goto st105;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st180;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st180;
	} else
		goto st180;
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
			goto st180;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st180;
	} else
		goto st180;
	goto st0;
tr226:
#line 161 "../../src/uri.rl"
	{ s = p; }
	goto st181;
tr220:
#line 128 "../../src/uri.rl"
	{ s = p;}
	goto st181;
st181:
	if ( ++p == pe )
		goto _test_eof181;
case 181:
#line 2842 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st181;
		case 35: goto tr151;
		case 37: goto st109;
		case 58: goto tr225;
		case 61: goto st181;
		case 63: goto tr155;
		case 95: goto st181;
		case 126: goto st181;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st181;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st181;
	} else
		goto st181;
	goto st0;
st109:
	if ( ++p == pe )
		goto _test_eof109;
case 109:
	switch( (*p) ) {
		case 37: goto st181;
		case 117: goto st110;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st181;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st181;
	} else
		goto st181;
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
			goto st181;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st181;
	} else
		goto st181;
	goto st0;
tr225:
#line 111 "../../src/uri.rl"
	{
			/*
			 * This action is also called for path_* terminals.
			 * I absolute have no idea why. Please don't blame
			 * and fix grammar if you have a LOT of free time.
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
	goto st182;
st182:
	if ( ++p == pe )
		goto _test_eof182;
case 182:
#line 2954 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st181;
		case 35: goto tr209;
		case 37: goto st109;
		case 47: goto tr226;
		case 58: goto tr225;
		case 61: goto st181;
		case 63: goto tr212;
		case 95: goto st181;
		case 126: goto st181;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st181;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st181;
	} else
		goto st181;
	goto st0;
tr223:
#line 111 "../../src/uri.rl"
	{
			/*
			 * This action is also called for path_* terminals.
			 * I absolute have no idea why. Please don't blame
			 * and fix grammar if you have a LOT of free time.
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
	goto st183;
tr221:
#line 128 "../../src/uri.rl"
	{ s = p;}
#line 111 "../../src/uri.rl"
	{
			/*
			 * This action is also called for path_* terminals.
			 * I absolute have no idea why. Please don't blame
			 * and fix grammar if you have a LOT of free time.
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
	goto st183;
st183:
	if ( ++p == pe )
		goto _test_eof183;
case 183:
#line 3019 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st148;
		case 35: goto tr209;
		case 37: goto st11;
		case 47: goto tr210;
		case 61: goto st148;
		case 63: goto tr212;
		case 95: goto st148;
		case 126: goto st148;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st148;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st148;
	} else
		goto st148;
	goto st0;
tr178:
#line 141 "../../src/uri.rl"
	{ s = p; }
#line 131 "../../src/uri.rl"
	{ s = p; }
	goto st184;
st184:
	if ( ++p == pe )
		goto _test_eof184;
case 184:
#line 3049 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st21;
		case 35: goto tr185;
		case 37: goto st22;
		case 47: goto tr186;
		case 59: goto st21;
		case 61: goto st21;
		case 63: goto tr188;
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
		goto st184;
	goto st0;
tr180:
#line 141 "../../src/uri.rl"
	{ s = p; }
#line 131 "../../src/uri.rl"
	{ s = p; }
	goto st185;
st185:
	if ( ++p == pe )
		goto _test_eof185;
case 185:
#line 3084 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st21;
		case 35: goto tr185;
		case 37: goto st22;
		case 47: goto tr186;
		case 59: goto st21;
		case 61: goto st21;
		case 63: goto tr188;
		case 64: goto tr23;
		case 95: goto st21;
		case 126: goto st21;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 57 )
			goto st21;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st185;
	} else
		goto st185;
	goto st0;
tr153:
#line 182 "../../src/uri.rl"
	{ s = p; }
	goto st186;
st186:
	if ( ++p == pe )
		goto _test_eof186;
case 186:
#line 3114 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st187;
		case 35: goto tr151;
		case 37: goto st114;
		case 61: goto st187;
		case 63: goto tr155;
		case 95: goto st187;
		case 126: goto st187;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st187;
	} else if ( (*p) > 59 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st187;
		} else if ( (*p) >= 64 )
			goto st187;
	} else
		goto st187;
	goto st0;
st187:
	if ( ++p == pe )
		goto _test_eof187;
case 187:
	switch( (*p) ) {
		case 33: goto st187;
		case 35: goto tr151;
		case 37: goto st114;
		case 61: goto st187;
		case 63: goto tr155;
		case 95: goto st187;
		case 126: goto st187;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st187;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st187;
	} else
		goto st187;
	goto st0;
st114:
	if ( ++p == pe )
		goto _test_eof114;
case 114:
	switch( (*p) ) {
		case 37: goto st187;
		case 117: goto st115;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st187;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st187;
	} else
		goto st187;
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
			goto st187;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st187;
	} else
		goto st187;
	goto st0;
tr154:
#line 137 "../../src/uri.rl"
	{ s = p; }
#line 99 "../../src/uri.rl"
	{ s = p; }
#line 92 "../../src/uri.rl"
	{ s = p; }
#line 178 "../../src/uri.rl"
	{ uri->service = p; }
	goto st188;
st188:
	if ( ++p == pe )
		goto _test_eof188;
case 188:
#line 3241 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st145;
		case 35: goto tr159;
		case 37: goto st6;
		case 46: goto st189;
		case 47: goto tr161;
		case 58: goto tr162;
		case 59: goto st145;
		case 61: goto st145;
		case 63: goto tr163;
		case 64: goto tr164;
		case 95: goto st145;
		case 126: goto st145;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st145;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st145;
		} else if ( (*p) >= 65 )
			goto st145;
	} else
		goto st201;
	goto st0;
st189:
	if ( ++p == pe )
		goto _test_eof189;
case 189:
	switch( (*p) ) {
		case 33: goto st145;
		case 35: goto tr159;
		case 37: goto st6;
		case 47: goto tr161;
		case 58: goto tr162;
		case 59: goto st145;
		case 61: goto st145;
		case 63: goto tr163;
		case 64: goto tr164;
		case 95: goto st145;
		case 126: goto st145;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st145;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st145;
		} else if ( (*p) >= 65 )
			goto st145;
	} else
		goto st190;
	goto st0;
st190:
	if ( ++p == pe )
		goto _test_eof190;
case 190:
	switch( (*p) ) {
		case 33: goto st145;
		case 35: goto tr159;
		case 37: goto st6;
		case 46: goto st191;
		case 47: goto tr161;
		case 58: goto tr162;
		case 59: goto st145;
		case 61: goto st145;
		case 63: goto tr163;
		case 64: goto tr164;
		case 95: goto st145;
		case 126: goto st145;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st145;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st145;
		} else if ( (*p) >= 65 )
			goto st145;
	} else
		goto st199;
	goto st0;
st191:
	if ( ++p == pe )
		goto _test_eof191;
case 191:
	switch( (*p) ) {
		case 33: goto st145;
		case 35: goto tr159;
		case 37: goto st6;
		case 47: goto tr161;
		case 58: goto tr162;
		case 59: goto st145;
		case 61: goto st145;
		case 63: goto tr163;
		case 64: goto tr164;
		case 95: goto st145;
		case 126: goto st145;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st145;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st145;
		} else if ( (*p) >= 65 )
			goto st145;
	} else
		goto st192;
	goto st0;
st192:
	if ( ++p == pe )
		goto _test_eof192;
case 192:
	switch( (*p) ) {
		case 33: goto st145;
		case 35: goto tr159;
		case 37: goto st6;
		case 46: goto st193;
		case 47: goto tr161;
		case 58: goto tr162;
		case 59: goto st145;
		case 61: goto st145;
		case 63: goto tr163;
		case 64: goto tr164;
		case 95: goto st145;
		case 126: goto st145;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st145;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st145;
		} else if ( (*p) >= 65 )
			goto st145;
	} else
		goto st197;
	goto st0;
st193:
	if ( ++p == pe )
		goto _test_eof193;
case 193:
	switch( (*p) ) {
		case 33: goto st145;
		case 35: goto tr159;
		case 37: goto st6;
		case 47: goto tr161;
		case 58: goto tr162;
		case 59: goto st145;
		case 61: goto st145;
		case 63: goto tr163;
		case 64: goto tr164;
		case 95: goto st145;
		case 126: goto st145;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st145;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st145;
		} else if ( (*p) >= 65 )
			goto st145;
	} else
		goto st194;
	goto st0;
st194:
	if ( ++p == pe )
		goto _test_eof194;
case 194:
	switch( (*p) ) {
		case 33: goto st145;
		case 35: goto tr200;
		case 37: goto st6;
		case 47: goto tr201;
		case 58: goto tr240;
		case 59: goto st145;
		case 61: goto st145;
		case 63: goto tr204;
		case 64: goto tr164;
		case 95: goto st145;
		case 126: goto st145;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st145;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st145;
		} else if ( (*p) >= 65 )
			goto st145;
	} else
		goto st195;
	goto st0;
st195:
	if ( ++p == pe )
		goto _test_eof195;
case 195:
	switch( (*p) ) {
		case 33: goto st145;
		case 35: goto tr200;
		case 37: goto st6;
		case 47: goto tr201;
		case 58: goto tr240;
		case 59: goto st145;
		case 61: goto st145;
		case 63: goto tr204;
		case 64: goto tr164;
		case 95: goto st145;
		case 126: goto st145;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st145;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st145;
		} else if ( (*p) >= 65 )
			goto st145;
	} else
		goto st196;
	goto st0;
st196:
	if ( ++p == pe )
		goto _test_eof196;
case 196:
	switch( (*p) ) {
		case 33: goto st145;
		case 35: goto tr200;
		case 37: goto st6;
		case 47: goto tr201;
		case 58: goto tr240;
		case 61: goto st145;
		case 63: goto tr204;
		case 64: goto tr164;
		case 95: goto st145;
		case 126: goto st145;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st145;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st145;
	} else
		goto st145;
	goto st0;
st197:
	if ( ++p == pe )
		goto _test_eof197;
case 197:
	switch( (*p) ) {
		case 33: goto st145;
		case 35: goto tr159;
		case 37: goto st6;
		case 46: goto st193;
		case 47: goto tr161;
		case 58: goto tr162;
		case 59: goto st145;
		case 61: goto st145;
		case 63: goto tr163;
		case 64: goto tr164;
		case 95: goto st145;
		case 126: goto st145;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st145;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st145;
		} else if ( (*p) >= 65 )
			goto st145;
	} else
		goto st198;
	goto st0;
st198:
	if ( ++p == pe )
		goto _test_eof198;
case 198:
	switch( (*p) ) {
		case 33: goto st145;
		case 35: goto tr159;
		case 37: goto st6;
		case 46: goto st193;
		case 47: goto tr161;
		case 58: goto tr162;
		case 61: goto st145;
		case 63: goto tr163;
		case 64: goto tr164;
		case 95: goto st145;
		case 126: goto st145;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st145;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st145;
	} else
		goto st145;
	goto st0;
st199:
	if ( ++p == pe )
		goto _test_eof199;
case 199:
	switch( (*p) ) {
		case 33: goto st145;
		case 35: goto tr159;
		case 37: goto st6;
		case 46: goto st191;
		case 47: goto tr161;
		case 58: goto tr162;
		case 59: goto st145;
		case 61: goto st145;
		case 63: goto tr163;
		case 64: goto tr164;
		case 95: goto st145;
		case 126: goto st145;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st145;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st145;
		} else if ( (*p) >= 65 )
			goto st145;
	} else
		goto st200;
	goto st0;
st200:
	if ( ++p == pe )
		goto _test_eof200;
case 200:
	switch( (*p) ) {
		case 33: goto st145;
		case 35: goto tr159;
		case 37: goto st6;
		case 46: goto st191;
		case 47: goto tr161;
		case 58: goto tr162;
		case 61: goto st145;
		case 63: goto tr163;
		case 64: goto tr164;
		case 95: goto st145;
		case 126: goto st145;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st145;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st145;
	} else
		goto st145;
	goto st0;
st201:
	if ( ++p == pe )
		goto _test_eof201;
case 201:
	switch( (*p) ) {
		case 33: goto st145;
		case 35: goto tr159;
		case 37: goto st6;
		case 46: goto st189;
		case 47: goto tr161;
		case 58: goto tr162;
		case 59: goto st145;
		case 61: goto st145;
		case 63: goto tr163;
		case 64: goto tr164;
		case 95: goto st145;
		case 126: goto st145;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st145;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st145;
		} else if ( (*p) >= 65 )
			goto st145;
	} else
		goto st202;
	goto st0;
st202:
	if ( ++p == pe )
		goto _test_eof202;
case 202:
	switch( (*p) ) {
		case 33: goto st145;
		case 35: goto tr159;
		case 37: goto st6;
		case 46: goto st189;
		case 47: goto tr161;
		case 58: goto tr162;
		case 59: goto st145;
		case 61: goto st145;
		case 63: goto tr163;
		case 64: goto tr164;
		case 95: goto st145;
		case 126: goto st145;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st145;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st145;
		} else if ( (*p) >= 65 )
			goto st145;
	} else
		goto st203;
	goto st0;
st203:
	if ( ++p == pe )
		goto _test_eof203;
case 203:
	switch( (*p) ) {
		case 33: goto st145;
		case 35: goto tr159;
		case 37: goto st6;
		case 47: goto tr161;
		case 58: goto tr162;
		case 59: goto st145;
		case 61: goto st145;
		case 63: goto tr163;
		case 64: goto tr164;
		case 95: goto st145;
		case 126: goto st145;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st145;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st145;
		} else if ( (*p) >= 65 )
			goto st145;
	} else
		goto st203;
	goto st0;
st204:
	if ( ++p == pe )
		goto _test_eof204;
case 204:
	switch( (*p) ) {
		case 35: goto tr151;
		case 47: goto st148;
		case 63: goto tr155;
	}
	goto st0;
tr157:
#line 151 "../../src/uri.rl"
	{ s = p; }
#line 137 "../../src/uri.rl"
	{ s = p; }
#line 92 "../../src/uri.rl"
	{ s = p; }
	goto st205;
st205:
	if ( ++p == pe )
		goto _test_eof205;
case 205:
#line 3721 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st145;
		case 35: goto tr159;
		case 37: goto st6;
		case 43: goto st205;
		case 47: goto tr161;
		case 58: goto tr247;
		case 59: goto st145;
		case 61: goto st145;
		case 63: goto tr163;
		case 64: goto tr164;
		case 95: goto st145;
		case 126: goto st145;
	}
	if ( (*p) < 45 ) {
		if ( 36 <= (*p) && (*p) <= 44 )
			goto st145;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st205;
		} else if ( (*p) >= 65 )
			goto st205;
	} else
		goto st205;
	goto st0;
tr247:
#line 153 "../../src/uri.rl"
	{scheme = s; scheme_len = p - s; }
#line 138 "../../src/uri.rl"
	{ login = s; login_len = p - s; }
#line 93 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st206;
st206:
	if ( ++p == pe )
		goto _test_eof206;
case 206:
#line 3760 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto tr174;
		case 35: goto tr175;
		case 37: goto tr176;
		case 47: goto tr248;
		case 59: goto tr174;
		case 61: goto tr174;
		case 63: goto tr179;
		case 95: goto tr174;
		case 126: goto tr174;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto tr174;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto tr180;
		} else if ( (*p) >= 65 )
			goto tr180;
	} else
		goto tr178;
	goto st0;
tr248:
#line 169 "../../src/uri.rl"
	{ uri->scheme = scheme; uri->scheme_len = scheme_len;}
#line 131 "../../src/uri.rl"
	{ s = p; }
#line 132 "../../src/uri.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 161 "../../src/uri.rl"
	{ s = p; }
	goto st207;
st207:
	if ( ++p == pe )
		goto _test_eof207;
case 207:
#line 3798 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st148;
		case 35: goto tr151;
		case 37: goto st11;
		case 47: goto st208;
		case 61: goto st148;
		case 63: goto tr155;
		case 95: goto st148;
		case 126: goto st148;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st148;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st148;
	} else
		goto st148;
	goto st0;
st208:
	if ( ++p == pe )
		goto _test_eof208;
case 208:
	switch( (*p) ) {
		case 33: goto tr250;
		case 35: goto tr151;
		case 37: goto tr251;
		case 47: goto st148;
		case 58: goto st148;
		case 59: goto tr250;
		case 61: goto tr250;
		case 63: goto tr155;
		case 64: goto st148;
		case 91: goto st38;
		case 95: goto tr250;
		case 117: goto tr253;
		case 126: goto tr250;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto tr250;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto tr250;
		} else if ( (*p) >= 65 )
			goto tr250;
	} else
		goto tr252;
	goto st0;
tr250:
#line 137 "../../src/uri.rl"
	{ s = p; }
#line 92 "../../src/uri.rl"
	{ s = p; }
	goto st209;
st209:
	if ( ++p == pe )
		goto _test_eof209;
case 209:
#line 3859 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st209;
		case 35: goto tr159;
		case 37: goto st119;
		case 47: goto tr161;
		case 58: goto tr255;
		case 61: goto st209;
		case 63: goto tr163;
		case 64: goto tr256;
		case 95: goto st209;
		case 126: goto st209;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st209;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st209;
	} else
		goto st209;
	goto st0;
tr251:
#line 137 "../../src/uri.rl"
	{ s = p; }
#line 92 "../../src/uri.rl"
	{ s = p; }
	goto st119;
st119:
	if ( ++p == pe )
		goto _test_eof119;
case 119:
#line 3891 "../../src/uri.c"
	switch( (*p) ) {
		case 37: goto st209;
		case 117: goto st120;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st209;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st209;
	} else
		goto st209;
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
			goto st209;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st209;
	} else
		goto st209;
	goto st0;
tr255:
#line 138 "../../src/uri.rl"
	{ login = s; login_len = p - s; }
#line 93 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st210;
tr303:
#line 138 "../../src/uri.rl"
	{ login = s; login_len = p - s; }
#line 100 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;
			   uri->host_hint = 1; }
#line 93 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st210;
st210:
	if ( ++p == pe )
		goto _test_eof210;
case 210:
#line 3976 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto tr257;
		case 35: goto tr175;
		case 37: goto tr258;
		case 47: goto tr177;
		case 58: goto st148;
		case 59: goto tr257;
		case 61: goto tr257;
		case 63: goto tr179;
		case 64: goto st148;
		case 95: goto tr257;
		case 126: goto tr257;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto tr257;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto tr260;
		} else if ( (*p) >= 65 )
			goto tr260;
	} else
		goto tr259;
	goto st0;
tr257:
#line 141 "../../src/uri.rl"
	{ s = p; }
	goto st211;
st211:
	if ( ++p == pe )
		goto _test_eof211;
case 211:
#line 4010 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st211;
		case 35: goto tr151;
		case 37: goto st124;
		case 47: goto st148;
		case 58: goto st148;
		case 61: goto st211;
		case 63: goto tr155;
		case 64: goto tr262;
		case 95: goto st211;
		case 126: goto st211;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st211;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st211;
	} else
		goto st211;
	goto st0;
tr258:
#line 141 "../../src/uri.rl"
	{ s = p; }
	goto st124;
st124:
	if ( ++p == pe )
		goto _test_eof124;
case 124:
#line 4040 "../../src/uri.c"
	switch( (*p) ) {
		case 37: goto st211;
		case 117: goto st125;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st211;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st211;
	} else
		goto st211;
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
			goto st211;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st211;
	} else
		goto st211;
	goto st0;
tr262:
#line 142 "../../src/uri.rl"
	{ uri->password = s; uri->password_len = p - s; }
#line 146 "../../src/uri.rl"
	{ uri->login = login; uri->login_len = login_len; }
	goto st212;
tr256:
#line 138 "../../src/uri.rl"
	{ login = s; login_len = p - s; }
#line 146 "../../src/uri.rl"
	{ uri->login = login; uri->login_len = login_len; }
	goto st212;
st212:
	if ( ++p == pe )
		goto _test_eof212;
case 212:
#line 4122 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto tr263;
		case 35: goto tr151;
		case 37: goto tr264;
		case 47: goto st148;
		case 58: goto st148;
		case 59: goto tr263;
		case 61: goto tr263;
		case 63: goto tr155;
		case 64: goto st148;
		case 91: goto st38;
		case 95: goto tr263;
		case 117: goto tr266;
		case 126: goto tr263;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto tr263;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto tr263;
		} else if ( (*p) >= 65 )
			goto tr263;
	} else
		goto tr265;
	goto st0;
tr263:
#line 92 "../../src/uri.rl"
	{ s = p; }
	goto st213;
st213:
	if ( ++p == pe )
		goto _test_eof213;
case 213:
#line 4158 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st213;
		case 35: goto tr159;
		case 37: goto st129;
		case 47: goto tr161;
		case 58: goto tr268;
		case 61: goto st213;
		case 63: goto tr163;
		case 64: goto st148;
		case 95: goto st213;
		case 126: goto st213;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st213;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st213;
	} else
		goto st213;
	goto st0;
tr264:
#line 92 "../../src/uri.rl"
	{ s = p; }
	goto st129;
st129:
	if ( ++p == pe )
		goto _test_eof129;
case 129:
#line 4188 "../../src/uri.c"
	switch( (*p) ) {
		case 37: goto st213;
		case 117: goto st130;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st213;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st213;
	} else
		goto st213;
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
			goto st213;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st213;
	} else
		goto st213;
	goto st0;
tr268:
#line 93 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st214;
tr283:
#line 100 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;
			   uri->host_hint = 1; }
#line 93 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
	goto st214;
st214:
	if ( ++p == pe )
		goto _test_eof214;
case 214:
#line 4269 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st148;
		case 35: goto tr175;
		case 37: goto st11;
		case 47: goto tr177;
		case 61: goto st148;
		case 63: goto tr179;
		case 64: goto st148;
		case 95: goto st148;
		case 126: goto st148;
	}
	if ( (*p) < 58 ) {
		if ( (*p) > 46 ) {
			if ( 48 <= (*p) && (*p) <= 57 )
				goto tr269;
		} else if ( (*p) >= 36 )
			goto st148;
	} else if ( (*p) > 59 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto tr270;
		} else if ( (*p) >= 65 )
			goto tr270;
	} else
		goto st148;
	goto st0;
tr269:
#line 131 "../../src/uri.rl"
	{ s = p; }
	goto st215;
st215:
	if ( ++p == pe )
		goto _test_eof215;
case 215:
#line 4304 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st148;
		case 35: goto tr185;
		case 37: goto st11;
		case 47: goto tr186;
		case 61: goto st148;
		case 63: goto tr188;
		case 95: goto st148;
		case 126: goto st148;
	}
	if ( (*p) < 58 ) {
		if ( (*p) > 46 ) {
			if ( 48 <= (*p) && (*p) <= 57 )
				goto st215;
		} else if ( (*p) >= 36 )
			goto st148;
	} else if ( (*p) > 59 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st148;
		} else if ( (*p) >= 64 )
			goto st148;
	} else
		goto st148;
	goto st0;
tr270:
#line 131 "../../src/uri.rl"
	{ s = p; }
	goto st216;
st216:
	if ( ++p == pe )
		goto _test_eof216;
case 216:
#line 4338 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st148;
		case 35: goto tr185;
		case 37: goto st11;
		case 47: goto tr186;
		case 61: goto st148;
		case 63: goto tr188;
		case 64: goto st148;
		case 95: goto st148;
		case 126: goto st148;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st148;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st216;
	} else
		goto st216;
	goto st0;
tr265:
#line 99 "../../src/uri.rl"
	{ s = p; }
#line 92 "../../src/uri.rl"
	{ s = p; }
	goto st217;
st217:
	if ( ++p == pe )
		goto _test_eof217;
case 217:
#line 4369 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st213;
		case 35: goto tr159;
		case 37: goto st129;
		case 46: goto st218;
		case 47: goto tr161;
		case 58: goto tr268;
		case 59: goto st213;
		case 61: goto st213;
		case 63: goto tr163;
		case 64: goto st148;
		case 95: goto st213;
		case 126: goto st213;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st213;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st213;
		} else if ( (*p) >= 65 )
			goto st213;
	} else
		goto st230;
	goto st0;
st218:
	if ( ++p == pe )
		goto _test_eof218;
case 218:
	switch( (*p) ) {
		case 33: goto st213;
		case 35: goto tr159;
		case 37: goto st129;
		case 47: goto tr161;
		case 58: goto tr268;
		case 59: goto st213;
		case 61: goto st213;
		case 63: goto tr163;
		case 64: goto st148;
		case 95: goto st213;
		case 126: goto st213;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st213;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st213;
		} else if ( (*p) >= 65 )
			goto st213;
	} else
		goto st219;
	goto st0;
st219:
	if ( ++p == pe )
		goto _test_eof219;
case 219:
	switch( (*p) ) {
		case 33: goto st213;
		case 35: goto tr159;
		case 37: goto st129;
		case 46: goto st220;
		case 47: goto tr161;
		case 58: goto tr268;
		case 59: goto st213;
		case 61: goto st213;
		case 63: goto tr163;
		case 64: goto st148;
		case 95: goto st213;
		case 126: goto st213;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st213;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st213;
		} else if ( (*p) >= 65 )
			goto st213;
	} else
		goto st228;
	goto st0;
st220:
	if ( ++p == pe )
		goto _test_eof220;
case 220:
	switch( (*p) ) {
		case 33: goto st213;
		case 35: goto tr159;
		case 37: goto st129;
		case 47: goto tr161;
		case 58: goto tr268;
		case 59: goto st213;
		case 61: goto st213;
		case 63: goto tr163;
		case 64: goto st148;
		case 95: goto st213;
		case 126: goto st213;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st213;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st213;
		} else if ( (*p) >= 65 )
			goto st213;
	} else
		goto st221;
	goto st0;
st221:
	if ( ++p == pe )
		goto _test_eof221;
case 221:
	switch( (*p) ) {
		case 33: goto st213;
		case 35: goto tr159;
		case 37: goto st129;
		case 46: goto st222;
		case 47: goto tr161;
		case 58: goto tr268;
		case 59: goto st213;
		case 61: goto st213;
		case 63: goto tr163;
		case 64: goto st148;
		case 95: goto st213;
		case 126: goto st213;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st213;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st213;
		} else if ( (*p) >= 65 )
			goto st213;
	} else
		goto st226;
	goto st0;
st222:
	if ( ++p == pe )
		goto _test_eof222;
case 222:
	switch( (*p) ) {
		case 33: goto st213;
		case 35: goto tr159;
		case 37: goto st129;
		case 47: goto tr161;
		case 58: goto tr268;
		case 59: goto st213;
		case 61: goto st213;
		case 63: goto tr163;
		case 64: goto st148;
		case 95: goto st213;
		case 126: goto st213;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st213;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st213;
		} else if ( (*p) >= 65 )
			goto st213;
	} else
		goto st223;
	goto st0;
st223:
	if ( ++p == pe )
		goto _test_eof223;
case 223:
	switch( (*p) ) {
		case 33: goto st213;
		case 35: goto tr200;
		case 37: goto st129;
		case 47: goto tr201;
		case 58: goto tr283;
		case 59: goto st213;
		case 61: goto st213;
		case 63: goto tr204;
		case 64: goto st148;
		case 95: goto st213;
		case 126: goto st213;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st213;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st213;
		} else if ( (*p) >= 65 )
			goto st213;
	} else
		goto st224;
	goto st0;
st224:
	if ( ++p == pe )
		goto _test_eof224;
case 224:
	switch( (*p) ) {
		case 33: goto st213;
		case 35: goto tr200;
		case 37: goto st129;
		case 47: goto tr201;
		case 58: goto tr283;
		case 59: goto st213;
		case 61: goto st213;
		case 63: goto tr204;
		case 64: goto st148;
		case 95: goto st213;
		case 126: goto st213;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st213;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st213;
		} else if ( (*p) >= 65 )
			goto st213;
	} else
		goto st225;
	goto st0;
st225:
	if ( ++p == pe )
		goto _test_eof225;
case 225:
	switch( (*p) ) {
		case 33: goto st213;
		case 35: goto tr200;
		case 37: goto st129;
		case 47: goto tr201;
		case 58: goto tr283;
		case 61: goto st213;
		case 63: goto tr204;
		case 64: goto st148;
		case 95: goto st213;
		case 126: goto st213;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st213;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st213;
	} else
		goto st213;
	goto st0;
st226:
	if ( ++p == pe )
		goto _test_eof226;
case 226:
	switch( (*p) ) {
		case 33: goto st213;
		case 35: goto tr159;
		case 37: goto st129;
		case 46: goto st222;
		case 47: goto tr161;
		case 58: goto tr268;
		case 59: goto st213;
		case 61: goto st213;
		case 63: goto tr163;
		case 64: goto st148;
		case 95: goto st213;
		case 126: goto st213;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st213;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st213;
		} else if ( (*p) >= 65 )
			goto st213;
	} else
		goto st227;
	goto st0;
st227:
	if ( ++p == pe )
		goto _test_eof227;
case 227:
	switch( (*p) ) {
		case 33: goto st213;
		case 35: goto tr159;
		case 37: goto st129;
		case 46: goto st222;
		case 47: goto tr161;
		case 58: goto tr268;
		case 61: goto st213;
		case 63: goto tr163;
		case 64: goto st148;
		case 95: goto st213;
		case 126: goto st213;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st213;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st213;
	} else
		goto st213;
	goto st0;
st228:
	if ( ++p == pe )
		goto _test_eof228;
case 228:
	switch( (*p) ) {
		case 33: goto st213;
		case 35: goto tr159;
		case 37: goto st129;
		case 46: goto st220;
		case 47: goto tr161;
		case 58: goto tr268;
		case 59: goto st213;
		case 61: goto st213;
		case 63: goto tr163;
		case 64: goto st148;
		case 95: goto st213;
		case 126: goto st213;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st213;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st213;
		} else if ( (*p) >= 65 )
			goto st213;
	} else
		goto st229;
	goto st0;
st229:
	if ( ++p == pe )
		goto _test_eof229;
case 229:
	switch( (*p) ) {
		case 33: goto st213;
		case 35: goto tr159;
		case 37: goto st129;
		case 46: goto st220;
		case 47: goto tr161;
		case 58: goto tr268;
		case 61: goto st213;
		case 63: goto tr163;
		case 64: goto st148;
		case 95: goto st213;
		case 126: goto st213;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st213;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st213;
	} else
		goto st213;
	goto st0;
st230:
	if ( ++p == pe )
		goto _test_eof230;
case 230:
	switch( (*p) ) {
		case 33: goto st213;
		case 35: goto tr159;
		case 37: goto st129;
		case 46: goto st218;
		case 47: goto tr161;
		case 58: goto tr268;
		case 59: goto st213;
		case 61: goto st213;
		case 63: goto tr163;
		case 64: goto st148;
		case 95: goto st213;
		case 126: goto st213;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st213;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st213;
		} else if ( (*p) >= 65 )
			goto st213;
	} else
		goto st231;
	goto st0;
st231:
	if ( ++p == pe )
		goto _test_eof231;
case 231:
	switch( (*p) ) {
		case 33: goto st213;
		case 35: goto tr159;
		case 37: goto st129;
		case 46: goto st218;
		case 47: goto tr161;
		case 58: goto tr268;
		case 61: goto st213;
		case 63: goto tr163;
		case 64: goto st148;
		case 95: goto st213;
		case 126: goto st213;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st213;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st213;
	} else
		goto st213;
	goto st0;
tr266:
#line 92 "../../src/uri.rl"
	{ s = p; }
	goto st232;
st232:
	if ( ++p == pe )
		goto _test_eof232;
case 232:
#line 4802 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st213;
		case 35: goto tr159;
		case 37: goto st129;
		case 47: goto tr161;
		case 58: goto tr268;
		case 61: goto st213;
		case 63: goto tr163;
		case 64: goto st148;
		case 95: goto st213;
		case 110: goto st233;
		case 126: goto st213;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st213;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st213;
	} else
		goto st213;
	goto st0;
st233:
	if ( ++p == pe )
		goto _test_eof233;
case 233:
	switch( (*p) ) {
		case 33: goto st213;
		case 35: goto tr159;
		case 37: goto st129;
		case 47: goto tr161;
		case 58: goto tr268;
		case 61: goto st213;
		case 63: goto tr163;
		case 64: goto st148;
		case 95: goto st213;
		case 105: goto st234;
		case 126: goto st213;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st213;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st213;
	} else
		goto st213;
	goto st0;
st234:
	if ( ++p == pe )
		goto _test_eof234;
case 234:
	switch( (*p) ) {
		case 33: goto st213;
		case 35: goto tr159;
		case 37: goto st129;
		case 47: goto tr161;
		case 58: goto tr268;
		case 61: goto st213;
		case 63: goto tr163;
		case 64: goto st148;
		case 95: goto st213;
		case 120: goto st235;
		case 126: goto st213;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st213;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st213;
	} else
		goto st213;
	goto st0;
st235:
	if ( ++p == pe )
		goto _test_eof235;
case 235:
	switch( (*p) ) {
		case 33: goto st213;
		case 35: goto tr159;
		case 37: goto st129;
		case 47: goto tr216;
		case 58: goto tr268;
		case 61: goto st213;
		case 63: goto tr163;
		case 64: goto st148;
		case 95: goto st213;
		case 126: goto st213;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st213;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st213;
	} else
		goto st213;
	goto st0;
tr259:
#line 141 "../../src/uri.rl"
	{ s = p; }
#line 131 "../../src/uri.rl"
	{ s = p; }
	goto st236;
st236:
	if ( ++p == pe )
		goto _test_eof236;
case 236:
#line 4912 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st211;
		case 35: goto tr185;
		case 37: goto st124;
		case 47: goto tr186;
		case 58: goto st148;
		case 59: goto st211;
		case 61: goto st211;
		case 63: goto tr188;
		case 64: goto tr262;
		case 95: goto st211;
		case 126: goto st211;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st211;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st211;
		} else if ( (*p) >= 65 )
			goto st211;
	} else
		goto st236;
	goto st0;
tr260:
#line 141 "../../src/uri.rl"
	{ s = p; }
#line 131 "../../src/uri.rl"
	{ s = p; }
	goto st237;
st237:
	if ( ++p == pe )
		goto _test_eof237;
case 237:
#line 4948 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st211;
		case 35: goto tr185;
		case 37: goto st124;
		case 47: goto tr186;
		case 58: goto st148;
		case 61: goto st211;
		case 63: goto tr188;
		case 64: goto tr262;
		case 95: goto st211;
		case 126: goto st211;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st211;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st237;
	} else
		goto st237;
	goto st0;
tr252:
#line 137 "../../src/uri.rl"
	{ s = p; }
#line 99 "../../src/uri.rl"
	{ s = p; }
#line 92 "../../src/uri.rl"
	{ s = p; }
	goto st238;
st238:
	if ( ++p == pe )
		goto _test_eof238;
case 238:
#line 4982 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st209;
		case 35: goto tr159;
		case 37: goto st119;
		case 46: goto st239;
		case 47: goto tr161;
		case 58: goto tr255;
		case 59: goto st209;
		case 61: goto st209;
		case 63: goto tr163;
		case 64: goto tr256;
		case 95: goto st209;
		case 126: goto st209;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st209;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st209;
		} else if ( (*p) >= 65 )
			goto st209;
	} else
		goto st251;
	goto st0;
st239:
	if ( ++p == pe )
		goto _test_eof239;
case 239:
	switch( (*p) ) {
		case 33: goto st209;
		case 35: goto tr159;
		case 37: goto st119;
		case 47: goto tr161;
		case 58: goto tr255;
		case 59: goto st209;
		case 61: goto st209;
		case 63: goto tr163;
		case 64: goto tr256;
		case 95: goto st209;
		case 126: goto st209;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st209;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st209;
		} else if ( (*p) >= 65 )
			goto st209;
	} else
		goto st240;
	goto st0;
st240:
	if ( ++p == pe )
		goto _test_eof240;
case 240:
	switch( (*p) ) {
		case 33: goto st209;
		case 35: goto tr159;
		case 37: goto st119;
		case 46: goto st241;
		case 47: goto tr161;
		case 58: goto tr255;
		case 59: goto st209;
		case 61: goto st209;
		case 63: goto tr163;
		case 64: goto tr256;
		case 95: goto st209;
		case 126: goto st209;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st209;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st209;
		} else if ( (*p) >= 65 )
			goto st209;
	} else
		goto st249;
	goto st0;
st241:
	if ( ++p == pe )
		goto _test_eof241;
case 241:
	switch( (*p) ) {
		case 33: goto st209;
		case 35: goto tr159;
		case 37: goto st119;
		case 47: goto tr161;
		case 58: goto tr255;
		case 59: goto st209;
		case 61: goto st209;
		case 63: goto tr163;
		case 64: goto tr256;
		case 95: goto st209;
		case 126: goto st209;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st209;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st209;
		} else if ( (*p) >= 65 )
			goto st209;
	} else
		goto st242;
	goto st0;
st242:
	if ( ++p == pe )
		goto _test_eof242;
case 242:
	switch( (*p) ) {
		case 33: goto st209;
		case 35: goto tr159;
		case 37: goto st119;
		case 46: goto st243;
		case 47: goto tr161;
		case 58: goto tr255;
		case 59: goto st209;
		case 61: goto st209;
		case 63: goto tr163;
		case 64: goto tr256;
		case 95: goto st209;
		case 126: goto st209;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st209;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st209;
		} else if ( (*p) >= 65 )
			goto st209;
	} else
		goto st247;
	goto st0;
st243:
	if ( ++p == pe )
		goto _test_eof243;
case 243:
	switch( (*p) ) {
		case 33: goto st209;
		case 35: goto tr159;
		case 37: goto st119;
		case 47: goto tr161;
		case 58: goto tr255;
		case 59: goto st209;
		case 61: goto st209;
		case 63: goto tr163;
		case 64: goto tr256;
		case 95: goto st209;
		case 126: goto st209;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st209;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st209;
		} else if ( (*p) >= 65 )
			goto st209;
	} else
		goto st244;
	goto st0;
st244:
	if ( ++p == pe )
		goto _test_eof244;
case 244:
	switch( (*p) ) {
		case 33: goto st209;
		case 35: goto tr200;
		case 37: goto st119;
		case 47: goto tr201;
		case 58: goto tr303;
		case 59: goto st209;
		case 61: goto st209;
		case 63: goto tr204;
		case 64: goto tr256;
		case 95: goto st209;
		case 126: goto st209;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st209;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st209;
		} else if ( (*p) >= 65 )
			goto st209;
	} else
		goto st245;
	goto st0;
st245:
	if ( ++p == pe )
		goto _test_eof245;
case 245:
	switch( (*p) ) {
		case 33: goto st209;
		case 35: goto tr200;
		case 37: goto st119;
		case 47: goto tr201;
		case 58: goto tr303;
		case 59: goto st209;
		case 61: goto st209;
		case 63: goto tr204;
		case 64: goto tr256;
		case 95: goto st209;
		case 126: goto st209;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 46 )
			goto st209;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st209;
		} else if ( (*p) >= 65 )
			goto st209;
	} else
		goto st246;
	goto st0;
st246:
	if ( ++p == pe )
		goto _test_eof246;
case 246:
	switch( (*p) ) {
		case 33: goto st209;
		case 35: goto tr200;
		case 37: goto st119;
		case 47: goto tr201;
		case 58: goto tr303;
		case 61: goto st209;
		case 63: goto tr204;
		case 64: goto tr256;
		case 95: goto st209;
		case 126: goto st209;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st209;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st209;
	} else
		goto st209;
	goto st0;
st247:
	if ( ++p == pe )
		goto _test_eof247;
case 247:
	switch( (*p) ) {
		case 33: goto st209;
		case 35: goto tr159;
		case 37: goto st119;
		case 46: goto st243;
		case 47: goto tr161;
		case 58: goto tr255;
		case 59: goto st209;
		case 61: goto st209;
		case 63: goto tr163;
		case 64: goto tr256;
		case 95: goto st209;
		case 126: goto st209;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st209;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st209;
		} else if ( (*p) >= 65 )
			goto st209;
	} else
		goto st248;
	goto st0;
st248:
	if ( ++p == pe )
		goto _test_eof248;
case 248:
	switch( (*p) ) {
		case 33: goto st209;
		case 35: goto tr159;
		case 37: goto st119;
		case 46: goto st243;
		case 47: goto tr161;
		case 58: goto tr255;
		case 61: goto st209;
		case 63: goto tr163;
		case 64: goto tr256;
		case 95: goto st209;
		case 126: goto st209;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st209;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st209;
	} else
		goto st209;
	goto st0;
st249:
	if ( ++p == pe )
		goto _test_eof249;
case 249:
	switch( (*p) ) {
		case 33: goto st209;
		case 35: goto tr159;
		case 37: goto st119;
		case 46: goto st241;
		case 47: goto tr161;
		case 58: goto tr255;
		case 59: goto st209;
		case 61: goto st209;
		case 63: goto tr163;
		case 64: goto tr256;
		case 95: goto st209;
		case 126: goto st209;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st209;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st209;
		} else if ( (*p) >= 65 )
			goto st209;
	} else
		goto st250;
	goto st0;
st250:
	if ( ++p == pe )
		goto _test_eof250;
case 250:
	switch( (*p) ) {
		case 33: goto st209;
		case 35: goto tr159;
		case 37: goto st119;
		case 46: goto st241;
		case 47: goto tr161;
		case 58: goto tr255;
		case 61: goto st209;
		case 63: goto tr163;
		case 64: goto tr256;
		case 95: goto st209;
		case 126: goto st209;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st209;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st209;
	} else
		goto st209;
	goto st0;
st251:
	if ( ++p == pe )
		goto _test_eof251;
case 251:
	switch( (*p) ) {
		case 33: goto st209;
		case 35: goto tr159;
		case 37: goto st119;
		case 46: goto st239;
		case 47: goto tr161;
		case 58: goto tr255;
		case 59: goto st209;
		case 61: goto st209;
		case 63: goto tr163;
		case 64: goto tr256;
		case 95: goto st209;
		case 126: goto st209;
	}
	if ( (*p) < 48 ) {
		if ( 36 <= (*p) && (*p) <= 45 )
			goto st209;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st209;
		} else if ( (*p) >= 65 )
			goto st209;
	} else
		goto st252;
	goto st0;
st252:
	if ( ++p == pe )
		goto _test_eof252;
case 252:
	switch( (*p) ) {
		case 33: goto st209;
		case 35: goto tr159;
		case 37: goto st119;
		case 46: goto st239;
		case 47: goto tr161;
		case 58: goto tr255;
		case 61: goto st209;
		case 63: goto tr163;
		case 64: goto tr256;
		case 95: goto st209;
		case 126: goto st209;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st209;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st209;
	} else
		goto st209;
	goto st0;
tr253:
#line 137 "../../src/uri.rl"
	{ s = p; }
#line 92 "../../src/uri.rl"
	{ s = p; }
	goto st253;
st253:
	if ( ++p == pe )
		goto _test_eof253;
case 253:
#line 5417 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st209;
		case 35: goto tr159;
		case 37: goto st119;
		case 47: goto tr161;
		case 58: goto tr255;
		case 61: goto st209;
		case 63: goto tr163;
		case 64: goto tr256;
		case 95: goto st209;
		case 110: goto st254;
		case 126: goto st209;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st209;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st209;
	} else
		goto st209;
	goto st0;
st254:
	if ( ++p == pe )
		goto _test_eof254;
case 254:
	switch( (*p) ) {
		case 33: goto st209;
		case 35: goto tr159;
		case 37: goto st119;
		case 47: goto tr161;
		case 58: goto tr255;
		case 61: goto st209;
		case 63: goto tr163;
		case 64: goto tr256;
		case 95: goto st209;
		case 105: goto st255;
		case 126: goto st209;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st209;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st209;
	} else
		goto st209;
	goto st0;
st255:
	if ( ++p == pe )
		goto _test_eof255;
case 255:
	switch( (*p) ) {
		case 33: goto st209;
		case 35: goto tr159;
		case 37: goto st119;
		case 47: goto tr161;
		case 58: goto tr255;
		case 61: goto st209;
		case 63: goto tr163;
		case 64: goto tr256;
		case 95: goto st209;
		case 120: goto st256;
		case 126: goto st209;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st209;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st209;
	} else
		goto st209;
	goto st0;
st256:
	if ( ++p == pe )
		goto _test_eof256;
case 256:
	switch( (*p) ) {
		case 33: goto st209;
		case 35: goto tr159;
		case 37: goto st119;
		case 47: goto tr311;
		case 58: goto tr255;
		case 61: goto st209;
		case 63: goto tr163;
		case 64: goto tr256;
		case 95: goto st209;
		case 126: goto st209;
	}
	if ( (*p) < 65 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st209;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st209;
	} else
		goto st209;
	goto st0;
tr311:
#line 93 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 161 "../../src/uri.rl"
	{ s = p; }
	goto st257;
st257:
	if ( ++p == pe )
		goto _test_eof257;
case 257:
#line 5527 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st148;
		case 35: goto tr151;
		case 37: goto st11;
		case 58: goto st258;
		case 61: goto st148;
		case 63: goto tr155;
		case 95: goto st148;
		case 126: goto st148;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st148;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st148;
	} else
		goto st148;
	goto st0;
st258:
	if ( ++p == pe )
		goto _test_eof258;
case 258:
	switch( (*p) ) {
		case 33: goto tr313;
		case 35: goto tr314;
		case 37: goto tr315;
		case 47: goto tr316;
		case 58: goto tr221;
		case 61: goto tr313;
		case 63: goto tr317;
		case 95: goto tr313;
		case 126: goto tr313;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto tr313;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr313;
	} else
		goto tr313;
	goto st0;
tr313:
#line 128 "../../src/uri.rl"
	{ s = p;}
	goto st259;
st259:
	if ( ++p == pe )
		goto _test_eof259;
case 259:
#line 5579 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st259;
		case 35: goto tr318;
		case 37: goto st134;
		case 47: goto st260;
		case 58: goto tr223;
		case 61: goto st259;
		case 63: goto tr320;
		case 95: goto st259;
		case 126: goto st259;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st259;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st259;
	} else
		goto st259;
	goto st0;
tr315:
#line 128 "../../src/uri.rl"
	{ s = p;}
	goto st134;
st134:
	if ( ++p == pe )
		goto _test_eof134;
case 134:
#line 5608 "../../src/uri.c"
	switch( (*p) ) {
		case 37: goto st259;
		case 117: goto st135;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st259;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st259;
	} else
		goto st259;
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
			goto st259;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st259;
	} else
		goto st259;
	goto st0;
tr324:
#line 161 "../../src/uri.rl"
	{ s = p; }
	goto st260;
tr316:
#line 128 "../../src/uri.rl"
	{ s = p;}
	goto st260;
st260:
	if ( ++p == pe )
		goto _test_eof260;
case 260:
#line 5686 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st260;
		case 35: goto tr318;
		case 37: goto st139;
		case 58: goto tr322;
		case 61: goto st260;
		case 63: goto tr320;
		case 95: goto st260;
		case 126: goto st260;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st260;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st260;
	} else
		goto st260;
	goto st0;
st139:
	if ( ++p == pe )
		goto _test_eof139;
case 139:
	switch( (*p) ) {
		case 37: goto st260;
		case 117: goto st140;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st260;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st260;
	} else
		goto st260;
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
			goto st260;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st260;
	} else
		goto st260;
	goto st0;
tr322:
#line 111 "../../src/uri.rl"
	{
			/*
			 * This action is also called for path_* terminals.
			 * I absolute have no idea why. Please don't blame
			 * and fix grammar if you have a LOT of free time.
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
	goto st261;
st261:
	if ( ++p == pe )
		goto _test_eof261;
case 261:
#line 5798 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st260;
		case 35: goto tr323;
		case 37: goto st139;
		case 47: goto tr324;
		case 58: goto tr322;
		case 61: goto st260;
		case 63: goto tr325;
		case 95: goto st260;
		case 126: goto st260;
	}
	if ( (*p) < 64 ) {
		if ( 36 <= (*p) && (*p) <= 59 )
			goto st260;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st260;
	} else
		goto st260;
	goto st0;
tr158:
#line 151 "../../src/uri.rl"
	{ s = p; }
#line 137 "../../src/uri.rl"
	{ s = p; }
#line 92 "../../src/uri.rl"
	{ s = p; }
	goto st262;
st262:
	if ( ++p == pe )
		goto _test_eof262;
case 262:
#line 5831 "../../src/uri.c"
	switch( (*p) ) {
		case 33: goto st145;
		case 35: goto tr159;
		case 37: goto st6;
		case 43: goto st205;
		case 47: goto tr161;
		case 58: goto tr247;
		case 59: goto st145;
		case 61: goto st145;
		case 63: goto tr163;
		case 64: goto tr164;
		case 95: goto st145;
		case 110: goto st263;
		case 126: goto st145;
	}
	if ( (*p) < 45 ) {
		if ( 36 <= (*p) && (*p) <= 44 )
			goto st145;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st205;
		} else if ( (*p) >= 65 )
			goto st205;
	} else
		goto st205;
	goto st0;
st263:
	if ( ++p == pe )
		goto _test_eof263;
case 263:
	switch( (*p) ) {
		case 33: goto st145;
		case 35: goto tr159;
		case 37: goto st6;
		case 43: goto st205;
		case 47: goto tr161;
		case 58: goto tr247;
		case 59: goto st145;
		case 61: goto st145;
		case 63: goto tr163;
		case 64: goto tr164;
		case 95: goto st145;
		case 105: goto st264;
		case 126: goto st145;
	}
	if ( (*p) < 45 ) {
		if ( 36 <= (*p) && (*p) <= 44 )
			goto st145;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st205;
		} else if ( (*p) >= 65 )
			goto st205;
	} else
		goto st205;
	goto st0;
st264:
	if ( ++p == pe )
		goto _test_eof264;
case 264:
	switch( (*p) ) {
		case 33: goto st145;
		case 35: goto tr159;
		case 37: goto st6;
		case 43: goto st205;
		case 47: goto tr161;
		case 58: goto tr247;
		case 59: goto st145;
		case 61: goto st145;
		case 63: goto tr163;
		case 64: goto tr164;
		case 95: goto st145;
		case 120: goto st265;
		case 126: goto st145;
	}
	if ( (*p) < 45 ) {
		if ( 36 <= (*p) && (*p) <= 44 )
			goto st145;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st205;
		} else if ( (*p) >= 65 )
			goto st205;
	} else
		goto st205;
	goto st0;
st265:
	if ( ++p == pe )
		goto _test_eof265;
case 265:
	switch( (*p) ) {
		case 33: goto st145;
		case 35: goto tr159;
		case 37: goto st6;
		case 43: goto st205;
		case 47: goto tr311;
		case 58: goto tr247;
		case 59: goto st145;
		case 61: goto st145;
		case 63: goto tr163;
		case 64: goto tr164;
		case 95: goto st145;
		case 126: goto st145;
	}
	if ( (*p) < 45 ) {
		if ( 36 <= (*p) && (*p) <= 44 )
			goto st145;
	} else if ( (*p) > 57 ) {
		if ( (*p) > 90 ) {
			if ( 97 <= (*p) && (*p) <= 122 )
				goto st205;
		} else if ( (*p) >= 65 )
			goto st205;
	} else
		goto st205;
	goto st0;
	}
	_test_eof145: cs = 145; goto _test_eof; 
	_test_eof146: cs = 146; goto _test_eof; 
	_test_eof147: cs = 147; goto _test_eof; 
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
	_test_eof148: cs = 148; goto _test_eof; 
	_test_eof11: cs = 11; goto _test_eof; 
	_test_eof12: cs = 12; goto _test_eof; 
	_test_eof13: cs = 13; goto _test_eof; 
	_test_eof14: cs = 14; goto _test_eof; 
	_test_eof15: cs = 15; goto _test_eof; 
	_test_eof149: cs = 149; goto _test_eof; 
	_test_eof150: cs = 150; goto _test_eof; 
	_test_eof16: cs = 16; goto _test_eof; 
	_test_eof17: cs = 17; goto _test_eof; 
	_test_eof18: cs = 18; goto _test_eof; 
	_test_eof19: cs = 19; goto _test_eof; 
	_test_eof20: cs = 20; goto _test_eof; 
	_test_eof151: cs = 151; goto _test_eof; 
	_test_eof21: cs = 21; goto _test_eof; 
	_test_eof22: cs = 22; goto _test_eof; 
	_test_eof23: cs = 23; goto _test_eof; 
	_test_eof24: cs = 24; goto _test_eof; 
	_test_eof25: cs = 25; goto _test_eof; 
	_test_eof26: cs = 26; goto _test_eof; 
	_test_eof27: cs = 27; goto _test_eof; 
	_test_eof152: cs = 152; goto _test_eof; 
	_test_eof28: cs = 28; goto _test_eof; 
	_test_eof29: cs = 29; goto _test_eof; 
	_test_eof30: cs = 30; goto _test_eof; 
	_test_eof31: cs = 31; goto _test_eof; 
	_test_eof32: cs = 32; goto _test_eof; 
	_test_eof153: cs = 153; goto _test_eof; 
	_test_eof154: cs = 154; goto _test_eof; 
	_test_eof155: cs = 155; goto _test_eof; 
	_test_eof156: cs = 156; goto _test_eof; 
	_test_eof157: cs = 157; goto _test_eof; 
	_test_eof33: cs = 33; goto _test_eof; 
	_test_eof34: cs = 34; goto _test_eof; 
	_test_eof35: cs = 35; goto _test_eof; 
	_test_eof36: cs = 36; goto _test_eof; 
	_test_eof37: cs = 37; goto _test_eof; 
	_test_eof158: cs = 158; goto _test_eof; 
	_test_eof159: cs = 159; goto _test_eof; 
	_test_eof160: cs = 160; goto _test_eof; 
	_test_eof161: cs = 161; goto _test_eof; 
	_test_eof162: cs = 162; goto _test_eof; 
	_test_eof163: cs = 163; goto _test_eof; 
	_test_eof164: cs = 164; goto _test_eof; 
	_test_eof165: cs = 165; goto _test_eof; 
	_test_eof166: cs = 166; goto _test_eof; 
	_test_eof167: cs = 167; goto _test_eof; 
	_test_eof168: cs = 168; goto _test_eof; 
	_test_eof169: cs = 169; goto _test_eof; 
	_test_eof170: cs = 170; goto _test_eof; 
	_test_eof171: cs = 171; goto _test_eof; 
	_test_eof172: cs = 172; goto _test_eof; 
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
	_test_eof173: cs = 173; goto _test_eof; 
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
	_test_eof174: cs = 174; goto _test_eof; 
	_test_eof175: cs = 175; goto _test_eof; 
	_test_eof176: cs = 176; goto _test_eof; 
	_test_eof177: cs = 177; goto _test_eof; 
	_test_eof178: cs = 178; goto _test_eof; 
	_test_eof179: cs = 179; goto _test_eof; 
	_test_eof180: cs = 180; goto _test_eof; 
	_test_eof104: cs = 104; goto _test_eof; 
	_test_eof105: cs = 105; goto _test_eof; 
	_test_eof106: cs = 106; goto _test_eof; 
	_test_eof107: cs = 107; goto _test_eof; 
	_test_eof108: cs = 108; goto _test_eof; 
	_test_eof181: cs = 181; goto _test_eof; 
	_test_eof109: cs = 109; goto _test_eof; 
	_test_eof110: cs = 110; goto _test_eof; 
	_test_eof111: cs = 111; goto _test_eof; 
	_test_eof112: cs = 112; goto _test_eof; 
	_test_eof113: cs = 113; goto _test_eof; 
	_test_eof182: cs = 182; goto _test_eof; 
	_test_eof183: cs = 183; goto _test_eof; 
	_test_eof184: cs = 184; goto _test_eof; 
	_test_eof185: cs = 185; goto _test_eof; 
	_test_eof186: cs = 186; goto _test_eof; 
	_test_eof187: cs = 187; goto _test_eof; 
	_test_eof114: cs = 114; goto _test_eof; 
	_test_eof115: cs = 115; goto _test_eof; 
	_test_eof116: cs = 116; goto _test_eof; 
	_test_eof117: cs = 117; goto _test_eof; 
	_test_eof118: cs = 118; goto _test_eof; 
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
	_test_eof200: cs = 200; goto _test_eof; 
	_test_eof201: cs = 201; goto _test_eof; 
	_test_eof202: cs = 202; goto _test_eof; 
	_test_eof203: cs = 203; goto _test_eof; 
	_test_eof204: cs = 204; goto _test_eof; 
	_test_eof205: cs = 205; goto _test_eof; 
	_test_eof206: cs = 206; goto _test_eof; 
	_test_eof207: cs = 207; goto _test_eof; 
	_test_eof208: cs = 208; goto _test_eof; 
	_test_eof209: cs = 209; goto _test_eof; 
	_test_eof119: cs = 119; goto _test_eof; 
	_test_eof120: cs = 120; goto _test_eof; 
	_test_eof121: cs = 121; goto _test_eof; 
	_test_eof122: cs = 122; goto _test_eof; 
	_test_eof123: cs = 123; goto _test_eof; 
	_test_eof210: cs = 210; goto _test_eof; 
	_test_eof211: cs = 211; goto _test_eof; 
	_test_eof124: cs = 124; goto _test_eof; 
	_test_eof125: cs = 125; goto _test_eof; 
	_test_eof126: cs = 126; goto _test_eof; 
	_test_eof127: cs = 127; goto _test_eof; 
	_test_eof128: cs = 128; goto _test_eof; 
	_test_eof212: cs = 212; goto _test_eof; 
	_test_eof213: cs = 213; goto _test_eof; 
	_test_eof129: cs = 129; goto _test_eof; 
	_test_eof130: cs = 130; goto _test_eof; 
	_test_eof131: cs = 131; goto _test_eof; 
	_test_eof132: cs = 132; goto _test_eof; 
	_test_eof133: cs = 133; goto _test_eof; 
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
	_test_eof252: cs = 252; goto _test_eof; 
	_test_eof253: cs = 253; goto _test_eof; 
	_test_eof254: cs = 254; goto _test_eof; 
	_test_eof255: cs = 255; goto _test_eof; 
	_test_eof256: cs = 256; goto _test_eof; 
	_test_eof257: cs = 257; goto _test_eof; 
	_test_eof258: cs = 258; goto _test_eof; 
	_test_eof259: cs = 259; goto _test_eof; 
	_test_eof134: cs = 134; goto _test_eof; 
	_test_eof135: cs = 135; goto _test_eof; 
	_test_eof136: cs = 136; goto _test_eof; 
	_test_eof137: cs = 137; goto _test_eof; 
	_test_eof138: cs = 138; goto _test_eof; 
	_test_eof260: cs = 260; goto _test_eof; 
	_test_eof139: cs = 139; goto _test_eof; 
	_test_eof140: cs = 140; goto _test_eof; 
	_test_eof141: cs = 141; goto _test_eof; 
	_test_eof142: cs = 142; goto _test_eof; 
	_test_eof143: cs = 143; goto _test_eof; 
	_test_eof261: cs = 261; goto _test_eof; 
	_test_eof262: cs = 262; goto _test_eof; 
	_test_eof263: cs = 263; goto _test_eof; 
	_test_eof264: cs = 264; goto _test_eof; 
	_test_eof265: cs = 265; goto _test_eof; 

	_test_eof: {}
	if ( p == eof )
	{
	switch ( cs ) {
	case 150: 
#line 69 "../../src/uri.rl"
	{ uri->query = s; uri->query_len = p - s; }
	break;
	case 147: 
#line 73 "../../src/uri.rl"
	{ uri->fragment = s; uri->fragment_len = p - s; }
	break;
	case 156: 
	case 157: 
#line 111 "../../src/uri.rl"
	{
			/*
			 * This action is also called for path_* terminals.
			 * I absolute have no idea why. Please don't blame
			 * and fix grammar if you have a LOT of free time.
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
	case 144: 
	case 148: 
	case 178: 
	case 179: 
	case 180: 
	case 181: 
	case 204: 
	case 207: 
	case 208: 
	case 211: 
	case 212: 
	case 257: 
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
	break;
	case 149: 
#line 68 "../../src/uri.rl"
	{ s = p; }
#line 69 "../../src/uri.rl"
	{ uri->query = s; uri->query_len = p - s; }
	break;
	case 146: 
#line 72 "../../src/uri.rl"
	{ s = p; }
#line 73 "../../src/uri.rl"
	{ uri->fragment = s; uri->fragment_len = p - s; }
	break;
	case 173: 
	case 182: 
	case 183: 
#line 161 "../../src/uri.rl"
	{ s = p; }
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
	break;
	case 186: 
	case 187: 
	case 259: 
	case 260: 
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 111 "../../src/uri.rl"
	{
			/*
			 * This action is also called for path_* terminals.
			 * I absolute have no idea why. Please don't blame
			 * and fix grammar if you have a LOT of free time.
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
	case 145: 
	case 152: 
	case 158: 
	case 159: 
	case 160: 
	case 161: 
	case 162: 
	case 163: 
	case 167: 
	case 168: 
	case 169: 
	case 170: 
	case 171: 
	case 172: 
	case 174: 
	case 175: 
	case 176: 
	case 177: 
	case 189: 
	case 190: 
	case 191: 
	case 192: 
	case 193: 
	case 197: 
	case 198: 
	case 199: 
	case 200: 
	case 205: 
	case 209: 
	case 213: 
	case 217: 
	case 218: 
	case 219: 
	case 220: 
	case 221: 
	case 222: 
	case 226: 
	case 227: 
	case 228: 
	case 229: 
	case 230: 
	case 231: 
	case 232: 
	case 233: 
	case 234: 
	case 235: 
	case 238: 
	case 239: 
	case 240: 
	case 241: 
	case 242: 
	case 243: 
	case 247: 
	case 248: 
	case 249: 
	case 250: 
	case 251: 
	case 252: 
	case 253: 
	case 254: 
	case 255: 
	case 256: 
	case 262: 
	case 263: 
	case 264: 
	case 265: 
#line 93 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 161 "../../src/uri.rl"
	{ s = p; }
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
	break;
	case 154: 
	case 155: 
	case 184: 
	case 185: 
	case 215: 
	case 216: 
	case 236: 
	case 237: 
#line 132 "../../src/uri.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 161 "../../src/uri.rl"
	{ s = p; }
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
	break;
	case 261: 
#line 161 "../../src/uri.rl"
	{ s = p; }
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 111 "../../src/uri.rl"
	{
			/*
			 * This action is also called for path_* terminals.
			 * I absolute have no idea why. Please don't blame
			 * and fix grammar if you have a LOT of free time.
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
	case 258: 
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 128 "../../src/uri.rl"
	{ s = p;}
#line 111 "../../src/uri.rl"
	{
			/*
			 * This action is also called for path_* terminals.
			 * I absolute have no idea why. Please don't blame
			 * and fix grammar if you have a LOT of free time.
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
	case 188: 
	case 201: 
	case 202: 
	case 203: 
#line 93 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 161 "../../src/uri.rl"
	{ s = p; }
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
#line 179 "../../src/uri.rl"
	{ uri->service_len = p - uri->service;
			   uri->host = NULL; uri->host_len = 0; }
	break;
	case 164: 
	case 165: 
	case 166: 
	case 194: 
	case 195: 
	case 196: 
	case 223: 
	case 224: 
	case 225: 
	case 244: 
	case 245: 
	case 246: 
#line 100 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;
			   uri->host_hint = 1; }
#line 93 "../../src/uri.rl"
	{ uri->host = s; uri->host_len = p - s;}
#line 161 "../../src/uri.rl"
	{ s = p; }
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
	break;
	case 151: 
	case 153: 
	case 206: 
	case 210: 
	case 214: 
#line 131 "../../src/uri.rl"
	{ s = p; }
#line 132 "../../src/uri.rl"
	{ uri->service = s; uri->service_len = p - s; }
#line 161 "../../src/uri.rl"
	{ s = p; }
#line 165 "../../src/uri.rl"
	{ uri->path = s; uri->path_len = p - s; }
	break;
#line 6492 "../../src/uri.c"
	}
	}

	_out: {}
	}

#line 192 "../../src/uri.rl"


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

char *
uri_format(const struct uri *uri)
{
	static char buf[1024];
	/* very primitive implementation suitable for our needs */
	snprintf(buf, sizeof(buf), "%.*s:%.*s",
		 (int) uri->host_len, uri->host != NULL ? uri->host : "*",
		 (int) uri->service_len, uri->service);
	return buf;
}
/* vim: set ft=ragel: */
