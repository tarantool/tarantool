
#line 1 "src/port_uri.rl"
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
#include "port_uri.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <netdb.h>

const char *
port_uri_to_string(const struct port_uri * uri)
{
	static __thread char
		str[NI_MAXSERV + NI_MAXHOST + sizeof(uri->schema)];

	if (!uri || !uri->addr_len) {
		snprintf(str, sizeof(str), "unknown address");
		return str;
	}

	switch (uri->addr.sa_family) {
	case AF_INET6:
	case AF_INET:
	{
		char shost[NI_MAXHOST];
		char sservice[NI_MAXSERV];
		getnameinfo(
			    (struct sockaddr *)&uri->addr,
			    uri->addr_len,
			    shost, sizeof(shost),
			    sservice, sizeof(sservice),
			    NI_NUMERICHOST|NI_NUMERICSERV);
		if (uri->addr.sa_family == AF_INET)
			snprintf(str, sizeof(str), "%s://%s:%s",
				 uri->schema, shost, sservice);
		else
			snprintf(str, sizeof(str), "%s://[%s]:%s",
				 uri->schema, shost, sservice);
                break;
	}
	case AF_UNIX:
	{
		struct sockaddr_un *un =
			(struct sockaddr_un *)&uri->addr;
		snprintf(str, sizeof(str), "unix://%.*s",
			 (int) sizeof(un->sun_path), un->sun_path);
	        break;
	}
	default:
		snprintf(str, sizeof(str), "unknown address");
	        break;
	}
	return str;
}

int
port_uri_parse(struct port_uri *uri, const char *p)
{
	(void) uri;
	const char *pe = p + strlen(p);
	const char *eof = pe;
	int cs;
	memset(uri, 0, sizeof(*uri));

	struct {
		const char *start;
		const char *end;
	}	schema		= { 0, 0 },
		host		= { 0, 0 },
		service		= { 0, 0 },
		sport		= { 0, 0 },
		login		= { 0, 0 },
		password	= { 0, 0 },
		ip4		= { 0, 0 },
		ip6		= { 0, 0 },
		path		= { 0, 0 },
		dport		= { 0, 0 }
	;

	unsigned port = 0;

	
#line 117 "src/port_uri.cc"
static const int port_uri_start = 1;
static const int port_uri_first_final = 74;
static const int port_uri_error = 0;

static const int port_uri_en_main = 1;


#line 125 "src/port_uri.cc"
	{
	cs = port_uri_start;
	}

#line 130 "src/port_uri.cc"
	{
	if ( p == pe )
		goto _test_eof;
	switch ( cs )
	{
case 1:
	switch( (*p) ) {
		case 47: goto tr1;
		case 48: goto tr2;
		case 58: goto st0;
		case 63: goto st0;
		case 91: goto tr6;
		case 117: goto tr7;
	}
	if ( (*p) < 65 ) {
		if ( 49 <= (*p) && (*p) <= 57 )
			goto tr3;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr5;
	} else
		goto tr5;
	goto tr0;
tr0:
#line 146 "src/port_uri.rl"
	{ host.start   = p; }
	goto st74;
st74:
	if ( ++p == pe )
		goto _test_eof74;
case 74:
#line 162 "src/port_uri.cc"
	switch( (*p) ) {
		case 58: goto tr91;
		case 63: goto st0;
	}
	goto st74;
tr91:
#line 147 "src/port_uri.rl"
	{ host.end     = p; }
	goto st2;
tr141:
#line 134 "src/port_uri.rl"
	{ ip4.end   = p; }
#line 147 "src/port_uri.rl"
	{ host.end     = p; }
	goto st2;
tr154:
#line 143 "src/port_uri.rl"
	{ ip6.end   = p - 1; }
#line 147 "src/port_uri.rl"
	{ host.end     = p; }
	goto st2;
st2:
	if ( ++p == pe )
		goto _test_eof2;
case 2:
#line 188 "src/port_uri.cc"
	if ( (*p) < 65 ) {
		if ( 49 <= (*p) && (*p) <= 57 )
			goto tr8;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr9;
	} else
		goto tr9;
	goto st0;
st0:
cs = 0;
	goto _out;
tr8:
#line 155 "src/port_uri.rl"
	{ service.start = p; }
#line 150 "src/port_uri.rl"
	{ dport.start   = p; port = 0; }
#line 151 "src/port_uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st75;
tr92:
#line 151 "src/port_uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st75;
st75:
	if ( ++p == pe )
		goto _test_eof75;
case 75:
#line 217 "src/port_uri.cc"
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr92;
	goto st0;
tr9:
#line 155 "src/port_uri.rl"
	{ service.start = p; }
	goto st76;
st76:
	if ( ++p == pe )
		goto _test_eof76;
case 76:
#line 229 "src/port_uri.cc"
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st77;
	} else if ( (*p) >= 65 )
		goto st77;
	goto st0;
st77:
	if ( ++p == pe )
		goto _test_eof77;
case 77:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st78;
	} else if ( (*p) >= 65 )
		goto st78;
	goto st0;
st78:
	if ( ++p == pe )
		goto _test_eof78;
case 78:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st79;
	} else if ( (*p) >= 65 )
		goto st79;
	goto st0;
st79:
	if ( ++p == pe )
		goto _test_eof79;
case 79:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st80;
	} else if ( (*p) >= 65 )
		goto st80;
	goto st0;
st80:
	if ( ++p == pe )
		goto _test_eof80;
case 80:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st81;
	} else if ( (*p) >= 65 )
		goto st81;
	goto st0;
st81:
	if ( ++p == pe )
		goto _test_eof81;
case 81:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st82;
	} else if ( (*p) >= 65 )
		goto st82;
	goto st0;
st82:
	if ( ++p == pe )
		goto _test_eof82;
case 82:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st83;
	} else if ( (*p) >= 65 )
		goto st83;
	goto st0;
st83:
	if ( ++p == pe )
		goto _test_eof83;
case 83:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st84;
	} else if ( (*p) >= 65 )
		goto st84;
	goto st0;
st84:
	if ( ++p == pe )
		goto _test_eof84;
case 84:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st85;
	} else if ( (*p) >= 65 )
		goto st85;
	goto st0;
st85:
	if ( ++p == pe )
		goto _test_eof85;
case 85:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st86;
	} else if ( (*p) >= 65 )
		goto st86;
	goto st0;
st86:
	if ( ++p == pe )
		goto _test_eof86;
case 86:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st87;
	} else if ( (*p) >= 65 )
		goto st87;
	goto st0;
st87:
	if ( ++p == pe )
		goto _test_eof87;
case 87:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st88;
	} else if ( (*p) >= 65 )
		goto st88;
	goto st0;
st88:
	if ( ++p == pe )
		goto _test_eof88;
case 88:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st89;
	} else if ( (*p) >= 65 )
		goto st89;
	goto st0;
st89:
	if ( ++p == pe )
		goto _test_eof89;
case 89:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st90;
	} else if ( (*p) >= 65 )
		goto st90;
	goto st0;
st90:
	if ( ++p == pe )
		goto _test_eof90;
case 90:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st91;
	} else if ( (*p) >= 65 )
		goto st91;
	goto st0;
st91:
	if ( ++p == pe )
		goto _test_eof91;
case 91:
	goto st0;
tr1:
#line 146 "src/port_uri.rl"
	{ host.start   = p; }
#line 165 "src/port_uri.rl"
	{ path.start = p; }
	goto st92;
st92:
	if ( ++p == pe )
		goto _test_eof92;
case 92:
#line 391 "src/port_uri.cc"
	switch( (*p) ) {
		case 58: goto tr109;
		case 63: goto st95;
	}
	goto st93;
st93:
	if ( ++p == pe )
		goto _test_eof93;
case 93:
	switch( (*p) ) {
		case 58: goto tr109;
		case 63: goto st95;
	}
	goto st93;
tr109:
#line 147 "src/port_uri.rl"
	{ host.end     = p; }
	goto st94;
st94:
	if ( ++p == pe )
		goto _test_eof94;
case 94:
#line 414 "src/port_uri.cc"
	if ( (*p) < 65 ) {
		if ( 49 <= (*p) && (*p) <= 57 )
			goto tr111;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr112;
	} else
		goto tr112;
	goto st95;
st95:
	if ( ++p == pe )
		goto _test_eof95;
case 95:
	goto st95;
tr111:
#line 155 "src/port_uri.rl"
	{ service.start = p; }
#line 150 "src/port_uri.rl"
	{ dport.start   = p; port = 0; }
#line 151 "src/port_uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st96;
tr113:
#line 151 "src/port_uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st96;
st96:
	if ( ++p == pe )
		goto _test_eof96;
case 96:
#line 445 "src/port_uri.cc"
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr113;
	goto st95;
tr112:
#line 155 "src/port_uri.rl"
	{ service.start = p; }
	goto st97;
st97:
	if ( ++p == pe )
		goto _test_eof97;
case 97:
#line 457 "src/port_uri.cc"
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st98;
	} else if ( (*p) >= 65 )
		goto st98;
	goto st95;
st98:
	if ( ++p == pe )
		goto _test_eof98;
case 98:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st99;
	} else if ( (*p) >= 65 )
		goto st99;
	goto st95;
st99:
	if ( ++p == pe )
		goto _test_eof99;
case 99:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st100;
	} else if ( (*p) >= 65 )
		goto st100;
	goto st95;
st100:
	if ( ++p == pe )
		goto _test_eof100;
case 100:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st101;
	} else if ( (*p) >= 65 )
		goto st101;
	goto st95;
st101:
	if ( ++p == pe )
		goto _test_eof101;
case 101:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st102;
	} else if ( (*p) >= 65 )
		goto st102;
	goto st95;
st102:
	if ( ++p == pe )
		goto _test_eof102;
case 102:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st103;
	} else if ( (*p) >= 65 )
		goto st103;
	goto st95;
st103:
	if ( ++p == pe )
		goto _test_eof103;
case 103:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st104;
	} else if ( (*p) >= 65 )
		goto st104;
	goto st95;
st104:
	if ( ++p == pe )
		goto _test_eof104;
case 104:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st105;
	} else if ( (*p) >= 65 )
		goto st105;
	goto st95;
st105:
	if ( ++p == pe )
		goto _test_eof105;
case 105:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st106;
	} else if ( (*p) >= 65 )
		goto st106;
	goto st95;
st106:
	if ( ++p == pe )
		goto _test_eof106;
case 106:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st107;
	} else if ( (*p) >= 65 )
		goto st107;
	goto st95;
st107:
	if ( ++p == pe )
		goto _test_eof107;
case 107:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st108;
	} else if ( (*p) >= 65 )
		goto st108;
	goto st95;
st108:
	if ( ++p == pe )
		goto _test_eof108;
case 108:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st109;
	} else if ( (*p) >= 65 )
		goto st109;
	goto st95;
st109:
	if ( ++p == pe )
		goto _test_eof109;
case 109:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st110;
	} else if ( (*p) >= 65 )
		goto st110;
	goto st95;
st110:
	if ( ++p == pe )
		goto _test_eof110;
case 110:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st111;
	} else if ( (*p) >= 65 )
		goto st111;
	goto st95;
st111:
	if ( ++p == pe )
		goto _test_eof111;
case 111:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st112;
	} else if ( (*p) >= 65 )
		goto st112;
	goto st95;
st112:
	if ( ++p == pe )
		goto _test_eof112;
case 112:
	goto st95;
tr2:
#line 125 "src/port_uri.rl"
	{ login.start  = p; }
#line 146 "src/port_uri.rl"
	{ host.start   = p; }
#line 133 "src/port_uri.rl"
	{ ip4.start = p; }
	goto st113;
st113:
	if ( ++p == pe )
		goto _test_eof113;
case 113:
#line 621 "src/port_uri.cc"
	switch( (*p) ) {
		case 46: goto st114;
		case 58: goto tr131;
		case 63: goto st0;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st126;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st128;
	} else
		goto st128;
	goto st74;
st114:
	if ( ++p == pe )
		goto _test_eof114;
