
#line 1 "src/port-uri.rl"
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


#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "port-uri.h"
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <netdb.h>


const char *
port_uri_to_string(const struct port_uri * uri)
{
	static __thread
		char str[ NI_MAXSERV + NI_MAXHOST + sizeof(uri->schema) ];

	if (!uri || !uri->addr_len) {
		sprintf(str, "unknown address");
		return str;
	}

	switch(uri->addr.ss_family) {
		case AF_INET6:
		case AF_INET: {
			char shost[NI_MAXHOST];
			char sservice[NI_MAXSERV];
			getnameinfo(
				(struct sockaddr *)&uri->addr,
				uri->addr_len,
				shost, sizeof(shost),
				sservice, sizeof(sservice),
				NI_NUMERICHOST|NI_NUMERICSERV);
			if (uri->addr.ss_family == AF_INET)
				sprintf(str, "%s://%s:%s",
					uri->schema, shost, sservice);
			else
				sprintf(str, "%s://[%s]:%s",
					uri->schema, shost, sservice);
			return str;

		}
		
		case AF_LOCAL: {
			struct sockaddr_un *un =
				(struct sockaddr_un *)&uri->addr;
			sprintf(str, "unix://%.*s",
				(int)sizeof(un->sun_path), un->sun_path);
			return str;
		}
		default:
			assert(false);
	}

}


#line 93 "src/port-uri.cc"
static const int port_uri_start = 1;
static const int port_uri_first_final = 72;
static const int port_uri_error = 0;

static const int port_uri_en_main = 1;


#line 92 "src/port-uri.rl"


struct port_uri *
port_uri_parse(struct port_uri *uri, const char *p)
{
	(void)uri;
	const char *pe = p + strlen(p);
	const char *eof = pe;
	int cs;
	memset(uri, 0, sizeof(*uri));

	struct {
		const char *start;
		const char *end;
	} schema	= { 0, 0 },
	  host		= { 0, 0 },
	  service	= { 0, 0 },
	  sport		= { 0, 0 },
	  login		= { 0, 0 },
	  password	= { 0, 0 },
	  ip4		= { 0, 0 },
	  ip6		= { 0, 0 },
	  path		= { 0, 0 },
	  dport		= { 0, 0 }
	;

	unsigned port = 0;

	
#line 131 "src/port-uri.cc"
	{
	cs = port_uri_start;
	}

#line 136 "src/port-uri.cc"
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
#line 152 "src/port-uri.rl"
	{ host.start   = p; }
	goto st72;
st72:
	if ( ++p == pe )
		goto _test_eof72;
case 72:
#line 168 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto tr88;
		case 63: goto st0;
	}
	goto st72;
tr88:
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
	goto st2;
tr138:
#line 139 "src/port-uri.rl"
	{ ip4.end   = p; }
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
	goto st2;
tr172:
#line 148 "src/port-uri.rl"
	{ ip6.end   = p - 1; }
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
	goto st2;
st2:
	if ( ++p == pe )
		goto _test_eof2;
case 2:
#line 194 "src/port-uri.cc"
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
#line 161 "src/port-uri.rl"
	{ service.start = p; }
#line 156 "src/port-uri.rl"
	{ dport.start   = p; port = 0; }
#line 157 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st73;
tr89:
#line 157 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st73;
st73:
	if ( ++p == pe )
		goto _test_eof73;
case 73:
#line 223 "src/port-uri.cc"
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr89;
	goto st0;
tr9:
#line 161 "src/port-uri.rl"
	{ service.start = p; }
	goto st74;
st74:
	if ( ++p == pe )
		goto _test_eof74;
case 74:
#line 235 "src/port-uri.cc"
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st75;
	} else if ( (*p) >= 65 )
		goto st75;
	goto st0;
st75:
	if ( ++p == pe )
		goto _test_eof75;
case 75:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st76;
	} else if ( (*p) >= 65 )
		goto st76;
	goto st0;
st76:
	if ( ++p == pe )
		goto _test_eof76;
case 76:
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
	goto st0;
tr1:
#line 152 "src/port-uri.rl"
	{ host.start   = p; }
#line 171 "src/port-uri.rl"
	{ path.start = p; }
	goto st90;
st90:
	if ( ++p == pe )
		goto _test_eof90;
case 90:
#line 397 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto tr106;
		case 63: goto st93;
	}
	goto st91;
st91:
	if ( ++p == pe )
		goto _test_eof91;
case 91:
	switch( (*p) ) {
		case 58: goto tr106;
		case 63: goto st93;
	}
	goto st91;
tr106:
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
	goto st92;
st92:
	if ( ++p == pe )
		goto _test_eof92;
case 92:
#line 420 "src/port-uri.cc"
	if ( (*p) < 65 ) {
		if ( 49 <= (*p) && (*p) <= 57 )
			goto tr108;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr109;
	} else
		goto tr109;
	goto st93;
st93:
	if ( ++p == pe )
		goto _test_eof93;
case 93:
	goto st93;
tr108:
#line 161 "src/port-uri.rl"
	{ service.start = p; }
#line 156 "src/port-uri.rl"
	{ dport.start   = p; port = 0; }
#line 157 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st94;
tr110:
#line 157 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st94;
st94:
	if ( ++p == pe )
		goto _test_eof94;
case 94:
#line 451 "src/port-uri.cc"
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr110;
	goto st93;
tr109:
#line 161 "src/port-uri.rl"
	{ service.start = p; }
	goto st95;
st95:
	if ( ++p == pe )
		goto _test_eof95;
case 95:
#line 463 "src/port-uri.cc"
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st96;
	} else if ( (*p) >= 65 )
		goto st96;
	goto st93;
st96:
	if ( ++p == pe )
		goto _test_eof96;
case 96:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st97;
	} else if ( (*p) >= 65 )
		goto st97;
	goto st93;
st97:
	if ( ++p == pe )
		goto _test_eof97;
case 97:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st98;
	} else if ( (*p) >= 65 )
		goto st98;
	goto st93;
st98:
	if ( ++p == pe )
		goto _test_eof98;
case 98:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st99;
	} else if ( (*p) >= 65 )
		goto st99;
	goto st93;
st99:
	if ( ++p == pe )
		goto _test_eof99;
case 99:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st100;
	} else if ( (*p) >= 65 )
		goto st100;
	goto st93;
st100:
	if ( ++p == pe )
		goto _test_eof100;
case 100:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st101;
	} else if ( (*p) >= 65 )
		goto st101;
	goto st93;
st101:
	if ( ++p == pe )
		goto _test_eof101;
case 101:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st102;
	} else if ( (*p) >= 65 )
		goto st102;
	goto st93;
st102:
	if ( ++p == pe )
		goto _test_eof102;
case 102:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st103;
	} else if ( (*p) >= 65 )
		goto st103;
	goto st93;
st103:
	if ( ++p == pe )
		goto _test_eof103;
case 103:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st104;
	} else if ( (*p) >= 65 )
		goto st104;
	goto st93;
st104:
	if ( ++p == pe )
		goto _test_eof104;
case 104:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st105;
	} else if ( (*p) >= 65 )
		goto st105;
	goto st93;
st105:
	if ( ++p == pe )
		goto _test_eof105;
case 105:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st106;
	} else if ( (*p) >= 65 )
		goto st106;
	goto st93;
st106:
	if ( ++p == pe )
		goto _test_eof106;
case 106:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st107;
	} else if ( (*p) >= 65 )
		goto st107;
	goto st93;
st107:
	if ( ++p == pe )
		goto _test_eof107;
case 107:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st108;
	} else if ( (*p) >= 65 )
		goto st108;
	goto st93;
st108:
	if ( ++p == pe )
		goto _test_eof108;
case 108:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st109;
	} else if ( (*p) >= 65 )
		goto st109;
	goto st93;
st109:
	if ( ++p == pe )
		goto _test_eof109;
case 109:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st110;
	} else if ( (*p) >= 65 )
		goto st110;
	goto st93;
st110:
	if ( ++p == pe )
		goto _test_eof110;
case 110:
	goto st93;
tr2:
#line 130 "src/port-uri.rl"
	{ login.start  = p; }
#line 152 "src/port-uri.rl"
	{ host.start   = p; }
#line 138 "src/port-uri.rl"
	{ ip4.start = p; }
	goto st111;
st111:
	if ( ++p == pe )
		goto _test_eof111;
case 111:
#line 627 "src/port-uri.cc"
	switch( (*p) ) {
		case 46: goto st112;
		case 58: goto tr88;
		case 63: goto st0;
		case 64: goto tr128;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st124;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st126;
	} else
		goto st126;
	goto st72;
st112:
	if ( ++p == pe )
		goto _test_eof112;
