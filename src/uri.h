#ifndef TARANTOOL_URI_H_INCLUDED
#define TARANTOOL_URI_H_INCLUDED
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
#include <sys/types.h>		/* for netinet/ip.h on BSD */
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netdb.h>
#include <sys/un.h>

enum { URI_STR_LEN = 32 };

/** A parsed representation of an URI */
struct uri {

	union {
		struct sockaddr addr;
		struct sockaddr_in in;
		struct sockaddr_un un;
		struct sockaddr_in6 in6;
		struct sockaddr_storage addr_storage;
	};
	socklen_t addr_len;

	char schema[URI_STR_LEN];
	char login[URI_STR_LEN];
	char password[URI_STR_LEN];
};

/**
 * Parse a string and fill uri struct.
 * @retval 0 success
 * @retval -1 error
 */
int
uri_parse(struct uri *uri, const char *str);

/** Convert an uri to string */
const char *
uri_to_string(const struct uri * uri);


#endif /* TARANTOOL_URI_H_INCLUDED */