case 114:
	switch( (*p) ) {
		case 58: goto tr91;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st115;
	goto st74;
st115:
	if ( ++p == pe )
		goto _test_eof115;
case 115:
	switch( (*p) ) {
		case 46: goto st116;
		case 58: goto tr91;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st124;
	goto st74;
st116:
	if ( ++p == pe )
		goto _test_eof116;
case 116:
	switch( (*p) ) {
		case 58: goto tr91;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st117;
	goto st74;
st117:
	if ( ++p == pe )
		goto _test_eof117;
case 117:
	switch( (*p) ) {
		case 46: goto st118;
		case 58: goto tr91;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st122;
	goto st74;
st118:
	if ( ++p == pe )
		goto _test_eof118;
case 118:
	switch( (*p) ) {
		case 58: goto tr91;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st119;
	goto st74;
st119:
	if ( ++p == pe )
		goto _test_eof119;
case 119:
	switch( (*p) ) {
		case 58: goto tr141;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st120;
	goto st74;
st120:
	if ( ++p == pe )
		goto _test_eof120;
case 120:
	switch( (*p) ) {
		case 58: goto tr141;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st121;
	goto st74;
st121:
	if ( ++p == pe )
		goto _test_eof121;
case 121:
	switch( (*p) ) {
		case 58: goto tr141;
		case 63: goto st0;
	}
	goto st74;
st122:
	if ( ++p == pe )
		goto _test_eof122;
case 122:
	switch( (*p) ) {
		case 46: goto st118;
		case 58: goto tr91;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st123;
	goto st74;
st123:
	if ( ++p == pe )
		goto _test_eof123;
case 123:
	switch( (*p) ) {
		case 46: goto st118;
		case 58: goto tr91;
		case 63: goto st0;
	}
	goto st74;
st124:
	if ( ++p == pe )
		goto _test_eof124;
case 124:
	switch( (*p) ) {
		case 46: goto st116;
		case 58: goto tr91;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st125;
	goto st74;
st125:
	if ( ++p == pe )
		goto _test_eof125;
case 125:
	switch( (*p) ) {
		case 46: goto st116;
		case 58: goto tr91;
		case 63: goto st0;
	}
	goto st74;
st126:
	if ( ++p == pe )
		goto _test_eof126;
case 126:
	switch( (*p) ) {
		case 46: goto st114;
		case 58: goto tr131;
		case 63: goto st0;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st127;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st128;
	} else
		goto st128;
	goto st74;
st127:
	if ( ++p == pe )
		goto _test_eof127;
case 127:
	switch( (*p) ) {
		case 46: goto st114;
		case 58: goto tr131;
		case 63: goto st0;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st128;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st128;
	} else
		goto st128;
	goto st74;
tr82:
#line 125 "src/port_uri.rl"
	{ login.start  = p; }
#line 146 "src/port_uri.rl"
	{ host.start   = p; }
	goto st128;
st128:
	if ( ++p == pe )
		goto _test_eof128;
case 128:
#line 814 "src/port_uri.cc"
	switch( (*p) ) {
		case 58: goto tr131;
		case 63: goto st0;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st128;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st128;
	} else
		goto st128;
	goto st74;
tr131:
#line 126 "src/port_uri.rl"
	{ login.end    = p; }
#line 147 "src/port_uri.rl"
	{ host.end     = p; }
	goto st3;
st3:
	if ( ++p == pe )
		goto _test_eof3;
case 3:
#line 838 "src/port_uri.cc"
	if ( (*p) == 48 )
		goto tr10;
	if ( (*p) < 65 ) {
		if ( 49 <= (*p) && (*p) <= 57 )
			goto tr11;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr12;
	} else
		goto tr12;
	goto st0;
tr10:
#line 129 "src/port_uri.rl"
	{ password.start = p; }
	goto st4;
st4:
	if ( ++p == pe )
		goto _test_eof4;
case 4:
#line 858 "src/port_uri.cc"
	if ( (*p) == 64 )
		goto tr14;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st4;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st4;
	} else
		goto st4;
	goto st0;
tr14:
#line 130 "src/port_uri.rl"
	{ password.end   = p; }
	goto st5;
st5:
	if ( ++p == pe )
		goto _test_eof5;
case 5:
#line 878 "src/port_uri.cc"
	switch( (*p) ) {
		case 58: goto st0;
		case 63: goto st0;
		case 91: goto tr6;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr15;
	goto tr0;
tr15:
#line 146 "src/port_uri.rl"
	{ host.start   = p; }
#line 133 "src/port_uri.rl"
	{ ip4.start = p; }
	goto st129;
st129:
	if ( ++p == pe )
		goto _test_eof129;
case 129:
#line 897 "src/port_uri.cc"
	switch( (*p) ) {
		case 46: goto st114;
		case 58: goto tr91;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st130;
	goto st74;
st130:
	if ( ++p == pe )
		goto _test_eof130;
case 130:
	switch( (*p) ) {
		case 46: goto st114;
		case 58: goto tr91;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st131;
	goto st74;
st131:
	if ( ++p == pe )
		goto _test_eof131;
case 131:
	switch( (*p) ) {
		case 46: goto st114;
		case 58: goto tr91;
		case 63: goto st0;
	}
	goto st74;
tr6:
#line 146 "src/port_uri.rl"
	{ host.start   = p; }
#line 142 "src/port_uri.rl"
	{ ip6.start = p + 1; }
	goto st132;
st132:
	if ( ++p == pe )
		goto _test_eof132;
case 132:
#line 938 "src/port_uri.cc"
	switch( (*p) ) {
		case 58: goto tr149;
		case 63: goto st0;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st133;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st133;
	} else
		goto st133;
	goto st74;
st133:
	if ( ++p == pe )
		goto _test_eof133;
case 133:
	switch( (*p) ) {
		case 58: goto tr151;
		case 63: goto st0;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st134;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st134;
	} else
		goto st134;
	goto st74;
st134:
	if ( ++p == pe )
		goto _test_eof134;
case 134:
	switch( (*p) ) {
		case 58: goto tr151;
		case 63: goto st0;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st135;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st135;
	} else
		goto st135;
	goto st74;
st135:
	if ( ++p == pe )
		goto _test_eof135;
case 135:
	switch( (*p) ) {
		case 58: goto tr151;
		case 63: goto st0;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st136;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st136;
	} else
		goto st136;
	goto st74;
st136:
	if ( ++p == pe )
		goto _test_eof136;
case 136:
	switch( (*p) ) {
		case 58: goto tr151;
		case 63: goto st0;
	}
	goto st74;
tr151:
#line 147 "src/port_uri.rl"
	{ host.end     = p; }
	goto st6;
st6:
	if ( ++p == pe )
		goto _test_eof6;
case 6:
#line 1020 "src/port_uri.cc"
	switch( (*p) ) {
		case 48: goto st7;
		case 58: goto st11;
		case 93: goto st137;
	}
	if ( (*p) < 71 ) {
		if ( (*p) > 57 ) {
			if ( 65 <= (*p) && (*p) <= 70 )
				goto tr19;
		} else if ( (*p) >= 49 )
			goto tr17;
	} else if ( (*p) > 90 ) {
		if ( (*p) > 102 ) {
			if ( 103 <= (*p) && (*p) <= 122 )
				goto tr9;
		} else if ( (*p) >= 97 )
			goto tr19;
	} else
		goto tr9;
	goto st0;
st7:
	if ( ++p == pe )
		goto _test_eof7;
case 7:
	switch( (*p) ) {
		case 58: goto st11;
		case 93: goto st137;
	}
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
	switch( (*p) ) {
		case 58: goto st11;
		case 93: goto st137;
	}
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
	switch( (*p) ) {
		case 58: goto st11;
		case 93: goto st137;
	}
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
	switch( (*p) ) {
		case 58: goto st11;
		case 93: goto st137;
	}
	goto st0;
st11:
	if ( ++p == pe )
		goto _test_eof11;
case 11:
	switch( (*p) ) {
		case 58: goto st16;
		case 93: goto st137;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st12;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st12;
	} else
		goto st12;
	goto st0;
st12:
	if ( ++p == pe )
		goto _test_eof12;
case 12:
	switch( (*p) ) {
		case 58: goto st16;
		case 93: goto st137;
	}
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
	switch( (*p) ) {
		case 58: goto st16;
		case 93: goto st137;
	}
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
	switch( (*p) ) {
		case 58: goto st16;
		case 93: goto st137;
	}
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
	switch( (*p) ) {
		case 58: goto st16;
		case 93: goto st137;
	}
	goto st0;
st16:
	if ( ++p == pe )
		goto _test_eof16;
case 16:
	switch( (*p) ) {
		case 58: goto st21;
		case 93: goto st137;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st17;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st17;
	} else
		goto st17;
	goto st0;
st17:
	if ( ++p == pe )
		goto _test_eof17;
case 17:
	switch( (*p) ) {
		case 58: goto st21;
		case 93: goto st137;
	}
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
	switch( (*p) ) {
		case 58: goto st21;
		case 93: goto st137;
	}
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
	switch( (*p) ) {
		case 58: goto st21;
		case 93: goto st137;
	}
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
	switch( (*p) ) {
		case 58: goto st21;
		case 93: goto st137;
	}
	goto st0;
st21:
	if ( ++p == pe )
		goto _test_eof21;
case 21:
	switch( (*p) ) {
		case 58: goto st26;
		case 93: goto st137;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st22;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st22;
	} else
		goto st22;
	goto st0;
st22:
	if ( ++p == pe )
		goto _test_eof22;
case 22:
	switch( (*p) ) {
		case 58: goto st26;
		case 93: goto st137;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st23;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st23;
	} else
		goto st23;
	goto st0;
st23:
	if ( ++p == pe )
		goto _test_eof23;
case 23:
	switch( (*p) ) {
		case 58: goto st26;
		case 93: goto st137;
	}
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
	switch( (*p) ) {
		case 58: goto st26;
		case 93: goto st137;
	}
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
	switch( (*p) ) {
		case 58: goto st26;
		case 93: goto st137;
	}
	goto st0;
st26:
	if ( ++p == pe )
		goto _test_eof26;
case 26:
	switch( (*p) ) {
		case 58: goto st31;
		case 93: goto st137;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st27;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st27;
	} else
		goto st27;
	goto st0;
st27:
	if ( ++p == pe )
		goto _test_eof27;
case 27:
	switch( (*p) ) {
		case 58: goto st31;
		case 93: goto st137;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st28;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st28;
	} else
		goto st28;
	goto st0;
st28:
	if ( ++p == pe )
		goto _test_eof28;
case 28:
	switch( (*p) ) {
		case 58: goto st31;
		case 93: goto st137;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st29;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st29;
	} else
		goto st29;
	goto st0;
st29:
	if ( ++p == pe )
		goto _test_eof29;
case 29:
	switch( (*p) ) {
		case 58: goto st31;
		case 93: goto st137;
	}
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
	switch( (*p) ) {
		case 58: goto st31;
		case 93: goto st137;
	}
	goto st0;
st31:
	if ( ++p == pe )
		goto _test_eof31;
case 31:
	switch( (*p) ) {
		case 58: goto st36;
		case 93: goto st137;
	}
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
	switch( (*p) ) {
		case 58: goto st36;
		case 93: goto st137;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st33;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st33;
	} else
		goto st33;
	goto st0;
st33:
	if ( ++p == pe )
		goto _test_eof33;
case 33:
	switch( (*p) ) {
		case 58: goto st36;
		case 93: goto st137;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st34;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st34;
	} else
		goto st34;
	goto st0;
st34:
	if ( ++p == pe )
		goto _test_eof34;
case 34:
	switch( (*p) ) {
		case 58: goto st36;
		case 93: goto st137;
	}
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
	switch( (*p) ) {
		case 58: goto st36;
		case 93: goto st137;
	}
	goto st0;
st36:
	if ( ++p == pe )
		goto _test_eof36;
case 36:
	switch( (*p) ) {
		case 58: goto st41;
		case 93: goto st137;
	}
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
	switch( (*p) ) {
		case 58: goto st41;
		case 93: goto st137;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st38;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st38;
	} else
		goto st38;
	goto st0;
st38:
	if ( ++p == pe )
		goto _test_eof38;
case 38:
	switch( (*p) ) {
		case 58: goto st41;
		case 93: goto st137;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st39;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st39;
	} else
		goto st39;
	goto st0;
st39:
	if ( ++p == pe )
		goto _test_eof39;
case 39:
	switch( (*p) ) {
		case 58: goto st41;
		case 93: goto st137;
	}
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
	switch( (*p) ) {
		case 58: goto st41;
		case 93: goto st137;
	}
	goto st0;
st41:
	if ( ++p == pe )
		goto _test_eof41;
case 41:
	if ( (*p) == 93 )
		goto st137;
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
	if ( (*p) == 93 )
		goto st137;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st43;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st43;
	} else
		goto st43;
	goto st0;
st43:
	if ( ++p == pe )
		goto _test_eof43;
case 43:
	if ( (*p) == 93 )
		goto st137;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st44;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st44;
	} else
		goto st44;
	goto st0;
st44:
	if ( ++p == pe )
		goto _test_eof44;
case 44:
	if ( (*p) == 93 )
		goto st137;
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
	if ( (*p) == 93 )
		goto st137;
	goto st0;
st137:
	if ( ++p == pe )
		goto _test_eof137;
case 137:
	if ( (*p) == 58 )
		goto tr154;
	goto st0;
tr17:
#line 155 "src/port_uri.rl"
	{ service.start = p; }
#line 150 "src/port_uri.rl"
	{ dport.start   = p; port = 0; }
#line 151 "src/port_uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st138;
st138:
	if ( ++p == pe )
		goto _test_eof138;
case 138:
#line 1649 "src/port_uri.cc"
	switch( (*p) ) {
		case 58: goto st11;
		case 93: goto st137;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr155;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st8;
	} else
		goto st8;
	goto st0;
tr155:
#line 151 "src/port_uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st139;
st139:
	if ( ++p == pe )
		goto _test_eof139;
case 139:
#line 1671 "src/port_uri.cc"
	switch( (*p) ) {
		case 58: goto st11;
		case 93: goto st137;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr156;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st9;
	} else
		goto st9;
	goto st0;
tr156:
#line 151 "src/port_uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st140;
st140:
	if ( ++p == pe )
		goto _test_eof140;
case 140:
#line 1693 "src/port_uri.cc"
	switch( (*p) ) {
		case 58: goto st11;
		case 93: goto st137;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr157;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st10;
	} else
		goto st10;
	goto st0;
tr157:
#line 151 "src/port_uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st141;
st141:
	if ( ++p == pe )
		goto _test_eof141;
case 141:
#line 1715 "src/port_uri.cc"
	switch( (*p) ) {
		case 58: goto st11;
		case 93: goto st137;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr92;
	goto st0;
tr19:
#line 155 "src/port_uri.rl"
	{ service.start = p; }
	goto st142;
st142:
	if ( ++p == pe )
		goto _test_eof142;
case 142:
#line 1731 "src/port_uri.cc"
	switch( (*p) ) {
		case 58: goto st11;
		case 93: goto st137;
	}
	if ( (*p) < 71 ) {
		if ( (*p) > 57 ) {
			if ( 65 <= (*p) && (*p) <= 70 )
				goto st143;
		} else if ( (*p) >= 48 )
			goto st8;
	} else if ( (*p) > 90 ) {
		if ( (*p) > 102 ) {
			if ( 103 <= (*p) && (*p) <= 122 )
				goto st77;
		} else if ( (*p) >= 97 )
			goto st143;
	} else
		goto st77;
	goto st0;
st143:
	if ( ++p == pe )
		goto _test_eof143;
case 143:
	switch( (*p) ) {
		case 58: goto st11;
		case 93: goto st137;
	}
	if ( (*p) < 71 ) {
		if ( (*p) > 57 ) {
			if ( 65 <= (*p) && (*p) <= 70 )
				goto st144;
		} else if ( (*p) >= 48 )
			goto st9;
	} else if ( (*p) > 90 ) {
		if ( (*p) > 102 ) {
			if ( 103 <= (*p) && (*p) <= 122 )
				goto st78;
		} else if ( (*p) >= 97 )
			goto st144;
	} else
		goto st78;
	goto st0;
st144:
	if ( ++p == pe )
		goto _test_eof144;
case 144:
	switch( (*p) ) {
		case 58: goto st11;
		case 93: goto st137;
	}
	if ( (*p) < 71 ) {
		if ( (*p) > 57 ) {
			if ( 65 <= (*p) && (*p) <= 70 )
				goto st145;
		} else if ( (*p) >= 48 )
			goto st10;
	} else if ( (*p) > 90 ) {
		if ( (*p) > 102 ) {
			if ( 103 <= (*p) && (*p) <= 122 )
				goto st79;
		} else if ( (*p) >= 97 )
			goto st145;
	} else
		goto st79;
	goto st0;
st145:
	if ( ++p == pe )
		goto _test_eof145;
case 145:
	switch( (*p) ) {
		case 58: goto st11;
		case 93: goto st137;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st80;
	} else if ( (*p) >= 65 )
		goto st80;
	goto st0;
tr149:
#line 147 "src/port_uri.rl"
	{ host.end     = p; }
	goto st46;
st46:
	if ( ++p == pe )
		goto _test_eof46;
case 46:
#line 1819 "src/port_uri.cc"
	switch( (*p) ) {
		case 48: goto st7;
		case 58: goto st47;
		case 93: goto st137;
	}
	if ( (*p) < 71 ) {
		if ( (*p) > 57 ) {
			if ( 65 <= (*p) && (*p) <= 70 )
				goto tr19;
		} else if ( (*p) >= 49 )
			goto tr17;
	} else if ( (*p) > 90 ) {
		if ( (*p) > 102 ) {
			if ( 103 <= (*p) && (*p) <= 122 )
				goto tr9;
		} else if ( (*p) >= 97 )
			goto tr19;
	} else
		goto tr9;
	goto st0;
st47:
	if ( ++p == pe )
		goto _test_eof47;
case 47:
	switch( (*p) ) {
		case 58: goto st16;
		case 70: goto st48;
		case 93: goto st137;
		case 102: goto st48;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st12;
	} else if ( (*p) > 69 ) {
		if ( 97 <= (*p) && (*p) <= 101 )
			goto st12;
	} else
		goto st12;
	goto st0;
st48:
	if ( ++p == pe )
		goto _test_eof48;
case 48:
	switch( (*p) ) {
		case 58: goto st16;
		case 70: goto st49;
		case 93: goto st137;
		case 102: goto st49;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st13;
	} else if ( (*p) > 69 ) {
		if ( 97 <= (*p) && (*p) <= 101 )
			goto st13;
	} else
		goto st13;
	goto st0;
st49:
	if ( ++p == pe )
		goto _test_eof49;
case 49:
	switch( (*p) ) {
		case 58: goto st16;
		case 70: goto st50;
		case 93: goto st137;
		case 102: goto st50;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st14;
	} else if ( (*p) > 69 ) {
		if ( 97 <= (*p) && (*p) <= 101 )
			goto st14;
	} else
		goto st14;
	goto st0;
st50:
	if ( ++p == pe )
		goto _test_eof50;
case 50:
	switch( (*p) ) {
		case 58: goto st16;
		case 70: goto st51;
		case 93: goto st137;
		case 102: goto st51;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st15;
	} else if ( (*p) > 69 ) {
		if ( 97 <= (*p) && (*p) <= 101 )
			goto st15;
	} else
		goto st15;
	goto st0;
st51:
	if ( ++p == pe )
		goto _test_eof51;
case 51:
	switch( (*p) ) {
		case 58: goto st52;
		case 93: goto st137;
	}
	goto st0;
st52:
	if ( ++p == pe )
		goto _test_eof52;
case 52:
	switch( (*p) ) {
		case 58: goto st21;
		case 93: goto st137;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr64;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st17;
	} else
		goto st17;
	goto st0;
tr64:
#line 133 "src/port_uri.rl"
	{ ip4.start = p; }
	goto st53;
st53:
	if ( ++p == pe )
		goto _test_eof53;
case 53:
#line 1950 "src/port_uri.cc"
	switch( (*p) ) {
		case 46: goto st54;
		case 58: goto st21;
		case 93: goto st137;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st66;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st18;
	} else
		goto st18;
	goto st0;
st54:
	if ( ++p == pe )
		goto _test_eof54;
case 54:
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st55;
	goto st0;
st55:
	if ( ++p == pe )
		goto _test_eof55;
case 55:
	if ( (*p) == 46 )
		goto st56;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st64;
	goto st0;
st56:
	if ( ++p == pe )
		goto _test_eof56;
case 56:
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st57;
	goto st0;
st57:
	if ( ++p == pe )
		goto _test_eof57;
case 57:
	if ( (*p) == 46 )
		goto st58;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st62;
	goto st0;
st58:
	if ( ++p == pe )
		goto _test_eof58;
case 58:
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st59;
	goto st0;
st59:
	if ( ++p == pe )
		goto _test_eof59;
case 59:
	if ( (*p) == 93 )
		goto tr75;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st60;
	goto st0;
st60:
	if ( ++p == pe )
		goto _test_eof60;
case 60:
	if ( (*p) == 93 )
		goto tr75;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st61;
	goto st0;
st61:
	if ( ++p == pe )
		goto _test_eof61;
case 61:
	if ( (*p) == 93 )
		goto tr75;
	goto st0;
tr75:
#line 134 "src/port_uri.rl"
	{ ip4.end   = p; }
	goto st146;
st146:
	if ( ++p == pe )
		goto _test_eof146;
case 146:
#line 2037 "src/port_uri.cc"
	if ( (*p) == 58 )
		goto tr91;
	goto st0;
st62:
	if ( ++p == pe )
		goto _test_eof62;
case 62:
	if ( (*p) == 46 )
		goto st58;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st63;
	goto st0;
st63:
	if ( ++p == pe )
		goto _test_eof63;
case 63:
	if ( (*p) == 46 )
		goto st58;
	goto st0;
st64:
	if ( ++p == pe )
		goto _test_eof64;
case 64:
	if ( (*p) == 46 )
		goto st56;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st65;
	goto st0;
st65:
	if ( ++p == pe )
		goto _test_eof65;
case 65:
	if ( (*p) == 46 )
		goto st56;
	goto st0;
st66:
	if ( ++p == pe )
		goto _test_eof66;
case 66:
	switch( (*p) ) {
		case 46: goto st54;
		case 58: goto st21;
		case 93: goto st137;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st67;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st19;
	} else
		goto st19;
	goto st0;
st67:
	if ( ++p == pe )
		goto _test_eof67;
case 67:
	switch( (*p) ) {
		case 46: goto st54;
		case 58: goto st21;
		case 93: goto st137;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st20;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st20;
	} else
		goto st20;
	goto st0;
tr11:
#line 129 "src/port_uri.rl"
	{ password.start = p; }
#line 155 "src/port_uri.rl"
	{ service.start = p; }
#line 150 "src/port_uri.rl"
	{ dport.start   = p; port = 0; }
#line 151 "src/port_uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st147;
tr161:
#line 151 "src/port_uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st147;
st147:
	if ( ++p == pe )
		goto _test_eof147;
case 147:
#line 2127 "src/port_uri.cc"
	if ( (*p) == 64 )
		goto tr14;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr161;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st4;
	} else
		goto st4;
	goto st0;
tr12:
#line 129 "src/port_uri.rl"
	{ password.start = p; }
#line 155 "src/port_uri.rl"
	{ service.start = p; }
	goto st148;
st148:
	if ( ++p == pe )
		goto _test_eof148;
case 148:
#line 2149 "src/port_uri.cc"
	if ( (*p) == 64 )
		goto tr14;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st4;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st149;
	} else
		goto st149;
	goto st0;
st149:
	if ( ++p == pe )
		goto _test_eof149;
case 149:
	if ( (*p) == 64 )
		goto tr14;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st4;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st150;
	} else
		goto st150;
	goto st0;
st150:
	if ( ++p == pe )
		goto _test_eof150;
case 150:
	if ( (*p) == 64 )
		goto tr14;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st4;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st151;
	} else
		goto st151;
	goto st0;
st151:
	if ( ++p == pe )
		goto _test_eof151;
case 151:
	if ( (*p) == 64 )
		goto tr14;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st4;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st152;
	} else
		goto st152;
	goto st0;
st152:
	if ( ++p == pe )
		goto _test_eof152;
case 152:
	if ( (*p) == 64 )
		goto tr14;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st4;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st153;
	} else
		goto st153;
	goto st0;
st153:
	if ( ++p == pe )
		goto _test_eof153;
case 153:
	if ( (*p) == 64 )
		goto tr14;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st4;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st154;
	} else
		goto st154;
	goto st0;
st154:
	if ( ++p == pe )
		goto _test_eof154;
case 154:
	if ( (*p) == 64 )
		goto tr14;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st4;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st155;
	} else
		goto st155;
	goto st0;
st155:
	if ( ++p == pe )
		goto _test_eof155;
case 155:
	if ( (*p) == 64 )
		goto tr14;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st4;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st156;
	} else
		goto st156;
	goto st0;
st156:
	if ( ++p == pe )
		goto _test_eof156;
case 156:
	if ( (*p) == 64 )
		goto tr14;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st4;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st157;
	} else
		goto st157;
	goto st0;