case 112:
	switch( (*p) ) {
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st113;
	goto st72;
st113:
	if ( ++p == pe )
		goto _test_eof113;
case 113:
	switch( (*p) ) {
		case 46: goto st114;
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st122;
	goto st72;
st114:
	if ( ++p == pe )
		goto _test_eof114;
case 114:
	switch( (*p) ) {
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st115;
	goto st72;
st115:
	if ( ++p == pe )
		goto _test_eof115;
case 115:
	switch( (*p) ) {
		case 46: goto st116;
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st120;
	goto st72;
st116:
	if ( ++p == pe )
		goto _test_eof116;
case 116:
	switch( (*p) ) {
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st117;
	goto st72;
st117:
	if ( ++p == pe )
		goto _test_eof117;
case 117:
	switch( (*p) ) {
		case 58: goto tr138;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st118;
	goto st72;
st118:
	if ( ++p == pe )
		goto _test_eof118;
case 118:
	switch( (*p) ) {
		case 58: goto tr138;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st119;
	goto st72;
st119:
	if ( ++p == pe )
		goto _test_eof119;
case 119:
	switch( (*p) ) {
		case 58: goto tr138;
		case 63: goto st0;
	}
	goto st72;
st120:
	if ( ++p == pe )
		goto _test_eof120;
case 120:
	switch( (*p) ) {
		case 46: goto st116;
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st121;
	goto st72;
st121:
	if ( ++p == pe )
		goto _test_eof121;
case 121:
	switch( (*p) ) {
		case 46: goto st116;
		case 58: goto tr88;
		case 63: goto st0;
	}
	goto st72;
st122:
	if ( ++p == pe )
		goto _test_eof122;
case 122:
	switch( (*p) ) {
		case 46: goto st114;
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st123;
	goto st72;
st123:
	if ( ++p == pe )
		goto _test_eof123;
case 123:
	switch( (*p) ) {
		case 46: goto st114;
		case 58: goto tr88;
		case 63: goto st0;
	}
	goto st72;
st124:
	if ( ++p == pe )
		goto _test_eof124;
case 124:
	switch( (*p) ) {
		case 46: goto st112;
		case 58: goto tr88;
		case 63: goto st0;
		case 64: goto tr128;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st125;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st126;
	} else
		goto st126;
	goto st72;
st125:
	if ( ++p == pe )
		goto _test_eof125;
case 125:
	switch( (*p) ) {
		case 46: goto st112;
		case 58: goto tr88;
		case 63: goto st0;
		case 64: goto tr128;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st126;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st126;
	} else
		goto st126;
	goto st72;
tr79:
#line 130 "src/port-uri.rl"
	{ login.start  = p; }
#line 152 "src/port-uri.rl"
	{ host.start   = p; }
	goto st126;
st126:
	if ( ++p == pe )
		goto _test_eof126;
case 126:
#line 823 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto tr88;
		case 63: goto st0;
		case 64: goto tr128;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st126;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st126;
	} else
		goto st126;
	goto st72;
tr128:
#line 131 "src/port-uri.rl"
	{ login.end    = p; }
	goto st127;
st127:
	if ( ++p == pe )
		goto _test_eof127;
case 127:
#line 846 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr143;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr143;
	} else
		goto tr143;
	goto st72;
tr143:
#line 134 "src/port-uri.rl"
	{ password.start = p; }
	goto st128;
st128:
	if ( ++p == pe )
		goto _test_eof128;
case 128:
#line 868 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto tr145;
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
	goto st72;
tr145:
#line 135 "src/port-uri.rl"
	{ password.end   = p; }
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
	goto st3;
st3:
	if ( ++p == pe )
		goto _test_eof3;
case 3:
#line 892 "src/port-uri.cc"
	switch( (*p) ) {
		case 48: goto tr10;
		case 58: goto st0;
		case 63: goto st0;
		case 91: goto tr6;
	}
	if ( (*p) < 65 ) {
		if ( 49 <= (*p) && (*p) <= 57 )
			goto tr11;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr12;
	} else
		goto tr12;
	goto tr0;
tr10:
#line 152 "src/port-uri.rl"
	{ host.start   = p; }
#line 138 "src/port-uri.rl"
	{ ip4.start = p; }
	goto st129;
st129:
	if ( ++p == pe )
		goto _test_eof129;
case 129:
#line 918 "src/port-uri.cc"
	switch( (*p) ) {
		case 46: goto st112;
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st130;
	goto st72;
st130:
	if ( ++p == pe )
		goto _test_eof130;
case 130:
	switch( (*p) ) {
		case 46: goto st112;
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st131;
	goto st72;
st131:
	if ( ++p == pe )
		goto _test_eof131;
case 131:
	switch( (*p) ) {
		case 46: goto st112;
		case 58: goto tr88;
		case 63: goto st0;
	}
	goto st72;
tr11:
#line 152 "src/port-uri.rl"
	{ host.start   = p; }
#line 138 "src/port-uri.rl"
	{ ip4.start = p; }
#line 161 "src/port-uri.rl"
	{ service.start = p; }
#line 156 "src/port-uri.rl"
	{ dport.start   = p; port = 0; }
#line 157 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st132;
st132:
	if ( ++p == pe )
		goto _test_eof132;
case 132:
#line 965 "src/port-uri.cc"
	switch( (*p) ) {
		case 46: goto st112;
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr148;
	goto st72;
tr148:
#line 157 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st133;
st133:
	if ( ++p == pe )
		goto _test_eof133;
case 133:
#line 982 "src/port-uri.cc"
	switch( (*p) ) {
		case 46: goto st112;
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr149;
	goto st72;
tr149:
#line 157 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st134;
st134:
	if ( ++p == pe )
		goto _test_eof134;
case 134:
#line 999 "src/port-uri.cc"
	switch( (*p) ) {
		case 46: goto st112;
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr150;
	goto st72;
tr150:
#line 157 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st135;
st135:
	if ( ++p == pe )
		goto _test_eof135;
case 135:
#line 1016 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr150;
	goto st72;
tr12:
#line 152 "src/port-uri.rl"
	{ host.start   = p; }
#line 161 "src/port-uri.rl"
	{ service.start = p; }
	goto st136;
st136:
	if ( ++p == pe )
		goto _test_eof136;
case 136:
#line 1034 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st137;
	} else if ( (*p) >= 65 )
		goto st137;
	goto st72;
st137:
	if ( ++p == pe )
		goto _test_eof137;
case 137:
	switch( (*p) ) {
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st138;
	} else if ( (*p) >= 65 )
		goto st138;
	goto st72;
st138:
	if ( ++p == pe )
		goto _test_eof138;
case 138:
	switch( (*p) ) {
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st139;
	} else if ( (*p) >= 65 )
		goto st139;
	goto st72;
st139:
	if ( ++p == pe )
		goto _test_eof139;
case 139:
	switch( (*p) ) {
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st140;
	} else if ( (*p) >= 65 )
		goto st140;
	goto st72;
st140:
	if ( ++p == pe )
		goto _test_eof140;
case 140:
	switch( (*p) ) {
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st141;
	} else if ( (*p) >= 65 )
		goto st141;
	goto st72;
st141:
	if ( ++p == pe )
		goto _test_eof141;
case 141:
	switch( (*p) ) {
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st142;
	} else if ( (*p) >= 65 )
		goto st142;
	goto st72;
st142:
	if ( ++p == pe )
		goto _test_eof142;
case 142:
	switch( (*p) ) {
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st143;
	} else if ( (*p) >= 65 )
		goto st143;
	goto st72;
st143:
	if ( ++p == pe )
		goto _test_eof143;
case 143:
	switch( (*p) ) {
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st144;
	} else if ( (*p) >= 65 )
		goto st144;
	goto st72;
st144:
	if ( ++p == pe )
		goto _test_eof144;
case 144:
	switch( (*p) ) {
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st145;
	} else if ( (*p) >= 65 )
		goto st145;
	goto st72;
st145:
	if ( ++p == pe )
		goto _test_eof145;
case 145:
	switch( (*p) ) {
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st146;
	} else if ( (*p) >= 65 )
		goto st146;
	goto st72;
st146:
	if ( ++p == pe )
		goto _test_eof146;
case 146:
	switch( (*p) ) {
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st147;
	} else if ( (*p) >= 65 )
		goto st147;
	goto st72;
st147:
	if ( ++p == pe )
		goto _test_eof147;
case 147:
	switch( (*p) ) {
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st148;
	} else if ( (*p) >= 65 )
		goto st148;
	goto st72;
st148:
	if ( ++p == pe )
		goto _test_eof148;
case 148:
	switch( (*p) ) {
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st149;
	} else if ( (*p) >= 65 )
		goto st149;
	goto st72;
st149:
	if ( ++p == pe )
		goto _test_eof149;
case 149:
	switch( (*p) ) {
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st150;
	} else if ( (*p) >= 65 )
		goto st150;
	goto st72;
st150:
	if ( ++p == pe )
		goto _test_eof150;
case 150:
	switch( (*p) ) {
		case 58: goto tr88;
		case 63: goto st0;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st151;
	} else if ( (*p) >= 65 )
		goto st151;
	goto st72;
st151:
	if ( ++p == pe )
		goto _test_eof151;
case 151:
	switch( (*p) ) {
		case 58: goto tr88;
		case 63: goto st0;
	}
	goto st72;
tr6:
#line 152 "src/port-uri.rl"
	{ host.start   = p; }
#line 147 "src/port-uri.rl"
	{ ip6.start = p + 1; }
	goto st152;
st152:
	if ( ++p == pe )
		goto _test_eof152;
case 152:
#line 1260 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto tr167;
		case 63: goto st0;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st153;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st153;
	} else
		goto st153;
	goto st72;
st153:
	if ( ++p == pe )
		goto _test_eof153;
case 153:
	switch( (*p) ) {
		case 58: goto tr169;
		case 63: goto st0;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st154;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st154;
	} else
		goto st154;
	goto st72;
st154:
	if ( ++p == pe )
		goto _test_eof154;
case 154:
	switch( (*p) ) {
		case 58: goto tr169;
		case 63: goto st0;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st155;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st155;
	} else
		goto st155;
	goto st72;
st155:
	if ( ++p == pe )
		goto _test_eof155;
case 155:
	switch( (*p) ) {
		case 58: goto tr169;
		case 63: goto st0;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st156;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st156;
	} else
		goto st156;
	goto st72;
st156:
	if ( ++p == pe )
		goto _test_eof156;
case 156:
	switch( (*p) ) {
		case 58: goto tr169;
		case 63: goto st0;
	}
	goto st72;
tr169:
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
	goto st4;
st4:
	if ( ++p == pe )
		goto _test_eof4;
case 4:
#line 1342 "src/port-uri.cc"
	switch( (*p) ) {
		case 48: goto st5;
		case 58: goto st9;
		case 93: goto st157;
	}
	if ( (*p) < 71 ) {
		if ( (*p) > 57 ) {
			if ( 65 <= (*p) && (*p) <= 70 )
				goto tr16;
		} else if ( (*p) >= 49 )
			goto tr14;
	} else if ( (*p) > 90 ) {
		if ( (*p) > 102 ) {
			if ( 103 <= (*p) && (*p) <= 122 )
				goto tr9;
		} else if ( (*p) >= 97 )
			goto tr16;
	} else
		goto tr9;
	goto st0;
st5:
	if ( ++p == pe )
		goto _test_eof5;
case 5:
	switch( (*p) ) {
		case 58: goto st9;
		case 93: goto st157;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st6;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st6;
	} else
		goto st6;
	goto st0;
st6:
	if ( ++p == pe )
		goto _test_eof6;
case 6:
	switch( (*p) ) {
		case 58: goto st9;
		case 93: goto st157;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st7;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st7;
	} else
		goto st7;
	goto st0;
st7:
	if ( ++p == pe )
		goto _test_eof7;
case 7:
	switch( (*p) ) {
		case 58: goto st9;
		case 93: goto st157;
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
		case 58: goto st9;
		case 93: goto st157;
	}
	goto st0;
st9:
	if ( ++p == pe )
		goto _test_eof9;
case 9:
	switch( (*p) ) {
		case 58: goto st14;
		case 93: goto st157;
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
		case 58: goto st14;
		case 93: goto st157;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st11;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st11;
	} else
		goto st11;
	goto st0;
st11:
	if ( ++p == pe )
		goto _test_eof11;
case 11:
	switch( (*p) ) {
		case 58: goto st14;
		case 93: goto st157;
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
		case 58: goto st14;
		case 93: goto st157;
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
		case 58: goto st14;
		case 93: goto st157;
	}
	goto st0;
st14:
	if ( ++p == pe )
		goto _test_eof14;
case 14:
	switch( (*p) ) {
		case 58: goto st19;
		case 93: goto st157;
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
		case 58: goto st19;
		case 93: goto st157;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st16;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st16;
	} else
		goto st16;
	goto st0;
st16:
	if ( ++p == pe )
		goto _test_eof16;
case 16:
	switch( (*p) ) {
		case 58: goto st19;
		case 93: goto st157;
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
		case 58: goto st19;
		case 93: goto st157;
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
		case 58: goto st19;
		case 93: goto st157;
	}
	goto st0;
st19:
	if ( ++p == pe )
		goto _test_eof19;
case 19:
	switch( (*p) ) {
		case 58: goto st24;
		case 93: goto st157;
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
		case 58: goto st24;
		case 93: goto st157;
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
st21:
	if ( ++p == pe )
		goto _test_eof21;
case 21:
	switch( (*p) ) {
		case 58: goto st24;
		case 93: goto st157;
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
		case 58: goto st24;
		case 93: goto st157;
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
		case 58: goto st24;
		case 93: goto st157;
	}
	goto st0;
st24:
	if ( ++p == pe )
		goto _test_eof24;
case 24:
	switch( (*p) ) {
		case 58: goto st29;
		case 93: goto st157;
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
		case 58: goto st29;
		case 93: goto st157;
	}
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
	switch( (*p) ) {
		case 58: goto st29;
		case 93: goto st157;
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
		case 58: goto st29;
		case 93: goto st157;
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
		case 58: goto st29;
		case 93: goto st157;
	}
	goto st0;
st29:
	if ( ++p == pe )
		goto _test_eof29;
case 29:
	switch( (*p) ) {
		case 58: goto st34;
		case 93: goto st157;
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
		case 58: goto st34;
		case 93: goto st157;
	}
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
	switch( (*p) ) {
		case 58: goto st34;
		case 93: goto st157;
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
		case 58: goto st34;
		case 93: goto st157;
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
		case 58: goto st34;
		case 93: goto st157;
	}
	goto st0;
st34:
	if ( ++p == pe )
		goto _test_eof34;
case 34:
	switch( (*p) ) {
		case 58: goto st39;
		case 93: goto st157;
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
		case 58: goto st39;
		case 93: goto st157;
	}
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
	switch( (*p) ) {
		case 58: goto st39;
		case 93: goto st157;
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
		case 58: goto st39;
		case 93: goto st157;
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
		case 58: goto st39;
		case 93: goto st157;
	}
	goto st0;
st39:
	if ( ++p == pe )
		goto _test_eof39;
case 39:
	if ( (*p) == 93 )
		goto st157;
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
	if ( (*p) == 93 )
		goto st157;
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
	if ( (*p) == 93 )
		goto st157;
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
		goto st157;
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
		goto st157;
	goto st0;
st157:
	if ( ++p == pe )
		goto _test_eof157;
case 157:
	if ( (*p) == 58 )
		goto tr172;
	goto st0;
tr14:
#line 161 "src/port-uri.rl"
	{ service.start = p; }
#line 156 "src/port-uri.rl"
	{ dport.start   = p; port = 0; }
#line 157 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st158;
st158:
	if ( ++p == pe )
		goto _test_eof158;
case 158:
#line 1971 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto st9;
		case 93: goto st157;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr173;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st6;
	} else
		goto st6;
	goto st0;
tr173:
#line 157 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st159;
st159:
	if ( ++p == pe )
		goto _test_eof159;
case 159:
#line 1993 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto st9;
		case 93: goto st157;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr174;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st7;
	} else
		goto st7;
	goto st0;
tr174:
#line 157 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st160;
st160:
	if ( ++p == pe )
		goto _test_eof160;
case 160:
#line 2015 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto st9;
		case 93: goto st157;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr175;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st8;
	} else
		goto st8;
	goto st0;
tr175:
#line 157 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st161;
st161:
	if ( ++p == pe )
		goto _test_eof161;
case 161:
#line 2037 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto st9;
		case 93: goto st157;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr89;
	goto st0;
tr16:
#line 161 "src/port-uri.rl"
	{ service.start = p; }
	goto st162;
st162:
	if ( ++p == pe )
		goto _test_eof162;
case 162:
#line 2053 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto st9;
		case 93: goto st157;
	}
	if ( (*p) < 71 ) {
		if ( (*p) > 57 ) {
			if ( 65 <= (*p) && (*p) <= 70 )
				goto st163;
		} else if ( (*p) >= 48 )
			goto st6;
	} else if ( (*p) > 90 ) {
		if ( (*p) > 102 ) {
			if ( 103 <= (*p) && (*p) <= 122 )
				goto st75;
		} else if ( (*p) >= 97 )
			goto st163;
	} else
		goto st75;
	goto st0;
st163:
	if ( ++p == pe )
		goto _test_eof163;
case 163:
	switch( (*p) ) {
		case 58: goto st9;
		case 93: goto st157;
	}
	if ( (*p) < 71 ) {
		if ( (*p) > 57 ) {
			if ( 65 <= (*p) && (*p) <= 70 )
				goto st164;
		} else if ( (*p) >= 48 )
			goto st7;
	} else if ( (*p) > 90 ) {
		if ( (*p) > 102 ) {
			if ( 103 <= (*p) && (*p) <= 122 )
				goto st76;
		} else if ( (*p) >= 97 )
			goto st164;
	} else
		goto st76;
	goto st0;
st164:
	if ( ++p == pe )
		goto _test_eof164;
case 164:
	switch( (*p) ) {
		case 58: goto st9;
		case 93: goto st157;
	}
	if ( (*p) < 71 ) {
		if ( (*p) > 57 ) {
			if ( 65 <= (*p) && (*p) <= 70 )
				goto st165;
		} else if ( (*p) >= 48 )
			goto st8;
	} else if ( (*p) > 90 ) {
		if ( (*p) > 102 ) {
			if ( 103 <= (*p) && (*p) <= 122 )
				goto st77;
		} else if ( (*p) >= 97 )
			goto st165;
	} else
		goto st77;
	goto st0;
st165:
	if ( ++p == pe )
		goto _test_eof165;
case 165:
	switch( (*p) ) {
		case 58: goto st9;
		case 93: goto st157;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st78;
	} else if ( (*p) >= 65 )
		goto st78;
	goto st0;
tr167:
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
	goto st44;
st44:
	if ( ++p == pe )
		goto _test_eof44;
case 44:
#line 2141 "src/port-uri.cc"
	switch( (*p) ) {
		case 48: goto st5;
		case 58: goto st45;
		case 93: goto st157;
	}
	if ( (*p) < 71 ) {
		if ( (*p) > 57 ) {
			if ( 65 <= (*p) && (*p) <= 70 )
				goto tr16;
		} else if ( (*p) >= 49 )
			goto tr14;
	} else if ( (*p) > 90 ) {
		if ( (*p) > 102 ) {
			if ( 103 <= (*p) && (*p) <= 122 )
				goto tr9;
		} else if ( (*p) >= 97 )
			goto tr16;
	} else
		goto tr9;
	goto st0;
st45:
	if ( ++p == pe )
		goto _test_eof45;
case 45:
	switch( (*p) ) {
		case 58: goto st14;
		case 70: goto st46;
		case 93: goto st157;
		case 102: goto st46;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st10;
	} else if ( (*p) > 69 ) {
		if ( 97 <= (*p) && (*p) <= 101 )
			goto st10;
	} else
		goto st10;
	goto st0;
st46:
	if ( ++p == pe )
		goto _test_eof46;
case 46:
	switch( (*p) ) {
		case 58: goto st14;
		case 70: goto st47;
		case 93: goto st157;
		case 102: goto st47;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st11;
	} else if ( (*p) > 69 ) {
		if ( 97 <= (*p) && (*p) <= 101 )
			goto st11;
	} else
		goto st11;
	goto st0;
st47:
	if ( ++p == pe )
		goto _test_eof47;
case 47:
	switch( (*p) ) {
		case 58: goto st14;
		case 70: goto st48;
		case 93: goto st157;
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
		case 58: goto st14;
		case 70: goto st49;
		case 93: goto st157;
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
		case 58: goto st50;
		case 93: goto st157;
	}
	goto st0;
st50:
	if ( ++p == pe )
		goto _test_eof50;
case 50:
	switch( (*p) ) {
		case 58: goto st19;
		case 93: goto st157;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr61;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st15;
	} else
		goto st15;
	goto st0;
tr61:
#line 138 "src/port-uri.rl"
	{ ip4.start = p; }
	goto st51;
st51:
	if ( ++p == pe )
		goto _test_eof51;
case 51:
#line 2272 "src/port-uri.cc"
	switch( (*p) ) {
		case 46: goto st52;
		case 58: goto st19;
		case 93: goto st157;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st64;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st16;
	} else
		goto st16;
	goto st0;
st52:
	if ( ++p == pe )
		goto _test_eof52;
case 52:
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st53;
	goto st0;
st53:
	if ( ++p == pe )
		goto _test_eof53;
case 53:
	if ( (*p) == 46 )
		goto st54;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st62;
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
		goto st60;
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
	if ( (*p) == 93 )
		goto tr72;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st58;
	goto st0;
st58:
	if ( ++p == pe )
		goto _test_eof58;
case 58:
	if ( (*p) == 93 )
		goto tr72;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st59;
	goto st0;
st59:
	if ( ++p == pe )
		goto _test_eof59;
case 59:
	if ( (*p) == 93 )
		goto tr72;
	goto st0;
tr72:
#line 139 "src/port-uri.rl"
	{ ip4.end   = p; }
	goto st166;
st166:
	if ( ++p == pe )
		goto _test_eof166;
case 166:
#line 2359 "src/port-uri.cc"
	if ( (*p) == 58 )
		goto tr88;
	goto st0;
st60:
	if ( ++p == pe )
		goto _test_eof60;
case 60:
	if ( (*p) == 46 )
		goto st56;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st61;
	goto st0;
st61:
	if ( ++p == pe )
		goto _test_eof61;
case 61:
	if ( (*p) == 46 )
		goto st56;
	goto st0;
st62:
	if ( ++p == pe )
		goto _test_eof62;
case 62:
	if ( (*p) == 46 )
		goto st54;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st63;
	goto st0;
st63:
	if ( ++p == pe )
		goto _test_eof63;
case 63:
	if ( (*p) == 46 )
		goto st54;
	goto st0;
st64:
	if ( ++p == pe )
		goto _test_eof64;
case 64:
	switch( (*p) ) {
		case 46: goto st52;
		case 58: goto st19;
		case 93: goto st157;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st65;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st17;
	} else
		goto st17;
	goto st0;
st65:
	if ( ++p == pe )
		goto _test_eof65;
case 65:
	switch( (*p) ) {
		case 46: goto st52;
		case 58: goto st19;
		case 93: goto st157;
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
tr3:
#line 130 "src/port-uri.rl"
	{ login.start  = p; }
#line 152 "src/port-uri.rl"
	{ host.start   = p; }
#line 138 "src/port-uri.rl"
	{ ip4.start = p; }
#line 166 "src/port-uri.rl"
	{ sport.start   = p; port = 0; }
#line 167 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st167;
st167:
	if ( ++p == pe )
		goto _test_eof167;
case 167:
#line 2447 "src/port-uri.cc"
	switch( (*p) ) {
		case 46: goto st112;
		case 58: goto tr88;
		case 63: goto st0;
		case 64: goto tr128;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr179;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st126;
	} else
		goto st126;
	goto st72;
tr179:
#line 167 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st168;
st168:
	if ( ++p == pe )
		goto _test_eof168;
case 168:
#line 2471 "src/port-uri.cc"
	switch( (*p) ) {
		case 46: goto st112;
		case 58: goto tr88;
		case 63: goto st0;
		case 64: goto tr128;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr180;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st126;
	} else
		goto st126;
	goto st72;
tr180:
#line 167 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st169;
st169:
	if ( ++p == pe )
		goto _test_eof169;
case 169:
#line 2495 "src/port-uri.cc"
	switch( (*p) ) {
		case 46: goto st112;
		case 58: goto tr88;
		case 63: goto st0;
		case 64: goto tr128;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr181;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st126;
	} else
		goto st126;
	goto st72;
tr181:
#line 167 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st170;
st170:
	if ( ++p == pe )
		goto _test_eof170;
case 170:
#line 2519 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto tr88;
		case 63: goto st0;
		case 64: goto tr128;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr181;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st126;
	} else
		goto st126;
	goto st72;
tr5:
#line 126 "src/port-uri.rl"
	{ schema.start = p; }
#line 130 "src/port-uri.rl"
	{ login.start  = p; }
#line 152 "src/port-uri.rl"
	{ host.start   = p; }
	goto st171;
st171:
	if ( ++p == pe )
		goto _test_eof171;
case 171:
#line 2546 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto tr182;
		case 63: goto st0;
		case 64: goto tr128;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st126;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st171;
	} else
		goto st171;
	goto st72;
tr182:
#line 127 "src/port-uri.rl"
	{ schema.end   = p; }
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
	goto st66;
st66:
	if ( ++p == pe )
		goto _test_eof66;
case 66:
#line 2571 "src/port-uri.cc"
	if ( (*p) == 47 )
		goto st67;
	if ( (*p) < 65 ) {
		if ( 49 <= (*p) && (*p) <= 57 )
			goto tr8;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr9;
	} else
		goto tr9;
	goto st0;
st67:
	if ( ++p == pe )
		goto _test_eof67;
case 67:
	if ( (*p) == 47 )
		goto st68;
	goto st0;
st68:
	if ( ++p == pe )
		goto _test_eof68;
case 68:
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
			goto tr79;
	} else
		goto tr79;
	goto tr0;
tr7:
#line 126 "src/port-uri.rl"
	{ schema.start = p; }
#line 130 "src/port-uri.rl"
	{ login.start  = p; }
#line 152 "src/port-uri.rl"
	{ host.start   = p; }
	goto st172;
st172:
	if ( ++p == pe )
		goto _test_eof172;
case 172:
#line 2620 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto tr182;
		case 63: goto st0;
		case 64: goto tr128;
		case 110: goto st173;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st126;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st171;
	} else
		goto st171;
	goto st72;
st173:
	if ( ++p == pe )
		goto _test_eof173;
case 173:
	switch( (*p) ) {
		case 58: goto tr182;
		case 63: goto st0;
		case 64: goto tr128;
		case 105: goto st174;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st126;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st171;
	} else
		goto st171;
	goto st72;
st174:
	if ( ++p == pe )
		goto _test_eof174;
case 174:
	switch( (*p) ) {
		case 58: goto tr182;
		case 63: goto st0;
		case 64: goto tr128;
		case 120: goto st175;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st126;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st171;
	} else
		goto st171;
	goto st72;
st175:
	if ( ++p == pe )
		goto _test_eof175;
case 175:
	switch( (*p) ) {
		case 58: goto tr187;
		case 63: goto st0;
		case 64: goto tr128;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st126;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st171;
	} else
		goto st171;
	goto st72;
tr187:
#line 127 "src/port-uri.rl"
	{ schema.end   = p; }
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
	goto st69;
st69:
	if ( ++p == pe )
		goto _test_eof69;
case 69:
#line 2702 "src/port-uri.cc"
	if ( (*p) == 47 )
		goto st70;
	if ( (*p) < 65 ) {
		if ( 49 <= (*p) && (*p) <= 57 )
			goto tr8;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr9;
	} else
		goto tr9;
	goto st0;
st70:
	if ( ++p == pe )
		goto _test_eof70;
case 70:
	if ( (*p) == 47 )
		goto st71;
	goto st0;
st71:
	if ( ++p == pe )
		goto _test_eof71;
case 71:
	switch( (*p) ) {
		case 58: goto tr84;
		case 63: goto tr84;
		case 91: goto tr86;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr83;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr85;
	} else
		goto tr85;
	goto tr82;
tr82:
#line 175 "src/port-uri.rl"
	{ path.start = p; }
#line 152 "src/port-uri.rl"
	{ host.start   = p; }
	goto st176;
st176:
	if ( ++p == pe )
		goto _test_eof176;
case 176:
#line 2749 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto tr189;
		case 63: goto st178;
	}
	goto st176;
tr189:
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
	goto st177;
tr221:
#line 139 "src/port-uri.rl"
	{ ip4.end   = p; }
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
	goto st177;
tr300:
#line 148 "src/port-uri.rl"
	{ ip6.end   = p - 1; }
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
	goto st177;
st177:
	if ( ++p == pe )
		goto _test_eof177;
case 177:
#line 2775 "src/port-uri.cc"
	if ( (*p) < 65 ) {
		if ( 49 <= (*p) && (*p) <= 57 )
			goto tr191;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr192;
	} else
		goto tr192;
	goto st178;
tr84:
#line 175 "src/port-uri.rl"
	{ path.start = p; }
	goto st178;
st178:
	if ( ++p == pe )
		goto _test_eof178;
case 178:
#line 2793 "src/port-uri.cc"
	goto st178;
tr191:
#line 161 "src/port-uri.rl"
	{ service.start = p; }
#line 156 "src/port-uri.rl"
	{ dport.start   = p; port = 0; }
#line 157 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st179;
tr193:
#line 157 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st179;
st179:
	if ( ++p == pe )
		goto _test_eof179;
case 179:
#line 2811 "src/port-uri.cc"
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr193;
	goto st178;
tr192:
#line 161 "src/port-uri.rl"
	{ service.start = p; }
	goto st180;
st180:
	if ( ++p == pe )
		goto _test_eof180;
case 180:
#line 2823 "src/port-uri.cc"
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st181;
	} else if ( (*p) >= 65 )
		goto st181;
	goto st178;
st181:
	if ( ++p == pe )
		goto _test_eof181;
case 181:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st182;
	} else if ( (*p) >= 65 )
		goto st182;
	goto st178;
st182:
	if ( ++p == pe )
		goto _test_eof182;
case 182:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st183;
	} else if ( (*p) >= 65 )
		goto st183;
	goto st178;
