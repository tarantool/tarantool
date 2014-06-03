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
#include "port-uri.h"
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
#include <say.h>

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
		return str;

	}
	case AF_UNIX:
	{
		struct sockaddr_un *un =
			(struct sockaddr_un *)&uri->addr;
		snprintf(str, sizeof(str), "unix://%.*s",
			 (int) sizeof(un->sun_path), un->sun_path);
		return str;
	}
	default:
		assert(false);
	}
}

struct port_uri *
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

	%%{
		machine port_uri;
		write data;

		hex1_4 = ([0-9a-fA-F]{1,4});


		schema		= (alpha+)
			>{ schema.start = p; }
		    %{ schema.end   = p; };

		login		= (alnum+)
			>{ login.start  = p; }
		    %{ login.end    = p; };

		password	= (alnum+)
			>{ password.start = p; }
		    %{ password.end   = p; };

		ip4	= ((digit{1,3}) (("." digit{1,3}){3}))
			>{ ip4.start = p; }
		    %{ ip4.end   = p; };

		ip4_6	= ("[::" [fF][fF][fF][fF] ":" ip4 "]");

		ip6	= ("["
			   (hex1_4?)
			   ((":" (hex1_4?)){1,8})
			   "]")
			>{ ip6.start = p + 1; }
		    %{ ip6.end   = p - 1; };

		host		= (ip4_6 | ip4 | ip6 | ([^:?]+))
			>{ host.start   = p; }
		    %{ host.end     = p; };

		dport		= ([1-9] (digit*))
			>{ dport.start   = p; port = 0; }
		    ${ port = port * 10 + (int)(*p - '0'); }
		    %{ dport.end	 = p; };

		service		= (dport | (alpha{1,16}))
			>{ service.start = p; }
		    %{ service.end   = p; };


		port		= ([1-9] digit*)
			>{ sport.start   = p; port = 0; }
		    ${ port = port * 10 + (int)(*p - '0'); }
		    %{ sport.end     = p; };

		abspath		= ("/" any+)
			>{ path.start = p; }
		    %{ path.end   = p; };

		file		= (any+)
			>{ path.start = p; }
		    %{ path.end   = p; };


		main := (
		    ("unix://"
		      ((login ":" password "@") ?) file) |

		     ((schema "://")?
			((login ":" password "@")?)
			host
			((":" service)?))		    |

		     port				    |
		     abspath
		);
		write init;
		write exec;
	}%%

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
			return NULL;

		snprintf(un->sun_path, sizeof(un->sun_path),
			 "%.*s", (int) (path.end - path.start), path.start);
		snprintf(uri->schema, sizeof(uri->schema), "unix");
		return uri;
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
		return uri;
	}


	if (!(dport.start && dport.end)) {
		port = 0;
		if (service.start && service.end) {
			if (service.end - service.start >= NI_MAXSERV)
				return NULL;
			char sname[NI_MAXSERV];
			snprintf(sname, sizeof(sname), "%.*s",
				 (int) (service.end - service.start),
                 service.start);
			struct servent *s = getservbyname(sname, NULL);
			if (!s)
				return NULL;
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
		snprintf(sip6, sizeof(sip6), "%.*s", (int) (ip6.end - ip6.start),
			 ip6.start);

		in6->sin6_family = AF_INET6;
		in6->sin6_port = htonl(port);

		if (inet_pton(AF_INET6, sip6, (void *)&in6->sin6_addr))
			return uri;

		return NULL;
	}


	if (!host.start || !host.end)
		return NULL;

	if (host.end - host.start >= NI_MAXHOST)
		return NULL;

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
		return NULL;

	uri->addr_len = res->ai_addrlen;
	memcpy((void *)&uri->addr, (void *)res->ai_addr, res->ai_addrlen);
	freeaddrinfo(res);
	return uri;
}

/* vim: set ft=ragel: */