st157:
	if ( ++p == pe )
		goto _test_eof157;
case 157:
	if ( (*p) == 64 )
		goto tr14;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st4;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st158;
	} else
		goto st158;
	goto st0;
st158:
	if ( ++p == pe )
		goto _test_eof158;
case 158:
	if ( (*p) == 64 )
		goto tr14;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st4;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st159;
	} else
		goto st159;
	goto st0;
st159:
	if ( ++p == pe )
		goto _test_eof159;
case 159:
	if ( (*p) == 64 )
		goto tr14;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st4;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st160;
	} else
		goto st160;
	goto st0;
st160:
	if ( ++p == pe )
		goto _test_eof160;
case 160:
	if ( (*p) == 64 )
		goto tr14;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st4;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st161;
	} else
		goto st161;
	goto st0;
st161:
	if ( ++p == pe )
		goto _test_eof161;
case 161:
	if ( (*p) == 64 )
		goto tr14;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st4;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st162;
	} else
		goto st162;
	goto st0;
st162:
	if ( ++p == pe )
		goto _test_eof162;
case 162:
	if ( (*p) == 64 )
		goto tr14;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st4;
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
	if ( (*p) == 64 )
		goto tr14;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st4;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st4;
	} else
		goto st4;
	goto st0;
tr3:
#line 125 "src/port_uri.rl"
	{ login.start  = p; }