st183:
	if ( ++p == pe )
		goto _test_eof183;
case 183:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st184;
	} else if ( (*p) >= 65 )
		goto st184;
	goto st178;
st184:
	if ( ++p == pe )
		goto _test_eof184;
case 184:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st185;
	} else if ( (*p) >= 65 )
		goto st185;
	goto st178;
st185:
	if ( ++p == pe )
		goto _test_eof185;
case 185:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st186;
	} else if ( (*p) >= 65 )
		goto st186;
	goto st178;
st186:
	if ( ++p == pe )
		goto _test_eof186;
case 186:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st187;
	} else if ( (*p) >= 65 )
		goto st187;
	goto st178;
st187:
	if ( ++p == pe )
		goto _test_eof187;
case 187:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st188;
	} else if ( (*p) >= 65 )
		goto st188;
	goto st178;
st188:
	if ( ++p == pe )
		goto _test_eof188;
case 188:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st189;
	} else if ( (*p) >= 65 )
		goto st189;
	goto st178;
st189:
	if ( ++p == pe )
		goto _test_eof189;
case 189:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st190;
	} else if ( (*p) >= 65 )
		goto st190;
	goto st178;
st190:
	if ( ++p == pe )
		goto _test_eof190;
case 190:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st191;
	} else if ( (*p) >= 65 )
		goto st191;
	goto st178;
st191:
	if ( ++p == pe )
		goto _test_eof191;
case 191:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st192;
	} else if ( (*p) >= 65 )
		goto st192;
	goto st178;
st192:
	if ( ++p == pe )
		goto _test_eof192;
case 192:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st193;
	} else if ( (*p) >= 65 )
		goto st193;
	goto st178;
st193:
	if ( ++p == pe )
		goto _test_eof193;
case 193:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st194;
	} else if ( (*p) >= 65 )
		goto st194;
	goto st178;
st194:
	if ( ++p == pe )
		goto _test_eof194;
case 194:
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st195;
	} else if ( (*p) >= 65 )
		goto st195;
	goto st178;
st195:
	if ( ++p == pe )
		goto _test_eof195;
case 195:
	goto st178;
tr83:
#line 130 "src/port-uri.rl"
	{ login.start  = p; }
#line 175 "src/port-uri.rl"
	{ path.start = p; }
#line 152 "src/port-uri.rl"
	{ host.start   = p; }
#line 138 "src/port-uri.rl"
	{ ip4.start = p; }
	goto st196;
st196:
	if ( ++p == pe )
		goto _test_eof196;
case 196:
#line 2989 "src/port-uri.cc"
	switch( (*p) ) {
		case 46: goto st197;
		case 58: goto tr189;
		case 63: goto st178;
		case 64: goto tr211;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st209;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st211;
	} else
		goto st211;
	goto st176;