#line 146 "src/port_uri.rl"
	{ host.start   = p; }
#line 133 "src/port_uri.rl"
	{ ip4.start = p; }
#line 160 "src/port_uri.rl"
	{ sport.start   = p; port = 0; }
#line 161 "src/port_uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st164;
st164:
	if ( ++p == pe )
		goto _test_eof164;
case 164:
#line 2402 "src/port_uri.cc"
	switch( (*p) ) {
		case 46: goto st114;
		case 58: goto tr131;
		case 63: goto st0;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr177;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st128;
	} else
		goto st128;
	goto st74;
tr177:
#line 161 "src/port_uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st165;
st165:
	if ( ++p == pe )
		goto _test_eof165;
case 165:
#line 2425 "src/port_uri.cc"
	switch( (*p) ) {
		case 46: goto st114;
		case 58: goto tr131;
		case 63: goto st0;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr178;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st128;
	} else
		goto st128;
	goto st74;
tr178:
#line 161 "src/port_uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st166;
st166:
	if ( ++p == pe )
		goto _test_eof166;
case 166:
#line 2448 "src/port_uri.cc"
	switch( (*p) ) {
		case 46: goto st114;
		case 58: goto tr131;
		case 63: goto st0;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr179;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st128;
	} else
		goto st128;
	goto st74;
tr179:
#line 161 "src/port_uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st167;
st167:
	if ( ++p == pe )
		goto _test_eof167;
case 167:
#line 2471 "src/port_uri.cc"
	switch( (*p) ) {
		case 58: goto tr131;
		case 63: goto st0;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr179;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st128;
	} else
		goto st128;
	goto st74;
tr5:
#line 121 "src/port_uri.rl"
	{ schema.start = p; }
#line 125 "src/port_uri.rl"
	{ login.start  = p; }
#line 146 "src/port_uri.rl"
	{ host.start   = p; }
	goto st168;
st168:
	if ( ++p == pe )
		goto _test_eof168;
case 168:
#line 2497 "src/port_uri.cc"
	switch( (*p) ) {
		case 58: goto tr180;
		case 63: goto st0;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st128;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st168;
	} else
		goto st168;
	goto st74;
tr180:
#line 122 "src/port_uri.rl"
	{ schema.end   = p; }
#line 126 "src/port_uri.rl"
	{ login.end    = p; }
#line 147 "src/port_uri.rl"
	{ host.end     = p; }
	goto st68;
st68:
	if ( ++p == pe )
		goto _test_eof68;
case 68:
#line 2523 "src/port_uri.cc"
	switch( (*p) ) {
		case 47: goto st69;
		case 48: goto tr10;
	}
	if ( (*p) < 65 ) {
		if ( 49 <= (*p) && (*p) <= 57 )
			goto tr11;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr12;
	} else
		goto tr12;
	goto st0;
st69:
	if ( ++p == pe )
		goto _test_eof69;
case 69:
	if ( (*p) == 47 )
		goto st70;
	goto st0;
st70:
	if ( ++p == pe )
		goto _test_eof70;
case 70:
	switch( (*p) ) {
		case 58: goto st0;
		case 63: goto st0;
		case 91: goto tr6;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr2;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr82;
	} else
		goto tr82;
	goto tr0;
tr7:
#line 121 "src/port_uri.rl"
	{ schema.start = p; }
#line 125 "src/port_uri.rl"
	{ login.start  = p; }
#line 146 "src/port_uri.rl"
	{ host.start   = p; }
	goto st169;
st169:
	if ( ++p == pe )
		goto _test_eof169;
case 169:
#line 2574 "src/port_uri.cc"
	switch( (*p) ) {
		case 58: goto tr180;
		case 63: goto st0;
		case 110: goto st170;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st128;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st168;
	} else
		goto st168;
	goto st74;
st170:
	if ( ++p == pe )
		goto _test_eof170;
case 170:
	switch( (*p) ) {
		case 58: goto tr180;
		case 63: goto st0;
		case 105: goto st171;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st128;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st168;
	} else
		goto st168;
	goto st74;
st171:
	if ( ++p == pe )
		goto _test_eof171;
case 171:
	switch( (*p) ) {
		case 58: goto tr180;
		case 63: goto st0;
		case 120: goto st172;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st128;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st168;
	} else
		goto st168;
	goto st74;
st172:
	if ( ++p == pe )
		goto _test_eof172;
case 172:
	switch( (*p) ) {
		case 58: goto tr185;
		case 63: goto st0;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st128;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st168;
	} else
		goto st168;
	goto st74;
tr185:
#line 122 "src/port_uri.rl"
	{ schema.end   = p; }
#line 126 "src/port_uri.rl"
	{ login.end    = p; }
#line 147 "src/port_uri.rl"
	{ host.end     = p; }
	goto st71;
st71:
	if ( ++p == pe )
		goto _test_eof71;
case 71:
#line 2654 "src/port_uri.cc"
	switch( (*p) ) {
		case 47: goto st72;
		case 48: goto tr10;
	}
	if ( (*p) < 65 ) {
		if ( 49 <= (*p) && (*p) <= 57 )
			goto tr11;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr12;
	} else
		goto tr12;
	goto st0;
st72:
	if ( ++p == pe )
		goto _test_eof72;
case 72:
	if ( (*p) == 47 )
		goto st73;
	goto st0;
st73:
	if ( ++p == pe )
		goto _test_eof73;
case 73:
	switch( (*p) ) {
		case 58: goto tr87;
		case 63: goto tr87;
		case 91: goto tr89;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr86;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr88;
	} else
		goto tr88;
	goto tr85;
tr85:
#line 169 "src/port_uri.rl"
	{ path.start = p; }
#line 146 "src/port_uri.rl"
	{ host.start   = p; }
	goto st173;
st173:
	if ( ++p == pe )
		goto _test_eof173;
case 173:
#line 2703 "src/port_uri.cc"
	switch( (*p) ) {
		case 58: goto tr187;
		case 63: goto st175;
	}
	goto st173;
tr187:
#line 147 "src/port_uri.rl"
	{ host.end     = p; }
	goto st174;
tr219:
#line 134 "src/port_uri.rl"
	{ ip4.end   = p; }
#line 147 "src/port_uri.rl"
	{ host.end     = p; }
	goto st174;
tr280:
#line 143 "src/port_uri.rl"
	{ ip6.end   = p - 1; }
#line 147 "src/port_uri.rl"
	{ host.end     = p; }
	goto st174;
st174:
	if ( ++p == pe )
		goto _test_eof174;
case 174:
#line 2729 "src/port_uri.cc"
	if ( (*p) < 65 ) {
		if ( 49 <= (*p) && (*p) <= 57 )
			goto tr189;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr190;
	} else
		goto tr190;
	goto st175;
tr87:
#line 169 "src/port_uri.rl"
	{ path.start = p; }
	goto st175;
st175:
	if ( ++p == pe )
		goto _test_eof175;
case 175:
#line 2747 "src/port_uri.cc"
	goto st175;
tr189:
#line 155 "src/port_uri.rl"
	{ service.start = p; }
#line 150 "src/port_uri.rl"
	{ dport.start   = p; port = 0; }
#line 151 "src/port_uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st176;
tr191:
#line 151 "src/port_uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st176;
st176:
	if ( ++p == pe )
		goto _test_eof176;
case 176:
#line 2765 "src/port_uri.cc"
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr191;
	goto st175;
tr190:
#line 155 "src/port_uri.rl"
	{ service.start = p; }
	goto st177;
st177:
	if ( ++p == pe )
		goto _test_eof177;
case 177:
#line 2777 "src/port_uri.cc"
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st178;
	} else if ( (*p) >= 65 )
		goto st178;
	goto st175;
st178:
	if ( ++p == pe )
		goto _test_eof178;
case 178:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st179;
	} else if ( (*p) >= 65 )
		goto st179;
	goto st175;
st179:
	if ( ++p == pe )
		goto _test_eof179;
case 179:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st180;
	} else if ( (*p) >= 65 )
		goto st180;
	goto st175;
st180:
	if ( ++p == pe )
		goto _test_eof180;
case 180:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st181;
	} else if ( (*p) >= 65 )
		goto st181;
	goto st175;
st181:
	if ( ++p == pe )
		goto _test_eof181;
case 181:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st182;
	} else if ( (*p) >= 65 )
		goto st182;
	goto st175;
st182:
	if ( ++p == pe )
		goto _test_eof182;
case 182:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st183;
	} else if ( (*p) >= 65 )
		goto st183;
	goto st175;
st183:
	if ( ++p == pe )
		goto _test_eof183;
case 183:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st184;
	} else if ( (*p) >= 65 )
		goto st184;
	goto st175;
st184:
	if ( ++p == pe )
		goto _test_eof184;
case 184:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st185;
	} else if ( (*p) >= 65 )
		goto st185;
	goto st175;
st185:
	if ( ++p == pe )
		goto _test_eof185;
case 185:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st186;
	} else if ( (*p) >= 65 )
		goto st186;
	goto st175;
st186:
	if ( ++p == pe )
		goto _test_eof186;
case 186:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st187;
	} else if ( (*p) >= 65 )
		goto st187;
	goto st175;
st187:
	if ( ++p == pe )
		goto _test_eof187;