st197:
	if ( ++p == pe )
		goto _test_eof197;
case 197:
	switch( (*p) ) {
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st198;
	goto st176;
st198:
	if ( ++p == pe )
		goto _test_eof198;
case 198:
	switch( (*p) ) {
		case 46: goto st199;
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st207;
	goto st176;
st199:
	if ( ++p == pe )
		goto _test_eof199;
case 199:
	switch( (*p) ) {
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st200;
	goto st176;
st200:
	if ( ++p == pe )
		goto _test_eof200;
case 200:
	switch( (*p) ) {
		case 46: goto st201;
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st205;
	goto st176;
st201:
	if ( ++p == pe )
		goto _test_eof201;
case 201:
	switch( (*p) ) {
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st202;
	goto st176;
st202:
	if ( ++p == pe )
		goto _test_eof202;
case 202:
	switch( (*p) ) {
		case 58: goto tr221;
		case 63: goto st178;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st203;
	goto st176;
st203:
	if ( ++p == pe )
		goto _test_eof203;
case 203:
	switch( (*p) ) {
		case 58: goto tr221;
		case 63: goto st178;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st204;
	goto st176;
st204:
	if ( ++p == pe )
		goto _test_eof204;
case 204:
	switch( (*p) ) {
		case 58: goto tr221;
		case 63: goto st178;
	}
	goto st176;
st205:
	if ( ++p == pe )
		goto _test_eof205;
case 205:
	switch( (*p) ) {
		case 46: goto st201;
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st206;
	goto st176;
st206:
	if ( ++p == pe )
		goto _test_eof206;
case 206:
	switch( (*p) ) {
		case 46: goto st201;
		case 58: goto tr189;
		case 63: goto st178;
	}
	goto st176;
st207:
	if ( ++p == pe )
		goto _test_eof207;
case 207:
	switch( (*p) ) {
		case 46: goto st199;
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st208;
	goto st176;
st208:
	if ( ++p == pe )
		goto _test_eof208;
case 208:
	switch( (*p) ) {
		case 46: goto st199;
		case 58: goto tr189;
		case 63: goto st178;
	}
	goto st176;
st209:
	if ( ++p == pe )
		goto _test_eof209;
case 209:
	switch( (*p) ) {
		case 46: goto st197;
		case 58: goto tr189;
		case 63: goto st178;
		case 64: goto tr211;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st210;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st211;
	} else
		goto st211;
	goto st176;
st210:
	if ( ++p == pe )
		goto _test_eof210;
case 210:
	switch( (*p) ) {
		case 46: goto st197;
		case 58: goto tr189;
		case 63: goto st178;
		case 64: goto tr211;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st211;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st211;
	} else
		goto st211;
	goto st176;
tr85:
#line 130 "src/port-uri.rl"
	{ login.start  = p; }
#line 175 "src/port-uri.rl"
	{ path.start = p; }
#line 152 "src/port-uri.rl"
	{ host.start   = p; }
	goto st211;
st211:
	if ( ++p == pe )
		goto _test_eof211;
case 211:
#line 3187 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto tr189;
		case 63: goto st178;
		case 64: goto tr211;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st211;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st211;
	} else
		goto st211;
	goto st176;
tr211:
#line 131 "src/port-uri.rl"
	{ login.end    = p; }
	goto st212;
st212:
	if ( ++p == pe )
		goto _test_eof212;
case 212:
#line 3210 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr226;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr226;
	} else
		goto tr226;
	goto st176;
tr226:
#line 134 "src/port-uri.rl"
	{ password.start = p; }
	goto st213;
st213:
	if ( ++p == pe )
		goto _test_eof213;
case 213:
#line 3232 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto tr228;
		case 63: goto st178;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st213;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st213;
	} else
		goto st213;
	goto st176;
tr228:
#line 135 "src/port-uri.rl"
	{ password.end   = p; }
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
	goto st214;
st214:
	if ( ++p == pe )
		goto _test_eof214;
case 214:
#line 3256 "src/port-uri.cc"
	switch( (*p) ) {
		case 48: goto tr229;
		case 58: goto tr84;
		case 63: goto tr84;
		case 91: goto tr86;
	}
	if ( (*p) < 65 ) {
		if ( 49 <= (*p) && (*p) <= 57 )
			goto tr230;
	} else if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto tr231;
	} else
		goto tr231;
	goto tr82;
tr229:
#line 175 "src/port-uri.rl"
	{ path.start = p; }
#line 152 "src/port-uri.rl"
	{ host.start   = p; }
#line 138 "src/port-uri.rl"
	{ ip4.start = p; }
	goto st215;
st215:
	if ( ++p == pe )
		goto _test_eof215;
case 215:
#line 3284 "src/port-uri.cc"
	switch( (*p) ) {
		case 46: goto st197;
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st216;
	goto st176;
st216:
	if ( ++p == pe )
		goto _test_eof216;
case 216:
	switch( (*p) ) {
		case 46: goto st197;
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st217;
	goto st176;
st217:
	if ( ++p == pe )
		goto _test_eof217;
case 217:
	switch( (*p) ) {
		case 46: goto st197;
		case 58: goto tr189;
		case 63: goto st178;
	}
	goto st176;
tr230:
#line 175 "src/port-uri.rl"
	{ path.start = p; }
#line 152 "src/port-uri.rl"
	{ host.start   = p; }
#line 138 "src/port-uri.rl"
	{ ip4.start = p; }
#line 161 "src/port-uri.rl"
	{ service.start = p; }
#line 156 "src/port-uri.rl"
	{ dport.start   = p; port = 0; }
#line 157 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st218;
st218:
	if ( ++p == pe )
		goto _test_eof218;
case 218:
#line 3333 "src/port-uri.cc"
	switch( (*p) ) {
		case 46: goto st197;
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr234;
	goto st176;
tr234:
#line 157 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st219;
st219:
	if ( ++p == pe )
		goto _test_eof219;
case 219:
#line 3350 "src/port-uri.cc"
	switch( (*p) ) {
		case 46: goto st197;
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr235;
	goto st176;
tr235:
#line 157 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st220;
st220:
	if ( ++p == pe )
		goto _test_eof220;
case 220:
#line 3367 "src/port-uri.cc"
	switch( (*p) ) {
		case 46: goto st197;
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr236;
	goto st176;
tr236:
#line 157 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st221;
st221:
	if ( ++p == pe )
		goto _test_eof221;
case 221:
#line 3384 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr236;
	goto st176;
tr231:
#line 175 "src/port-uri.rl"
	{ path.start = p; }
#line 152 "src/port-uri.rl"
	{ host.start   = p; }
#line 161 "src/port-uri.rl"
	{ service.start = p; }
	goto st222;
st222:
	if ( ++p == pe )
		goto _test_eof222;
case 222:
#line 3404 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st223;
	} else if ( (*p) >= 65 )
		goto st223;
	goto st176;
st223:
	if ( ++p == pe )
		goto _test_eof223;
case 223:
	switch( (*p) ) {
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st224;
	} else if ( (*p) >= 65 )
		goto st224;
	goto st176;
st224:
	if ( ++p == pe )
		goto _test_eof224;
case 224:
	switch( (*p) ) {
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st225;
	} else if ( (*p) >= 65 )
		goto st225;
	goto st176;
st225:
	if ( ++p == pe )
		goto _test_eof225;
case 225:
	switch( (*p) ) {
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st226;
	} else if ( (*p) >= 65 )
		goto st226;
	goto st176;
st226:
	if ( ++p == pe )
		goto _test_eof226;
case 226:
	switch( (*p) ) {
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st227;
	} else if ( (*p) >= 65 )
		goto st227;
	goto st176;
st227:
	if ( ++p == pe )
		goto _test_eof227;
case 227:
	switch( (*p) ) {
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st228;
	} else if ( (*p) >= 65 )
		goto st228;
	goto st176;
st228:
	if ( ++p == pe )
		goto _test_eof228;
case 228:
	switch( (*p) ) {
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st229;
	} else if ( (*p) >= 65 )
		goto st229;
	goto st176;
st229:
	if ( ++p == pe )
		goto _test_eof229;
case 229:
	switch( (*p) ) {
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st230;
	} else if ( (*p) >= 65 )
		goto st230;
	goto st176;
st230:
	if ( ++p == pe )
		goto _test_eof230;
case 230:
	switch( (*p) ) {
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st231;
	} else if ( (*p) >= 65 )
		goto st231;
	goto st176;
st231:
	if ( ++p == pe )
		goto _test_eof231;
case 231:
	switch( (*p) ) {
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st232;
	} else if ( (*p) >= 65 )
		goto st232;
	goto st176;
st232:
	if ( ++p == pe )
		goto _test_eof232;
case 232:
	switch( (*p) ) {
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st233;
	} else if ( (*p) >= 65 )
		goto st233;
	goto st176;
st233:
	if ( ++p == pe )
		goto _test_eof233;
case 233:
	switch( (*p) ) {
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st234;
	} else if ( (*p) >= 65 )
		goto st234;
	goto st176;
st234:
	if ( ++p == pe )
		goto _test_eof234;
case 234:
	switch( (*p) ) {
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st235;
	} else if ( (*p) >= 65 )
		goto st235;
	goto st176;
st235:
	if ( ++p == pe )
		goto _test_eof235;
case 235:
	switch( (*p) ) {
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st236;
	} else if ( (*p) >= 65 )
		goto st236;
	goto st176;
st236:
	if ( ++p == pe )
		goto _test_eof236;
case 236:
	switch( (*p) ) {
		case 58: goto tr189;
		case 63: goto st178;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st237;
	} else if ( (*p) >= 65 )
		goto st237;
	goto st176;
st237:
	if ( ++p == pe )
		goto _test_eof237;
case 237:
	switch( (*p) ) {
		case 58: goto tr189;
		case 63: goto st178;
	}
	goto st176;
tr86:
#line 175 "src/port-uri.rl"
	{ path.start = p; }
#line 152 "src/port-uri.rl"
	{ host.start   = p; }
#line 147 "src/port-uri.rl"
	{ ip6.start = p + 1; }
	goto st238;
st238:
	if ( ++p == pe )
		goto _test_eof238;
case 238:
#line 3632 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto tr253;
		case 63: goto st178;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st239;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st239;
	} else
		goto st239;
	goto st176;
st239:
	if ( ++p == pe )
		goto _test_eof239;
case 239:
	switch( (*p) ) {
		case 58: goto tr255;
		case 63: goto st178;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st240;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st240;
	} else
		goto st240;
	goto st176;
st240:
	if ( ++p == pe )
		goto _test_eof240;
case 240:
	switch( (*p) ) {
		case 58: goto tr255;
		case 63: goto st178;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st241;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st241;
	} else
		goto st241;
	goto st176;
st241:
	if ( ++p == pe )
		goto _test_eof241;
case 241:
	switch( (*p) ) {
		case 58: goto tr255;
		case 63: goto st178;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st242;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st242;
	} else
		goto st242;
	goto st176;
st242:
	if ( ++p == pe )
		goto _test_eof242;
case 242:
	switch( (*p) ) {
		case 58: goto tr255;
		case 63: goto st178;
	}
	goto st176;
tr255:
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
	goto st243;
st243:
	if ( ++p == pe )
		goto _test_eof243;
case 243:
#line 3714 "src/port-uri.cc"
	switch( (*p) ) {
		case 48: goto st244;
		case 58: goto st248;
		case 93: goto st283;
	}
	if ( (*p) < 71 ) {
		if ( (*p) > 57 ) {
			if ( 65 <= (*p) && (*p) <= 70 )
				goto tr261;
		} else if ( (*p) >= 49 )
			goto tr259;
	} else if ( (*p) > 90 ) {
		if ( (*p) > 102 ) {
			if ( 103 <= (*p) && (*p) <= 122 )
				goto tr192;
		} else if ( (*p) >= 97 )
			goto tr261;
	} else
		goto tr192;
	goto st178;
st244:
	if ( ++p == pe )
		goto _test_eof244;
case 244:
	switch( (*p) ) {
		case 58: goto st248;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st245;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st245;
	} else
		goto st245;
	goto st178;
st245:
	if ( ++p == pe )
		goto _test_eof245;
case 245:
	switch( (*p) ) {
		case 58: goto st248;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st246;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st246;
	} else
		goto st246;
	goto st178;
st246:
	if ( ++p == pe )
		goto _test_eof246;
case 246:
	switch( (*p) ) {
		case 58: goto st248;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st247;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st247;
	} else
		goto st247;
	goto st178;
st247:
	if ( ++p == pe )
		goto _test_eof247;
case 247:
	switch( (*p) ) {
		case 58: goto st248;
		case 93: goto st283;
	}
	goto st178;
st248:
	if ( ++p == pe )
		goto _test_eof248;
case 248:
	switch( (*p) ) {
		case 58: goto st253;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st249;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st249;
	} else
		goto st249;
	goto st178;
st249:
	if ( ++p == pe )
		goto _test_eof249;
case 249:
	switch( (*p) ) {
		case 58: goto st253;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st250;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st250;
	} else
		goto st250;
	goto st178;
st250:
	if ( ++p == pe )
		goto _test_eof250;
case 250:
	switch( (*p) ) {
		case 58: goto st253;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st251;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st251;
	} else
		goto st251;
	goto st178;
st251:
	if ( ++p == pe )
		goto _test_eof251;
case 251:
	switch( (*p) ) {
		case 58: goto st253;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st252;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st252;
	} else
		goto st252;
	goto st178;
st252:
	if ( ++p == pe )
		goto _test_eof252;
case 252:
	switch( (*p) ) {
		case 58: goto st253;
		case 93: goto st283;
	}
	goto st178;
st253:
	if ( ++p == pe )
		goto _test_eof253;
case 253:
	switch( (*p) ) {
		case 58: goto st258;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st254;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st254;
	} else
		goto st254;
	goto st178;
st254:
	if ( ++p == pe )
		goto _test_eof254;
case 254:
	switch( (*p) ) {
		case 58: goto st258;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st255;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st255;
	} else
		goto st255;
	goto st178;
st255:
	if ( ++p == pe )
		goto _test_eof255;
case 255:
	switch( (*p) ) {
		case 58: goto st258;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st256;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st256;
	} else
		goto st256;
	goto st178;
st256:
	if ( ++p == pe )
		goto _test_eof256;
case 256:
	switch( (*p) ) {
		case 58: goto st258;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st257;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st257;
	} else
		goto st257;
	goto st178;
st257:
	if ( ++p == pe )
		goto _test_eof257;
case 257:
	switch( (*p) ) {
		case 58: goto st258;
		case 93: goto st283;
	}
	goto st178;
st258:
	if ( ++p == pe )
		goto _test_eof258;
case 258:
	switch( (*p) ) {
		case 58: goto st263;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st259;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st259;
	} else
		goto st259;
	goto st178;
st259:
	if ( ++p == pe )
		goto _test_eof259;
case 259:
	switch( (*p) ) {
		case 58: goto st263;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st260;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st260;
	} else
		goto st260;
	goto st178;
st260:
	if ( ++p == pe )
		goto _test_eof260;
case 260:
	switch( (*p) ) {
		case 58: goto st263;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st261;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st261;
	} else
		goto st261;
	goto st178;
st261:
	if ( ++p == pe )
		goto _test_eof261;
case 261:
	switch( (*p) ) {
		case 58: goto st263;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st262;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st262;
	} else
		goto st262;
	goto st178;
st262:
	if ( ++p == pe )
		goto _test_eof262;
case 262:
	switch( (*p) ) {
		case 58: goto st263;
		case 93: goto st283;
	}
	goto st178;
st263:
	if ( ++p == pe )
		goto _test_eof263;
case 263:
	switch( (*p) ) {
		case 58: goto st268;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st264;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st264;
	} else
		goto st264;
	goto st178;
st264:
	if ( ++p == pe )
		goto _test_eof264;
case 264:
	switch( (*p) ) {
		case 58: goto st268;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st265;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st265;
	} else
		goto st265;
	goto st178;
st265:
	if ( ++p == pe )
		goto _test_eof265;
case 265:
	switch( (*p) ) {
		case 58: goto st268;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st266;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st266;
	} else
		goto st266;
	goto st178;
st266:
	if ( ++p == pe )
		goto _test_eof266;
case 266:
	switch( (*p) ) {
		case 58: goto st268;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st267;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st267;
	} else
		goto st267;
	goto st178;
st267:
	if ( ++p == pe )
		goto _test_eof267;
case 267:
	switch( (*p) ) {
		case 58: goto st268;
		case 93: goto st283;
	}
	goto st178;
st268:
	if ( ++p == pe )
		goto _test_eof268;
case 268:
	switch( (*p) ) {
		case 58: goto st273;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st269;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st269;
	} else
		goto st269;
	goto st178;
st269:
	if ( ++p == pe )
		goto _test_eof269;
case 269:
	switch( (*p) ) {
		case 58: goto st273;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st270;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st270;
	} else
		goto st270;
	goto st178;
st270:
	if ( ++p == pe )
		goto _test_eof270;
case 270:
	switch( (*p) ) {
		case 58: goto st273;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st271;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st271;
	} else
		goto st271;
	goto st178;
st271:
	if ( ++p == pe )
		goto _test_eof271;
case 271:
	switch( (*p) ) {
		case 58: goto st273;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st272;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st272;
	} else
		goto st272;
	goto st178;
st272:
	if ( ++p == pe )
		goto _test_eof272;
case 272:
	switch( (*p) ) {
		case 58: goto st273;
		case 93: goto st283;
	}
	goto st178;
st273:
	if ( ++p == pe )
		goto _test_eof273;
case 273:
	switch( (*p) ) {
		case 58: goto st278;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st274;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st274;
	} else
		goto st274;
	goto st178;
st274:
	if ( ++p == pe )
		goto _test_eof274;
case 274:
	switch( (*p) ) {
		case 58: goto st278;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st275;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st275;
	} else
		goto st275;
	goto st178;
st275:
	if ( ++p == pe )
		goto _test_eof275;
case 275:
	switch( (*p) ) {
		case 58: goto st278;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st276;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st276;
	} else
		goto st276;
	goto st178;
st276:
	if ( ++p == pe )
		goto _test_eof276;
case 276:
	switch( (*p) ) {
		case 58: goto st278;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st277;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st277;
	} else
		goto st277;
	goto st178;
st277:
	if ( ++p == pe )
		goto _test_eof277;
case 277:
	switch( (*p) ) {
		case 58: goto st278;
		case 93: goto st283;
	}
	goto st178;
st278:
	if ( ++p == pe )
		goto _test_eof278;
case 278:
	if ( (*p) == 93 )
		goto st283;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st279;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st279;
	} else
		goto st279;
	goto st178;
st279:
	if ( ++p == pe )
		goto _test_eof279;
case 279:
	if ( (*p) == 93 )
		goto st283;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st280;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st280;
	} else
		goto st280;
	goto st178;
st280:
	if ( ++p == pe )
		goto _test_eof280;
case 280:
	if ( (*p) == 93 )
		goto st283;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st281;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st281;
	} else
		goto st281;
	goto st178;
st281:
	if ( ++p == pe )
		goto _test_eof281;
case 281:
	if ( (*p) == 93 )
		goto st283;
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st282;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st282;
	} else
		goto st282;
	goto st178;
st282:
	if ( ++p == pe )
		goto _test_eof282;
case 282:
	if ( (*p) == 93 )
		goto st283;
	goto st178;
st283:
	if ( ++p == pe )
		goto _test_eof283;
case 283:
	if ( (*p) == 58 )
		goto tr300;
	goto st178;
tr259:
#line 161 "src/port-uri.rl"
	{ service.start = p; }
#line 156 "src/port-uri.rl"
	{ dport.start   = p; port = 0; }
#line 157 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st284;
st284:
	if ( ++p == pe )
		goto _test_eof284;
case 284:
#line 4343 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto st248;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr301;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st245;
	} else
		goto st245;
	goto st178;
tr301:
#line 157 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st285;
st285:
	if ( ++p == pe )
		goto _test_eof285;
case 285:
#line 4365 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto st248;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr302;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st246;
	} else
		goto st246;
	goto st178;
tr302:
#line 157 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st286;
st286:
	if ( ++p == pe )
		goto _test_eof286;
case 286:
#line 4387 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto st248;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr303;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st247;
	} else
		goto st247;
	goto st178;
tr303:
#line 157 "src/port-uri.rl"
	{ port = port * 10 + (int)(*p - '0'); }
	goto st287;
st287:
	if ( ++p == pe )
		goto _test_eof287;
case 287:
#line 4409 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto st248;
		case 93: goto st283;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr193;
	goto st178;
tr261:
#line 161 "src/port-uri.rl"
	{ service.start = p; }
	goto st288;
st288:
	if ( ++p == pe )
		goto _test_eof288;
case 288:
#line 4425 "src/port-uri.cc"
	switch( (*p) ) {
		case 58: goto st248;
		case 93: goto st283;
	}
	if ( (*p) < 71 ) {
		if ( (*p) > 57 ) {
			if ( 65 <= (*p) && (*p) <= 70 )
				goto st289;
		} else if ( (*p) >= 48 )
			goto st245;
	} else if ( (*p) > 90 ) {
		if ( (*p) > 102 ) {
			if ( 103 <= (*p) && (*p) <= 122 )
				goto st181;
		} else if ( (*p) >= 97 )
			goto st289;
	} else
		goto st181;
	goto st178;
st289:
	if ( ++p == pe )
		goto _test_eof289;
case 289:
	switch( (*p) ) {
		case 58: goto st248;
		case 93: goto st283;
	}
	if ( (*p) < 71 ) {
		if ( (*p) > 57 ) {
			if ( 65 <= (*p) && (*p) <= 70 )
				goto st290;
		} else if ( (*p) >= 48 )
			goto st246;
	} else if ( (*p) > 90 ) {
		if ( (*p) > 102 ) {
			if ( 103 <= (*p) && (*p) <= 122 )
				goto st182;
		} else if ( (*p) >= 97 )
			goto st290;
	} else
		goto st182;
	goto st178;
st290:
	if ( ++p == pe )
		goto _test_eof290;
case 290:
	switch( (*p) ) {
		case 58: goto st248;
		case 93: goto st283;
	}
	if ( (*p) < 71 ) {
		if ( (*p) > 57 ) {
			if ( 65 <= (*p) && (*p) <= 70 )
				goto st291;
		} else if ( (*p) >= 48 )
			goto st247;
	} else if ( (*p) > 90 ) {
		if ( (*p) > 102 ) {
			if ( 103 <= (*p) && (*p) <= 122 )
				goto st183;
		} else if ( (*p) >= 97 )
			goto st291;
	} else
		goto st183;
	goto st178;
st291:
	if ( ++p == pe )
		goto _test_eof291;
case 291:
	switch( (*p) ) {
		case 58: goto st248;
		case 93: goto st283;
	}
	if ( (*p) > 90 ) {
		if ( 97 <= (*p) && (*p) <= 122 )
			goto st184;
	} else if ( (*p) >= 65 )
		goto st184;
	goto st178;
tr253:
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
	goto st292;
st292:
	if ( ++p == pe )
		goto _test_eof292;
case 292:
#line 4513 "src/port-uri.cc"
	switch( (*p) ) {
		case 48: goto st244;
		case 58: goto st293;
		case 93: goto st283;
	}
	if ( (*p) < 71 ) {
		if ( (*p) > 57 ) {
			if ( 65 <= (*p) && (*p) <= 70 )
				goto tr261;
		} else if ( (*p) >= 49 )
			goto tr259;
	} else if ( (*p) > 90 ) {
		if ( (*p) > 102 ) {
			if ( 103 <= (*p) && (*p) <= 122 )
				goto tr192;
		} else if ( (*p) >= 97 )
			goto tr261;
	} else
		goto tr192;
	goto st178;
st293:
	if ( ++p == pe )
		goto _test_eof293;
case 293:
	switch( (*p) ) {
		case 58: goto st253;
		case 70: goto st294;
		case 93: goto st283;
		case 102: goto st294;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st249;
	} else if ( (*p) > 69 ) {
		if ( 97 <= (*p) && (*p) <= 101 )
			goto st249;
	} else
		goto st249;
	goto st178;
st294:
	if ( ++p == pe )
		goto _test_eof294;
case 294:
	switch( (*p) ) {
		case 58: goto st253;
		case 70: goto st295;
		case 93: goto st283;
		case 102: goto st295;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st250;
	} else if ( (*p) > 69 ) {
		if ( 97 <= (*p) && (*p) <= 101 )
			goto st250;
	} else
		goto st250;
	goto st178;
st295:
	if ( ++p == pe )
		goto _test_eof295;
case 295:
	switch( (*p) ) {
		case 58: goto st253;
		case 70: goto st296;
		case 93: goto st283;
		case 102: goto st296;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st251;
	} else if ( (*p) > 69 ) {
		if ( 97 <= (*p) && (*p) <= 101 )
			goto st251;
	} else
		goto st251;
	goto st178;
st296:
	if ( ++p == pe )
		goto _test_eof296;
case 296:
	switch( (*p) ) {
		case 58: goto st253;
		case 70: goto st297;
		case 93: goto st283;
		case 102: goto st297;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st252;
	} else if ( (*p) > 69 ) {
		if ( 97 <= (*p) && (*p) <= 101 )
			goto st252;
	} else
		goto st252;
	goto st178;
st297:
	if ( ++p == pe )
		goto _test_eof297;
case 297:
	switch( (*p) ) {
		case 58: goto st298;
		case 93: goto st283;
	}
	goto st178;
st298:
	if ( ++p == pe )
		goto _test_eof298;
case 298:
	switch( (*p) ) {
		case 58: goto st258;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto tr313;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st254;
	} else
		goto st254;
	goto st178;
tr313:
#line 138 "src/port-uri.rl"
	{ ip4.start = p; }
	goto st299;
st299:
	if ( ++p == pe )
		goto _test_eof299;
case 299:
#line 4644 "src/port-uri.cc"
	switch( (*p) ) {
		case 46: goto st300;
		case 58: goto st258;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st313;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st255;
	} else
		goto st255;
	goto st178;
st300:
	if ( ++p == pe )
		goto _test_eof300;
case 300:
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st301;
	goto st178;
st301:
	if ( ++p == pe )
		goto _test_eof301;
case 301:
	if ( (*p) == 46 )
		goto st302;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st311;
	goto st178;
st302:
	if ( ++p == pe )
		goto _test_eof302;
case 302:
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st303;
	goto st178;
st303:
	if ( ++p == pe )
		goto _test_eof303;
case 303:
	if ( (*p) == 46 )
		goto st304;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st309;
	goto st178;
st304:
	if ( ++p == pe )
		goto _test_eof304;
case 304:
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st305;
	goto st178;
st305:
	if ( ++p == pe )
		goto _test_eof305;
case 305:
	if ( (*p) == 93 )
		goto tr324;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st306;
	goto st178;
st306:
	if ( ++p == pe )
		goto _test_eof306;
case 306:
	if ( (*p) == 93 )
		goto tr324;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st307;
	goto st178;
st307:
	if ( ++p == pe )
		goto _test_eof307;
case 307:
	if ( (*p) == 93 )
		goto tr324;
	goto st178;
tr324:
#line 139 "src/port-uri.rl"
	{ ip4.end   = p; }
	goto st308;
st308:
	if ( ++p == pe )
		goto _test_eof308;
case 308:
#line 4731 "src/port-uri.cc"
	if ( (*p) == 58 )
		goto tr189;
	goto st178;
st309:
	if ( ++p == pe )
		goto _test_eof309;
case 309:
	if ( (*p) == 46 )
		goto st304;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st310;
	goto st178;
st310:
	if ( ++p == pe )
		goto _test_eof310;
case 310:
	if ( (*p) == 46 )
		goto st304;
	goto st178;
st311:
	if ( ++p == pe )
		goto _test_eof311;
case 311:
	if ( (*p) == 46 )
		goto st302;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st312;
	goto st178;
st312:
	if ( ++p == pe )
		goto _test_eof312;
case 312:
	if ( (*p) == 46 )
		goto st302;
	goto st178;
st313:
	if ( ++p == pe )
		goto _test_eof313;
case 313:
	switch( (*p) ) {
		case 46: goto st300;
		case 58: goto st258;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st314;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st256;
	} else
		goto st256;
	goto st178;
st314:
	if ( ++p == pe )
		goto _test_eof314;
case 314:
	switch( (*p) ) {
		case 46: goto st300;
		case 58: goto st258;
		case 93: goto st283;
	}
	if ( (*p) < 65 ) {
		if ( 48 <= (*p) && (*p) <= 57 )
			goto st257;
	} else if ( (*p) > 70 ) {
		if ( 97 <= (*p) && (*p) <= 102 )
			goto st257;
	} else
		goto st257;
	goto st178;
	}
	_test_eof72: cs = 72; goto _test_eof; 
	_test_eof2: cs = 2; goto _test_eof; 
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
	_test_eof129: cs = 129; goto _test_eof; 
	_test_eof130: cs = 130; goto _test_eof; 
	_test_eof131: cs = 131; goto _test_eof; 
	_test_eof132: cs = 132; goto _test_eof; 
	_test_eof133: cs = 133; goto _test_eof; 
	_test_eof134: cs = 134; goto _test_eof; 
	_test_eof135: cs = 135; goto _test_eof; 
	_test_eof136: cs = 136; goto _test_eof; 
	_test_eof137: cs = 137; goto _test_eof; 
	_test_eof138: cs = 138; goto _test_eof; 
	_test_eof139: cs = 139; goto _test_eof; 
	_test_eof140: cs = 140; goto _test_eof; 
	_test_eof141: cs = 141; goto _test_eof; 
	_test_eof142: cs = 142; goto _test_eof; 
	_test_eof143: cs = 143; goto _test_eof; 
	_test_eof144: cs = 144; goto _test_eof; 
	_test_eof145: cs = 145; goto _test_eof; 
	_test_eof146: cs = 146; goto _test_eof; 
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
	_test_eof4: cs = 4; goto _test_eof; 
	_test_eof5: cs = 5; goto _test_eof; 
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
	_test_eof157: cs = 157; goto _test_eof; 
	_test_eof158: cs = 158; goto _test_eof; 
	_test_eof159: cs = 159; goto _test_eof; 
	_test_eof160: cs = 160; goto _test_eof; 
	_test_eof161: cs = 161; goto _test_eof; 
	_test_eof162: cs = 162; goto _test_eof; 
	_test_eof163: cs = 163; goto _test_eof; 
	_test_eof164: cs = 164; goto _test_eof; 
	_test_eof165: cs = 165; goto _test_eof; 
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
	_test_eof166: cs = 166; goto _test_eof; 
	_test_eof60: cs = 60; goto _test_eof; 
	_test_eof61: cs = 61; goto _test_eof; 
	_test_eof62: cs = 62; goto _test_eof; 
	_test_eof63: cs = 63; goto _test_eof; 
	_test_eof64: cs = 64; goto _test_eof; 
	_test_eof65: cs = 65; goto _test_eof; 
	_test_eof167: cs = 167; goto _test_eof; 
	_test_eof168: cs = 168; goto _test_eof; 
	_test_eof169: cs = 169; goto _test_eof; 
	_test_eof170: cs = 170; goto _test_eof; 
	_test_eof171: cs = 171; goto _test_eof; 
	_test_eof66: cs = 66; goto _test_eof; 
	_test_eof67: cs = 67; goto _test_eof; 
	_test_eof68: cs = 68; goto _test_eof; 
	_test_eof172: cs = 172; goto _test_eof; 
	_test_eof173: cs = 173; goto _test_eof; 
	_test_eof174: cs = 174; goto _test_eof; 
	_test_eof175: cs = 175; goto _test_eof; 
	_test_eof69: cs = 69; goto _test_eof; 
	_test_eof70: cs = 70; goto _test_eof; 
	_test_eof71: cs = 71; goto _test_eof; 
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
	_test_eof309: cs = 309; goto _test_eof; 
	_test_eof310: cs = 310; goto _test_eof; 
	_test_eof311: cs = 311; goto _test_eof; 
	_test_eof312: cs = 312; goto _test_eof; 
	_test_eof313: cs = 313; goto _test_eof; 
	_test_eof314: cs = 314; goto _test_eof; 

	_test_eof: {}
	if ( p == eof )
	{
	switch ( cs ) {
	case 72: 
	case 90: 
	case 111: 
	case 112: 
	case 113: 
	case 114: 
	case 115: 
	case 116: 
	case 120: 
	case 121: 
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
	case 152: 
	case 153: 
	case 154: 
	case 155: 
	case 156: 
	case 166: 
	case 171: 
	case 172: 
	case 173: 
	case 174: 
	case 175: 
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
	break;
	case 74: 
	case 75: 
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
	case 162: 
	case 163: 
	case 164: 
	case 165: 
#line 162 "src/port-uri.rl"
	{ service.end   = p; }
	break;
	case 92: 
	case 93: 
#line 172 "src/port-uri.rl"
	{ path.end   = p; }
	break;
	case 177: 
	case 178: 
	case 214: 
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
	case 260: 
	case 261: 
	case 262: 
	case 263: 
	case 264: 
	case 265: 
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
	case 276: 
	case 277: 
	case 278: 
	case 279: 
	case 280: 
	case 281: 
	case 282: 
	case 292: 
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
	case 309: 
	case 310: 
	case 311: 
	case 312: 
	case 313: 
	case 314: 
#line 176 "src/port-uri.rl"
	{ path.end   = p; }
	break;
	case 117: 
	case 118: 
	case 119: 
#line 139 "src/port-uri.rl"
	{ ip4.end   = p; }
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
	break;
	case 157: 
#line 148 "src/port-uri.rl"
	{ ip6.end   = p - 1; }
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
	break;
	case 136: 
	case 137: 
	case 138: 
	case 139: 
	case 140: 
	case 141: 
	case 142: 
	case 143: 
	case 144: 
	case 145: 
	case 146: 
	case 147: 
	case 148: 
	case 149: 
	case 150: 
	case 151: 
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
#line 162 "src/port-uri.rl"
	{ service.end   = p; }
	break;
	case 167: 
	case 168: 
	case 169: 
	case 170: 
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
#line 168 "src/port-uri.rl"
	{ sport.end     = p; }
	break;
	case 91: 
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
#line 172 "src/port-uri.rl"
	{ path.end   = p; }
	break;
	case 73: 
	case 158: 
	case 159: 
	case 160: 
	case 161: 
#line 158 "src/port-uri.rl"
	{ dport.end	 = p; }
#line 162 "src/port-uri.rl"
	{ service.end   = p; }
	break;
	case 95: 
	case 96: 
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
#line 162 "src/port-uri.rl"
	{ service.end   = p; }
#line 172 "src/port-uri.rl"
	{ path.end   = p; }
	break;
	case 176: 
	case 196: 
	case 197: 
	case 198: 
	case 199: 
	case 200: 
	case 201: 
	case 205: 
	case 206: 
	case 207: 
	case 208: 
	case 209: 
	case 210: 
	case 211: 
	case 212: 
	case 213: 
	case 215: 
	case 216: 
	case 217: 
	case 238: 
	case 239: 
	case 240: 
	case 241: 
	case 242: 
	case 308: 
#line 176 "src/port-uri.rl"
	{ path.end   = p; }
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
	break;
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
	case 193: 
	case 194: 
	case 195: 
	case 288: 
	case 289: 
	case 290: 
	case 291: 
#line 176 "src/port-uri.rl"
	{ path.end   = p; }
#line 162 "src/port-uri.rl"
	{ service.end   = p; }
	break;
	case 132: 
	case 133: 
	case 134: 
	case 135: 
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
#line 158 "src/port-uri.rl"
	{ dport.end	 = p; }
#line 162 "src/port-uri.rl"
	{ service.end   = p; }
	break;
	case 94: 
#line 158 "src/port-uri.rl"
	{ dport.end	 = p; }
#line 162 "src/port-uri.rl"
	{ service.end   = p; }
#line 172 "src/port-uri.rl"
	{ path.end   = p; }
	break;
	case 202: 
	case 203: 
	case 204: 
#line 176 "src/port-uri.rl"
	{ path.end   = p; }
#line 139 "src/port-uri.rl"
	{ ip4.end   = p; }
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
	break;
	case 283: 
#line 176 "src/port-uri.rl"
	{ path.end   = p; }
#line 148 "src/port-uri.rl"
	{ ip6.end   = p - 1; }
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
	break;
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
#line 176 "src/port-uri.rl"
	{ path.end   = p; }
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
#line 162 "src/port-uri.rl"
	{ service.end   = p; }
	break;
	case 179: 
	case 284: 
	case 285: 
	case 286: 
	case 287: 
#line 176 "src/port-uri.rl"
	{ path.end   = p; }
#line 158 "src/port-uri.rl"
	{ dport.end	 = p; }
#line 162 "src/port-uri.rl"
	{ service.end   = p; }
	break;
	case 218: 
	case 219: 
	case 220: 
	case 221: 
#line 176 "src/port-uri.rl"
	{ path.end   = p; }
#line 153 "src/port-uri.rl"
	{ host.end     = p; }
#line 158 "src/port-uri.rl"
	{ dport.end	 = p; }
#line 162 "src/port-uri.rl"
	{ service.end   = p; }
	break;
#line 5473 "src/port-uri.cc"
	}
	}

	_out: {}
	}