case 187:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st188;
	} else if ( (*p) >= 65 )
		goto st188;
	goto st175;
st188:
	if ( ++p == pe )
		goto _test_eof188;
case 188:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st189;
	} else if ( (*p) >= 65 )
		goto st189;
	goto st175;
st189:
	if ( ++p == pe )
		goto _test_eof189;
case 189:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st190;
	} else if ( (*p) >= 65 )
		goto st190;
	goto st175;
st190:
	if ( ++p == pe )
		goto _test_eof190;
case 190:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st191;
	} else if ( (*p) >= 65 )
		goto st191;
	goto st175;
st191:
	if ( ++p == pe )
		goto _test_eof191;
case 191:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st192;
	} else if ( (*p) >= 65 )
		goto st192;
	goto st175;
st192:
	if ( ++p == pe )
		goto _test_eof192;
case 192:
	goto st175;
tr86:
#line 125 "src/port_uri.rl"
	{ login.start  = p; }
#line 169 "src/port_uri.rl"
	{ path.start = p; }
#line 146 "src/port_uri.rl"
	{ host.start   = p; }
#line 133 "src/port_uri.rl"
	{ ip4.start = p; }
	goto st193;
st193:
	if ( ++p == pe )
		goto _test_eof193;
case 193:
#line 2943 "src/port_uri.cc"
	switch( (*p) ) {
		case 46: goto st194;
		case 58: goto tr209;
		case 63: goto st175;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st206;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st208;
	} else
		goto st208;
	goto st173;
st194:
	if ( ++p == pe )
		goto _test_eof194;
case 194:
	switch( (*p) ) {
		case 58: goto tr187;
		case 63: goto st175;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st195;
	goto st173;
st195:
	if ( ++p == pe )
		goto _test_eof195;
case 195:
	switch( (*p) ) {
		case 46: goto st196;
		case 58: goto tr187;
		case 63: goto st175;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st204;
	goto st173;
st196:
	if ( ++p == pe )
		goto _test_eof196;
case 196:
	switch( (*p) ) {
		case 58: goto tr187;
		case 63: goto st175;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st197;
	goto st173;
st197:
	if ( ++p == pe )
		goto _test_eof197;
case 197:
	switch( (*p) ) {
		case 46: goto st198;
		case 58: goto tr187;
		case 63: goto st175;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st202;
	goto st173;
st198:
	if ( ++p == pe )
		goto _test_eof198;
case 198:
	switch( (*p) ) {
		case 58: goto tr187;
		case 63: goto st175;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st199;
	goto st173;
st199:
	if ( ++p == pe )
		goto _test_eof199;
case 199:
	switch( (*p) ) {
		case 58: goto tr219;
		case 63: goto st175;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st200;
	goto st173;
st200:
	if ( ++p == pe )
		goto _test_eof200;
case 200:
	switch( (*p) ) {
		case 58: goto tr219;
		case 63: goto st175;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st201;
	goto st173;
st201:
	if ( ++p == pe )
		goto _test_eof201;
case 201:
	switch( (*p) ) {
		case 58: goto tr219;
		case 63: goto st175;
	}
	goto st173;
st202:
	if ( ++p == pe )
		goto _test_eof202;
case 202:
	switch( (*p) ) {
		case 46: goto st198;
		case 58: goto tr187;
		case 63: goto st175;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st203;
	goto st173;
st203:
	if ( ++p == pe )
		goto _test_eof203;
case 203:
	switch( (*p) ) {
		case 46: goto st198;
		case 58: goto tr187;
		case 63: goto st175;
	}
	goto st173;
st204:
	if ( ++p == pe )
		goto _test_eof204;
case 204:
	switch( (*p) ) {
		case 46: goto st196;
		case 58: goto tr187;
		case 63: goto st175;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st205;
	goto st173;
st205:
	if ( ++p == pe )
		goto _test_eof205;
case 205:
	switch( (*p) ) {
		case 46: goto st196;
		case 58: goto tr187;
		case 63: goto st175;
	}
	goto st173;
st206:
	if ( ++p == pe )
		goto _test_eof206;
case 206:
	switch( (*p) ) {
		case 46: goto st194;
		case 58: goto tr209;
		case 63: goto st175;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st207;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st208;
	} else
		goto st208;
	goto st173;
st207:
	if ( ++p == pe )
		goto _test_eof207;
case 207:
	switch( (*p) ) {
		case 46: goto st194;
		case 58: goto tr209;
		case 63: goto st175;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st208;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st208;
	} else
		goto st208;
	goto st173;
tr88:
#line 125 "src/port_uri.rl"
	{ login.start  = p; }
#line 169 "src/port_uri.rl"
	{ path.start = p; }
#line 146 "src/port_uri.rl"
	{ host.start   = p; }
	goto st208;
st208:
	if ( ++p == pe )
		goto _test_eof208;
case 208:
#line 3138 "src/port_uri.cc"
	switch( (*p) ) {
		case 58: goto tr209;
		case 63: goto st175;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st208;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st208;
	} else
		goto st208;
	goto st173;
tr209:
#line 126 "src/port_uri.rl"
	{ login.end    = p; }
#line 147 "src/port_uri.rl"
	{ host.end     = p; }
	goto st209;
st209:
	if ( ++p == pe )
		goto _test_eof209;
case 209:
#line 3162 "src/port_uri.cc"
	if ( (*p) == 48 )
		goto tr224;
	if ( (*p) < 65 ) {
		if ( 49 <= (*p) && (*p) <= 57 )
			goto tr225;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr226;
	} else
		goto tr226;
	goto st175;
tr224:
#line 129 "src/port_uri.rl"
	{ password.start = p; }
	goto st210;
st210:
	if ( ++p == pe )
		goto _test_eof210;
case 210:
#line 3182 "src/port_uri.cc"
	if ( (*p) == 64 )
		goto tr228;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st210;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st210;
	} else
		goto st210;
	goto st175;
tr228:
#line 130 "src/port_uri.rl"
	{ password.end   = p; }
	goto st211;
st211:
	if ( ++p == pe )
		goto _test_eof211;
case 211:
#line 3202 "src/port_uri.cc"
	switch( (*p) ) {
		case 58: goto tr87;
		case 63: goto tr87;
		case 91: goto tr89;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr229;
	goto tr85;
tr229:
#line 169 "src/port_uri.rl"
	{ path.start = p; }
#line 146 "src/port_uri.rl"
	{ host.start   = p; }
#line 133 "src/port_uri.rl"
	{ ip4.start = p; }
	goto st212;
st212:
	if ( ++p == pe )
		goto _test_eof212;
case 212:
#line 3223 "src/port_uri.cc"
	switch( (*p) ) {
		case 46: goto st194;
		case 58: goto tr187;
		case 63: goto st175;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st213;
	goto st173;
st213:
	if ( ++p == pe )
		goto _test_eof213;
case 213:
	switch( (*p) ) {
		case 46: goto st194;
		case 58: goto tr187;
		case 63: goto st175;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st214;
	goto st173;
st214:
	if ( ++p == pe )
		goto _test_eof214;
case 214:
	switch( (*p) ) {
		case 46: goto st194;
		case 58: goto tr187;
		case 63: goto st175;
	}
	goto st173;
tr89:
#line 169 "src/port_uri.rl"
	{ path.start = p; }
#line 146 "src/port_uri.rl"
	{ host.start   = p; }
#line 142 "src/port_uri.rl"
	{ ip6.start = p + 1; }
	goto st215;
st215:
	if ( ++p == pe )
		goto _test_eof215;
case 215:
#line 3266 "src/port_uri.cc"
	switch( (*p) ) {
		case 58: goto tr233;
		case 63: goto st175;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st216;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st216;
	} else
		goto st216;
	goto st173;
st216:
	if ( ++p == pe )
		goto _test_eof216;
case 216:
	switch( (*p) ) {
		case 58: goto tr235;
		case 63: goto st175;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st217;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st217;
	} else
		goto st217;
	goto st173;
st217:
	if ( ++p == pe )
		goto _test_eof217;
case 217:
	switch( (*p) ) {
		case 58: goto tr235;
		case 63: goto st175;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st218;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st218;
	} else
		goto st218;
	goto st173;
st218:
	if ( ++p == pe )
		goto _test_eof218;
case 218:
	switch( (*p) ) {
		case 58: goto tr235;
		case 63: goto st175;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st219;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st219;
	} else
		goto st219;
	goto st173;
st219:
	if ( ++p == pe )
		goto _test_eof219;
case 219:
	switch( (*p) ) {
		case 58: goto tr235;
		case 63: goto st175;
	}
	goto st173;
tr235:
#line 147 "src/port_uri.rl"
	{ host.end     = p; }
	goto st220;
st220:
	if ( ++p == pe )
		goto _test_eof220;
case 220:
#line 3348 "src/port_uri.cc"
	switch( (*p) ) {
		case 48: goto st221;
		case 58: goto st225;
		case 93: goto st260;
	}
	if ( (*p) < 71 ) {
		if ( (*p) > 57 ) {
			if ( 65 <= (*p) && (*p) <= 70 )
				goto tr241;
		} else if ( (*p) >= 49 )
			goto tr239;
	} else if ( (*p) > 90 ) {
		if ( (*p) > 102 ) {
			if ( 103 <= (*p) && (*p) <= 122 )
				goto tr190;
		} else if ( (*p) >= 97 )
			goto tr241;
	} else
		goto tr190;
	goto st175;
st221:
	if ( ++p == pe )
		goto _test_eof221;
case 221:
	switch( (*p) ) {
		case 58: goto st225;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st222;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st222;
	} else
		goto st222;
	goto st175;
st222:
	if ( ++p == pe )
		goto _test_eof222;
case 222:
	switch( (*p) ) {
		case 58: goto st225;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st223;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st223;
	} else
		goto st223;
	goto st175;
st223:
	if ( ++p == pe )
		goto _test_eof223;
case 223:
	switch( (*p) ) {
		case 58: goto st225;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st224;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st224;
	} else
		goto st224;
	goto st175;
st224:
	if ( ++p == pe )
		goto _test_eof224;
case 224:
	switch( (*p) ) {
		case 58: goto st225;
		case 93: goto st260;
	}
	goto st175;
st225:
	if ( ++p == pe )
		goto _test_eof225;
case 225:
	switch( (*p) ) {
		case 58: goto st230;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st226;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st226;
	} else
		goto st226;
	goto st175;
st226:
	if ( ++p == pe )
		goto _test_eof226;
case 226:
	switch( (*p) ) {
		case 58: goto st230;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st227;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st227;
	} else
		goto st227;
	goto st175;
st227:
	if ( ++p == pe )
		goto _test_eof227;
case 227:
	switch( (*p) ) {
		case 58: goto st230;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st228;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st228;
	} else
		goto st228;
	goto st175;
st228:
	if ( ++p == pe )
		goto _test_eof228;
case 228:
	switch( (*p) ) {
		case 58: goto st230;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st229;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st229;
	} else
		goto st229;
	goto st175;
st229:
	if ( ++p == pe )
		goto _test_eof229;
case 229:
	switch( (*p) ) {
		case 58: goto st230;
		case 93: goto st260;
	}
	goto st175;
st230:
	if ( ++p == pe )
		goto _test_eof230;
case 230:
	switch( (*p) ) {
		case 58: goto st235;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st231;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st231;
	} else
		goto st231;
	goto st175;
st231:
	if ( ++p == pe )
		goto _test_eof231;
case 231:
	switch( (*p) ) {
		case 58: goto st235;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st232;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st232;
	} else
		goto st232;
	goto st175;
st232:
	if ( ++p == pe )
		goto _test_eof232;
case 232:
	switch( (*p) ) {
		case 58: goto st235;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st233;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st233;
	} else
		goto st233;
	goto st175;
st233:
	if ( ++p == pe )
		goto _test_eof233;
case 233:
	switch( (*p) ) {
		case 58: goto st235;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st234;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st234;
	} else
		goto st234;
	goto st175;
st234:
	if ( ++p == pe )
		goto _test_eof234;
case 234:
	switch( (*p) ) {
		case 58: goto st235;
		case 93: goto st260;
	}
	goto st175;
st235:
	if ( ++p == pe )
		goto _test_eof235;
case 235:
	switch( (*p) ) {
		case 58: goto st240;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st236;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st236;
	} else
		goto st236;
	goto st175;
st236:
	if ( ++p == pe )
		goto _test_eof236;
case 236:
	switch( (*p) ) {
		case 58: goto st240;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st237;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st237;
	} else
		goto st237;
	goto st175;
st237:
	if ( ++p == pe )
		goto _test_eof237;
case 237:
	switch( (*p) ) {
		case 58: goto st240;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st238;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st238;
	} else
		goto st238;
	goto st175;
st238:
	if ( ++p == pe )
		goto _test_eof238;
case 238:
	switch( (*p) ) {
		case 58: goto st240;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st239;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st239;
	} else
		goto st239;
	goto st175;
st239:
	if ( ++p == pe )
		goto _test_eof239;
case 239:
	switch( (*p) ) {
		case 58: goto st240;
		case 93: goto st260;
	}
	goto st175;
st240:
	if ( ++p == pe )
		goto _test_eof240;
case 240:
	switch( (*p) ) {
		case 58: goto st245;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st241;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st241;
	} else
		goto st241;
	goto st175;
st241:
	if ( ++p == pe )
		goto _test_eof241;
case 241:
	switch( (*p) ) {
		case 58: goto st245;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st242;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st242;
	} else
		goto st242;
	goto st175;
st242:
	if ( ++p == pe )
		goto _test_eof242;
case 242:
	switch( (*p) ) {
		case 58: goto st245;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st243;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st243;
	} else
		goto st243;
	goto st175;
st243:
	if ( ++p == pe )
		goto _test_eof243;
case 243:
	switch( (*p) ) {
		case 58: goto st245;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st244;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st244;
	} else
		goto st244;
	goto st175;
st244:
	if ( ++p == pe )
		goto _test_eof244;
case 244:
	switch( (*p) ) {
		case 58: goto st245;
		case 93: goto st260;
	}
	goto st175;
st245:
	if ( ++p == pe )
		goto _test_eof245;
case 245:
	switch( (*p) ) {
		case 58: goto st250;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st246;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st246;
	} else
		goto st246;
	goto st175;
st246:
	if ( ++p == pe )
		goto _test_eof246;
case 246:
	switch( (*p) ) {
		case 58: goto st250;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st247;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st247;
	} else
		goto st247;
	goto st175;
st247:
	if ( ++p == pe )
		goto _test_eof247;
case 247:
	switch( (*p) ) {
		case 58: goto st250;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st248;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st248;
	} else
		goto st248;
	goto st175;
st248:
	if ( ++p == pe )
		goto _test_eof248;
case 248:
	switch( (*p) ) {
		case 58: goto st250;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st249;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st249;
	} else
		goto st249;
	goto st175;
st249:
	if ( ++p == pe )
		goto _test_eof249;
case 249:
	switch( (*p) ) {
		case 58: goto st250;
		case 93: goto st260;
	}
	goto st175;
st250:
	if ( ++p == pe )
		goto _test_eof250;
case 250:
	switch( (*p) ) {
		case 58: goto st255;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st251;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st251;
	} else
		goto st251;
	goto st175;
st251:
	if ( ++p == pe )
		goto _test_eof251;
case 251:
	switch( (*p) ) {
		case 58: goto st255;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st252;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st252;
	} else
		goto st252;
	goto st175;
st252:
	if ( ++p == pe )
		goto _test_eof252;
case 252:
	switch( (*p) ) {
		case 58: goto st255;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st253;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st253;
	} else
		goto st253;
	goto st175;
st253:
	if ( ++p == pe )
		goto _test_eof253;
case 253:
	switch( (*p) ) {
		case 58: goto st255;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st254;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st254;
	} else
		goto st254;
	goto st175;
st254:
	if ( ++p == pe )
		goto _test_eof254;
case 254:
	switch( (*p) ) {
		case 58: goto st255;
		case 93: goto st260;
	}
	goto st175;
st255:
	if ( ++p == pe )
		goto _test_eof255;
case 255:
	if ( (*p) == 93 )
		goto st260;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st256;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st256;
	} else
		goto st256;
	goto st175;
st256:
	if ( ++p == pe )
		goto _test_eof256;
case 256:
	if ( (*p) == 93 )
		goto st260;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st257;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st257;
	} else
		goto st257;
	goto st175;
st257:
	if ( ++p == pe )
		goto _test_eof257;
case 257:
	if ( (*p) == 93 )
		goto st260;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st258;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st258;
	} else
		goto st258;
	goto st175;
st258:
	if ( ++p == pe )
		goto _test_eof258;
case 258:
	if ( (*p) == 93 )
		goto st260;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st259;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st259;
	} else
		goto st259;
	goto st175;
st259:
	if ( ++p == pe )
		goto _test_eof259;
case 259:
	if ( (*p) == 93 )
		goto st260;
	goto st175;
st260:
	if ( ++p == pe )
		goto _test_eof260;
case 260:
	if ( (*p) == 58 )
		goto tr280;
	goto st175;
tr239:
#line 155 "src/port_uri.rl"
	{ service.start = p; }
#line 150 "src/port_uri.rl"
	{ dport.start   = p; port = 0; }
#line 151 "src/port_uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st261;
st261:
	if ( ++p == pe )
		goto _test_eof261;
case 261:
#line 3977 "src/port_uri.cc"
	switch( (*p) ) {
		case 58: goto st225;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr281;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st222;
	} else
		goto st222;
	goto st175;
tr281:
#line 151 "src/port_uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st262;
st262:
	if ( ++p == pe )
		goto _test_eof262;
case 262:
#line 3999 "src/port_uri.cc"
	switch( (*p) ) {
		case 58: goto st225;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr282;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st223;
	} else
		goto st223;
	goto st175;
tr282:
#line 151 "src/port_uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st263;
st263:
	if ( ++p == pe )
		goto _test_eof263;
case 263:
#line 4021 "src/port_uri.cc"
	switch( (*p) ) {
		case 58: goto st225;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr283;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st224;
	} else
		goto st224;
	goto st175;
tr283:
#line 151 "src/port_uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st264;
st264:
	if ( ++p == pe )
		goto _test_eof264;
case 264:
#line 4043 "src/port_uri.cc"
	switch( (*p) ) {
		case 58: goto st225;
		case 93: goto st260;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr191;
	goto st175;
tr241:
#line 155 "src/port_uri.rl"
	{ service.start = p; }
	goto st265;
st265:
	if ( ++p == pe )
		goto _test_eof265;
case 265:
#line 4059 "src/port_uri.cc"
	switch( (*p) ) {
		case 58: goto st225;
		case 93: goto st260;
	}
	if ( (*p) < 71 ) {
		if ( (*p) > 57 ) {
			if ( 65 <= (*p) && (*p) <= 70 )
				goto st266;
		} else if ( (*p) >= 48 )
			goto st222;
	} else if ( (*p) > 90 ) {
		if ( (*p) > 102 ) {
			if ( 103 <= (*p) && (*p) <= 122 )
				goto st178;
		} else if ( (*p) >= 97 )
			goto st266;
	} else
		goto st178;
	goto st175;
st266:
	if ( ++p == pe )
		goto _test_eof266;
case 266:
	switch( (*p) ) {
		case 58: goto st225;
		case 93: goto st260;
	}
	if ( (*p) < 71 ) {
		if ( (*p) > 57 ) {
			if ( 65 <= (*p) && (*p) <= 70 )
				goto st267;
		} else if ( (*p) >= 48 )
			goto st223;
	} else if ( (*p) > 90 ) {
		if ( (*p) > 102 ) {
			if ( 103 <= (*p) && (*p) <= 122 )
				goto st179;
		} else if ( (*p) >= 97 )
			goto st267;
	} else
		goto st179;
	goto st175;
st267:
	if ( ++p == pe )
		goto _test_eof267;
case 267:
	switch( (*p) ) {
		case 58: goto st225;
		case 93: goto st260;
	}
	if ( (*p) < 71 ) {
		if ( (*p) > 57 ) {
			if ( 65 <= (*p) && (*p) <= 70 )
				goto st268;
		} else if ( (*p) >= 48 )
			goto st224;
	} else if ( (*p) > 90 ) {
		if ( (*p) > 102 ) {
			if ( 103 <= (*p) && (*p) <= 122 )
				goto st180;
		} else if ( (*p) >= 97 )
			goto st268;
	} else
		goto st180;
	goto st175;
st268:
	if ( ++p == pe )
		goto _test_eof268;
case 268:
	switch( (*p) ) {
		case 58: goto st225;
		case 93: goto st260;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st181;
	} else if ( (*p) >= 65 )
		goto st181;
	goto st175;
tr233:
#line 147 "src/port_uri.rl"
	{ host.end     = p; }
	goto st269;
st269:
	if ( ++p == pe )
		goto _test_eof269;
case 269:
#line 4147 "src/port_uri.cc"
	switch( (*p) ) {
		case 48: goto st221;
		case 58: goto st270;
		case 93: goto st260;
	}
	if ( (*p) < 71 ) {
		if ( (*p) > 57 ) {
			if ( 65 <= (*p) && (*p) <= 70 )
				goto tr241;
		} else if ( (*p) >= 49 )
			goto tr239;
	} else if ( (*p) > 90 ) {
		if ( (*p) > 102 ) {
			if ( 103 <= (*p) && (*p) <= 122 )
				goto tr190;
		} else if ( (*p) >= 97 )
			goto tr241;
	} else
		goto tr190;
	goto st175;
st270:
	if ( ++p == pe )
		goto _test_eof270;
case 270:
	switch( (*p) ) {
		case 58: goto st230;
		case 70: goto st271;
		case 93: goto st260;
		case 102: goto st271;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st226;
	} else if ( (*p) > 69 ) {
		if ( 97 <= (*p) && (*p) <= 101 )
			goto st226;
	} else
		goto st226;
	goto st175;
st271:
	if ( ++p == pe )
		goto _test_eof271;
case 271:
	switch( (*p) ) {
		case 58: goto st230;
		case 70: goto st272;
		case 93: goto st260;
		case 102: goto st272;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st227;
	} else if ( (*p) > 69 ) {
		if ( 97 <= (*p) && (*p) <= 101 )
			goto st227;
	} else
		goto st227;
	goto st175;
st272:
	if ( ++p == pe )
		goto _test_eof272;
case 272:
	switch( (*p) ) {
		case 58: goto st230;
		case 70: goto st273;
		case 93: goto st260;
		case 102: goto st273;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st228;
	} else if ( (*p) > 69 ) {
		if ( 97 <= (*p) && (*p) <= 101 )
			goto st228;
	} else
		goto st228;
	goto st175;
st273:
	if ( ++p == pe )
		goto _test_eof273;
case 273:
	switch( (*p) ) {
		case 58: goto st230;
		case 70: goto st274;
		case 93: goto st260;
		case 102: goto st274;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st229;
	} else if ( (*p) > 69 ) {
		if ( 97 <= (*p) && (*p) <= 101 )
			goto st229;
	} else
		goto st229;
	goto st175;
st274:
	if ( ++p == pe )
		goto _test_eof274;
case 274:
	switch( (*p) ) {
		case 58: goto st275;
		case 93: goto st260;
	}
	goto st175;
st275:
	if ( ++p == pe )
		goto _test_eof275;
case 275:
	switch( (*p) ) {
		case 58: goto st235;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr293;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st231;
	} else
		goto st231;
	goto st175;
tr293:
#line 133 "src/port_uri.rl"
	{ ip4.start = p; }
	goto st276;
st276:
	if ( ++p == pe )
		goto _test_eof276;
case 276:
#line 4278 "src/port_uri.cc"
	switch( (*p) ) {
		case 46: goto st277;
		case 58: goto st235;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st290;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st232;
	} else
		goto st232;
	goto st175;
st277:
	if ( ++p == pe )
		goto _test_eof277;
case 277:
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st278;
	goto st175;
st278:
	if ( ++p == pe )
		goto _test_eof278;
case 278:
	if ( (*p) == 46 )
		goto st279;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st288;
	goto st175;
st279:
	if ( ++p == pe )
		goto _test_eof279;
case 279:
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st280;
	goto st175;
st280:
	if ( ++p == pe )
		goto _test_eof280;
case 280:
	if ( (*p) == 46 )
		goto st281;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st286;
	goto st175;
st281:
	if ( ++p == pe )
		goto _test_eof281;
case 281:
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st282;
	goto st175;
st282:
	if ( ++p == pe )
		goto _test_eof282;
case 282:
	if ( (*p) == 93 )
		goto tr304;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st283;
	goto st175;
st283:
	if ( ++p == pe )
		goto _test_eof283;
case 283:
	if ( (*p) == 93 )
		goto tr304;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st284;
	goto st175;
st284:
	if ( ++p == pe )
		goto _test_eof284;
case 284:
	if ( (*p) == 93 )
		goto tr304;
	goto st175;
tr304:
#line 134 "src/port_uri.rl"
	{ ip4.end   = p; }
	goto st285;
st285:
	if ( ++p == pe )
		goto _test_eof285;
case 285:
#line 4365 "src/port_uri.cc"
	if ( (*p) == 58 )
		goto tr187;
	goto st175;
st286:
	if ( ++p == pe )
		goto _test_eof286;
case 286:
	if ( (*p) == 46 )
		goto st281;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st287;
	goto st175;
st287:
	if ( ++p == pe )
		goto _test_eof287;
case 287:
	if ( (*p) == 46 )
		goto st281;
	goto st175;
st288:
	if ( ++p == pe )
		goto _test_eof288;
case 288:
	if ( (*p) == 46 )
		goto st279;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st289;
	goto st175;
st289:
	if ( ++p == pe )
		goto _test_eof289;
case 289:
	if ( (*p) == 46 )
		goto st279;
	goto st175;
st290:
	if ( ++p == pe )
		goto _test_eof290;
case 290:
	switch( (*p) ) {
		case 46: goto st277;
		case 58: goto st235;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st291;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st233;
	} else
		goto st233;
	goto st175;
st291:
	if ( ++p == pe )
		goto _test_eof291;
case 291:
	switch( (*p) ) {
		case 46: goto st277;
		case 58: goto st235;
		case 93: goto st260;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st234;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st234;
	} else
		goto st234;
	goto st175;
tr225:
#line 129 "src/port_uri.rl"
	{ password.start = p; }
#line 155 "src/port_uri.rl"
	{ service.start = p; }
#line 150 "src/port_uri.rl"
	{ dport.start   = p; port = 0; }
#line 151 "src/port_uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st292;
tr309:
#line 151 "src/port_uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st292;
st292:
	if ( ++p == pe )
		goto _test_eof292;
case 292:
#line 4455 "src/port_uri.cc"
	if ( (*p) == 64 )
		goto tr228;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr309;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st210;
	} else
		goto st210;
	goto st175;
tr226:
#line 129 "src/port_uri.rl"
	{ password.start = p; }
#line 155 "src/port_uri.rl"
	{ service.start = p; }
	goto st293;
st293:
	if ( ++p == pe )
		goto _test_eof293;
case 293:
#line 4477 "src/port_uri.cc"
	if ( (*p) == 64 )
		goto tr228;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st210;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st294;
	} else
		goto st294;
	goto st175;
st294:
	if ( ++p == pe )
		goto _test_eof294;
case 294:
	if ( (*p) == 64 )
		goto tr228;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st210;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st295;
	} else
		goto st295;
	goto st175;
st295:
	if ( ++p == pe )
		goto _test_eof295;
case 295:
	if ( (*p) == 64 )
		goto tr228;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st210;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st296;
	} else
		goto st296;
	goto st175;
st296:
	if ( ++p == pe )
		goto _test_eof296;
case 296:
	if ( (*p) == 64 )
		goto tr228;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st210;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st297;
	} else
		goto st297;
	goto st175;
st297:
	if ( ++p == pe )
		goto _test_eof297;
case 297:
	if ( (*p) == 64 )
		goto tr228;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st210;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st298;
	} else
		goto st298;
	goto st175;
st298:
	if ( ++p == pe )
		goto _test_eof298;
case 298:
	if ( (*p) == 64 )
		goto tr228;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st210;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st299;
	} else
		goto st299;
	goto st175;
st299:
	if ( ++p == pe )
		goto _test_eof299;
case 299:
	if ( (*p) == 64 )
		goto tr228;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st210;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st300;
	} else
		goto st300;
	goto st175;