#line 195 "src/port-uri.rl"



	if (login.start && login.end && password.start && password.end) {
		strncpy(uri->login, login.start, login.end - login.start);
		strncpy(uri->password,
			password.start, password.end - password.start);
	}

	if (path.start && path.end) {
		struct sockaddr_un *un = (struct sockaddr_un *)&uri->addr;
		uri->addr_len = sizeof(*un);
		un->sun_family = AF_LOCAL;
		if (path.end - path.start >= sizeof(un->sun_path))
			return NULL;

		strncpy(un->sun_path, path.start, path.end - path.start);
		strcpy(uri->schema, "unix");
		return uri;
	}

	if (schema.start && schema.end) {
		strncpy(uri->schema, schema.start, schema.end - schema.start);
	} else {
		strcpy(uri->schema, "tcp");
	}


	/* only port was defined */
	if (sport.start && sport.end) {
		struct sockaddr_in *in = (struct sockaddr_in *)&uri->addr;
		uri->addr_len = sizeof(*in);

		in->sin_family = AF_INET;
		in->sin_port = htons(port);
		in->sin_addr.s_addr = INADDR_ANY;
		return uri;
	}
	

	if (!(dport.start && dport.end)) {
		port = 0;
		if (service.start && service.end) {
			if (service.end - service.start >= NI_MAXSERV)
				return NULL;
			char sname[NI_MAXSERV];
			strncpy(sname, service.start,
				service.end - service.start);
			struct servent *s = getservbyname(sname, NULL);
			if (!s)
				return NULL;
			port = ntohs( s->s_port );
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
		strncpy(sip4, ip4.start, ip4.end - ip4.start);
		if (inet_aton(sip4, &in->sin_addr))
			return uri;
		return NULL;
	}

	/* IPv6 uri */
	if (ip6.start && ip6.end) {
		struct sockaddr_in6 *in6 =
			(struct sockaddr_in6 *)&uri->addr;
		uri->addr_len = sizeof(*in6);


		char sip6[8 * 4 + 7 + 1];
		memset(sip6, 0, sizeof(sip6));
		strncpy(sip6, ip6.start, ip6.end - ip6.start);
		
		in6->sin6_family = AF_INET6;
		in6->sin6_port = htonl(port);
		
		if (inet_pton(AF_INET6, sip6, (void *)&in6->sin6_addr))
			return uri;
		
		return NULL;
	}


	if (!(host.start && host.end))
		return NULL;

	if (host.end - host.start >= NI_MAXHOST)
		return NULL;

	char shost[NI_MAXHOST];
	char sservice[NI_MAXSERV];
	strncpy(shost, host.start, host.end - host.start);
	if (service.end) {
		strncpy(sservice, service.start, service.end - service.start);
	} else {
		sservice[0] = 0;
	}

	struct addrinfo hints, *res;
	memset(&hints, 0, sizeof(hints));
	hints.ai_protocol = getprotobyname("tcp")->p_proto;

	if (getaddrinfo(shost, sservice, &hints, &res) != 0)
		return NULL;
	
	uri->addr_len = res->ai_addrlen;
	memcpy((void *)&uri->addr, (void *)res->ai_addr, res->ai_addrlen);
	freeaddrinfo(res);
	return uri;
}


void
port_uri_destroy(struct port_uri *uri)
{
	(void)uri;
}

/* vim: set ft=ragel: */