st300:
	if ( ++p == pe )
		goto _test_eof300;
case 300:
	if ( (*p) == 64 )
		goto tr228;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st210;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st301;
	} else
		goto st301;
	goto st175;
st301:
	if ( ++p == pe )
		goto _test_eof301;
case 301:
	if ( (*p) == 64 )
		goto tr228;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st210;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st302;
	} else
		goto st302;
	goto st175;
st302:
	if ( ++p == pe )
		goto _test_eof302;
case 302:
	if ( (*p) == 64 )
		goto tr228;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st210;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st303;
	} else
		goto st303;
	goto st175;
st303:
	if ( ++p == pe )
		goto _test_eof303;
case 303:
	if ( (*p) == 64 )
		goto tr228;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st210;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st304;
	} else
		goto st304;
	goto st175;
st304:
	if ( ++p == pe )
		goto _test_eof304;
case 304:
	if ( (*p) == 64 )
		goto tr228;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st210;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st305;
	} else
		goto st305;
	goto st175;
st305:
	if ( ++p == pe )
		goto _test_eof305;
case 305:
	if ( (*p) == 64 )
		goto tr228;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st210;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st306;
	} else
		goto st306;
	goto st175;
st306:
	if ( ++p == pe )
		goto _test_eof306;
case 306:
	if ( (*p) == 64 )
		goto tr228;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st210;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st307;
	} else
		goto st307;
	goto st175;
st307:
	if ( ++p == pe )
		goto _test_eof307;
case 307:
	if ( (*p) == 64 )
		goto tr228;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st210;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st308;
	} else
		goto st308;
	goto st175;
st308:
	if ( ++p == pe )
		goto _test_eof308;
case 308:
	if ( (*p) == 64 )
		goto tr228;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st210;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st210;
	} else
		goto st210;
	goto st175;
	}
	_test_eof74: cs = 74; goto _test_eof; 
	_test_eof2: cs = 2; goto _test_eof; 
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
	_test_eof119: cs = 119; goto _test_eof; 
	_test_eof120: cs = 120; goto _test_eof; 
	_test_eof121: cs = 121; goto _test_eof; 
	_test_eof122: cs = 122; goto _test_eof; 
	_test_eof123: cs = 123; goto _test_eof; 
	_test_eof124: cs = 124; goto _test_eof; 
	_test_eof125: cs = 125; goto _test_eof; 
	_test_eof126: cs = 126; goto _test_eof; 
	_test_eof127: cs = 127; goto _test_eof; 
	_test_eof128: cs = 128; goto _test_eof; 
	_test_eof3: cs = 3; goto _test_eof; 
	_test_eof4: cs = 4; goto _test_eof; 
	_test_eof5: cs = 5; goto _test_eof; 
	_test_eof129: cs = 129; goto _test_eof; 
	_test_eof130: cs = 130; goto _test_eof; 
	_test_eof131: cs = 131; goto _test_eof; 
	_test_eof132: cs = 132; goto _test_eof; 
	_test_eof133: cs = 133; goto _test_eof; 
	_test_eof134: cs = 134; goto _test_eof; 
	_test_eof135: cs = 135; goto _test_eof; 
	_test_eof136: cs = 136; goto _test_eof; 
	_test_eof6: cs = 6; goto _test_eof; 
	_test_eof7: cs = 7; goto _test_eof; 
	_test_eof8: cs = 8; goto _test_eof; 
	_test_eof9: cs = 9; goto _test_eof; 
	_test_eof10: cs = 10; goto _test_eof; 
	_test_eof11: cs = 11; goto _test_eof; 
	_test_eof12: cs = 12; goto _test_eof; 
	_test_eof13: cs = 13; goto _test_eof; 
	_test_eof14: cs = 14; goto _test_eof; 
	_test_eof15: cs = 15; goto _test_eof; 
	_test_eof16: cs = 16; goto _test_eof; 
	_test_eof17: cs = 17; goto _test_eof; 
	_test_eof18: cs = 18; goto _test_eof; 
	_test_eof19: cs = 19; goto _test_eof; 
	_test_eof20: cs = 20; goto _test_eof; 
	_test_eof21: cs = 21; goto _test_eof; 
	_test_eof22: cs = 22; goto _test_eof; 
	_test_eof23: cs = 23; goto _test_eof; 
	_test_eof24: cs = 24; goto _test_eof; 
	_test_eof25: cs = 25; goto _test_eof; 
	_test_eof26: cs = 26; goto _test_eof; 
	_test_eof27: cs = 27; goto _test_eof; 
	_test_eof28: cs = 28; goto _test_eof; 
	_test_eof29: cs = 29; goto _test_eof; 
	_test_eof30: cs = 30; goto _test_eof; 
	_test_eof31: cs = 31; goto _test_eof; 
	_test_eof32: cs = 32; goto _test_eof; 
	_test_eof33: cs = 33; goto _test_eof; 
	_test_eof34: cs = 34; goto _test_eof; 
	_test_eof35: cs = 35; goto _test_eof; 
	_test_eof36: cs = 36; goto _test_eof; 
	_test_eof37: cs = 37; goto _test_eof; 
	_test_eof38: cs = 38; goto _test_eof; 
	_test_eof39: cs = 39; goto _test_eof; 
	_test_eof40: cs = 40; goto _test_eof; 
	_test_eof41: cs = 41; goto _test_eof; 
	_test_eof42: cs = 42; goto _test_eof; 
	_test_eof43: cs = 43; goto _test_eof; 
	_test_eof44: cs = 44; goto _test_eof; 
	_test_eof45: cs = 45; goto _test_eof; 
	_test_eof137: cs = 137; goto _test_eof; 
	_test_eof138: cs = 138; goto _test_eof; 
	_test_eof139: cs = 139; goto _test_eof; 
	_test_eof140: cs = 140; goto _test_eof; 
	_test_eof141: cs = 141; goto _test_eof; 
	_test_eof142: cs = 142; goto _test_eof; 
	_test_eof143: cs = 143; goto _test_eof; 
	_test_eof144: cs = 144; goto _test_eof; 
	_test_eof145: cs = 145; goto _test_eof; 
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
	_test_eof146: cs = 146; goto _test_eof; 
	_test_eof62: cs = 62; goto _test_eof; 
	_test_eof63: cs = 63; goto _test_eof; 
	_test_eof64: cs = 64; goto _test_eof; 
	_test_eof65: cs = 65; goto _test_eof; 
	_test_eof66: cs = 66; goto _test_eof; 
	_test_eof67: cs = 67; goto _test_eof; 
	_test_eof147: cs = 147; goto _test_eof; 
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
	_test_eof163: cs = 163; goto _test_eof; 
	_test_eof164: cs = 164; goto _test_eof; 
	_test_eof165: cs = 165; goto _test_eof; 
	_test_eof166: cs = 166; goto _test_eof; 
	_test_eof167: cs = 167; goto _test_eof; 
	_test_eof168: cs = 168; goto _test_eof; 
	_test_eof68: cs = 68; goto _test_eof; 
	_test_eof69: cs = 69; goto _test_eof; 
	_test_eof70: cs = 70; goto _test_eof; 
	_test_eof169: cs = 169; goto _test_eof; 
	_test_eof170: cs = 170; goto _test_eof; 
	_test_eof171: cs = 171; goto _test_eof; 
	_test_eof172: cs = 172; goto _test_eof; 
	_test_eof71: cs = 71; goto _test_eof; 
	_test_eof72: cs = 72; goto _test_eof; 
	_test_eof73: cs = 73; goto _test_eof; 
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
	_test_eof283: cs = 283; goto _test_eof; 
	_test_eof284: cs = 284; goto _test_eof; 
	_test_eof285: cs = 285; goto _test_eof; 
	_test_eof286: cs = 286; goto _test_eof; 
	_test_eof287: cs = 287; goto _test_eof; 
	_test_eof288: cs = 288; goto _test_eof; 
	_test_eof289: cs = 289; goto _test_eof; 
	_test_eof290: cs = 290; goto _test_eof; 
	_test_eof291: cs = 291; goto _test_eof; 
	_test_eof292: cs = 292; goto _test_eof; 
	_test_eof293: cs = 293; goto _test_eof; 
	_test_eof294: cs = 294; goto _test_eof; 
	_test_eof295: cs = 295; goto _test_eof; 
	_test_eof296: cs = 296; goto _test_eof; 
	_test_eof297: cs = 297; goto _test_eof; 
	_test_eof298: cs = 298; goto _test_eof; 
	_test_eof299: cs = 299; goto _test_eof; 
	_test_eof300: cs = 300; goto _test_eof; 
	_test_eof301: cs = 301; goto _test_eof; 
	_test_eof302: cs = 302; goto _test_eof; 
	_test_eof303: cs = 303; goto _test_eof; 
	_test_eof304: cs = 304; goto _test_eof; 
	_test_eof305: cs = 305; goto _test_eof; 
	_test_eof306: cs = 306; goto _test_eof; 
	_test_eof307: cs = 307; goto _test_eof; 
	_test_eof308: cs = 308; goto _test_eof; 

	_test_eof: {}
	if ( p == eof )
	{
	switch ( cs ) {
	case 74: 
	case 92: 
	case 113: 
	case 114: 
	case 115: 
	case 116: 
	case 117: 
	case 118: 
	case 122: 
	case 123: 
	case 124: 
	case 125: 
	case 126: 
	case 127: 
	case 128: 
	case 129: 
	case 130: 
	case 131: 
	case 132: 
	case 133: 
	case 134: 
	case 135: 
	case 136: 
	case 146: 
	case 168: 
	case 169: 
	case 170: 
	case 171: 
	case 172: 
#line 147 "src/port_uri.rl"
	{ host.end     = p; }
	break;
	case 76: 
	case 77: 
	case 78: 
	case 79: 
	case 80: 
	case 81: 
	case 82: 
	case 83: 
	case 84: 
	case 85: 
	case 86: 
	case 87: 
	case 88: 
	case 89: 
	case 90: 
	case 91: 
	case 142: 
	case 143: 
	case 144: 
	case 145: 
	case 148: 
	case 149: 
	case 150: 
	case 151: 
	case 152: 
	case 153: 
	case 154: 
	case 155: 
	case 156: 
	case 157: 
	case 158: 
	case 159: 
	case 160: 
	case 161: 
	case 162: 
	case 163: 
#line 156 "src/port_uri.rl"
	{ service.end   = p; }
	break;
	case 94: 
	case 95: 
#line 166 "src/port_uri.rl"
	{ path.end   = p; }
	break;
	case 174: 
	case 175: 
	case 209: 
	case 210: 
	case 211: 
	case 220: 
	case 221: 
	case 222: 
	case 223: 
	case 224: 
	case 225: 
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
	case 236: 
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
	case 257: 
	case 258: 
	case 259: 
	case 269: 
	case 270: 
	case 271: 
	case 272: 
	case 273: 
	case 274: 
	case 275: 
	case 276: 
	case 277: 
	case 278: 
	case 279: 
	case 280: 
	case 281: 
	case 282: 
	case 283: 
	case 284: 
	case 286: 
	case 287: 
	case 288: 
	case 289: 
	case 290: 
	case 291: 
#line 170 "src/port_uri.rl"
	{ path.end   = p; }
	break;
	case 119: 
	case 120: 
	case 121: 
#line 134 "src/port_uri.rl"
	{ ip4.end   = p; }
#line 147 "src/port_uri.rl"
	{ host.end     = p; }
	break;
	case 137: 
#line 143 "src/port_uri.rl"
	{ ip6.end   = p - 1; }
#line 147 "src/port_uri.rl"
	{ host.end     = p; }
	break;
	case 164: 
	case 165: 
	case 166: 
	case 167: 
#line 147 "src/port_uri.rl"
	{ host.end     = p; }
#line 162 "src/port_uri.rl"
	{ sport.end     = p; }
	break;
	case 93: 
#line 147 "src/port_uri.rl"
	{ host.end     = p; }
#line 166 "src/port_uri.rl"
	{ path.end   = p; }
	break;
	case 75: 
	case 138: 
	case 139: 
	case 140: 
	case 141: 
	case 147: 
#line 152 "src/port_uri.rl"
	{ dport.end	 = p; }
#line 156 "src/port_uri.rl"
	{ service.end   = p; }
	break;
	case 97: 
	case 98: 
	case 99: 
	case 100: 
	case 101: 
	case 102: 
	case 103: 
	case 104: 
	case 105: 
	case 106: 
	case 107: 
	case 108: 
	case 109: 
	case 110: 
	case 111: 
	case 112: 
#line 156 "src/port_uri.rl"
	{ service.end   = p; }
#line 166 "src/port_uri.rl"
	{ path.end   = p; }
	break;
	case 173: 
	case 193: 
	case 194: 
	case 195: 
	case 196: 
	case 197: 
	case 198: 
	case 202: 
	case 203: 
	case 204: 
	case 205: 
	case 206: 
	case 207: 
	case 208: 
	case 212: 
	case 213: 
	case 214: 
	case 215: 
	case 216: 
	case 217: 
	case 218: 
	case 219: 
	case 285: 
#line 170 "src/port_uri.rl"
	{ path.end   = p; }
#line 147 "src/port_uri.rl"
	{ host.end     = p; }
	break;
	case 177: 
	case 178: 
	case 179: 
	case 180: 
	case 181: 
	case 182: 
	case 183: 
	case 184: 
	case 185: 
	case 186: 
	case 187: 
	case 188: 
	case 189: 
	case 190: 
	case 191: 
	case 192: 
	case 265: 
	case 266: 
	case 267: 
	case 268: 
	case 293: 
	case 294: 
	case 295: 
	case 296: 
	case 297: 
	case 298: 
	case 299: 
	case 300: 
	case 301: 
	case 302: 
	case 303: 
	case 304: 
	case 305: 
	case 306: 
	case 307: 
	case 308: 
#line 170 "src/port_uri.rl"
	{ path.end   = p; }
#line 156 "src/port_uri.rl"
	{ service.end   = p; }
	break;
	case 96: 
#line 152 "src/port_uri.rl"
	{ dport.end	 = p; }
#line 156 "src/port_uri.rl"
	{ service.end   = p; }
#line 166 "src/port_uri.rl"
	{ path.end   = p; }
	break;
	case 199: 
	case 200: 
	case 201: 
#line 170 "src/port_uri.rl"
	{ path.end   = p; }
#line 134 "src/port_uri.rl"
	{ ip4.end   = p; }
#line 147 "src/port_uri.rl"
	{ host.end     = p; }
	break;
	case 260: 
#line 170 "src/port_uri.rl"
	{ path.end   = p; }
#line 143 "src/port_uri.rl"
	{ ip6.end   = p - 1; }
#line 147 "src/port_uri.rl"
	{ host.end     = p; }
	break;
	case 176: 
	case 261: 
	case 262: 
	case 263: 
	case 264: 
	case 292: 
#line 170 "src/port_uri.rl"
	{ path.end   = p; }
#line 152 "src/port_uri.rl"
	{ dport.end	 = p; }
#line 156 "src/port_uri.rl"
	{ service.end   = p; }
	break;
#line 5342 "src/port_uri.cc"
	}
	}

	_out: {}
	}

#line 187 "src/port_uri.rl"


	(void)port_uri_first_final;
	(void)port_uri_error;
	(void)port_uri_en_main;

	if (login.start && login.end && password.start && password.end) {
		snprintf(uri->login, sizeof(uri->login),
			 "%.*s", (int) (login.end - login.start), login.start);
		snprintf(uri->password, sizeof(uri->password),
			 "%.*s", (int) (password.end - password.start),
			 password.start);
	}

	if (path.start && path.end) {
		struct sockaddr_un *un = (struct sockaddr_un *)&uri->addr;
		uri->addr_len = sizeof(*un);
		un->sun_family = AF_UNIX;
		if (path.end - path.start >= sizeof(un->sun_path))
			return -1;

		snprintf(un->sun_path, sizeof(un->sun_path),
			 "%.*s", (int) (path.end - path.start), path.start);
		snprintf(uri->schema, sizeof(uri->schema), "unix");
		return 0;
	}

	if (schema.start && schema.end) {
		snprintf(uri->schema, sizeof(uri->schema),
			 "%.*s", (int) (schema.end - schema.start), schema.start);
	} else {
		snprintf(uri->schema, sizeof(uri->schema), "tcp");
	}


	/* only port was defined */
	if (sport.start && sport.end) {
		struct sockaddr_in *in = (struct sockaddr_in *)&uri->addr;
		uri->addr_len = sizeof(*in);

		in->sin_family = AF_INET;
		in->sin_port = htons(port);
		in->sin_addr.s_addr = INADDR_ANY;
		return 0;
	}


	if (!(dport.start && dport.end)) {
		port = 0;
		if (service.start && service.end) {
			if (service.end - service.start >= NI_MAXSERV)
				return -1;
			char sname[NI_MAXSERV];
			snprintf(sname, sizeof(sname), "%.*s",
				 (int) (service.end - service.start),
                 service.start);
			struct servent *s = getservbyname(sname, NULL);
			if (!s)
				return -1;
			port = ntohs(s->s_port);
		}
	}


	/* IPv4 uri */
	if (ip4.start && ip4.end) {
		struct sockaddr_in *in =
			(struct sockaddr_in *)&uri->addr;
		uri->addr_len = sizeof(*in);

		in->sin_family = AF_INET;
		in->sin_port = htons(port);

		char sip4[3 * 4 + 3 + 1];
		memset(sip4, 0, sizeof(sip4));
		snprintf(sip4, sizeof(sip4), "%.*s", (int) (ip4.end - ip4.start),
			 ip4.start);
		if (inet_aton(sip4, &in->sin_addr))
			return 0;
		return -1;
	}

	/* IPv6 uri */
	if (ip6.start && ip6.end) {
		struct sockaddr_in6 *in6 =
			(struct sockaddr_in6 *)&uri->addr;
		uri->addr_len = sizeof(*in6);


		char sip6[8 * 4 + 7 + 1];
		memset(sip6, 0, sizeof(sip6));
		snprintf(sip6, sizeof(sip6), "%.*s", (int) (ip6.end - ip6.start),
			 ip6.start);

		in6->sin6_family = AF_INET6;
		in6->sin6_port = htonl(port);

		if (inet_pton(AF_INET6, sip6, (void *)&in6->sin6_addr))
			return 0;

		return -1;
	}


	if (!host.start || !host.end)
		return -1;

	if (host.end - host.start >= NI_MAXHOST)
		return -1;

	char shost[NI_MAXHOST];
	char sservice[NI_MAXSERV];
	snprintf(shost, sizeof(shost), "%.*s", (int) (host.end - host.start),
		 host.start);
	if (service.end) {
		snprintf(sservice, sizeof(sservice), "%.*s",
			 (int) (service.end - service.start), service.start);
	} else {
		sservice[0] = '\0';
	}

	struct addrinfo hints, *res;
	memset(&hints, 0, sizeof(hints));
	hints.ai_protocol = getprotobyname("tcp")->p_proto;

	if (getaddrinfo(shost, sservice, &hints, &res) != 0)
		return -1;

	uri->addr_len = res->ai_addrlen;
	memcpy((void *)&uri->addr, (void *)res->ai_addr, res->ai_addrlen);
	freeaddrinfo(res);
	return 0;
}

/* vim: set ft=ragel: */
